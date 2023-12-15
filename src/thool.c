/******************************************************************************
 * A threadpool that uses a fixed-size circular-buffer to delegate and execute
 * tasks concurrently. The circular buffer is a blocking-queue which is provided
 * via the `taskqueue` datastructure. All tasks are managed by a single mutex
 * owned by the taskqueue.
 *
 * Worker threads are managed with a conditional-variable which is used to
 * suspend and notify threads of pending tasks. Threads are initialized *once*
 * at threadpool initialization and are tracked using the `thread_count_lock`
 * mutex. Threads are (attempted) shutdown gracefully when threadpool is freed.
 *
 * All structures have been packed to minimize slop.
 *****************************************************************************/

#include <err.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "thool.h"


typedef struct task {
    void *arg;
    void (*func)(void*);
} task;

typedef struct taskqueue {
    task* queue;                        // queue container for task objects
    uint16_t head;                      // starting queue index
    uint16_t tail;                      // ending queue index
    uint16_t size;                      // current size of queue
    uint16_t capacity;                  // maximum capacity of queue
    pthread_cond_t jobs_pending;        // notifies threads of pending jobs
    pthread_mutex_t taskqueue_lock;     // queue read-write lock
} taskqueue;

typedef struct thread {
    thread_count_t id;
    pthread_t pthread;
    struct threadpool* threadpool;
} thread;

typedef struct threadpool {
    int shutdown;                       // prevents threadpool from continuing
    int keepalive;                      // dictates whether threads can live
    thread *threads;                    // container for all *alive* threads
    taskqueue taskqueue;                // taskqueue manager
    thread_count_t threads_alive;       // total threads alive in pool
    thread_count_t threads_working;     // total active-working threads in pool
    pthread_mutex_t thread_count_lock;  // threadpool thread lock
} threadpool;


int thread_init(threadpool* tp, thread* t, thread_count_t id);
void* thread_work(thread* t);

int taskqueue_init(taskqueue* tq, uint16_t q_size);
int taskqueue_add(taskqueue* tq, void (*func)(void*), void* arg);
int taskqueue_destroy(taskqueue* tq);

unsigned int utils_get_num_procs();


/* ------------------------- THREADPOOL ----------------------------- */

struct threadpool* threadpool_init(uint32_t num_threads, uint32_t queue_size) {
    if (num_threads <= 0) {
        num_threads = utils_get_num_procs() + 1;
    }

    threadpool *tp;
    tp = (struct threadpool*)malloc(sizeof(struct threadpool));
    if (tp == NULL) {
        err(THREADPOOL_ALLOC_FAILURE, "Cannot allocate memory for thread pool");
        return NULL;
    }

    if (taskqueue_init(&tp->taskqueue, queue_size) != 0) {
        err(TASKQUEUE_ALLOC_FAILURE, "Failed to initialize task queue");
        return NULL;
    }

    // initialize tp primitives first as threads might require them immediately
    pthread_mutex_init(&(tp->thread_count_lock), NULL);

    tp->shutdown = 0;
    tp->keepalive = 1;
    tp->threads_alive = 0;
    tp->threads_working = 0;
    tp->threads = (struct thread*)calloc(num_threads, sizeof(struct thread));
    if (tp->threads == NULL) {
        err(THREADPOOL_THREAD_FAILURE, "Failed to initialize thread memory");
        return NULL;
    }
    for (int i=0; i < num_threads; i++) {
        thread_init(tp, &tp->threads[i], i);
    }

    return tp;
}

int threadpool_add_task(threadpool* tp, void (*func)(void*), void* arg) {
    if (tp->shutdown) { // reject new tasks if we're shutting down
        return THREADPOOL_SHUTDOWN;
    }
    return taskqueue_add(&tp->taskqueue, func, arg);
}

int threadpool_destroy(threadpool* tp, ThreadShutdown flag) {
    if (tp == NULL) {
        return 0;
    }


    tp->shutdown = 1;
    if (flag == GRACEFUL_SHUTDOWN) {
        while (tp->taskqueue.size > 0) {
            // complete remaining tasks (no more will be added)
            pthread_cond_broadcast(&(tp->taskqueue.jobs_pending));
            usleep(10000);
        }
    }

    // notify all threads to finish-up their current task
    tp->keepalive = 0;
    ThreadPoolError err = 0;
    if (pthread_cond_broadcast(&(tp->taskqueue.jobs_pending)) != 0) {
        err = TASKQUEUE_COND_FAILURE; // failed to wake up all working threads
    }
    sleep(1);

    // keep polling threads until they're done
    while (tp->threads_working){
        pthread_cond_broadcast(&(tp->taskqueue.jobs_pending));
        usleep(10000); // a bit hacky but give remaining threads time to finish
    }

    for (int i=0; i < tp->threads_alive; i++) {
        pthread_join(tp->threads[i].pthread, NULL);
    }

    if (err) {
        return err;
    }

    // all good to free resources
    taskqueue_destroy(&(tp->taskqueue));
    free(tp->threads);
    pthread_mutex_destroy(&(tp->thread_count_lock));
    free(tp);
    return 0;
};

thread_count_t threadpool_working_thread_count(threadpool* tp) {
    return tp->threads_working;
}


/* -------------------------- TASKQUEUE ----------------------------- */

int taskqueue_init(taskqueue* tq, uint16_t q_size) {
    tq->head = 0;
    tq->tail = 0;
    tq->size = 0;
    tq->capacity = q_size;
    tq->queue = (struct task*)calloc(q_size, sizeof(struct task));
    if (tq->queue == NULL) {
        err(TASKQUEUE_ALLOC_FAILURE, "Could not allocate memory for queue");
        return TASKQUEUE_ALLOC_FAILURE;
    }

    pthread_cond_init(&(tq->jobs_pending), NULL);
    pthread_mutex_init(&(tq->taskqueue_lock), NULL);
    return 0;
}

int taskqueue_destroy(taskqueue* tq) {
    if (pthread_mutex_lock(&(tq->taskqueue_lock)) != 0) {
        return TASKQUEUE_LOCK_FAILURE;
    }

    free(tq->queue);
    pthread_cond_destroy(&(tq->jobs_pending));
    pthread_mutex_destroy(&(tq->taskqueue_lock));
    return 0;
}

/**
 * @brief Adds task and associated argument to taskqueue
 *
 * Taskqueue implements a circular-buffer meaning it can and will block if
 * the queue is full. Additions are broadcast to sleeping threads on each call.
 *
 * @param   taskqueue*      active taskqueue
 * @param   func            pointer to function task
 * @param   arg             pointer to value which will be submitted to function
 * @return  0               failure: ThreadPoolError enum
 */
int taskqueue_add(taskqueue* tq, void (*func)(void*), void* arg) {
    if (pthread_mutex_lock(&(tq->taskqueue_lock)) != 0) {
        return TASKQUEUE_LOCK_FAILURE;
    }

    uint16_t i = (tq->tail + 1) % tq->capacity;
    if (i == tq->capacity) { // TODO: consider adding force flag
        pthread_mutex_unlock(&(tq->taskqueue_lock));
        return TASKQUEUE_FULL;
    }

    tq->queue[tq->tail].arg = arg;
    tq->queue[tq->tail].func = func;
    tq->tail = i;
    tq->size++;
    pthread_cond_signal(&(tq->jobs_pending));
    pthread_mutex_unlock(&(tq->taskqueue_lock));
    return 0;
}


/* --------------------------- THREAD ------------------------------- */

int thread_init(threadpool* tp, thread* t, thread_count_t id) {
    t->id = id;
    t->threadpool = tp;
    pthread_create(&(t->pthread), NULL, (void* (*)(void*))thread_work, t);
    pthread_detach(t->pthread);
    return 0;
}

/**
 * @brief Runs worker thread responsible for executing tasks
 *
 * Responsible for managing a worker thread runtime. Tasks are executed one at
 * a time. Thread is suspended when no work is pending using the `jobs_pending`
 * cond. var. Thread can be terminated by parent when `keepalive` is nullified.
 * Thread is active only while carrying out work, otherwise it is alive. Alive
 * and active are not mutually exclusive.
 *
 * @param   thread*         worker thread responsible for task execution
 * @return  void*
 */
void* thread_work(thread* t) {
    task task;
    threadpool *tp = t->threadpool;

    pthread_mutex_lock(&(tp->thread_count_lock));
    tp->threads_alive++;
    pthread_mutex_unlock(&(tp->thread_count_lock));

    // begin working loop
    while (tp->keepalive) {
        pthread_mutex_lock(&(tp->taskqueue.taskqueue_lock));
        while (tp->taskqueue.size == 0 && tp->keepalive) {
            pthread_cond_wait(&(tp->taskqueue.jobs_pending),
                              &(tp->taskqueue.taskqueue_lock));
        }

        if (!tp->keepalive) {
            pthread_mutex_unlock(&(tp->taskqueue.taskqueue_lock));
            break;
        }

        // TODO: check if this could end in deadlock
        pthread_mutex_lock(&(tp->thread_count_lock));
        tp->threads_working++;
        pthread_mutex_unlock(&(tp->thread_count_lock));

        // remove task
        task.arg = tp->taskqueue.queue[tp->taskqueue.head].arg;
        task.func = tp->taskqueue.queue[tp->taskqueue.head].func;
        tp->taskqueue.head = (tp->taskqueue.head + 1) % tp->taskqueue.capacity;
        tp->taskqueue.size--;
        pthread_mutex_unlock(&(tp->taskqueue.taskqueue_lock));

        // carry out task
        task.func(task.arg);

        pthread_mutex_lock(&(tp->thread_count_lock));
        tp->threads_working--;
        pthread_mutex_unlock(&(tp->thread_count_lock));
    }

    pthread_mutex_lock(&(tp->thread_count_lock));
    tp->threads_alive--;
    pthread_mutex_unlock(&(tp->thread_count_lock));
    pthread_exit(NULL);
}


/* ---------------------------- UTILS ------------------------------- */

#ifdef __unix__
unsigned int utils_get_num_procs()
{
    return (unsigned int)sysconf(_SC_NPROCESSORS_ONLN);
}
#else
unsigned int utils_get_num_procs()
{
    return 1;
}
#endif

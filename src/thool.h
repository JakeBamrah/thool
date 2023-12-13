#include <stdint.h>


typedef enum {
    THREADPOOL_ALLOC_FAILURE    = 1,
    THREADPOOL_THREAD_FAILURE   = 2,
    TASKQUEUE_ALLOC_FAILURE     = 3,
    TASKQUEUE_LOCK_FAILURE      = 4,
    TASKQUEUE_COND_FAILURE      = 5,
    TASKQUEUE_FULL              = 6
} ThreadPoolError;

typedef uint8_t thread_count_t;
typedef struct threadpool threadpool;

/**
 * @brief  Initializes threadpool
 *
 * @example
 *
 *      threadpool* thpool = threadpool_init(5, 20);
 *
 * @param   num_threads     number of worker threads threadpool maintains
 * @param   queue_size      maximum size of bounded-task-queue
 * @return  threadpool*     failure: NULL
 */
struct threadpool* threadpool_init(uint32_t num_threads, uint32_t queue_size);

/**
 * @brief Adds a single task onto the threadpool's task-queue
 *
 * Groups a function and a *single* argument into a task for the taskqueue. Both
 * function and arg must be cast appropriately using void (see example) to
 * avoid compiler warnings.
 *
 * NOTE: taskqueue can and will block if queue is full.
 *
 * @example
 *
 *      void print_num(int num){
 *          _sum = num + 4;
 *          printf("%d\n", _sum);
 *      }
 *
 *      threadpool_add_task(threadpool, (void*)print_num, (void*)num);
 *
 * @param   threadpool      executing threadpool for task
 * @param   func            pointer to function task
 * @param   arg             pointer to value which will be submitted to function
 * @return  0               failure: ThreadPoolError enum
 */
int threadpool_add_task(threadpool* tp, void (*func)(void*), void* arg);

/**
 * @brief Destroys threadpool
 *
 * Clean-up is graceful, i.e., threadpool will wait for working threads to
 * finish their current task before deallocating memory.
 *
 * @param   threadpool*     threadpool to destroy
 * @return  0               failure: ThreadPoolError enum
 */
int threadpool_destroy(threadpool* tp);

/**
 * @brief Get number of actively working threads
 *
 * @param   threadpool*     threadpool workers belong to
 * @return  uint8_t
 */
thread_count_t threadpool_working_thread_count(threadpool* tp);

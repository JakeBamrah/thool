#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#include "../src/thool.h"


#define THREADS 4
#define QUEUES  8
#define QUEUE   1024

int remaining;
int tasks[QUEUE];
threadpool* pool[QUEUES];
pthread_mutex_t lock;

void task(void* arg) {
    int* i = (int*)arg;
    *i += 1;
    if(*i < QUEUES) {
        assert(threadpool_add_task(pool[*i], task, arg) == 0);
    } else {
        pthread_mutex_lock(&lock);
        remaining--;
        pthread_mutex_unlock(&lock);
    }
}

int main(int argc, char** argv)
{
    pthread_mutex_init(&lock, NULL);
    for(int i = 0; i < QUEUES; i++) {
        pool[i] = threadpool_init(THREADS, QUEUE);
        assert(pool[i] != NULL);
    }
    usleep(10);

    remaining = QUEUE;
    for(int i = 0; i < QUEUE; i++) {
        tasks[i] = 0;
        assert(threadpool_add_task(pool[0], task, &(tasks[i])) == 0);
    }

    int copy = 1;
    while(copy > 0) {
        usleep(10);
        pthread_mutex_lock(&lock);
        copy = remaining;
        pthread_mutex_unlock(&lock);
    }

    for(int i = 0; i < QUEUES; i++) {
        assert(threadpool_destroy(pool[i], IMMEDIATE_SHUTDOWN) == 0);
    }

    pthread_mutex_destroy(&lock);
    return 0;
}

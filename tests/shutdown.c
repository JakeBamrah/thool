#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#include "../src/thool.h"


#define THREADS 4
#define QUEUE   1024

int remaining;
pthread_mutex_t lock;

void task(void* arg) {
    usleep(10);
    pthread_mutex_lock(&lock);
    remaining--;
    pthread_mutex_unlock(&lock);
}

int main(int argc, char** argv)
{
    // immediate shutdown
    remaining = QUEUE;
    pthread_mutex_init(&lock, NULL);
    threadpool* tp = threadpool_init(THREADS, QUEUE);
    for(int i = 0; i < QUEUE; i++) {
        assert(threadpool_add_task(tp, task, NULL) == 0);
    }

    assert(threadpool_destroy(tp, IMMEDIATE_SHUTDOWN) == 0);
    assert(remaining > 0);

    // graceful shutdown
    remaining = QUEUE;
    pthread_mutex_init(&lock, NULL);
    tp = threadpool_init(THREADS, QUEUE);
    for(int i = 0; i < QUEUE; i++) {
        assert(threadpool_add_task(tp, task, NULL) == 0);
    }

    assert(threadpool_destroy(tp, GRACEFUL_SHUTDOWN) == 0);
    assert(remaining == 0);

    pthread_mutex_destroy(&lock);
    return 0;
}

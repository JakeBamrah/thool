#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../src/thool.h"


void task(void* arg) {
    sleep(2);
    printf("tid:%p, val:%d\n", (void*)pthread_self(), *(int*)arg);
}

int main(int argc, char* argv[]) {
    int expected_threads = 4;
    int* vals = calloc(5, sizeof(*vals));
    threadpool* thpool = threadpool_init(10, 20);
    for (int i=0; i < expected_threads; i++) {
        vals[i] = i + 4;
        threadpool_add_task(thpool, task, vals + i);
    }
    sleep(1);

    int working_threads = threadpool_working_thread_count(thpool);
    assert(working_threads == expected_threads);
    return 0;
}

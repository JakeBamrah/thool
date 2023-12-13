## Thool
A simple and lightweight threadpool written in C.

### Usage
Define the number of threads and the size of the taskqueue:
```C
#include "thool.h"

threadpool* tp = threadpool_init(4, 1024);
```

Add work to the pool:
```C
void* sleep_test(void* num) {
    sleep(2);
    printf("tid=%p, val=%d\n", (void*)pthread_self(), *(int*)num);
}

int i = 12;
threadpool_add_task(tp, (void*)func, (void*)&i);
```

When finished, simply free the threadpool:
```C
threadpool_destroy(tp);
```

### Implementation
Thool is a very small and lightweight threadpool implementation that uses a
fixed-size circular-buffer to delegate and execute tasks concurrently. The
circular buffer is a blocking-queue which is provided via the `taskqueue`
datastructure. All tasks are managed by a single mutex owned by the
`taskqueue`.

Worker threads are managed with a conditional-variable which is used to
suspend and notify threads of pending tasks. Threads are initialized *once*
at threadpool initialization and are tracked using the `thread_count_lock`
mutex. Threads are (attempted) shutdown gracefully when threadpool is freed.

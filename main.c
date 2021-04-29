#include <stdio.h>
#include <pthread.h>
#include "ThreadPool.h"
#include <unistd.h>
#include <stdlib.h>

void taskFunc(void* arg)
{
    int num = *(int*)arg;
    printf("thread %ld is working, number = %d\n",
           pthread_self(), num);
    sleep(1);
}

void destoryPool(void* arg)
{
    ThreadPool* pool = (ThreadPool*)arg;
    sleep(3);
    threadPoolDestroy(pool);
}

int main()
{
    // 创建线程池
    ThreadPool* pool = threadPoolCreate(3, 10, 200);
    // 新增销毁线程，3s后自动销毁线程池
    pthread_t del_t;
    pthread_create(&del_t, NULL, destoryPool, pool);

    for (int i = 0; i < 200; ++i)
    {
        int* num = (int*)malloc(sizeof(int));
        *num = i + 100;
        threadPoolAddTask(pool, taskFunc, num);
    }

    sleep(15);

    return 0;
}

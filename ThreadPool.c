//
// Created by zhengqi on 2021/4/29.
//

#include "ThreadPool.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

ThreadPool* threadPoolCreate(int min, int max, int queueCapacity) {
    ThreadPool* pool = (ThreadPool*) malloc(sizeof(ThreadPool));
    do
    {
        if (pool == NULL)
        {
            printf("malloc threadpool fail...\n");
            break;
        }

        // 池本身属性初始化
        pool->workers = malloc(max * sizeof(pthread_t));
        if (pool->workers == NULL)
        {
            printf("malloc workers fail...\n");
            break;
        }
        memset(pool->workers, 0, max * sizeof(pthread_t));
        pool->minNum = min;
        pool->maxNum = max;
        pool->busyNum = 0;
        pool->liveNum = min;
        pool->deleteNum = 0;

        // 互斥锁等初始化

        if (pthread_mutex_init(&pool->mutexPool, NULL) != 0
            || pthread_mutex_init(&pool->mutexBusy, NULL) != 0
            || pthread_cond_init(&pool->notEmpty, NULL) != 0
            || pthread_cond_init(&pool->notFull, NULL) != 0)
        {
            printf("mutex or condition init fail...\n");
            break;
        }

        // 任务队列属性初始化
        pool->queueCapacity = queueCapacity;
        pool->taskQ = malloc(queueCapacity * sizeof(Task));
        if (pool->taskQ == NULL)
        {
            printf("malloc taskQueue fail...\n");
            break;
        }
        pool->queueSize = 0;
        pool->queueFront = 0;
        pool->queueRear = 0;
        pool->shutdown = 0;

        // 工作线程与管理线程初始化
        pthread_create(&pool->manager, NULL, manageProc, pool);
        for (int i = 0; i < min; i++)
        {
            pthread_create(&pool->workers[i], NULL, workProc, pool);
        }
        return pool;
    } while (0);

    // 初始化失败，初始化过程中成功申请的资源需要释放
    if (pool) {
        // 申请pool->workers成功，释放
        if (pool->workers) free(pool->workers);
        // 申请pool->taskQ成功，释放
        if (pool->taskQ) free(pool->taskQ);
        free(pool);
    }

    return NULL;
}

int threadPoolDestroy(ThreadPool* pool)
{
    printf("pool starts destroying.\n");
    if (pool == NULL) {
        return -1;
    }
    // 设置销毁flag
    pool->shutdown = 1;
    // 等待管理者销毁
    pthread_join(pool->manager, NULL);
    printf("manager destroyed.\n");
    // 唤醒阻塞worker线程，销毁
    // 正常运行的worker线程会在任务结束后自行销毁
    for (int i = 0; i < pool->liveNum; i++)
    {
        pthread_cond_signal(&pool->notEmpty); // 伪造信号，pool->shutdown被设为true，因此worker线程会自动销毁
    }
    printf("worker destroyed.\n");
    // 释放堆内存
    if (pool->taskQ)
    {
        free(pool->taskQ);
        pool->taskQ = NULL;
    }
    if (pool->workers)
    {
        free(pool->workers);
        pool->workers = NULL;
    }
    printf("malloc freed.\n");
    // 清理互斥锁与条件变量
    pthread_mutex_destroy(&pool->mutexPool);
    pthread_mutex_destroy(&pool->mutexBusy);
    pthread_cond_destroy(&pool->notEmpty);
    pthread_cond_destroy(&pool->notFull);
    printf("mutex cleaned.\n");
    free(pool);
    pool = NULL;
    printf("pool ends destroying.\n");
    return 0;
}

void threadPoolAddTask(ThreadPool* pool, void(*func)(void*), void* args)
{

    // 判断池是否已销毁
    if (pool == 0)
    {
        printf("pool is null.\n");
        return;
    }
    // 向队列中新增任务
    // 访问共享队列
    // 加锁
    pthread_mutex_lock(&pool->mutexPool);
    // 任务队列满了 生产者阻塞
    while (pool->queueSize == pool->queueCapacity && !pool->shutdown)
    {
        int busyNum = threadPoolBusyNum(pool);
        if (busyNum == pool->maxNum){
            // 拒绝继续添加任务
            pthread_mutex_unlock(&pool->mutexPool);
            return;
        }
        pthread_cond_wait(&pool->notFull, &pool->mutexPool);
    }
    // 被唤醒后发现池被销毁则取消添加
    if (pool->shutdown)
    {
        printf("pool is already destroyed.\n");
        pthread_mutex_unlock(&pool->mutexPool);
        return;
    }
    // 向任务队列中新增任务
    pool->taskQ[pool->queueRear].function = func; // 函数指针
    pool->taskQ[pool->queueRear].args = args; // 函数参数

    // 更新队列信息
    pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
    pool->queueSize += 1;

    // 唤醒阻塞的worker线程
    pthread_cond_signal(&pool->notEmpty);
    pthread_mutex_unlock(&pool->mutexPool);

}

int threadPoolBusyNum(ThreadPool *pool) {
    pthread_mutex_lock(&pool->mutexBusy);
    int busyNum = pool->busyNum;
    pthread_mutex_unlock(&pool->mutexBusy);
    return busyNum;
}

int threadPoolLiveNum(ThreadPool *pool) {
    // 共享变量加锁
    pthread_mutex_lock(&pool->mutexPool);
    int aliveNum = pool->liveNum;
    // 共享变量解锁
    pthread_mutex_unlock(&pool->mutexPool);
    return aliveNum;
}

/*
	线程退出，更新pool->workers[线程id] = 0，方便manage线程管理时新增线程
*/
void threadExit(ThreadPool* pool) {
    pthread_t tid = pthread_self();
    // 如果pool->shutdown，则不用重新设置pool->workers
    if (!pool->shutdown) {
        for (int i = 0; i < pool->maxNum; i++) {
            if (pool->workers[i] == tid) {
                pool->workers[i] = 0;
                break;
            }
        }
    }
    printf("Thread %ld has exited.\n", tid);
    pthread_exit(NULL);
}

/*
	每个worker线程遵循的工作流程
	参数：ThreadPool* 实例
*/
void* workProc(void* args) {
    ThreadPool* pool = (ThreadPool*)args;

    // 每个worker线程不断访问线程池中的任务队列获取任务并执行
    // worker线程不会终止，而是等待manager线程传来销毁信号外部终止
    while (1)
    {
        // 不断访问任务队列，因此线程池为共享实例
        // 需要互斥访问
        pthread_mutex_lock(&pool->mutexPool);
        // 任务队列为空，此worker线程阻塞
        while (pool->queueSize == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

            // 判断管理线程是不是要销毁该线程
            if (pool->deleteNum > 0) {
                pool->deleteNum -= 1; // 此处不用加锁，pthread_cond_wait确保线程抢到互斥锁
                if (pool->liveNum > pool->minNum) { // 活着的线程个数必须大于池规定的最小个数
                    pool->liveNum -= 1; // 线程自杀，池中活着的线程数-1
                    // 自杀前释放互斥锁，避免死锁
                    pthread_mutex_unlock(&pool->mutexPool);
                    threadExit(pool); // 线程自杀
                }
            }
        }

        // 线程池被关闭，worker线程释放互斥锁并终止
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->mutexPool);
            threadExit(pool);
        }

        // 消费任务
        Task task;
        task.function = pool->taskQ[pool->queueFront].function;
        task.args = pool->taskQ[pool->queueFront].args;

        // 更新任务队列信息
        pool->queueSize -= 1;
        pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;

        // 1. 修改线程池busyNum
        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum += 1;
        pthread_mutex_unlock(&pool->mutexBusy);

        // 解锁，并通知生产者队列有空位
        pthread_cond_signal(&pool->notFull);
        pthread_mutex_unlock(&pool->mutexPool);

        // 执行任务
        printf("Thread %ld starts working....\n", pthread_self());

        // 2. 执行获取的任务
        task.function(task.args);
        free(task.args); // 执行完任务后清理资源
        task.args = NULL;
        printf("Thread %ld ends working....\n", pthread_self());
        // 执行完任务发现池被销毁则终止线程
        if (pool->shutdown)
        {
            threadExit(pool);
        }
        // 3. 修改线程池busyNum
        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum -= 1;
        pthread_mutex_unlock(&pool->mutexBusy);
        // 任务执行完成，重新去队列中取任务执行
    }
    return NULL;
}

/*
	每个manage线程遵循的工作流程
*/
void* manageProc(void* args) {
    ThreadPool* pool = (ThreadPool*)args;
    while (!pool->shutdown)
    {
        // 每隔三秒对池中的线程进行一次管理
        sleep(3);

        // 准备工作
        // 获取线程池中存活的线程数与任务的数量
        pthread_mutex_lock(&pool->mutexPool);
        int taskNum = pool->queueSize;
        int aliveNum = pool->liveNum;
        pthread_mutex_unlock(&pool->mutexPool);
        // 获取线程池中正在工作的线程的数量
        int busyNum = threadPoolBusyNum(pool);

        // 判断是否需要往池中新增线程
        // 判断条件：任务的数量大于存活的线程数 && 存活的线程数 < 池限制的最大线程数
        if (taskNum > aliveNum && aliveNum < pool->maxNum) {
            int counter = 0; // 新增的线程个数
            pthread_mutex_lock(&pool->mutexPool);
            for (int i = 0; i < pool->maxNum && counter < THREADINCREMENT && pool->liveNum < pool->maxNum; i++)
            {
                if (pool->workers[i] == 0) { // 该位置可以用来新建线程
                    pthread_create(&pool->workers[i], NULL, workProc, pool);
                    pool->liveNum += 1; // 写共享变量，因此外层加了锁
                    counter += 1;
                }
            }
            pthread_mutex_unlock(&pool->mutexPool);
        }

        // 判断是否要删除池中空闲线程
        // 判断条件：忙的线程*2 < 存活的线程数 && 存活的线程>最小线程数
        if (busyNum * 2 < aliveNum && aliveNum > pool->minNum)
        {
            // 加锁，修改变量pool->deleteNum
            pthread_mutex_lock(&pool->mutexPool);
            pool->deleteNum = THREADINCREMENT; // 待删除数量为THREADINCREMENT
            pthread_mutex_unlock(&pool->mutexPool);

            for (int i = 0; i < THREADINCREMENT; i++) {
                // 由于在唤醒worker线程前设置了pool的deleteNum属性，因此唤醒后的空闲线程会自杀
                pthread_cond_signal(&pool->notEmpty); // 伪造信号，让唤醒的worker线程自杀
            }
        }

        // 管理结束，3s后继续对池进行管理
        // 如果外部调用线程池销毁函数，则跳出while，管理线程终止
    }
    return NULL;
}

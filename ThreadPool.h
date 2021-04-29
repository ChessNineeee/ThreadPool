//
// Created by zhengqi on 2021/4/29.
//

#ifndef THREADPOOL_THREADPOOL_H
#define THREADPOOL_THREADPOOL_H
#include <pthread.h>
#define THREADINCREMENT 2 // 每次增加减少的线程数

typedef struct Task {
    void (*function)(void* args); // 待执行函数
    void* args; // 待执行函数的参数
} Task;

typedef struct ThreadPool {
    Task* taskQ; // 任务队列
    int queueCapacity; // 队列容量
    int queueSize; // 队列中现有任务个数
    int queueFront; // 队列头
    int queueRear; // 队列尾


    pthread_t manager; // 线程池管理者线程
    pthread_t* workers; // 线程池中的工作线程
    int minNum; // 线程池中最小的线程数量
    int maxNum; // 线程池中最大的线程数量
    int busyNum; // 线程池中正在工作的线程数量
    int liveNum; // 线程池中存活的线程数量
    int deleteNum; // 线程池中待删除的线程数量

    pthread_mutex_t mutexPool; // 线程池互斥锁
    pthread_mutex_t mutexBusy; // busyNum互斥锁
    pthread_cond_t notFull; // 任务队列是否满
    pthread_cond_t notEmpty; // 任务队列是否为空

    int shutdown; // 是否要关闭线程池
} ThreadPool;

// 创建线程池并初始化
ThreadPool* threadPoolCreate(int min, int max, int queueCapacity);

// 销毁线程池
int threadPoolDestroy(ThreadPool *pool);

// 给线程池添加任务
void threadPoolAddTask(ThreadPool* pool, void(*func)(void*), void* args);

// 获取线程池中工作的线程的个数
int threadPoolBusyNum(ThreadPool* pool);

// 获取线程池中活着的线程的个数
int threadPoolLiveNum(ThreadPool* pool);

// 工作线程执行任务函数
void* workProc(void* args);

// 管理者线程任务函数
void* manageProc(void* args);

// 线程退出函数
void threadExit(ThreadPool* pool);
#endif //THREADPOOL_THREADPOOL_H

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;         // 线程池中的线程数
    int m_max_requests;          // 请求队列中允许的最大请求数
    pthread_t *m_threads;        // 描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue;  // 请求队列
    locker m_queuelocker;        // 保护请求队列的互斥锁
    sem m_queuestat;             // 是否有任务需要处理
    connection_pool *m_connPool; // 数据库
    int m_actor_model;           // 模型切换
};
template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    // 创建m_thread_number大小的线程池，m_threads为数组首地址，通过对其偏移访问不同线程
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    // 循环创建线程池
    for (int i = 0; i < thread_number; ++i)
    {
        // 创建线程并且检查是否出错
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        // 将线程和主线程分离，结束时直接自动释放所有资源
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

// 向请求队列中添加任务
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    // 请求锁
    m_queuelocker.lock();
    // 如果请求数量已经超过最大请求数量，释放锁并返回错误
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    // 加入请求队列
    m_workqueue.push_back(request);
    // 解锁
    m_queuelocker.unlock();
    // 信号量post，通知其他线程
    m_queuestat.post();
    return true;
}

// 请求队列添加请求的无状态版本
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

// 工作线程的工作
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}
template <typename T>
void threadpool<T>::run()
{
    // 死循环一直工作
    while (true)
    { // 等待条件变量
        m_queuestat.wait();
        // 上锁
        m_queuelocker.lock();
        // 如果请求队列无内容，则释放锁，并进入下一次循环
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        // 取出一个请求，并将队列中的任务弹出
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        // 取出请求后释放锁
        m_queuelocker.unlock();
        // 如果请求为空，continue
        if (!request)
            continue;
        if (1 == m_actor_model)
        {
            if (0 == request->m_state)
            {
                if (request->read_once())
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif

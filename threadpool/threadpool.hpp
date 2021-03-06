#ifndef THREADPOOL_HPP
#define THREADPOOL_HPP
/**
 * @file threadpool.hpp
 * @author lixiang (lix0419@outlook.com)
 * @brief 使用一个工作队列完全解除了主线程和工作线程的耦合关系：主线程往工作队列中插入任务，工作线程通过竞争来取得任务并执行它。
 * @date 2021-05-23
 * 
 */
#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.hpp"
#include "../connpool/conn_pool.h"

// 线程池类
template <typename T>
class threadpool
{
public:
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    static void *worker(void *arg); // 线程执行函数(静态)
    [[noreturn]] void run();

private:
    int m_thread_number;         // 线程池中的线程数
    pthread_t *m_threads;        // 线程池的数组，其大小为m_thread_number
    std::list<T *> m_work_queue; // 请求队列
    int m_max_requests;          // 请求队列中允许的最大请求数
    locker m_queue_lock;         // 请求队列的互斥锁
    sem m_queue_sem;             // 是否有任务需要处理
    connection_pool *m_connPool; // 数据库连接池
    int m_actor_model;           // 模型切换(Reactor/Proactor)
};

template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model), m_thread_number(thread_number),
                                                                                                             m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool)
{
    // 输入检查
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    // 创建线程池数组
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    // 创建thread_number个线程(根据硬件性能)
    for (int i = 0; i < thread_number; ++i)
    {
        // pthread_create函数原型中的第三个参数，为函数指针，指向处理线程函数的地址
        // 若weoker是成员函数，则this指针会作为默认的参数被传进函数中，从而和线程函数参数(void*)不能匹配
        // 静态函数worker没有this指针，不能调用成员函数，因此把this作为参数传给worker
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        // 设置为脱离线程，脱离后的线程在脱离后自动释放占用的资源
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

// reactor模式下的请求入队
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queue_lock.lock();
    if (m_work_queue.size() >= m_max_requests)
    {
        m_queue_lock.unlock();
        return false;
    }

    request->m_io_state = state; // reactor模式要标记IO事件类别，0为读
    m_work_queue.push_back(request);
    m_queue_lock.unlock();
    m_queue_sem.post();
    return true;
}

// proactor模式下的请求入队(默认)
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queue_lock.lock();
    if (m_work_queue.size() >= m_max_requests)
    {
        m_queue_lock.unlock();
        return false;
    }
    m_work_queue.push_back(request);
    m_queue_lock.unlock();
    // 信号量提醒有新的任务要处理
    m_queue_sem.post();
    return true;
}

// 工作线程运行的函数，它不断从工作队列中取出任务并执行之
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    //线程池中每一个线程创建时都会调用run()，睡眠在队列中
    pool->run();
    return pool;
}

// 工作线程从请求队列中取出某个任务进行处理
template <typename T>
[[noreturn]] void threadpool<T>::run()
{
    while (true)
    {
        // 线程建立之后，就等待信号量，当有连接进来之后，这些线程就会竞争处理连接
        m_queue_sem.wait();
        m_queue_lock.lock();
        if (m_work_queue.empty())
        {
            m_queue_lock.unlock();
            continue;
        }
        // 获取队列中的第一个任务
        T *request = m_work_queue.front();
        m_work_queue.pop_front();
        m_queue_lock.unlock();
        if (!request)
            continue;

        // Reactor模式子线程负责处理IO
        // 读事件先读取http::read()把数据读到缓存,再解析读进来的数据http::process();
        // 写事件调用http::write()发送数据
        if (1 == m_actor_model)
        {
            // Reactor模式读取IO请求
            if (0 == request->m_io_state)
            {
                if (request->read())
                {
                    request->improv = 1;
                    // 从连接池中获得一个连接
                    connectionRAII mysql_conn(&request->m_mysql, m_connPool);
                    request->process();
                }
                // TODO:这是干啥的
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            // Reactor模式写IO事件
            else
            {
                // 在子线程中执行write
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
        // Proactor模式，主线程已经做好了IO,因此这里只需要解析请求
        else
        {
            // 拿到一个连接
            connectionRAII mysql_conn(&request->m_mysql, m_connPool);
            request->process();
        }
    }
}
#endif

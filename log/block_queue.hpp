#ifndef BLOCK_QUEUE_HPP
#define BLOCK_QUEUE_HPP

#include <iostream>
#include <cstdlib>
#include <pthread.h>
#include <sys/time.h>
#include <cstring>
#include <ctime>
#include <cstdarg>
#include "../lock/locker.hpp"

using namespace std;

// 阻塞的循环队列类
// 封装了生产者-消费者模型，其中push成员是生产者，pop成员是消费者
template <class T>
class block_queue
{
public:
    block_queue(int max_size = 1000)
    {
        if (max_size <= 0)
        {
            exit(-1);
        }
        // 构造函数创建循环队列
        m_max_size = max_size;
        m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    void clear()
    {
        m_bq_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_bq_mutex.unlock();
    }

    ~block_queue()
    {
        m_bq_mutex.lock();
        if (m_array != NULL)
            delete[] m_array;

        m_bq_mutex.unlock();
    }

    // 判断队列是否满了
    bool full()
    {
        m_bq_mutex.lock();
        if (m_size >= m_max_size)
        {

            m_bq_mutex.unlock();
            return true;
        }
        m_bq_mutex.unlock();
        return false;
    }

    // 判断队列是否为空
    bool empty()
    {
        m_bq_mutex.lock();
        if (0 == m_size)
        {
            m_bq_mutex.unlock();
            return true;
        }
        m_bq_mutex.unlock();
        return false;
    }

    // 返回队首元素
    bool front(T &value)
    {
        m_bq_mutex.lock();
        if (0 == m_size)
        {
            m_bq_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_bq_mutex.unlock();
        return true;
    }

    // 返回队尾元素
    bool back(T &value)
    {
        m_bq_mutex.lock();
        if (0 == m_size)
        {
            m_bq_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_bq_mutex.unlock();
        return true;
    }

    int size()
    {
        int tmp = 0;

        m_bq_mutex.lock();
        tmp = m_size;

        m_bq_mutex.unlock();
        return tmp;
    }

    int max_size()
    {
        int tmp = 0;

        m_bq_mutex.lock();
        tmp = m_max_size;

        m_bq_mutex.unlock();
        return tmp;
    }

    // 往队列添加元素，需要将所有使用队列的线程先唤醒
    // 当有元素push进队列,相当于生产者生产了一个元素
    // 若当前没有线程等待条件变量,则唤醒无意义
    bool push(const T &item)
    {
        m_bq_mutex.lock();
        // 循环队列是满的
        if (m_size >= m_max_size)
        {
            m_bq_cond.broadcast();
            m_bq_mutex.unlock();
            return false;
        }
        // 取余运算就是实现循环队列的方法
        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;
        m_size++;
        m_bq_cond.broadcast();
        m_bq_mutex.unlock();
        return true;
    }

    // pop时,如果当前队列没有元素,将会等待条件变量
    bool pop(T &item)
    {
        m_bq_mutex.lock();
        while (m_size <= 0)
        {
            if (!m_bq_cond.wait(m_bq_mutex.get()))
            {
                m_bq_mutex.unlock();
                return false;
            }
        }
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_bq_mutex.unlock();
        return true;
    }

    // 增加了超时处理
    bool pop(T &item, int ms_timeout)
    {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_bq_mutex.lock();
        if (m_size <= 0)
        {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_bq_cond.timewait(m_bq_mutex.get(), t))
            {
                m_bq_mutex.unlock();
                return false;
            }
        }
        if (m_size <= 0)
        {
            m_bq_mutex.unlock();
            return false;
        }
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_bq_mutex.unlock();
        return true;
    }

private:
    locker m_bq_mutex;
    cond m_bq_cond;

    T *m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};

#endif

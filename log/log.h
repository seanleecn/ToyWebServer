#ifndef LOG_H
#define LOG_H

#include <cstdio>
#include <iostream>
#include <string>
#include <cstdarg>
#include <pthread.h>
#include "block_queue.hpp"

// 单例模式
class Log
{
public:
    // C++11以后,使用局部变量懒汉不用加锁
    static Log *get_instance()
    {
        static Log instance;
        return &instance;
    }

    static void *flush_log_thread(void *args)
    {
        Log::get_instance()->async_write_log();
    }

    // 参数:日志文件、日志缓冲区大小、最大行数、最长日志条队列
    bool init(const char *file_name, int log_buf_size, int split_lines, int max_queue_size);

    void write_log(int level, const char *format, ...);

    void flush(void);

private:
    Log();

    virtual ~Log();

    void *async_write_log()
    {
        string single_log;
        // 从阻塞队列中取出一个日志string，写入文件
        while (m_log_queue->pop(single_log))
        {
            m_log_mutex.lock();
            fputs(single_log.c_str(), m_log_fp);
            m_log_mutex.unlock();
        }
    }

private:
    char m_log_path[128];               // 路径名
    char m_log_name[128];               // log文件名
    int m_log_split_lines;                // 日志最大行数
    int m_log_buf_size;               // 日志缓冲区大小
    long long m_log_line_count;                // 日志行数记录
    int m_log_today;                      // 因为按天分类,记录当前时间是那一天
    FILE *m_log_fp;                       // 打开log的文件指针
    char *m_log_out_buf;                      // 要输出的内容
    block_queue<string> *m_log_queue; // 阻塞队列
    bool m_log_is_async;                  // 是否同步标志位
    locker m_log_mutex;                   // 同步类
//    int m_close_log{};                // 关闭日志
};

// 这四个宏定义在其他文件中使用，主要用于不同类型的日志输出
// __VA_ARGS__是一个可变参数的宏，定义时宏定义中参数列表的最后一个参数为省略号
// __VA_ARGS__宏前面加上##的作用在于，当可变参数的个数为0时，这里printf参数列表中的的##会把前面多余的","去掉
#define LOG_DEBUG(format, ...)                                    \
    if (0 == m_close_log)                                         \
    {                                                             \
        Log::get_instance()->write_log(0, format, ##__VA_ARGS__); \
        Log::get_instance()->flush();                             \
    }
#define LOG_INFO(format, ...)                                     \
    if (0 == m_close_log)                                         \
    {                                                             \
        Log::get_instance()->write_log(1, format, ##__VA_ARGS__); \
        Log::get_instance()->flush();                             \
    }
#define LOG_WARN(format, ...)                                     \
    if (0 == m_close_log)                                         \
    {                                                             \
        Log::get_instance()->write_log(2, format, ##__VA_ARGS__); \
        Log::get_instance()->flush();                             \
    }
#define LOG_ERROR(format, ...)                                    \
    if (0 == m_close_log)                                         \
    {                                                             \
        Log::get_instance()->write_log(3, format, ##__VA_ARGS__); \
        Log::get_instance()->flush();                             \
    }

#endif

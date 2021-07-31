#include "log.h"
using namespace std;

Log::Log()
{
    m_log_line_count = 0;
    m_log_is_async = false;
}

Log::~Log()
{
    if (m_log_fp != nullptr)
    {
        fclose(m_log_fp);
    }
}

/**
 * @param file_name 日志文件
 * @param close_log
 * @param log_buf_size 日志缓冲区大小
 * @param split_lines 最大行数
 * @param max_queue_size 最长日志条队列,异步需要设置阻塞队列的长度，同步不需要设置
 */
bool Log::init(const char *file_name, int log_buf_size, int split_lines, int max_queue_size)
{
    // 如果设置了max_queue_size,则设置为异步
    if (max_queue_size >= 1)
    {
        m_log_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        // flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, nullptr, flush_log_thread, nullptr);
    }

    // m_close_log = close_log;
    // 输出内容的长度
    m_log_buf_size = log_buf_size;
    m_log_out_buf = new char[m_log_buf_size];
    memset(m_log_out_buf, '\0', m_log_buf_size);
    // 最大行数
    m_log_split_lines = split_lines;

    time_t t = time(nullptr);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    // 从后向前找到第一个/
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    // 自定义日志名
    if (p == nullptr)
    {
        snprintf(log_full_name, 300, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
        strcpy(m_log_name, p + 1);
        strncpy(m_log_path, file_name, p - file_name + 1);
        snprintf(log_full_name, 300, "%s%d_%02d_%02d_%s", m_log_path, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, m_log_name);
    }

    m_log_today = my_tm.tm_mday;

    m_log_fp = fopen(log_full_name, "a");
    if (m_log_fp == nullptr)
    {
        return false;
    }

    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};

    // 日志分级
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    // 写入一个log，更新行数
    m_log_mutex.lock();
    m_log_line_count++;

    // 如果不是今天或者行数超过最大行数
    if (m_log_today != my_tm.tm_mday || m_log_line_count % m_log_split_lines == 0) //everyday log
    {
        char new_log[256] = {0};
        fflush(m_log_fp);
        fclose(m_log_fp);
        char tail[16] = {0};
        // 格式化日志名字中间的名字部分
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        // 如果不是今天就创建今天的日志，并更新m_log_today和m_count
        if (m_log_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", m_log_path, tail, m_log_name);
            m_log_today = my_tm.tm_mday;
            m_log_line_count = 0;
        }
        else
        {
            // 超过了最大行就在之前的日志名之前加上后缀
            snprintf(new_log, 300, "%s%s%s.%lld", m_log_path, tail, m_log_name, m_log_line_count / m_log_split_lines);
        }
        m_log_fp = fopen(new_log, "a");
    }

    m_log_mutex.unlock();
    // 将传入的format参数赋值给valst，便于格式化输出
    va_list valst;
    va_start(valst, format);
    string log_str;
    m_log_mutex.lock();

    // 写入内容格式：时间 + 内容
    // 时间格式化，snprintf成功返回写字符的总数，其中不包括结尾的null字符
    int n = snprintf(m_log_out_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);

    int m = vsnprintf(m_log_out_buf + n, m_log_buf_size - 1, format, valst);
    m_log_out_buf[n + m] = '\n';
    m_log_out_buf[n + m + 1] = '\0';
    log_str = m_log_out_buf;

    m_log_mutex.unlock();
    // 若m_is_async为true表示异步，默认为同步
    // 若异步,则将日志信息加入阻塞队列,
    if (m_log_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    // 同步则加锁向文件中写
    else
    {
        m_log_mutex.lock();
        fputs(log_str.c_str(), m_log_fp);
        m_log_mutex.unlock();
    }
    va_end(valst);
}

// fflush()会强迫将缓冲区内的数据写回参数stream 指定的文件中
// 在prinf()后加上fflush()，可以避免，下一个printf就把另一个数据加入输出缓冲区，结果冲掉了原来的数据
void Log::flush()
{
    m_log_mutex.lock();
    // 强制刷新写入流缓冲区
    fflush(m_log_fp);
    m_log_mutex.unlock();
}

#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <unistd.h>
#include <stdlib.h>

class Config
{
public:
    Config()
    {
        // 端口号,默认9006
        m_port = 9006;

        // 触发组合模式,默认listenfd LT + connfd LT
        m_trigger_mode = 0;

        // 优雅关闭链接，默认不使用
        m_linger = 0;

        // 数据库连接池数量,默认8
        m_sql_num = 8;

        // 线程池内的线程数量,默认8
        m_thread_num = 8;

        // 关闭日志,默认不关闭
        m_close_log = 0;

        // 日志写入方式，默认同步
        m_log_mode = 0;

        // 并发模型,默认是proactor
        m_actor_mode = 0;
    }

    ~Config(){};

    // 解析参数
    void parse_arg(int argc, char *argv[])
    {
        int opt;
        const char *str = "p:l:m:o:s:t:c:a:";
        while ((opt = getopt(argc, argv, str)) != -1)
        {
            switch (opt)
            {
            case 'p':
            {
                m_port = atoi(optarg);
                break;
            }
            case 'l':
            {
                m_log_mode = atoi(optarg);
                break;
            }
            case 'm':
            {
                m_trigger_mode = atoi(optarg);
                break;
            }
            case 'o':
            {
                m_linger = atoi(optarg);
                break;
            }
            case 's':
            {
                m_sql_num = atoi(optarg);
                break;
            }
            case 't':
            {
                m_thread_num = atoi(optarg);
                break;
            }
            case 'c':
            {
                m_close_log = atoi(optarg);
                break;
            }
            case 'a':
            {
                m_actor_mode = atoi(optarg);
                break;
            }
            default:
                break;
            }
        }
    }

public:
    // 端口号
    int m_port;

    // 日志写入方式
    int m_log_mode;

    // 触发组合模式
    int m_trigger_mode;

    // listenfd触发模式
    // int m_lisTriggerMode;

    // connfd触发模式
    // int m_conTriggerMode;

    // 优雅关闭链接
    int m_linger;

    // 数据库连接池数量
    int m_sql_num;

    // 线程池内的线程数量
    int m_thread_num;

    // 关闭日志
    int m_close_log;

    // 并发模型选择
    int m_actor_mode;
};

#endif
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

        // 日志写入方式，默认同步
        m_logWrite = 0;

        // 触发组合模式,默认listenfd LT + connfd LT
        m_triggerMode = 0;

        // listenfd触发模式，默认LT
        m_lisTriggerMode = 0;

        // connfd触发模式，默认LT
        m_conTriggerMode = 0;

        // 优雅关闭链接，默认不使用
        m_linger = 0;

        // 数据库连接池数量,默认8
        m_sqlNum = 8;

        // 线程池内的线程数量,默认8
        m_threadNum = 8;

        // 关闭日志,默认关闭
        m_closeLog = 1;

        // 并发模型,默认是proactor
        m_actorMode = 0;
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
                m_logWrite = atoi(optarg);
                break;
            }
            case 'm':
            {
                m_triggerMode = atoi(optarg);
                break;
            }
            case 'o':
            {
                m_linger = atoi(optarg);
                break;
            }
            case 's':
            {
                m_sqlNum = atoi(optarg);
                break;
            }
            case 't':
            {
                m_threadNum = atoi(optarg);
                break;
            }
            case 'c':
            {
                m_closeLog = atoi(optarg);
                break;
            }
            case 'a':
            {
                m_actorMode = atoi(optarg);
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
    int m_logWrite;

    // 触发组合模式
    int m_triggerMode;

    // listenfd触发模式
    int m_lisTriggerMode;

    // connfd触发模式
    int m_conTriggerMode;

    // 优雅关闭链接
    int m_linger;

    // 数据库连接池数量
    int m_sqlNum;

    // 线程池内的线程数量
    int m_threadNum;

    // 关闭日志
    int m_closeLog;

    // 并发模型选择
    int m_actorMode;
};

#endif
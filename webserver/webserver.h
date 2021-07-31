#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdio>
#include <unistd.h>
#include <cerrno>
#include <fcntl.h>
#include <cstdlib>
#include <cassert>
#include <sys/epoll.h>

#include "../threadpool/threadpool.hpp"
#include "../connpool/conn_pool.h"
#include "../http/http_conn.h"
#include "../config/config.hpp"
#include "../timer/timer.h"

const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数
const int TIMESLOT = 5;             // 最小超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    // 初始化用户名、数据库等信息
    void init(const string &user, const string &passWord, const string &DBName, const string &rootPath, const Config &config);
    // 创建线程池
    void thread_pool();
    // 单例模式初始化数据库连接池
    void sql_pool();
    // 单例模式获取一个日志的实例
    void log_write();
    // 设置监听socket，epoll和定时器
    void eventListen();
    // 事件回环(即服务器主线程)
    void eventLoop();
    // 初始化定时器
    void init_timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(timer_node *timer);

    // 事件回环的五个逻辑
    void deal_timer(timer_node *timer, int sockfd);
    bool deal_client();
    bool deal_signal(bool &timeout, bool &stop_server);
    void deal_read(int sockfd);
    void deal_write(int sockfd);

public:
    int m_port;   // 端口号,默认9006
    char *m_root; // root文件夹路径

    // 日志相关
    int m_log_mode; // 日志写入方式，默认同步
    int m_close_log; // 关闭日志,默认不关闭

    // 并发模型
    int m_actormodel; // 并发模型,默认是proactor
    int m_pipefd[2];  // 信号传输管道
    int m_epollfd;     // epoll文件描述符
    http_conn *m_http_conns; // 保存全部连接
    epoll_event m_events[MAX_EVENT_NUMBER];  // epoll事件数组

    // 数据库相关
    connection_pool *m_sql_pool; // 数据库实例
    string m_DB_user;               // 登陆数据库用户名
    string m_DB_password;           // 登陆数据库密码
    string m_DB_name;       // 使用数据库名
    int m_sql_num;               // 数据库连接池数量,默认8

    // 线程池相关
    threadpool<http_conn> *m_thread_pool; // 线程池实例
    int m_thread_num;              // 线程池内的线程数量,默认8

    // 文件描述符性质文件描述符的
    int m_listenfd;
    int m_linger; // 优雅关闭链接，默认不使用
    int m_trigger_mode;       // 触发组合模式,默认listenfd LT + connfd LT
    int m_listen_trigger_mode; // listenfd触发模式，默认LT
    int m_conn_trigger_mode;   // connfd触发模式，默认LT

    client_data *m_client_datas; // 保存全部客户端数据

    Utils m_utils; // 工具类
};
#endif

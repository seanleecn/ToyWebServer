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
#include "../http/http_conn.h"

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;             //最小超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int port, string user, string passWord, string databaseName,
              int log_write, int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);

    void thread_pool();
    void sql_pool();
    void log_write() const;
    void trig_mode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    bool dealclinetdata();
    bool dealwithsignal(bool &timeout, bool &stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);

public:
    int m_port; // 端口号,默认9006
    char *m_root; // root文件夹路径

    // 日志相关
    int m_log_write; // 日志写入方式，默认同步
    int m_close_log; // 关闭日志,默认不关闭

    int m_actormodel; // 并发模型,默认是proactor
    int m_pipefd[2];
    int m_epollfd;
    http_conn *users; // 保存全部连接

    //数据库相关
    connection_pool *m_connPool; // 数据库实例
    string m_user;               // 登陆数据库用户名
    string m_passWord;           // 登陆数据库密码
    string m_databaseName;       // 使用数据库名
    int m_sql_num;               // 数据库连接池数量,默认8

    //线程池相关
    threadpool<http_conn> *m_pool; // 线程池实例
    int m_thread_num;              // 线程池内的线程数量,默认8

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    int m_OPT_LINGER;     // 优雅关闭链接，默认不使用
    
    // 触发模式
    int m_TRIGMode;       // 触发组合模式,默认listenfd LT + connfd LT
    int m_LISTENTrigmode; // listenfd触发模式，默认LT
    int m_CONNTrigmode;   // connfd触发模式，默认LT

    //定时器相关
    client_data *users_timer; // 定时器
    Utils utils;              // 定时器信号处理函数
};
#endif

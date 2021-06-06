#ifndef TIMER_H
#define TIMER_H

#include <unistd.h>
#include <csignal>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <sys/stat.h>
#include <cstring>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <cstdarg>
#include <cerrno>
#include <sys/wait.h>
#include <sys/uio.h>
#include <ctime>

#include "../http/http_conn.h"
#include "../log/log.h"

// 连接资源结构体成员需要用到定时器类
// 需要前向声明
class timer_node;

// 客户端数据
struct client_data
{
    sockaddr_in address; // 客户端socket地址
    int sockfd;          // socket文件描述符
    timer_node *timer;   // 定时器
};

// 将连接资源、定时事件和超时时间封装为类，并以双向链表的形式组织起来
class timer_node
{
public:
    timer_node() : prev(nullptr), next(nullptr) {}

public:
    time_t expire;                  // 超时时间，绝对时间
    void (*cb_func)(client_data *); // 回调函数:从内核事件表删除事件，关闭文件描述符，释放连接资源
    client_data *user_data;         // 连接资源
    timer_node *prev;               // 前向指针
    timer_node *next;               // 后继指针
};

// 定时器链表类
// 为每个连接创建一个定时器，将其添加到链表中，并按照超时时间升序排列。执行定时任务时，将到期的定时器从链表中删除
// 添加定时器的事件复杂度是O(n),删除定时器的事件复杂度是O(1)
class timer_list
{
public:
    timer_list() : head(nullptr), tail(nullptr) {}
    ~timer_list();

    // 添加定时器，内部调用私有成员add_timer
    void add_timer(timer_node *timer);

    // 调整定时器，任务发生变化时，调整定时器在链表中的位置
    void adjust_timer(timer_node *timer);

    // 删除定时器
    void del_timer(timer_node *timer);

    // SIGALARM信号每次被触发就在信号处理函数中执行一次tick()
    // 该函数清理链表上的到期的节点
    void tick();

private:
    // 被add_timer调用，把一个节点按照超市顺序插入
    void add_timer(timer_node *timer, timer_node *lst_head);

    //头尾结点
    timer_node *head;
    timer_node *tail;
};

// 通用的工具类
class Utils
{
public:
    Utils() = default;
    ~Utils() = default;

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    static int u_epollfd;
    timer_list m_timer_lst;
    int m_TIMESLOT;
};

// 定时器回调函数
void cb_func(client_data *user_data);

#endif

#include "timer.h"

timer_list::~timer_list()
{
    timer_node *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

// 添加定时器
void timer_list::add_timer(timer_node *timer)
{
    if (!timer)
    {
        return;
    }
    // 如果当前链表为空
    if (!head)
    {
        head = tail = timer;
        return;
    }
    // 如果新的定时器超时时间小于当前头部结点，直接将新的定时器结点作为头部结点
    if (timer->expire < head->expire)
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    // 否则调用私有成员函数add_timer查找合适的位置保证链表按照超时时间(expire)升序
    add_timer(timer, head);
}

// 调整定时器，任务发生变化时，调整定时器在链表中的位置
void timer_list::adjust_timer(timer_node *timer)
{
    if (!timer)
    {
        return;
    }

    // 被调整的定时器在链表尾部 or 定时器超时值仍然小于下一个定时器超时值，不调整
    if (!timer->next || (timer->expire < timer->next->expire))
    {
        return;
    }

    // 被调整定时器是链表头结点，将定时器取出，调用私有成员函数add_timer重新插入
    if (timer == head)
    {
        head = head->next;
        head->prev = nullptr;
        timer->next = nullptr;
        add_timer(timer, head);
    }
    // 被调整定时器在内部，将定时器取出，调用私有成员函数add_timer重新插入
    else
    {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

// 删除定时器:即是双向链表节点的删除
void timer_list::del_timer(timer_node *timer)
{
    if (!timer)
    {
        return;
    }
    //链表中只有一个定时器，需要删除该定时器
    if ((timer == head) && (timer == tail))
    {
        delete timer;
        head = nullptr;
        tail = nullptr;
        return;
    }

    //被删除的定时器为头结点
    if (timer == head)
    {
        head = head->next;
        head->prev = nullptr;
        delete timer;
        return;
    }

    //被删除的定时器为尾结点
    if (timer == tail)
    {
        tail = tail->prev;
        tail->next = nullptr;
        delete timer;
        return;
    }

    //被删除的定时器在链表内部，常规链表结点删除
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

// 清理到期的链表节点，调用了cb_func()
void timer_list::tick()
{
    if (!head)
    {
        return;
    }

    // 获取当前时间
    time_t cur = time(nullptr);

    // 遍历定时器链表
    timer_node *tmp = head;
    while (tmp)
    {
        // 链表容器为升序排列
        // 当前时间小于头部定时器的超时时间，后面的定时器也没有到期，直接跳出
        if (cur < tmp->expire)
        {
            break;
        }

        // 当前定时器到期，则调用回调函数，执行定时事件
        tmp->cb_func(tmp->user_data);

        // 将处理后的定时器从链表容器中删除，并重置头结点
        head = tmp->next;
        if (head)
        {
            head->prev = nullptr;
        }
        delete tmp;
        tmp = head;
    }
}

// 找到应该插入的地方
// TODO:使用C++11的优先队列实现定时器
void timer_list::add_timer(timer_node *timer, timer_node *lst_head)
{
    timer_node *prev = lst_head;
    timer_node *tmp = prev->next;
    // 遍历链表，找到合适的位置
    while (tmp)
    {
        if (timer->expire < tmp->expire)
        {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }

    //遍历完发现，目标定时器需要放到尾结点处
    if (!tmp)
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = nullptr;
        tail = timer;
    }
}

void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

// 对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event{};
    event.data.fd = fd;
    // 配置监听的事件
    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;
    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 从内核时间表删除描述符
void Utils::removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT
void Utils::modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event{};
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 信号处理函数
// 把当前信号值写到管道[1]端
void Utils::sig_handler(int sig)
{
    // 为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    // 将信号值从管道写端写入，传输字符类型，而非整型
    send(u_pipefd[1], (char *)&msg, 1, 0);
    // 恢复原来的errno
    errno = save_errno;
}

// 设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick();
    alarm(m_TIMESLOT); // 重新触发SIGALRM信号
}

// 向连接客户发送错误报告
void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

// static变量类内定义类外初始化
int *Utils::u_pipefd = nullptr;
int Utils::u_epollfd = 0;

class Utils;

// 定时器回调函数:从内核事件表删除事件，关闭文件描述符，释放连接资源
void cb_func(client_data *user_data)
{
    // 删除非活动连接在socket上的注册事件
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->client_sockfd, nullptr);
    assert(user_data);
    // 删除非活动连接在socket上的注册事件
    close(user_data->client_sockfd);
    // 减少连接数
    http_conn::m_user_count--;
}

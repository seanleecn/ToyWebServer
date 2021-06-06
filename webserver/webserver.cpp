#include "webserver.h"

// 主要完成服务器初始化：http连接、根目录、定时器
WebServer::WebServer()
{
    // 保存全部的客户端信息
    users = new http_conn[MAX_FD];

    // root文件夹路径
    char server_path[100] = "../root";
    m_root = (char *)malloc(strlen(server_path) + 1);
    strcpy(m_root, server_path);

    // 定时器
    users_timer = new client_data[MAX_FD];
}

// 服务器资源释放
WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

// 初始化用户名、数据库等信息
void WebServer::init(string user, string passWord, string databaseName, // 数据库参数
                     Config config)                                     // 解析的参数
{
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_port = config.m_port;
    m_sql_num = config.m_sqlNum;
    m_thread_num = config.m_threadNum;
    m_log_write = config.m_logWrite;
    m_OPT_LINGER = config.m_linger;
    m_TRIGMode = config.m_triggerMode;
    m_close_log = config.m_closeLog;
    m_actormodel = config.m_actorMode;
    // LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    // LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    // ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    // ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

// 单例模式获取一个日志的实例
void WebServer::log_write() const
{
    if (0 == m_close_log)
    {
        // 异步日志
        if (1 == m_log_write)
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        // 同步日志
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

// 单例模式初始化数据库连接池
void WebServer::sql_pool()
{
    m_connPool = connection_pool::GetInstance();
    m_connPool->init_sql_pool("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);
    // 将数据库中的用户名和密码载入到服务器的map中来
    users->initmysql_result(m_connPool);
}

// 创建线程池
void WebServer::thread_pool()
{
    // 线程池,线程池的任务是http_conn
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

// 设置epoll的触发模式
// void WebServer::trig_mode()
// {
//     // LT + LT
//     if (0 == m_TRIGMode)
//     {
//         m_LISTENTrigmode = 0;
//         m_CONNTrigmode = 0;
//     }
//     // LT + ET
//     else if (1 == m_TRIGMode)
//     {
//         m_LISTENTrigmode = 0;
//         m_CONNTrigmode = 1;
//     }
//     // ET + LT
//     else if (2 == m_TRIGMode)
//     {
//         m_LISTENTrigmode = 1;
//         m_CONNTrigmode = 0;
//     }
//     // ET + ET
//     else if (3 == m_TRIGMode)
//     {
//         m_LISTENTrigmode = 1;
//         m_CONNTrigmode = 1;
//     }
// }

// 设置监听socket，epoll和定时器
void WebServer::eventListen()
{
    // 网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    // 优雅关闭连接
    // 游双5.11.4 linger结构体第一个参数控制开关,第二个参数控制时间
    // close函数采用默认行为(四次挥手)来关闭socket
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    // 非阻塞的socket:调用close立即返回
    // 阻塞的socket:  等待指定的时间后，直到残留数据发送完成且收到确认；否则close返回-1且errno为EWOUDLDBLOCK
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    // 配置地址
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int reuse = 1;
    // SO_REUSEADDR:处在TIME_WAIT状态的连接可以被强制使用
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);

    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    // 初始化定时器的时间片
    utils.init(TIMESLOT);

    // 配置epoll
    epoll_event events[MAX_EVENT_NUMBER]; // e存储epoll就绪事件
    m_epollfd = epoll_create(5);          // 创建epoll文件描述符
    assert(m_epollfd != -1);

    // 设置listenfd为不开启oneshot以及触发模式
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd; // http_conn中的epollfd是一个static全局变量

    // 创建管道，管道写端[1]写入信号值，管道读端[0]通过I/O复用系统监测读事件
    // socketpair相当于两端可读可写的pipe
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);

    // 管道写端非阻塞
    utils.setnonblocking(m_pipefd[1]);
    // 管道读端为ET+非阻塞并用epoll监听
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    // 设置信号处理函数SIGALRM（时间到了触发）和SIGTERM（kill会触发，Ctrl+C）
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, Utils::sig_handler, false);
    utils.addsig(SIGTERM, Utils::sig_handler, false);

    // 每隔TIMESLOT(5s)触发SIGALRM信号
    alarm(TIMESLOT);

    // 这两个是static变量
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

/************************** 定时器相关的函数 **************************/

// 初始化定时器
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    // 将connfd注册到内核事件表
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    auto *timer = new timer_node;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(nullptr);

    //TIMESLOT:最小时间间隔单位为5s
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(timer_node *timer)
{
    time_t cur = time(nullptr);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

// 关闭定时器
void WebServer::deal_timer(timer_node *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }
    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}
/************************** 定时器相关的函数 **************************/

// 7.1 处理用户连接
// 执行了accept得到一个connfd
// 分配了定时器
bool WebServer::dealclinetdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    // LT模式(默认)
    if (0 == m_LISTENTrigmode)
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }
    // ET模式
    else
    {
        // TODO:这个死循环没理解
        while (true)
        {
            // accept返回了一个新的connfd用于send()和recv()
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

// 7.2 处理定时器信号
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    // 从管道读端读出信号值，成功返回字节数，失败返回-1
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    // 正常情况下，这里的ret返回值总是1，只有14和15两个ASCII码对应的字符
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                // 用timeout参数标记右超时任务需要处理，但是没有立刻处理
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

// 7.3 读操作
void WebServer::dealwithread(int sockfd)
{
    // 取出当前socket对应的定时器
    timer_node *timer = users_timer[sockfd].timer;

    // reactor
    if (1 == m_actormodel)
    {
        // 调整定时器
        if (timer)
        {
            adjust_timer(timer); // 将定时器往后延迟15s
        }
        // 若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);
        while (true)
        {
            // TODO:这个没懂
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    // proactor
    else
    {
        if (users[sockfd].read_once()) // 主线程从这一sockfd读取数据, 直到没有更多数据可读
        {
            // 日志部分设置了一些宏
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            m_pool->append_p(users + sockfd); // 将读取到的数据封装成一个请求对象并插入请求队列
            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

// 7.4 写操作
void WebServer::dealwithwrite(int sockfd)
{
    timer_node *timer = users_timer[sockfd].timer;
    // reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        // proactor
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

// 事件回环（即服务器主线程）
void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        // 等待所监控文件描述符上有事件的产生
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        // 遍历epoll返回的就绪事件数组 处理就绪的事件
        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd; // 事件表中就绪的socket文件描述符
            // 7.1 处理用户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclinetdata(); // 添加了定时事件
                if (!flag)
                    continue;
            }
            // 处理异常事件
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 服务器端关闭连接
                timer_node *timer = users_timer[sockfd].timer;
                // 移除对应的定时器
                deal_timer(timer, sockfd);
            }
            // 7.2处理定时器信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (!flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            // 7.3 读操作
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            // 7.4 写操作
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        // 处理定时器为非必须事件，收到信号并不是立马处理，完成读写事件后，再进行处理
        if (timeout)
        {
            utils.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}
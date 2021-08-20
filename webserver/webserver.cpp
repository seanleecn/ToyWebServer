#include "webserver.h"

// 主要完成服务器初始化：http连接、根目录、定时器
WebServer::WebServer()
{
    // 保存全部的客户端信息
    m_http_conns = new http_conn[MAX_FD];

    // 保存全部定时器
    m_client_datas = new client_data[MAX_FD];
}

// 服务器资源释放
WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] m_http_conns;
    delete[] m_client_datas;
    delete m_thread_pool;
}

// 初始化用户名、数据库等信息
void WebServer::init(const string &user, const string &passWord, const string &DBName, const string &rootPath, const Config &config)
{
    m_DB_user = user;
    m_DB_password = passWord;
    m_DB_name = DBName;
    m_root = (char *)rootPath.c_str();

    m_port = config.m_port;
    m_sql_num = config.m_sql_num;
    m_thread_num = config.m_thread_num;
    m_log_mode = config.m_log_mode;
    m_linger = config.m_linger;
    m_close_log = config.m_close_log;
    m_actormodel = config.m_actor_mode;

    // 配置触发模式
    m_trigger_mode = config.m_trigger_mode;
    if (0 == m_trigger_mode) // LT + LT
    {
        m_listen_trigger_mode = 0;
        m_conn_trigger_mode = 0;
    }
    else if (1 == m_trigger_mode) // LT + ET
    {
        m_listen_trigger_mode = 0;
        m_conn_trigger_mode = 1;
    }
    else if (2 == m_trigger_mode) // ET + LT
    {
        m_listen_trigger_mode = 1;
        m_conn_trigger_mode = 0;
    }
    else if (3 == m_trigger_mode) // ET + ET
    {
        m_listen_trigger_mode = 1;
        m_conn_trigger_mode = 1;
    }
}

// 单例模式获取一个日志的实例
void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        // 异步日志
        if (1 == m_log_mode)
            Log::get_instance()->init("./ServerLog", 2000, 800000, 800);
        // 同步日志
        else
            Log::get_instance()->init("./ServerLog", 2000, 800000, 0);
    }
}

// 单例模式初始化数据库连接池
void WebServer::sql_pool()
{
    m_sql_pool = connection_pool::GetInstance();
    // 初始化连接池
    m_sql_pool->init_sql_pool("localhost", m_DB_user, m_DB_password, m_DB_name, 3306, m_sql_num, m_close_log);
    // 将数据库中的用户名和密码载入到服务器的map中来
    m_http_conns->init_mysql_result(m_sql_pool);
}

// 创建线程池
void WebServer::thread_pool()
{
    // 线程池,线程池的任务是http_conn
    m_thread_pool = new threadpool<http_conn>(m_actormodel, m_sql_pool, m_thread_num);
}

// 设置监听socket，epoll和定时器
void WebServer::eventListen()
{
    // 网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    // 优雅关闭连接
    // 游双5.11.4 linger结构体第一个参数控制开关,第二个参数控制时间
    // close函数采用默认行为(四次挥手)来关闭socket
    if (0 == m_linger)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    // 非阻塞的socket:调用close立即返回
    // 阻塞的socket:  等待指定的时间后，直到残留数据发送完成且收到确认；否则close返回-1且errno为EWOUDLDBLOCK
    else if (1 == m_linger)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    // 配置地址
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int reuse = 1;
    // SO_REUSEADDR:处在TIME_WAIT状态的连接可以被强制使用
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    int bind_ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(bind_ret >= 0);

    int listen_ret = listen(m_listenfd, 5);
    assert(listen_ret >= 0);

    // 初始化定时器的时间片
    m_utils.init(TIMESLOT);

    // 配置epoll
    epoll_event events[MAX_EVENT_NUMBER]; // e存储epoll就绪事件
    m_epollfd = epoll_create(5);          // 创建epoll文件描述符
    assert(m_epollfd != -1);

    // 设置listenfd为不开启oneshot以及触发模式
    // 调用了epoll_ctl
    m_utils.addfd(m_epollfd, m_listenfd, false, m_listen_trigger_mode);

    // http_conn中的epollfd是一个static全局变量
    http_conn::m_epollfd = m_epollfd;

    // 创建管道，管道写端[1]写入信号值，管道读端[0]通过I/O复用系统监测读事件
    // socketpair相当于两端可读可写的pipe
    int socketpair_ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(socketpair_ret != -1);

    // 管道写端非阻塞
    m_utils.setnonblocking(m_pipefd[1]);

    // 管道读端为ET+非阻塞并用epoll监听
    m_utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    // 设置信号处理函数SIGALRM（时间到了触发）和SIGTERM（kill会触发，Ctrl+C）
    m_utils.addsig(SIGPIPE, SIG_IGN);
    m_utils.addsig(SIGALRM, Utils::sig_handler, false);
    m_utils.addsig(SIGTERM, Utils::sig_handler, false);

    // 每隔TIMESLOT(5s)触发SIGALRM信号
    alarm(TIMESLOT);

    // 这两个是static变量
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

/************************** 定时器相关的函数 **************************/

// 初始化定时器
void WebServer::init_timer(int connfd, struct sockaddr_in client_address)
{
    // 将connfd注册到内核事件表
    m_http_conns[connfd].init(connfd, client_address, m_root, m_conn_trigger_mode, m_close_log, m_DB_user, m_DB_password, m_DB_name);

    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    m_client_datas[connfd].clinet_address = client_address;
    m_client_datas[connfd].client_sockfd = connfd;
    auto *timer = new timer_node;
    timer->user_data = &m_client_datas[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(nullptr);

    // TIMESLOT:最小时间间隔单位为5s
    timer->expire = cur + 3 * TIMESLOT; // 15s定时
    m_client_datas[connfd].client_timer = timer;
    m_utils.m_timer_lst.add_timer(timer);
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(timer_node *timer)
{
    time_t cur = time(nullptr);
    timer->expire = cur + 3 * TIMESLOT;
    m_utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust client_timer once");
}

// 关闭定时器
void WebServer::deal_timer(timer_node *timer, int sockfd)
{
    timer->cb_func(&m_client_datas[sockfd]);
    if (timer)
    {
        m_utils.m_timer_lst.del_timer(timer);
    }
    LOG_INFO("close fd %d", m_client_datas[sockfd].client_sockfd);
}
/************************** 定时器相关的函数 **************************/

// 处理用户连接
// 执行了accept得到一个connfd
// 分配了定时器
bool WebServer::deal_client()
{
    struct sockaddr_in client_address
    {
    };
    socklen_t client_addr_length = sizeof(client_address);

    // 监听socket为LT模式(默认)，不需要一次就处理
    if (0 == m_listen_trigger_mode)
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addr_length);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            m_utils.show_error(connfd, "Internal server busy"); // 告诉客户此时服务器繁忙
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        init_timer(connfd, client_address);
    }
    // 监听socket为ET模式，需要一次性处理数据(死循环)
    else
    {
        // ! 最外层的while保证了accept不会失效
        // https://www.cnblogs.com/qinguoyi/p/12355519.html
        while (true)
        {
            // accept返回了一个新的connfd用于send()和recv()
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addr_length);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                m_utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            init_timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

// 处理定时器信号
bool WebServer::deal_signal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    // 从管道读端读出信号值，成功返回字节数，失败返回-1
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1 || ret == 0)
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

// 读操作
void WebServer::deal_read(int sockfd)
{
    // 取出当前socket对应的定时器
    timer_node *timer = m_client_datas[sockfd].client_timer;

    // reactor
    // 只负责把请求放到队列中去
    if (1 == m_actormodel)
    {
        // 调整定时器
        if (timer)
        {
            adjust_timer(timer); // 将定时器往后延迟15s
        }

        // 监测到读事件，将该事件放入请求队列，标记为读事件0
        m_thread_pool->append(m_http_conns + sockfd, 0);

        // TODO:这个while循环把timer_flag和improv改为0
        while (true)
        {
            if (1 == m_http_conns[sockfd].improv)
            {
                if (1 == m_http_conns[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    m_http_conns[sockfd].timer_flag = 0;
                }
                m_http_conns[sockfd].improv = 0;
                break;
            }
        }
    }
    // proactor(默认)，主线程循环读取客户数据
    else
    {
        // 主线程从这一sockfd读取数据, 直到没有更多数据可读
        if (m_http_conns[sockfd].read())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(m_http_conns[sockfd].get_address()->sin_addr));
            // 将读取到的数据封装成一个请求对象并插入请求队列
            m_thread_pool->append_p(m_http_conns + sockfd);
            if (timer)
            {
                adjust_timer(timer);
            }
        }
        // TODO:一次性没有读完就把连接关了
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

// 写操作
void WebServer::deal_write(int sockfd)
{
    timer_node *timer = m_client_datas[sockfd].client_timer;
    // reactor模式，把请求添加到任务队列，让子线程写数据
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }
        // 添加到线程池中，标记事件为写1
        m_thread_pool->append(m_http_conns + sockfd, 1);

        // TODO:同没搞懂这些定时器的操作在干嘛
        while (true)
        {
            if (1 == m_http_conns[sockfd].improv)
            {
                if (1 == m_http_conns[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    m_http_conns[sockfd].timer_flag = 0;
                }
                m_http_conns[sockfd].improv = 0;
                break;
            }
        }
    }
    // proactor
    else
    {
        // 在主线程中执行write
        if (m_http_conns[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(m_http_conns[sockfd].get_address()->sin_addr));
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

// 事件回环(即服务器主线程)
void WebServer::eventLoop()
{
    bool timeout_flag = false;
    bool stop_server = false;

    while (!stop_server)
    {
        // 等待所监控文件描述符上有事件的产生
        int number = epoll_wait(m_epollfd, m_events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        // 遍历epoll返回的就绪事件数组 处理就绪的事件
        for (int i = 0; i < number; i++)
        {
            int sockfd = m_events[i].data.fd; // 事件表中就绪的socket文件描述符
            // 处理用户连接
            if (sockfd == m_listenfd)
            {
                deal_client();
            }
            // 处理定时和异常事件
            else if (m_events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                timer_node *timer = m_client_datas[sockfd].client_timer;
                deal_timer(timer, sockfd);
            }
            // 处理定时器信号 有数据进来，且是在管道[0]的数据
            else if ((sockfd == m_pipefd[0]) && (m_events[i].events & EPOLLIN))
            {
                // 判断信号，并修改参数
                deal_signal(timeout_flag, stop_server);
            }
            // 处理读操作
            else if (m_events[i].events & EPOLLIN)
            {
                deal_read(sockfd);
            }
            // 处理写操作
            else if (m_events[i].events & EPOLLOUT)
            {
                deal_write(sockfd);
            }
        }
        // 处理定时器为非必须事件，收到信号并不是立马处理，完成读写事件后，再进行处理
        if (timeout_flag)
        {
            m_utils.timer_handler();
            LOG_INFO("%s", "client_timer tick");
            timeout_flag = false;
        }
    }
}
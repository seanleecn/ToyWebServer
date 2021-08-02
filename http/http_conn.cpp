#include "http_conn.h"

// 定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

map<string, string> m_users_map; // 数据库里面已经有的用户密码
Utils m_utils; // 工具类

// 下面两个是static变量
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 将数据库中的用户名和密码载入到服务器的map中来
void http_conn::init_mysql_result(connection_pool *connPool)
{
    // 先从连接池中取一个连接
    MYSQL *conn = nullptr;
    connectionRAII mysql_conn(&conn, connPool);

    // 在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(conn, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(conn));
    }

    // 从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(conn);

    // 返回结果集中的列数
    // int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    // MYSQL_FIELD *fields = mysql_fetch_fields(result);

    // 从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string key(row[0]);
        string val(row[1]);
        m_users_map[key] = val;
    }
}

// 关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        printf("close %d\n", m_sockfd);
        m_utils.removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

/**
 * @brief 初始化http连接
 * 
 * @param sockfd 分配的客户fd 
 * @param address 客户地址
 * @param root 根目录地址
 * @param trigger_mode 客户fd的触发模式
 * @param close_log 是否关闭日志
 * @param user 数据库用户名
 * @param passwd 数据库密码
 * @param sql_name 数据库名字
 */
void http_conn::init(int sockfd, const sockaddr_in &address, char *root, int trigger_mode, int close_log,
                     const string &user, const string &passwd, const string &sql_name)
{
    m_sockfd = sockfd;
    m_address = address;

    m_utils.addfd(m_epollfd, sockfd, true, m_trigger_mode);
    m_user_count++;

    // 当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    m_doc_root = root;
    m_trigger_mode = trigger_mode;
    m_close_log = close_log;

    strcpy(m_sql_user, user.c_str());
    strcpy(m_sql_passwd, passwd.c_str());
    strcpy(m_sql_name, sql_name.c_str());
    // 私有函数初始化变量
    init();
}

// 初始化新接受的连接
void http_conn::init()
{
    m_mysql = nullptr;
    m_bytes_to_send = 0;
    m_bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_keep_alive = false;
    m_method = GET;
    m_url = nullptr;
    m_version = nullptr;
    m_content_length = 0;
    m_host = nullptr;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    m_cgi = 0;
    m_io_state = 0; // 默认读状态的请求
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

// 从状态机：分析出一行内容
// HTTP报文中，每一行的数据由\r\n作为结束字符，空行就是只有字符\r\n
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];
        // 找到\r,确认下一个是不是\n
        if (temp == '\r')
        {
            // 只解析到了\r
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            // 解析到了\r\n,表示一行完整了，把结尾替换为\0\0
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        // 找到\n,确认上一个是不是\r
        // 有可能上一次m_read_idx读到了\r\n中间，导致\r\n被两次检查
        else if (temp == '\n')
        {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 读取客户数据
bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }

    int bytes_read = 0; // 本次读取的字节数

    // connfd是LT模式
    if (0 == m_trigger_mode)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if (bytes_read <= 0)
        {
            return false;
        }

        return true;
    }
    // connfd是ET模式，一次性读完
    else
    {
        while (true)
        {
            // 参数 -读取的fd -缓冲区的位置 -缓冲区大小 -flag
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            // 返回-1：有错误
            if (bytes_read == -1)
            {
                // 非阻塞ET模式下，需要一次性将数据读完
                // EAGAIN表示读完了，可以跳出循环
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            // 返回0：已经断开连接
            else if (bytes_read == 0)
            {
                return false;
            }
            // 缓冲区的起始位置+读取的大小
            m_read_idx += bytes_read;
        }
        return true;
    }
}

// 解析http请求行
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // text:GET /index.html HTTP/1.1 为例

    // 找到空格或者\t的位置
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        return BAD_REQUEST;
    }

    // 将匹配到空格或者\t的位置改为\0，然后指针后移一位
    *m_url++ = '\0';

    // text:GET\0/index.html HTTP/1.1

    // 判断请求方式
    char *method = text;
    // 因为上面将第一个空格改为了\0，因此method为GET
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    // POST请求要打开cgi
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        m_cgi = 1; // 這个很关键
    }
    else
    {
        return BAD_REQUEST;
    }

    // m_url此时跳过了第一个空格或\t字符，但不知道之后是否还有
    // strspn:跳过匹配的字符串片段
    m_url += strspn(m_url, " \t");

    // text:/index.html HTTP/1.1

    // 判断HTTP版本号
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");

    // text:/index.html\0HTTP/1.1

    // 仅支持HTTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;

    // 有些报文的请求资源中会带有http://，对这种情况进行单独处理
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        // strchr:找到/的位置
        m_url = strchr(m_url, '/');
    }
    // https情况
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    // 一般的不会带有上述两种符号，直接是单独的/或/后面带访问资源
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;

    // 当url为/时，显示判断是登录还是注册的界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");

    // 请求行处理完毕，将主状态机转移处理请求头
    m_check_state = CHECK_STATE_HEADER;

    return NO_REQUEST;
}

// 解析http请求头
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    // 判断是空行还是请求头
    if (text[0] == '\0')
    {
        // 如果请求体长度不是0，说明是POST请求，主状态机 CHECK_STATE_HEADER->CHECK_STATE_CONTENT
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 是GET请求，读到空行表示完整解析了一个GET请求
        return GET_REQUEST;
    }

    // 解析请求头部connection字段
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;                  // 跳过文字
        text += strspn(text, " \t"); // 跳过空格或者\t

        // TODO:keep-alive和优雅关闭连接有什么关系
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_keep_alive = true; //如果是长连接，则将linger标志设置为true
        }
    }

    // 解析请求头部Content-length字段
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text); // 请求数据的长度
    }

    // 解析请求头部Host字段
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        LOG_INFO("oop! unknown request header: %s", text);
    }
    return NO_REQUEST;
}

// 解析请求体数据
// 只有POST请求有这一部分，GET请求是直接写在请求行的url里面了
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    // 这个判断保证已经完整把整个请求数据部分都读进来了
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        m_content = text; // 把请求数据的内容存起来
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 主状态机：有限状态机处理请求报文
http_conn::HTTP_CODE http_conn::process_read()
{
    // 初始化从状态机状态
    LINE_STATUS line_status = LINE_OK;

    // 初始化HTTP请求解析结果
    HTTP_CODE ret = NO_REQUEST;

    char *text = nullptr;

    // 从状态机解析(按行读取缓冲区数据并分析)
    // GET请求报文中，每一行都是\r\n作为结束
    // 仅用从状态机的状态((line_status = parse_line()) == LINE_OK)判断即可
     while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
           ((line_status = parse_line()) == LINE_OK))
           // POST请求报文中，消息体的末尾没有任何字符
           // 使用 主状态机 的状态 m_check_state == CHECK_STATE_CONTENT
           // 解析完消息体后，报文的完整解析就完成了，但此时主状态机的状态还是CHECK_STATE_CONTENT，还会再次进入循环
           // 增加了 && line_status == LINE_OK，并在完成消息体解析后，将line_status变量更改为LINE_OPEN，此时可以跳出循环
    {
        text = get_line();

        m_start_line = m_checked_idx; // 更新已经解析的数据起点指针

        LOG_INFO("%s", text);

        // 主状态机的三种状态转移
        switch (m_check_state)
        {
        // 请求行，没问题的话主状态机的状态 CHECK_STATE_REQUESTLINE->CHECK_STATE_HEADER
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }

        // 请求头
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            // 完整解析GET请求后，跳转到报文响应函数
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }

        // 请求消息体
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text);
            // 完整解析GET请求后，跳转到报文响应函数
            if (ret == GET_REQUEST)
                return do_request();
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

// 解析是要请求文件，这个函数把文件路径准备好
http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, m_doc_root);
    int len = strlen(m_doc_root);

    // p是/所在的位置,根据/后面的字符判断是登录还是注册
    const char *p = strrchr(m_url, '/');

    // 2:登录校验  3:注册校验
    // m_cgi表示此时是post
    if (m_cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        // 根据标志判断是登录检测还是注册检测
        char flag = m_url[1];
        // 登录
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2); // 拼接路径
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        // 将用户名和密码提取出来
        // user=123&password=123
        char name[100], password[100];
        int i; // 这里的5就是跳过user=
        for (i = 5; m_content[i] != '&'; ++i)
            name[i - 5] = m_content[i];
        name[i - 5] = '\0';

        int j = 0; // 这里的10是跳过&password=
        for (i = i + 10; m_content[i] != '\0'; ++i, ++j)
            password[j] = m_content[i];
        password[j] = '\0';

        // 注册
        if (*(p + 1) == '3')
        {

            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            // 没有重名的
            if (m_users_map.find(name) == m_users_map.end())
            {
                m_http_lock.lock();
                // 插入密码
                // 新的密码
                int res = mysql_query(m_mysql, sql_insert);
                m_users_map.insert(pair<string, string>(name, password));
                m_http_lock.unlock();
                // 如果sql插入成功
                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            // 有重名的
            else
            {
                strcpy(m_url, "/registerError.html");
            }
        }
        // 登录
        else if (*(p + 1) == '2')
        {
            // 如果密码正确
            if (m_users_map.find(name) != m_users_map.end() && m_users_map[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    // 0:跳转注册页面,GET
    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        // 将网站目录和/register.html进行拼接，更新到m_real_file中
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }

    // 1:跳转登录界面,GET
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }

    // 5:显示图片页面,POST
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }

    // 6:显示视频页面,POST
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }

    // 7:显示关注页面,POST
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    // 以上均不符合发送url实际请求的文件
    else
    {
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

    // 通过stat获取请求资源文件信息，成功则将信息更新到m_file_stat结构体
    // 失败返回NO_RESOURCE状态，表示资源不存在
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;

    // 判断文件的权限，是否可读，不可读则返回FORBIDDEN_REQUEST状态
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    // 判断文件类型，如果是目录，则返回BAD_REQUEST，表示请求报文有误
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    // 以只读方式获取文件描述符，通过mmap将该文件映射到内存中
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(nullptr, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    // 避免文件描述符的浪费和占用
    close(fd);

    // 表示请求文件存在，且可以访问
    return FILE_REQUEST;
}

// 取消内存映射
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = nullptr;
    }
}

// 服务器子线程调用process_write完成响应报文，随后注册epollout事件(可写)
// 服务器主线程检测写事件，调用write函数将响应报文发送给浏览器端
bool http_conn::write()
{
    // 没有待发送的数据
    if (m_bytes_to_send == 0)
    {
        m_utils.modfd(m_epollfd, m_sockfd, EPOLLIN, m_trigger_mode);
        init();
        return true;
    }
    // 保证一次性发完
    // TODO:为啥这里没有区别ET或者LT模式
    while (true)
    {
        // 调用分散写writev函数把状态行、消息头、空行和响应正文写到socket的发送缓冲区
        // m_iv数组保存了报文和mmap映射到内存中的文件的地址
        // 返回正常发送字节数
        int writev_ret = 0;
        // TODO:这里多次调用writev没问题吗
        writev_ret = writev(m_sockfd, m_iv, m_iv_count);
        if (writev_ret < 0)
        {
            // 判断缓冲区是否满了
            if (errno == EAGAIN || errno==EWOULDBLOCK)
            {
                // 重新注册写事件
                m_utils.modfd(m_epollfd, m_sockfd, EPOLLOUT, m_trigger_mode);
                return true;
            }
            // 不是缓冲区得问题,取消内存映射
            unmap();
            return false;
        }

        m_bytes_have_send += writev_ret; // 更新已发送字节
        m_bytes_to_send -= writev_ret;   // 更新未发送字节

        // 第一个iovec(头部)的数据已发送完，发送第二个iovec数据
        if (m_bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (m_bytes_have_send - m_write_idx);
            m_iv[1].iov_len = m_bytes_to_send;
        }

        // 继续发送第一个iovec头部信息的数据
        else
        {
            m_iv[0].iov_base = m_write_buf + m_bytes_have_send;
            // TODO:这里减去已发送的值可能越界
            m_iv[0].iov_len = m_iv[0].iov_len - m_bytes_have_send;
        }

        // 判断条件，数据已全部发送完
        if (m_bytes_to_send <= 0)
        {
            unmap();
            // 重置oneshot，重新监听可读事件
            m_utils.modfd(m_epollfd, m_sockfd, EPOLLIN, m_trigger_mode);

            // 浏览器的请求为长连接
            if (m_keep_alive)
            {
                init();
                return true;
            }
            else
                return false;
        }
    }
}

// add...系列函数最终都是调用这个函数操作指针
bool http_conn::add_response(const char *format, ...)
{
    // 如果写入内容超出m_write_buf大小则报错
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;

    // 定义可变参数列表
    va_list arg_list;

    // 将变量arg_list初始化为传入参数
    va_start(arg_list, format);

    // 将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);

    // 如果写入的数据长度超过缓冲区剩余空间，则报错
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }

    // 更新m_write_idx位置
    m_write_idx += len;

    // 清空可变参列表
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}

// 添加状态行
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 添加消息报头，具体的添加文本长度、连接状态和空行
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() && add_blank_line();
}

// 添加Content-Length，表示响应报文的长度
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}

// 添加文本类型，这里是html
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 添加连接状态，通知浏览器端是保持连接还是关闭
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", m_keep_alive ? "keep-alive" : "close");
}

// 添加空行
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

// 添加文本content
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

// 根据process_read()的报文解析结果，向m_write_buf中写入响应报文
// 内部涉及到add...系列函数，均是内部调用add_response函数
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    // 服务器内部错误
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    // 客户请求语法错误
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    // 客户对资源没有足够的访问权限
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    // 文件请求,获取文件成功
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        // 如果请求的资源存在
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            // 第一个iovec指针指向m_write_buf写缓冲区
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            // 第二个iovec指针指向共享内存mmap返回的m_file_address
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            // 待发送的全部数据为响应报文头部信息和文件大小
            m_bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        // 如果请求的资源大小为0，则返回空白html文件
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }

    // 除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    m_bytes_to_send = m_write_idx;
    return true;
}

// 数据读到了缓冲区之后，子线程会调用这个函数解析请求报文
void http_conn::process()
{
    // 报文解析
    HTTP_CODE read_ret = process_read();

    // 请求不完整，需要继续接收请求数据
    if (read_ret == NO_REQUEST)
    {
        // 注册并监听读事件
        m_utils.modfd(m_epollfd, m_sockfd, EPOLLIN, m_trigger_mode);
        return;
    }

    // 根据解析的报文状态，生成相应的报文响应
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    // 注册并监听写事件
    m_utils.modfd(m_epollfd, m_sockfd, EPOLLOUT, m_trigger_mode);
}

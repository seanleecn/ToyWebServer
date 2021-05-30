#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
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
#include <map>

#include "../lock/locker.hpp"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    /* 1. HTTP请求方法，支持GET和POST */
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    /* 2. 主状态机的状态
        CHECK_STATE_REQUESTLINE  当前正在分析请求行
        CHECK_STATE_HEADER       当前正在分析头部字段
        CHECK_STATE_CONTENT      当前正在解析请求体*/
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    /* 3. HTTP状态码
        NO_REQUEST          请求不完整，需要继续读取客户数据
        GET_REQUEST         获得了一个完成的客户请求
        BAD_REQUEST         客户请求语法错误
        NO_RESOURCE         服务器没有资源
        FORBIDDEN_REQUEST   客户对资源没有足够的访问权限
        FILE_REQUEST        文件请求,获取文件成功
        INTERNAL_ERROR      服务器内部错误
        CLOSED_CONNECTION   客户端已经关闭连接*/
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    /* 4. 从状态机的状态，即行的读取状态
        LINE_OK       读取到一个完整的行 
        LINE_BAD      行出错 
        LINE_OPEN     行数据尚且不完整 */
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() = default;
    ~http_conn() = default;

    //初始化套接字，会调用私有函数void init()
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);

    //关闭HTTP连接
    void close_conn(bool real_close = true);

    //http处理函数
    void process();

    //读取浏览器发送的数据
    bool read_once();

    //给相应报文中写入数据
    bool write();

    sockaddr_in *get_address()
    {
        return &m_address;
    }

    // 初始化数据库读取线程
    void initmysql_result(connection_pool *connPool) const;

    // 时间事件类型
    int timer_flag;
    int improv;

private:
    // 由public的init调用，对私有成员进程初始化
    void init();

    // 从m_read_buf读取，并处理请求报文
    HTTP_CODE process_read();

    // 向m_write_buf写入响应报文数据
    bool process_write(HTTP_CODE ret);

    // 主状态机解析报文中的请求行数据
    HTTP_CODE parse_request_line(char *text);

    // 主状态机解析报文中的请求头数据
    HTTP_CODE parse_headers(char *text);

    // 主状态机解析报文中的请求内容
    HTTP_CODE parse_content(char *text);

    // 生成响应报文
    HTTP_CODE do_request();

    // m_start_line是已经解析的字符
    // get_line用于将指针向后偏移，指向未处理的字符
    char *get_line() { return m_read_buf + m_start_line; };

    // 从状态机读取一行，分析是请求报文的哪一部分
    LINE_STATUS parse_line();

    void unmap();

    // 根据响应报文格式，生成对应8个部分，以下函数均由do_request调用
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    int m_state; // IO事件类别:读为0, 写为1

    static const int FILENAME_LEN = 200;       // 读取文件长度上限
    static const int READ_BUFFER_SIZE = 2048;  // 读缓存大小
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓存大小

private:
    int m_sockfd;
    sockaddr_in m_address;

    char m_read_buf[READ_BUFFER_SIZE];   // 存储读取的请求报文数据
    int m_read_idx;                      // m_read_buf中数据的最后一个字节的下一个位置
    int m_checked_idx;                   // m_read_buf读取的位置
    int m_start_line;                    // m_read_buf中已经解析的字符个数
    char m_write_buf[WRITE_BUFFER_SIZE]; // 存储发出的响应报文数据
    int m_write_idx;                     // 指示buffer中的长度
    CHECK_STATE m_check_state;           // 主状态机的状态
    METHOD m_method;                     // 请求方法

    // 解析请求报文中对应的6个变量
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;

    // 读取服务器上的文件地址
    char *m_file_address;
    struct stat m_file_stat;

    // io向量机制iovec
    struct iovec m_iv[2];
    int m_iv_count;

    int cgi;             // 是否启用的POST
    char *m_string;      // 存储请求头数据
    int bytes_to_send;   // 剩余发送字节数
    int bytes_have_send; // 已发送字节数
    char *doc_root;

    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif

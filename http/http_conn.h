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
#include <mysql/mysql.h>
#include <fstream>
#include <string>

#include "../lock/locker.hpp"
#include "../connpool/conn_pool.h"
#include "../timer/timer.h"
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
        NO_REQUEST = 0,
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
    http_conn(){};
    ~http_conn(){};

    // 初始化套接字，会调用私有函数void init()
    void init(int sockfd, const sockaddr_in &address, char *root, int trigger_mode, int close_log,
              const string &user, const string &passwd, const string &sql_name);

    // 关闭HTTP连接
    void close_conn(bool real_close = true);

    // 线程池入口 解析请求报文
    void process();

    // 把数据从socket缓冲读到http缓冲
    bool read();

    // 把数据从http缓冲读到socket缓冲
    bool write();

    sockaddr_in *get_address() { return &m_address; };

    // 初始化读取账户和密码
    void init_mysql_result(connection_pool *connPool);

private:
    // 由public的init调用，对私有成员进程初始化
    void init();

    /*** 从读缓冲区读取报文并解析报文 ***/
    HTTP_CODE process_read();                               // 从m_read_buf读取，并解析报文入口
    HTTP_CODE parse_request_line(char *text);               // 解析HTTP请求行:获得请求方法，目标URL,以及HTTP版本号
    HTTP_CODE parse_headers(char *text);                    // 解析HTTP请求的一个头部信息
    HTTP_CODE parse_content(char *text);                    // 解析HTTP请求的消息体
    LINE_STATUS parse_line();                               // 从状态机读取一行，分析是请求报文的哪一部分
    char *get_line() { return m_read_buf + m_start_line; }; // 拿到从状态机已经解析好的一行,m_start_line是从状态机已经解析的字符
    HTTP_CODE do_request();                                 // 根据解析的请求，将不同的相应页面准备好

    /*** 根据解析返回的HTTP_CODE向写缓冲区写入数据 ***/
    bool process_write(HTTP_CODE ret); // 向m_write_buf写入响应报文数据，入口
    // 根据响应报文格式，生成对应8个部分，以下函数均由do_request调用
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
    void unmap();

public:
    // static变量类内声明，类外初始化
    static int m_epollfd;    // 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
    static int m_user_count; // 统计用户的数量
    // static const在声明时需要指定值
    static const int FILENAME_LEN = 200;       // 读取文件长度上限
    static const int READ_BUFFER_SIZE = 2048;  // 读缓存大小
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓存大小

    // 这两个参数是reactor模式中用到了
    int timer_flag;
    int improv;

    MYSQL *m_mysql; // 从连接池中取出一个mysql连接
    int m_io_state; // IO事件类别:读为0, 写为1

    int m_sockfd;          // 该HTTP连接的socket
    sockaddr_in m_address; // 对方的socket地址

    /*** 读缓冲区 ***/
    char m_read_buf[READ_BUFFER_SIZE]; // 存储读取的请求报文数据
    int m_read_idx;                    // m_read_buf中数据的最后一个字节的下一个位置
    int m_checked_idx;                 // m_read_buf读取的位置
    int m_start_line;                  // m_read_buf中已经解析的字符个数

    /*** 写缓冲区 ***/
    char m_write_buf[WRITE_BUFFER_SIZE]; // HTTP的写缓冲区，和socket的缓冲区不同
    int m_write_idx;                     // 指示buffer中的长度
    int m_bytes_to_send;                 // 剩余发送字节数
    int m_bytes_have_send;               // 已发送字节数

    /*** 解析HTTP报文并保存相关的参数 ***/
    CHECK_STATE m_check_state; // 主状态机的状态
    METHOD m_method;           // 请求方法，get还是post
    char *m_file_address;      // 文件地址
    struct stat m_file_stat;   // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct iovec m_iovec[2];   // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int m_iovec_cnt;

    char m_real_file[FILENAME_LEN]; // 客户请求的目标文件的完整路径，其内容等于 m_doc_root + m_url
    char *m_url;                    // 客户请求的目标文件的文件名
    char *m_version;                // HTTP协议版本号，我们仅支持HTTP1.1
    char *m_host;                   // 主机名
    int m_content_length;           // HTTP请求的消息总长度
    bool m_keep_alive;              // HTTP请求是否要求保持连接
    char *m_doc_root;               // 资源目录

    int m_cgi;       // 是否启用的POST
    char *m_content; // HTTP请求的请求体内容

    int m_trigger_mode;
    int m_close_log;

    char m_sql_user[100];
    char m_sql_passwd[100];
    char m_sql_name[100];

    locker m_http_lock;
};

#endif

# ToyWebServer

Linux下基于C++的玩具级服务器

## 项目架构

```
.
├── CGImysql        数据库
├── config          服务器参数
├── http            实现HTTP协议的连接和销毁 
├── lock            封装互斥锁和信号量
├── log             日志系统
├── root            静态网页的数据 
├── threadpool      线程池
├── timer           定时器
├── webbench-1.5    压力测试
├── webserver       服务器
├── CMakeLists.txt  
├── README.md
└── main.cpp
```

**有什么特点** 

* 使用 **线程池 + 非阻塞socket + epoll(ET和LT均实现) + 事件处理(Reactor和模拟Proactor均实现)** 的并发模型
* 使用**状态机**解析HTTP请求报文，支持解析**GET和POST**请求
* 访问服务器数据库实现web端用户**注册、登录**功能，可以请求播放服务器**图片和视频文件**
* 实现**同步/异步日志系统**，记录服务器运行状态
* 经Webbench压力测试可以实现**上万的并发连接**数据交换


## 测试


* 安装MySQL数据库

    ```bash
    sudo apt install mysql-server libmysqlclient-dev
    ```
    
* 创建密码表单
    ```sql
    <bash> sudo mysql
    
    // 建立webserDb库
    create database webserDb;
    USE webserDb;
    
    // 创建user表
    CREATE TABLE user(
        username char(50) NULL,
        passwd char(50) NULL
    )ENGINE=InnoDB;
    
    // 添加用户数据
    INSERT INTO user(username, passwd) VALUES('lx', '123');
    ```
    
* 修改main.cpp中的数据库初始化信息

    ```C++
    //登录mysql的用户密码，不是上面添加的用户数据
    string user = "root";
    string passwd = "1234";
    string databasename = "webserDb";
    ```

* build

    ```bash
    mkdir build && cd build
    cmake ..
    make
    ```

* 启动server

    ```bash
    ./toy_web_server #如果使用root登录mysql,要用sudo运行
    ```

* 本机浏览器端

    ```C++
    127.0.0.1:9006
    ```
    
* 压力测试    
  
    在**关闭日志**后，使用Webbench对服务器进行压力测试，对listenfd和connfd分别采用ET和LT模式，均可实现上万的并发连接，下面列出的是两者组合后的测试结果. 
    
    > * 并发连接总数：10000
    > * 访问服务器时间：5s
    > * 所有访问均成功

个性化运行
------

```bash
./toy_web_server [-p port] [-l LOGWrite] [-m TRIGMode] [-o OPT_LINGER] [-s sql_num] [-t thread_num] [-c close_log] [-a actor_model]
```

温馨提示:以上参数不是非必须，不用全部使用，根据个人情况搭配选用即可.

* -p，自定义端口号
	* 默认9006
* -l，选择日志写入方式，默认同步写入
	* 0，同步写入
	* 1，异步写入
* -m，listenfd和connfd的模式组合，默认使用LT + LT
	* 0，表示使用LT + LT
	* 1，表示使用LT + ET
    * 2，表示使用ET + LT
    * 3，表示使用ET + ET
* -o，优雅关闭连接，默认不使用
	* 0，不使用
	* 1，使用
* -s，数据库连接数量
	* 默认为8
* -t，线程数量
	* 默认为8
* -c，关闭日志，默认打开
	* 0，打开日志
	* 1，关闭日志
* -a，选择反应堆模型，默认Proactor
	* 0，Proactor模型
	* 1，Reactor模型

测试示例命令与含义

```C++
./server -p 9007 -l 1 -m 0 -o 1 -s 10 -t 10 -c 1 -a 1
```


- [x] 端口9007
- [x] 异步写入日志
- [x] 使用LT + LT组合
- [x] 使用优雅关闭连接
- [x] 数据库连接池内有10条连接
- [x] 线程池内有10条线程
- [x] 关闭日志
- [x] Reactor反应堆模型

# 致谢
Linux高性能服务器编程，游双著.

https://github.com/qinguoyi/TinyWebServer


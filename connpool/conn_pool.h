#ifndef SQL_CONN_POOL_H
#define SQL_CONN_POOL_H

#include <cstdio>
#include <error.h>
#include <cstring>
#include <iostream>
#include <string>
#include <mysql/mysql.h>
#include <cstdlib>
#include <list>

#include "../lock/locker.hpp"
#include "../log/log.h"

// 单例模式 这个是利用局部静态变量懒汉模式实现单例
// C++11之后局部静态变量线程安全
class connection_pool
{
public:
	MYSQL *get_conn();				   // 获取数据库连接
	bool release_conn(MYSQL *conn);   // 释放连接
	int get_free_conn();			   // 获取连接
	void destroy_pool();					   // 销毁所有连接(析构调用)
	static connection_pool *GetInstance(); // 获取一个实例

	void init_sql_pool(const string &url, const string &user, const string &password,
                       const string &DBname, int port, int max_conn, int close_log);

private:
	connection_pool();
	~connection_pool();

	int m_max_conn;			// 最大连接数
	int m_cur_conn;			// 当前已使用的连接数
	int m_free_conn;			// 当前空闲的连接数
	locker m_conn_lock;			// 锁
	list<MYSQL *> m_conn_list; // 连接池
	sem m_reserve_conn;			// 信号量

public:
	string m_url;		   // 主机地址
	int m_port;		   // 数据库端口号
	string m_user;		   // 登陆数据库用户名
	string m_password;	   // 登陆数据库密码
	string m_DB_name; // 使用数据库名
	int m_close_log;	   // 日志开关
};

// 数据库连接的获取与释放通过RAII机制封装，避免手动释放
class connectionRAII
{
public:
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();

private:
	MYSQL *conRAII;
	connection_pool *poolRAII;
};

#endif

#include "conn_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	m_cur_conn = 0;
	m_free_conn = 0;
}

connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

// 初始化
void connection_pool::init_sql_pool(const string &url, const string &user, const string &password,
									const string &DBname, int port, int max_conn, int close_log)
{
	// 初始化数据库信息
	m_url = url;
	m_port = port;
	m_user = user;
	m_password = password;
	m_DB_name = DBname;
	m_close_log = close_log;

	// 创建MaxConn条数据库连接
	for (int i = 0; i < max_conn; i++)
	{
		MYSQL *sql_conn = nullptr;
		sql_conn = mysql_init(sql_conn);
		if (sql_conn == nullptr)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		sql_conn = mysql_real_connect(sql_conn, m_url.c_str(), m_user.c_str(), m_password.c_str(),
									  m_DB_name.c_str(), m_port, nullptr, 0);
		if (sql_conn == nullptr)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		// 更新连接池
		m_conn_list.push_back(sql_conn);
		++m_free_conn;
	}
	// 信号量初始化为当前可用的连接数量
	m_reserve_conn = sem(m_free_conn);
	m_max_conn = m_free_conn;
}

// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::get_conn()
{
	MYSQL *con = nullptr;

	if (m_conn_list.empty())
		return nullptr;
	// 取出连接，信号量原子减1，为0则等待
	m_reserve_conn.wait();

	m_conn_lock.lock();

	con = m_conn_list.front();
	m_conn_list.pop_front();

	--m_free_conn;
	++m_cur_conn;

	m_conn_lock.unlock();
	return con;
}

// 释放当前使用的连接
bool connection_pool::release_conn(MYSQL *conn)
{
	if (nullptr == conn)
		return false;

	m_conn_lock.lock();
	m_conn_list.push_back(conn);
	++m_free_conn;
	--m_cur_conn;
	m_conn_lock.unlock();
	// 释放连接原子加1
	m_reserve_conn.post();
	return true;
}

// 销毁数据库连接池
void connection_pool::destroy_pool()
{
	m_conn_lock.lock();
	if (!m_conn_list.empty())
	{
		for (auto it = m_conn_list.begin(); it != m_conn_list.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_cur_conn = 0;
		m_free_conn = 0;
		m_conn_list.clear();
	}

	m_conn_lock.unlock();
}

// 当前空闲的连接数
int connection_pool::get_free_conn()
{
	return this->m_free_conn;
}

// RAII机制销毁连接池
connection_pool::~connection_pool()
{
    destroy_pool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
	*SQL = connPool->get_conn();

	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
    poolRAII->release_conn(conRAII);
}
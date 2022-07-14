#include "log.h"

#include "mysql_conn.h"

using namespace std;

// 销毁数据库连接池
ConnPool::~ConnPool() {
    mutex_.lock();
    if (conn_list_.size() > 0) {
        // 通过迭代器遍历，关闭数据库连接
        for (MYSQL* conn : conn_list_) 
            mysql_close(conn);
        // 清空list
        conn_list_.clear();
        mutex_.unlock();
    }
    mutex_.unlock();
}

// 构造初始化
void ConnPool::init(string url, string username, string password, string db_name, int port, unsigned int max_conn, bool close_log) {
    // 初始化数据库信息
    url_ = url;
    port_ = port;
    username_ = username;
    password_ = password;
    db_name_ = db_name;
    close_log_ = close_log;

    // 创建max_conn条数据库连接
    for (int i = 0; i < max_conn; i++) {
        MYSQL* conn = nullptr;
        conn = mysql_init(conn);
        if (conn == nullptr) {
            LOG_ERROR("MySQL Error: %s!", mysql_error(conn));
            exit(EXIT_FAILURE);
        }
        conn = mysql_real_connect(conn, url.c_str(), username.c_str(), password.c_str(), db_name.c_str(), port, nullptr, 0);

        if (conn == nullptr) {
            LOG_ERROR("MySQL Error: %s!", mysql_error(conn));
            exit(EXIT_FAILURE);
        }
        // 更新连接池和空闲连接数量
        conn_list_.push_back(conn);
        free_conn_++;
    }
    // 将信号量初始化为空闲连接数量
    sem_ = Sem(free_conn_);
    max_conn_ = free_conn_;
}

// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL* ConnPool::get_conn() {
    MYSQL* conn = nullptr;
    if (conn_list_.size() == 0)
        return nullptr;

    // 取出连接，信号量原子减1，为0则等待
    sem_.wait();
    mutex_.lock();

    conn = conn_list_.front();
    conn_list_.pop_front();

    free_conn_--;
    curr_conn_++;

    mutex_.unlock();
    return conn;
}

// 释放当前使用的连接
bool ConnPool::rel_conn(MYSQL* conn) {
    if (conn == nullptr) 
        return false;

    mutex_.lock();

    conn_list_.push_back(conn);
    
    free_conn_++;
    curr_conn_--;

    mutex_.unlock();
    // 释放连接原子加1
    sem_.post();

    return true;
}

ConnRaii::ConnRaii(MYSQL** conn, ConnPool* conn_pool) : pool_(conn_pool) {
    *conn = conn_pool->get_conn();
    conn_ = *conn;
    pool_ = conn_pool;
}

ConnRaii::~ConnRaii() {
    pool_->rel_conn(conn_);
}

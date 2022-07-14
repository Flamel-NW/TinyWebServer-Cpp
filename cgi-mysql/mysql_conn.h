#ifndef MYSQL_CONN_H
#define MYSQL_CONN_H

#include "pch.h"

#include "lock.h"

class ConnPool {
public:
    // 局部静态变量单例模式
    static ConnPool* get_instance() {
        static ConnPool conn_pool;
        return &conn_pool;
    }

    void init(std::string url, std::string username, std::string password, std::string db_name, int port, unsigned int max_conn, bool close_log);
    // 获取数据库连接
    MYSQL* get_conn();
    // 释放连接
    bool rel_conn(MYSQL* conn);

    // 获取连接
    int get_free_conn() {
        return free_conn_;
    }

private:
    ConnPool() = default;
    // 销毁所有连接
    ~ConnPool();

    int max_conn_;                  // 最大连接数
    int curr_conn_;                 // 当前已使用的连接数
    int free_conn_;                 // 当前空闲的连接数

    Mutex mutex_;
    std::list<MYSQL*> conn_list_;   // 连接池
    Sem sem_;

    std::string url_;               // 主机地址
    std::string port_;              // 数据库端口号
    std::string username_;          // 登录数据库用户名
    std::string password_;          // 登录数据库密码
    std::string db_name_;           // 使用数据库名
    int close_log_;                 // 日志开关
};

class ConnRaii {
public:
    // 双指针对MYSQL* conn修改
    ConnRaii(MYSQL** conn, ConnPool* conn_pool);
    ~ConnRaii();

private:
    MYSQL* conn_;
    ConnPool* pool_;
};

#endif

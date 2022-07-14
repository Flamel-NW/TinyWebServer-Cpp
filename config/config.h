#ifndef CONFIG_H
#define CONFIG_H

#include "pch.h"

class Config {
public:
    Config() = delete;

    static void parse_arg(int argc, char* argv[]) ;

    // 端口号，默认81
    static int port_;
    // 日志写入方式，默认同步
    static bool write_log_;
    // 触发组合模式，默认listenfd LT + connfd LT
    static int trig_mode_;
    // listenfd触发模式，默认LT
    static bool listenfd_trig_mode_;
    // connfd触发模式，默认LT
    static bool connfd_trig_mode_;
    // 优雅关闭连接，默认不使用
    static bool opt_linger_;
    // 数据库连接池容量，默认8
    static int conn_pool_size_;
    // 线程池容量，默认8
    static int thread_pool_size_;
    // 关闭日志，默认不关闭
    static bool close_log_;
    // 并发模型，默认是proactor
    static bool actor_pattern_;
};


#endif

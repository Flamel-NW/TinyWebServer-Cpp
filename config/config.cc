#include "config.h"


// 端口号，默认81
int Config::port_ = 81;
// 日志写入方式，默认同步
bool Config::write_log_ = false;
// 触发组合模式，默认listenfd LT + connfd LT
int Config::trig_mode_ = 0;
// listenfd触发模式，默认LT
bool Config::listenfd_trig_mode_ = false;
// connfd触发模式，默认LT
bool Config::connfd_trig_mode_ = false;
// 优雅关闭连接，默认不使用
bool Config::opt_linger_ = false;
// 数据库连接池容量，默认8
int Config::conn_pool_size_ = 8;
// 线程池容量，默认8
int Config::thread_pool_size_ = 8;
// 关闭日志，默认不关闭
bool Config::close_log_ = false;
// 并发模型，默认是proactor
bool Config::actor_pattern_ = false;


void Config::parse_arg(int argc, char* argv[]) {
    int opt = 0;
    const char str[] = "p:w:m:o:c:t:l:e:";
    while ((opt = getopt(argc, argv, str)) != -1) {
        if (opt == 'p') port_ = atoi(optarg);
        if (opt == 'w') write_log_ = atoi(optarg);
        if (opt == 'm') trig_mode_ = atoi(optarg);
        if (opt == 'o') opt_linger_ = atoi(optarg);
        if (opt == 'c') conn_pool_size_ = atoi(optarg);
        if (opt == 't') thread_pool_size_ = atoi(optarg);
        if (opt == 'l') close_log_ = atoi(optarg);
        if (opt == 'e') actor_pattern_ = atoi(optarg);
    }
}

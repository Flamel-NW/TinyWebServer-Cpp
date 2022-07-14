#include "pch.h"

#include "config.h"
#include "server.h"

using namespace std;

int main(int argc, char* argv[]) {
    // 数据库信息：登录名，密码，库名
    string username = "root";
    string password = "root";
    string db_name = "TinyWebServerDB";

    // 命令行解析
    Config::parse_arg(argc, argv);

    // 初始化
    Server server;
    server.init(username, password, db_name,
        Config::port_, Config::actor_pattern_, Config::opt_linger_, Config::trig_mode_,
        Config::close_log_, Config::write_log_, Config::thread_pool_size_, Config::conn_pool_size_);

    // 日志
    server.init_log();

    // 数据库连接池
    server.init_conn_pool();

    // 线程池
    server.init_thread_pool();

    // 触发模式
    server.init_trig_mode();

    // 监听
    server.event_listen();

    // 运行
    server.event_loop();

    return 0;
}

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
    Server server(Config::port_, Config::close_log_, Config::write_log_, 
        Config::conn_pool_size_, Config::thread_pool_size_,
        username, password, db_name,
        Config::opt_linger_, Config::trig_mode_, Config::actor_pattern_);

    // 监听
    server.event_listen();

    // 运行
    server.event_loop();

    return 0;
}

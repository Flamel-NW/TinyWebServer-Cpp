#ifndef HTTP_CONNECTION_H
#define HTTP_CONNECTION_H

#include "pch.h"

#include "lock.h"
#include "mysql_conn.h"
#include "utils.h"

class HttpConn {
public:
    static constexpr int FILENAME_LEN = 200;
    static constexpr int READ_BUFFER_SIZE = 2048;
    static constexpr int WRITE_BUFFER_SIZE = 1024;
    
    enum Method {
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

    // 主状态机的状态
    enum CheckState {
        CHECK_STATE_REQUEST_LINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    // 报文解析的结果
    enum HttpCode {
        NO_REQUEST = 0,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    // 从状态机的状态
    enum LineState {
        LINE_STATE_OK = 0,
        LINE_STATE_BAD,
        LINE_STATE_OPEN
    };

    // 初始化套接字地址，函数内部会调用私有方法init
    void init(int sockfd, const sockaddr_in& addr, const char* root_dir, bool trig_mode, bool close_log,
        std::string username, std::string password, std::string db_name);
    // 关闭http连接
    void close(bool real_close = true);
    void process();
    // 读取浏览器端发来的全部数据
    bool read_once();
    // 响应报文写入函数
    bool write();
    sockaddr_in* get_address() {
        return &address_;
    }
    // CGI使用线程池初始化数据库表
    void init_mysql_result(ConnPool* conn_pool);

    static int epollfd_;
    static int user_count_;

    MYSQL* mysql_;
    bool state_;                             // 读为false，写为true
    bool timer_flag_;
    bool improve_;

private:
    void init();
    // 从read_buf_读取，并处理请求报文
    HttpCode process_read();
    // 向write_buf_写入响应报文数据
    bool process_write(HttpCode ret);
    // 主状态机解析报文中的请求行数据
    HttpCode parse_request_line(char* text);
    // 主状态机解析报文中的请求头数据
    HttpCode parse_headers(char* text);
    // 主状态机解析报文中的请求内容
    HttpCode parse_content(char* text);
    // 生成响应报文
    HttpCode do_request();
    // start_line_是已经解析的字符
    // get_line用于将指针向后偏移，指向未处理的字符
    // start_line_是行在buffer中的起始位置，将该位置后面的数据赋给text
    // 此时状态机已提前将前一行的末尾字符\r\n变为\0\0，所以text可以直接取出完整的行进行解析
    char* get_line() {
        return read_buf_ + start_line_;
    }
    // 从状态机读取一行，分析是请求报文的哪一部分
    LineState parse_line();
    void unmap();
    
    // 根据响应报文格式，生成对应的8个部分，以下函数均由do_request调用
    bool add_response(const char* format, ...);
    bool add_content(const char* format);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

    void jump_to(const char* url);
    
    int sockfd_;
    sockaddr_in address_;
    
    char read_buf_[READ_BUFFER_SIZE];       // 存储读取的请求报文数据
    int read_idx_;                          // 缓冲区read_buf_中数据的最后一个字节的下一个位置
    int checked_idx_;                       // read_buf_读取的位置
    int start_line_;                        // read_buf_中已经解析的字符个数
    
    char write_buf_[WRITE_BUFFER_SIZE];     // 存储发出的响应报文数据
    int write_idx_;                         // 指示wirte_buf_中数据的长度
    
    CheckState check_state_;                // 主状态机的状态
    Method method_;                         // 请求方法

    // 以下为解析请求报文中对应的6个变量
    char real_file_[FILENAME_LEN];          // 存储读取文件的名称
    char* url_;
    char* version_;
    char* host_;
    int content_length_;
    bool linger_;

    char* file_address_;                    // 读取服务器上的文件地址
    struct stat file_stat_;
    struct iovec iv_[2];                    // io向量机制iovec
    int iv_count_;
    int cgi_;                               // 是否启用POST
    char* content_;                         // 存储请求体数据
    int bytes_unsent_;                      // 未发送字节数
    int bytes_sent_;                        // 已发送字节数

    
    // 网站根目录，文件夹内存放请求的资源和跳转的html文件
    // 当浏览器出现连接重置时，可能时网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    const char* root_dir_;

    // 将表中的用户名和密码放入
    std::unordered_map<std::string, std::string> users_;
    bool trig_mode_;
    bool close_log_;

    char username_[128];
    char password_[128];
    char db_name_[128];
};

#endif

#ifndef LOG_H
#define LOG_H

#include "pch.h"

#include "config.h"
#include "blocking_queue.h"

// 这四个宏定义在其他文件中使用，主要用于不同类型的日志输出
#define LOG_DEBUG(format, ...)\
    if (Config::close_log_) {\
        Log::get_instance()->write_log(0, format, ##__VA_ARGS__);\
        Log::get_instance()->flush();\
    }
    
#define LOG_INFO(format, ...)\
    if (Config::close_log_) {\
        Log::get_instance()->write_log(1, format, ##__VA_ARGS__);\
        Log::get_instance()->flush();\
    }

#define LOG_WARN(format, ...)\
    if (Config::close_log_) {\
        Log::get_instance()->write_log(2, format, ##__VA_ARGS__);\
        Log::get_instance()->flush();\
    }

#define LOG_ERROR(format, ...)\
    if (Config::close_log_) {\
        Log::get_instance()->write_log(3, format, ##__VA_ARGS__);\
        Log::get_instance()->flush();\
    }
    

class Log {
public:
    // C++11以后，使用局部变量懒汉不用加锁
    static Log* get_instance() {
        static Log instance;
        return &instance;
    }
    // 异步写日志公有方法，调用私有方法async_write_log
    static void* flush_log_thread(void* args) {
        return Log::get_instance()->async_write_log();
    }

    // 可选择的参数有日志文件，日志缓冲区大小，最大行数以及最长日志条队列
    bool init(const char* filename, bool close_log = false, int buf_size = 8192, int max_lines = 5000000, int max_queue_size = 0);
    // 将输出内容按照标准格式整理
    void write_log(int level, const char* format, ...);
    // 强制刷新缓冲区
    void flush() {
        mutex_.lock();
        // 强制刷新写入流缓冲区
        fflush(fp_);
        mutex_.unlock();
    }

private:
    ~Log();

    // 异步写日志方法
    void* async_write_log();

    FILE* fp_;                              // 打开log的文件指针
    char dir_[128];                         // 路径名
    char filename_[128];                    // log文件名
    int max_lines_;                         // 日志最大行数        
    char* buf_;                             // 日志缓冲区，要输出的内容
    int buf_size_;                          // 日志缓冲区大小
    long long count_;                       // 日志行数记录
    int date_;                              // 按天分文件，记录当前时间是哪一天

    bool isasync_;                          // 是否同步标志位
    BlockingQueue<std::string>* log_queue_; // 阻塞队列
    Mutex mutex_;                           // 互斥锁
    bool close_log_;                         // 关闭日志
};

#endif

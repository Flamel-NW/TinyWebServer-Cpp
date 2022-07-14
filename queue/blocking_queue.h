#ifndef BLOCKING_QUEUE_H
#define BLOCKING_QUEUE_H

#include "pch.h"

#include "lock.h"

/**
 * @brief 循环数组实现的阻塞序列，back_ = (back_ + 1) % max_size_;
 * 线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
 */
template <typename T>
class BlockingQueue {
public:
    // 初始化私有成员
    BlockingQueue(int max_size = 1000) 
        : max_size_(max_size), size_(0), front_(-1), back_(-1) {
        if (max_size <= 0) {
            STDERR_FUNC_LINE();
            exit(EXIT_FAILURE);
        }
        // 构造函数创建循环数组
        array_ = new T[max_size_];
    }

    ~BlockingQueue() {
        mutex_.lock();
        if (array_ != nullptr)
            delete[] array_;
        mutex_.unlock();
    }

    void clear() {
        mutex_.lock();
        size_ = 0;
        front_ = back_ = -1;
        mutex_.unlock();
    }

    bool full() {
        mutex_.lock();
        if (size_ >= max_size_) {
            mutex_.unlock();
            return true;
        }
        mutex_.unlock();
        return false;
    }

    bool empty() {
        mutex_.lock();
        if (size_ == 0) {
            mutex_.unlock();
            return true;
        }
        mutex_.unlock();
        return false;
    }

    bool front(T& value) {
        mutex_.lock();
        if (size_ == 0) {
            mutex_.unlock();
            return false;
        }
        value = array_[front_];
        mutex_.unlock();
        return true;
    }

    bool back(T& value) {
        mutex_.lock();
        if (size_ == 0) {
            mutex_.unlock();
            return false;
        }
        value = array_[back_];
        mutex_.unlock();
        return true;
    }

    int get_size() {
        int temp = 0;
        mutex_.lock();
        temp = size_;
        mutex_.unlock();
        return temp;
    }

    int get_max_size() {
        int temp = 0;
        mutex_.lock();
        temp = max_size_;
        mutex_.unlock();
        return temp;
    }

    /**
     * @brief 往队列添加元素，需要将所有使用队列的线程先唤醒
     * 当有元素push进队列，相当于生产者生产了一个元素
     * 若当前没有线程等待条件变量，则唤醒无意义
     * 
     * @param item 
     * @return true 
     * @return false 
     */
    bool push(const T& item) {
        mutex_.lock();
        if (size_ >= max_size_) {
            cond_.broadcast();
            mutex_.unlock();
            return false;
        }
        // 将新增数据放在循环数组的对应位置
        back_ = (back_ + 1) % max_size_;
        array_[back_] = item;
        size_++;

        cond_.broadcast();
        mutex_.unlock();
        return true;
    }

    // pop时，如果当前队列没有元素，将会等待条件变量
    bool pop(T& item) {
        mutex_.lock();
        // 多个消费者的时候，这里要是用while而不是if
        while (size_ <= 0) {
            // 当重新抢到互斥锁，Cond::wait返回为0
            if (!cond_.wait(mutex_.get())) {
                mutex_.unlock();
                return false;
            }
        }
        
        // 取出循环队列首的元素
        front_ = (front_ + 1) % max_size_;
        item = array_[front_];
        size_--;
        mutex_.unlock();
        return true; 
    }

    /**
     * @brief 增加了超时处理，再项目中没有用到
     * 在Cond::wait改为Cond::timewait，增加了等待的事件，只指定时间内能抢到互斥锁即可
     * 其他逻辑不变
     * 
     * @param item 
     * @param timeout 单位ms 
     * @return true 
     * @return false 
     */
    bool pop(T& item, int timeout) {
        struct timespec t = { 0, 0 };
        struct timeval now = { 0, 0 };
        gettimeofday(&now, nullptr);
        mutex_.lock();
        if (size_ <= 0) {
            t.tv_sec = now.tv_sec + timeout / 1000;
            t.tv_nsec = (timeout % 1000) * 1000;
            if (!cond_.timewait(mutex_.get(), t)) {
                mutex_.unlock();
                return false;
            }
        }
        if (size_ <= 0) {
            mutex_.unlock();
            return false;
        }

        front_ = (front_ + 1) % max_size_;
        item = array_[front_];
        size_--;
        mutex_.unlock();
        return true;
    }

private:
    Mutex mutex_;
    Cond cond_;

    T* array_;
    int size_;
    int max_size_;
    int front_;
    int back_;
};

#endif

#include "timer.h"
#include <cstdio>

// 常规销毁链表
TimerList::~TimerList() {
    TimerUtil* temp = head_;
    while (temp != nullptr) {
        head_ = temp->next;
        delete temp;
        temp = head_;
    }
}

// 添加定时器,内部调用私有成员add_timer
void TimerList::add_timer(TimerUtil* timer) {
    if (timer == nullptr)
        return;
    if (head_ == nullptr) {
        head_ = tail_ = timer;
        return;
    }
    // 如果新的定时器超时时间小于当前头部节点
    // 直接将当前定时器节点作为头部节点
    if (timer->expire < head_->expire) {
        timer->next = head_;
        head_->pre = timer;
        head_ = timer;
        return;
    }
    // 否则调用私有成员,调整内部节点
    add_timer(timer, head_);
}

// 调整定时器，任务发生变化时，调整的定时器在链表中的位置
void TimerList::modify_timer(TimerUtil* timer) {
    if (timer == nullptr) 
        return;
    TimerUtil* temp = timer->next;
    // 被调整的定时器在链表尾部
    // 定时器超时值仍然小于下一个定时器超时值，不调整
    if (temp == nullptr || (timer->expire < temp->expire))
        return;
    // 被调整定时器是链表头节点，将定时器取出，重新插入
    if (temp == head_) {
        head_ = head_->next;
        head_->pre = nullptr;
        timer->next = nullptr;
        add_timer(timer, head_);
    // 被调整定时器在内部，将定时器取出，重新插入
    } else {
        timer->pre->next = timer->next;
        timer->next->pre = timer->pre;
        add_timer(timer, timer->next);
    }
}

// 删除定时器
void TimerList::del_timer(TimerUtil* timer) {
    if (timer == nullptr)
        return;
    // 链表中只有一个定时器，需要删除该定时器
    if ((timer == head_) && (timer == tail_)) {
        delete timer;
        head_ = tail_ = nullptr;
        return;
    }
    // 被删的定时器为头节点
    if (timer == head_) {
        head_ = head_->next;
        head_->pre = nullptr;
        delete timer;
        return;
    }
    // 被删的定时器为尾节点
    if (timer == tail_) {
        tail_ = tail_->pre;
        tail_->next = nullptr;
        delete timer;
        return;
    }
    // 被删除的定时器在链表内部，常规链表节点删除
    timer->pre->next = timer->next;
    timer->next->pre = timer->pre;
    delete timer;
}

// 定时任务处理函数
void TimerList::tick() {
    if (head_ == nullptr) 
        return;

    // 获取当前时间
    time_t curr = time(nullptr);
    // 遍历定时器链表
    TimerUtil* temp = head_;
    while (temp) {
        // 链表容器为升序排列
        // 当前时间小于定时器超时时间，后面的定时器也没有到期
        if (curr < temp->expire)
            break;
        // 当前定时器到期，则调用回调函数，执行定时事件
        temp->callback(temp->user_data);
        // 将处理后的定时器从链表容器中删除，并重置头节点
        head_ = temp->next;
        if (head_ != nullptr)
            head_->pre = nullptr;
        delete temp;
        temp = head_;
    }
}

// 私有成员，被公有成员add_timer和modify_timer调用
// 主要用于调整链表内部节点
void TimerList::add_timer(TimerUtil* timer, TimerUtil* head) {
    TimerUtil* pre = head;
    TimerUtil* temp = pre->next;
    // 遍历当前节点之后的链表，按照超时时间找到目标定时器对应的位置，常规双向链表插入操作
    while (temp) {
        if (timer->expire < temp->expire) {
            pre->next = timer;
            timer->next = temp;
            temp->pre = timer;
            timer->pre = pre;
            break;
        }
        pre = temp;
        temp = temp->next;
    }
    // 遍历完发现，目标定时器需要放到尾节点处
    if (temp == nullptr) {
        pre->next = timer;
        timer->pre = pre;
        timer->next = nullptr;
        tail_ = timer;
    }
}

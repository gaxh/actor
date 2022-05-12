#ifndef __ACTOR_SCHEDULER_TIMER_H__
#define __ACTOR_SCHEDULER_TIMER_H__

#include <functional>

class SpinLock;

// 毫秒unix时间戳
unsigned long actor_scheduler_timer_current_time();

class ActorSchedulerTimer {
public:
    using TIMER_ID = unsigned long long;
    using TIMER_CALLBACK = std::function<void(TIMER_ID)>;

    static constexpr int MAX_TRIGGER_PER_UPDATE = 100;

    void Init(size_t pool_capacity = 0);

    void Destroy();

    // 创建新的定时器
    TIMER_ID New(SpinLock &lock, unsigned long delay_ms, TIMER_CALLBACK cb);

    // 更新，返回触发的定时器数量
    int Update(SpinLock &lock);

private:
    struct IMPL;
    IMPL *m_impl = NULL;
};

#endif

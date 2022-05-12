#ifndef __ACTOR_MESSAGE_H__
#define __ACTOR_MESSAGE_H__

#include <memory>
#include <functional>
#include <queue>
#include <atomic>

#include "spinlock.h"

// 系统消息类型，用户不能占用
enum ActorMessageReservedType : unsigned {
    BEGIN = 0xffff0000,
    ACTOR_START,
    ACTOR_STOP,
    ACTOR_TIMER,
    END,
};

struct ActorMessage {
    unsigned from_id; // 消息是从哪个actor发来的
    unsigned long seq_id; // 消息序列号
    unsigned long long reserved;
    unsigned type; // 消息类型
    std::shared_ptr<void> payload; // 消息内容
};

class ActorMessageQueue : public std::enable_shared_from_this<ActorMessageQueue> {
public:
    ActorMessageQueue(unsigned id, unsigned max_priority);
    ~ActorMessageQueue();

    void Push(const ActorMessage &m, unsigned priority);

    bool Pop(ActorMessage &m, unsigned &priority);

    size_t Size();

    void Flush(std::function<void(const ActorMessage &)> flush_cb);

    unsigned Id();

    bool Trylock();

    void Unlock();

    static std::shared_ptr<ActorMessageQueue> Acquire();

    void Release();

private:
    unsigned m_id; // 消息队列所属actor
    unsigned m_max_priority;
    std::vector<std::queue<ActorMessage>> m_q;
    SpinLock m_lock;
    std::atomic_flag m_processing = ATOMIC_FLAG_INIT;

    enum class State {
        IDLE,
        QUEUED,
        ACQUIRED,
    };

    State m_state = State::IDLE;

    struct GlobalPendingQueue {
        std::queue<std::shared_ptr<ActorMessageQueue>> q;
        SpinLock lock;
    };

    static GlobalPendingQueue s_pending_queue;
};

#endif


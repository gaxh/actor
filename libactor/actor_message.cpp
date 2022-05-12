#include "actor_message.h"

ActorMessageQueue::ActorMessageQueue(unsigned id, unsigned max_priority) :
    m_id(id), m_max_priority(max_priority) {

    m_q.resize(max_priority);
}

ActorMessageQueue::~ActorMessageQueue() {

}

void ActorMessageQueue::Push(const ActorMessage &m, unsigned priority) {
    if(priority >= m_max_priority) {
        return;
    }

    SpinLockGuard g(m_lock);
    m_q[priority].push(m);

    if(m_state == State::IDLE) {
        m_state = State::QUEUED;

        SpinLockGuard g2(s_pending_queue.lock);
        s_pending_queue.q.push(shared_from_this());
    }
}

bool ActorMessageQueue::Pop(ActorMessage &m, unsigned &priority) {
    SpinLockGuard g(m_lock);

    for(unsigned i = 0; i < m_max_priority; ++i) {
        std::queue<ActorMessage> &q = m_q[i];

        if(!q.empty()) {
            m = q.front();
            priority = i;
            q.pop();

            return true;
        }
    }

    return false;
}

size_t ActorMessageQueue::Size() {
    SpinLockGuard g(m_lock);
    size_t sz = 0;

    for(unsigned i = 0; i < m_max_priority; ++i) {
        sz += m_q[i].size();
    }

    return sz;
}

void ActorMessageQueue::Flush(std::function<void(const ActorMessage &)> flush_cb) {
    SpinLockGuard g(m_lock);

    for(unsigned i = 0; i < m_max_priority; ++i) {
        std::queue<ActorMessage> &q = m_q[i];

        while(!q.empty()) {
            ActorMessage msg = q.front();
            q.pop();

            flush_cb(msg);
        }
    }
}

unsigned ActorMessageQueue::Id() {
    return m_id;
}

bool ActorMessageQueue::Trylock() {
    return m_processing.test_and_set() == false;
}

void ActorMessageQueue::Unlock() {
    m_processing.clear();
}

std::shared_ptr<ActorMessageQueue>
ActorMessageQueue::Acquire() {
    // 这里获得锁的顺序，跟其他地方是反的
    // 所以第2个锁用trylock
    // 允许Acquire失败，即使存在可以Acquire的队列

    SpinLockGuard g(s_pending_queue.lock);

    if(s_pending_queue.q.empty()) {
        return NULL;
    }

    std::shared_ptr<ActorMessageQueue> q = s_pending_queue.q.front();

    SpinTryLockGuard t(q->m_lock);

    if(!t.Trylock()) {
        return NULL;
    }

    s_pending_queue.q.pop();
    q->m_state = State::ACQUIRED;

    return q;
}

void ActorMessageQueue::Release() {
    SpinLockGuard g(m_lock);

    for(unsigned i = 0; i < m_max_priority; ++i) {
        if(!m_q[i].empty()) {
            // queue is not empty
            // shell be push back to global pending queue
            m_state = State::QUEUED;
            SpinLockGuard g2(s_pending_queue.lock);

            s_pending_queue.q.push(shared_from_this());
            return;
        }
    }

    m_state = State::IDLE;
}

ActorMessageQueue::GlobalPendingQueue ActorMessageQueue::s_pending_queue;


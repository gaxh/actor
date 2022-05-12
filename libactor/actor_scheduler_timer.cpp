#include "actor_scheduler_timer.h"

#include <sys/time.h>
#include <time.h>
#include <assert.h>

#include <queue>
#include <unordered_map>
#include <vector>

#include "spinlock.h"

unsigned long actor_scheduler_timer_current_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return (unsigned long)tv.tv_sec * (unsigned long)1000 +
        (unsigned long)tv.tv_usec / (unsigned long)1000;
}

struct ActorSchedulerTimer::IMPL {

    void Init(size_t pool_capacity) {
        m_pool_capacity = pool_capacity;
        m_pool.reserve(pool_capacity);
    }

    void Destroy() {
        while(!m_heap.empty()) {
            TimerObj *obj = m_heap.top();
            m_heap.pop();

            ReleaseTimerObj(obj);
        }

        for(auto iter = m_pool.begin(); iter != m_pool.end(); ++iter) {
            TimerObj *obj = *iter;

            DeleteTimerObj(obj);
        }

        m_pool.clear();

    }

    TIMER_ID New(SpinLock &lock, unsigned long delay_ms, TIMER_CALLBACK cb) {
        unsigned long now = actor_scheduler_timer_current_time();

        TIMER_ID id;

        {
            SpinLockGuard g(lock);

            id = NextTimerId();
            TimerObj *obj = AcquireTimerObj();
            obj->id = id;
            obj->create_ts = now;
            obj->timeout_ts = now + delay_ms;
            obj->cb = cb;
            m_heap.emplace(obj);
        }

        return id;
    }

    int Update(SpinLock &lock) {
        int ret = 0;
        unsigned long now = actor_scheduler_timer_current_time();

        while(ret < MAX_TRIGGER_PER_UPDATE) {

            TIMER_CALLBACK cb;
            TIMER_ID id;

            {
                SpinLockGuard g(lock);

                if(m_heap.empty() || now < m_heap.top()->timeout_ts) {
                    break;
                }

                TimerObj *obj = m_heap.top();
                m_heap.pop();

                cb = obj->cb;
                id = obj->id;

                ReleaseTimerObj(obj);
            }

            if(cb) {
                cb(id);
            }

            ++ret;
        }

        return ret;
    }

    struct TimerObj;

    TimerObj *AcquireTimerObj() {
        TimerObj *obj = NULL;
        if(m_pool.empty()) {
            obj = NewTimerObj();
        } else {
            obj = m_pool.back();
            m_pool.pop_back();
        }

        obj = new(obj) TimerObj();
        return obj;
    }

    void ReleaseTimerObj(TimerObj *obj) {
        obj->~TimerObj();

        if(m_pool.size() >= m_pool_capacity) {
            DeleteTimerObj(obj);
        } else {
            m_pool.emplace_back(obj);
        }
    }

    TimerObj *NewTimerObj() {
        return (TimerObj *)malloc(sizeof(TimerObj));
    }

    void DeleteTimerObj(TimerObj *obj) {
        free(obj);
    }

    struct TimerObj {
        TIMER_ID id;
        unsigned long create_ts;
        unsigned long timeout_ts;
        TIMER_CALLBACK cb;
    };

    struct TimerObjCompare {
        bool operator()(const TimerObj *x, const TimerObj *y) {
            return x->timeout_ts != y->timeout_ts ?
                (x->timeout_ts > y->timeout_ts) :
                (x->id > y->id); // id 是自增的，可以拿来做比较，让先注册的timer先执行
        }
    };

    TIMER_ID NextTimerId() {
        TIMER_ID id = m_next_timer_id + 1;

        assert(id > m_next_timer_id); // 禁止溢出
        m_next_timer_id = id;

        return id;
    }

    TIMER_ID m_next_timer_id = 0;
    std::priority_queue<TimerObj *, std::vector<TimerObj *>, TimerObjCompare> m_heap;

    std::vector<TimerObj *> m_pool;
    size_t m_pool_capacity;
};

void ActorSchedulerTimer::Init(size_t pool_capacity) {
    if(m_impl == NULL) {
        m_impl = new ActorSchedulerTimer::IMPL();
        m_impl->Init(pool_capacity);
    }
}

void ActorSchedulerTimer::Destroy() {
    if(m_impl) {
        m_impl->Destroy();
        delete m_impl;
        m_impl = NULL;
    }
}

ActorSchedulerTimer::TIMER_ID
ActorSchedulerTimer::New(SpinLock &lock, unsigned long delay_ms, ActorSchedulerTimer::TIMER_CALLBACK cb) {
    return m_impl->New(lock, delay_ms, cb);
}

int ActorSchedulerTimer::Update(SpinLock &lock) {
    return m_impl->Update(lock);
}


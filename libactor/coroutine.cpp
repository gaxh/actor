#include "coroutine.h"

#include <unordered_map>
#include <vector>

#include <assert.h>
#include <ucontext.h>

#include <bits/wordsize.h>
#if __WORDSIZE == 64
#define __COROUTINE_LONG_SIZE__ 64
#elif __WORDSIZE == 32
#define __COROUTINE_LONG_SIZE__ 32
#else
#error "long size is not supported"
#endif

static constexpr int COROUTINE_DOG_TAG = 0xcccccccc;

struct CoroutineScheduler::IMPL {
void Init(size_t stack_capacity, size_t pool_capacity) {
        m_stack_capacity = stack_capacity;
        m_pool_capacity = pool_capacity;
        m_pool.reserve(pool_capacity);
    }

    void Destroy() {
        assert(m_current == NULL); // can only call destroy at main coroutine

        for(auto iter = m_coroutines.begin(); iter != m_coroutines.end(); ++iter) {
            Coroutine *co = iter->second;
            ReleaseCoroutineObject(co);
        }

        m_coroutines.clear();

        for(auto iter = m_pool.begin(); iter != m_pool.end(); ++iter) {
            Coroutine *co = *iter;
            DeleteCoroutineObject(co);
        }

        m_pool.clear();
    }

    COROUTINE_ID New(COROUTINE_FUNC fn) {
        COROUTINE_ID id = GenNextId();

        m_coroutines[id] = AcquireCoroutineObject(id, fn);

        return id;
    }

    void Resume(COROUTINE_ID id) {
        m_last_suspended = false;

        if(m_current != NULL) {
            // can only call Resume at main coroutine
            COROUTINE_ERROR("Resume failed: can only Resume at main coroutine");
            return;
        }

        auto iter = m_coroutines.find(id);

        if(iter == m_coroutines.end()) {
            COROUTINE_ERROR("Resume failed: can not get coroutine object %llu", id);
            return;
        }

        Coroutine *co = iter->second;
        CoroutineStatus status = co->m_status;

        if(status == CoroutineStatus::READY) {
            getcontext(&co->m_context);
            co->m_context.uc_stack.ss_sp = co->m_call_stack;
            co->m_context.uc_stack.ss_size = co->m_stack_capacity;
            co->m_context.uc_link = &m_main_context;
            co->m_status = CoroutineStatus::RUNNING;
            m_current = co;

            unsigned long p = (unsigned long)this;
#if __COROUTINE_LONG_SIZE__ == 64
            makecontext(&co->m_context, (void (*)(void))CoroutineMain, 2, (unsigned int)p, (unsigned int)(p >> 32));
#elif __COROUTINE_LONG_SIZE__ == 32
            makecontext(&co->m_context, (void (*)(void))CoroutineMain, 2, (unsigned int)p, 0);
#else
#error "long size is not supported"
#endif
            swapcontext(&m_main_context, &co->m_context);
        } else if(status == CoroutineStatus::SUSPENDED) {
            co->m_status = CoroutineStatus::RUNNING;
            m_current = co;
            swapcontext(&m_main_context, &co->m_context);
        } else {
            COROUTINE_ERROR("Resume failed: coroutine status is %d", (int)status);
        }

        // after swap back, delete DEAD coroutine

        if(co->m_status == CoroutineStatus::DEAD) {
            m_coroutines.erase(co->m_id);
            ReleaseCoroutineObject(co);
        } else {
            m_last_suspended = true;
        }
    }

    void Yield() {
        if(m_current == NULL) {
            // main coroutine can not yield
            COROUTINE_ERROR("Yield failed: can not yield main coroutine");
            return;
        }

        Coroutine *co = m_current;

        // check if call stack is valid (possibly)
        assert(co->m_dog_tag == COROUTINE_DOG_TAG);

        co->m_status = CoroutineStatus::SUSPENDED;
        m_current = NULL;
        swapcontext(&co->m_context, &m_main_context);
    }

    CoroutineStatus Status(COROUTINE_ID id) {
        auto iter = m_coroutines.find(id);

        return iter != m_coroutines.end() ? iter->second->m_status : CoroutineStatus::DEAD;
    }

    COROUTINE_ID Current() {
        return m_current ? m_current->m_id : COROUTINE_MAIN;
    }

    size_t Size() {
        return m_coroutines.size();
    }

    size_t PoolSize() {
        return m_pool.size();
    }

    bool LastSuspended() {
        return m_last_suspended;
    }

    // private functions

    static void CoroutineMain(unsigned int l, unsigned int h) {
        unsigned long p;
#if __COROUTINE_LONG_SIZE__ == 64
        p = (unsigned long)l | (((unsigned long)h) << 32);
#elif __COROUTINE_LONG_SIZE__ == 32
        p = (unsigned long)l;
#else
#error "long size is not supported"
#endif
        CoroutineScheduler::IMPL *this_ptr = (CoroutineScheduler::IMPL *)p;

        Coroutine *co = this_ptr->m_current;
        co->m_fn();
        co->m_status = CoroutineStatus::DEAD; // will delete this coroutine after swap back to main coroutine

        this_ptr->m_current = NULL;
    }

    struct Coroutine;

    Coroutine *AcquireCoroutineObject(COROUTINE_ID id, COROUTINE_FUNC fn) {
        Coroutine *co = NULL;
        if(m_pool.empty()) {
            co = NewCoroutineObject();
        } else {
            co = m_pool.back();
            m_pool.pop_back();
        }

        co = new (co) Coroutine;

        co->m_id = id;
        co->m_fn = fn;
        co->m_scheduler = this;
        co->m_status = CoroutineStatus::READY;
        co->m_stack_capacity = m_stack_capacity;
        co->m_dog_tag = COROUTINE_DOG_TAG;

        return co;
    }

    void ReleaseCoroutineObject(Coroutine *co) {
        co->~Coroutine();

        if(m_pool.size() >= m_pool_capacity) {
            DeleteCoroutineObject(co);
        } else {
            m_pool.emplace_back(co);
        }
    }

    Coroutine *NewCoroutineObject() {
        size_t malloc_size = sizeof(struct Coroutine) + m_stack_capacity;

        return (Coroutine *)malloc(malloc_size);
    }

    void DeleteCoroutineObject(Coroutine *co) {
        free(co);
    }

    COROUTINE_ID GenNextId() {
        COROUTINE_ID next_id = m_next_id + 1;
        assert(next_id > m_next_id); // avoid overflow
        m_next_id = next_id;
        return next_id;
    }

    struct Coroutine {
        COROUTINE_ID m_id;
        COROUTINE_FUNC m_fn;
        ucontext_t m_context;
        CoroutineScheduler::IMPL *m_scheduler;
        CoroutineStatus m_status;
        size_t m_stack_capacity;
        int m_dog_tag;
        char m_call_stack[0];
    };

    std::unordered_map<COROUTINE_ID, Coroutine *> m_coroutines;

    std::vector<Coroutine *> m_pool;

    size_t m_stack_capacity;
    size_t m_pool_capacity;

    COROUTINE_ID m_next_id = COROUTINE_MAIN;
    ucontext_t m_main_context;

    Coroutine *m_current = NULL;
    bool m_last_suspended = false;
};

// interfaces of CoroutineScheduler

void CoroutineScheduler::Init(size_t stack_capacity, size_t pool_capacity) {
    if(m_impl) {
        COROUTINE_ERROR("init has been called");
        return;
    }

    m_impl = new IMPL();
    m_impl->Init(stack_capacity, pool_capacity);
}

void CoroutineScheduler::Destroy() {
    if(m_impl) {
        m_impl->Destroy();
        delete m_impl;
        m_impl = NULL;
    }
}

COROUTINE_ID CoroutineScheduler::New(COROUTINE_FUNC fn) {
    return m_impl->New(fn);
}

void CoroutineScheduler::Resume(COROUTINE_ID id) {
    m_impl->Resume(id);
}

void CoroutineScheduler::Yield() {
    m_impl->Yield();
}

CoroutineStatus CoroutineScheduler::Status(COROUTINE_ID id) {
    return m_impl->Status(id);
}

COROUTINE_ID CoroutineScheduler::Current() {
    return m_impl->Current();
}

size_t CoroutineScheduler::Size() {
    return m_impl->Size();
}

size_t CoroutineScheduler::PoolSize() {
    return m_impl->PoolSize();
}

bool CoroutineScheduler::LastSuspended() {
    return m_impl->LastSuspended();
}


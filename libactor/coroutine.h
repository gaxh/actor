#ifndef __COROUTINE_H__
#define __COROUTINE_H__

#include <functional>
#include <stddef.h>

enum class CoroutineStatus {
    UNKNOWN,
    READY,
    RUNNING,
    SUSPENDED,
    DEAD,
};

using COROUTINE_ID = unsigned long long;

using COROUTINE_FUNC = std::function<void()>;

constexpr COROUTINE_ID COROUTINE_MAIN = 0;

class CoroutineScheduler {
public:
    void Init(size_t stack_capacity, size_t pool_capacity = 0);

    void Destroy();

    COROUTINE_ID New(COROUTINE_FUNC);

    void Resume(COROUTINE_ID id);

    void Yield();

    CoroutineStatus Status(COROUTINE_ID id);

    COROUTINE_ID Current();

    size_t Size();

    size_t PoolSize();

private:
    struct IMPL;
    IMPL *m_impl = NULL;
};

#include <stdio.h>

#define COROUTINE_ERROR(fmt, args...)   fprintf(stderr, "[%s:%d:%s] " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ##args)

#endif

#ifndef __SPIN_LOCK_H__
#define __SPIN_LOCK_H__

#include <atomic>

class SpinLock {
public:
    void Lock();

    void Unlock();

    bool Trylock();
private:
    std::atomic_flag m_atomic_bool = ATOMIC_FLAG_INIT;
};

class SpinLockGuard {
public:
    SpinLockGuard(SpinLock &);

    ~SpinLockGuard();
private:
    SpinLock &m_lock;
};

class SpinTryLockGuard {
public:
    SpinTryLockGuard(SpinLock &);

    ~SpinTryLockGuard();

    bool Trylock();
private:
    SpinLock &m_lock;
    bool m_locked = false;
};

class RWLock {
public:
    void RLock();

    void RUnlock();

    void WLock();

    void WUnlock();
private:
    std::atomic<int> m_atomic_read = ATOMIC_VAR_INIT(0);
    std::atomic<int> m_atomic_write = ATOMIC_VAR_INIT(0);
};

class RLockGuard {
public:
    RLockGuard(RWLock &);

    ~RLockGuard();
private:
    RWLock &m_lock;
};

class WLockGuard {
public:
    WLockGuard(RWLock &);

    ~WLockGuard();
private:
    RWLock &m_lock;
};

#endif

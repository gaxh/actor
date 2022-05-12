#include "spinlock.h"

void SpinLock::Lock() {
    while(m_atomic_bool.test_and_set());
}

void SpinLock::Unlock() {
    m_atomic_bool.clear();
}

bool SpinLock::Trylock() {
    return m_atomic_bool.test_and_set() == false;
}

SpinLockGuard::SpinLockGuard(SpinLock &lock) : m_lock(lock) {
    m_lock.Lock();
}

SpinLockGuard::~SpinLockGuard() {
    m_lock.Unlock();
}

SpinTryLockGuard::SpinTryLockGuard(SpinLock &lock) : m_lock(lock) {
}

SpinTryLockGuard::~SpinTryLockGuard() {
    if(m_locked) {
        m_lock.Unlock();
    }
}

bool SpinTryLockGuard::Trylock() {
    m_locked = m_lock.Trylock();

    return m_locked;
}

void RWLock::RLock() {
    for(;;) {
        while(m_atomic_write.load());
        m_atomic_read.fetch_add(1);
        if(m_atomic_write.load()) {
            m_atomic_read.fetch_sub(1);
        } else {
            break;
        }
    }
}

void RWLock::RUnlock() {
    m_atomic_read.fetch_sub(1);
}

void RWLock::WLock() {
    for(;;) {
        int write_expected = 0;
        if(m_atomic_write.compare_exchange_weak(write_expected, 1)) {
            break;
        }
    }
    while(m_atomic_read.load());
}

void RWLock::WUnlock() {
    m_atomic_write.store(0);
}

RLockGuard::RLockGuard(RWLock &lock) : m_lock(lock) {
    m_lock.RLock();
}

RLockGuard::~RLockGuard() {
    m_lock.RUnlock();
}

WLockGuard::WLockGuard(RWLock &lock) : m_lock(lock) {
    m_lock.WLock();
}

WLockGuard::~WLockGuard() {
    m_lock.WUnlock();
}


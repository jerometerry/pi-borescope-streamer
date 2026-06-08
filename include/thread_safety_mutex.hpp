#pragma once
#include <mutex>
#include "thread_safety.hpp"

class CAPABILITY("mutex") Mutex {
public:
    void lock() ACQUIRE() { 
        m_.lock(); 
    }
    
    void unlock() RELEASE() { 
        m_.unlock(); 
    }
    
    bool try_lock() { 
        return m_.try_lock(); 
    }

private:
    std::mutex m_;
};

class SCOPED_CAPABILITY MutexLock {
public:
    explicit MutexLock(Mutex& mu) ACQUIRE(mu) : mu_(mu) {
        mu_.lock();
    }
    
    ~MutexLock() RELEASE() {
        mu_.unlock();
    }

    // Explicitly delete copy and assignment operators.
    // Lock guards strictly govern hardware mutexes and cannot be duplicated.
    MutexLock(const MutexLock&) = delete;
    MutexLock& operator=(const MutexLock&) = delete;
    MutexLock(MutexLock&&) = delete;
    MutexLock& operator=(MutexLock&&) = delete;

private:
    // NOLINT suppresses the warning because non-assignability is the deliberate architectural goal.
    Mutex& mu_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};
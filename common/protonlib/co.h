#pragma once
#include <inttypes.h>
#include <assert.h>
#include <errno.h>

namespace proton
{
    int init();
    int fini();

    struct thread;
    extern thread* CURRENT;

    enum states
    {
        READY = 0,      // ready to run
        RUNNING = 1,    // CURRENTly running
        WAITING = 2,    // waiting for some events
        DONE = 4,       // finished the whole life-cycle
    };

    typedef void* (*thread_entry)(void*);
    const uint64_t DEFAULT_STACK_SIZE = 8 * 1024 * 1024;
    thread* thread_create(thread_entry start, void* arg,
                          uint64_t stack_size = DEFAULT_STACK_SIZE);

    // switching to other threads (without going into sleep queue)
#if defined(NEW) || defined(CACSTH)
    __attribute__((preserve_none))
#endif
    void thread_yield();

    // Threads are join-able *only* through their join_handle.
    // Once join is enabled, the thread will remain existing until being joined.
    // Failing to do so will cause resource leak.
    struct join_handle;
    join_handle* thread_enable_join(thread* th, bool flag = true);
    void thread_join(join_handle* jh);

#if defined(NEW) || defined(CACSTH)
    __attribute__((preserve_none))
#endif
    int thread_usleep(uint64_t useconds);
#if defined(NEW) || defined(CACSTH)
    __attribute__((preserve_none))
#endif
    void thread_interrupt(thread* th, int error_number);
};


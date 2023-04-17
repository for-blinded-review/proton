#define protected public
#include "co.h"
#undef protected
#include <memory.h>
#include <assert.h>
#include <errno.h>
#include <vector>
#include <new>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include <sys/time.h>
#include "common/list.h"
#include "alog.h"

#include "context.h"

namespace proton
{
    static std::condition_variable idle_sleep;
    static int default_idle_sleeper(uint64_t usec)
    {
        static std::mutex mutex;
        std::unique_lock<std::mutex> lock(mutex);
        auto us = ((int64_t&)usec) < 0 ?
            std::chrono::microseconds::max() :
            std::chrono::microseconds(usec);
        idle_sleep.wait_for(lock, us);
        return 0;
    }
    typedef int (*IdleSleeper)(uint64_t usec);
    IdleSleeper idle_sleeper = &default_idle_sleeper;
    void set_idle_sleeper(IdleSleeper sleeper)
    {
        idle_sleeper = sleeper ? sleeper : &default_idle_sleeper;
    }
    IdleSleeper get_idle_sleeper()
    {
        return idle_sleeper;
    }

    class waitq
    {
    public:
        int wait(uint64_t timeout = -1);
        void resume(thread* th);            // `th` must be waiting in this waitq!
        void resume_one()
        {
            if (q)
                resume(q);
        }
        waitq() = default;
        waitq(const waitq& rhs) = delete;   // not allowed to copy construct
        waitq(waitq&& rhs)
        {
            q = rhs.q;
            rhs.q = nullptr;
        }
        ~waitq()
        {
            assert(q == nullptr);
        }

    protected:
        thread* q = nullptr;                // the first thread in queue, if any
    };

    class mutex : protected waitq
    {
    public:
        void unlock();
        int lock(uint64_t timeout = -1);        // threads are guaranteed to get the lock
        int try_lock(uint64_t timeout = -1)     // in FIFO order, when there's contention
        {
            return owner ? -1 : lock();
        }
        ~mutex()
        {
            assert(owner == nullptr);
        }

    protected:
        thread* owner = nullptr;
    };

    class scoped_lock
    {
    public:
        // do lock() if `do_lock` > 0, and lock() can NOT fail if `do_lock` > 1
        explicit scoped_lock(mutex& mutex, uint64_t do_lock = 2) : m_mutex(&mutex)
        {
            if (do_lock > 0)
                lock(do_lock > 1);
        }
        scoped_lock(scoped_lock&& rhs) : m_mutex(rhs.m_mutex)
        {
            m_locked = rhs.m_locked;
            rhs.m_mutex = nullptr;
            rhs.m_locked = false;
        }
        scoped_lock(const scoped_lock& rhs) = delete;
        int lock(bool must_lock = true)
        {
            int ret; do
            {
                ret = m_mutex->lock();
                m_locked = (ret == 0);
            } while (!m_locked && must_lock);
            return ret;
        }
        bool locked()
        {
            return m_locked;
        }
        operator bool()
        {
            return locked();
        }
        void unlock()
        {
            if (m_locked)
            {
                m_mutex->unlock();
                m_locked = false;
            }
        }
        ~scoped_lock()
        {
            if (m_locked)
                m_mutex->unlock();
        }
        void operator = (const scoped_lock& rhs) = delete;
        void operator = (scoped_lock&& rhs) = delete;

    protected:
        mutex* m_mutex;
        bool m_locked;
    };

    class condition_variable : protected waitq
    {
    public:
        int wait(scoped_lock& lock, uint64_t timeout = -1);
        int wait_no_lock(uint64_t timeout = -1);
        void notify_one();
        void notify_all();
    };

    class semaphore : protected waitq   // NOT TESTED YET
    {
    public:
        explicit semaphore(uint64_t count) : m_count(count) { }
        int wait(uint64_t count, uint64_t timeout = -1);
        int signal(uint64_t count);

    protected:
        uint64_t m_count;
    };

    // to be different to timer flags
    // mark flag should be larger than 999, and not touch lower bits
    // here we selected
    constexpr int RLOCK=0x1000;
    constexpr int WLOCK=0x2000;
    class rwlock : protected waitq
    {
    public:
        int lock(int mode, uint64_t timeout = -1);
        int unlock();
    protected:
        int64_t state = 0;
    };

    // Saturating addition, primarily for timeout caculation
    __attribute__((always_inline)) inline
    uint64_t sat_add(uint64_t x, uint64_t y)
    {
        register uint64_t z asm ("rax");
        asm("add %2, %1; sbb %0, %0; or %1, %0;" : "=r"(z), "+r"(x) : "r"(y) : "cc");
        return z;
    }

    // Saturating subtract, primarily for timeout caculation
    __attribute__((always_inline)) inline
    uint64_t sat_sub(uint64_t x, uint64_t y)
    {
        register uint64_t z asm ("rax");
        asm("xor %0, %0; sub %2, %1; cmovnc %1, %0;" : "=r"(z), "+r"(x) : "r"(y) : "cc");
        return z;
    }


    class scoped_rwlock
    {
    public:
        scoped_rwlock(rwlock &rwlock, int lockmode) {
            m_rwlock = &rwlock;
            m_locked = (0 == m_rwlock->lock(lockmode));
        }
        bool locked() {
            return m_locked;
        }
        operator bool() {
            return locked();
        }
        int lock(int mode, bool must_lock = true) {
            int ret; do
            {
                m_locked = (0 == m_rwlock->lock(mode));
            } while (!m_locked && must_lock);
            return ret;
        }
        ~scoped_rwlock() {
            m_rwlock->unlock();
        }
    protected:
        rwlock* m_rwlock;
        bool m_locked;
    };

    class Stack
    {
    public:
        template<typename F>
        void init(void* ptr, F ret2func)
        {
            _ptr = ptr;
            push(0);
            push(ret2func);
#if !defined(NEW) && !defined(CACSTH)
            push(ret2func);
            // make room for rbx, rbp, r12~r15
            (uint64_t*&)_ptr -= 6;
#endif
//            auto rsp = _ptr;
//            LOG_DEBUG(th, VALUE(rsp));
        }
//        void* cache_line_align()
//        {
//            auto sp = (uint64_t)_ptr;
//            sp = ((sp >> 6) - 1) << 6;
//            _ptr = (void*)sp;
//        }
        void** pointer_ref()
        {
            return &_ptr;
        }
        void push(uint64_t x)
        {
            *--(uint64_t*&)_ptr = x;
        }
        template<typename T>
        void push(const T& x)
        {
            push((uint64_t)x);
        }
        uint64_t pop()
        {
            return *((uint64_t*&)_ptr)++;
        }
        uint64_t& operator[](int i)
        {
            return static_cast<uint64_t*>(_ptr)[i];
        }
        void* _ptr;
    };

    struct thread;
    typedef intrusive_list<thread> thread_list;
    struct thread : public intrusive_list_node<thread>
    {
        Stack stack;
        uint64_t ret_addr;
        states state = states::READY;
        int error_number = 0;
        int idx;                            /* index in the sleep queue array */
        int flags = 0;
        int reserved;
        bool joinable = false;
        bool shutting_down = false;         // the thread should cancel what is doing, and quit
                                            // current job ASAP; not allowed to sleep or block more
                                            // than 10ms, otherwise -1 will be returned and errno == EPERM

        thread_list* waitq = nullptr;       /* the q if WAITING in a queue */

        thread_entry start;
        void* arg;
        void* retval;
        void* go() { return retval = start(arg); }
        char* buf;

        uint64_t ts_wakeup = 0;             /* Wakeup time when thread is sleeping */
        condition_variable cond;            /* used for join, or timer REUSE */

        int set_error_number()
        {
            if (error_number)
            {
                errno = error_number;
                error_number = 0;
                return -1;
            }
            return 0;
        }

        void dequeue_ready()
        {
            if (waitq) {
                waitq->erase(this);
                waitq = nullptr;
            } else {
                assert(this->single());
            }
            state = states::READY;
            CURRENT->insert_tail(this);
        }

        bool operator < (const thread &rhs)
        {
            return this->ts_wakeup < rhs.ts_wakeup;
        }

        void dispose()
        {
            delete [] buf;
        }
    };
    static_assert(offsetof(thread, stack) == 16, "...");
    static_assert(offsetof(thread, ret_addr) == 24, "...");

    struct join_handle: public thread {};

    class SleepQueue
    {
    public:
        std::vector<thread *> q;

        [[gnu::always_inline]]
        thread* front() const
        {
            return q.empty() ? nullptr : q.front();
        }

        [[gnu::always_inline]]
        bool empty() const
        {
            return q.empty();
        }

        [[gnu::always_inline]]
        int push(thread *obj)
        {
            q.push_back(obj);
            obj->idx = q.size() - 1;
            up(obj->idx);
            return 0;
        }

        [[gnu::always_inline]]
        thread* pop_front()
        {
            auto ret = q[0];
            q[0] = q.back();
            q[0]->idx = 0;
            q.pop_back();
            down(0);
            return ret;
        }

        [[gnu::always_inline]]
        int pop(thread *obj)
        {
            if (obj->idx == -1) return -1;
            if ((size_t)obj->idx == q.size() - 1){
                q.pop_back();
                obj->idx = -1;
                return 0;
            }

            auto id = obj->idx;
            q[obj->idx] = q.back();
            q[id]->idx = id;
            q.pop_back();
            if (!up(id)) down(id);
            obj->idx = -1;

            return 0;
        }

        __attribute__((always_inline))
        void update_node(int idx, thread *&obj)
        {
            q[idx] = obj;
            q[idx]->idx = idx;
        }

        // compare m_nodes[idx] with parent node.
        [[gnu::always_inline]]
        bool up(int idx)
        {
            auto tmp = q[idx];
            bool ret = false;
            while (idx != 0){
                auto cmpIdx = (idx - 1) >> 1;
                if (*tmp < *q[cmpIdx]) {
                    update_node(idx, q[cmpIdx]);
                    idx = cmpIdx;
                    ret = true;
                    continue;
                }
                break;
            }
            if (ret) update_node(idx, tmp);
            return ret;
        }

        // compare m_nodes[idx] with child node.
        [[gnu::always_inline]]
        bool down(int idx)
        {
            auto tmp = q[idx];
            size_t cmpIdx = (idx << 1) + 1;
            bool ret = false;
            while (cmpIdx < q.size()) {
                if (cmpIdx + 1 < q.size() && *q[cmpIdx + 1] < *q[cmpIdx]) cmpIdx++;
                if (*q[cmpIdx] < *tmp){
                    update_node(idx, q[cmpIdx]);
                    idx = cmpIdx;
                    cmpIdx = (idx << 1) + 1;
                    ret = true;
                    continue;
                }
                break;
            }
            if (ret) update_node(idx, tmp);
            return ret;
        }
    };

    thread* CURRENT = new thread;
    static SleepQueue sleepq;

    static void thread_die(thread* th)
    {
//        LOG_DEBUG(th, th->buf);
        th->dispose();
    }

    extern void proton_die_and_jmp_to_context(thread* dying_th, void** dest_context,
        void(*th_die)(thread*)) asm ("_proton_die_and_jmp_to_context");
    __attribute__((preserve_none))
    extern void proton_die_and_jmp_to_context_new(thread* dying_th, thread* dest_context,
        void(*th_die)(thread*)) asm ("_proton_die_and_jmp_to_context_new");
    static int do_idle_sleep(uint64_t usec);
    static int resume_sleepers();
    static inline void switch_context(thread* from, states new_state, thread* to);

    extern void proton_switch_context(void**, void**) asm ("_proton_switch_context");
    inline void switch_context(thread* from, states new_state, thread* to)
    {
        from->state = new_state;
        to->state   = states::RUNNING;
        proton_switch_context(from->stack.pointer_ref(), to->stack.pointer_ref());
    }

    __attribute__((preserve_none))
    extern void proton_switch_context_new(thread*, thread*) asm ("_proton_switch_context_new");
    inline void switch_context_new(thread* from, states new_state, thread* to)
    {
        from->state = new_state;
        to->state   = states::RUNNING;
        // proton_switch_context_new(from, to);
        __asm__ volatile (R"(
            mov 0x18(%1), %%rcx
            lea 1f(%%rip), %%rdx
            mov %%rsp, 0x10(%0)
            mov %%rdx, 0x18(%0)
            mov 0x10(%1), %%rsp
            jmp *%%rcx
            1:
        )" : : "r"(from), "r"(to));
        __asm__ volatile ("" : : :
            "rax", "rbx", "rcx", "rdx", "rsi", "rdi",
            "rbp", "r8", "r9", "r10", "r11", "r12",
            "r13", "r14", "r15");
    }

    static void enqueue_wait(thread_list* q, thread* th, uint64_t expire)
    {
        assert(th->waitq == nullptr);
        th->ts_wakeup = expire;
        if (q)
        {
            q->push_back(th);
            th->waitq = q;
        }
    }

    static void thread_stub()
    {
        CURRENT->go();
        CURRENT->cond.notify_all();
        while (CURRENT->single() && !sleepq.empty())
        {
            if (resume_sleepers() == 0)
                do_idle_sleep(-1);
        }

        auto th = CURRENT;
        CURRENT = CURRENT->remove_from_list();
        if (!th->joinable)
        {
            th->state = states::DONE;
            #if defined(CACSTH)
            thread* to = CURRENT;
            CACS_die_switch_defer(th, to, (void(*)(void*))&thread_die);
            #elif defined(NEW)
            thread* to = CURRENT;
            proton_die_and_jmp_to_context_new(th, to, &thread_die);
            #else
            proton_die_and_jmp_to_context(th,
                CURRENT->stack.pointer_ref(), &thread_die);
            #endif
        }
        else
        {
            #if defined(CACSTH)
            thread *to = CURRENT;
            th->state = states::DONE;
            to->state = states::RUNNING;
            CACS(th, to);
            #elif defined(NEW)
            thread *to = CURRENT;
            th->state = states::DONE;
            to->state = states::RUNNING;
            proton_switch_context_new(th, to);
            #else
            switch_context(th, states::DONE, CURRENT);
            #endif
        }
    }

    thread* thread_create(void* (*start)(void*), void* arg, uint64_t stack_size)
    {
        auto ptr = new char[stack_size];
        auto p = ptr + stack_size - sizeof(thread);
        (uint64_t&)p &= ~63;
#ifdef RANDOMIZE_SP
        p -= 16 * (rand() % 32);
#endif
        auto th = new (p) thread;
        th->buf = ptr;
        th->idx = -1;
        th->start = start;
        th->arg = arg;
        // p -= 64;
        th->stack.init(p, &thread_stub);
        th->ret_addr = (uint64_t)&thread_stub;
        th->state = states::READY;
        CURRENT->insert_tail(th);
        return th;
    }

    uint64_t now;
    static inline uint64_t update_now()
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        now = tv.tv_sec;
        now *= 1000 * 1000;
        return now += tv.tv_usec;
    }
    static inline void prefetch_context(thread* from, thread* to)
    {
#ifdef CONTEXT_PREFETCHING
        const int CACHE_LINE_SIZE = 64;
        auto f = *from->stack.pointer_ref();
        __builtin_prefetch(f, 1);
        __builtin_prefetch((char*)f + CACHE_LINE_SIZE, 1);
        auto t = *to->stack.pointer_ref();
        __builtin_prefetch(t, 0);
        __builtin_prefetch((char*)t + CACHE_LINE_SIZE, 0);
#endif
    }

    static int resume_sleepers()
    {
        int count = 0;
        update_now();
        while(true)
        {
            auto th = sleepq.front();
            if (!th || now < th->ts_wakeup)
                break;

//            LOG_DEBUG(VALUE(now), " resuming thread ", VALUE(th), " which is supposed to wake up at ", th->ts_wakeup);
            sleepq.pop_front();
            th->dequeue_ready();
            count++;
        }
        return count;
    }

    states thread_stat(thread* th)
    {
        return th->state;
    }

#if !defined(CACSTH) && !defined(NEW)
    void thread_yield() {
        auto t0 = CURRENT;
        if (unlikely(t0->single())) {
            if (0 == resume_sleepers())
                return;     // no target to yield to
        }

        auto next = CURRENT = t0->next();
        prefetch_context(t0, next);
        switch_context(t0, states::READY, next);
    }
#elif defined(NEW)
    __attribute__((preserve_none))
    void thread_yield() {
        auto from = CURRENT;
        if (unlikely(from->single())) {
            if (resume_sleepers() == 0)
                return;
        }
        auto to = CURRENT = from->next();
        from->state = states::READY;
        to->state   = states::RUNNING;
        proton_switch_context_new(from, to);
        // asm volatile ("");
        // switch_context_new(from, states::RUNNING, to);
    }
#elif defined(CACSTH)
    __attribute__((preserve_none))
    void thread_yield() {
        auto from = CURRENT;
        if (unlikely(from->single())) {
            #ifndef CACSTAIL
            // if (resume_sleepers() == 0)
            #endif
                return;
        }
        auto to = CURRENT = from->next();
        from->state = states::READY;
        to->state   = states::RUNNING;
        #ifndef CACSTAIL
        CACS(from, to);
        #else
        CACS_tail(from, to);
        #endif
    }
#endif

    __attribute__((preserve_none))
    inline void _switch_context(void* from, void* to) {
        __asm__ (R"(
            lea 1f(%%rip), %%rax
            mov %%rsp, 16(%0)
            mov %%rax, 24(%0)
            mov 16(%1), %%rsp
            mov 24(%1), %%rax
            // pop %%rax
            jmp *%%rax
            1:
        )" :: "r" (from), "r" (to) : "rax");
    }


    void thread_yield_to(thread* th)
    {
        if (th == nullptr) // yield to any thread
        {
            if (CURRENT->single())
            {
                auto ret = resume_sleepers(); //proton::now will be update
                if (ret == 0)
                    return;     // no target to yield to
            }
            th = CURRENT->next(); // or not update
        }
        else if (th->state != states::READY)
        {
            LOG_ERROR_RETURN(EINVAL, , VALUE(th), " must be READY!");
        }

        auto t0 = CURRENT;
        CURRENT = th;
        prefetch_context(t0, CURRENT);
        #ifdef CACSTH
        t0->state=states::READY;
        th->state=states::RUNNING;
        CACS(t0, th);
        #elif defined(NEW)
        t0->state=states::READY;
        th->state=states::RUNNING;
        proton_switch_context_new(t0, th);
        #else
        switch_context(t0, states::READY, CURRENT);
        #endif
    }

    static int do_idle_sleep(uint64_t usec)
    {
        if (!sleepq.empty())
            if (sleepq.front()->ts_wakeup > now)
                usec = std::min(usec, sleepq.front()->ts_wakeup - now);

        return idle_sleeper(usec);
    }

    // returns 0 if slept well (at lease `useconds`), -1 otherwise
#if defined(NEW) || defined(CACSTH)
    __attribute__((preserve_none))
#endif
    static int thread_usleep(uint64_t useconds, thread_list* waitq)
    {
        if (__builtin_expect(useconds == 0, 0)) {
            thread_yield();
            return 0;
        }
        CURRENT->state = states::WAITING;
        auto expire = sat_add(now, useconds);
        while (unlikely(CURRENT->single())) { // if no active threads available
            if (unlikely(resume_sleepers() > 0)) // will update_now() in it
            {
                break;
            }
            if (unlikely(now >= expire)) {
                return 0;
            }
            do_idle_sleep(useconds);
            if (likely(CURRENT->state ==
                       states::READY)) // CURRENT has been woken up during idle
                                       // sleep
            {
                CURRENT->set_error_number();
                return -1;
            }
        }

        auto t0 = CURRENT;
        CURRENT = CURRENT->remove_from_list();
        prefetch_context(t0, CURRENT);
        assert(CURRENT != nullptr);
        enqueue_wait(waitq, t0, expire);
        sleepq.push(t0);
        // LOG_INFO("BEFORE ` `", t0, CURRENT);
#if defined(CACSTH)
    t0->state = states::WAITING;
    CURRENT->state = states::RUNNING;
    CACS(t0, CURRENT);
#elif defined(NEW)
    t0->state = states::WAITING;
    CURRENT->state = states::RUNNING;
    proton_switch_context_new(t0, CURRENT);
#else
    switch_context(t0, states::WAITING, CURRENT);
#endif
    // LOG_INFO("AFTER ` `", t0, CURRENT);
    // CANNOT use t0 because switch context may crash all registers
    return CURRENT->set_error_number();
    }

#if defined(NEW) || defined(CACSTH)
    __attribute__((preserve_none))
#endif
    int thread_usleep(uint64_t useconds)
    {
        if (unlikely(CURRENT->shutting_down && useconds > 10*1000))
        {
            int ret = thread_usleep(10*1000, nullptr);
           if (ret >= 0)
                errno = EPERM ;
            return -1;
        }
        return thread_usleep(useconds, nullptr);
    }

#if defined(NEW) || defined(CACSTH)
    __attribute__((preserve_none))
#endif
    void thread_interrupt(thread* th, int error_number)
    {
        if (__builtin_expect(th->state == states::READY, 0)) { // th is already in runing queue
            return;
        }

        if (__builtin_expect(!th || th->state != states::WAITING, 0))
            LOG_ERROR_RETURN(EINVAL, , "invalid parameter");

        if (__builtin_expect(th == CURRENT, 0))
        {   // idle_sleep may run in CURRENT's context, which may be single() and WAITING
            th->state = states::READY;
            th->error_number = error_number;
            return;
        }
        sleepq.pop(th);
        th->dequeue_ready();
        th->error_number = error_number;
    }

    join_handle* thread_enable_join(thread* th, bool flag)
    {
        th->joinable = flag;
        return (join_handle*)th;
    }

    void thread_join(join_handle* jh)
    {
        auto th = (thread*)jh;
        if (!th->joinable)
            LOG_ERROR_RETURN(ENOSYS, , "join is not enabled for thread ", th);

        if (th->state != states::DONE)
        {
            th->cond.wait_no_lock();
            th->remove_from_list();
        }
        th->dispose();
    }

    int thread_shutdown(thread* th, bool flag)
    {
        if (!th)
            LOG_ERROR_RETURN(EINVAL, -1, "invalid thread");

        th->shutting_down = flag;
        if (th->state == states::WAITING)
            thread_interrupt(th, EPERM);
        return 0;
    }

   // if set, the timer will fire repeatedly, using `timer_entry` return val as the next timeout
    // if the return val is 0, use `default_timedout` as the next timedout instead;
    // if the return val is -1, stop the timer;
    const uint64_t TIMER_FLAG_REPEATING     = 1 << 0;

    // if set, the timer object is ready for reuse via `timer_reset()`, implying REPEATING
    // if `default_timedout` is -1, the created timer does NOT fire a first shot before `timer_reset()`
    const uint64_t TIMER_FLAG_REUSE         = 1 << 1;

    typedef uint64_t (*timer_entry)(void*);
    struct timer_args
    {
        uint64_t default_timeout;
        timer_entry on_timer;
        uint64_t flags;
        void* arg;
    };
    const uint64_t TIMER_FLAG_WAITING_SHOT = 999;
    // bitmark for telling timing loop that timer has already reset by on_timer
    const uint64_t TIMER_FLAG_RESET = 1<<3;
    struct timer : public thread
    {
    public:
        void init(uint64_t default_timeout, uint64_t flags)
        {
            if (flags & TIMER_FLAG_REUSE)
                flags |= TIMER_FLAG_REPEATING;
            error_number = (int)flags;
            retval = (void*)default_timeout;
            stack[6] = (uint64_t)&timer_thread_stub;
            thread_yield_to(this);  // switch to timer_stub to init timer
        }
        static void timer_thread_stub()
        {
            timer_args args;
            args.default_timeout = (uint64_t)CURRENT->retval;
            args.on_timer = (timer_entry)CURRENT->start;
            args.flags = CURRENT->error_number;
            args.arg = CURRENT->arg;
            CURRENT->arg = &args;
            CURRENT->start = &__do_timer;
            CURRENT->error_number = 0;
            CURRENT->retval = 0;
            thread_stub();
        }
        static void* __do_timer(void* arg)
        {
            auto args = (timer_args*)arg;
            static_cast<timer*>(CURRENT)->do_timer(args);
            return 0;
        }
        uint64_t wait_for_reuse()
        {
            retval = (void*)TIMER_FLAG_REUSE;
            cond.wait_no_lock();
            return (uint64_t)retval;
        }
        bool is_waiting_for_reuse()
        {
            return retval == (void*)TIMER_FLAG_REUSE;
        }
        void do_reuse(uint64_t timeout)
        {
            // do_reuse may be called during on_timer
            // at this situation, the status may not be
            // waiting for reuse.
            // and notify may before waiting
            // assert(retval == (void*)TIMER_FLAG_REUSE);
            // assert(cond.q && cond.q->single());
            retval = (void*)timeout;
            cond.notify_one();
        }
        int wait_for_shot(uint64_t timeout)
        {
            retval = (void*)TIMER_FLAG_WAITING_SHOT;
            int ret = thread_usleep(timeout);
            retval = 0;
            return ret;
        }
        bool is_waiting_for_shot()
        {
            return retval == (void*)TIMER_FLAG_WAITING_SHOT;
        }
        void do_timer(timer_args* args)
        {
            auto timeout = args->default_timeout;
            if (timeout == (uint64_t)-1 && (args->flags & TIMER_FLAG_REUSE))
                timeout = wait_for_reuse();
            do {
//                LOG_DEBUG("sleep ` us before on timer", timedout);
                if (timeout == (uint64_t)-1) break;
                if (timeout == 0) timeout = args->default_timeout;
                int ret = wait_for_shot(timeout);
                if (ret < 0)
                {
                    if (errno == ECANCELED)
                    {
                        timeout = args->default_timeout;
                        goto do_continue;
                    }
                    break;
                }

                // clear reset mark
                args->flags &= ~TIMER_FLAG_RESET;
                timeout = args->on_timer(args->arg);

            do_continue:
                // once reset during ontime, should not wait and need to start one more round
                // skip wait if already called reset
                if ((args->flags & TIMER_FLAG_REUSE) && !(args->flags & TIMER_FLAG_RESET))
                    timeout = wait_for_reuse();
            } while((args->flags & TIMER_FLAG_REPEATING));
        }
    };
    timer* timer_create(uint64_t default_timedout, timer_entry on_timer,
                        void* arg, uint64_t flags, uint64_t stack_size)
    {
        auto th = thread_create((thread_entry)on_timer, arg, stack_size);
        if (!th)
            return nullptr;

        auto tmr = (timer*)th;
        tmr->init(default_timedout, flags);
        return tmr;
    }
    int timer_reset(timer* tmr, uint64_t new_timeout)
    {
        if (!tmr) return -1;
        auto args = (timer_args*)tmr->arg;
        args->default_timeout = new_timeout;
        if (tmr->is_waiting_for_shot())
        {   // timer is in sleepq
            sleepq.pop(tmr);
            tmr->ts_wakeup = sat_add(update_now(), new_timeout);
            // It is possible that tmr waked after sleep, but caller of timer_reset
            // goes here before tmr set the TIMER_FLAG_IS_WAITING back to 0.
            // in this situation, tmr is in waitq, once there is any other thread in waitq,
            // tmr will not be single, and cannot put back to sleepq.
            // Here should remove it from existen list, then put it into sleepq.
            tmr->remove_from_list();
            sleepq.push(tmr);
        }
        else
        {
            tmr->do_reuse(new_timeout);
            args->flags |= TIMER_FLAG_RESET;
        }
        return 0;
    }
    int timer_cancel(timer* tmr)
    {
        if (!tmr) return -1;
        if (tmr->is_waiting_for_reuse())
        {
        }
        else if (tmr->is_waiting_for_shot())
        {
            thread_interrupt(tmr, ECANCELED);
        }
        else return -1;
        return 0;
    }
    int timer_destroy(timer* tmr)
    {
        if (!tmr) return -1;
        auto args = (timer_args*)tmr->arg;
        args->flags &= ~TIMER_FLAG_REPEATING;
        if (tmr->is_waiting_for_reuse())
        {
            tmr->do_reuse(-1);
        }
        else if (tmr->is_waiting_for_shot())
        {
            thread_interrupt(tmr, ECANCELED);
        }
        else return -1;
        return 0;
    }

    int waitq::wait(uint64_t timeout)
    {
        static_assert(sizeof(q) == sizeof(thread_list), "...");
        int ret = thread_usleep(timeout, (thread_list*)&q);
        if (ret == 0)
        {
            errno = ETIMEDOUT;
            return -1;
        }
        return (errno == ECANCELED) ? 0 : -1;
    }
    void waitq::resume(thread* th)
    {
        assert(th->waitq == (thread_list*)&q);
        if (!th || !q || th->waitq != (thread_list*)&q)
            return;
        // will update q during thread_interrupt()
        thread_interrupt(th, ECANCELED);
    }
    int mutex::lock(uint64_t timeout)
    {
        if (owner == CURRENT)
             LOG_ERROR_RETURN(EINVAL, -1, "recursive locking is not supported");

        while(owner)
        {
            // LOG_INFO("wait(timeout);", VALUE(CURRENT), VALUE(this), VALUE(owner));
            if (wait(timeout) < 0)
            {
                // EINTR means break waiting without holding lock
                // it is normal in OutOfOrder result collected situation, and that is the only
                // place using EINTR to interrupt micro-threads (during getting lock)
                // normally, ETIMEOUT means wait timeout and ECANCELED means resume from sleep
                // LOG_DEBUG("timeout return -1;", VALUE(CURRENT), VALUE(this), VALUE(owner));
                return -1;   // timedout or interrupted
            }
        }

        owner = CURRENT;
        // LOG_INFO(VALUE(CURRENT), VALUE(this));
        return 0;
    }
    void mutex::unlock()
    {
        if (owner != CURRENT)
            return;

        owner = nullptr;
        resume_one();
        // LOG_INFO(VALUE(CURRENT), VALUE(this), VALUE(owner));
    }
    int condition_variable::wait_no_lock(uint64_t timeout)
    {
        return waitq::wait(timeout);
    }
    int condition_variable::wait(scoped_lock& lock, uint64_t timeout)
    {   // current implemention is only for interface compatibility, needs REDO for multi-vcpu
        if (!lock.locked())
            return wait_no_lock(timeout);

        lock.unlock();
        int ret = wait_no_lock(timeout);
        lock.lock();
        return ret;
    }
    void condition_variable::notify_one()
    {
        resume_one();
    }
    void condition_variable::notify_all()
    {
        while(q)
            resume_one();
    }
    int semaphore::wait(uint64_t count, uint64_t timeout)
    {
        if (count == 0) return 0;
        while(m_count < count)
        {
            CURRENT->retval = (void*)count;
            int ret = waitq::wait(timeout);
            if (ret < 0) {
                // when timeout, and CURRENT was the first in waitq,
                // we need to try to resume the next one in q
                signal(0);
                return -1;
            }
        }
        m_count -= count;
        return 0;
    }
    int semaphore::signal(uint64_t count)
    {
        m_count += count;
        while(q)
        {
            auto q_front_count = (uint64_t)q->retval;
            if (m_count < q_front_count) break;
            resume_one();
        }
        return 0;
    }
    int rwlock::lock(int mode, uint64_t timeout)
    {
        if (mode != RLOCK && mode != WLOCK)
            LOG_ERROR_RETURN(EINVAL, -1, "mode unknow");
        // backup retval
        void* bkup = CURRENT->retval;
        DEFER(CURRENT->retval = bkup);
        auto mark = (uint64_t)CURRENT->retval;
        // mask mark bits, keep RLOCK WLOCK bit clean
        mark &= ~(RLOCK | WLOCK);
        // mark mode and set as retval
        mark |= mode;
        CURRENT->retval = (void*)(mark);
        bool wait_cond;
        int op;
        if (mode == RLOCK) {
            wait_cond = state<0;
            op = 1;
        } else { // WLOCK
            wait_cond = state;
            op = -1;
        }
        if (q || wait_cond) {
            int ret = wait(timeout);
            if (ret < 0)
                return -1; // break by timeout or interrupt
        }
        state += op;
        return 0;
    }
    int rwlock::unlock()
    {
        assert(state != 0);
        if (state>0)
            state --;
        else
            state ++;
        if (state == 0 && q) {
            if (((uint64_t)q->retval) & WLOCK)
                resume_one();
            else
                while (q && (((uint64_t)q->retval) & RLOCK))
                    resume_one();
        }
        return 0;
    }

    int init()
    {
        CURRENT->idx = -1;
        CURRENT->state = states::RUNNING;
        update_now();
        return 0;
    }
    int fini()
    {
        return 0;
    }
}

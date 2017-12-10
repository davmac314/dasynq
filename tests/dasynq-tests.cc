#include <sys/socket.h>
#include <sys/un.h>

#include <cassert>
#include <iostream>
#include <thread>

#include "testbackend.h"
#include "dasynq.h"

class checking_mutex
{
    bool is_locked = false;

    public:
    void lock()
    {
        if (is_locked) throw "mutex already locked";
        is_locked = true;
    }

    void unlock()
    {
        if (! is_locked) throw "mutex not locked";
        is_locked = false;
    }
};

class test_traits : public dasynq::default_traits<checking_mutex>
{
    public:
    template <typename Base> using backend_t = dasynq::test_loop<Base>;
    using backend_traits_t = dasynq::test_loop_traits;
};

using Loop_t = dasynq::event_loop<checking_mutex, test_traits>;

using dasynq::rearm;
using dasynq::test_io_engine;

namespace dasynq {
    std::unordered_map<int, test_io_engine::fd_data> test_io_engine::fd_data_map;
    time_val test_io_engine::cur_sys_time;
    time_val test_io_engine::cur_mono_time;
}

// Set up two file descriptor watches on two different descriptors, and make sure the correct handler
// is triggered when input is available on each.
void test_fd_watch1()
{
    test_io_engine::clear_fd_data();
    Loop_t my_loop;
    
    bool seen1 = false;
    bool seen2 = false;
    
    Loop_t::fd_watcher::add_watch(my_loop, 0, dasynq::IN_EVENTS,
            [&seen1](Loop_t &eloop, int fd, int flags) -> rearm {
        seen1 = true;
        return rearm::REMOVE;
    });
    
    Loop_t::fd_watcher::add_watch(my_loop, 1, dasynq::IN_EVENTS,
            [&seen2](Loop_t &eloop, int fd, int flags) -> rearm {
        seen2 = true;
        return rearm::REMOVE;
    });

    test_io_engine::trigger_fd_event(0, dasynq::IN_EVENTS);
    
    my_loop.run();
    
    assert(seen1);
    assert(!seen2);
    
    seen1 = false;
    
    test_io_engine::trigger_fd_event(0, dasynq::IN_EVENTS);
    test_io_engine::trigger_fd_event(1, dasynq::IN_EVENTS);
        
    my_loop.run();
    
    assert(!seen1);
    assert(seen2);
}

// Set up a handler which auto-rearms, and make sure it still receives events.
// Set up a 2nd handler which disarms, and make sure it receives no further events.
void test_fd_watch2()
{
    test_io_engine::clear_fd_data();
    Loop_t my_loop;

    bool seen1 = false;
    bool seen2 = false;
    
    auto watcher1 = Loop_t::fd_watcher::add_watch(my_loop, 0, dasynq::IN_EVENTS,
            [&seen1](Loop_t &eloop, int fd, int flags) -> rearm {
        seen1 = true;
        return rearm::REARM;
    });

    auto watcher2 = Loop_t::fd_watcher::add_watch(my_loop, 1, dasynq::IN_EVENTS,
            [&seen2](Loop_t &eloop, int fd, int flags) -> rearm {
        seen2 = true;
        return rearm::DISARM;
    });
    
    test_io_engine::trigger_fd_event(0, dasynq::IN_EVENTS);
    test_io_engine::trigger_fd_event(1, dasynq::IN_EVENTS);
    my_loop.run();
    
    assert(seen1);
    assert(seen2);
    
    seen1 = false;
    seen2 = false;
    
    test_io_engine::trigger_fd_event(0, dasynq::IN_EVENTS);
    test_io_engine::trigger_fd_event(1, dasynq::IN_EVENTS);
    my_loop.run();
    
    assert(seen1);
    assert(!seen2);
    
    watcher1->deregister(my_loop);
    watcher2->deregister(my_loop);
}

void test_fd_watch3()
{
    test_io_engine::clear_fd_data();
    Loop_t my_loop;

    bool seen1 = false;

    class my_watcher : public Loop_t::fd_watcher_impl<my_watcher>
    {
        public:
        bool &seen1;

        my_watcher(bool &seen) : seen1(seen) { }

        rearm fd_event(Loop_t &eloop, int fd, int flags)
        {
            // Process I/O here
            seen1 = true;
            set_enabled(eloop, true);
            return rearm::DISARM;  // disable watcher
        }
    };

    my_watcher watcher1(seen1);
    watcher1.add_watch(my_loop, 0, dasynq::IN_EVENTS);

    test_io_engine::trigger_fd_event(0, dasynq::IN_EVENTS);
    my_loop.run();

    assert(seen1);

    seen1 = false;

    test_io_engine::trigger_fd_event(0, dasynq::IN_EVENTS);
    my_loop.poll();

    assert(! seen1);

    watcher1.deregister(my_loop);
}

void test_fd_emu()
{
    test_io_engine::clear_fd_data();
    Loop_t my_loop;

    int seen_count = 0;

    class my_watcher : public Loop_t::fd_watcher_impl<my_watcher>
    {
        public:
        int &seen_count;

        my_watcher(int &seen) : seen_count(seen) { }

        rearm fd_event(Loop_t &eloop, int fd, int flags)
        {
            // Process I/O here
            seen_count++;
            if (seen_count == 10) {
                return rearm::NOOP; // don't re-enable
            }
            else {
                return rearm::REARM;
            }
        }
    };

    my_watcher watcher1(seen_count);

    test_io_engine::mark_fd_needs_emulation(0);

    watcher1.add_watch(my_loop, 0, dasynq::IN_EVENTS);

    // test_io_engine::trigger_fd_event(0, dasynq::IN_EVENTS);
    my_loop.run();

    assert(seen_count == 10);

    seen_count = 0;

    // test_io_engine::trigger_fd_event(0, dasynq::IN_EVENTS);
    my_loop.poll();

    assert(seen_count == 0);

    watcher1.deregister(my_loop);
}

static void testTimespecDiv()
{
    using dasynq::divide_timespec;

    struct timespec n;
    struct timespec d;
    struct timespec rem;

    // 0.9999.. / 0.2
    n.tv_sec = 0;
    n.tv_nsec = 999999999;
    d.tv_sec = 0;
    d.tv_nsec = 200000000;

    assert(divide_timespec(n, d, rem) == 4);
    assert(rem.tv_sec == 0);
    assert(rem.tv_nsec == 199999999);

    // 0.9999.. / 0.1
    d.tv_nsec = 100000000;
    assert(divide_timespec(n, d, rem) == 9);
    assert(rem.tv_sec == 0);
    assert(rem.tv_nsec == 99999999);

    // 12.0 / 2.9999..
    n.tv_sec = 12;
    n.tv_nsec = 0; // n is 12.0
    d.tv_sec = 2;
    d.tv_nsec = 999999999; // d is now 2.999999..
    assert(divide_timespec(n, d, rem) == 4);
    assert(rem.tv_sec == 0);
    assert(rem.tv_nsec == 4);

    // 12.999999996 / 2.99...
    n.tv_nsec = 999999996;
    assert(divide_timespec(n, d, rem) == 4);
    assert(rem.tv_sec == 1);
    assert(rem.tv_nsec == 0);

    // 0.9 / 1.5
    n.tv_sec = 0;
    n.tv_nsec = 900000000;
    d.tv_sec = 1;
    d.tv_nsec = 500000000;
    assert(divide_timespec(n, d, rem) == 0);
    assert(rem.tv_sec == 0);
    assert(rem.tv_nsec == 900000000);
}

static void test_timers_1()
{
    using dasynq::clock_type;
    using dasynq::time_val;
    using loop_t = Loop_t;
    loop_t my_loop;

    class my_timer : public loop_t::timer_impl<my_timer>
    {
        public:
        rearm timer_expiry(loop_t &loop, int expiry_count)
        {
            expiries += expiry_count;
            return rearm::REARM;
        }

        int expiries = 0;
    };

    // First timer is a one-shot timer expiring at 3 seconds:
    my_timer timer_1;
    struct timespec timeout_1 = { .tv_sec = 3, .tv_nsec = 0 };
    timer_1.add_timer(my_loop, clock_type::MONOTONIC);
    timer_1.arm_timer(my_loop, timeout_1);

    // Second timer expires at 4 seconds and then every 1 second after:
    my_timer timer_2;
    struct timespec timeout_2 = { .tv_sec = 4, .tv_nsec = 0 };
    struct timespec interval_2 = { .tv_sec = 1, .tv_nsec = 0 };
    timer_2.add_timer(my_loop, clock_type::MONOTONIC);
    timer_2.arm_timer(my_loop, timeout_2, interval_2);

    test_io_engine::cur_mono_time = time_val(0, 0);

    my_loop.poll();
    assert(timer_1.expiries == 0);
    assert(timer_2.expiries == 0);

    // After 1 second, should be no expirations:
    test_io_engine::cur_mono_time = time_val(1, 0);
    my_loop.poll();
    assert(timer_1.expiries == 0);
    assert(timer_2.expiries == 0);

    // After 3 seconds, should have one expiration on t1:
    test_io_engine::cur_mono_time = time_val(3, 0);
    my_loop.poll();
    assert(timer_1.expiries == 1);
    assert(timer_2.expiries == 0);

    // After 4.5 seconds, should have an expiration on t2:
    test_io_engine::cur_mono_time = time_val(4, 500000000);
    my_loop.poll();
    assert(timer_1.expiries == 1);
    assert(timer_2.expiries == 1);

    // After 5.5 seconds, a second expiration on t2:
    test_io_engine::cur_mono_time = time_val(5, 500000000);
    my_loop.poll();
    assert(timer_1.expiries == 1);
    assert(timer_2.expiries == 2);

    // After 6 seconds, a third expiration on t2:
    test_io_engine::cur_mono_time = time_val(6, 0);
    my_loop.poll();
    assert(timer_1.expiries == 1);
    assert(timer_2.expiries == 3);
}

static void test_timers_2()
{
    using dasynq::clock_type;
    using dasynq::time_val;
    using loop_t = Loop_t;
    loop_t my_loop;

    class my_timer : public loop_t::timer_impl<my_timer>
    {
        public:
        rearm timer_expiry(loop_t &loop, int expiry_count)
        {
            expiries += expiry_count;
            return handler_rearm;
        }

        int expiries = 0;
        rearm handler_rearm = rearm::REARM;
    };

    // Create timer expiring at 1 second intervals:
    my_timer timer_1;
    struct timespec timeout_1 = { .tv_sec = 1, .tv_nsec = 0 };
    struct timespec interval_1 = { .tv_sec = 1, .tv_nsec = 0 };
    timer_1.add_timer(my_loop, clock_type::MONOTONIC);
    timer_1.arm_timer(my_loop, timeout_1, interval_1);

    test_io_engine::cur_mono_time = time_val(0, 0);
    my_loop.poll();
    assert(timer_1.expiries == 0);

    // Have the timer disarm after it fires once:
    test_io_engine::cur_mono_time = time_val(1, 0);
    timer_1.handler_rearm = rearm::DISARM;
    my_loop.poll();
    assert(timer_1.expiries == 1);

    // Check that the timer doesn't fire when disabled:
    test_io_engine::cur_mono_time = time_val(5, 0);
    my_loop.poll();
    assert(timer_1.expiries == 1);

    // Re-enable timer and check that it fires immediately:
    timer_1.set_enabled(my_loop, clock_type::MONOTONIC, true);
    my_loop.poll();
    assert(timer_1.expiries == 5);

    // It's disabled again; check 2 more periods:
    test_io_engine::cur_mono_time = time_val(7, 0);
    my_loop.poll();
    assert(timer_1.expiries == 5);

    // Re-enable, and then disable, and wait two more periods:
    timer_1.set_enabled(my_loop, clock_type::MONOTONIC, true);
    timer_1.set_enabled(my_loop, clock_type::MONOTONIC, false);
    test_io_engine::cur_mono_time = time_val(9, 0);
    my_loop.poll();
    assert(timer_1.expiries == 5);

    // Now re-enable and check for the 4 missing periods reported:
    timer_1.set_enabled(my_loop, clock_type::MONOTONIC, true);
    my_loop.poll();
    assert(timer_1.expiries == 9);
}

static void create_pipe(int filedes[2])
{
    if (pipe(filedes) == -1) {
        throw std::system_error(errno, std::system_category());
    }
}

static void create_bidi_pipe(int filedes[2])
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, filedes) == -1) {
        throw std::system_error(errno, std::system_category());
    }
}

void ftest_fd_watch1()
{
    using Loop_t = dasynq::event_loop<checking_mutex>;
    Loop_t my_loop;

    bool seen1 = false;
    bool seen2 = false;

    int pipe1[2];
    int pipe2[2];
    create_pipe(pipe1);
    create_pipe(pipe2);

    Loop_t::fd_watcher::add_watch(my_loop, pipe1[0], dasynq::IN_EVENTS,
            [&seen1](Loop_t &eloop, int fd, int flags) -> rearm {
        seen1 = true;
        return rearm::REMOVE;
    });

    Loop_t::fd_watcher::add_watch(my_loop, pipe2[0], dasynq::IN_EVENTS,
            [&seen2](Loop_t &eloop, int fd, int flags) -> rearm {
        seen2 = true;
        return rearm::REMOVE;
    });

    // test_io_engine::trigger_fd_event(0, dasynq::IN_EVENTS);
    char wbuf[1] = {'a'};
    char rbuf[1];
    write(pipe1[1], wbuf, 1);

    my_loop.run();

    assert(seen1);
    assert(!seen2);

    seen1 = false;

    read(pipe1[0], rbuf, 1);

    //test_io_engine::trigger_fd_event(0, dasynq::IN_EVENTS);
    //test_io_engine::trigger_fd_event(1, dasynq::IN_EVENTS);
    write(pipe1[1], wbuf, 1);
    write(pipe2[1], wbuf, 1);

    my_loop.run();

    assert(!seen1);
    assert(seen2);

    close(pipe1[0]);
    close(pipe1[1]);
    close(pipe2[0]);
    close(pipe2[1]);
}

void ftest_bidi_fd_watch1()
{
    using Loop_t = dasynq::event_loop<checking_mutex>;
    Loop_t my_loop;

    bool flags1[3] = { false, false, false };  // in, out, removed

    int pipe1[2];
    create_bidi_pipe(pipe1);

    class MyBidiWatcher : public Loop_t::bidi_fd_watcher_impl<MyBidiWatcher> {
        bool (&flags)[3];

        public:
        MyBidiWatcher(bool (&flags_a)[3]) : flags(flags_a)
        {
        }

        rearm read_ready(Loop_t &eloop, int fd) noexcept
        {
            flags[0] = true;
            char rbuf;
            read(fd, &rbuf, 1);
            return rearm::REMOVE;
        }

        rearm write_ready(Loop_t &eloop, int fd) noexcept
        {
            flags[1] = true;
            return rearm::REMOVE;
        }

        void watch_removed() noexcept override
        {
            flags[2] = true;
        }
    };

    MyBidiWatcher watch {flags1};

    // Both read and write trigger remove. Once both have triggered, the watch should auto-remove.

    char wbuf = 'a';
    write(pipe1[1], &wbuf, 1);

    watch.add_watch(my_loop, pipe1[0], dasynq::IN_EVENTS | dasynq::OUT_EVENTS);

    my_loop.run();

    assert(flags1[0] && flags1[1]);   // in and out flag should be set
    assert(flags1[2]);

    close(pipe1[0]);
    close(pipe1[1]);
}

void ftest_bidi_fd_watch2()
{
    using Loop_t = dasynq::event_loop<checking_mutex>;
    Loop_t my_loop;

    bool flags1[3] = { false, false, false };  // in, out, removed

    int pipe1[2];
    create_bidi_pipe(pipe1);

    class MyBidiWatcher : public Loop_t::bidi_fd_watcher_impl<MyBidiWatcher> {
        bool (&flags)[3];

        public:
        MyBidiWatcher(bool (&flags_a)[3]) : flags(flags_a)
        {
        }

        rearm read_ready(Loop_t &eloop, int fd) noexcept
        {
            flags[0] = true;
            char rbuf;
            read(fd, &rbuf, 1);
            return rearm::REMOVE;
        }

        rearm write_ready(Loop_t &eloop, int fd) noexcept
        {
            flags[1] = true;
            return rearm::REARM;
        }

        void watch_removed() noexcept override
        {
            flags[2] = true;
        }
    };

    MyBidiWatcher watch {flags1};

    // Only read watch triggers remove. Once both have triggered, the watch should not have been removed.

    char wbuf = 'a';
    write(pipe1[1], &wbuf, 1);

    watch.add_watch(my_loop, pipe1[0], dasynq::IN_EVENTS | dasynq::OUT_EVENTS);

    my_loop.run();

    assert(flags1[0] && flags1[1]);   // in and out flag should be set
    assert(! flags1[2]);  // watch not removed (write watch not removed)

    flags1[0] = false;
    flags1[1] = false;

    // Send more to pipe; read watch should not see this, as it is inactive:
    write(pipe1[1], &wbuf, 1);

    my_loop.run();

    assert(!flags1[0]); // should see no input
    assert(flags1[1]);  // should see output
    assert(! flags1[2]);  // watch not removed

    close(pipe1[0]);
    close(pipe1[1]);
}

void ftest_bidi_fd_watch3()
{
    using Loop_t = dasynq::event_loop<checking_mutex>;
    Loop_t my_loop;

    bool flags1[3] = { false, false, false };  // in, out, removed

    int pipe1[2];
    create_bidi_pipe(pipe1);

    class MyBidiWatcher : public Loop_t::bidi_fd_watcher_impl<MyBidiWatcher> {
        bool (&flags)[3];

        public:
        MyBidiWatcher(bool (&flags_a)[3]) : flags(flags_a)
        {
        }

        rearm read_ready(Loop_t &eloop, int fd) noexcept
        {
            flags[0] = true;
            char rbuf;
            read(fd, &rbuf, 1);
            return rearm::REARM;
        }

        rearm write_ready(Loop_t &eloop, int fd) noexcept
        {
            flags[1] = true;
            return rearm::NOOP;
        }

        void watch_removed() noexcept override
        {
            flags[2] = true;
        }
    };

    MyBidiWatcher watch {flags1};

    watch.add_watch(my_loop, pipe1[0], dasynq::IN_EVENTS | dasynq::OUT_EVENTS);

    my_loop.run(); // write watch should trigger, and remain disabled

    assert(flags1[1]);
    assert(!flags1[0]);
    assert(!flags1[2]);

    watch.set_watches(my_loop, dasynq::IN_EVENTS | dasynq::OUT_EVENTS);

    char wbuf = 'a';
    write(pipe1[1], &wbuf, 1);

    my_loop.run();

    assert(flags1[0] && flags1[1]);   // in and out flag should be set
    assert(! flags1[2]);  // watch not removed (write watch not removed)

    flags1[0] = false;
    flags1[1] = false;

    // Send more to pipe
    write(pipe1[1], &wbuf, 1);

    my_loop.run();

    assert(flags1[0]); // should see input
    assert(! flags1[1]);  // should see output
    assert(! flags1[2]);  // watch not removed

    close(pipe1[0]);
    close(pipe1[1]);
}

void ftest_sig_watch1()
{
    using Loop_t = dasynq::event_loop<checking_mutex>;
    Loop_t my_loop;

    bool seen1 = false;
    bool seen2 = false;

    using siginfo_p = Loop_t::signal_watcher::siginfo_p;

    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGUSR1);
    sigaddset(&sigmask, SIGUSR2);
    sigprocmask(SIG_BLOCK, &sigmask, nullptr);

    Loop_t::signal_watcher::add_watch(my_loop, SIGUSR1,
            [&seen1](Loop_t &eloop, int signo, siginfo_p info) -> rearm {
        seen1 = true;
        return rearm::REMOVE;
    });

    Loop_t::signal_watcher::add_watch(my_loop, SIGUSR2,
            [&seen2](Loop_t &eloop, int signo, siginfo_p info) -> rearm {
        seen2 = true;
        return rearm::REMOVE;
    });

    // We avoid using raise(...) since a bug in some versions of MacOS prevents signals generated
    // using raise from being detected by kqueue (and therefore by Dasynq).

    // raise(SIGUSR1);
    kill(getpid(), SIGUSR1);

    my_loop.run();

    assert(seen1);
    assert(!seen2);

    seen1 = false;

    //raise(SIGUSR1);
    //raise(SIGUSR2);
    kill(getpid(), SIGUSR1);
    kill(getpid(), SIGUSR2);

    my_loop.run();

    assert(!seen1);
    assert(seen2);
}

void ftest_sig_watch2()
{
    using Loop_t = dasynq::event_loop<checking_mutex>;
    Loop_t my_loop;

    bool seen1 = false;

    using siginfo_p = Loop_t::signal_watcher::siginfo_p;

    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &sigmask, nullptr);

    auto *swatch = Loop_t::signal_watcher::add_watch(my_loop, SIGUSR1,
            [&seen1](Loop_t &eloop, int signo, siginfo_p info) -> rearm {
        seen1 = true;
        return rearm::REARM;
    });

    // We avoid using raise(...) since a bug in some versions of MacOS prevents signals generated
    // using raise from being detected by kqueue (and therefore by Dasynq).

    // raise(SIGUSR1);
    kill(getpid(), SIGUSR1);

    my_loop.run();

    assert(seen1);

    seen1 = false;

    //raise(SIGUSR1);
    kill(getpid(), SIGUSR1);

    my_loop.run();

    assert(seen1);

    swatch->deregister(my_loop);
}

void ftest_multi_thread1()
{
    using Loop_t = dasynq::event_loop<std::mutex>;
    Loop_t my_loop;

    bool seen1 = false;
    bool seen2 = false;

    int pipe1[2];
    int pipe2[2];
    create_pipe(pipe1);
    create_pipe(pipe2);

    char wbuf[1] = {'a'};
    write(pipe2[1], wbuf, 1);

    auto fwatch = Loop_t::fd_watcher::add_watch(my_loop, pipe1[0], dasynq::IN_EVENTS,
            [&seen1](Loop_t &eloop, int fd, int flags) -> rearm {
        seen1 = true;
        return rearm::REMOVE;
    });

    std::thread t([&my_loop]() -> void {
        my_loop.run();
    });

    fwatch->deregister(my_loop);

    Loop_t::fd_watcher::add_watch(my_loop, pipe2[0], dasynq::IN_EVENTS,
            [&seen2](Loop_t &eloop, int fd, int flags) -> rearm {
        seen2 = true;
        return rearm::REMOVE;
    });

    // join thread
    t.join();

    assert(seen2);
    assert(!seen1);

    close(pipe1[0]);
    close(pipe1[1]);
    close(pipe2[0]);
    close(pipe2[1]);
}

void ftest_multi_thread2()
{
    using Loop_t = dasynq::event_loop<std::mutex>;
    Loop_t my_loop;

    bool seen1 = false;

    int pipe1[2];
    //int pipe2[2];
    create_pipe(pipe1);

    auto fwatch = Loop_t::fd_watcher::add_watch(my_loop, pipe1[0], dasynq::IN_EVENTS,
            [&seen1](Loop_t &eloop, int fd, int flags) -> rearm {
        seen1 = true;
        return rearm::REMOVE;
    });

    char wbuf[1] = {'a'};
    write(pipe1[1], wbuf, 1);

    struct timespec t200ms;
    t200ms.tv_sec = 0;
    t200ms.tv_nsec = 200 * 1000 * 1000;
    nanosleep(&t200ms, nullptr);

    std::thread t([&my_loop,fwatch]() -> void {
        fwatch->deregister(my_loop);
    });

    t.join();

    my_loop.poll();

    assert(!seen1);

    close(pipe1[0]);
    close(pipe1[1]);
}

void ftest_child_watch()
{
    using loop_t = dasynq::event_loop<std::mutex>;
    loop_t my_loop;

    class my_child_proc_watcher : public loop_t::child_proc_watcher_impl<my_child_proc_watcher>
    {
        public:
        bool did_exit = false;

        rearm status_change(loop_t &, pid_t child, int status)
        {
            did_exit = true;
            return rearm::DISARM;
        }
    };

    pid_t fake_child_pid = 0;
    my_child_proc_watcher my_child_watcher;
    my_child_watcher.add_watch(my_loop, fake_child_pid);

    if (my_child_watcher.fork(my_loop, false) == 0) {
        // child
        _exit(0);
    }

    my_loop.run();
    assert(my_child_watcher.did_exit);
}

int main(int argc, char **argv)
{
    std::cout << "test_fd_watch1... ";
    test_fd_watch1();
    std::cout << "PASSED" << std::endl;

    std::cout << "test_fd_watch2... ";
    test_fd_watch2();
    std::cout << "PASSED" << std::endl;

    std::cout << "test_fd_watch3... ";
    test_fd_watch3();
    std::cout << "PASSED" << std::endl;

    std::cout << "test_fd_emu... ";
    test_fd_emu();
    std::cout << "PASSED" << std::endl;

    std::cout << "testTimespecDiv... ";
    testTimespecDiv();
    std::cout << "PASSED" << std::endl;

    std::cout << "test_timers_1... ";
    test_timers_1();
    std::cout << "PASSED" << std::endl;

    std::cout << "test_timers_2... ";
    test_timers_2();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftest_fd_watch1... ";
    ftest_fd_watch1();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftest_bidi_fd_watch1... ";
    ftest_bidi_fd_watch1();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftest_bidi_fd_watch2... ";
    ftest_bidi_fd_watch2();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftest_bidi_fd_watch3... ";
    ftest_bidi_fd_watch3();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftest_sig_watch1... ";
    ftest_sig_watch1();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftest_sig_watch2... ";
    ftest_sig_watch2();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftest_multi_thread1... ";
    ftest_multi_thread1();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftest_multi_thread2... ";
    ftest_multi_thread2();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftest_child_watch... ";
    ftest_child_watch();
    std::cout << "PASSED" << std::endl;

    return 0;
}

#include <sys/socket.h>
#include <sys/un.h>

#include <cassert>
#include <iostream>
#include <thread>

#include "testbackend.h"
#include "dasynq.h"

using Loop_t = dasynq::event_loop<dasynq::NullMutex, dasynq::test_loop, dasynq::test_loop_traits>;

using dasynq::rearm;
using dasynq::test_io_engine;

// Set up two file descriptor watches on two different descriptors, and make sure the correct handler
// is triggered when input is available on each.
void testFdWatch1()
{
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
void testFdWatch2()
{
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

static void test_timer_base_processing()
{
    using dasynq::timer_handle_t;
    using dasynq::timer_queue_t;

    class tbase {
        public:
        std::vector<void *> received_expirations;

        void receiveTimerExpiry(dasynq::timer_handle_t & thandle, void *userdata, int expiry_count)
        {
            received_expirations.push_back(userdata);
        }
    };

    class ttb_t : public dasynq::timer_base<tbase>
    {
        public:
        void process(timer_queue_t &queue, struct timespec curtime)
        {
            dasynq::timer_base<tbase>::process_timer_queue(queue, curtime);
        }
    };

    ttb_t ttb;

    timer_queue_t timer_queue;

    // First timer is a one-shot timer expiring at 3 seconds:
    timer_handle_t hnd1;
    timer_queue.allocate(hnd1);
    timer_queue.node_data(hnd1).interval_time = {0, 0};
    timer_queue.node_data(hnd1).userdata = &hnd1;
    timer_queue.insert(hnd1, {3, 0});

    // Second timer expires at 4 seconds and then every 1 second after:
    timer_handle_t hnd2;
    timer_queue.allocate(hnd2);
    timer_queue.node_data(hnd2).interval_time = {1, 0};
    timer_queue.node_data(hnd2).userdata = &hnd2;
    timer_queue.insert(hnd2, {4, 0});

    ttb.process(timer_queue, {0, 0});
    assert(ttb.received_expirations.empty());

    ttb.process(timer_queue, {1, 0});
    assert(ttb.received_expirations.empty());

    ttb.process(timer_queue, {3, 0});
    assert(ttb.received_expirations.size() == 1);
    assert(ttb.received_expirations[0] == &hnd1);

    ttb.process(timer_queue, {4, 5});
    assert(ttb.received_expirations.size() == 2);
    assert(ttb.received_expirations[1] == &hnd2);
    timer_queue.node_data(hnd2).enabled = true;

    ttb.process(timer_queue, {5, 5});
    assert(ttb.received_expirations.size() == 3);
    assert(ttb.received_expirations[2] == &hnd2);
    timer_queue.node_data(hnd2).enabled = true;

    ttb.process(timer_queue, {6, 0});
    assert(ttb.received_expirations.size() == 4);
    assert(ttb.received_expirations[3] == &hnd2);
    // don't re-enable the timer this time: shouldn't fire again

    ttb.process(timer_queue, {7, 0});
    assert(ttb.received_expirations.size() == 4);
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

void ftestFdWatch1()
{
    using Loop_t = dasynq::event_loop<dasynq::NullMutex>;
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

void ftestBidiFdWatch1()
{
    using Loop_t = dasynq::event_loop<dasynq::NullMutex>;
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

void ftestBidiFdWatch2()
{
    using Loop_t = dasynq::event_loop<dasynq::NullMutex>;
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

void ftestBidiFdWatch3()
{
    using Loop_t = dasynq::event_loop<dasynq::NullMutex>;
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

void ftestSigWatch()
{
    using Loop_t = dasynq::event_loop<dasynq::NullMutex>;
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

    // test_io_engine::trigger_fd_event(0, dasynq::IN_EVENTS);
    raise(SIGUSR1);

    my_loop.run();

    assert(seen1);
    assert(!seen2);

    seen1 = false;

    //test_io_engine::trigger_fd_event(0, dasynq::IN_EVENTS);
    //test_io_engine::trigger_fd_event(1, dasynq::IN_EVENTS);
    raise(SIGUSR1);
    raise(SIGUSR2);

    my_loop.run();

    assert(!seen1);
    assert(seen2);
}

void ftestMultiThread1()
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

int main(int argc, char **argv)
{
    std::cout << "testFdWatch1... ";
    testFdWatch1();
    std::cout << "PASSED" << std::endl;

    std::cout << "testFdWatch2... ";
    testFdWatch2();
    std::cout << "PASSED" << std::endl;

    std::cout << "testTimespecDiv... ";
    testTimespecDiv();
    std::cout << "PASSED" << std::endl;

    std::cout << "test_timer_base_processing... ";
    test_timer_base_processing();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftestFdWatch1... ";
    ftestFdWatch1();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftestBidiFdWatch1... ";
    ftestBidiFdWatch1();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftestBidiFdWatch2... ";
    ftestBidiFdWatch2();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftestBidiFdWatch3... ";
    ftestBidiFdWatch3();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftestSigWatch... ";
    ftestSigWatch();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftestMultiThread1... ";
    ftestMultiThread1();
    std::cout << "PASSED" << std::endl;
    
    return 0;
}

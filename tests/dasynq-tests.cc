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
    
    Loop_t::FdWatcher::addWatch(my_loop, 0, dasynq::IN_EVENTS,
            [&seen1](Loop_t &eloop, int fd, int flags) -> rearm {
        seen1 = true;
        return rearm::REMOVE;
    });
    
    Loop_t::FdWatcher::addWatch(my_loop, 1, dasynq::IN_EVENTS,
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
    
    auto watcher1 = Loop_t::FdWatcher::addWatch(my_loop, 0, dasynq::IN_EVENTS,
            [&seen1](Loop_t &eloop, int fd, int flags) -> rearm {
        seen1 = true;
        return rearm::REARM;
    });

    auto watcher2 = Loop_t::FdWatcher::addWatch(my_loop, 1, dasynq::IN_EVENTS,
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

static void create_pipe(int filedes[2])
{
    if (pipe(filedes) == -1) {
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

    Loop_t::FdWatcher::addWatch(my_loop, pipe1[0], dasynq::IN_EVENTS,
            [&seen1](Loop_t &eloop, int fd, int flags) -> rearm {
        seen1 = true;
        return rearm::REMOVE;
    });

    Loop_t::FdWatcher::addWatch(my_loop, pipe2[0], dasynq::IN_EVENTS,
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

void ftestSigWatch()
{
    using Loop_t = dasynq::event_loop<dasynq::NullMutex>;
    Loop_t my_loop;

    bool seen1 = false;
    bool seen2 = false;

    using SigInfo_p = Loop_t::SignalWatcher::SigInfo_p;

    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGUSR1);
    sigaddset(&sigmask, SIGUSR2);
    sigprocmask(SIG_BLOCK, &sigmask, nullptr);

    Loop_t::SignalWatcher::addWatch(my_loop, SIGUSR1,
            [&seen1](Loop_t &eloop, int signo, SigInfo_p info) -> rearm {
        seen1 = true;
        return rearm::REMOVE;
    });

    Loop_t::SignalWatcher::addWatch(my_loop, SIGUSR2,
            [&seen2](Loop_t &eloop, int signo, SigInfo_p info) -> rearm {
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

    auto fwatch = Loop_t::FdWatcher::addWatch(my_loop, pipe1[0], dasynq::IN_EVENTS,
            [&seen1](Loop_t &eloop, int fd, int flags) -> rearm {
        seen1 = true;
        return rearm::REMOVE;
    });

    std::thread t([&my_loop]() -> void {
        my_loop.run();
    });

    fwatch->deregister(my_loop);

    Loop_t::FdWatcher::addWatch(my_loop, pipe2[0], dasynq::IN_EVENTS,
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

    std::cout << "ftestFdWatch1... ";
    ftestFdWatch1();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftestSigWatch... ";
    ftestSigWatch();
    std::cout << "PASSED" << std::endl;

    std::cout << "ftestMultiThread1... ";
    ftestMultiThread1();
    std::cout << "PASSED" << std::endl;
    
    return 0;
}

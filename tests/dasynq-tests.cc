#include <cassert>
#include <iostream>

#include "testbackend.h"

namespace dasynq {
    template <typename T> using Loop = test_loop<T>;
    using LoopTraits = test_loop_traits;
}

#define DASYNQ_CUSTOM_LOOP_IMPLEMENTATION 1
#include "dasynq.h"


using Loop_t = dasynq::EventLoop<dasynq::NullMutex>;

using dasynq::Rearm;
using dasynq::test_io_engine;

// Set up two file descriptor watches on two different descriptors, and make sure the correct handler
// is triggered when input is available on each.
void testFdWatch1()
{
    Loop_t my_loop;
    
    bool seen1 = false;
    bool seen2 = false;
    
    Loop_t::FdWatcher::addWatch(my_loop, 0, dasynq::IN_EVENTS,
            [&seen1](Loop_t &eloop, int fd, int flags) -> Rearm {
        seen1 = true;
        return Rearm::REMOVE;
    });
    
    Loop_t::FdWatcher::addWatch(my_loop, 1, dasynq::IN_EVENTS,
            [&seen2](Loop_t &eloop, int fd, int flags) -> Rearm {
        seen2 = true;
        return Rearm::REMOVE;
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
            [&seen1](Loop_t &eloop, int fd, int flags) -> Rearm {
        seen1 = true;
        return Rearm::REARM;
    });

    auto watcher2 = Loop_t::FdWatcher::addWatch(my_loop, 1, dasynq::IN_EVENTS,
            [&seen2](Loop_t &eloop, int fd, int flags) -> Rearm {
        seen2 = true;
        return Rearm::DISARM;
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

int main(int argc, char **argv)
{
    std::cout << "testFdWatch1... ";
    testFdWatch1();
    std::cout << "PASSED" << std::endl;

    std::cout << "testFdWatch2... ";
    testFdWatch2();
    std::cout << "PASSED" << std::endl;
    
    return 0;
}

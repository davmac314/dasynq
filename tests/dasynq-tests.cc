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

void testFdWatch1()
{
    Loop_t my_loop;

    class MyFdWatcher : public Loop_t::FdWatcher
    {
        public:
        bool * seen;
    
        Rearm fdEvent(Loop_t &eloop, int fd, int flags) override
        {
            *seen = true;
            return Rearm::REARM;
        }
    };
    
    bool seen1 = false;
    MyFdWatcher myFdWatcher1;
    myFdWatcher1.seen = &seen1;
    
    bool seen2 = false;
    MyFdWatcher myFdWatcher2;
    myFdWatcher2.seen = &seen2;
    
    myFdWatcher1.addWatch(my_loop, 0, dasynq::IN_EVENTS);
    myFdWatcher2.addWatch(my_loop, 1, dasynq::IN_EVENTS);

    test_io_engine::trigger_fd_event(0, dasynq::IN_EVENTS);
    
    my_loop.run();
    
    assert(seen1);
    assert(!seen2);
    
    seen1 = false;
    
    test_io_engine::trigger_fd_event(1, dasynq::IN_EVENTS);
        
    my_loop.run();
    
    assert(!seen1);
    assert(seen2);
}

int main(int argc, char **argv)
{
    std::cout << "testFdWatch1... ";
    testFdWatch1();
    std::cout << "PASSED" << std::endl;
    
    return 0;
}

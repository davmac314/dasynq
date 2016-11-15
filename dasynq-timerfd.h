#include <vector>
#include <utility>

#include <sys/timerfd.h>
#include <time.h>

#include "dasynq-binaryheap.h"

namespace dasynq {

// We could use one timerfd per timer, but then we need to differentiate timer
// descriptors from regular file descriptors when events are reported by the loop
// mechanism so that we can correctly report a timer event or fd event.

// With a file descriptor or signal, we can use the item itself as the identifier for
// adding/removing watches. For timers, it's more complicated. When we add a timer,
// we are given a handle; we need to use this to modify the watch.

// We maintain two vectors;
//   - One with timerdata; a timer handle is an index into this vector. The timer data
//     includes the current index of this timer in the heap vector (heap_index).
//   - heap vector; times, and timer handles. When the position on the heap changes,
//     the timer data heap_index value must be updated accordingly.
//
// So each running timer has one entry in each vector. A stopped timer only has an
// entry in the main vector, not in the queue. A disabled timer, however, retains its
// queue entry (and continues to count, but not report, interval expirations).


class TimerData
{
    public:
    // initial time?
    struct timespec interval_time; // interval (if 0, one-off timer)
    int heap_index; // current heap index, -1 if inactive
    int expiry_count;  // number of times expired
    void *userdata;
    
    TimerData(void *udata) : interval_time({0,0}), heap_index(-1), expiry_count(0), userdata(udata)
    {
        // constructor
    }
};

class CompareTimespec
{
    public:
    bool operator()(struct timespec &a, struct timespec &b)
    {
        if (a.tv_sec < b.tv_sec) {
            return true;
        }
        
        if (a.tv_sec == b.tv_sec) {
            return a.tv_nsec < b.tv_nsec;
        }
        
        return false;
    }
};


template <class Base> class TimerFdEvents : public Base
{
    private:
    int timerfd_fd = -1;

    BinaryHeap<TimerData, struct timespec, CompareTimespec> timer_queue;

    
    static int divide_timespec(const struct timespec &num, const struct timespec &den)
    {
        // TODO
        return 0;
    }
    
    void set_timer_from_queue()
    {
        struct itimerspec newtime;
        if (timer_queue.empty()) {
            newtime.it_value = {0, 0};
            newtime.it_interval = {0, 0};
        }
        else {
            newtime.it_value = timer_queue.get_root_priority();
            newtime.it_interval = {0, 0};
        }
        timerfd_settime(timerfd_fd, TFD_TIMER_ABSTIME, &newtime, nullptr);
    }
    
    public:
    template <typename T>
    void receiveFdEvent(T &loop_mech, typename Base::FD_r fd_r, void * userdata, int flags)
    {
        if (userdata == &timerfd_fd) {
            struct timespec curtime;
            clock_gettime(CLOCK_MONOTONIC, &curtime); // in theory, can't fail on Linux
            
            // Peek timer queue; calculate difference between current time and timeout
            struct timespec * timeout = &timer_queue.get_root_priority();
            while (timeout->tv_sec < curtime.tv_sec || (timeout->tv_sec == curtime.tv_sec &&
                    timeout->tv_nsec <= curtime.tv_nsec)) {
                // Increment expiry count
                timer_queue.node_data(timer_queue.get_root()).expiry_count++;
                // (a periodic timer may have overrun; calculated below).
                
                timespec &interval = timer_queue.node_data(timer_queue.get_root()).interval_time;
                if (interval.tv_sec == 0 && interval.tv_nsec == 0) {
                    // Non periodic timer
                    int thandle = timer_queue.get_root();
                    timer_queue.pull_root();
                    int expiry_count = timer_queue.node_data(thandle).expiry_count;
                    timer_queue.node_data(thandle).expiry_count = 0;
                    Base::receiveTimerExpiry(thandle, timer_queue.node_data(thandle).userdata, expiry_count);
                    if (timer_queue.empty()) {
                        break;
                    }
                }
                else {
                    // Periodic timer TODO
                    // First calculate the overrun in time:
                    /*
                    struct timespec diff;
                    diff.tv_sec = curtime.tv_sec - timeout->tv_sec;
                    diff.tv_nsec = curtime.tv_nsec - timeout->tv_nsec;
                    if (diff.tv_nsec < 0) {
                        diff.tv_nsec += 1000000000;
                        diff.tv_sec--;
                    }
                    */
                    // Now we have to divide the time overrun by the period to find the
                    // interval overrun. This requires a division of a value not representable
                    // as a long...
                    // TODO use divide_timespec
                    // TODO better not to remove from queue maybe, but instead mark as inactive,
                    // adjust timeout, and bubble into correct position
                    // call Base::receieveTimerEvent
                    // TODO
                }
                
                // repeat until all expired timeouts processed
                // timeout = &timer_queue[0].timeout;
                //  (shouldn't be necessary; address hasn't changed...)
            }
            // arm timerfd with timeout from head of queue
            set_timer_from_queue();
        }
        else {
            Base::receiveFdEvent(loop_mech, fd_r, userdata, flags);
        }
    }

    template <typename T> void init(T *loop_mech)
    {
        timerfd_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
        if (timerfd_fd == -1) {
            throw std::system_error(errno, std::system_category());
        }
        loop_mech->addFdWatch(timerfd_fd, &timerfd_fd, IN_EVENTS);
        Base::init(loop_mech);
    }

    // Add timer, return handle (TODO: clock id param?)
    int addTimer(void *userdata)
    {
        int h;
        timer_queue.allocate(h, userdata);
        return h;
    }
    
    void removeTimer(int timer_id) noexcept
    {
        // TODO
    }
    
    void removeTimer_nolock(int timer_id) noexcept
    {
        // TODO
    }
    
    // starts (if not started) a timer to timeout at the given time. Resets the expiry count to 0.
    //   enable: specifies whether to enable reporting of timeouts/intervals
    void setTimer(int timer_id, struct timespec &timeout, struct timespec &interval, bool enable) noexcept
    {
        auto &ts = timer_queue.node_data(timer_id);
        ts.interval_time = interval;
        ts.expiry_count = 0;
        
        // TODO check if timer already active, deal with appropriately
        
        if (timer_queue.insert(timer_id, timeout)) {
            set_timer_from_queue();
        }
        
        // TODO locking (here and everywhere)
    }

    // Set timer relative to current time:    
    void setTimerRel(int timer_id, struct timespec &timeout, struct timespec &interval, bool enable) noexcept
    {
        // TODO consider caching current time somehow; need to decide then when to update cached value.
        struct timespec curtime;
        clock_gettime(CLOCK_MONOTONIC, &curtime);
        curtime.tv_sec += timeout.tv_sec;
        curtime.tv_nsec += timeout.tv_nsec;
        if (curtime.tv_nsec > 1000000000) {
            curtime.tv_nsec -= 1000000000;
            curtime.tv_sec++;
        }
        setTimer(timer_id, curtime, interval, enable);
    }
    
    // Enables or disabling report of timeouts (does not stop timer)
    void enableTimer(int timer_id, bool enable) noexcept
    {
        // TODO
    }
    
    void enableTimer_nolock(int timer_id, bool enable) noexcept
    {
        // TODO
    }
};

}

#include <vector>
#include <utility>

#include <sys/timerfd.h>
#include <time.h>

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

// Free timer data node; links to next free node
class TimerDataFree
{
    public:
    int next_free; // -1 if last in chain
};

union TimerDataU
{
    TimerData td;
    TimerDataFree tf;
    
    TimerDataU(void *udata) : td(udata)
    {
    }
};

struct TimerHeapEntry
{
    // public:
    struct timespec timeout;
    int timer_handle;
    
    TimerHeapEntry(struct timespec &time, int hndl) : timeout(time), timer_handle(hndl)
    {
    }
};



// TODO implement pairing heap with non-moving elements for better performance
// than the current binary heap.

// We can't use std::priority_queue because it won't update TimerData heap_index pointers
class TimerQueue
{
    // less-than comparison
    static bool lt(TimerHeapEntry &a, TimerHeapEntry &b)
    {
        if (a.timeout.tv_sec < b.timeout.tv_sec) return true;
        if (a.timeout.tv_sec == b.timeout.tv_sec) {
            return a.timeout.tv_nsec < b.timeout.tv_nsec;
        }
        return false;
    }
    
    
    public:
    static bool lt(struct timespec &a, struct timespec &b)
    {
        if (a.tv_sec < b.tv_sec) return true;
        if (a.tv_sec == b.tv_sec) {
            return a.tv_nsec < b.tv_nsec;
        }
        return false;
    }

    // Remove from the queue: used if the timer has expired and is not periodic
    static void pull(std::vector<TimerHeapEntry> &v, std::vector<TimerDataU> &d)
    {
        if (v.size() > 1) {
            // replace the first element with the last:
            d[v[0].timer_handle].td.heap_index = -1;
            d[v.back().timer_handle].td.heap_index = 0;
            v[0] = v.back();
            v.pop_back();
            
            // Now bubble up:
            bubble_up(v, d);
        }
        else {
            v.pop_back();
        }
    }

    // Bubble an adjusted timer up to the correct position
    static void bubble_up(std::vector<TimerHeapEntry> &v, std::vector<TimerDataU> &d, int pos = 0)
    {
        int rmax = v.size();
        int max = (rmax - 1) / 2;

        while (pos <= max) {
            int selchild;
            int lchild = pos * 2 + 1;
            int rchild = lchild + 1;
            if (rchild >= rmax) {
                selchild = lchild;
            }
            else {
                // select the sooner of lchild and rchild
                selchild = lt(v[lchild], v[rchild]) ? lchild : rchild;
            }
            
            if (! lt(v[selchild], v[pos])) {
                break;
            }
            
            std::swap(d[v[selchild].timer_handle].td.heap_index, d[v[pos].timer_handle].td.heap_index);
            std::swap(v[selchild], v[pos]);
            pos = selchild;
        }
    
    }
    
    static bool bubble_down(std::vector<TimerHeapEntry> &v, std::vector<TimerDataU> &d)
    {
        return bubble_down(v, d, v.size() - 1);
    }
    
    // Bubble a newly added timer down to the correct position
    static bool bubble_down(std::vector<TimerHeapEntry> &v, std::vector<TimerDataU> &d, int pos)
    {
        // int pos = v.size() - 1;
        while (pos > 0) {
            int parent = (pos - 1) / 2;
            if (! lt(v[pos], v[parent])) {
                break;
            }
            
            std::swap(v[pos], v[parent]);
            std::swap(d[v[pos].timer_handle].td.heap_index, d[v[parent].timer_handle].td.heap_index);
            pos = parent;
        }
        
        return pos == 0;
    }
};


template <class Base> class TimerFdEvents : public Base
{
    private:
    int timerfd_fd = -1;
    int first_free_td = -1; // index of first free timerdata slot in vector
    int num_timers = 0;
    
    std::vector<TimerDataU> timer_vec;
    std::vector<TimerHeapEntry> timer_queue;
    
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
            newtime.it_value = timer_queue[0].timeout;
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
            struct timespec * timeout = &timer_queue[0].timeout;
            while (timeout->tv_sec < curtime.tv_sec || (timeout->tv_sec == curtime.tv_sec &&
                    timeout->tv_nsec <= curtime.tv_nsec)) {
                // Increment expiry count
                int thandle = timer_queue[0].timer_handle;
                timer_vec[thandle].td.expiry_count++;
                // (a periodic timer may have overrun; calculated below).
                
                timespec &interval = timer_vec[thandle].td.interval_time;
                if (interval.tv_sec == 0 && interval.tv_nsec == 0) {
                    // Non periodic timer
                    TimerQueue::pull(timer_queue, timer_vec);
                    int expiry_count = timer_vec[thandle].td.expiry_count;
                    timer_vec[thandle].td.expiry_count = 0;
                    Base::receiveTimerExpiry(thandle, timer_vec[thandle].td.userdata, expiry_count);
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
        if (first_free_td == -1) {
            h = timer_vec.size();
            timer_vec.emplace_back(userdata);
        }
        else {
            h = first_free_td;
            first_free_td = timer_vec[h].tf.next_free;
            new (&timer_vec[h].td) TimerData(userdata);
        }
        
        num_timers++;
        timer_queue.reserve(num_timers);
        
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
        timer_vec[timer_id].td.interval_time = interval;
        timer_vec[timer_id].td.expiry_count = 0;
    
        // TODO locking (here and everywhere)
        if (timer_vec[timer_id].td.heap_index == -1) {
            timer_vec[timer_id].td.heap_index = timer_queue.size();
            timer_queue.emplace_back(timeout, timer_vec[timer_id].td.heap_index);
            if (TimerQueue::bubble_down(timer_queue, timer_vec)) {
                set_timer_from_queue();
            }
        }
        else {
            int heap_index = timer_vec[timer_id].td.heap_index;
            if (TimerQueue::lt(timeout, timer_queue[heap_index].timeout)) {
                timer_queue[timer_vec[timer_id].td.heap_index].timeout = timeout;
                if (TimerQueue::bubble_down(timer_queue, timer_vec, heap_index)) {
                    set_timer_from_queue();            
                }
            }
            else {
                auto heap_index = timer_vec[timer_id].td.heap_index;
                timer_queue[heap_index].timeout = timeout;
                TimerQueue::bubble_up(timer_queue, timer_vec, heap_index);
                if (heap_index == 0) {
                    // We moved the next expiring timer back, adjust the timerfd:
                    set_timer_from_queue();
                }
            }
        }
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

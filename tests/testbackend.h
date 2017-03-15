#include "dasynq-flags.h"
#include "dasynq-binaryheap.h"

#include <vector>
#include <memory>
#include <unordered_map>

namespace dasynq {

// An interface for a receiver of I/O events
class io_receiver
{
    public:
    virtual void receiveFdEvent(int fd_num, int events) = 0;
};

class event
{
    public:
    virtual void fire(io_receiver &) = 0;
};

class fd_event : public event
{
    int fd;
    int events;
    
    public:
    fd_event(int fd, int events)
    {
        this->fd = fd;
        this->events = events;
    }

    void fire(io_receiver &r) override
    {
        r.receiveFdEvent(fd, events);
    }
};

static std::vector<std::unique_ptr<event>> event_queue;

class test_io_engine
{
    
    public:
    static void trigger_fd_event(int fd_num, int events)
    {
        event_queue.emplace_back(new fd_event(fd_num, events));
    }
    
    static void pull_events(io_receiver &receiver)
    {
        while (! event_queue.empty()) {
            auto b = event_queue.begin();
            std::unique_ptr<event> eptr;
            eptr.swap(*b);
            event_queue.erase(b);
            eptr->fire(receiver);
        }
    }
};


class TimerData
{
    public:
    // initial time?
    struct timespec interval_time; // interval (if 0, one-off timer)
    int expiry_count;  // number of times expired
    bool enabled;   // whether timer reports events  
    void *userdata;
    
    TimerData(void *udata = nullptr) : interval_time({0,0}), expiry_count(0), enabled(true), userdata(udata)
    {
        // constructor
    }
};

class CompareTimespec
{
    public:
    bool operator()(const struct timespec &a, const struct timespec &b)
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

using timer_handle_t = BinaryHeap<TimerData, struct timespec, CompareTimespec>::handle_t;

static void init_timer_handle(timer_handle_t &hnd) noexcept
{
    BinaryHeap<TimerData, struct timespec, CompareTimespec>::init_handle(hnd);
}

class test_loop_traits
{
    public:
    class SigInfo
    {
        public:
        int get_signo()
        {
            return 0; // TODO
        }
    };
    
    constexpr static bool has_separate_rw_fd_watches = false;
    
    class FD_r;

    // File descriptor optional storage. If the mechanism can return the file descriptor, this
    // class will be empty, otherwise it can hold a file descriptor.
    class FD_s {
        friend class FD_r;
        
        // Epoll doesn't return the file descriptor (it can, but it can't return both file
        // descriptor and user data).
        int fd;
    };

    // File descriptor reference (passed to event callback). If the mechanism can return the
    // file descriptor, this class holds the file descriptor. Otherwise, the file descriptor
    // must be stored in an FD_s instance.
    class FD_r {
        public:
        int getFd(FD_s ss)
        {
            return ss.fd;
        }
    };    
};

template <class Base> class test_loop : public Base, io_receiver
{
    using timer_handle_t = BinaryHeap<TimerData, struct timespec, CompareTimespec>::handle_t;
    
    struct fd_data
    {
        void *userdata;
        int events;
    };
    
    std::unordered_map<int, fd_data> fd_data_map;

    public:    
    
    void pullEvents(bool b)
    {
        test_io_engine::pull_events(*this);
    }
    
    void interrupt_wait()
    {
    
    }
    
    void addFdWatch(int fd, void * callback, int eventmask, bool enabled)
    {
        if (! enabled) eventmask = 0;
        fd_data data = {callback, eventmask};
        fd_data_map.insert({fd, data});
    }
    
    void enableTimer_nolock(timer_handle_t &hnd, bool enable)
    {
        // TODO
    }
    
    void removeTimer_nolock(timer_handle_t &hnd)
    {
        // TODO
    }
    
    void enableFdWatch_nolock(int fd_num, void *userdata, int events)
    {
        auto srch = fd_data_map.find(fd_num);
        if (srch != fd_data_map.end()) {
            srch->second.events = events;
            srch->second.userdata = userdata;
        }
    }
    
    void removeFdWatch(int fd, int events)
    {
        removeFdWatch_nolock(fd, events);
    }
    
    void removeFdWatch_nolock(int fd, int events)
    {
        fd_data_map.erase(fd);
    }
    
    void rearmSignalWatch_nolock(int signo)
    {
        // TODO
    }
     
    void removeSignalWatch_nolock(int signo)
    {
        // TODO
    }
    
    // Receive events from test queue:
    
    void receiveFdEvent(int fd_num, int events)
    {
        auto srch = fd_data_map.find(fd_num);
        if (srch != fd_data_map.end()) {
            auto &data = srch->second;
            if ((data.events & events) != 0) {
                int rep_events = data.events & events;
                if (data.events & ONE_SHOT) {
                    data.events &= ~IN_EVENTS & ~OUT_EVENTS;
                }
                Base::receiveFdEvent(*this, test_loop_traits::FD_r(), data.userdata, rep_events);
            }
        }
    }
};

}

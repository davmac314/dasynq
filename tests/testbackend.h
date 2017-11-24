#include <vector>
#include <memory>
#include <unordered_map>

#include "dasynq-config.h"
#include "dasynq-flags.h"
#include "dasynq-timerbase.h"
#include "dasynq-mutex.h"

namespace dasynq {

// An interface for a receiver of I/O events
class io_receiver
{
    public:
    virtual void receive_fd_event(int fd_num, int events) = 0;
};

class event
{
    public:
    virtual void fire(io_receiver &) = 0;
    virtual ~event() { }
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
        r.receive_fd_event(fd, events);
    }
};

static std::vector<std::unique_ptr<event>> event_queue;

class test_io_engine
{
    public:

    struct fd_data
    {
        void *userdata;
        int events;
        bool emulate;
    };

    static std::unordered_map<int, fd_data> fd_data_map;

    static void mark_fd_needs_emulation(int fd_num)
    {
        auto srch = fd_data_map.find(fd_num);
        if (srch != fd_data_map.end()) {
            auto &data = srch->second;
            data.emulate = true;
        }
        else {
            fd_data data = {nullptr, 0, true};
            fd_data_map.insert(std::make_pair(fd_num, data));
        }
    }

    static void clear_fd_data()
    {
        fd_data_map.clear();
    }

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

class test_loop_traits
{
    public:
    class sigdata_t
    {
        public:
        int get_signo()
        {
            return 0; // TODO
        }
    };
    
    constexpr static bool has_separate_rw_fd_watches = false;
    
    class fd_r;

    // File descriptor optional storage. If the mechanism can return the file descriptor, this
    // class will be empty, otherwise it can hold a file descriptor.
    class fd_s {
        friend class fd_r;
        
        // Epoll doesn't return the file descriptor (it can, but it can't return both file
        // descriptor and user data).
        int fd;
    };

    // File descriptor reference (passed to event callback). If the mechanism can return the
    // file descriptor, this class holds the file descriptor. Otherwise, the file descriptor
    // must be stored in an fd_s instance.
    class fd_r {
        public:
        int getFd(fd_s ss)
        {
            return ss.fd;
        }
    };    
};

template <class Base> class test_loop : public Base, io_receiver
{
    using timer_handle_t = timer_queue_t::handle_t;
    
    using fd_data = test_io_engine::fd_data;
    
    std::unordered_map<int, fd_data> & fd_data_map = test_io_engine::fd_data_map;

    public:    
    
    using reaper_mutex_t = null_mutex;

    void pull_events(bool b)
    {
        test_io_engine::pull_events(*this);
    }
    
    void interrupt_wait()
    {
    
    }
    
    bool add_fd_watch(int fd, void * callback, int eventmask, bool enabled, bool emulate = false)
    {
        if (! enabled) eventmask = 0;

        auto srch = fd_data_map.find(fd);
        if (srch != fd_data_map.end()) {
            if (srch->second.emulate) {
                if (emulate) return false;
                throw new std::system_error(ENOTSUP, std::system_category());
            }
            srch->second.events = eventmask;
            srch->second.userdata = callback;
            srch->second.emulate = false;
            return true;
        }

        fd_data data = {callback, eventmask, false};
        fd_data_map.insert({fd, data});
        return true;
    }
    
    void enable_timer_nolock(timer_handle_t &hnd, bool enable, clock_type clock = clock_type::MONOTONIC)
    {
        // TODO
    }
    
    void remove_timer_nolock(timer_handle_t &hnd, clock_type clock = clock_type::MONOTONIC)
    {
        // TODO
    }
    
    void enable_fd_watch_nolock(int fd_num, void *userdata, int events)
    {
        auto srch = fd_data_map.find(fd_num);
        if (srch != fd_data_map.end()) {
            srch->second.events = events;
            srch->second.userdata = userdata;
        }
        else {
            throw std::invalid_argument("trying to enable fd that is not watched");
        }
    }
    
    void disable_fd_watch_nolock(int fd_num, int events)
    {
        auto srch = fd_data_map.find(fd_num);
        if (srch != fd_data_map.end()) {
            srch->second.events = 0;
        }
        else {
            throw std::invalid_argument("trying to disable fd that is not watched");
        }
    }

    void remove_fd_watch(int fd, int events)
    {
        remove_fd_watch_nolock(fd, events);
    }
    
    void remove_fd_watch_nolock(int fd, int events)
    {
        fd_data_map.erase(fd);
    }
    
    void rearm_signal_watch_nolock(int signo, void *userdata)
    {
        // TODO
    }
     
    void remove_signal_watch_nolock(int signo)
    {
        // TODO
    }
    
    // Receive events from test queue:

    void receive_fd_event(int fd_num, int events)
    {
        auto srch = fd_data_map.find(fd_num);
        if (srch != fd_data_map.end()) {
            auto &data = srch->second;
            if ((data.events & events) != 0) {
                int rep_events = data.events & events;
                if (data.events & ONE_SHOT) {
                    data.events &= ~IN_EVENTS & ~OUT_EVENTS;
                }
                Base::receive_fd_event(*this, test_loop_traits::fd_r(), data.userdata, rep_events);
            }
        }
    }
};

}

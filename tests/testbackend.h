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

    static time_val cur_sys_time;
    static time_val cur_mono_time;

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
    constexpr static bool interrupt_after_fd_add = false;
    
    class fd_r;

    // File descriptor optional storage. If the mechanism can return the file descriptor, this
    // class will be empty, otherwise it can hold a file descriptor.
    class fd_s {
        friend class fd_r;
        
        // Epoll doesn't return the file descriptor (it can, but it can't return both file
        // descriptor and user data).
        int fd;

        public:
        fd_s(int fd) noexcept : fd(fd) { }
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

template <class Base> class test_loop : public timer_base<Base>, io_receiver
{
    using timer_handle_t = timer_queue_t::handle_t;
    
    using fd_data = test_io_engine::fd_data;
    
    std::unordered_map<int, fd_data> & fd_data_map = test_io_engine::fd_data_map;

    public:    
    
    using reaper_mutex_t = null_mutex;

    void pull_events(bool b)
    {
        if (! this->queue_for_clock(clock_type::MONOTONIC).empty()) {
            this->process_timer_queue(this->queue_for_clock(clock_type::MONOTONIC), test_io_engine::cur_mono_time);
        }
        if (! this->queue_for_clock(clock_type::SYSTEM).empty()) {
            this->process_timer_queue(this->queue_for_clock(clock_type::SYSTEM), test_io_engine::cur_sys_time);
        }
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
    
    bool add_bidi_fd_watch(int fd, void *userdata, int flags, bool emulate)
    {
        // No implementation.
        throw std::system_error(std::make_error_code(std::errc::not_supported));
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
    
    void remove_bidi_fd_watch(int fd) noexcept
    {
        // Shouldn't be called.
        remove_fd_watch(fd, IN_EVENTS | OUT_EVENTS);
    }

    void rearm_signal_watch_nolock(int signo, void *userdata)
    {
        // TODO
    }
     
    void remove_signal_watch_nolock(int signo)
    {
        // TODO
    }
    
    void set_timer(timer_handle_t &timer_id, const time_val &timeouttv, const time_val &intervaltv,
            bool enable, clock_type clock = clock_type::MONOTONIC) noexcept
    {
        auto &timer_queue = this->queue_for_clock(clock);
        timespec timeout = timeouttv;
        timespec interval = intervaltv;

        std::lock_guard<decltype(Base::lock)> guard(Base::lock);

        auto &ts = timer_queue.node_data(timer_id);
        ts.interval_time = interval;
        ts.expiry_count = 0;
        ts.enabled = enable;

        if (timer_queue.is_queued(timer_id)) {
            // Already queued; alter timeout
            if (timer_queue.set_priority(timer_id, timeout)) {
                // set_timer_from_queue();
            }
        }
        else {
            if (timer_queue.insert(timer_id, timeout)) {
                // set_timer_from_queue();
            }
        }
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

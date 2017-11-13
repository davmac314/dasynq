// Dasynq: early declarations and base watchers.
//
// Here we define watcher functionality that is not dependent on the event loop type. In particular,
// base classes for the various watcher types. These are not part of the public API.
//
// In general access to the members of the basewatcher should be protected by a mutex. The
// event_dispatch lock is used for this purpose.

namespace dasynq {

// Forward declarations:
template <typename T_Mutex, template <typename> class Loop = dasynq::Loop, typename LoopTraits = dasynq::LoopTraits>
class event_loop;

inline namespace {
    constexpr int DEFAULT_PRIORITY = 50;
}

namespace dprivate {
    // (non-public API)

    class base_watcher;
    using prio_queue = NaryHeap<dprivate::base_watcher *, int>;

    template <typename T_Loop> class fd_watcher;
    template <typename T_Loop> class bidi_fd_watcher;
    template <typename T_Loop> class signal_watcher;
    template <typename T_Loop> class child_proc_watcher;
    template <typename T_Loop> class timer;

    template <typename, typename> class fd_watcher_impl;
    template <typename, typename> class bidi_fd_watcher_impl;
    template <typename, typename> class signal_watcher_impl;
    template <typename, typename> class child_proc_watcher_impl;
    template <typename, typename> class timer_impl;

    enum class watch_type_t
    {
        SIGNAL,
        FD,
        CHILD,
        SECONDARYFD,
        TIMER
    };

    template <typename T_Mutex, typename Traits> class event_dispatch;

    // For FD watchers:
    // Use this watch flag to indicate that in and out events should be reported separately,
    // that is, watcher should not be disabled until all watched event types are queued.
    constexpr static int multi_watch = 4;

    // Represents a queued event notification. Various event watchers derive from this type.
    class base_watcher
    {
        template <typename T_Mutex, typename Traits> friend class event_dispatch;
        template <typename T_Mutex, template <typename> class, typename> friend class dasynq::event_loop;
        friend inline void basewatcher_set_active(base_watcher &watcher, bool active);
        friend inline bool basewatcher_get_deleteme(const base_watcher &watcher);
        friend inline bool basewatcher_get_emulatefd(const base_watcher &watcher);

        protected:
        watch_type_t watchType;
        int active : 1;    // currently executing handler?
        int deleteme : 1;  // delete when handler finished?
        int emulatefd : 1; // emulate file watch (by re-queueing)
        int emulate_enabled : 1;   // whether an emulated watch is enabled
        int child_termd : 1;  // child process has terminated

        prio_queue::handle_t heap_handle;
        int priority;

        static void set_priority(base_watcher &p, int prio)
        {
            p.priority = prio;
        }

        public:

        // Perform initialisation necessary before registration with an event loop
        void init()
        {
            active = false;
            deleteme = false;
            emulatefd = false;
            emulate_enabled = false;
            child_termd = false;
            prio_queue::init_handle(heap_handle);
            priority = DEFAULT_PRIORITY;
        }

        base_watcher(watch_type_t wt) noexcept : watchType(wt) { }
        base_watcher(const base_watcher &) = delete;
        base_watcher &operator=(const base_watcher &) = delete;

        // The dispatch function is called to process a watcher's callback. It is the "real" callback
        // function; it usually delegates to a user-provided callback.
        virtual void dispatch(void *loop_ptr) noexcept { };

        // Bi-directional file descriptor watches have a secondary dispatch function for the secondary
        // watcher (i.e. the output watcher):
        virtual void dispatch_second(void *loop_ptr) noexcept { }

        virtual ~base_watcher() noexcept { }

        // Called when the watcher has been removed.
        // It is guaranteed by the caller that:
        // - the dispatch method is not currently running
        // - the dispatch method will not be called.
        virtual void watch_removed() noexcept
        {
            // Later: the "delete" behaviour could be dependent on a flag, perhaps?
            // delete this;
        }
    };

    inline void basewatcher_set_active(base_watcher &watcher, bool active)
    {
        watcher.active = active;
    }

    inline bool basewatcher_get_deleteme(const base_watcher &watcher)
    {
        return watcher.deleteme;
    }

    inline bool basewatcher_get_emulatefd(const base_watcher &watcher)
    {
        return watcher.emulatefd;
    }

    // Base signal event - not part of public API
    template <typename T_Mutex, typename Traits>
    class base_signal_watcher : public base_watcher
    {
        friend class event_dispatch<T_Mutex, Traits>;
        template <typename, template <typename> class, typename> friend class dasynq::event_loop;

        protected:
        typename Traits::sigdata_t siginfo;
        base_signal_watcher() : base_watcher(watch_type_t::SIGNAL) { }

        public:
        using siginfo_t = typename Traits::sigdata_t;
        typedef siginfo_t &siginfo_p;
    };

    template <typename T_Mutex>
    class base_fd_watcher : public base_watcher
    {
        template <typename, typename Traits> friend class event_dispatch;
        template <typename, template <typename> class, typename> friend class dasynq::event_loop;

        protected:
        int watch_fd;

        // These flags are protected by the loop's internal lock:
        int watch_flags;  // events being watched
        int event_flags;  // events pending (queued)

        // watch_flags: for a regular fd_watcher, this specifies the events that the watcher
        //              is watching (or was watching if disabled). For a bidi_fd_watcher, specifies
        //              the events that the watcher is currently watching (i.e. specifies which
        //              halves of the Bidi watcher are enabled).

        base_fd_watcher() noexcept : base_watcher(watch_type_t::FD) { }
    };

    template <typename T_Mutex>
    class base_bidi_fd_watcher : public base_fd_watcher<T_Mutex>
    {
        template <typename, typename Traits> friend class event_dispatch;
        template <typename, template <typename> class, typename> friend class dasynq::event_loop;

        protected:

        // The main instance is the "input" watcher only; we keep a secondary watcher with a secondary set
        // of flags for the "output" watcher. Note that some of the flags in the secondary watcher aren't
        // used; it exists mainly so that it can be queued independently of the primary watcher.
        base_watcher out_watcher {watch_type_t::SECONDARYFD};

        int read_removed : 1; // read watch removed?
        int write_removed : 1; // write watch removed?
    };

    template <typename T_Mutex>
    class base_child_watcher : public base_watcher
    {
        template <typename, typename Traits> friend class event_dispatch;
        template <typename, template <typename> class, typename> friend class dasynq::event_loop;

        protected:
        pid_watch_handle_t watch_handle;
        pid_t watch_pid;
        int child_status;

        base_child_watcher() : base_watcher(watch_type_t::CHILD) { }
    };


    template <typename T_Mutex>
    class base_timer_watcher : public base_watcher
    {
        template <typename, typename Traits> friend class event_dispatch;
        template <typename, template <typename> class, typename> friend class dasynq::event_loop;

        protected:
        timer_handle_t timer_handle;
        int intervals;
        clock_type clock;

        base_timer_watcher() : base_watcher(watch_type_t::TIMER)
        {
            init_timer_handle(timer_handle);
        }
    };
} // dprivate
} // dasynq

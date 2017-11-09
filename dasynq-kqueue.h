#include <system_error>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <vector>

#ifdef __OpenBSD__
#include <sys/signal.h> // for __thrsigdivert aka sigtimedwait
#include <sys/syscall.h>
extern "C" { 
    int __thrsigdivert(sigset_t set, siginfo_t *info, const struct timespec * timeout);
}
#endif

#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <unistd.h>
#include <signal.h>

#include "dasynq-config.h"

// "kqueue"-based event loop mechanism.
//
// kqueue is available on BSDs and Mac OS X, though there are subtle differences from OS to OS.
//
// kqueue supports watching file descriptors (input and output as separate watches only),
// signals, child processes, and timers. Unfortunately support for the latter two is imperfect;
// it is not possible to reserve process watches in advance; timers can only be active, count
// down immediately when created, and cannot be reset to another time. For timers especially
// the problems are significant: we can't allocate timers in advance, and we can't even feasibly
// manage our own timer queue via a single kqueue-backed timer. Therefore, an alternate timer
// mechanism must be used together with kqueue.

namespace dasynq {

template <class Base> class kqueue_loop;

class kqueue_traits
{
    template <class Base> friend class kqueue_loop;

    public:

    class sigdata_t
    {
        template <class Base> friend class kqueue_loop;
        
        siginfo_t info;
        
        public:
        int get_signo() { return info.si_signo; }
        int get_sicode() { return info.si_code; }
        void * get_ssiaddr() { return info.si_addr; }
        
        void set_signo(int signo) { info.si_signo = signo; }
    };    

    class fd_r;

    // File descriptor optional storage. If the mechanism can return the file descriptor, this
    // class will be empty, otherwise it can hold a file descriptor.
    class fd_s {
        DASYNQ_EMPTY_BODY
    };

    // File descriptor reference (passed to event callback). If the mechanism can return the
    // file descriptor, this class holds the file descriptor. Otherwise, the file descriptor
    // must be stored in an fd_s instance.
    class fd_r {
        int fd;
        public:
        int getFd(fd_s ss)
        {
            return fd;
        }
        fd_r(int nfd) : fd(nfd)
        {
        }
    };
    
    const static bool has_bidi_fd_watch = false;
    const static bool has_separate_rw_fd_watches = true;
    const static bool supports_childwatch_reservation = true;
};

#if defined(__OpenBSD__) && _POSIX_REALTIME_SIGNALS <= 0
// OpenBSD has no sigtimedwait (or sigwaitinfo) but does have "__thrsigdivert", which is
// essentially an incomplete version of the same thing. Discussion with OpenBSD developer
// Ted Unangst suggested that the siginfo_t structure returned might not always have all fields
// set correctly. Furthermore there is a bug (at least in 5.9)  such that specifying a zero
// timeout (or indeed any timeout less than a tick) results in NO timeout. We get around this by
// instead specifying an *invalid* timeout, which won't error out if a signal is pending.
static inline int sigtimedwait(const sigset_t *ssp, siginfo_t *info, struct timespec *timeout)
{
    // We know that we're only called with a timeout of 0 (which doesn't work properly) and
    // that we safely overwrite the timeout. So, we set tv_nsec to an invalid value, which
    // will cause EINVAL to be returned, but will still pick up any pending signals *first*.
    timeout->tv_nsec = 1000000001;
    return __thrsigdivert(*ssp, info, timeout);
}
#endif

#if defined(__OpenBSD__) || _POSIX_REALTIME_SIGNALS > 0
static inline void prepare_signal(int signo) { }
static inline void unprep_signal(int signo) { }

inline bool get_siginfo(int signo, siginfo_t *siginfo)
{
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 0;

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, signo);
    return (sigtimedwait(&mask, siginfo, &timeout) != -1);
}
#else

// If we have no sigtimedwait implementation, we have to retrieve signal data by establishing a
// signal handler.

// We need to declare and define a non-static data variable, "siginfo_p", in this header, without
// violating the "one definition rule". The only way to do that is via a template, even though we
// don't otherwise need a template here:
template <typename T = decltype(nullptr)> class sig_capture_templ
{
    public:
    static siginfo_t * siginfo_p;

    static void signalHandler(int signo, siginfo_t *siginfo, void *v)
    {
        *siginfo_p = *siginfo;
    }
};
template <typename T> siginfo_t * sig_capture_templ<T>::siginfo_p = nullptr;

using sig_capture = sig_capture_templ<>;

inline void prepare_signal(int signo)
{
    struct sigaction the_action;
    the_action.sa_sigaction = sig_capture::signalHandler;
    the_action.sa_flags = SA_SIGINFO;
    sigfillset(&the_action.sa_mask);

    sigaction(signo, &the_action, nullptr);
}

inline void unprep_signal(int signo)
{
    signal(signo, SIG_DFL);
}

inline bool get_siginfo(int signo, siginfo_t *siginfo)
{
    sig_capture::siginfo_p = siginfo;

    sigset_t mask;
    sigfillset(&mask);
    sigdelset(&mask, signo);
    sigsuspend(&mask);
    return true;
}

#endif

template <class Base> class kqueue_loop : public Base
{
    int kqfd; // kqueue fd
    sigset_t sigmask; // enabled signal watch mask

    // Map of signal number to user data pointer. If kqueue had been better thought-through,
    // we shouldn't need this. Although we can associate user data with an EVFILT_SIGNAL kqueue
    // filter, the problem is that the kqueue signal report *coexists* with the regular signal
    // delivery mechanism without having any connection to it. Whereas regular signals can be
    // queued (especially "realtime" signals, via sigqueue()), kqueue just maintains a counter
    // of delivery attempts and clears this when we read the event. What this means is that
    // kqueue won't necessarily tell us if signals are pending, in the case that:
    //  1) it already reported the attempted signal delivery and
    //  2) more than one of the same signal was pending at that time and
    //  3) no more deliveries of the same signal have been attempted in the meantime.
    // Of course, if kqueue doesn't report the signal, then it doesn't give us the data associated
    // with the event, so we need to maintain that separately too:
    std::unordered_map<int, void *> sigdata_map;
    
    // Base contains:
    //   lock - a lock that can be used to protect internal structure.
    //          receive*() methods will be called with lock held.
    //   receive_signal(sigdata_t &, user *) noexcept
    //   receive_fd_event(fd_r, user *, int flags) noexcept
    
    using sigdata_t = kqueue_traits::sigdata_t;
    using fd_r = typename kqueue_traits::fd_r;
    
    void process_events(struct kevent *events, int r)
    {
        std::lock_guard<decltype(Base::lock)> guard(Base::lock);
        
        for (int i = 0; i < r; i++) {
            if (events[i].filter == EVFILT_SIGNAL) {
                sigdata_t siginfo;
                if (get_siginfo(events[i].ident, &siginfo.info)
                        && Base::receive_signal(*this, siginfo, (void *)events[i].udata)) {
                    sigdelset(&sigmask, events[i].ident);
                    events[i].flags = EV_DISABLE;
                }
                else {
                    events[i].flags = EV_ENABLE;
                }
            }
            else if (events[i].filter == EVFILT_READ || events[i].filter == EVFILT_WRITE) {
                int flags = events[i].filter == EVFILT_READ ? IN_EVENTS : OUT_EVENTS;
                Base::receive_fd_event(*this, fd_r(events[i].ident), events[i].udata, flags);
                events[i].flags = EV_DISABLE | EV_CLEAR;
                // we use EV_CLEAR to clear the EOF status of fifos/pipes (and wait for
                // another connection).
            }
            else {
                events[i].flags = EV_DISABLE;
            }
        }
        
        // Now we disable all received events, to simulate EV_DISPATCH:
        kevent(kqfd, events, r, nullptr, 0, nullptr);
    }
    
    public:
    
    /**
     * kqueue_loop constructor.
     *
     * Throws std::system_error or std::bad_alloc if the event loop cannot be initialised.
     */
    kqueue_loop()
    {
        kqfd = kqueue();
        if (kqfd == -1) {
            throw std::system_error(errno, std::system_category());
        }
        sigemptyset(&sigmask);
        Base::init(this);
    }
    
    ~kqueue_loop()
    {
        close(kqfd);
    }
    
    void set_filter_enabled(short filterType, uintptr_t ident, void *udata, bool enable)
    {
        // Note, on OpenBSD enabling or disabling filter will not alter the filter parameters (udata etc);
        // on OS X however, it will. Therefore we set udata here (to the same value as it was originally
        // set) in order to work correctly on both kernels.
        struct kevent kev;
        EV_SET(&kev, ident, filterType, enable ? EV_ENABLE : EV_DISABLE, 0, 0, udata);
        kevent(kqfd, &kev, 1, nullptr, 0, nullptr);
    }
    
    void remove_filter(short filterType, uintptr_t ident)
    {
        struct kevent kev;
        EV_SET(&kev, ident, filterType, EV_DELETE, 0, 0, 0);
        kevent(kqfd, &kev, 1, nullptr, 0, nullptr);    
    }
    
    //        fd:  file descriptor to watch
    //  userdata:  data to associate with descriptor
    //     flags:  IN_EVENTS | OUT_EVENTS | ONE_SHOT
    //             (only one of IN_EVENTS/OUT_EVENTS can be specified)
    // soft_fail:  true if unsupported file descriptors should fail by returning false instead
    //             of throwing an exception
    // returns: true on success; false if file descriptor type isn't supported and soft_fail == true
    // throws:  std::system_error or std::bad_alloc on failure
    bool add_fd_watch(int fd, void *userdata, int flags, bool enabled = true, bool emulate = false)
    {
        short filter = (flags & IN_EVENTS) ? EVFILT_READ : EVFILT_WRITE;

        struct kevent kev;
        EV_SET(&kev, fd, filter, EV_ADD | (enabled ? 0 : EV_DISABLE), 0, 0, userdata);
        if (kevent(kqfd, &kev, 1, nullptr, 0, nullptr) == -1) {
            // Note that kqueue supports EVFILT_READ on regular file fd's, but not EVFIL_WRITE.
            if (filter == EVFILT_WRITE && errno == EINVAL) {
                return false; // emulate
            }
            throw new std::system_error(errno, std::system_category());
        }
        return true;
    }

    // returns: 0 on success
    //          IN_EVENTS  if in watch requires emulation
    //          OUT_EVENTS if out watch requires emulation
    int add_bidi_fd_watch(int fd, void *userdata, int flags, bool emulate = false)
    {
#ifdef EV_RECEIPT
        struct kevent kev[2];
        struct kevent kev_r[2];
        short rflags = EV_ADD | ((flags & IN_EVENTS) ? 0 : EV_DISABLE) | EV_RECEIPT;
        short wflags = EV_ADD | ((flags & OUT_EVENTS) ? 0 : EV_DISABLE) | EV_RECEIPT;
        EV_SET(&kev[0], fd, EVFILT_READ, rflags, 0, 0, userdata);
        EV_SET(&kev[1], fd, EVFILT_WRITE, wflags, 0, 0, userdata);

        int r = kevent(kqfd, kev, 2, kev_r, 2, nullptr);

        if (r == -1) {
            throw new std::system_error(errno, std::system_category());
        }

        // Some possibilities:
        // - both ends failed. We'll throw an error rather than allowing emulation.
        // - read watch failed, write succeeded : should not happen.
        // - read watch added, write failed: if emulate == true, succeed;
        //                                   if emulate == false, remove read and fail.

        if (kev_r[0].data != 0) {
            // read failed
            throw new std::system_error(kev_r[0].data, std::system_category());
        }

        if (kev_r[1].data != 0) {
            if (emulate) {
                return OUT_EVENTS;
            }
            // remove read watch
            EV_SET(&kev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, userdata);
            kevent(kqfd, kev, 1, nullptr, 0, nullptr);
            // throw exception
            throw new std::system_error(kev_r[1].data, std::system_category());
        }

        return 0;
#else
        // OpenBSD doesn't have EV_RECEIPT: install the watches one at a time
        struct kevent kev[1];

        short rflags = EV_ADD | ((flags & IN_EVENTS) ? 0 : EV_DISABLE);
        short wflags = EV_ADD | ((flags & OUT_EVENTS) ? 0 : EV_DISABLE);
        EV_SET(&kev[0], fd, EVFILT_READ, rflags, 0, 0, userdata);

        int r = kevent(kqfd, kev, 1, nullptr, 0, nullptr);

        if (r == -1) {
            throw new std::system_error(errno, std::system_category());
        }

        EV_SET(&kev[0], fd, EVFILT_WRITE, wflags, 0, 0, userdata);

        r = kevent(kqfd, kev, 1, nullptr, 0, nullptr);

        if (r == -1) {
            if (emulate) {
                return OUT_EVENTS;
            }
            // remove read watch
            EV_SET(&kev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, userdata);
            kevent(kqfd, kev, 1, nullptr, 0, nullptr);
            // throw exception
            throw new std::system_error(errno, std::system_category());
        }

        return 0;
#endif
    }
    
    // flags specifies which watch to remove; ignored if the loop doesn't support
    // separate read/write watches.
    void remove_fd_watch(int fd, int flags)
    {        
        remove_filter((flags & IN_EVENTS) ? EVFILT_READ : EVFILT_WRITE, fd);
    }
    
    void remove_fd_watch_nolock(int fd, int flags)
    {
        remove_fd_watch(fd, flags);
    }

    void remove_bidi_fd_watch(int fd) noexcept
    {
        struct kevent kev[2];
        EV_SET(&kev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        EV_SET(&kev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
        
        kevent(kqfd, kev, 2, nullptr, 0, nullptr);
    }
    
    void enable_fd_watch(int fd, void *userdata, int flags)
    {
        set_filter_enabled((flags & IN_EVENTS) ? EVFILT_READ : EVFILT_WRITE, fd, userdata, true);
    }
    
    void enable_fd_watch_nolock(int fd, void *userdata, int flags)
    {
        enable_fd_watch(fd, userdata, flags);
    }
    
    void disable_fd_watch(int fd, int flags)
    {
        set_filter_enabled((flags & IN_EVENTS) ? EVFILT_READ : EVFILT_WRITE, fd, nullptr, false);
    }
    
    void disable_fd_watch_nolock(int fd, int flags)
    {
        disable_fd_watch(fd, flags);
    }
    
    // Note signal should be masked before call.
    void add_signal_watch(int signo, void *userdata)
    {
        std::lock_guard<decltype(Base::lock)> guard(Base::lock);
        
        sigdata_map[signo] = userdata;
        sigaddset(&sigmask, signo);
        
        prepare_signal(signo);

        struct kevent evt;
        EV_SET(&evt, signo, EVFILT_SIGNAL, EV_ADD, 0, 0, userdata);
        // TODO use EV_DISPATCH if available (not on OpenBSD/OS X)
        
        if (kevent(kqfd, &evt, 1, nullptr, 0, nullptr) == -1) {
            throw new std::system_error(errno, std::system_category());
        }
    }
    
    // Note, called with lock held:
    void rearm_signal_watch_nolock(int signo) noexcept
    {
        sigaddset(&sigmask, signo);
        
        struct kevent evt;
        EV_SET(&evt, signo, EVFILT_SIGNAL, EV_ENABLE, 0, 0, 0);
        // TODO use EV_DISPATCH if available (not on OpenBSD)
        
        kevent(kqfd, &evt, 1, nullptr, 0, nullptr);
    }
    
    void remove_signal_watch_nolock(int signo) noexcept
    {
        unprep_signal(signo);
        sigdelset(&sigmask, signo);
        
        struct kevent evt;
        EV_SET(&evt, signo, EVFILT_SIGNAL, EV_DELETE, 0, 0, 0);
        
        kevent(kqfd, &evt, 1, nullptr, 0, nullptr);
    }

    void remove_signal_watch(int signo) noexcept
    {
        std::lock_guard<decltype(Base::lock)> guard(Base::lock);
        remove_signal_watch_nolock(signo);
    }
    
    private:

    // We actually need to check pending signals before polling the kqueue, since kqueue can
    // count signals as they are delivered but the count is cleared when we poll the kqueue,
    // meaning that signals might still be pending if they were queued multiple times at the
    // last poll (since we report only one signal delivery at a time and the watch is
    // automatically disabled each time).
    //
    // The check is not necessary on systems that don't queue signals.
    void pull_signals()
    {
#if _POSIX_REALTIME_SIGNALS > 0
        // TODO we should only poll for signals that *have* been reported
        // as being raised more than once prior via kevent, rather than all
        // signals that have been registered - in many cases that may allow
        // us to skip the sigtimedwait call altogether.
        {
            std::lock_guard<decltype(Base::lock)> guard(Base::lock);

            struct timespec timeout;
            timeout.tv_sec = 0;
            timeout.tv_nsec = 0;
            sigdata_t siginfo;
            int rsigno = sigtimedwait(&sigmask, &siginfo.info, &timeout);
            while (rsigno > 0) {
                if (Base::receive_signal(*this, siginfo, sigdata_map[rsigno])) {
                    sigdelset(&sigmask, rsigno);
                    // TODO accumulate and disable multiple filters with a single kevents call
                    //      rather than disabling each individually
                    set_filter_enabled(EVFILT_SIGNAL, rsigno, sigdata_map[rsigno], false);
                }
                rsigno = sigtimedwait(&sigmask, &siginfo.info, &timeout);
            }
        }
#endif
    }

    public:

    // If events are pending, process an unspecified number of them.
    // If no events are pending, wait until one event is received and
    // process this event (and possibly any other events received
    // simultaneously).
    // If processing an event removes a watch, there is a possibility
    // that the watched event will still be reported (if it has
    // occurred) before pull_events() returns.
    //
    //  do_wait - if false, returns immediately if no events are
    //            pending.
    void pull_events(bool do_wait)
    {
        pull_signals();
        
        struct kevent events[16];
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 0;
        int r = kevent(kqfd, nullptr, 0, events, 16, do_wait ? nullptr : &ts);
        if (r == -1 || r == 0) {
            // signal or no events
            return;
        }
        
        do {
            process_events(events, r);
            r = kevent(kqfd, nullptr, 0, events, 16, &ts);
        } while (r > 0);
    }
};

} // end namespace

#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <unistd.h>
#include <signal.h>

#include "dasynq-config.h"

// "pselect"-based event loop mechanism.
//

namespace dasynq {

template <class Base> class select_events;

class select_traits
{
    public:

    class sigdata_t
    {
        template <class Base> friend class select_events;

        siginfo_t info;

        public:
        // mandatory:
        int get_signo() { return info.si_signo; }
        int get_sicode() { return info.si_code; }
        pid_t get_sipid() { return info.si_pid; }
        uid_t get_siuid() { return info.si_uid; }
        void * get_siaddr() { return info.si_addr; }
        int get_sistatus() { return info.si_status; }
        int get_sival_int() { return info.si_value.sival_int; }
        void * get_sival_ptr() { return info.si_value.sival_ptr; }

        // XSI
        int get_sierrno() { return info.si_errno; }

        // XSR (streams) OB (obselete)
#if !defined(__OpenBSD__)
        // Note: OpenBSD doesn't have this; most other systems do. Technically it is part of the STREAMS
        // interface.
        int get_siband() { return info.si_band; }
#endif

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
};

#if _POSIX_REALTIME_SIGNALS > 0
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

template <class Base> class select_events : public Base
{
    // These are protected by the "only poll from a single thread" guarantee - but not by any mutex.
    fd_set read_set;
    fd_set write_set;
    //fd_set error_set;  // logical OR of both the above
    int max_fd = 0; // highest fd in any of the sets

    // userdata pointers in read and write respectively, for each fd:
    std::vector<void *> rd_udata;
    std::vector<void *> wr_udata;

    // Base contains:
    //   lock - a lock that can be used to protect internal structure.
    //          receive*() methods will be called with lock held.
    //   receive_signal(sigdata_t &, user *) noexcept
    //   receive_fd_event(fd_r, user *, int flags) noexcept

    using sigdata_t = select_traits::sigdata_t;
    using fd_r = typename select_traits::fd_r;

    void process_events(fd_set *read_set_p, fd_set *write_set_p, fd_set *error_set_p)
    {
        std::lock_guard<decltype(Base::lock)> guard(Base::lock);

        // Note: if error is set, report read and write.

        // DAV need a way for non-oneshot fds

        for (int i = 0; i <= max_fd; i++) {
            if (FD_ISSET(i, read_set_p) || FD_ISSET(i, error_set_p)) {
                // report read
                Base::receive_fd_event(*this, fd_r(i), rd_udata[i], IN_EVENTS);
                FD_CLR(i, &read_set);
            }
        }

        for (int i = 0; i <= max_fd; i++) {
            if (FD_ISSET(i, write_set_p) || FD_ISSET(i, error_set_p)) {
                // report write
                Base::receive_fd_event(*this, fd_r(i), wr_udata[i], OUT_EVENTS);
                FD_CLR(i, &write_set);
            }
        }
    }

    // Pull a signal from pending, and report it, until it is no longer pending or the watch
    // should be disabled. Call with lock held.
    // Returns:  true if watcher should be enabled, false if disabled.
    bool pull_signal(int signo, void *userdata)
    {
        bool enable_filt = true;
        sigdata_t siginfo;

#if _POSIX_REALTIME_SIGNALS > 0
        struct timespec timeout = {0, 0};
        sigset_t sigw_mask;
        sigemptyset(&sigw_mask);
        sigaddset(&sigw_mask, signo);
        int rsigno = sigtimedwait(&sigw_mask, &siginfo.info, &timeout);
        while (rsigno > 0) {
            if (Base::receive_signal(*this, siginfo, userdata)) {
                enable_filt = false;
                break;
            }
            rsigno = sigtimedwait(&sigw_mask, &siginfo.info, &timeout);
        }
#else
        // we have no sigtimedwait.
        sigset_t pending_sigs;
        sigpending(&pending_sigs);
        while (sigismember(&pending_sigs, signo)) {
            get_siginfo(signo, &siginfo.info);
            if (Base::receive_signal(*this, siginfo, userdata)) {
                enable_filt = false;
                break;
            }
            sigpending(&pending_sigs);
        }
#endif
        return enable_filt;
    }

    public:

    /**
     * select_events constructor.
     *
     * Throws std::system_error or std::bad_alloc if the event loop cannot be initialised.
     */
    select_events()
    {
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        Base::init(this);
    }

    ~select_events()
    {
    }

    //        fd:  file descriptor to watch
    //  userdata:  data to associate with descriptor
    //     flags:  IN_EVENTS | OUT_EVENTS | ONE_SHOT
    //             (only one of IN_EVENTS/OUT_EVENTS can be specified)
    // soft_fail:  true if unsupported file descriptors should fail by returning false instead
    //             of throwing an exception
    // returns: true on success; false if file descriptor type isn't supported and emulate == true
    // throws:  std::system_error or std::bad_alloc on failure
    bool add_fd_watch(int fd, void *userdata, int flags, bool enabled = true, bool soft_fail = false)
    {
        /*
        if (! soft_fail) {
            // Check now if it's a regular file
            struct stat statbuf;
            if (fstat(fd, &statbuf) == -1) {
                throw new std::system_error(errno, std::system_category());
            }
            if ((statbuf.st_mode & S_IFMT) == S_IFREG) {
                // Regular file: emulation required
                throw new std::system_error(errno, std::system_category());
            }
        }
        */

        if (flags & IN_EVENTS) {
            FD_SET(fd, &read_set);
            if (fd >= rd_udata.size()) {
                rd_udata.resize(fd + 1);
            }
            rd_udata[fd] = userdata;
        }
        else {
            FD_SET(fd, &write_set);
            if (fd >= wr_udata.size()) {
                wr_udata.resize(fd + 1);
            }
            wr_udata[fd] = userdata;
        }

        max_fd = std::max(fd, max_fd);

        // TODO signal any other currently polling thread

        return true;
    }

    // returns: 0 on success
    //          IN_EVENTS  if in watch requires emulation
    //          OUT_EVENTS if out watch requires emulation
    int add_bidi_fd_watch(int fd, void *userdata, int flags, bool emulate = false)
    {
        if (flags & IN_EVENTS) {
            FD_SET(fd, &read_set);
            if (fd >= rd_udata.size()) {
                rd_udata.resize(fd + 1);
            }
            rd_udata[fd] = userdata;
        }
        if (flags & OUT_EVENTS) {
            FD_SET(fd, &write_set);
            if (fd >= wr_udata.size()) {
                wr_udata.resize(fd + 1);
            }
            wr_udata[fd] = userdata;
        }

        max_fd = std::max(fd, max_fd);

        // TODO signal any other currently polling thread
        return 0;
    }

    // flags specifies which watch to remove; ignored if the loop doesn't support
    // separate read/write watches.
    void remove_fd_watch(int fd, int flags)
    {
        if (flags & IN_EVENTS) {
            FD_CLR(fd, &read_set);
        }
        if (flags & OUT_EVENTS) {
            FD_CLR(fd, &write_set);
        }

        // TODO potentially reduce size of userdata vectors

        // TODO signal any other currently polling thread
    }

    void remove_fd_watch_nolock(int fd, int flags)
    {
        remove_fd_watch(fd, flags);
    }

    void remove_bidi_fd_watch(int fd) noexcept
    {
        FD_CLR(fd, &read_set);
        FD_CLR(fd, &write_set);
        
        // TODO potentially reduce size of userdata vectors

        // TODO signal any other currently polling thread
    }

    void enable_fd_watch(int fd, void *userdata, int flags)
    {
        if (flags & IN_EVENTS) {
            FD_SET(fd, &read_set);
        }
        else {
            FD_SET(fd, &write_set);
        }

        // TODO signal other polling thread
    }

    void enable_fd_watch_nolock(int fd, void *userdata, int flags)
    {
        enable_fd_watch(fd, userdata, flags);
    }

    void disable_fd_watch(int fd, int flags)
    {
        if (flags & IN_EVENTS) {
            FD_CLR(fd, &read_set);
        }
        else {
            FD_CLR(fd, &write_set);
        }

        // TODO signal other polling thread? maybe not - do it lazily
    }

    void disable_fd_watch_nolock(int fd, int flags)
    {
        disable_fd_watch(fd, flags);
    }

    // Note signal should be masked before call.
    void add_signal_watch(int signo, void *userdata)
    {
        std::lock_guard<decltype(Base::lock)> guard(Base::lock);
        add_signal_watch_nolock(signo, userdata);
    }

    // Note signal should be masked before call.
    void add_signal_watch_nolock(int signo, void *userdata)
    {
        /*
        prepare_signal(signo);

        // We need to register the filter with the kqueue early, to avoid a race where we miss
        // signals:
        struct kevent evt;
        EV_SET(&evt, signo, EVFILT_SIGNAL, EV_ADD | EV_DISABLE, 0, 0, userdata);
        if (kevent(kqfd, &evt, 1, nullptr, 0, nullptr) == -1) {
            throw new std::system_error(errno, std::system_category());
        }
        // TODO use EV_DISPATCH if available (not on OpenBSD/OS X)

        // The signal might be pending already but won't be reported by kqueue in that case. We can queue
        // it immediately (note that it might be pending multiple times, so we need to re-check once signal
        // processing finishes if it is re-armed).

        bool enable_filt = pull_signal(signo, userdata);

        if (enable_filt) {
            evt.flags = EV_ENABLE;
            if (kevent(kqfd, &evt, 1, nullptr, 0, nullptr) == -1) {
                throw new std::system_error(errno, std::system_category());
            }
        }
        */
    }

    // Note, called with lock held:
    void rearm_signal_watch_nolock(int signo, void *userdata) noexcept
    {
        /*
        if (pull_signal(signo, userdata)) {
            struct kevent evt;
            EV_SET(&evt, signo, EVFILT_SIGNAL, EV_ENABLE, 0, 0, userdata);
            // TODO use EV_DISPATCH if available (not on OpenBSD)

            kevent(kqfd, &evt, 1, nullptr, 0, nullptr);
        }
        */
    }

    void remove_signal_watch_nolock(int signo) noexcept
    {
        /*
        unprep_signal(signo);

        struct kevent evt;
        EV_SET(&evt, signo, EVFILT_SIGNAL, EV_DELETE, 0, 0, 0);

        kevent(kqfd, &evt, 1, nullptr, 0, nullptr);
        */
    }

    void remove_signal_watch(int signo) noexcept
    {
        std::lock_guard<decltype(Base::lock)> guard(Base::lock);
        remove_signal_watch_nolock(signo);
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
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 0;

        fd_set read_set_c;
        fd_set write_set_c;
        fd_set err_set;

        FD_COPY(&read_set, &read_set_c);
        FD_COPY(&write_set, &write_set_c);
        FD_ZERO(&err_set);

        sigset_t sigmask;
        // TODO fill sigmask properly
        sigprocmask(SIG_UNBLOCK, nullptr, &sigmask);

        // TODO pre-check signals?

        int r = pselect(max_fd + 1, &read_set_c, &write_set_c, &err_set, do_wait ? nullptr : &ts, &sigmask);
        if (r == -1 || r == 0) {
            // signal or no events
            // TODO event with EINTR we may want to poll once more
            return;
        }

        do {
            process_events(&read_set_c, &write_set_c, &err_set);

            FD_COPY(&read_set, &read_set_c);
            FD_COPY(&write_set, &write_set_c);
            FD_ZERO(&err_set);

            r = pselect(max_fd + 1, &read_set_c, &write_set_c, &err_set, &ts, &sigmask);
        } while (r > 0);
    }
};

} // end namespace

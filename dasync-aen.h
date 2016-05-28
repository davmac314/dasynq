#include <map>
#include <system_error>

#include <sys/epoll.h>
#include <sys/signalfd.h>

#include <unistd.h>
#include <signal.h>

namespace dasync {

// Event type bits
constexpr unsigned int in_events = 1;
constexpr unsigned int out_events = 2;

constexpr unsigned int one_shot = 4;


template <class Base> class EpollLoop;

class EpollTraits
{
    template <class Base> friend class EpollLoop;

    public:

    class SigInfo
    {
        template <class Base> friend class EpollLoop;
        
        struct signalfd_siginfo info;
        
        public:
        int get_signo() { return info.ssi_signo; }
        int get_sicode() { return info.ssi_code; }
        int get_siint() { return info.ssi_int; }
        int get_ssiptr() { return info.ssi_ptr; }
        int get_ssiaddr() { return info.ssi_addr; }
    };    

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


template <class Base> class EpollLoop : Base
{
    int epfd; // epoll fd
    int sigfd; // signalfd fd; -1 if not initialised
    sigset_t sigmask;

    std::map<int, void *> sigdataMap;

    // Base contains:
    //   receiveSignal(SigInfo &, user *)
    //   receiveFdEvent(FD_r, user *, int flags)
    
    using SigInfo = EpollTraits::SigInfo;
    using FD_r = typename Base::FD_r;
    
    void processEvents(epoll_event *events, int r)
    {
        Base::beginEventBatch();
        
        for (int i = 0; i < r; i++) {
            void * ptr = events[i].data.ptr;
            
            if (ptr == &sigfd) {
                // Signal
                SigInfo siginfo;
                read(sigfd, &siginfo.info, sizeof(siginfo.info));
                // TODO thread-safety for sigdataMap
                auto iter = sigdataMap.find(siginfo.get_signo());
                if (iter != sigdataMap.end()) {
                    void *userdata = (*iter).second;
                    Base::receiveSignal(siginfo, userdata);
                }
            }
            else {
                int flags = 0;
                (events[i].events & EPOLLIN) && (flags |= in_events);
                (events[i].events & EPOLLOUT) && (flags |= out_events);
                Base::receiveFdEvent(FD_r(), ptr, flags);
            }            
        }
        
        Base::endEventBatch();
    }
    
    public:
    
    /**
     * EpollLoop constructor.
     *
     * Throws std::system_error or std::bad_alloc if the event loop cannot be initialised.
     */
    EpollLoop() : sigfd(-1)
    {
        // sigemptyset(&smask);
        epfd = epoll_create1(EPOLL_CLOEXEC);
        if (epfd == -1) {
            throw std::system_error(errno, std::system_category());
        }
        sigemptyset(&sigmask);
    }
    
    ~EpollLoop()
    {
        close(epfd);
        if (sigfd != -1) {
            close(sigfd);
        }
    }
    
    
    // flags:  in_events | out_events
    void addFdWatch(int fd, void *userdata, int flags)
    {
        struct epoll_event epevent;
        // epevent.data.fd = fd;
        epevent.data.ptr = userdata;
        epevent.events = 0;
        
        if (flags & one_shot) {
            epevent.events = EPOLLONESHOT;
        }
        if (flags & in_events) {
            epevent.events |= EPOLLIN;
        }
        if (flags & out_events) {
            epevent.events |= EPOLLOUT;
        }

        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &epevent) == -1) {
            throw new std::system_error(errno, std::system_category());        
        }
    }
    
    void removeFdWatch(int fd)
    {
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    }
    
    void enableFdWatch(int fd, void *userdata, int flags)
    {
        struct epoll_event epevent;
        // epevent.data.fd = fd;
        epevent.data.ptr = userdata;
        epevent.events = 0;
        
        if (flags & one_shot) {
            epevent.events = EPOLLONESHOT;
        }
        if (flags & in_events) {
            epevent.events |= EPOLLIN;
        }
        if (flags & out_events) {
            epevent.events |= EPOLLOUT;
        }

        if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &epevent) == -1) {
            throw new std::system_error(errno, std::system_category());        
        }
    }
    
    void disableFdWatch(int fd)
    {
        struct epoll_event epevent;
        // epevent.data.fd = fd;
        epevent.data.ptr = nullptr;
        epevent.events = 0;
        
        // Epoll documentation says that hangup will still be reported, need to check
        // whether this is really the case. Suspect it is really only the case if
        // EPOLLIN is set.
        if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &epevent) == -1) {
            throw new std::system_error(errno, std::system_category());        
        }
    }
    
    // Note signal should be masked before call.
    void addSignalWatch(int signo, void *userdata)
    {
        // TODO:
        // Not thread-safe!
        sigdataMap[signo] = userdata;

        // Modify the signal fd to watch the new signal
        bool was_no_sigfd = (sigfd == -1);
        sigaddset(&sigmask, signo);
        sigfd = signalfd(sigfd, &sigmask, SFD_NONBLOCK | SFD_CLOEXEC);
        if (sigfd == -1) {
            throw new std::system_error(errno, std::system_category());
        }
        
        if (was_no_sigfd) {
            // Add the signalfd to the epoll set.
            struct epoll_event epevent;
            epevent.data.ptr = &sigfd;
            epevent.events = EPOLLIN;
            // No need for EPOLLONESHOT - we can pull the signals out
            // as we see them.
            if (epoll_ctl(epfd, EPOLL_CTL_ADD, sigfd, &epevent) == -1) {
                throw new std::system_error(errno, std::system_category());        
            }
            
        }
    }
    
    void removeSignalWatch(int signo)
    {
        // Not thread-safe!
        sigdelset(&sigmask, signo);
        signalfd(sigfd, &sigmask, 0);
    }
    
    // If events are pending, process an unspecified number of them.
    // If no events are pending, wait until one event is received and
    // process this event (and possibly any other events received
    // simultaneously).
    // If processing an event removes a watch, there is a possibility
    // that the watched event will still be reported (if it has
    // occurred) before pullEvents() returns.
    //
    //  do_wait - if false, returns immediately if no events are
    //            pending.
    void pullEvents(bool do_wait)
    {
        epoll_event events[16];
        int r = epoll_wait(epfd, events, 16, do_wait ? -1 : 0);
        if (r == -1 || r == 0) {
            // signal or no events
            return;
        }
    
        processEvents(events, r);
    }

    // If events are pending, process one of them.
    // If no events are pending, wait until one event is received and
    // process this event.
    //
    //  do_wait - if false, returns immediately if no events are
    //            pending.    
    void pullOneEvent(bool do_wait)
    {
        epoll_event events[1];
        int r = epoll_wait(epfd, events, 1, do_wait ? -1 : 0);
        if (r == -1 || r == 0) {
            // signal or no events
            return;
        }
    
        processEvents(events, r);    
    }
};

template <class Base> class ChildProcEvents : Base
{
    public:
    using SigInfo = typename Base::SigInfo;
    
    void receiveSignal(SigInfo &siginfo, void *userdata)
    {
        if (siginfo.get_signo() == SIGCHLD) {
            // TODO
        
        }
        else {
            Base::receiveSignal(siginfo, userdata);
        }
    }
};

} // end namespace

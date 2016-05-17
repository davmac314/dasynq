#ifndef DASYNC_H_INCLUDED
#define DASYNC_H_INCLUDED

#include "dasync-aen.h"

#include <csignal>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <vector>
#include <atomic>
#include <cstdint>
#include <map>

#include <system_error>
#include <cerrno>

#include "dmutex.h"



// TODO put all private API in nested namespace / class private members

// TODO consider using atomic variables instead of explicit locking where appropriate

// Allow optimisation of empty classes by including this in the body:
// May be included as the last entry for a class which is only
// _potentially_ empty.
#ifdef __GNUC__
#ifdef __clang__
#define EMPTY_BODY private: xxasdfadsf char empty_fill[0] ;
#else
#define EMPTY_BODY private: char empty_fill[0];
#endif
#else
#define EMPTY_BODY
#endif

namespace dasync {


/**
 * Values for rearm/disarm return from event handlers
 */
enum class Rearm
{
    /** Re-arm the event watcher so that it receives further events */
    REARM,
    /** Disarm the event handler so that it receives no further events, until it is re-armed explicitly */
    DISARM
};


// Forward declarations:
template <typename T_Mutex> class EventLoop;
template <typename T_Mutex> class PosixFdWatcher;
template <typename T_Mutex> class PosixSignalWatcher;


// Information about a received signal.
// This is essentially a wrapper for the POSIX siginfo_t; its existence allows for mechanisms that receive
// equivalent signal information in a different format (eg signalfd on Linux).
using SigInfo = EpollTraits::SigInfo;



// (non-public API)
// Represents a queued event notification
class BaseWatcher
{
    template <typename T_Mutex, typename Traits> friend class EventDispatch;
    //friend class BaseSignalWatcher;
    
    int active : 1;
    int deleteme : 1;
    
    BaseWatcher * next;
    
    virtual void dispatch() { }
    
    public:
    BaseWatcher() : active(0), deleteme(0), next(nullptr) { }
};


// (Non-template) Base signal event - not part of public API
class BaseSignalWatcher : public BaseWatcher
{
    template <typename T_Mutex, typename Traits> friend class EventDispatch;

    SigInfo siginfo;    

    void dispatch() override
    {
        gotSignal(siginfo.get_signo(), siginfo);
    }

    public:
    virtual void gotSignal(int signo, SigInfo siginfo) = 0;
    // TODO should siginfo parameter be reference?
};

//extern BaseSignalWatcher * signalEvents[NSIG];

template <typename T_Mutex> class EventLoop;

template <typename T_Mutex, typename Traits> class EventDispatch : public Traits
{
    friend class EventLoop<T_Mutex>;

    T_Mutex queue_lock; // lock to protect active queue
    T_Mutex wait_lock;  // wait lock, used to prevent multiple threads from waiting
                        // on the event queue simultaneously.
    
    // queue data structure/pointer
    BaseWatcher * first;

    // TODO receiveXXX need to be bracketed by queue_lock lock and unlock.
    // AEN can notify just before and after delivering a set of events?
    // - Maybe need doMechanismLock and doMechanismUnlock? AEN mechanism may not be thread safe,
    //   gives it a way to lock a mutex if necessary
    // - beginEventBatch/endEventBatch? (to allow locking)
    
    protected:
    void receiveSignal(typename Traits::SigInfo & siginfo, void * userdata)
    {
        // Put callback in queue:
        PosixSignalWatcher<T_Mutex> * watcher = static_cast<PosixSignalWatcher<T_Mutex> *>(userdata);
        BaseSignalWatcher * bwatcher = (BaseSignalWatcher *) watcher;
        
        if (bwatcher->deleteme) {
            // TODO handle this.
        }
        
        bwatcher->siginfo = siginfo;
        // TODO for now, I'll set it active; but this prevents it being deleted until we can next
        // process events, so once we have a proper linked list or better structure should probably
        // remove this:
        bwatcher->active = true;
        
        // Put in queue:
        BaseWatcher * prev_first = first;
        first = bwatcher;
        bwatcher->next = prev_first;
    }
    
    void receiveFdEvent(typename Traits::FD_r fd_r, void * userdata, int flags)
    {
        // TODO put callback in queue
    }
    
    // TODO is this needed?:
    BaseWatcher * pullEvent()
    {
        if (first) {
            BaseWatcher * r = first;
            first = first->next;
            return r;
        }
        return nullptr;
    }
    
    // Process any pending events, return true if any events were processed or false if none
    // were queued
    bool processEvents() noexcept
    {
        queue_lock.lock();
        
        // So this pulls *all* currently pending events and processes them in the current thread.
        // That's probably good for throughput, but maybe the behavior should be configurable.
        
        BaseWatcher * pqueue = first;
        first = nullptr;
        bool active;
        
        for (BaseWatcher * q = pqueue; q != nullptr; q = q->next) {
            if (q->deleteme) {
                // TODO
                // try-lock the wait_lock; if successful, unlock and delete
                //                         lf unsuccesful, re-queue
            }
            q->active = true;
            active = true;
        }
        
        queue_lock.unlock();
        
        while (pqueue != nullptr) {
            pqueue->dispatch(); // TODO: handle re-arm etc
                     // TODO need to do this with lock
            queue_lock.lock();
            pqueue->active = false;
            queue_lock.unlock();
                // TODO check delete flag; if set while active,
                // we can remove immediately.
            pqueue = pqueue->next;    
        }
        
        return active;
    }
};



template <typename T_Mutex> class EventLoop
{
    //friend class PosixFdWatcher<T_Mutex>;
    friend class PosixSignalWatcher<T_Mutex>;
    //friend class SignalFdWatcher<T_Mutex>;
    
    T_Mutex mutex;
    EpollLoop<EventDispatch<T_Mutex, EpollTraits>> loop_mech;
    
    // TODO do we need this here?
    class Locker {
        T_Mutex &mutex;
        
        public:
        Locker(T_Mutex &m) : mutex(m) { mutex.lock(); }
        ~Locker() { mutex.unlock(); }
    };
    
    // TODO thread safety for event loop mechanism is tricky in subtle ways. The same
    // mutex used to prevent two threads from polling at the same time should not be
    // used to guard add/delete watch.
    
    // At least, the mutex should only be locked once there is no blocking needed
    // (i.e. once events have actually been received), but that probably pushes
    // locking responsibility down into the mechanism layer (or at least requires
    // a callback from the mechanism layer).
    
    void registerSignal(PosixSignalWatcher<T_Mutex> *callBack, int signo)
    {
        loop_mech.addSignalWatch(signo, callBack);
    }

    void registerFd(PosixFdWatcher<T_Mutex> *callback, int fd, int eventmask)
    {
        loop_mech.addFdWatch(fd, callback, eventmask);
    }
    
    public:
    void run() noexcept
    {
        EventDispatch<T_Mutex, EpollTraits> & ed = (EventDispatch<T_Mutex, EpollTraits> &) loop_mech;
        while (! ed.processEvents()) {
            // We only allow one thread to poll the mechanism at any time, since otherwise
            // removing event watchers is a nightmare beyond comprehension.
            ed.wait_lock.lock();
            loop_mech.pullEvents(true);
            ed.wait_lock.unlock();
            // TODO process event queue
        }
    }
};


// TODO move non-type-arg dependent members into a non-template base class
/*
template <typename T_Mutex>
class EventLoopOld
{
    friend class PosixFdWatcher<T_Mutex>;
    friend class PosixSignalWatcher<T_Mutex>;
    friend class SignalFdWatcher<T_Mutex>;

private:
    int epfd; // epoll fd
    int signalfd; // signalfd fd; -1 if not initialised
    
    T_Mutex mutex;
    SignalFdWatcher<T_Mutex> signalWatcher;
    
    struct FdInfo {
        PosixFdWatcher<T_Mutex> * handler;
        
        // Could possibly move the processing/armed/deactivate flags
        // here (from the PosixFdWatcher structure).
    };
    
    // protected by the mutex:
    sigset_t smask;  // signal mask to look at
    BaseSignalWatcher *signalCallbacks[NSIG];
    std::vector<FdInfo> fdinfo;

    // Register a signal handler for a given signal. Reception of the signal
    // should be blocked by the caller. Having more than one handler for a
    // particular signal, including across different event loops, is not
    // supported.
    void registerSignal(PosixSignalWatcher<T_Mutex> *callBack, int signo)
    {
        mutex.lock();
        
        sigaddset(&smask, signo);
        //signalCallbacks[signo] = static_cast<BaseSignalWatcher *>(callBack); // DAV XXX
        signalCallbacks[signo] = callBack;
        
        if (signalfd == -1) {
            signalfd = ::signalfd(signalfd, &smask, SFD_NONBLOCK | SFD_CLOEXEC);
            if (signalfd == -1) {
                mutex.unlock();
                throw std::system_error(errno, std::system_category());                
            }
            
            // Now add the signal fd to the epoll set:
            mutex.unlock();
            registerFd(&signalWatcher, signalfd, in_events);            
        }
        else {
            // Just have to add the requested signal to the mask
            signalfd = ::signalfd(signalfd, &smask, 0);
            mutex.unlock();
        }
    }
    
    // Event mask should be in_events / out_events
    // may throw allocation exception
    void registerFd(PosixFdWatcher<T_Mutex> *callback, int fd, int eventmask)
    {
        struct epoll_event epevent;
        epevent.data.fd = fd;
        // epevent.data.ptr = callback;
        
        callback->processing = 0;
        callback->deactivate = 0;
        
        mutex.lock();
        if (fdinfo.size() <= (unsigned)fd) fdinfo.resize(fd + 1);
        fdinfo[fd].handler = callback;
        mutex.unlock();
        
        epevent.events = EPOLLONESHOT;
        if (eventmask & in_events) {
            epevent.events |= EPOLLIN;
        }
        if (eventmask & out_events) {
            epevent.events |= EPOLLOUT;
        }

        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &epevent) == -1) {
            throw new std::system_error(errno, std::system_category());        
        }
    }
    
    void deregisterFd(PosixFdWatcher<T_Mutex> *callback, int fd)
    {
        mutex.lock();
        // assert ( fdinfo[fd].handler == callback )
        
        if (callback->processing) {
            callback->deactivate = 1;
            mutex.unlock();
            // deactivation call will now be made when processing is finished
        }
        else {
            fdinfo[fd].handler = nullptr;
            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
            mutex.unlock();
            callback->deactivated();
        }
    }
    
    // Deactivate watcher immediately, only callable from event callback
    void deactivateFd(PosixFdWatcher<T_Mutex> *callback, int fd)
    {
        mutex.lock();
        fdinfo[fd].handler = nullptr;
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        mutex.unlock();
    }
    
    // Process signals noticed on the signal fd.
    void processSignalFd()
    {
        struct timespec timeout;
        timeout.tv_sec = 0;
        timeout.tv_nsec = 0;
        
        siginfo_t siginfo;
        
        int signum = sigtimedwait(&smask, &siginfo, &timeout);
        while (signum != -1) {
            if (signalCallbacks[signum] != 0) {
                signalCallbacks[signum]->gotSignal(signum, SignalInfo(&siginfo));
            }
            // Check for more signals:
            signum = sigtimedwait(&smask, &siginfo, &timeout);
        }
    }
    
    void processEvents(epoll_event *events, int r)
    {
        mutex.lock();
            
        for (int i = 0; i < r; i++) {
            int fd = events[i].data.fd;
            //PosixFdWatcher<T_Mutex> * pfw = (PosixFdWatcher<T_Mutex> *) events[i].data.ptr;
            PosixFdWatcher<T_Mutex> * pfw = fdinfo[fd].handler;
                        
            if (pfw == nullptr) continue;

            if (pfw->processing) {
                // This shouldn't actually happen, if EPOLLONESHOT is working correctly.
                // But it seems better to be safe:
                continue;
            }            
            pfw->processing = 1;
            
            // We actually call the handler with the mutex unlocked, to avoid deadlock if
            // the handler calls back to the event loop (and also to avoid holding the
            // mutex for too long):
            mutex.unlock();
            
            int flags = 0;
            (events[i].events & EPOLLIN) && (flags |= in_events);
            (events[i].events & EPOLLOUT) && (flags |= out_events);
            Rearm r = pfw->fdReady(this, FD_r(), flags);
            
            // We are using one-shot events (EPOLLONESHOT), so must re-arm the descriptor if
            // it is supposed to be armed; but first we check if it's scheduled for
            // deactivation.
            
            mutex.lock();
            
            pfw->processing = 0;
            
            if (pfw->deactivate) {
                // deactivate is only set once the fd is already removed from epoll set
                // it should now be safe to deactivate:
                
                mutex.unlock();
                pfw->deactivated();
                mutex.lock();
            }
            else if (r == Rearm::REARM) {
                events[i].events = EPOLLONESHOT;
                if (pfw->in_events) events[i].events |= EPOLLIN;
                if (pfw->out_events) events[i].events |= EPOLLOUT;
                epoll_ctl(epfd, EPOLL_CTL_MOD, events[i].data.fd, &events[i]);
            }
        }
        
        mutex.unlock();
    }

public:
    typedef T_Mutex mutex_t;
    
    // EventLoop constructor.
    //
    // Throws std::system_error if the event loop cannot be initialised.
    EventLoop() : signalfd(-1)
    {
        sigemptyset(&smask);
        epfd = epoll_create1(EPOLL_CLOEXEC);
        if (epfd == -1) {
            throw std::system_error(errno, std::system_category());
        }
    }
    
    ~EventLoop()
    {
        close(epfd);
        if (signalfd != -1) {
            close(signalfd);
        }
    }
    
    // Run any events that are currently pending
    void checkEvents()
    {
        epoll_event events[10];
        int r = epoll_wait(epfd, events, 10, 0);
        if (r == -1 || r == 0) {
            // signal or no events
            return;
        }
    
        processEvents(events, r);
    }

    // Wait for any event and then process it before returning. May process
    // more than one event.
    void run()
    {
        epoll_event events[10];

        int r = epoll_wait(epfd, events, 10, -1);
        if (r == -1) {
            // presumably got a signal that we are not watching.
            return;
        }
        
        processEvents(events, r);
    }
};
*/


typedef EventLoop<NullMutex> NEventLoop;
typedef EventLoop<DMutex> TEventLoop;

// from dasync.cc:
TEventLoop & getSystemLoop();


/*
template <typename T_Mutex> Rearm SignalFdWatcher<T_Mutex>::fdReady(EventLoop<T_Mutex> *eloop, FD_r fdr, int flags)
{
    // processSignalFd(&eloop->smask);
    eloop->processSignalFd();
    return Rearm::REARM;
}
*/


// Posix signal event
template <typename T_Mutex>
class PosixSignalWatcher : private BaseSignalWatcher
{
    friend class EventLoop<T_Mutex>;
    
public:
    // If an attempt is made to register with more than one event loop at
    // a time, behaviour is undefined.
    inline void registerWith(EventLoop<T_Mutex> *eloop, int signo)
    {
        eloop->registerSignal(this, signo);
    }
    
    virtual void gotSignal(int signo, SigInfo info) = 0;
};

}  // namespace org_davmac

#endif

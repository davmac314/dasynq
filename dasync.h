#ifndef DASYNC_H_INCLUDED
#define DASYNC_H_INCLUDED

#include "dasync-aen.h"

#include <csignal>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <vector>
#include <atomic>
#include <condition_variable>
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

/*
#ifdef __GNUC__
#ifdef __clang__
#define EMPTY_BODY private: char empty_fill[0];
#else
#define EMPTY_BODY private: char empty_fill[0];
#endif
#else
#define EMPTY_BODY
#endif
*/

namespace dasync {


/**
 * Values for rearm/disarm return from event handlers
 */
enum class Rearm
{
    /** Re-arm the event watcher so that it receives further events */
    REARM,
    /** Disarm the event watcher so that it receives no further events, until it is re-armed explicitly */
    DISARM,
    /** Remove the event watcher (and call "removed" callback) */
    REMOVE
};


// Forward declarations:
template <typename T_Mutex> class EventLoop;
template <typename T_Mutex> class PosixFdWatcher;
template <typename T_Mutex> class PosixSignalWatcher;

// Information about a received signal.
// This is essentially a wrapper for the POSIX siginfo_t; its existence allows for mechanisms that receive
// equivalent signal information in a different format (eg signalfd on Linux).
using SigInfo = EpollTraits::SigInfo;

//using FD_s = EpollTraits::FD_s;
//using FD_r = EpollTraits::FD_r;

namespace dprivate {
    // (non-public API)

    enum class WatchType
    {
        SIGNAL,
        FD
    };
    
    template <typename T_Mutex, typename Traits> class EventDispatch;
    
    // Represents a queued event notification
    class BaseWatcher
    {
        template <typename T_Mutex, typename Traits> friend class EventDispatch;
        template <typename T_Mutex> friend class dasync::EventLoop;
        
        protected:
        WatchType watchType;
        int active : 1;
        int deleteme : 1;
        
        BaseWatcher * next;
        
        public:
        BaseWatcher(WatchType wt) : watchType(wt), active(0), deleteme(0), next(nullptr) { }
        
        // Called when the watcher has been removed.
        // When called it is guaranteed that:
        // - the dispatch method is not currently running
        // - the dispatch method will not be called.
        virtual void watchRemoved() noexcept { }
    };


    // (Non-template) Base signal event - not part of public API
    class BaseSignalWatcher : public BaseWatcher
    {
        template <typename T_Mutex, typename Traits> friend class EventDispatch;
        template <typename T_Mutex> friend class dasync::EventLoop;

        SigInfo siginfo;

        protected:
        BaseSignalWatcher() : BaseWatcher(WatchType::SIGNAL) { }

        public:
        typedef SigInfo &SigInfo_p;
        
        virtual Rearm gotSignal(int signo, SigInfo_p siginfo) = 0;
    };
    
    class BaseFdWatcher : public BaseWatcher
    {
        template <typename T_Mutex, typename Traits> friend class EventDispatch;
        template <typename T_Mutex> friend class dasync::EventLoop;
        
        protected:
        int watch_fd;
        int watch_flags;
        int event_flags;
        
        BaseFdWatcher() : BaseWatcher(WatchType::FD) { }
        
        public:
        virtual Rearm gotEvent(int fd, int flags) = 0;
    };

    // Classes for implementing a fair(ish) wait queue.
    // A queue node can be signalled when it reaches the head of
    // the queue.

    template <typename T_Mutex> class waitqueue;
    template <typename T_Mutex> class waitqueue_node;

    // Select an appropriate conditiona variable type for a mutex:
    // condition_variable if mutex is std::mutex, or condition_variable_any
    // otherwise.
    template <class T_Mutex> class condvarSelector;

    template <> class condvarSelector<std::mutex>
    {
        public:
        typedef std::condition_variable condvar;
    };

    template <class T_Mutex> class condvarSelector
    {
        public:
        typedef std::condition_variable_any condvar;
    };

    template <> class waitqueue_node<NullMutex>
    {
        // Specialised waitqueue_node for NullMutex.
        // TODO can this be reduced to 0 data members?
        friend class waitqueue<NullMutex>;
        waitqueue_node * next = nullptr;
        
        public:
        void wait(std::unique_lock<NullMutex> &ul) { }
        void signal() { }
    };

    template <typename T_Mutex> class waitqueue_node
    {
        typename condvarSelector<T_Mutex>::condvar condvar;
        friend class waitqueue<T_Mutex>;
        waitqueue_node * next = nullptr;
        
        public:
        void signal()
        {
            condvar.notify_one();
        }
        
        void wait(std::unique_lock<T_Mutex> &mutex_lock)
        {
            condvar.wait(mutex_lock);
        }
    };

    template <typename T_Mutex> class waitqueue
    {
        waitqueue_node<T_Mutex> * tail = nullptr;
        waitqueue_node<T_Mutex> * head = nullptr;

        public:
        waitqueue_node<T_Mutex> * unqueue()
        {
            head = head->next;
            return head;
        }
        
        waitqueue_node<T_Mutex> * getHead()
        {
            return head;
        }
        
        void queue(waitqueue_node<T_Mutex> *node)
        {
            if (tail) {
                tail->next = node;
            }
            else {
                head = node;
            }
        }
    };

    // This class serves as the base class (mixin) for the AEN mechanism class.
    template <typename T_Mutex, typename Traits> class EventDispatch : public Traits
    {
        friend class EventLoop<T_Mutex>;

        // queue data structure/pointer
        BaseWatcher * first;

        // TODO receiveXXX need to be bracketed by queue_lock lock and unlock.
        // AEN can notify just before and after delivering a set of events?
        // - Maybe need doMechanismLock and doMechanismUnlock? AEN mechanism may not be thread safe,
        //   gives it a way to lock a mutex if necessary
        // - beginEventBatch/endEventBatch? (to allow locking)
        
        protected:
        T_Mutex lock;
        
        void receiveSignal(typename Traits::SigInfo & siginfo, void * userdata)
        {
            // Put callback in queue:
            PosixSignalWatcher<T_Mutex> * watcher = static_cast<PosixSignalWatcher<T_Mutex> *>(userdata);
            BaseSignalWatcher * bwatcher = (BaseSignalWatcher *) watcher;
            
            if (bwatcher->deleteme) {
                return;
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
            PosixFdWatcher<T_Mutex> * watcher = static_cast<PosixFdWatcher<T_Mutex> *>(userdata);
            BaseFdWatcher * bwatcher = (BaseFdWatcher *) watcher;
            
            if (bwatcher->deleteme) {
                return;
            }
            
            bwatcher->event_flags = flags;
            bwatcher->active = true;
            
            // Put in queue:
            BaseWatcher * prev_first = first;
            first = bwatcher;
            bwatcher->next = prev_first;
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
        
        void issueDelete(BaseWatcher *watcher) noexcept
        {
            // This is only called when the attention lock is held, so if the watcher is not
            // active now, it cannot become active during execution of this function.
            
            lock.lock();
            
            if (watcher->active) {
                watcher->deleteme = true;
            }
            else {
                // Actually do the delete.
                watcher->watchRemoved();
            }
            
            lock.unlock();
        }
    };
}


template <typename T_Mutex> class EventLoop
{
    //friend class PosixFdWatcher<T_Mutex>;
    friend class PosixSignalWatcher<T_Mutex>;
    //friend class SignalFdWatcher<T_Mutex>;
    
    template <typename T, typename U> using EventDispatch = dprivate::EventDispatch<T,U>;
    template <typename T> using waitqueue = dprivate::waitqueue<T>;
    template <typename T> using waitqueue_node = dprivate::waitqueue_node<T>;
    using BaseWatcher = dprivate::BaseWatcher;
    using BaseSignalWatcher = dprivate::BaseSignalWatcher;
    using BaseFdWatcher = dprivate::BaseFdWatcher;
    using WatchType = dprivate::WatchType;
    
    EpollLoop<EventDispatch<T_Mutex, EpollTraits>> loop_mech;

    // There is a complex problem with most asynchronous event notification mechanisms
    // when used in a multi-threaded environment. Generally, a file descriptor or other
    // event type that we are watching will be associated with some data used to manage
    // that event source. For example a web server needs to maintain information about
    // each client connection, such as the state of the connection (what protocol version
    // has been negotiated, etc; if a transfer is taking place, what file is being
    // transferred etc).
    //
    // However, sometimes we want to remove an event source (eg webserver wants to drop
    // a connection) and delete the associated data. The problem here is that it is
    // difficult to be sure when it is ok to actually remove the data, since when
    // requesting to unwatch the source in one thread it is still possible that an
    // event from that source is just being reported to another thread (in which case
    // the data will be needed).
    //
    // To solve that, we:
    // - allow only one thread to poll for events at a time, using a lock
    // - use the same lock to prevent polling, if we want to unwatch an event source
    // - generate an event to interrupt any polling that may already be occurring in
    //   another thread
    // - mark handlers as active if they are currently executing, and
    // - when removing an active handler, simply set a flag which causes it to be
    //   removed once the current processing is finished, rather than removing it
    //   immediately.
    //
    // In particular the lock mechanism for preventing multiple threads polling and
    // for allowing polling to be interrupted is tricky. We can't use a simple mutex
    // since there is significant chance that it will be highly contended and there
    // are no guarantees that its acquisition will be fair. In particular, we don't
    // want a thread that is trying to unwatch a source being starved while another
    // thread polls the event source.
    //
    // So, we use two wait queues protected by a single mutex. The "attn_waitqueue"
    // (attention queue) is the high-priority queue, used for threads wanting to
    // unwatch event sources. The "wait_waitquueue" is the queue used by threads
    // that wish to actually poll for events.
    // - The head of the "attn_waitqueue" is always the holder of the lock
    // - Therefore, a poll-waiter must be moved from the wait_waitqueue to the
    //   attn_waitqueue to actually gain the lock. This is only done if the
    //   attn_waitqueue is otherwise empty.
    // - The mutex only protects manipulation of the wait queues, and so should not
    //   be highly contended.
    
    T_Mutex wait_lock;  // wait lock, used to prevent multiple threads from waiting
                        // on the event queue simultaneously.
    waitqueue<T_Mutex> attn_waitqueue;
    waitqueue<T_Mutex> wait_waitqueue;
    
    
    void registerSignal(PosixSignalWatcher<T_Mutex> *callBack, int signo)
    {
        loop_mech.addSignalWatch(signo, callBack);
    }
    
    void deregisterSignal(PosixSignalWatcher<T_Mutex> *callBack, int signo) noexcept
    {
        loop_mech.removeSignalWatch(signo);
        
        waitqueue_node<T_Mutex> qnode;
        getAttnLock(qnode);        
        
        EventDispatch<T_Mutex, EpollTraits> & ed = (EventDispatch<T_Mutex, EpollTraits> &) loop_mech;
        ed.issueDelete(callBack);
        
        releaseLock(qnode);
    }

    void registerFd(PosixFdWatcher<T_Mutex> *callback, int fd, int eventmask)
    {
        loop_mech.addFdWatch(fd, callback, eventmask);
    }

    // Acquire the attention lock (when held, ensures that no thread is polling the AEN
    // mechanism).
    void getAttnLock(waitqueue_node<T_Mutex> &qnode)
    {
        std::unique_lock<T_Mutex> ulock(wait_lock);
        attn_waitqueue.queue(&qnode);        
        while (attn_waitqueue.getHead() != &qnode) {
            qnode.wait(ulock);
        }        
    }
    
    // Acquire the poll-wait lock (to be held when polling the AEN mechanism; lower
    // priority than the attention lock).
    void getPollwaitLock(waitqueue_node<T_Mutex> &qnode)
    {
        std::unique_lock<T_Mutex> ulock(wait_lock);
        if (attn_waitqueue.getHead() == nullptr) {
            // Queue is completely empty:
            attn_waitqueue.queue(&qnode);
        }
        else {
            wait_waitqueue.queue(&qnode);
        }
        
        while (attn_waitqueue.getHead() != &qnode) {
            qnode.wait(ulock);
        }    
    }
    
    // Release the poll-wait/attention lock.
    void releaseLock(waitqueue_node<T_Mutex> &qnode)
    {
        std::unique_lock<T_Mutex> ulock(wait_lock);
        waitqueue_node<T_Mutex> * nhead = attn_waitqueue.unqueue();
        if (nhead != nullptr) {
            nhead->signal();
        }
        else {
            nhead = wait_waitqueue.getHead();
            if (nhead != nullptr) {
                attn_waitqueue.queue(nhead);
                nhead->signal();
            }
        }                
    }
    
    void processSignalRearm(BaseSignalWatcher * bsw, Rearm rearmType)
    {
        // Called with lock held
        if (rearmType == Rearm::REARM) {
            loop_mech.rearmSignalWatch_nolock(bsw->siginfo.get_signo());
        }
        else if (rearmType == Rearm::REMOVE) {
            loop_mech.removeSignalWatch_nolock(bsw->siginfo.get_signo());
        }
    }

    void processFdRearm(BaseFdWatcher * bfw, Rearm rearmType)
    {
        // Called with lock held
        if (rearmType == Rearm::REARM) {
            loop_mech.enableFdWatch_nolock(bfw->watch_fd, bfw, bfw->watch_flags);
        }
        else if (rearmType == Rearm::REMOVE) {
            loop_mech.removeFdWatch_nolock(bfw->watch_fd);
        }
    }

    bool processEvents() noexcept
    {
        EventDispatch<T_Mutex, EpollTraits> & ed = (EventDispatch<T_Mutex, EpollTraits> &) loop_mech;
        ed.lock.lock();
        
        // So this pulls *all* currently pending events and processes them in the current thread.
        // That's probably good for throughput, but maybe the behavior should be configurable.
        
        BaseWatcher * pqueue = ed.first;
        ed.first = nullptr;
        bool active = false;
        
        BaseWatcher * prev = nullptr;
        for (BaseWatcher * q = pqueue; q != nullptr; q = q->next) {
            if (q->deleteme) {
                q->watchRemoved();
                if (prev) {
                    prev->next = q->next;
                }
                else {
                    pqueue = q->next;
                }
            }
            else {
                q->active = true;
                active = true;
            }
        }
        
        ed.lock.unlock();
        
        while (pqueue != nullptr) {
            Rearm rearmType;
            
            switch (pqueue->watchType) {
            case WatchType::SIGNAL: {
                BaseSignalWatcher *bsw = static_cast<BaseSignalWatcher *>(pqueue);
                rearmType = bsw->gotSignal(bsw->siginfo.get_signo(), bsw->siginfo);
                break;
            }
            case WatchType::FD: {
                BaseFdWatcher *bfw = static_cast<BaseFdWatcher *>(pqueue);
                rearmType = bfw->gotEvent(bfw->watch_fd, bfw->event_flags);
                break;
            }
            default: ;
            }
            
            ed.lock.lock();
            
            pqueue->active = false;
            if (pqueue->deleteme) {
                rearmType = Rearm::REMOVE;
            }
            switch (pqueue->watchType) {
            case WatchType::SIGNAL:
                processSignalRearm(static_cast<BaseSignalWatcher *>(pqueue), rearmType);
                break;
            case WatchType::FD:
                processFdRearm(static_cast<BaseFdWatcher *>(pqueue), rearmType);
                break;
            default: ;
            }
            
            if (rearmType == Rearm::REMOVE) {
                pqueue->watchRemoved();
            }
            
            ed.lock.unlock();
            
            pqueue = pqueue->next;
        }
        
        return active;
    }

    
    public:
    void run() noexcept
    {
        while (! processEvents()) {
            waitqueue_node<T_Mutex> qnode;
            
            // We only allow one thread to poll the mechanism at any time, since otherwise
            // removing event watchers is a nightmare beyond comprehension.
            getPollwaitLock(qnode);
            
            // Pull events from the AEN mechanism and insert them in our internal queue:
            loop_mech.pullEvents(true);
            
            // Now release the wait lock:
            releaseLock(qnode);
        }
    }
};



typedef EventLoop<NullMutex> NEventLoop;
typedef EventLoop<std::mutex> TEventLoop;

// from dasync.cc:
TEventLoop & getSystemLoop();

// Posix signal event
template <typename T_Mutex>
class PosixSignalWatcher : private dprivate::BaseSignalWatcher
{
    friend class EventLoop<T_Mutex>;
    
public:
    using SigInfo_p = BaseSignalWatcher::SigInfo_p;

    // If an attempt is made to register with more than one event loop at
    // a time, behaviour is undefined.
    inline void registerWith(EventLoop<T_Mutex> *eloop, int signo)
    {
        eloop->registerSignal(this, signo);
    }
    
    //virtual void gotSignal(int signo, SigInfo_p info) = 0;
};

template <typename T_Mutex>
class PosixFdWatcher : private dprivate::BaseFdWatcher
{
public:
    void registerWith(EventLoop<T_Mutex> *eloop, int fd, int flags)
    {
        this->watch_fd = fd;
        this->watch_flags = flags;
        eloop->registerFd(this, fd, flags);
    }
    
    // virtual void gotEvent(int fd, int flags) = 0;
};

}  // namespace dasync

#endif

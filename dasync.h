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



template <typename T_Mutex, typename Traits> class EventDispatch : public Traits
{
    friend class EventLoop<T_Mutex>;

    // T_Mutex queue_lock; // lock to protect active queue and AEN internal structures
    
    
    // queue data structure/pointer
    BaseWatcher * first;

    // TODO receiveXXX need to be bracketed by queue_lock lock and unlock.
    // AEN can notify just before and after delivering a set of events?
    // - Maybe need doMechanismLock and doMechanismUnlock? AEN mechanism may not be thread safe,
    //   gives it a way to lock a mutex if necessary
    // - beginEventBatch/endEventBatch? (to allow locking)
    
    protected:
    T_Mutex lock;
    
    /*
    void beginEventBatch()
    {
        queue_lock.lock();
    }
    
    void endEventBatch()
    {
        queue_lock.unlock();
    }
    */
    
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
        lock.lock();
        
        // So this pulls *all* currently pending events and processes them in the current thread.
        // That's probably good for throughput, but maybe the behavior should be configurable.
        
        BaseWatcher * pqueue = first;
        first = nullptr;
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
        
        lock.unlock();
        
        while (pqueue != nullptr) {
            pqueue->dispatch(); // TODO: handle re-arm etc
                     // (need to do this with lock)
            lock.lock();
            pqueue->active = false;
            if (pqueue->deleteme) {
                pqueue->watchRemoved();
            }
            lock.unlock();
            pqueue = pqueue->next;    
        }
        
        return active;
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



template <typename T_Mutex> class EventLoop
{
    //friend class PosixFdWatcher<T_Mutex>;
    friend class PosixSignalWatcher<T_Mutex>;
    //friend class SignalFdWatcher<T_Mutex>;
    
    
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
    
    public:
    void run() noexcept
    {
        EventDispatch<T_Mutex, EpollTraits> & ed = (EventDispatch<T_Mutex, EpollTraits> &) loop_mech;
        while (! ed.processEvents()) {

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

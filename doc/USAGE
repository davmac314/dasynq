Dasynq is in early development, and is not feature complete. With that in mind, here is a quick
run down on usage:


## 1. Selecting model - single threaded or thread-safe

Dasynq can be used as a thread-safe event loop in a multi-threaded client, or it can be used as
in a single-threaded client. You can instantiate an event loop of your chosen type with:

    #include "dasynq.h"
    
    using Loop_t = dasynq::EventLoop<dasynq::NullMutex>();  // single-threaded
    using Loop_t = dasynq::EventLoop<std::mutex>();         // multi-threaded

    auto my_loop = Loop_t();

There are essentially three ways of using an event loop:

- Only access the loop from a single thread. You can use a single-threaded event loop for this.
- Access the loop from multiple threads, but poll it (run callbacks) on only one thread. This
  requries a thread-safe event loop, and there are some limitations on use of certain methods.
- Access the loop from multiple threads, including polling it from multiple threads. This requires
  (obviously) a thread-safe event loop, and places significant (but not necssarily problematic)
  limitations on the use of certain methods.

Once the event loop instance is created, you can register event watchers with it; then you must
process event callbacks by calling one of the poll/run functions. Event callbacks will only be
run on threads that call the poll/run methods, during the execution of those methods.

The restrictions when using multiple threads mainly concern the activation of callbacks. When an
event occurs it is (temporarily) disabled while the callback is called. The application must
ensure that the callback cannot be re-entered while it is active, either by not enabling it or
by polling the event loop with only a single thread.


## 2. Event Watchers

Once you have created an event loop instance, you can create watchers for various events, and
register them with the event loop. For example, to watch for input readiness on a file descriptor,
create an FdWatcher:

    using Rearm = dasync::Rearm;

    class MyFdWatcher : public Loop_t::FdWatcher
    {
        Rearm gotEvent(Loop_t *, int fd, int flags) override
        {
            // Process I/O here
    
            return Rearm::REARM;  // re-enable watcher
        }
    };

    MyFdWatcher my_fd_watcher;
    my_fd_watcher.registerWith(&my_loop, fd, IN_EVENTS);

As shown above, you should extend the appropriate watcher class and override the callback method.
The watcher is normally disabled during the processing of the callback method (and must not be
re-enabled until the callback method has finished executing, if there may be other threads polling
the event loop. Callback methods cannot be re-entrant!).

Callback methods usually return a "Rearm" value. There are several possible values:

- Rearm::REARM : re-enable the watcheer
- Rearm::DISARM : disable the watcher (if it has been re-enabled in the meantime, which should
                  only be the case for a single-threaded event loop).
- Rearm::NOOP   : do not change the watcher state (leave it disabled, or enabled if it has been
                  enabled explicitly; the latter is only safe if only one thread polls the event
                  loop).
- Rearm::REMOVE : disable and de-register the watcher
- Rearm::REMOVED : assume the watcher has been removed already and may be invalid. Do not touch
                   the watcher in any way! Necessary if the watcher has been deleted.

Note that if multiple threads are polling the event loop, it is difficult to use some of the Rearm
values correctly.  

To remove a watcher, call deregister:

    my_fd_watch.deregisterWatch(&my_loop);

The 'watchRemoved' callback gets called when the watch has been successfully de-registered. If the
event callback is currently executing in another thread, the `watchRemoved` will _not_ be called
straight away. You should never `delete` your watcher object until the `watchRemoved` callback is
called.


## 2.1 File descriptor watches

See example above.

Some backends support watching IN_EVENTS | OUT_EVENTS at the same time, but some don't. Use a
BidiFdWatcher if you need to do that:

    class MyBidiFdWatcher : public Loop_t::BidiFdWatcher
    {
        Rearm readReady(Loop_t *, int fd) override
        {
            return Rearm::REARM;
        }
        
        Rearm writeReady(Loop_t *, int fd) override
        {
            return Rearm::REARM;
        }
    };

    MyBidiFdWatcher my_bidi_watcher;
    my_bidi_watcher.registerWith(my_loop, fd, IN_EVENTS | OUT_EVENTS);
    // use the IN_EVENTS/OUT_EVENTS flags to specify which events you want enabled.

Beware! If the loop is being polled by multiple threads then the "readReady" and "writeReady"
callbacks may be called simultaneously!

## 2.2 Signal watchers

    class MySignalWatcher : public Loop_t::SignalWatcher
    {
	// SignalWatcher defines type "SigInfo_p"
        Rearm gotSignal(Loop_t *, int signo, SigInfo_p siginfo)
        {
            return Rearm::REARM;
        }
    }

Methods available in SigInfo_p may vary from platform to platform, but are intended to mirror the
`siginfo_t` structure of the platform. One standard method is "int get_signo()".

## 2.3 Child process watchers


## 3. Error handling

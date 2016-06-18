Dasynq is in early development, and is not feature complete. With that in mind, here is a quick
run down on usage:


## 1. Selecting model - single threaded or thread-safe

Dasynq can be used as a thread-safe event loop in a multi-threaded client, or it can be used as
in a single-threaded client. You can instantiate an event loop of your chosen type with:

    #include "dasynq.h"
    
    using Loop_t = dasynq::EventLoop<dasynq::NullMutex>();  // single-threaded
    using Loop_t = dasynq::EventLoop<std::mutex>();         // multi-threaded

    Loop_t my_loop();

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
        Rearm fdEvent(Loop_t *, int fd, int flags) override
        {
            // Process I/O here
    
            return Rearm::REARM;  // re-enable watcher
        }
    };

    MyFdWatcher my_fd_watcher;
    my_fd_watcher.addWatch(my_loop, fd, IN_EVENTS);

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

    my_fd_watch.deregister(my_loop);

The 'watchRemoved' callback gets called when the watch has been successfully de-registered. If the
event callback is currently executing in another thread, the `watchRemoved` will _not_ be called
straight away. You should never `delete` your watcher object until the `watchRemoved` callback is
called.


## 2.1 File descriptor watches

See example above. Not shown there is the `setEnabled` method:

    setEnabled(Loop_t &, bool b);

This enables or disables the watcher. You should not enable a watcher that might currently have
its callback running (unless the event loop is polled by only single thread).

Some backends support watching IN_EVENTS | OUT_EVENTS at the same time, but some don't. Use a
BidiFdWatcher if you need to do that:

    class MyBidiFdWatcher : public Loop_t::BidiFdWatcher
    {
        Rearm readReady(Loop_t &, int fd) override
        {
            return Rearm::REARM;
        }
        
        Rearm writeReady(Loop_t &, int fd) override
        {
            return Rearm::REARM;
        }
    };

    MyBidiFdWatcher my_bidi_watcher;
    my_bidi_watcher.addWatch(my_loop, fd, IN_EVENTS | OUT_EVENTS);
    // use the IN_EVENTS/OUT_EVENTS flags to specify which events you want enabled.

Beware! If the loop is being polled by multiple threads then the "readReady" and "writeReady"
callbacks may be called simultaneously! BidiFdWatcher has (protected) methods to enable the input
and output channels separately. As usual, you should make certain not to enable a watcher that
is currently active, unless there is only a single thread running callbacks:

    setInWatchEnabled(Loop_t &, bool);

    setOutWatchEnabled(Loop_t &, bool);

    setWatches(Loop_t &, int flags);

   

## 2.2 Signal watchers

    class MySignalWatcher : public Loop_t::SignalWatcher
    {
	// SignalWatcher defines type "SigInfo_p"
        Rearm received(Loop_t &, int signo, SigInfo_p siginfo)
        {
            return Rearm::REARM;
        }
    };

    MySignalWatcher msw;
    msw.addWatch(my_loop, SIGINT);

Methods available in SigInfo_p may vary from platform to platform, but are intended to mirror the
`siginfo_t` structure of the platform. One standard method is "int get_signo()".

You should mask the signal (with sigprocmask/pthread_sigmask) in all threads before adding a
watcher for that signal to an event loop.


## 2.3 Child process watchers

    class MyChildProcWatcher : public Loop_t::ChildProcWatcher
    {
        Rearm childStatus(Loop_t &, pid_t child, int status)
        {
            return Rearm::REARM;
        }
    };

    MyChildProcWatcher my_child_watcher;
    my_child_watcher.addWatch(my_loop, my_child_pid);

Since adding a watcher can fail, but you can't try to add a watcher before the child is actually
spawned, you might get into the awkward position of having a child that you can't watch. To avoid
that, ChildProcWatcher has a special method to reserve a watch before actually registering it with
the event loop. Use:

    my_child_watcher.reserveWatch(my_loop);

... to reserve the watch (this can fail!), and then, after spawning a child process:

    my_child_watcher.addReserved(my_loop, my_child_pid);

... to claim the reserved watch. This cannot fail. However, if done while another thread may be
polling the event loop, it is potentially racy; the other thread may reap the child before the
watcher is added. So, you should fork using the ChildProcWatcher::fork method:

    pid_t child_pid = my_child_watcher.fork(my_loop);

This "atomically" creates the child process and registers the watcher. (If registration fails the
child will terminate, or will never be forked). A `std::bad_alloc` or `std::system_error` exception
is thrown on failure.


## 3. Error handling

In general, the only operations that can fail (other than due to program error) are the watch
registration methods (addWatch). If they fail, they will throw an exception; either a
`std::bad_alloc` or a `std::system_error`.


## 4. Restrictions and limitations

Most of the following limitations carry through from the underlying backend, rather than being
inherent in the design of Dasynq itself.

You cannot generally add two watchers for the same identity (file descriptor, signal, child
process). (Exception: when using kqueue backend, you can add one fd read watcher and one fd write
watcher; however, it's better to use a `BidiFdWatcher` to abstract away platform differences).

You should remove the watcher(s) for a file descriptor *before* you close the file descriptor,
and especially before adding a new watcher for the same file descriptor number (due to having
opened in the meantime another file/pipe/socket which was assigned the same fd). (This might
not cause an issue with the current design/backends, but might be a problem in the future).

The event loop may not function correctly after a fork() operation (including a call to
ChildProcWatcher::fork).

The implementation may use SIGCHLD to detect child process termination. Therefore you should not
try to watch SIGCHLD independently.

Creating two event loop instances in a single application is not likely to work well, or at all.
(This limitation may be lifted in a future release).

You must not register a watcher with an event loop while it is currently registered.

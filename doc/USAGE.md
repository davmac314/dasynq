Dasynq is in early development, and is not feature complete. With that in mind, here is a quick
run down on usage:


## 1. Selecting model - single threaded or thread-safe

Dasynq can be used as a thread-safe event loop in a multi-threaded client, or it can be used as
in a single-threaded client. You can instantiate an event loop of your chosen type with:

    #include "dasynq.h"

    using loop_t = dasynq::event_loop_n;    // single-threaded
              // OR
    using loop_t = dasynq::event_loop_th;   // multi-threaded
 
 	// You can also specify a custom mutex implementation. It must
 	// support "lock", "try_lock" and "unlock" operations:
    
    using loop_t = dasynq::event_loop<my_mutex_type>();

	// instantiate the chosen loop:
    loop_t my_loop();

There are essentially three ways of using an event loop:

- Only access the loop from a single thread. You can use a single-threaded event loop for this.
- Access the loop from multiple threads, but poll it (run callbacks) on only one thread. This
  requires a thread-safe event loop, and there are some limitations on use of certain methods.
- Access the loop from multiple threads, including polling it from multiple threads. This requires
  (obviously) a thread-safe event loop, and places significant (but not necessarily problematic)
  limitations on the use of certain methods.

Once the event loop instance is created, you can register event watchers with it; then you must
process event callbacks by calling one of the poll/run functions. Event callbacks will only be
run on threads that call the poll/run methods, during the execution of those methods.

The restrictions when using multiple threads mainly concern the activation of callbacks. When an
event occurs it is (temporarily) disabled while the callback is called. The application must
ensure that the callback cannot be re-entered while it is active, either by not enabling it or
by polling the event loop with only a single thread (and only calling enable/disable from within
that thread).


## 2. Event Watchers

Once you have created an event loop instance, you can create watchers for various events, and
register them with the event loop. For example, to watch for input readiness on a file descriptor,
create an `fd_watcher`:

    using rearm = dasynq::rearm;

    class my_fd_watcher : public loop_t::fd_watcher_impl<my_fd_watcher>
    {
        rearm fd_event(loop_t &, int fd, int flags)
        {
            // Process I/O here
            //   int r = read(fd, ...) etc
    
            return rearm::REARM;  // re-enable watcher
        }
    };

    my_fd_watcher my_fd_watcher;
    my_fd_watcher.add_watch(my_loop, fd, dasynq::IN_EVENTS);

(To watch for output readiness, use `OUT_EVENTS` instead of `IN_EVENTS`).

The above may look strange, as it makes use of the _curiously recurring template pattern_ - that
is, the `fd_watcher_impl` template class which your watcher derives from is parameterised by the
type of your watcher itself! Although it may take a little getting used to, this allows the base
class to call the `fd_event(...)` callback function directly without requiring it to be declared
`virtual`, thereby avoiding the cost of a dynamic dispatch. 

A watcher is normally disabled during the processing of the callback method (and must not be
re-enabled until the callback method has finished executing, if there may be other threads polling
the event loop. Event watchers are not re-entrant!).

A more convenient way to add a watcher, in many cases, is to use a lambda expression:

    using rearm = dasynq::rearm;

    auto watcher = loop_t::fd_watcher::add_watch(my_loop, fd, IN_EVENTS,
            [](loop_t &eloop, int fd, int flags) -> rearm {
        
        // Process I/O here

        return rearm::REARM; // or REMOVE etc
    });

Callback methods usually return a "rearm" value. There are several possible values:

- rearm::REARM : re-enable the watcher.
- rearm::DISARM : disable the watcher (if it has been re-enabled in the meantime, which should
                  only be the case for a single-threaded event loop).
- rearm::NOOP   : do not change the watcher state (leave it disabled, or enabled if it has been
                  enabled explicitly; the latter is only safe if only one thread polls the event
                  loop).
- rearm::REMOVE : disable and de-register the watcher.
- rearm::REMOVED : assume the watcher has been removed already and may be invalid. This tells
                  Dasynq not to touch the watcher in any way! Necessary if the watcher has been
                  deleted.
- rearm::REQUEUE : re-queue the watcher. Use this if further processing is required but you want
                  other pending events (of suitable priority) to be processed first.

Note that if multiple threads are polling the event loop, it is difficult to use some of the
`rearm` values correctly; you should generally use only `REARM`, `REMOVE` or `NOOP` in that
case. 

To remove a watcher, call `deregister`:

    my_fd_watch.deregister(my_loop);

The 'watch_removed' callback gets called when the watch has been successfully de-registered. If the
event callback is currently executing in another thread, the `watch_removed` will _not_ be called
straight away. You should never `delete` your watcher object until the `watch_removed` callback has
been called. (Often, it will be convenient to `delete` the watcher from _within_ the callback).


## 2.1 File descriptor watches

See example above. Not shown there is the `set_enabled` method:

    set_enabled(loop_t &, bool b);

This enables or disables the watcher. You should not enable a watcher that might currently have
its callback running (unless the event loop is polled by only a single thread).

Some backends support watching IN_EVENTS | OUT_EVENTS at the same time, but some don't. Use a
`bidi_fd_watcher` if you need to do that:

    class my_bidi_fd_watcher : public loop_t::bidi_fd_watcher_impl<my_bidi_fd_watcher>
    {
        rearm read_ready(loop_t &, int fd) override
        {
            // Process input
            //   int r = read(fd, ...) etc
            return rearm::REARM;
        }
        
        rearm write_ready(loop_t &, int fd) override
        {
            // Perform output
            //   int r = write(fd, ...) etc
            return rearm::REARM;
        }
    };

    my_bidi_fd_watcher my_bidi_watcher;
    my_bidi_watcher.add_watch(my_loop, fd, IN_EVENTS | OUT_EVENTS);
    // use the IN_EVENTS/OUT_EVENTS flags to specify which events you want enabled.

Beware! If the loop is being polled by multiple threads then the `readReady` and `writeReady`
callbacks may be called simultaneously! `bidi_fd_watcher` has (protected) methods to enable the
input and output channels separately. A `bidi_fd_watcher` acts as two separate watchers, which can
be enabled and disabled separately:

    set_in_watch_enabled(loop_t &, bool);

    set_out_watch_enabled(loop_t &, bool);

    set_watches(loop_t &, int flags);

It is possible to disarm either the in or out watch if the corresponding handler is active, but
it is not safe to arm them in that case. This is the same rule as for enabling/disabling watchers
generally, except that the `bidi_fd_watcher` itself actually constitutes two separate watchers.

Note also that the `bidi_fd_watcher` channels can be separately removed (by returning
`rearm::REMOVE` from the handler). The `watch_removed` callback is only called when both channels
have been removed. You cannot register channels with the event loop separately, and you must
remove both channels before you register the `bidi_fd_watcher` with another (or the same) loop.

For convenience there is also a lambda-expression version:

    auto watcher = loop_t::bidi_fd_watcher::add_watch(my_loop, fd, IN_EVENTS | OUT_EVENTS,
            [](loop_t &eloop, int fd, int flags) -> rearm {
        
        // Process I/O here

        return rearm::REARM; // or REMOVE etc
    });

The read-ready and write-notifications will go through the same function in this case; the `flags`
argument can be used to distinguish them (as `IN_EVENTS` or `OUT_EVENTS`).


## 2.2 Signal watchers

    class my_signal_watcher : public loop_t::signal_watcher_impl<my_signal_watcher>
    {
        // SignalWatcher defines type "siginfo_p"
        rearm received(loop_t &, int signo, siginfo_p siginfo)
        {
            return rearm::REARM;
        }
    };

    // Mask SIGINT signal:
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigprocmask(SIG_BLOCK, &set, NULL);

    my_signal_watcher msw;
    msw.add_watch(my_loop, SIGINT);

Methods available in `siginfo_p` may vary from platform to platform, but are intended to mirror
the `siginfo_t` structure of the platform. One standard method is `int get_signo()`.

You should mask the signal (with `sigprocmask`/`pthread_sigmask`) in all threads before adding a
watcher for that signal to an event loop. The only practical way to do this in mult-threaded
programs is by masking the signal at program startup, before creating any additional threads.
Note that signal masks are inherited by child processes; if you mask commonly used signals you
should generally unmask them after forking a child process.


## 2.3 Child process watchers

    class my_child_proc_watcher : public loop_t::child_proc_watcher<my_child_proc_watcher>
    {
        rearm status_change(loop_t &, pid_t child, int status)
        {
            return rearm::REARM;
        }
    };

    my_child_proc_watcher my_child_watcher;
    my_child_watcher.add_watch(my_loop, my_child_pid);

Since adding a watcher can fail, but you can't try to add a watcher before the child is actually
spawned, you might get into the awkward position of having a child that you can't watch. To avoid
that, ChildProcWatcher has a special method to reserve a watch before actually registering it with
the event loop. Use:

    my_child_watcher.reserve_watch(my_loop);

... to reserve the watch (this can fail!), and then, after spawning a child process:

    my_child_watcher.add_reserved(my_loop, my_child_pid);

... to claim the reserved watch. This cannot fail. However, if done while another thread may be
polling the event loop, it is potentially racy; the other thread may reap the child before the
watcher is added. So, you should fork using the ChildProcWatcher::fork method:

    pid_t child_pid = my_child_watcher.fork(my_loop);

This "atomically" creates the child process and registers the watcher. (If registration fails the
child will terminate, or will never be forked). A `std::bad_alloc` or `std::system_error` exception
is thrown on failure. If you have reserved a watch using reserve_watch(...), pass "true" as the
second (optional) parameter:

    pid_t child_pid = my_child_watcher.fork(my_loop, true);

Note however that using the `fork(...)` function largely removes the need to reserve watches: an
unwatchable child process never results.

Also note that establishing a child watch may cause the child process to be reaped (releasing its
process id) before the watch callback is called. If you want to send a signal to a child process,
special care should be taken; see the discussion on prioritising watches in section 4.3.


## 2.4 Timers

Dasynq supports timers, both "one-shot" and periodic (with a separate initial expiry and interval)
and against either the system clock (which is supposed to represent the current time as displayed
on a wall clock, and which may be adjusted while the system is running) or a monotonic clock
which only increases and should never jump.

    class my_timer : public loop_t::timer_impl<my_timer>
    {
        rearm timer_expiry(loop_t &, int expiry_count)
        {
        	return rearm::REARM;
        }
    }

Setting a timer is a two-step operation: first you add the timer to the event loop (this can fail)
and then you arm the timer with a timeout and optional interval period (this cannot fail). The
clock type (`SYSTEM` or `MONOTONIC`) is specified when you register the timer with the event loop.
    
    my_timer t1, t2, t3;
    t1.add_timer(my_loop); // defaults to MONOTONIC clock
    t2.add_timer(my_loop, clock_type::SYSTEM);
    t3.add_timer(my_loop, clock_type::MONOTONIC);

    struct timespec timeout = {5 /* seconds */, 0 /* nanoseconds */};
    t1.arm_timer_rel(my_loop, timeout);  // relative timeout (5 seconds from now)

    t2.arm_timer(my_loop, timeout);  // absolute timeout (5 seconds from beginning of 1970)
    
    t3.arm_timer_rel(my_loop, timeout, timeout); // relative, with periodic timeout 

A timer can be stopped or disabled. A stopped timer no longer expires. A disabled timer does not
issue a callback when it expires, but will do so immediately when re-enabled. Timers are disabled
when they issue a callback (but are re-enabled by returning `rearm::REARM` from the `timer_expiry`
callback function). To stop a timer, use the `stop_timer` function:

    t3.stop_timer(my_loop);

You can add and set a timer using a lambda expression:

	struct timespec expiry = {5, 0};
	struct timespec interval = {5, 0}; // {0, 0} for non-periodic	
    auto timer = loop_t::timer::add_timer(my_loop, fd, clock_type::MONOTONIC, true /* relative */,
    		expiry, interval,
            [](loop_t &eloop, int expiry count) -> rearm {
        
        // Process timer expiry here

        return rearm::REARM; // or REMOVE etc
    });


## 3. Polling the event loop

To wait for events and run watcher callbacks, call the `run` function of the event loop:

    my_loop.run();

The run function waits for one or more events, puts the events in an internal queue,  and then
notifies the watchers for those events (i.e. processes the queue) before returning. You would
generally call it in a loop:

	bool do_exit = false;
    // do_exit could be set from a callback to cause the loop to terminate
	
    while (! do_exit) {
        my_loop.run();
    }

If you want to poll the event loop, that is, check for events without waiting, use the `poll`
function instead:

    my_loop.poll();

Both `poll` and `run` will process queued events (which have been already been pulled from the
backend mechanism) if there are any, and return immediately in this case even if there were no
more events pulled from the backend. This makes the queue largely transparent to the client
application.


## 4. Other matters

### 4.1 Error handling

In general, the only operations that can fail (other than due to program error) are the watch
registration functions (`add_watch`). If they fail, they will throw an exception; either a
`std::bad_alloc` or a `std::system_error`.


### 4.2 Regular files

Most (all?) event loop mechanisms do not support watching read/write readiness on file descriptors 
associated with "regular" files (as opposed to sockets/terminals/other devices). Arguably this is
sensible, since there is no fixed buffer for such file descriptors and the ability to perform a
non-blocking read or write is not part of any state directly associated with the descriptor itself
(rather it happens due to buffering at the device level i.e. the disk). The traditonal "select"
and "poll" calls typically handle this by returning that such file descriptors are always
available for both read and write operations; when backed by a fast disk, this is practically
true, although it can be problematic for slow media or network filesystems.  

Unfortunately modern mechanisms do not carry through with support for regular files. The epoll
mechanism doesn't support them at all, and kqueue supports only read watches, with slightly
altered semantics: specifically, kqueue indicates that a regular file fd is available for reading
so long as it is not positioned exactly at the end of the associated file. 

Since it is convenient to be able to handle all file descriptors similarly, Dasynq emulates the
select/poll style of signalling "always ready" for regular files if they are not handled by the
underlying event loop mechanism. (Note that if the underlying mechanism is kqueue, you'll get
kqueue semantics for read watches, and emulated write watches; for epoll, both read and write
watches will be emulated).

If, for whatever reason, you do not want this emulation enabled for a particular file descriptor
watch, you can prevent it by using the "noemu" variant of the registration function:
 
    my_fd_watcher.add_watch_noemu(my_loop, fd, dasynq::IN_EVENTS);
    my_bidi_watcher.add_watch_noemu(my_loop, fd, IN_EVENTS | OUT_EVENTS);

When using the "no emulation" registration functions, an exception will be thrown if the file
descriptor is associated with a regular file (or is not otherwise supported by the backend
event loop mechanism).


### 4.3 Prioritising watches

Watchers can be given a priority level when they are registered, by adding an additional priority
parameter (type `int`) to the registration function, eg:

    my_fd_watcher.add_watch(my_loop, fd, dasynq::IN_EVENTS, true /* enabled *, 100 /* priority*/);
    my_bidi_watcher.add_watch(my_loop, fd, IN_EVENTS | OUT_EVENTS, 100 /* priority */);
    my_signal_watcher.add_watch(my_loop, SIGINT, 100 /* priority */);
    my_child_watcher.add_watch(my_loop, child_pid, 100 /* priority */);
    my_child_watcher.fork(my_loop, false /* reserved watch? */, 100 /* priority */);
    my_timer.add_timer(my_loop, clock_type::MONOTONIC, 100 /* priority */);

A lower priority value corresponds to a higher priority, so that "priority one" (1) is higher
priority than "priority two". High priority watches will be called before lower priority watches.

If you intend to send signals to child processes, you should always give the child process
watchers a high priority, since the watcher may cause the child to be reaped before the status
change is reported to the watcher. For multi-threaded event loop polling, there is currently no
way to ensure that the child process id is still valid; this will likely be addressed in a future
release. (Usually, the result of trying to signal a reaped process will be that the `signal`
function returns an error; in theory, though, the signal might be sent to the wrong process, if
the process id happens to have been re-used by a new process in the meantime).


### 4.4 Restrictions and limitations

Most of the following limitations carry through from the underlying backend, rather than being
inherent in the design of Dasynq itself.

You cannot generally add two watchers for the same identity (file descriptor, signal, child
process). (Exception: when using kqueue backend, you can add one fd read watcher and one fd write
watcher for the same file descriptor; however, it's better to use a `bidi_fd_watcher` to abstract
away platform differences).

You should fully remove the watcher(s) for a file descriptor *before* you close the file
descriptor. In a multi-threaded program, failure to do this could cause a watcher to read from
or write to a file descriptor which was re-opened for another purpose in the meantime.

There is a special problem when listening for connections to sockets: accepting a new connection
requires a process to have not exhausted its file descriptors. If it has, it will be unable to
accept new connections, but will also be unable to close them; the connections will remain pending
and will cause the watcher for the socket to be notified continuously (unless it is disabled).
This problem is not specific to Dasynq and is ultimately due to deficiencies in system APIs
(POSIX etc). A process that ignores this problem will potentially consume processor resources by
spinning in a tight loop when the problem occurs; however, there are no very satisfactory
solutions that do not require significant and intrusive changes to the program.

On systems without a suitable timer interface - in particular, systems without POSIX timers,
including Mac OS X up to at least 10.11.6 and OpenBSD up to at least 6.1 - there is no distinction
between `MONOTONIC` and `SYSTEM` timers. On these systems you should use only relative timers
and be aware that timers may not function correctly if the system time is adjusted.

The event loop may not function correctly in the child process after a fork() operation
(including a call to child_proc_watcher::fork). It is recommended to re-create the event loop
in the child process, if it is needed.

The implementation may use SIGCHLD to detect child process termination. Therefore you should not
try to watch SIGCHLD independently.

Creating two event loop instances in a single application is not likely to work well, or at all.
(This limitation may be lifted in a future release).

You must not register a watcher with an event loop while it is currently registered.

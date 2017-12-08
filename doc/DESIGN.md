# Notes on design and implementation

## General notes

The current implementation is not optimal but is still reasonably performant. Dasynq tries very hard to be
an as-thin-as-possible layer over the underlying backend, while also providing an extremely low-overhead
processing queue (which allows for event prioritisation and multi-threading). Some of the code might not
considered clean or idiomatic, since an effort has been made to shave off as much overhead as possible; for
instance, member fields are not initialised in cases where it is not necessary (i.e. where the value will in
any case be assigned before being used). In general code style favours raw efficiency over safety and good
practice (this does not mean that bugs necessarily result, just that more effort is required for development
and maintenance of the code base to avoid certain classes of bugs).

Some small usability issues result from the extreme reduction of overhead: a prominent example is that many
watcher functions require an event loop reference as an argument, which could be avoided by having the
watcher maintain an internal (member) reference to the event loop. This was not done since it mandates
a storage overhead which in many cases can otherwise be avoided, and it is anyway trivial to have subclasses
include such a member if desired. Similar reasoning applies to the decision for the event loop not to
maintain a list of watchers (which would allow it to release them on destruction, and perhaps to
automatically re-create the event loop after a `fork()` operation).

In some locations the offset of a class member is taken, using the `offsetof` macro. By word of the C++
language standard, this yields an undefined result (because the class is not a standard-layout class, by
virtue of having virtual members) but in practice should be safe. This is done to obtain a reference to
the containing object of a object to which we have a pointer (and which we know is a member subobject).
The alternative would be to store a pointer to the containing object in the subobject, which would be
strictly compliant but would add unnecessary overhead.

Friend classes/functions are used liberally throughout the code, especially where it helps to keep
implementation details out of the public API. (The use of friends could possibly be reduced however with
some simple changes to the design; in particular members of classes within the private internal
namespace could probably be made public).

Future versions of the API are expected to remain backwards-compatible, except in cases where this is
exceptionally difficult.


## Design overview

The design separates, as cleanly as is practical, the backend event delivery mechanism (kqueue, epoll) from
the event loop itself (which adds a prioritised queue of pending watchers). The backend mechansims are
implemented as mix-in templates which pass received events through to their base class (which is specified
as a template parameter). In fact, different event types are generally handled by different backend
mix-ins; timers are pulled in via `posix_timer_events`, `itimer_events` or (on Linux) `timerfd_events`,
and child status events are handled by `child_proc_events`. These mix-in mechanisms may rely on the level
above, eg. `child_proc_events` watches for `SIGCHLD` signals being reported by the mechanism above it.

The `dasynq::dprivate::event_dispatch` template class provides a suitable base class for the backend
mechanisms, which manages the queue of event watchers and in particular which queues the appropriate
watcher when it receives an event (via the various `receive_XXX` methods).
 
The `event_loop` class (template) simply encapsulates a backend delivery mechanism (itself wrapping an
`event_dispatch` base object which maintains the queue) providing various private functions to allow watcher
registration and enabling etc. as well as public functions for loop polling. The `event_loop` also manages
thread safety over the backend mechanism, by allowing only a single thread to poll the loop at once; this
means that de-registration can be performed by signalling a single thread and waiting for it to finish
polling. Without doing this, de-registration time would be unbounded, since it is not safe to assume that
the watcher being de-registered will not also be accessed (and queued) by a polling thread which receives an
associated event. Allowing only a single thread to poll at once might impose a bottleneck; it may prove
possible to improve this aspect of the design in the future.  However, care must be taken to ensure that
bounded-time de-registration can be performed safely.

All watchers inherit from a common base class, `base_watcher`, and it is this class that contains various
data members related to queueing functionality (a `base_watcher` contains a `heap_handle`-type member,
which is essentially a pointer to a queue node). Various `base_XXX_watcher` classes extend `base_watcher`
and add data members to record event information specific to the watcher type, and these are finally
subclassed by the public watcher types.

The `base_watcher` contains a virtual `dispatch` function that is called (with a pointer to the event loop as
a parameter) to process a queued event. This is not overridden by the watcher class, but instead by the
related `XXX_impl` class template, which has the dispatch function call the callback function from the
implementing class directly, without requiring it to be `virtual` (and therefore requiring only a single
virtual dispatch, rather than a separate virtual call for both the `dispatch` and the callback function).

For timers, multiple application timers are multiplexed over a single system level timer (or actually a pair
of systems timers - one for each clock type). Most of the functionality is common to all timer
implementations and has been factored out into the `timer_base` class.

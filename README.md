# Dasynq

Dasynq is an event loop library similar to libevent, libev and libuv. Like other such libraries, it is
crossplatform / portable. Unlike most other such libraries, it is intended to be completely usable in
a multi-threaded client program, and it is written in C++; furthermore the API is designed to allow
the creation of extremely robust clients.

The existing backends include **epoll** and **kqueue**, meaning that it works on Linux and various
BSDs (in theory - only tested on OpenBSD!) as well as Mac OS X.

*Dasynq is currently in a pre-release state*. The build system and documentation are not yet
complete. The implementation is largely complete but has not been widely tested in production
and has some minor known issues.


## Event loops

An event loop library provides a means for waiting on events that occur asynchronously. One good
example is network input/output; in a server with multiple client connections, a mechanism is needed to
wait until data is available, or until it is possible to write data, to one or more of the current
connetions (and to be able to identify _which_ connections are ready). An event loop library such as
Dasynq provides such functionality.

Note that an event loop generally supports managing various different kinds of event. Dasynq currrently
can be used for sockets and pipes (more generally, for file descriptors which provide suitable semantics,
which includes various kinds of device handle), as well as for POSIX signals, and for child process
status notifications (termination etc). It also supports one-shot and periodic timers.

Dasynq is fully multi-threaded, allowing events to be polled and processed on any thread, unlike nearly
every other event loop library (some of which are thread-safe, but which require that events be polled
from a single thread).

There are _some_ limitations on the use the Dasynq API in a multi-threaded application. However,
when used in a single-thread application, the API is just about as straight-forward as the API of most
other event loop libraries.


## Distinguishing features

There are a number of other event loop libraries. Dasynq has the following distinguishing features
when compared to the majority of other libraries:

- Dasynq is written in C++; most other event-loop libraries are in C. The use of C++ allows for a
  high-quality implementation with a straight-forward API which resolves platform differences mainly by
  using zero-runtime-cost abstractions. It also allows Dasynq to be implemented as a "header only library".
- Dasynq provides a *robust* API, usable in system-critical applications. This means that it avoids the
  possibility of errors (including allocation failures and excession of system resource limits)
  in places that would be awkward for the client to deal with. Dasynq generally *pre-allocates* resources
  meaning that many operations (such as enabling/disabling event watchers) cannot fail at run-time. Other
  libraries do not do this; *libev*, for example, goes to the other extreme and simply aborts the program
  when it hits a resource limit.
- Dasynq's multi-threading capabilities surpass many other event libraries. Although various other
  libraries have some level of multi-thread support, few allow polling for events from multiple threads
  simultaneously; typically it is necessary to poll in a single thread and dispatch events to other
  threads in a pool. Dasynq has no such limitation.
- Dasynq timers can be applied to either a monotonic or system clock, meaning that they can be used for
  correctly measuring time intervals or for setting wall-clock alarms which will correctly expire even
  if the system change is changed.

Other features of Dasynq include:

- Support for assigning priorities to events (with an arbitrary range of priority values);
- Efficient use of modern facilities such as epoll and kqueue, where available;
- Good performance.


## Using Dasynq

See doc/USAGE.md for details on how to use Dasynq.

Note: the build system currently incorporates no installation target; the suggested way to use Dasynq is to
copy the dasynq header files into the source tree of your own program.

Run "make check" to run the test suite. Minor edits to the top-level Makefile may be required.

On OpenBSD, you must use install "eg++"; the standard g++ is too old.

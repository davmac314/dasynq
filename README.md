# Dasynq

Dasynq is an event loop library similar to libevent, libev and libuv. Like other such libraries, it is
crossplatform / portable. Unlike most other such libraries, it is intended to be completely usable in
a multi-threaded client program, and it is written in C++; furthermore the API is designed to allow
the creation of extremely robust clients.

Right now, the library is in its infancy and is not well tested nor documented. The existing backends
include **epoll** and **kqueue**, meaning that it works on Linux and various BSDs (in theory - only
tested on OpenBSD).

## Event loops

An event loop library provides a means for waiting on events that occur asynchronously. One good
example is network input/output; in a server with multiple client connections, a mechanism is needed to
wait until data is available, or until it is possible to write data, to one or more of the current
connetions (and to be able to identify _which_ connections are ready). An event loop library such as
Dasynq provides such functionality.

Note that an event loop generally supports managing various different kinds of event. Dasynq currrently
can be used for sockets and pipes (more generally, for file descriptors which provide suitable semantics,
which includes various kinds of device handle), as well as for POSIX signals, and for child process
status notifications (termination etc).

## Design goals

The design aims of Dasynq are:
- it should provide a *robust* API, usable in system-critical applications. This means that it must
  avoid the possibility of errors (including allocation failures) in awkward places. For example, it
  should be possible to enable and disable event watches without possibility of error. For operations
  requiring resources, the resources can be allocated prior, so that failure is detected early
  (rather than at a point where there is no reasonable way to deal with it). Where errors can occur,
  clean recovery should be possible.
- it should be performant. The library should have good performance even when dealing with a large
  number of events / event sources.
- it should be *light-weight*, in that it should add minimal overhead compared to using the underlying
  backend directly, where this does not conflict with other design goals.
- it should be *straightforward* to use. This implies that platform differences should be abstracted
  away where possible, and that the API is consistent in design and behaviour.

## The multi-threaded event loop

Dasynq is fully multi-threaded, allowing events to be polled and processed on any thread, unlike nearly
every other event loop library (some of which are thread-safe, but which require that events be polled
from a single thread). A fully multi-thread-safe event loop is more difficult to implement than it might
sound. As such, there are _some_ limitations on the use the API in a multi-threaded application. However,
when used in a single-thread application, the API is just about as straight-forward as the API of most
other event loop libraries.

See doc/USAGE.md for how to use Dasynq.

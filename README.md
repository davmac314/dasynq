# Dasynq

Dasynq is an event loop library similar to libevent, libev and libuv. Unlike most other such libraries,
it is intended to be completely usable in a multi-threaded client program, and it is written in C++.

Right now, the library is in its infancy and is not well tested nor documented. The only existing
backend is **epoll**, meaning that it works on Linux, though I hope this will change soon (in
particular, I intend to develop a **kqueue** backend).

The design aims of Dasynq are:
- it should be *robust*, providing the means to avoid the possibility of errors in awkward places. For
  example, it should be possible to enable and disable event watchers without possibility of error (so
  long as the backend mechanism allows it).
- it should be super *light-weight*, in that it should add absolutely minimal overhead compared to
  using the underlying backend directly
- it should be *straightforward* to use.

More to come later.

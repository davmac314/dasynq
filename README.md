# Dasynq

_Version 1.2.0_

Dasynq is an event loop library similar to libevent, libev and libuv. Like other such libraries, it is
crossplatform / portable. Unlike most other such libraries, it is intended to be completely usable in
a multi-threaded client program, and it is written in C++; furthermore the API is designed to allow
the creation of extremely robust clients, by allowing allocation of resources up front (before they
are needed in critical sections). However, it is also designed to be lightweight, and it does not
require the use of threads (and so does not require linking against a thread library).

The existing backends include **epoll** and **kqueue**, meaning that it works on Linux and various
BSDs (at least OpenBSD and FreeBSD) as well as Mac OS X ("macOS" as it is now called). There is also
a less efficient backend based on **pselect**, meaning that it should also work on nearly all other
POSIX-compliant systems.

Dasynq is distributed under the terms of the Apache License, version 2.0, as found in the LICENSE file.

Dasynq is written in C++11, using POSIX functions and some OS-specific system calls.

See the [web site](https://davmac.org/projects/dasynq/) for more information.


## Event loops

An event loop library provides a means for waiting on events that occur asynchronously. One good
example is network input/output; in a server with multiple client connections, a mechanism is needed to
wait until data is available, or until it is possible to write data, to one or more of the current
connections (and to be able to identify _which_ connections are ready). Dasynq is a multi-platform,
thread-safe C++ library which provides such functionality.

Note that an event loop generally supports managing various different kinds of event. Dasynq can be used
for detecting:
- read/write readiness on sockets, pipes, and certain devices including terminals and serial lines;
- connections to listening sockets;
- reception of POSIX signals (such as SIGTERM); and
- child process status notification (termination etc).

It also supports one-shot and periodic timers, against both a monotonic and adjustable system clock
(on systems where this is possible).

Dasynq is fully thread-safe, allowing events to be polled and processed on any thread, unlike nearly
every other event loop library (some of which are thread-safe, but require that events be polled
from a single thread).

There are _some_ limitations on the use the Dasynq API in a multi-threaded application. However,
when used in a single-thread application, the API is just about as straight-forward as the API of most
other event loop libraries.

Dasynq is also intended to allow development of extremely robust client applications. Where possible, it
allows pre-allocation of resources to prevent allocation failures from occurring at inopportune moments
during program execution.


## Using Dasynq

See [doc/USAGE.md](doc/USAGE.md) for a quick guide on how to use the Dasynq API. A full reference manual
can be found in the [doc/html](doc/html) folder of the repository / source bundle, or
[online](https://davmac.org/projects/dasynq/doc/).

GNU make is required to run the test suite / automated install.

Find or create an appropriate makefile in the `makefiles` directory and edit it to your liking.
Either copy/link it to "Makefile" in the root of the source tree, or supply it via the `-f` argument to
the `make` (or `gmake`) command. Use the `check` target to run the test suite, or `install` to install
the library. The `DESTDIR` variable can be used to install to an alternative root (for packaging purposes
etc).

    make -f makefiles/Makefile.linux  check
    make -f makefiles/Makefile.linux  install  DESTDIR=/tmp/dasynq

On OpenBSD, you must install "eg++" or llvm; the g++ from the base system is too old (4.2 in OpenBSD 6.1;
4.9+ is required). The existing makefile sample (Makefile.openbsd) has appropriate settings.

Linux, OpenBSD, FreeBSD and MacOS are supported "out of the box". For other systems you may need to edit
the `dasynq-config.h` file (see instructions within). Currently either epoll or kqueue are required; in
many BSD variants it may be possible to build by defining `DASYNQ_HAVE_KQUEUE` to `1`.

After installation, you can use "pkg-config" to find the appropriate flags to compile against Dasynq,
assuming you have pkg-config installed:

    pkg-config --cflags dasynq
    pkg-config --libs dasynq

There is also CMake support. You can add the following to your `CMakeLists.txt` file:

    find_package(Dasynq 1.1.5)
    
    # The "old way". Not sexy, but works without hitches.
    #target_include_directories(testapp PRIVATE "${DASYNQ_INCLUDE_DIRS}")
    #target_link_libraries(testapp PRIVATE ${DASYNQ_LINK_LIBS}")
    
    # The "new way". Supposedly sexier, but harder to use properly:
    target_link_libraries(yourapp
        PRIVATE Dasynq::Dasynq)
    
    # Problematically, the "new way" adds the Dasynq include directory to the
    # *system header* include path. On some platforms this may cause problems.
    # You can prevent that with the following (this affects *all* imports for
    # the 'yourapp' target):
    set_target_properties(yourapp PROPERTIES
        NO_SYSTEM_FROM_IMPORTED true
    )

It is also possible to simply copy the Dasynq headers directly into your own project.

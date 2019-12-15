#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <cstdio>
#include <cstring>
#include <csignal>
#include <iostream>
#include <algorithm>

#include "dasynq.h"

// Extremely simple chat server example for Dasynq
//
// This is an example of a chat server, using Dasynq to manage the event loop. To compile, you
// will need to specify the include path for Dasynq; something like:
//
//    cc -I../.. chatserver.cc -o chatserver
//
// When run, it will listen on port 8367 for incoming client connections. You can use "telnet", if
// you have it installed, to connect:
//
//    telnet localhost 8367
//
// If you open multiple connections from different terminal windows, you'll see that anything you
// type from one client is distributed to all the other clients.
//


// We want to use the non-threaded event loop:
using event_loop_t = dasynq::event_loop_n;

// Now we declare the event loop itself:
event_loop_t eloop;

// We'll maintain a vector of clients, represented by instances of the "client_watcher" class
// which we'll define shortly:
class client_watcher;
std::vector<client_watcher *> clients;

// The client_watcher class will manage both the input and output to a client. We use a single
// string (outbuf) as an output buffer.
// - when the output buffer is not empty, we stay enabled for output readiness, so that we can
//   keep emptying the buffer
// - when we receive input, we'll add it to the output buffer of all other clients, via the
//   send_output() function.
class client_watcher : public event_loop_t::bidi_fd_watcher_impl<client_watcher> {

    // An output buffer
    std::string outbuf;

public:
    // This is called by Dasynq when we have input to read (or when the connection is closed)
    dasynq::rearm read_ready(event_loop_t & loop, int fd)
    {
        char buf[1024];
        int r = read(fd, buf, 1024);

        if (r <= 0) {
            // read error / EOF, probably connection closed
            if (r == -1) perror("read");
            close(fd);
            clients.erase(std::find(clients.begin(), clients.end(), this));
            deregister(eloop);
            return dasynq::rearm::REMOVED;
        }

        // We read something, let's distribute to the other clients:
        for (client_watcher * other_client : clients) {
            if (other_client == this) continue;
            other_client->send_output(buf, r);
        }

        return dasynq::rearm::REARM;
    }

    // This is called by Dasynq when we can send output from our buffer
    dasynq::rearm write_ready(event_loop_t & loop, int fd)
    {
        int r = write(fd, outbuf.c_str(), outbuf.length());

        // TODO should handle errors properly
        if (r > 0) {
            outbuf = outbuf.substr(r);
        }

        return outbuf.empty() ? dasynq::rearm::DISARM : dasynq::rearm::REARM;
    }

    // watch_removed: called by Dasynq when the watcher has been removed from the event loop
    void watch_removed() noexcept override
    {
        // If we removed ourself (see read_ready(..) above) then we can delete the client watcher
        delete this;
    }

    // Add data to the output buffer, and enable the write_ready watch if needed
    void send_output(const char *buf, size_t len)
    {
        bool was_empty = outbuf.empty();
        outbuf.append(buf, len);
        if (was_empty) {
            set_out_watch_enabled(eloop, true);
        }
    }
};

const char *welcome_msg = "*** Welcome to the chat server ***\n";

int main(int argc, char **argv)
{
    // Open a listening socket, accept connections
    int ssock = socket(AF_INET, SOCK_STREAM, 0);
    if (ssock == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    // bind address (0.0.0.0:8367)
    sockaddr_in in_addr;
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(8367);
    in_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(ssock, (struct sockaddr *)&in_addr, sizeof(in_addr)) == -1) {
        perror("bind");
        return EXIT_FAILURE;
    }

    // Listen for connections
    if (listen(ssock, 16) == -1) {
        perror("listen");
        return EXIT_FAILURE;
    }

    // We'll ignore SIGPIPE to prevent the whole process dying when a connection closes
    signal(SIGPIPE, SIG_IGN);

    // Here we'll add a watcher to receive new network connections. When we get a new connection,
    // we need to add a new client_watcher to handle the connection.
    event_loop_t::fd_watcher::add_watch(eloop, ssock, dasynq::IN_EVENTS,
            [ssock](event_loop_t &eloop, int fd, int flags) -> dasynq::rearm {
        int clfd = accept(ssock, nullptr, 0);
        if (clfd == -1) {
            perror("accept");
            return dasynq::rearm::REARM;
        }

        client_watcher *new_client_watch = new client_watcher();

        try {
            // Initially we enable the watch only for IN_EVENTS. We'll enable for OUT_EVENTS
            // only when there is something in the client connection's output buffer.
            new_client_watch->add_watch(eloop, clfd, dasynq::IN_EVENTS);
            clients.push_back(new_client_watch);

            new_client_watch->send_output(welcome_msg, strlen(welcome_msg));
        }
        catch (std::exception &exc) {
            std::cerr << "Error adding watch for client: " << exc.what() << std::endl;
            delete new_client_watch;
        }

        return dasynq::rearm::REARM;
    });

    // TODO Periodically output time to other clients

    while (true) {
        eloop.run();
    }

    return EXIT_SUCCESS;
}

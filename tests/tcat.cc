#include <iostream>
#include <unistd.h>

#include "dasynq.h"

using namespace dasynq;

using loop_t = dasynq::event_loop_n;

int main(int argc, char **argv)
{
    loop_t eloop;

    bool done = false;

    loop_t::fd_watcher::add_watch(eloop, 0, IN_EVENTS, [&done](loop_t &eloop, int fd, int flags) -> rearm {
        char buf[64];
        int r = read(0, buf, sizeof(buf));
        if (r <= 0) {
            done = true;
            return rearm::DISARM;
        }

        write(1, buf, r);
        return rearm::REARM;
    });

    while (! done){
        eloop.run();
    }

    return 0;
}

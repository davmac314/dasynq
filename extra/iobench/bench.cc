/*
 * Copyright 2003 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Mon 03/10/2003 - Modified by Davide Libenzi <davidel@xmailserver.org>
 *
 *     Added chain event propagation to improve the sensitivity of
 *     the measure respect to the event loop efficency.
 *
 * Thu 17/11/2016 - Modified by Davin McCall <davmac@davmac.org>
 *
 *     Remove event watchers in between runs; formatting; minor reorganisation;
 *     change event loop to Dasynq instead of libev/libevent.
 */

#define	timersub(tvp, uvp, vvp)						\
    do {								\
        (vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;		\
        (vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;	\
        if ((vvp)->tv_usec < 0) {				\
            (vvp)->tv_sec--;				\
            (vvp)->tv_usec += 1000000;			\
        }							\
    } while (0)

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "dasynq.h"


static int count, writes, fired;
static int *pipes;
static int num_pipes, num_active, num_writes, proc_limit;
static int timers, native;
static int set_prios;

using namespace dasynq;

event_loop_n eloop;


class Pipeio : public event_loop_n::fd_watcher_impl<Pipeio>
{
    public:
    int idx;

    rearm fd_event(event_loop_n &eloop, int fd, int flags);
};

class PTimer : public event_loop_n::timer_impl<PTimer>
{
    public:
    rearm timer_expiry(event_loop_n &eloop, int intervals)
    {
        return rearm::DISARM;
    }
};


static Pipeio *evio;
static PTimer *evto;

rearm Pipeio::fd_event(event_loop_n &eloop, int fd, int flags)
{
    int widx = idx + 1;
    if (timers) {
        struct timespec ts;
        ts.tv_sec = 10;
        ts.tv_nsec = drand48() * 1e9;
        evto[idx].arm_timer_rel(eloop, ts);
    }

    unsigned char ch;
    count += read(fd, &ch, sizeof(ch));
    if (writes) {
        if (widx >= num_pipes)
            widx -= num_pipes;
        write(pipes[2 * widx + 1], "e", 1);
        writes--;
        fired++;
    }
    return rearm::REARM;
}



struct timeval *
run_once(void)
{
    int *cp, i, space;
    static struct timeval ta, ts, te, tv;

    gettimeofday(&ta, NULL);
    for (cp = pipes, i = 0; i < num_pipes; i++, cp += 2) {
        if (set_prios) {
            evio[i].add_watch(eloop, cp[0], IN_EVENTS, true, static_cast<int>(drand48() * 1000));
        }
        else {
            evio[i].add_watch(eloop, cp[0], IN_EVENTS, true);
        }
        if (timers) {
            evto[i].add_timer(eloop);
            
            struct timespec tsv;
            tsv.tv_sec = 10;
            tsv.tv_nsec = drand48() * 1e9;
            evto[i].arm_timer_rel(eloop, tsv);
        }
    }
    
    fired = 0;
    space = num_pipes / num_active;
    space = space * 2;
    for (i = 0; i < num_active; i++, fired++) {
    	write(pipes[i * space + 1], "e", 1);
    }
    
    count = 0;
    writes = num_writes - fired;
    
    {
        int xcount = 0;
        gettimeofday(&ts, NULL);
        do {
            eloop.run(proc_limit);
            xcount++;
        } while (count != num_writes);
        gettimeofday(&te, NULL);
        
        //if (xcount != count) fprintf(stderr, "Xcount: %d, Rcount: %d\n", xcount, count);
    }

    timersub(&te, &ta, &ta);
    timersub(&te, &ts, &ts);
    fprintf(stdout, "%8ld %8ld\n",
		ta.tv_sec * 1000000L + ta.tv_usec,
		ts.tv_sec * 1000000L + ts.tv_usec);

    // deregister watchers now
    for (int j = 0; j < num_pipes; j++) {
        evio[j].deregister(eloop);
        if (timers) {
            evto[j].deregister(eloop);
        }
    }

    return (&te);
}

int
main (int argc, char **argv)
{
    struct rlimit rl;
    int i, c;
    struct timeval *tv;
    int *cp;
    extern char *optarg;

    num_pipes = 100;
    num_active = 1;
    num_writes = num_pipes;
    proc_limit = -1;

    while ((c = getopt(argc, argv, "n:a:w:l:tep")) != -1) {
        switch (c) {
            case 'n':
                num_pipes = atoi(optarg);
                break;
            case 'a':
                num_active = atoi(optarg);
                break;
            case 'w':
                num_writes = atoi(optarg);
                break;
            case 'l':
                proc_limit = atoi(optarg);
                break;
            case 'e':
                native = 1;
                break;
            case 't':
                timers = 1;
                break;
            case 'p':
                set_prios = 1;
                break;
            default:
                fprintf(stderr, "Illegal argument \"%c\"\n", c);
                exit(1);
        }
    }

    rl.rlim_cur = rl.rlim_max = num_pipes * 2 + 50;
    if (setrlimit(RLIMIT_NOFILE, &rl) == -1) {
        perror("setrlimit");
    }

    evio = new Pipeio[num_pipes];
    evto = new PTimer[num_pipes];
    pipes = (int *) calloc(num_pipes * 2, sizeof(int));
    if (pipes == NULL) {
        perror("malloc");
        exit(1);
    }

    for (cp = pipes, i = 0; i < num_pipes; i++, cp += 2) {
        evio[i].idx = i;
        //evto[i].addTimer(eloop);
            
#ifdef USE_PIPES
        if (pipe(cp) == -1) {
#else
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, cp) == -1) {
#endif
            perror("pipe");
            exit(1);
        }
    }

    for (i = 0; i < 2; i++) {
        tv = run_once();
    }

    exit(0);
}

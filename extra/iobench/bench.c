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
 *     Remove event watchers in between runs; formatting; minor reorganisation.
 */

#define EV_MINPRI 0
#define EV_MAXPRI 1000

#define	timersub(tvp, uvp, vvp)						\
	do {								\
		(vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;		\
		(vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;	\
		if ((vvp)->tv_usec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_usec += 1000000;			\
		}							\
	} while (0)
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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

#if NATIVE
# include "ev.h"
#endif
#include <event.h>

typedef struct io_block_s {
    struct event event;
#if NATIVE
    struct ev_io io;
    struct ev_timer timer;
#endif
    struct io_block_s * next;  // next non-busy block
    int idx;
} io_block;

static int count, writes, fired;
static int *pipes;
static int num_pipes, num_active, num_writes;
//static struct event *events;
static io_block *io_blocks;
static int timers, native;
static int set_prios;


void
read_cb(int fd, short which, void *arg)
{
    io_block * block = arg;
	int idx = block->idx, widx = idx + 1;
	u_char ch;

    if (timers) {
        if (native) {
#if NATIVE
            block->timer.repeat = 10. + drand48 ();
            ev_timer_again (EV_DEFAULT_UC_ &block->timer);
#else
            abort ();
#endif
        }
        else
        {
            struct timeval tv;
            event_del (&io_blocks[idx].event);
            tv.tv_sec  = 10;
            tv.tv_usec = drand48() * 1e6;
            event_add(&io_blocks[idx].event, &tv);
        }
    }

	count += read(fd, &ch, sizeof(ch));
	if (writes) {
		if (widx >= num_pipes)
			widx -= num_pipes;
		write(pipes[2 * widx + 1], "e", 1);
		writes--;
		fired++;
	}
}

#if NATIVE
void
read_thunk(struct ev_io *w, int revents)
{
    read_cb (w->fd, revents, w->data);
}

void
timer_cb (struct ev_timer *w, int revents)
{
    /* nop */
}
#endif

struct timeval *
run_once(void)
{
	int *cp, i, space;
	static struct timeval ta, ts, te, tv;

	gettimeofday(&ta, NULL);

    // Set up event watches:
	for (cp = pipes, i = 0; i < num_pipes; i++, cp += 2)
	{
        if (native) {
#if NATIVE
            //if (ev_is_active (&evio [i]))
            //  ev_io_stop (&evio [i]);

            ev_io_set (&io_blocks[i].io, cp [0], EV_READ);
            ev_io_start (EV_DEFAULT_UC_ &io_blocks[i].io);
            printf("started evio %p with data = %p\n", &io_blocks[i].io, io_blocks[i].io.data); // DAV

            io_blocks[i].timer.repeat = 10. + drand48 ();
            ev_timer_again (EV_DEFAULT_UC_ &io_blocks[i].timer);
#else
            abort ();
#endif
        }
        else
        {
            event_set(&io_blocks[i].event, cp[0], EV_READ | EV_PERSIST, read_cb, &io_blocks[i]);
            if (set_prios) {
                event_priority_set(&io_blocks[i].event, drand48() * EV_MAXPRI);
            }
            if (timers) {
                tv.tv_sec  = 10.;
                tv.tv_usec = drand48() * 1e6;
            }
		    event_add(&io_blocks[i].event, timers ? &tv : 0);
        }
	}

	event_loop(EVLOOP_ONCE | EVLOOP_NONBLOCK);

	// Make the chosen number of descriptors active:
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
	        event_loop(EVLOOP_ONCE | EVLOOP_NONBLOCK);
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

    cp = pipes;
	for (int j = 0; j < num_pipes; j++, cp += 2) {
        if (native) {
#if NATIVE
            ev_io_stop(EV_DEFAULT_UC_ &io_blocks[j].io);
#endif
        }
        else {
            event_del(&io_blocks[j].event);
            event_set(&io_blocks[j].event, cp[0], EV_READ | EV_PERSIST, read_cb, &io_blocks[j]);
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
    while ((c = getopt(argc, argv, "n:a:w:tep")) != -1) {
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

#if 1
    rl.rlim_cur = rl.rlim_max = num_pipes * 2 + 50;
    if (setrlimit(RLIMIT_NOFILE, &rl) == -1) {
        perror("setrlimit");
    }
#endif

    io_blocks = calloc(num_pipes, sizeof(*io_blocks));
    pipes = calloc(num_pipes * 2, sizeof(int));
    if (io_blocks == NULL || pipes == NULL) {
        perror("malloc");
        exit(1);
    }

    event_init();

    // Open as many pipes/sockets as required; allocate watchers and timers:
    for (cp = pipes, i = 0; i < num_pipes; i++, cp += 2) {
        io_blocks[i].idx = i;
        if (native) {
#if NATIVE
            ev_init (&io_blocks[i].timer, timer_cb);
            ev_init (&io_blocks[i].io, read_thunk);
            io_blocks[i].io.data = &io_blocks[i];
            printf("set pipe data %d to %p\n", i, &io_blocks[i]); // DAV
#endif
        }
        else {
            // This actually adds the event to the event loop, so we don't do it here:
            // event_set(&events[i], cp[0], EV_READ | EV_PERSIST, read_cb, (void *) i);
        }
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

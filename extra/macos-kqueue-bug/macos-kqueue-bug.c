#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

// Program to demonstrate kqueue bug on macOS (Sierra, possibly others). The
// kqueue mechanism can sometimes reports that a SIGCHLD signal has been
// received, but the signal does not yet report as pending.
//
// Running this program will output a message similar to the following, if the
// bug is present:
//
//    Received SIGCHLD kevent but SIGCHLD is not (yet) pending; i = 273
//
// (The number is the number of loop iterations before the bug was hit).
//
// This bug makes it impossible to reliably recover signal data when using
// kqueue to detect signals. The problem is that kqueue reports signal
// delivery attempts, which is different (potentially greater) than the
// number of signals actually received (i.e. because a signal can be merged
// with another, already pending, instance of the same signal). So, when
// this bug is present, and we receive a notification of a signal via kqueue
// but the signal does not show as pending, we do not know if it is because
// the signal has not yet arrived or if delivery was already attempted. We
// cannot risk waiting for the signal, because this might block indefinitely.
//
// Note that in this test, we just have the child process fork and exit to
// generate the SIGCHLD signal. However, we can have the child send the
// parent any signal prior to terminating, and reproduce the problem with that
// signal, i.e. it is not specific to the case of SIGCHLD and terminating
// processes.

void errout(const char * s)
{
    perror(s);
    exit(1);
}

void sighandler(int signo)
{

}

int main(int argc, char **argv)
{
    int kq = kqueue();
    if (kq == -1) errout("kqueue");

    // Mask SIGCHLD (but establish a handler):
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGCHLD);
    sigprocmask(SIG_BLOCK, &sigset, NULL);

    struct sigaction sa;
    sa.sa_handler = sighandler;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) errout("sigaction");

    struct timespec timeout;
    timeout.tv_nsec = 0;
    timeout.tv_sec = 0;

    // Set up kqueue to report SIGCHLD:
    struct kevent kev;
    EV_SET(&kev, SIGCHLD, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
    if (kevent(kq, &kev, 1, NULL, 0, &timeout) == -1) errout("kevent");

    for (int i = 0; i < 10000; i++) {
        // Fork a child, which immediately exits, to generate SIGCHLD:
        pid_t child = fork();
        if (child == 0) {
            _exit(0);
        }

        // Retrieve event from kqueue, and make sure it refers to the SIGCHLD:
        int r;
        if ((r = kevent(kq, NULL, 0, &kev, 1, NULL)) == -1) errout("kevent");
        if (r != 1) {
            fprintf(stderr, "kevent: didn't receive one event?\n");
            exit(1);
        }

        if (kev.filter != EVFILT_SIGNAL || kev.ident != SIGCHLD) {
            fprintf(stderr, "kevent: received wrong event?\n");
            exit(1);
        }

        // Now confirm that SIGCHLD is actually pending:
        sigset_t pendingsigs;
        sigpending(&pendingsigs);
        if (! sigismember(&pendingsigs, SIGCHLD)) {
            fprintf(stderr, "Received SIGCHLD kevent but SIGCHLD is not (yet) pending; i = %d\n", i);
            exit(1);
        }

        int signo;
        sigwait(&sigset, &signo);
        int status;
        wait(&status);
    }

    return 0;
}

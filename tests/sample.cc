#include <iostream>
#include <unistd.h>
#include <mutex>

#include "dasynq.h"

using namespace dasynq;

using Loop_t = event_loop<std::mutex>;

class MySignalWatcher : public Loop_t::signal_watcher_impl<MySignalWatcher>
{
    public:
    rearm received(Loop_t & eloop, int signo, siginfo_p siginfo)
    {
        using namespace std;
        cout << "Got signal: " << signo << endl;
        return rearm::REARM;
    }
};

class MyTimer : public Loop_t::timer_impl<MyTimer>
{
    public:
    rearm timer_expiry(Loop_t & eloop, int expiry_count)
    {
        using namespace std;
        cout << "Got timeout (1)!" << endl;
        return rearm::REMOVE;
    }
};

class MyTimer2 : public Loop_t::timer_impl<MyTimer2>
{
    public:
    rearm timer_expiry(Loop_t & eloop, int expiry_count)
    {
        using namespace std;
        cout << "Got timeout (2)!" << endl;
        return rearm::REARM;
    }
};

int main(int argc, char **argv)
{
    Loop_t eloop;

    // block USR1 / USR2 reception    
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigaddset(&set, SIGUSR2);    
    sigprocmask(SIG_BLOCK, &set, NULL);
    
    MySignalWatcher mse1, mse2;
    mse1.add_watch(eloop, SIGUSR1);
    mse2.add_watch(eloop, SIGUSR2);
    
    MyTimer mt1;
    mt1.add_timer(eloop);
    struct timespec timeout {3, 0};
    mt1.arm_timer_rel(eloop, timeout);
    
    MyTimer2 mt2;
    mt2.add_timer(eloop);
    timeout = {4, 0};
    struct timespec interval {1, 500000000}; // 1.5s
    mt2.arm_timer_rel(eloop, timeout, interval);

    sleep(1);
    
    std::cout << "Running eloop..." << std::endl;
    eloop.run();
    //std::cout << "Running eloop..." << std::endl;
    eloop.run();
    //std::cout << "Running eloop..." << std::endl;
    eloop.run();
    
    return 0;
}

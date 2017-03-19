#include <iostream>
#include <unistd.h>
#include <mutex>

#include "dasynq.h"

using namespace dasynq;

using Loop_t = event_loop<std::mutex>;

class MySignalWatcher : public Loop_t::signal_watcher
{
    rearm received(Loop_t & eloop, int signo, siginfo_p siginfo) override
    {
        using namespace std;
        cout << "Got signal: " << signo << endl;
        return rearm::REARM;
    }
};

class MyTimer : public Loop_t::timer
{
    rearm timer_expiry(Loop_t & eloop, int expiry_count)
    {
        using namespace std;
        cout << "Got timeout!" << endl;
        return rearm::REMOVE;
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
    
    MyTimer mt2;
    mt2.add_timer(eloop);
    timeout = {4, 0};
    mt2.arm_timer_rel(eloop, timeout);

    sleep(1);
    
    std::cout << "Running eloop..." << std::endl;
    eloop.run();
    //std::cout << "Running eloop..." << std::endl;
    eloop.run();
    //std::cout << "Running eloop..." << std::endl;
    eloop.run();
    
    return 0;
}

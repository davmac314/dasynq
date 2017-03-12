#include <iostream>
#include <unistd.h>
#include <mutex>

#include "dasynq.h"

using namespace dasynq;

using Loop_t = EventLoop<std::mutex>;

class MySignalWatcher : public Loop_t::SignalWatcher
{
    Rearm received(Loop_t & eloop, int signo, SigInfo_p siginfo) override
    {
        using namespace std;
        cout << "Got signal: " << signo << endl;
        return Rearm::REARM;
    }
};

class MyTimer : public Loop_t::Timer
{
    Rearm timerExpiry(Loop_t & eloop, int expiry_count)
    {
        using namespace std;
        cout << "Got timeout!" << endl;
        return Rearm::REMOVE;
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
    mse1.addWatch(eloop, SIGUSR1);
    mse2.addWatch(eloop, SIGUSR2);
    
    MyTimer mt1;
    mt1.addTimer(eloop);
    struct timespec timeout {3, 0};
    mt1.armTimerRel(eloop, timeout);
    
    MyTimer mt2;
    mt2.addTimer(eloop);
    timeout = {4, 0};
    mt2.armTimerRel(eloop, timeout);

    sleep(1);
    
    std::cout << "Running eloop..." << std::endl;
    eloop.run();
    //std::cout << "Running eloop..." << std::endl;
    eloop.run();
    //std::cout << "Running eloop..." << std::endl;
    eloop.run();
    
    return 0;
}

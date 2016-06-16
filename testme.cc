#include <iostream>
#include <unistd.h>
#include <mutex>
#include "dasynq.h"
// #include "dmutex.h"

using namespace dasynq;

class MySignalWatcher : public SignalWatcher<std::mutex>
{
    Rearm gotSignal(EventLoop<std::mutex> * eloop, int signo, SigInfo_p siginfo) override
    {
        using namespace std;
        cout << "Got signal: " << signo << endl;
        return Rearm::REARM;
    }
};

int main(int argc, char **argv)
{
    // NEventLoop * eloop = &getSystemLoop();
    EventLoop<std::mutex> * eloop = new EventLoop<std::mutex>();
    
    MySignalWatcher mse1, mse2;
    mse1.registerWatch(eloop, SIGUSR1);
    mse2.registerWatch(eloop, SIGUSR2);

    // block USR1 / USR2 reception    
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigaddset(&set, SIGUSR2);    
    sigprocmask(SIG_BLOCK, &set, NULL);

    //raise(SIGUSR1);
    //raise(SIGUSR2);
    
    sleep(1);
    
    std::cout << "Running eloop..." << std::endl;
    eloop->run();
    //std::cout << "Running eloop..." << std::endl;
    eloop->run();
    //std::cout << "Running eloop..." << std::endl;
    eloop->run();
    
    return 0;
}

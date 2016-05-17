#include <iostream>
#include <unistd.h>
#include <mutex>
#include "dasync.h"
#include "dmutex.h"

using namespace dasync;

class MySignalWatcher : public PosixSignalWatcher<std::mutex>
{
    void gotSignal(int signo, SigInfo siginfo)
    {
        using namespace std;
        cout << "Got signal: " << signo << endl;
    }
};

int main(int argc, char **argv)
{
    // NEventLoop * eloop = &getSystemLoop();
    EventLoop<std::mutex> * eloop = new EventLoop<std::mutex>();
    
    MySignalWatcher mse1, mse2;
    mse1.registerWith(eloop, SIGUSR1);
    mse2.registerWith(eloop, SIGUSR2);

    // block USR1 / USR2 reception    
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigaddset(&set, SIGUSR2);    
    sigprocmask(SIG_BLOCK, &set, NULL);

    //raise(SIGUSR1);
    //raise(SIGUSR2);
    
    sleep(1);
    
    eloop->run();
    
    return 0;
}

#include <csignal>
#include <sys/epoll.h>
#include <sys/signalfd.h>


namespace org_davmac {

class EpollMechanism
{
    private:
    int epollFd;
    int signalFd;
    
    public:
    void addFiledescriptor(int fd, int eventMask, void *data); // edge triggered!
    void removeFiledescriptor(int fd);
    void addSignal(int signum, void *data);
    void removeSignal(int signum);
    
    EpollMechanism()
    {
        // open epoll
        // open signal fd
    }
    
    template <class T> void getEvents(T)
    {
        // epoll wait (some arbitrary number of events)
        // process each with T::processEvent()
        // stop if that returns false
    }
    
    rearmFileDescriptor(int fd, int eventMask)
    {
    
    }
};

} // end namespace


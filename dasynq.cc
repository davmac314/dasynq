#include "dasynq.h"


namespace dasynq {

static TEventLoop sysEventLoop;

TEventLoop & getSystemLoop()
{
    return sysEventLoop;
}

// Instantiate templates:
template class EventLoop<NullMutex>;
template class EventLoop<DMutex>;


}

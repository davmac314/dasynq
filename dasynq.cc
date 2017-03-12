#include "dasynq.h"


namespace dasynq {

static TEventLoop sysEventLoop;

TEventLoop & getSystemLoop()
{
    return sysEventLoop;
}

// Instantiate templates:
template class event_loop<NullMutex>;
template class event_loop<DMutex>;


}

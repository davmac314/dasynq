This directory contains programs used to benchmark performance of Dasynq compared to
another event-loop library, Libev.


## The benchmark

The benchmark sets up a number of pipes or paired sockets, with event watchers on each; it then
makes a number of the watchers active by writing single bytes of data to a number of pipes.
The event handlers consume the written byte and write another byte, to maintain a constant
number of active events, until all writes have completed. Optionally, it sets up (or resets)
timers for each write.

The original benchmarkmark for Libev can be found at:
http://libev.schmorp.de/bench.c

The original program, which was used to benchmark Libev versus Libevent, suffered
several problems:

 * Calls event_del(...) on an uninitialized event object (line 154; the object will be
   initialised on the second pass, but not the first). This has been fixed. Watcher
   removal has also been added to the end of the benchmark 'run_once' method, after
   timing has finished.   
 * Runs the benchmark twice (loop at line 266) and counts the result from the second run.
   Because libev "caches" watches in the epoll backend and because the pipes used for the
   test are not closed between iterations, the second run is much faster when using Libev.
   I do not believe this is representative of a real-world usage pattern.

The benchmarks have been modified to address these issues.


## Running the benchmark

Benchmark arguments;

 * -n **num**  :   set the number of pipes and associated event watches (suggest 1000)
 * -a **num**  :   set the number of watches to be made active at a time (suggest 200)
 * -w **num**  :   set the total number of writes to be issued (suggest 10000)
 * -l **num**  :   limit the number of pending events to process between polling the
                   event notification mechanism (Dasynq only; default is -1, i.e unlimited).
 * -t          :   enable timers
 * -p          :   set random priorities on event watchers (rather than using the same
                   priority for all watchers)
 * -e          :   use native libev API instead of libevent API (seemingly broken?)

## Results
 
The results are in the format "total-time  loop-time", where total-time includes the setup time
required to enable the event watchers. There are two runs, so there are two sets of values
printed (one per line).

For 1000 pipes, with 200 active, 10000 writes, and timers enabled, typical values are:
 
 * evbench:  11005    10605
 * dbench:   12568    11981
 
That is to say, the performance is essentially identical. With timers disabled, the results are
not much different:

 * evbench:   9608     9208
 * dbench:   10197     9783

On increasing the number of active pipes to 800, however, Libev pulls ahead somewhat:

 * evbench:   7290     6585
 * dbench:    9670     8914
 
Astute readers will notice that both event loops perform **better** with a higher number of active
pipes; most likely, this is because fewer total polls are required (since each poll should return
all active events). 

If we enable different priorities as well as timers, it appears that Libev gains the upper hand by
an even more significant margin:

 * evbench:  10071     9337
 * dbench:   12810    11964

However, we can make the footings even again by limiting the number of events processed per poll
iteration by Dasynq (note that the total number of events processed remains the same). Using
`-l 16` argument, we get the following:

 * dbench:   10308     9401

Limiting events processed between polls this way means that high-priority events are able to starve
low priority events, that is, priority is actually priority, and not just a simple ordering of
processing between polls. While limiting the processing of events means that the notification
mechanism must be polled more times to retrieve the same number of events, it seems that in this
benchmark the locality benefits outweigh this cost.

While in general Libev appears slightly faster, it is important to realise that the robustness
guarantees that Dasynq provides come with a small performance cost. Dasynq also provides true
"hard" priority support, as discussed just prior, and extends this to multi-threaded programs
with an API that is natively thread-safe.

Finally, Dasynq happily allows the full range of priority values whereas Libev typically supports
only 5 (from -2 to +2, configurable at compile time, where a higher valid priority range will
reduce performance due to a linear sweep of the pending event arrays). The above tests have Libev
configured for 1000 different priority levels.

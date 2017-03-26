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

## Running the benchmark

Benchmark arguments;

 * -n <num>  :   set the number of pipes and associated event watches (suggest 1000)
 * -a <num>  :   set the number of watches to be made active at a time (suggest 200)
 * -w <num>  :   set the total number of writes to be issued (suggest 10000)
 * -t        :   enable timers
 * -p        :   set random priorities on event watchers (rather than using the same
                 priority for all watchers)
 * -e        :   use native libev API instead of libevent API (seemingly broken?)

## Results
 
The results are in the format "total-time  loop-time", where total-time includes the setup time
required to enable the event watchers. There are two runs, so there are two sets of values
printed (one per line).

For 1000 pipes, with 200 active, 10000 writes, and timers enabled, typical values are:
 
 * evbench:  11075    10572
 * dbench:   11221    10657
 
That is to say, the performance is essentially identical. With timers disabled, Libevent pulls
slighly ahead:

 * evbench:  9084     8581
 * dbench:   9748     9385

However, it is important to realise that the robustness guarantees that Dasynq provides come
with a small performance cost.

Furthermore, Dasynq's priority queue ensures stability, preventing starvation of any watcher
(where the event is active but not serviced as other watchers consistently get serviced before
it). Libev calls event watchers in reverse order and so can only prevent starvation by processing
all queued events before polling for more events; this is its normal mode of operation, however,
it prevents higher priority events from being polled before lower priority events are serviced
unless you call the loop run routine recursively (in which case there is risk of starvation).

Finally, Dasynq happily allows the full range of priority values whereas Libev typically supports
only 5 (from -2 to +2, configurable at compile time, where a higher valid priority range will
reduce performance due to a linear sweep of the pending event arrays).

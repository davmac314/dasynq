# Heaptest

This directory contains a number of priority queue implementations (which
are not necessarily fully production-ready), and a program (heaptest.cc) that
exercises them in various ways, which I have used for benchmarking.

Note that these implementations are designed to allow fast removal or re-
prioritisation of arbitrary elements; specifically, adding an item into the
queue yields a "handle" which can be used to remove or modify the item later.
The results here may not reflect those of well-written implementations which
do away with this requirement.

The queue implementations are:

 * **BinaryHeap**, a traditional binary heap implementation.
 * **NaryHeap**, an N-ary heap which organises as a heap of fixed-size binary
   heaps. It is designed to offer better cache locality than BinaryHeap.
 * **PairingHeap**, a more complex (but still relatively simple) heap
   implementation with theoretically better bounds on operation times
   than a binary heap
 * **BTreeQueue**, an in-memory B-Tree implementation.

The BinaryHeap, NaryHeap and PairingHeap are not stable - insertion order for
elements with different priority is not preserved. There is a StableQueue
template wrapper which creates a stable priority queue from an unstable queue
by adding an insertion "time" (an unsigned long long value which is
incremented on each insertion; a 64-bit value will not wrap for over 113 years
even if incremented every processor cycle on a 5GHz processor, so this seems
quite safe).

The benchmark program performs several different types of test, but it's
important to consider use cases. For an asynchronous event library, queue
stability seems important to avoid starvation. Also consider that:

 * The most common removal operation is likely "delete-min", i.e. retrieve
   and remove the head of the queue. This is best represented in the
   "ordered fill/dequeue", "random fill/dequeue", "cycle fill/dequeue"
   and "flat priority fill/dequeue" tests; these are used for evaluation.
 * It will be common enough that all events are assigned the same priority
   (or one of a handful of different priority values). This is best
   simulated with the "flat priority fill/dequeue" test.
 * Another use case is when differing priority levels are utilised: that is
   best simulated perhaps by the "random fill/dequeue" test.

The results are somewhat surprising (run on my personal desktop, compiled
with -O3):


|                |   (A) |   (B) |   (C) |   (D) |
| -------------- | ----- | ----- | ----- | ----- |
| Ordered fill   |  1549 |  1243 |   788 |   802 |
| Random f/dq    | 11834 |  6666 | 11672 |  4632 |
| Cycle f/dq     |   579 |   813 |   179 |   458 |
| Flat pri f/dq  |  1593 |  1235 |   708 |   213 |

 * (A) Stable BinaryHeap
 * (B) Stable NaryHeap (N=16)
 * (C) Stable PairingHeap
 * (D) BTreeQueue (naturally stable)


Note that the stable BinaryHeap is by far the worst general performer, although
it slightly outperforms the stable NaryHeap in one test. In general, NaryHeap
performs better, and significantly so in the random fill/dequeue test. The
stable PairingHeap performs well, but not as well as the NaryHeap in the random
fill/dequeue test. 

Against expectations, the BTreeQueue performs quite well - it falls behind the
PairingHeap in only one of the above four tests, and beats every other algorithm
in each of the other listed tests. Importantly, for the "random fill/dequeue"
test - which arguably best represents workloads using a wide range of priority
levels - it is more than twice as fast as the PairingHeap.

The good performance of the BTreeQueue can I think be attributed to the fact that
it naturally maintains good cache locality.

For unstable heaps, the results are as follows:

|                |   (A) |   (B) |   (C) |   (D) |
| -------------- | ----- | ----- | ----- | ----- |
| Ordered fill   |  1329 |  1041 |   569 |   802 |
| Random f/dq    |  8924 |  5659 | 10989 |  4632 |
| Cycle f/dq     |   525 |   720 |   171 |   458 |
| Flat pri f/dq  |   203 |   213 |    73 |   213 |

 * (A) BinaryHeap
 * (B) NaryHeap (N=16)
 * (C) PairingHeap
 * (D) BTreeQueue (stable)

Surprisingly enough, the BTreeQueue remains one of the most performant data
structures to implement a priority queue even when stability is not required.

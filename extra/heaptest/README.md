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
 * **DaryHeap**, a D-ary heap (a generalised binary heap for N>2). This should
   offer better cache locality than a regular BinaryHeap, and requires less
   handle updates than either a BinaryHeap or NaryHeap, but has some overhead
   in some operations.
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
 * Removal of elements which are not at the root of the heap potentially
   has a significantly different cost than removal of the root element;
   furthermore, adjustment of element priority can typically be achieved
   by removal and re-insertion, so the removal cost is important. It is
   best represented by the "random fill/random remove" test.
 * It will be common enough that all events are assigned the same priority
   (or one of a handful of different priority values). This is best
   simulated with the "flat priority fill/dequeue" test.
 * Another use case is when differing priority levels are utilised: that is
   best simulated perhaps by the "random fill/dequeue" test.

The results are as follows (run on my personal desktop, compiled with -O3):

|                |   (A) |   (B) |   (C) |   (D) |   (E) |
| -------------- | ----- | ----- | ----- | ----- | ----- |
| Ordered fill   |  1265 |  1035 |   973 |   788 |   802 |
| Random f/dq    |  8829 |  2803 |  5278 | 11672 |  4632 |
| Random f/r rm  |  3362 |  2611 |  2628 |  2029 |  6167 |
| Cycle f/dq     |   452 |   606 |   406 |   179 |   458 |
| Flat pri f/dq  |  1395 |  1111 |   973 |   708 |   213 |

 * (A) Stable BinaryHeap
 * (B) Stable NaryHeap (N=16)
 * (C) Stable DaryHeap (N=4)
 * (D) Stable PairingHeap
 * (E) BTreeQueue (naturally stable)


Note that the stable BinaryHeap is generally the worst performer, although it
beats the PairingHeap in the important "random fill/dequeue" test and edges
out the NaryHeap in the "cyclic fill/dequeue" test. It is beaten in all tests
by the the D-ary heap, however.

Against expectations, the BTreeQueue performs quite well, coming first or
second in 4 out of 5 tests. Its "flat priority" performance clearly outperforms
the heap-based queues (probably because it essentially degenerates to a linked-
list for this usage pattern, which is quite efficient). However, its "random
remove" time is somewhat high and it is handily beaten by the N-ary heap in the
important "random fill/dequeue" test.

For unstable heaps, the results are as follows:

|                |   (A) |   (B) |   (C) |   (D) |   (E) |
| -------------- | ----- | ----- | ----- | ----- | ----- |
| Ordered fill   |  1031 |   901 |   926 |   569 |   802 |
| Random f/dq    |  6863 |  2419 |  4082 | 10989 |  4632 |
| Random f/rr    |  2881 |  2450 |  2465 |  1834 |  6167 |
| Cycle f/dq     |   446 |   552 |   517 |   171 |   458 |
| Flat pri f/dq  |   203 |   210 |   236 |    73 |   213 |

 * (A) BinaryHeap
 * (B) NaryHeap (N=16)
 * (C) DaryHeap (N=4)
 * (D) PairingHeap
 * (E) BTreeQueue (stable)


When stability is not required, the NaryHeap contends for the pole position,
though it does win every test.

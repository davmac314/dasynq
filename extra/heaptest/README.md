# Heaptest

This directory contains a number of priority queue implementations (which
are not necessarily fully production-ready), and a program (heaptest.cc) that
exercises them in various ways, which I have used for benchmarking.

Note that these implementations are designed to allow fast removal or re-
prioritisation of arbitrary elements; specifically, adding an item into the
queue yields a "handle" which can be used to remove or modify the item later.
The results here may not reflect those of well-written implementations which
do away with this requirement.

The queue implementations are (please forgive naming inconsistency):

 * **BinaryHeap**, a traditional binary heap implementation.
 * **nary_heap**, an N-ary heap which organises as a heap of fixed-size binary
   heaps. It is designed to offer better cache locality than BinaryHeap.
 * **DaryHeap**, a D-ary heap (a generalised binary heap for N>2). This should
   offer better cache locality than a regular BinaryHeap, and requires less
   handle updates than either a BinaryHeap or nary_heap, but has some overhead
   in some operations.
 * **PairingHeap**, a more complex (but still relatively simple) heap
   implementation with theoretically better bounds on operation times
   than a binary heap
 * **btree_queue**, an in-memory B-Tree implementation.

The BinaryHeap, nary_heap, DaryHeap and PairingHeap are not stable - insertion order
for elements with different priority is not preserved. There is a StableQueue
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
| Ordered fill   |  1224 |   880 |   887 |   734 |   787 |
| Random f/dq    |  4438 |  4114 |  3491 |  9529 |  4515 |
| Random f/r rm  |  1172 |  1331 |   951 |   954 |  5655 |
| Cycle f/dq     |   418 |   716 |   426 |   170 |   503 |
| Flat pri f/dq  |  1562 |  1427 |  1591 |   696 |   178 |

 * (A) Stable BinaryHeap
 * (B) Stable nary_heap (N=16)
 * (C) Stable DaryHeap (N=4)
 * (D) Stable PairingHeap
 * (E) BTreeQueue (naturally stable)


Note that the stable BinaryHeap is generally the worst performer, although it
beats the PairingHeap in the important "random fill/dequeue" test and edges
out the nary_heap in the "cyclic fill/dequeue" test. It is beaten in nearly all
tests by the the D-ary heap, however, and in most by the nary_heap.

Against expectations, the btree_queue performs quite well, coming first or
second in 4 out of 5 tests. Its "flat priority" performance clearly outperforms
the heap-based queues (probably because it essentially degenerates to a linked-
list for this usage pattern, which is quite efficient). However, its "random
remove" time is somewhat high and it is handily beaten by the D-ary heap in the
important "random fill/dequeue" test.

For unstable heaps, the results are as follows:

|                |   (A) |   (B) |   (C) |   (D) |   (E) |
| -------------- | ----- | ----- | ----- | ----- | ----- |
| Ordered fill   |   983 |   917 |   754 |   538 |   787 |
| Random f/dq    |  3469 |  3638 |  3143 |  9174 |  4515 |
| Random f/rr    |  1064 |  1183 |   869 |   770 |  5655 |
| Cycle f/dq     |   456 |   775 |   407 |   169 |   503 |
| Flat pri f/dq  |   155 |   165 |   167 |    74 |   178 |

 * (A) BinaryHeap
 * (B) nary_heap (N=16)
 * (C) DaryHeap (N=4)
 * (D) PairingHeap
 * (E) BTreeQueue (stable)


When stability is not required, the DaryHeap contends for the pole position,
though only due to the PairingHeap's time for the important Random fill/dequeue
test. Otherwise, the PairingHeap performs best in every test.

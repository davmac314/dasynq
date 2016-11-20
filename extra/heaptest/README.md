# Heaptest

This directory contains a number of priority queue implementations (which
are not necessarily fully production-ready), and a program (heaptest.cc) that
exercises them in various ways, which I have used for benchmarking.

The queue implementations are:

 * **BinaryHeap**, a traditional binary heap implementation.
 * **PairingHeap**, a more complex (but still relatively simple) heap
   implementation with theoretically better bounds on operation times
   than a binary heap
 * **BTreeQueue**, an in-memory B-Tree implementation.

The BinaryHeao and PairingHeap are not stable - insertion order for elements
with different priority is not preserved. There is a StableQueue template
wrapper which creates a stable priority queue from an unstable queue by
adding an insertion "time" (an unsigned long long value which is incremented
on each insertion; a 64-bit value will not wrap for over 113 years even if
incremented every processor cycle on a 5GHz processor, so this seems quite
safe).

The benchmark program performs several different types of test, but it's
important to consider use cases. For an asynchronous event library, queue
stability seems important to avoid starvation. Also consider that:

 * The most common removal operation is likely "delete-min", i.e. retrieve
   and remove the head of the queue. This is best represented in the
   "ordered fill/dequeue", "random fill/dequeue" and "cycle fill/dequeue"
   tests.
 * It will be common enough that all events are assigned the same priority
   (or one of a handful of different priority values). This is best
   simulated with the "flat priority fill/dequeue" test.

The results are somewhat surprising (run on my personal desktop, compiled
with -O3):


|                | Stable BinaryHeap | Stable PairingHeap | BTreeQueue (stable) |
| -------------- | ----------------- | ------------------ | ------------------- |
| Ordered fill   |              1905 |                590 |                 869 |
| Random f/dq    |              6381 |               8790 |                3792 |
| Cycle f/dq     |               854 |                116 |                 487 |
| Flat pri f/dq  |              2099 |                506 |                 180 |


Note that the stable BinaryHeap is by far the worst general performer, although
it slightly outperforms the stable PairingHeap in one test. This is somewhat in
contrast to the results when an unstable BinaryHeap is used (not shown above).

The PairingHeap in general performs well, though it is fantastically worse in
the "random fill/dequeue" test (I do not understand why this is the case, but note
that the tree structure formed by a random fill will naturally be very different
to that formed by an ordered fill).

Against expectations, the BTreeQueue performs quite well - it falls behind the
PairingHeap in two of the above four tests, but these are the relatively "cheap"
tests anyway. Importantly, for the "random fill/dequeue" test - which arguably
best represents workloads using a wide range of priority levels - it is more than
twice as fast as the PairingHeap.

The good performance of the BTreeQueue can I think be attributed to the fact that
it naturally maintains good cache locality. It remains to be seen if the
implementations of the other two data structures can be altered to improve cache
locality and whether this will yield a significant performance benefit.


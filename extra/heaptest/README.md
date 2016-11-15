= Heaptest =

This is just a small heap benchmark program that I used to test a couple of
different priority queue (heap) implementations. There is a BinaryHeap,
which is implemented using an array-based classical heap, and PairingHeap,
which implements the "Pairing heap" algorithm.

A Pairing Heap has theoretically better asymptotic bounds on operations - in
particular, insert is O(1) instead of O(log n) - however this makes little
difference in practice; in particular, insert operations are in general
necessarily paired with remove operations which are still roughly O(log n),
and the constant time factor may be larger.

The benchmark shows better performance by the BinaryHeap in some tests and
by the PairingHeap in others, though for very large heaps PairingHeap does
tend to pull ahead in most tests. Also, the PairingHeap performs well in the
"cycle" test, which is supposed to be an emulation of typical usage pattern
(where eg. a number of timers with similar period expire at various
offsets).

Remember to compile with -O3.

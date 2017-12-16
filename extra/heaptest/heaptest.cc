#include "dasynq-stableheap.h"
#include "dasynq-btreequeue.h"
#include "dasynq-pairingheap.h"
#include "dasynq-binaryheap.h"
#include "dasynq-naryheap.h"
#include "dasynq-daryheap.h"
#include <functional>
#include <random>
#include <chrono>
#include <algorithm>

#include <iostream>

template <typename A, typename B, typename C> using Nary = dasynq::nary_heap<A,B,C, 16>;
template <typename A, typename B, typename C> using Dary = dasynq::DaryHeap<A,B,C, 4>;

int main(int argc, char **argv)
{
    // Template arguments are: data type, priority type, comparator
    // dasynq::BinaryHeap<int, int, std::less<int>> heap;
    // dasynq::nary_heap<int, int> heap;
    // dasynq::DaryHeap<int, int, std::less<int>, 4> heap;
    // dasynq::PairingHeap<int, int> heap;
    // dasynq::btree_queue<int, int, std::less<int>, 16> heap;
    
    // StableHeap<Dary, int, int> heap;
    // StableHeap<dasynq::BinaryHeap, int, int> heap;
    // StableHeap<Nary, int, int> heap;
    StableHeap<dasynq::PairingHeap, int, int> heap;
    
    constexpr int NUM = 10000000;
    // constexpr int NUM = 5;
    // constexpr int NUM = 100000;
    
    std::mt19937 gen(0);
    std::uniform_int_distribution<> r(0, NUM);
    
    // Ordered fill/dequeue
    
    auto starttime = std::chrono::high_resolution_clock::now();
    
    using handle_t = decltype(heap)::handle_t;
    using handle_t_r = decltype(heap)::handle_t_r;
    
    handle_t *indexes = new handle_t[NUM];
    for (int i = 0; i < NUM; i++) {
        heap.allocate(indexes[i], i);
        heap.insert(indexes[i], i);
    }
    
    for (int i = 0; i < NUM; i++) {
        handle_t_r r = heap.get_root();
        // std::cout << heap.get_data(r) << std::endl;
        heap.pull_root();
        heap.deallocate(r);
    }
    
    auto endtime = std::chrono::high_resolution_clock::now();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(endtime - starttime).count();
    
    std::cout << "Ordered fill/dequeue: " << millis << std::endl;
    
    if (! heap.empty()) abort();

    // Flat priority fill/dequeue
    
    starttime = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NUM; i++) {
        heap.allocate(indexes[i], i);
        heap.insert(indexes[i], 0);
    }
    
    for (int i = 0; i < NUM; i++) {
        handle_t_r r = heap.get_root();
        // std::cout << heap.get_data(r) << std::endl;
        heap.pull_root();
        heap.deallocate(r);
    }
    
    endtime = std::chrono::high_resolution_clock::now();
    millis = std::chrono::duration_cast<std::chrono::milliseconds>(endtime - starttime).count();
    
    std::cout << "Flat priority fill/dequeue: " << millis << std::endl;

    if (! heap.empty()) abort();
    
    // Random fill/dequeue
    
    starttime = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NUM; i++) {
        int ii = r(gen);
        heap.allocate(indexes[i], ii);
        heap.insert(indexes[i], ii);
    }
    
    for (int i = 0; i < NUM; i++) {
        handle_t_r r = heap.get_root();
        // std::cout << heap.get_data(r) << std::endl;
        heap.pull_root();
        heap.deallocate(r);
    }
    
    endtime = std::chrono::high_resolution_clock::now();
    millis = std::chrono::duration_cast<std::chrono::milliseconds>(endtime - starttime).count();
    
    std::cout << "Random fill/dequeue: " << millis << std::endl;

    if (! heap.empty()) abort();

    // Random fill/random remove

    int * order = new int[NUM];
    for (int i = 0; i < NUM; i++) order[i] = i;
    std::shuffle(order, order + NUM, gen);

    starttime = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM; i++) {
        int ii = r(gen);
        heap.allocate(indexes[i], ii);
        heap.insert(indexes[i], ii);
    }

    for (int i = 0; i < NUM; i++) {
        heap.remove(indexes[order[i]]);
        heap.deallocate(indexes[order[i]]);
    }

    endtime = std::chrono::high_resolution_clock::now();
    millis = std::chrono::duration_cast<std::chrono::milliseconds>(endtime - starttime).count();

    std::cout << "Random fill/random remove: " << millis << std::endl;

    if (! heap.empty()) abort();
    
    // Queue cycle
    
    starttime = std::chrono::high_resolution_clock::now();
    
    int active = 0;
    for (int i = 0; i < NUM; i++) {
        int ii = r(gen);
        heap.allocate(indexes[i], ii);
        heap.insert(indexes[i], ii);
        active++;
        
        if (active > 1000) {
            handle_t_r r = heap.get_root();
            heap.pull_root();
            heap.deallocate(r);
            active--;
        }
    }
    
    for (int i = 0; i < std::min(1000, NUM); i++) {
        handle_t_r r = heap.get_root();
        heap.pull_root();
        heap.deallocate(r);
    }

    endtime = std::chrono::high_resolution_clock::now();
    millis = std::chrono::duration_cast<std::chrono::milliseconds>(endtime - starttime).count();
    
    std::cout << "Cycle fill/dequeue: " << millis << std::endl;

    if (! heap.empty()) abort();
    
    // Ordered fill/random remove

    for (int i = 0; i < NUM; i++) order[i] = i;
    std::shuffle(order, order + NUM, gen);
    
    starttime = std::chrono::high_resolution_clock::now();
        
    for (int i = 0; i < NUM; i++) {
        heap.allocate(indexes[i], i);
        heap.insert(indexes[i], i);
    }
    
    // std::shuffle(indexes, indexes + NUM, gen);
    
    for (int i = 0; i < NUM; i++) {
        heap.remove(indexes[order[i]]);
        heap.deallocate(indexes[order[i]]);
    }    

    endtime = std::chrono::high_resolution_clock::now();
    millis = std::chrono::duration_cast<std::chrono::milliseconds>(endtime - starttime).count();
    
    std::cout << "Ordered fill/random remove: " << millis << std::endl;
    
    delete[] order;
    if (! heap.empty()) abort();

    // Pathological fill/remove

    starttime = std::chrono::high_resolution_clock::now();
        
    for (int i = 0; i < NUM; i++) {
        heap.allocate(indexes[i], i);
        heap.insert(indexes[i], i);
    }
        
    for (int i = 1; i < NUM; i++) {
        heap.remove(indexes[i]);
        heap.deallocate(indexes[i]);
    }
    
    heap.remove(indexes[0]);
    heap.deallocate(indexes[0]);

    endtime = std::chrono::high_resolution_clock::now();
    millis = std::chrono::duration_cast<std::chrono::milliseconds>(endtime - starttime).count();
    
    std::cout << "Pathological fill/remove: " << millis << std::endl;

    if (! heap.empty()) abort();

    return 0;
}

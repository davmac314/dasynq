#ifndef DASYNC_BINARYHEAP_H_INCLUDED
#define DASYNC_BINARYHEAP_H_INCLUDED

#include <vector>
#include <type_traits>
#include <functional>

namespace dasynq {

/**
 * Priority queue implementation based on a binary heap. In testing this turned out to be faster than
 * using more advanced data structures with theoretically better bounds for operations.
 *
 * The queue works by maintaing two vectors: one is a backing vector which stores node data, the other
 * is the heap vector which organises the nodes by priority.
 *
 * T : node data type
 * P : priority type (eg int)
 * Compare : functional object type to compare priorities
 */
template <typename T, typename P, typename Compare = std::less<P>>
class BinaryHeap
{
    // The backing vector has a union element type, containing either a data item (P) or a
    // free node. To simplify implementation we require that P trivally copyable, otherwise
    // we would need to add a boolean to track whether the node is free/full. We static_assert
    // now to avoid a very confusing compilation bug later.
    // (TODO: partial specialisation to support non-trivially-copyables; we actually don't need
    //  to add a boolean, can use heap_index == -2 or somesuch).
    static_assert(std::is_trivially_copy_constructible<T>::value, "P must be trivially copy constructible");
    
    public:
    // Handle to an element on the heap in the node buffer.
    using handle_t = int;
    
    static void init_handle(handle_t &h)
    {
        h = -1;
    }
    
    private:

    union DataNodeU
    {
        struct {
            T hd;
            int heap_index;
        };
        
        int next_free;
        
        template <typename ...U> DataNodeU(U... u) : hd(u...)
        {
            heap_index = -1;
        }
        
        ~DataNodeU() { }
    };

    class HeapNode
    {
        public:
        P data;
        int data_index;
        
        HeapNode(int d_index, const P &odata) : data(odata), data_index(d_index)
        {
            // nothing to do
        }
    };
    
    std::vector<DataNodeU> bvec;
    std::vector<HeapNode> hvec;
    
    int first_free = -1;
    int root_node = -1;
    typename std::vector<HeapNode>::size_type num_nodes = 0;
    
    bool bubble_down()
    {
        return bubble_down(hvec.size() - 1);
    }
    
    // Bubble a newly added timer down to the correct position
    bool bubble_down(int pos)
    {
        // int pos = v.size() - 1;
        Compare lt;
        while (pos > 0) {
            int parent = (pos - 1) / 2;
            if (! lt(hvec[pos].data, hvec[parent].data)) {
                break;
            }
            
            std::swap(hvec[pos], hvec[parent]);
            std::swap(bvec[hvec[pos].data_index].heap_index, bvec[hvec[parent].data_index].heap_index);
            pos = parent;
        }
        
        return pos == 0;
    }
    
    void bubble_up(int pos = 0)
    {
        Compare lt;
        int rmax = hvec.size();
        int max = (rmax - 1) / 2;

        while (pos <= max) {
            int selchild;
            int lchild = pos * 2 + 1;
            int rchild = lchild + 1;
            if (rchild >= rmax) {
                selchild = lchild;
            }
            else {
                // select the sooner of lchild and rchild
                selchild = lt(hvec[lchild].data, hvec[rchild].data) ? lchild : rchild;
            }
            
            if (! lt(hvec[selchild].data, hvec[pos].data)) {
                break;
            }
            
            std::swap(bvec[hvec[selchild].data_index].heap_index, bvec[hvec[pos].data_index].heap_index);
            std::swap(hvec[selchild], hvec[pos]);
            pos = selchild;
        }
    }

    template <typename ...U> handle_t alloc_slot(U... u)
    {
        // Make sure the heap vector has suitable capacity
        if (hvec.capacity() <= num_nodes) {
            hvec.reserve(num_nodes * 2);
        }
        
        num_nodes++;
        
        if (first_free != -1) {
            int r = first_free;
            first_free = bvec[r].next_free;
            return r;
        }
        else {
            int r = bvec.size();
            bvec.emplace_back(u...);
            return r;
        }
    }

    void remove_h(handle_t hidx)
    {
        bvec[hvec[hidx].data_index].heap_index = -1;
        if (hvec.size() != hidx + 1) {
            // replace the first element with the last:
            bvec[hvec.back().data_index].heap_index = hidx;
            hvec[hidx] = hvec.back();
            hvec.pop_back();
            
            // Now bubble up:
            bubble_up(hidx);
        }
        else {
            hvec.pop_back();
        }
    }
    
    public:
    
    T & node_data(handle_t index) noexcept
    {
        return bvec[index].hd;
    }
    
    // Allocate a slot, but do not incorporate into the heap:
    //  u... : parameters for data constructor T::T(...)
    template <typename ...U> void allocate(handle_t & hnd, U... u)
    {
        int r = alloc_slot(u...);
        hnd = r;
    }
    
    // Deallocate a slot
    void deallocate(handle_t index)
    {
        bvec[index].hd.T::~T();
        if (index == bvec.size() - 1) {
            bvec.pop_back();
            if (bvec.size() * 2 == bvec.capacity()) {
                bvec.shrink_to_fit();
            }
        }
        else {
            bvec[index].next_free = first_free;
            first_free = index;
        }
        num_nodes--;
        
        // TODO we should shrink the capacity of hvec if num_nodes is sufficiently
        // less than its current capacity, however, there is no way with a standard
        // vector to shrink capacity to an arbitrary amount. :/
    }

    bool insert(handle_t index, P pval = P())
    {
        bvec[index].heap_index = hvec.size();
        hvec.emplace_back(index, pval);
        return bubble_down();
    }
    
    // Get the root node handle. (For interoperability, callers should assume that this may return a
    // handle_t reference).
    handle_t get_root()
    {
        return hvec[0].data_index;
    }
    
    P &get_root_priority()
    {
        return hvec[0].data;
    }
    
    void pull_root()
    {
        remove_h(0);
    }
    
    void remove(handle_t index)
    {
        remove_h(bvec[index].heap_index);
    }
    
    bool empty()
    {
        return hvec.empty();
    }
    
    bool is_queued(handle_t index)
    {
        return bvec[index].heap_index != -1;
    }
    
    // Set a node priority. Returns true iff the node becomes the root node (and wasn't before).
    bool set_priority(handle_t index, P& p)
    {
        handle_t heap_index = bvec[index].heap_index;
        
        Compare lt;
        if (lt(hvec[heap_index].data, p)) {
            // Increase key
            hvec[heap_index].data = p;
            bubble_up(heap_index);
            return false;
        }
        else {
            // Decrease key
            hvec[heap_index].data = p;
            return bubble_down(heap_index);
        }
    }
    
    /*
    void dump()
    {
        using namespace std;
        cout << "Heap size: " << hvec.size() << endl;
        for (int i = 0; i < hvec.size(); i++) {
            cout << "  Heap idx# " << i << " data_index=" << hvec[i].data_index << " value=" << hvec[i].data << endl;
        }
    
    }
    */
};

}

#endif

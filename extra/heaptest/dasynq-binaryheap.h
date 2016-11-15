#include <iostream> // DAV

namespace dasynq {

template <typename T, typename Compare>
class BinaryHeap
{
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
    };

    class HeapNode
    {
        public:
        T data;
        int data_index;
        
        HeapNode(int d_index, const T &odata) : data(odata), data_index(d_index)
        {
        
        }
    };
    
    std::vector<DataNodeU> bvec;
    std::vector<HeapNode> hvec;
    
    int first_free = -1;
    int root_node = -1;
    int num_nodes = 0;
    
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
    

    public:
    
    int alloc_slot()
    {
        num_nodes++;
        if (hvec.capacity() < num_nodes) {
            hvec.reserve(num_nodes * 2);
        }
        
        if (first_free != -1) {
            int r = first_free;
            first_free = bvec[r].next_free;
            return r;
        }
        else {
            int r = bvec.size();
            bvec.emplace_back();
            return r;
        }
    }
    
    T & get_data(int index)
    {
        return bvec[index].hn.data;
    }
    
    // Allocate a slot, but do not incorporate into the heap:
    template <typename ...U> int allocate(U... u)
    {
        int r = alloc_slot();
        new (& bvec[r].hd) HeapNode(r, u...);
        return r;
    }
    
    void deallocate(int index)
    {
        bvec[index].hd.T::~T();
        if (index == bvec.size() - 1) {
            bvec.pop_back();
        }
        else {
            bvec[index].next_free = first_free;
            first_free = index;
        }
        num_nodes--;
    }

    bool insert(int index)
    {
        bvec[index].heap_index = hvec.size();
        hvec.emplace_back(index, bvec[index].hd);
        return bubble_down();
    }
    
    int get_root()
    {
        return hvec[0].data_index;
    }
    
    void pull_root()
    {
        remove_h(0);
        /*
        if (hvec.size() > 1) {
            // replace the first element with the last:
            bvec[hvec[0].data_index].heap_index = -1;
            bvec[hvec.back().data_index].heap_index = 0;
            hvec[0] = hvec.back();
            hvec.pop_back();
            
            // Now bubble up:
            bubble_up();
        }
        else {
            hvec.pop_back();
        }
        */
    }
    
    void remove(int index)
    {
        remove_h(bvec[index].heap_index);
    }
    
    void remove_h(int hidx)
    {
        bvec[hvec[hidx].data_index].heap_index = -1;
        if (hvec.size() > 1) {
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
    
    void dump()
    {
        using namespace std;
        cout << "Heap size: " << hvec.size() << endl;
        for (int i = 0; i < hvec.size(); i++) {
            cout << "  Heap idx# " << i << " data_index=" << hvec[i].data_index << " value=" << hvec[i].data << endl;
        }
    
    }
};

}

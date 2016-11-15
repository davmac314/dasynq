#include <vector>

#include <iostream> // DAV

namespace dasynq {

template <typename T, typename Compare>
class PairingHeap
{
    struct HeapNode
    {
        T data;
        int next_sibling;
        int prev_sibling; // (or parent)
        int first_child;
        
        template <typename ...U> HeapNode(U... u) : data(u...)
        {
            next_sibling = -1;
            prev_sibling = -1;
            first_child = -1;
        }
    };

    // A slot in the backing vector can be free, or occupied. We use a union to cover
    // these cases; if the slot is free, it just contains a link to the next free slot.
    union HeapNodeU
    {
        HeapNode hn;
        int next_free;
        
        HeapNodeU() { }
    };
    
    std::vector<HeapNodeU> bvec;
    
    int first_free = -1;
    int root_node = -1;
    
    int alloc_slot()
    {
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
    
    bool merge(int index)
    {
        if (root_node == -1) {
            root_node = index;
            return true;
        }
        else {
            Compare is_less;
            if (is_less(bvec[root_node].hn.data, bvec[index].hn.data)) {
                // inserted node becomes new subtree
                int root_fc = bvec[root_node].hn.first_child;
                bvec[index].hn.next_sibling = root_fc;
                if (root_fc != -1) {
                    bvec[root_fc].hn.prev_sibling = index;
                }
                bvec[index].hn.prev_sibling = root_node; // (parent)
                bvec[root_node].hn.first_child = index;
                return false;
            }
            else {
                // inserted node becomes new root
                int indx_fc = bvec[index].hn.first_child;
                bvec[root_node].hn.next_sibling = indx_fc;
                if (indx_fc != -1) {
                    bvec[indx_fc].hn.prev_sibling = root_node;
                }
                bvec[index].hn.first_child = root_node;
                bvec[root_node].hn.prev_sibling = index; // (parent)
                root_node = index;
                return true;
            }
        }
    }
    
    // merge a pair of sub-heaps, where i2 is the next sibling of i1
    int merge_pair(int i1, int i2)
    {
        Compare is_less;
        if (is_less(bvec[i2].hn.data, bvec[i1].hn.data)) {
            std::swap(i1, i2);
            int i2_prevsibling = bvec[i2].hn.prev_sibling; // may be parent ptr
            if (i2_prevsibling != -1) {
                if (bvec[i2_prevsibling].hn.first_child == i2) {
                    bvec[i2_prevsibling].hn.first_child = i1;
                }
                else {
                    bvec[i2_prevsibling].hn.next_sibling = i1;
                }
            }
            bvec[i1].hn.prev_sibling = i2_prevsibling;
        }
        else {
            // i1 will be the root; set its next sibling:
            int i2_nextsibling = bvec[i2].hn.next_sibling;
            bvec[i1].hn.next_sibling = i2_nextsibling;
            if (i2_nextsibling != -1) {
                bvec[i2_nextsibling].hn.prev_sibling = i1;
            }
        }
        
        // i1 is now the "lesser" node index
        int i1_firstchild = bvec[i1].hn.first_child;
        bvec[i2].hn.next_sibling = i1_firstchild;
        if (i1_firstchild != -1) {
            bvec[i1_firstchild].hn.prev_sibling = i2;
        }
        bvec[i1].hn.first_child = i2;
        bvec[i2].hn.prev_sibling = i1; // (parent)
        return i1;
    }

    int merge_pairs(int index)
    {
        // merge in pairs, left to right, then merge all resulting pairs right-to-left
        
        if (index == -1) return -1;
        
        int prev_pair = -1;
        int sibling = bvec[index].hn.next_sibling;
        if (sibling == -1) {
            return index;
        }
        
        while (sibling != -1) {
            // bvec[index].hn.parent = -1;
            // bvec[sibling].hn.parent = -1;
            int r = merge_pair(index, sibling);
            index = bvec[r].hn.next_sibling;
            
            if (prev_pair != -1) {
                bvec[r].hn.next_sibling = prev_pair;
                bvec[prev_pair].hn.prev_sibling = r;
            }
            else {
                bvec[r].hn.next_sibling = -1;
            }
            bvec[r].hn.prev_sibling = -1;
            
            prev_pair = r;
            if (index != -1) {
                bvec[index].hn.prev_sibling = -1;
                sibling = bvec[index].hn.next_sibling;
            }
            else {
                sibling = -1;
            }
        }
        
        if (index != -1) {
            // un-paired subheap at the end: move it to the start of the pair list
            //bvec[index].hn.parent = -1;
            bvec[index].hn.prev_sibling = -1;
            bvec[index].hn.next_sibling = prev_pair;
            bvec[prev_pair].hn.prev_sibling = index;
            prev_pair = index;
        }
        else {
            bvec[prev_pair].hn.prev_sibling = -1;
        }
        
        // Now merge the resulting heaps one by one
        index = prev_pair;
        sibling = bvec[index].hn.next_sibling;
        while (sibling != -1) {
            index = merge_pair(index, sibling);
            sibling = bvec[index].hn.next_sibling;
        }
        
        return index;
    }

    public:
    
    T & get_data(int index)
    {
        return bvec[index].hn.data;
    }
    
    // Allocate a slot, but do not incorporate into the heap:
    template <typename ...U> int allocate(U... u)
    {
        int r = alloc_slot();
        new (& bvec[r].hn) HeapNode(u...);
        return r;
    }
    
    void deallocate(int index)
    {
        bvec[index].hn.HeapNode::~HeapNode();
        if (index == bvec.size() - 1) {
            bvec.pop_back();
        }
        else {
            bvec[index].next_free = first_free;
            first_free = index;
        }
    }

    // Insert an allocated slot into the heap
    bool insert(int index)
    {
        return merge(index);
    }
    
    // Remove a slot from the heap (but don't deallocate it)
    void remove(int index)
    {
        if (index == root_node) {
            pull_root();
            return;
        }
    
        // We:
        // - Pull the node down to the root as if the key was decreased to -infinity; and then
        // - (effectively) pull_root().
        
        // First we have to find the node in the linked list of siblings, to cut it out:
        // int parent = bvec[index].hn.parent;
        
        int prev_sibling = bvec[index].hn.prev_sibling;
        int next_sibling = bvec[index].hn.next_sibling;
        if (bvec[prev_sibling].hn.first_child == index) {
            // we are the first child
            bvec[prev_sibling].hn.first_child = next_sibling;
            if (next_sibling != -1) {
                bvec[next_sibling].hn.prev_sibling = prev_sibling;
            }
        }
        else {
            bvec[prev_sibling].hn.next_sibling = next_sibling;
            if (next_sibling != -1) {
                bvec[next_sibling].hn.prev_sibling = prev_sibling;
            }
        }
        
        int first_child = bvec[index].hn.first_child;
        bvec[index].hn.next_sibling = -1;
        bvec[index].hn.prev_sibling = -1;
        bvec[index].hn.first_child = -1;
        
        if (first_child != -1) {
            bvec[root_node].hn.next_sibling = first_child;
            bvec[first_child].hn.prev_sibling = root_node;
        
            root_node = merge_pairs(root_node);
        }
    }
    
    int get_root()
    {
        return root_node;
    }
    
    int pull_root()
    {
        int nr = merge_pairs(bvec[root_node].hn.first_child);
        bvec[root_node].hn.first_child = -1;
        root_node = nr;
        return nr;
    }
    
    void dumpnode(int n, std::vector<int> &q)
    {
        using namespace std;
        cout << "Node #" << n << endl;
        cout << "    parent = " << bvec[n].hn.prev_sibling << endl;
        int fc = bvec[n].hn.first_child;
        while (fc != -1) {
            cout << "    child = " << fc << endl;
            q.push_back(fc);
            fc = bvec[fc].hn.next_sibling;
        }
    }
    
    void dump()
    {
        using namespace std;
        cout << "root = " << root_node << endl;
        if (root_node != -1) {
            vector<int> q;
            q.push_back(root_node);
            while (! q.empty()) {
                int b = q.back();
                q.pop_back();
                dumpnode(b, q);
            }
        }
    }
};

}

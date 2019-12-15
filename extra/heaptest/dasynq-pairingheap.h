#ifndef DASYNQ_PAIRINGHEAP_H
#define DASYNQ_PAIRINGHEAP_H

#include <functional>

namespace dasynq {

template <typename T, typename P, typename Compare = std::less<P>>
class pairing_heap
{
    struct heap_node
    {
        T data;
        P prio;
        heap_node * next_sibling;
        heap_node * prev_sibling; // (or parent)
        heap_node * first_child;
        
        template <typename ...U> heap_node(U... u) : data(u...)
        {
            next_sibling = nullptr;
            prev_sibling = nullptr;
            first_child = nullptr;
        }
    };

    public:

    using handle_t = heap_node;
    using handle_t_r = heap_node &;

    private:

    handle_t * root_node = nullptr;
    
    bool merge(handle_t &node)
    {
        if (root_node == nullptr) {
            root_node = &node;
            return true;
        }
        else {
            Compare is_less;
            if (is_less(root_node->prio, node.prio)) {
                // inserted node becomes new subtree
                handle_t * root_fc = root_node->first_child;
                node.next_sibling = root_fc;
                if (root_fc != nullptr) {
                    root_fc->prev_sibling = &node;
                }
                node.prev_sibling = root_node; // (parent)
                root_node->first_child = &node;
                return false;
            }
            else {
                // inserted node becomes new root
                handle_t * indx_fc = node.first_child;
                root_node->next_sibling = indx_fc;
                if (indx_fc != nullptr) {
                    indx_fc->prev_sibling = root_node;
                }
                node.first_child = root_node;
                root_node->prev_sibling = &node; // (parent)
                root_node = &node;
                return true;
            }
        }
    }
    
    // merge a pair of sub-heaps, where i2 is the next sibling of i1
    handle_t * merge_pair(handle_t * i1, handle_t * i2)
    {
        Compare is_less;
        if (is_less(i2->prio, i1->prio)) {
            std::swap(i1, i2);
            handle_t * i2_prevsibling = i2->prev_sibling; // may be parent ptr
            if (i2_prevsibling != nullptr) {
                if (i2_prevsibling->first_child == i2) {
                    i2_prevsibling->first_child = i1;
                }
                else {
                    i2_prevsibling->next_sibling = i1;
                }
            }
           i1->prev_sibling = i2_prevsibling;
        }
        else {
            // i1 will be the root; set its next sibling:
            handle_t * i2_nextsibling = i2->next_sibling;
            i1->next_sibling = i2_nextsibling;
            if (i2_nextsibling != nullptr) {
                i2_nextsibling->prev_sibling = i1;
            }
        }
        
        // i1 is now the "lesser" node index
        handle_t * i1_firstchild = i1->first_child;
        i2->next_sibling = i1_firstchild;
        if (i1_firstchild != nullptr) {
            i1_firstchild->prev_sibling = i2;
        }
        i1->first_child = i2;
        i2->prev_sibling = i1; // (parent)
        return i1;
    }

    handle_t * merge_pairs(handle_t * node)
    {
        // merge in pairs, left to right, then merge all resulting pairs right-to-left
        
        if (node == nullptr) return nullptr;
        
        handle_t * prev_pair = nullptr;
        handle_t * sibling = node->next_sibling;
        if (sibling == nullptr) {
            return node;
        }
        
        while (sibling != nullptr) {
            // bvec[index].hn.parent = -1;
            // bvec[sibling].hn.parent = -1;
            handle_t * r = merge_pair(node, sibling);
            node = r->next_sibling;
            
            if (prev_pair != nullptr) {
                r->next_sibling = prev_pair;
                prev_pair->prev_sibling = r;
            }
            else {
                r->next_sibling = nullptr;
            }
            r->prev_sibling = nullptr;
            
            prev_pair = r;
            if (node != nullptr) {
                node->prev_sibling = nullptr;
                sibling = node->next_sibling;
            }
            else {
                sibling = nullptr;
            }
        }
        
        if (node != nullptr) {
            // un-paired subheap at the end: move it to the start of the pair list
            //bvec[index].hn.parent = -1;
            node->prev_sibling = nullptr;
            node->next_sibling = prev_pair;
            prev_pair->prev_sibling = node;
            prev_pair = node;
        }
        else {
            prev_pair->prev_sibling = nullptr;
        }
        
        // Now merge the resulting heaps one by one
        node = prev_pair;
        sibling = node->next_sibling;
        while (sibling != nullptr) {
            node = merge_pair(node, sibling);
            sibling = node->next_sibling;
        }
        
        return node;
    }

    public:

    T & node_data(handle_t &index)
    {
        return index.data;
    }
    
    // Allocate a slot, but do not incorporate into the heap:
    template <typename ...U> void allocate(handle_t &hndl, U&&... u)
    {
        new (& hndl) heap_node(std::forward<U>(u)...);
    }
    
    void deallocate(handle_t & index)
    {
    }
    
    bool set_priority(handle_t_r index, P pval)
    {
        remove(index);
        return insert(index, pval);
    }

    // Insert an allocated slot into the heap
    bool insert(handle_t_r node, P pval = P())
    {
        node.prio = pval;
        return merge(node);
    }
    
    // Remove a slot from the heap (but don't deallocate it)
    void remove(handle_t_r node)
    {
        if (&node == root_node) {
            pull_root();
            return;
        }
    
        // We:
        // - Pull the node down to the root as if the key was decreased to -infinity; and then
        // - (effectively) pull_root().
        
        // First we have to find the node in the linked list of siblings, to cut it out:
        // int parent = bvec[index].hn.parent;
        
        handle_t * prev_sibling = node.prev_sibling;
        handle_t * next_sibling = node.next_sibling;
        if (prev_sibling->first_child == &node) {
            // we are the first child
            prev_sibling->first_child = next_sibling;
            if (next_sibling != nullptr) {
                next_sibling->prev_sibling = prev_sibling;
            }
        }
        else {
            prev_sibling->next_sibling = next_sibling;
            if (next_sibling != nullptr) {
                next_sibling->prev_sibling = prev_sibling;
            }
        }
        
        handle_t * first_child = node.first_child;
        node.next_sibling = nullptr;
        node.prev_sibling = nullptr;
        node.first_child = nullptr;
        
        if (first_child != nullptr) {
            root_node->next_sibling = first_child;
            first_child->prev_sibling = root_node;
        
            root_node = merge_pairs(root_node);
        }
    }
    
    handle_t_r get_root()
    {
        return *root_node;
    }
    
    const P & get_root_priority()
    {
        return root_node->prio;
    }
    
    handle_t * pull_root()
    {
        handle_t * nr = merge_pairs(root_node->first_child);
        root_node->first_child = nullptr;
        root_node = nr;
        return nr;
    }
    
    bool is_queued(handle_t_r hndl)
    {
        return hndl.prev_sibling != nullptr || root_node == &hndl;
    }
    
    bool empty()
    {
        return root_node == nullptr;
    }
    
    static void init_handle(handle_t_r hndl)
    {

    }

    /*
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
    */
};

}

#endif

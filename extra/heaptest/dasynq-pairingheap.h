#ifndef DASYNQ_PAIRINGHEAP_H
#define DASYNQ_PAIRINGHEAP_H

#include <functional>
#include <utility>
#include <new>

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

    union heap_handle
    {
        heap_node h;

        heap_handle() {}
    };

    public:

    using handle_t = heap_handle;
    using handle_t_r = heap_handle &;

    private:

    heap_node * root_node = nullptr;

    // Merge a new node into the heap. We maintain the invariants:
    // - root node is the "smallest" node in the heap
    // - root node has no siblings (only children)
    bool merge(heap_node &node)
    {
        if (root_node == nullptr) {
            root_node = &node;
            return true;
        }
        else {
            Compare is_less;
            if (is_less(root_node->prio, node.prio)) {
                // inserted node becomes new subtree
                heap_node *root_fc = root_node->first_child;
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
                heap_node * indx_fc = node.first_child;
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
    heap_node *merge_pair(heap_node *i1, heap_node *i2)
    {
        Compare is_less;
        if (is_less(i2->prio, i1->prio)) {
            std::swap(i1, i2);
            heap_node *i2_prevsibling = i2->prev_sibling; // may be parent ptr
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
            heap_node *i2_nextsibling = i2->next_sibling;
            i1->next_sibling = i2_nextsibling;
            if (i2_nextsibling != nullptr) {
                i2_nextsibling->prev_sibling = i1;
            }
        }

        // i1 is now the "lesser" node index
        heap_node *i1_firstchild = i1->first_child;
        i2->next_sibling = i1_firstchild;
        if (i1_firstchild != nullptr) {
            i1_firstchild->prev_sibling = i2;
        }
        i1->first_child = i2;
        i2->prev_sibling = i1; // (parent)
        return i1;
    }

    heap_node *merge_pairs(heap_node *node)
    {
        // merge in pairs, left to right, then merge all resulting pairs right-to-left

        if (node == nullptr) return nullptr;

        heap_node *prev_pair = nullptr;
        heap_node *sibling = node->next_sibling;
        if (sibling == nullptr) {
            return node;
        }

        while (sibling != nullptr) {
            heap_node *r = merge_pair(node, sibling);
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
        return index.h.data;
    }

    // Allocate a slot, but do not incorporate into the heap:
    template <typename ...U> void allocate(handle_t &hndl, U&&... u)
    {
        new (& hndl.h) heap_node(std::forward<U>(u)...);
    }

    void deallocate(handle_t &hndl)
    {
        hndl.h.heap_node::~heap_node();
    }

    bool set_priority(handle_t_r index, P pval)
    {
        remove(index);
        return insert(index, pval);
    }

    // Insert an allocated slot into the heap
    bool insert(handle_t_r node, P pval = P())
    {
        node.h.prio = pval;
        return merge(node.h);
    }

    // Remove a slot from the heap (but don't deallocate it)
    void remove(heap_node &node)
    {
        if (&node == root_node) {
            pull_root();
            return;
        }

        // First unlink the node from its siblings (linked list):
        heap_node *prev_sibling = node.prev_sibling;
        heap_node *next_sibling = node.next_sibling;
        heap_node *first_child = node.first_child;

        if (prev_sibling->first_child == &node) {
            // We are the first child
            prev_sibling->first_child = next_sibling;
            if (next_sibling != nullptr) {
                next_sibling->prev_sibling = prev_sibling;
            }
        }
        else {
            // We are not the first child
            prev_sibling->next_sibling = next_sibling;
            if (next_sibling != nullptr) {
                next_sibling->prev_sibling = prev_sibling;
            }
        }

        node.next_sibling = nullptr;
        node.prev_sibling = nullptr;
        node.first_child = nullptr;

        // Now we have to do something with the children, if any. We throw them together with the
        // root node and merge pairs to produce a single root.
        if (first_child != nullptr) {
            root_node->next_sibling = first_child;
            first_child->prev_sibling = root_node;

            root_node = merge_pairs(root_node);
        }
    }

    void remove(handle_t &hndl)
    {
        return remove(hndl.h);
    }

    handle_t_r get_root()
    {
        return *(handle_t *)root_node;
    }

    const P & get_root_priority()
    {
        return root_node->prio;
    }

    handle_t *pull_root()
    {
        heap_node *nr = merge_pairs(root_node->first_child);
        root_node->first_child = nullptr;
        root_node = nr;
        return (handle_t *)nr;
    }

    bool is_queued(handle_t_r hndl)
    {
        return hndl.h.prev_sibling != nullptr || root_node == &hndl;
    }

    bool empty()
    {
        return root_node == nullptr;
    }
};

}

#endif

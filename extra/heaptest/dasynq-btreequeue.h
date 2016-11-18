#ifndef DASYNQ_BTREEQUEUE_H
#define DASYNQ_BTREEQUEUE_H

#include <vector>
#include <functional>

namespace dasynq {

// Let this be a stable priority queue implementation...

template <typename T, typename P, typename Compare = std::less<P>, int N = 8>
class BTreeQueue
{
    struct SeptNode
    {
        P prio[N];
        int hnidx[N];  // index in the hn vector (linked list)
        SeptNode * children[N + 1];
        SeptNode * parent;
        
        SeptNode() : parent(nullptr)
        {
            for (int i = 0; i < N; i++) {
                hnidx[i] = -1;
                children[i] = nullptr;
            }
            children[N] = nullptr;
        }
        
        int num_vals() noexcept
        {
            // We expect to be >50% full, so count backwards:
            for (int i = N - 1; i >= 0; i--) {
                if (hnidx[i] != -1) {
                    return i + 1;
                }
            }
            return 0;
        }
        
        bool is_leaf() noexcept
        {
            return children[0] == nullptr;
        }
    };
    
    std::vector<SeptNode *> sn_reserve;
    
    struct HeapNode
    {
        T data;
        
        int next_sibling;
        int prev_sibling;
        
        SeptNode * parent; // only maintained for head of list
        
        template <typename ...U> HeapNode(U... u) : data(u...)
        {
            next_sibling = -1;
            prev_sibling = -1;
            parent = nullptr;
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
    
    int num_alloced = 0;
    int num_septs = 0;
    
    SeptNode * root_sept = nullptr; // root of the B=Tree
    SeptNode * left_sept = nullptr; // leftmost child (cache)
    
    int alloc_slot()
    {
        num_alloced++;
        
        // TODO make sure we have enough sept nodes (num_septs)
        
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
    
    SeptNode * alloc_sept()
    {
        // TODO fix
        return new SeptNode();
    }

    public:
    
    T & node_data(int index) noexcept
    {
        return bvec[index].hn.data;
    }
    
    // Allocate a slot, but do not incorporate into the heap:
    template <typename ...U> void allocate(int &hndl, U... u)
    {
        hndl = alloc_slot();
        new (& bvec[hndl].hn) HeapNode(u...);
    }
    
    void deallocate(int index) noexcept
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
    
    bool set_priority(int index, P pval)
    {
        // TODO maybe rework this
        remove(index);
        return insert(index, pval);
    }

    // Insert an allocated slot into the heap
    bool insert(int index, P pval = P()) noexcept
    {
        if (root_sept == nullptr) {
            root_sept = alloc_sept();
            left_sept = root_sept;
        }
        
        SeptNode * srch_sept = root_sept;
        
        bool leftmost = true;

        while (! srch_sept->is_leaf()) {
            // int children = srch_sept->num_vals();
            int min = 0;
            int max = N - 1;
            while (min <= max) {
                int i = (min + max) / 2;

                if (srch_sept->hnidx[i] == -1 || pval < srch_sept->prio[i]) {
                    max = i - 1;
                }
                else if (srch_sept->prio[i] == pval) {
                    // insert into linked list
                    int hnidx = srch_sept->hnidx[i];
                    bvec[index].hn.prev_sibling = bvec[hnidx].hn.prev_sibling;
                    bvec[bvec[index].hn.prev_sibling].hn.next_sibling = index;
                    bvec[index].hn.next_sibling = hnidx;
                    bvec[hnidx].hn.prev_sibling = index;
                    bvec[index].hn.parent = nullptr;
                    return false;
                }
                else {
                    min = i + 1;
                }
            }
            
            if (min != 0) {
                leftmost = false;
            }
            
            // go up to the right:
            srch_sept = srch_sept->children[max + 1];
        }
                
        // We got to a leaf: does it have space?
        // First check if we can add to a linked list
        int children = srch_sept->num_vals();
        
        {
            int min = 0;
            int max = children - 1;
            while (min <= max) {
                int i = (min + max) / 2;

                if (srch_sept->hnidx[i] == -1 || pval < srch_sept->prio[i]) {
                    max = i - 1;
                }
                else if (srch_sept->prio[i] == pval) {
                    // insert into linked list
                    int hnidx = srch_sept->hnidx[i];
                    bvec[index].hn.prev_sibling = bvec[hnidx].hn.prev_sibling;
                    bvec[bvec[index].hn.prev_sibling].hn.next_sibling = index;
                    bvec[index].hn.next_sibling = hnidx;
                    bvec[hnidx].hn.prev_sibling = index;
                    bvec[index].hn.parent = nullptr;
                    return false;
                }
                else {
                    min = i + 1;
                }
            }
        }
        
        bvec[index].hn.prev_sibling = index;
        bvec[index].hn.next_sibling = index;
        
        SeptNode * left_down = nullptr; // left node going down
        SeptNode * right_down = nullptr; // right node going down
        leftmost = leftmost && pval < srch_sept->prio[0];
        
        while (children == N) {
            // split and push value towards root
            SeptNode * new_sibling = alloc_sept();
            new_sibling->parent = srch_sept->parent;

            // create new sibling to the right:
            for (int i = N/2; i < N; i++) {
                new_sibling->prio[i - N/2] = srch_sept->prio[i];  // new[0] = old[4]
                new_sibling->hnidx[i - N/2] = srch_sept->hnidx[i];
                new_sibling->children[i - N/2 + 1] = srch_sept->children[i + 1];
                if (new_sibling->children[i - N/2 + 1]) new_sibling->children[i - N/2 + 1]->parent = new_sibling;
                bvec[new_sibling->hnidx[i - N/2]].hn.parent = new_sibling;
                srch_sept->hnidx[i] = -1;
            }
            // Note that new_sibling->children[0] has not yet been set.
            
            if (pval < srch_sept->prio[N/2 - 1])  {
                auto o_prio = srch_sept->prio[N/2 - 1];
                auto o_hidx = srch_sept->hnidx[N/2 - 1];
                
                new_sibling->children[0] = srch_sept->children[N/2];
                if (new_sibling->children[0]) new_sibling->children[0]->parent = new_sibling;
                
                int i = N/2 - 1;
                for ( ; i > 0 && pval < srch_sept->prio[i - 1]; i--) {
                    srch_sept->prio[i] = srch_sept->prio[i - 1];
                    srch_sept->children[i+1] = srch_sept->children[i];
                    srch_sept->hnidx[i] = srch_sept->hnidx[i - 1];
                }
                srch_sept->prio[i] = pval;
                srch_sept->hnidx[i] = index;
                bvec[index].hn.parent = srch_sept;
                srch_sept->children[i] = left_down;
                srch_sept->children[i+1] = right_down;
                index = o_hidx;
                pval = o_prio;
            }
            else if (pval < new_sibling->prio[0]) {
                // new value is right in the middle
                srch_sept->children[N/2] = left_down;
                new_sibling->children[0] = right_down;
                if (left_down) left_down->parent = srch_sept;
                if (right_down) right_down->parent = new_sibling;
            }
            else {
                auto o_prio = new_sibling->prio[0];
                auto o_hidx = new_sibling->hnidx[0];
                int i = 0;
                for ( ; i < (N/2 - 1) && new_sibling->prio[i + 1] < pval; i++) {
                    new_sibling->prio[i] = new_sibling->prio[i + 1];
                    new_sibling->children[i] = new_sibling->children[i + 1];
                    new_sibling->hnidx[i] = new_sibling->hnidx[i + 1];
                }
                new_sibling->prio[i] = pval;
                new_sibling->hnidx[i] = index;
                bvec[index].hn.parent = new_sibling;
                new_sibling->children[i] = left_down;
                new_sibling->children[i+1] = right_down;
                if (left_down) left_down->parent = new_sibling;
                if (right_down) right_down->parent = new_sibling;                
                index = o_hidx;
                pval = o_prio;
            }
            
            left_down = srch_sept;
            right_down = new_sibling;
            
            srch_sept = srch_sept->parent;
            if (srch_sept == nullptr) {
                // Need new root node:
                srch_sept = alloc_sept();
                root_sept = srch_sept;
                left_down->parent = root_sept;
                right_down->parent = root_sept;
                children = 0;                
            }
            else {
                children = srch_sept->num_vals();
            }
        }
        
        // Insert into non-full node:
        int inspos;
        for (inspos = children; inspos > 0; inspos--) {
            if (srch_sept->prio[inspos - 1] < pval) {
                break;
            }
            
            srch_sept->prio[inspos] = srch_sept->prio[inspos-1];
            srch_sept->hnidx[inspos] = srch_sept->hnidx[inspos-1];
            srch_sept->children[inspos+1] = srch_sept->children[inspos];
        }
        
        srch_sept->prio[inspos] = pval;
        srch_sept->hnidx[inspos] = index;
        srch_sept->children[inspos] = left_down;
        srch_sept->children[inspos+1] = right_down;
        bvec[index].hn.parent = srch_sept;
        return leftmost;
    }
    
    // Merge rsibling, and one value from the parent, into lsibling.
    // Index is the index of the parent value.
    void merge(SeptNode *lsibling, SeptNode *rsibling, int index) noexcept
    {
        int lchildren = lsibling->num_vals();
        lsibling->hnidx[lchildren] = lsibling->parent->hnidx[index];
        lsibling->prio[lchildren] = lsibling->parent->prio[index];
        bvec[lsibling->hnidx[lchildren]].hn.parent = lsibling;
        lchildren++;
        
        // bool leaf = lsibling->is_leaf();
        
        int ri = 0;
        for (ri = 0; rsibling->hnidx[ri] != -1; ri++) {
            lsibling->hnidx[lchildren] = rsibling->hnidx[ri];
            lsibling->prio[lchildren] = rsibling->prio[ri];
            lsibling->children[lchildren] = rsibling->children[ri];
            if (lsibling->children[lchildren]) lsibling->children[lchildren]->parent = lsibling;
            bvec[lsibling->hnidx[lchildren]].hn.parent = lsibling;
            lchildren++;
        }
        lsibling->children[lchildren] = rsibling->children[ri];
        if (lsibling->children[lchildren]) lsibling->children[lchildren]->parent = lsibling;
        delete rsibling;
        
        // Now delete in the parent:
        for (int i = index; i < (N-1); i++) {
            lsibling->parent->hnidx[i] = lsibling->parent->hnidx[i + 1];
            lsibling->parent->prio[i] = lsibling->parent->prio[i + 1];
            lsibling->parent->children[i + 1] = lsibling->parent->children[i + 2];
        }
        lsibling->parent->hnidx[N-1] = -1;
        
        if (lsibling->parent->hnidx[0] == -1) {
            // parent is now empty; it must be root. Make us the new root.
            root_sept = lsibling;
            lsibling->parent = nullptr;
        }        
    }
    
    // borrow values from, or merge with, a sibling node so that the node
    // is suitably (~>=50%) populated.
    void repop_node(SeptNode *sept, int children) noexcept
    {
        start:
        SeptNode *parent = sept->parent;
        if (parent == nullptr) {
            // It's the root node, so don't worry about it, unless empty
            if (sept->hnidx[0] == -1) {
                root_sept = nullptr;
                left_sept = nullptr;
                delete sept;
            }
            return;
        }
        
        // Find a suitable sibling to the left or right:
        if (parent->children[0] == sept) {
            // take right sibling
            SeptNode *rsibling = parent->children[1];
            if (rsibling->num_vals() + children + 1 <= N) {
                // We can merge
                merge(sept, rsibling, 0);
                if (sept->parent != nullptr) {
                    children = sept->parent->num_vals();
                    if (children < N/2) {
                        sept = sept->parent;
                        goto start;
                    }
                }
            }
            else {
                sept->hnidx[children] = parent->hnidx[0];
                sept->prio[children] = parent->prio[0];
                bvec[sept->hnidx[children]].hn.parent = sept;
                sept->children[children + 1] = rsibling->children[0];
                if (sept->children[children + 1]) sept->children[children + 1]->parent = sept;
                
                parent->hnidx[0] = rsibling->hnidx[0];
                parent->prio[0] = rsibling->prio[0];
                bvec[parent->hnidx[0]].hn.parent = parent;
                
                rsibling->children[0] = rsibling->children[1];
                for (int i = 0; i < (N-1); i++) {
                    rsibling->children[i + 1] = rsibling->children[i + 2];
                    rsibling->hnidx[i] = rsibling->hnidx[i + 1];
                    rsibling->prio[i] = rsibling->prio[i + 1];
                }
                rsibling->hnidx[N-1] = -1;

                return;
            }
        }
        else {
            // find left sibling
            int i;
            for (i = 1; i < N; i++) {
                if (parent->children[i] == sept) {
                    break;
                }
            }
            
            SeptNode *lsibling = parent->children[i-1];
            int lchildren = lsibling->num_vals();
            if (lchildren + children + 1 <= N) {
                // merge
                merge(lsibling, sept, i - 1);
                if (lsibling->parent != nullptr) {
                    children = lsibling->parent->num_vals();
                    if (children < N/2) {
                        sept = lsibling->parent;
                        goto start;
                    }
                }
            }
            else {
                // make space for the new value
                for (int i = children; i > 0; i--) {
                    sept->hnidx[i] = sept->hnidx[i - 1];
                    sept->prio[i] = sept->prio[i - 1];
                    sept->children[i + 1] = sept->children[i];
                }
                sept->children[1] = sept->children[0];
                
                sept->hnidx[0] = parent->hnidx[i - 1];
                sept->prio[0] = parent->prio[i - 1];
                bvec[sept->hnidx[0]].hn.parent = sept;
                sept->children[0] = lsibling->children[lchildren];
                if (sept->children[0]) sept->children[0]->parent = sept;
                
                parent->hnidx[i - 1] = lsibling->hnidx[lchildren - 1];
                parent->prio[i - 1] = lsibling->prio[lchildren - 1];
                bvec[parent->hnidx[i - 1]].hn.parent = parent;
                
                lsibling->hnidx[lchildren - 1] = -1;
                
                return;
            }
        }
    }
    
    // Remove a slot from the heap (but don't deallocate it)
    void remove(int index) noexcept
    {
        if (bvec[index].hn.prev_sibling != index) {
            // we're lucky: it's part of a linked list
            int prev = bvec[index].hn.prev_sibling;
            int next = bvec[index].hn.next_sibling;
            bvec[prev].hn.next_sibling = next;
            bvec[next].hn.prev_sibling = prev;
            bvec[index].hn.prev_sibling = -1; // mark as not in queue
            if (bvec[index].hn.parent != nullptr) {
                bvec[next].hn.parent = bvec[index].hn.parent;
                SeptNode * sept = bvec[next].hn.parent;
                for (int i = 0; i < N; i++) {
                    if (sept->hnidx[i] == index) {
                        sept->hnidx[i] = next;
                        break;
                    }
                }
            }
        }
        else {
            // we have to remove from the Btree itself
            // Pull nodes from a child, all the way down
            // the tree. Then re-balance back up the tree,
            // merging nodes if necessary.
            SeptNode * sept = bvec[index].hn.parent;
            
            int i;
            for (i = 0; i < N; i++) {
                if (sept->hnidx[i] == index) {
                    // Ok, go right, then as far as we can to the left:
                    SeptNode * lsrch = sept->children[i+1];
                    SeptNode * prev = sept;
                    while (lsrch != nullptr) {
                        prev = lsrch;
                        lsrch = lsrch->children[0];
                    }
                    
                    if (prev != sept) {
                        sept->hnidx[i] = prev->hnidx[0];
                        sept->prio[i] = prev->prio[0];
                        bvec[sept->hnidx[i]].hn.parent = sept;
                        prev->hnidx[0] = index;
                        sept = prev;
                        i = 0;
                    }
                    
                    // Now we have:
                    // - sept is a leaf in the BTree
                    // - i is the index of the child to remove from it
                    
                    for ( ; i < (N-1); i++) {
                        sept->hnidx[i] = sept->hnidx[i+1];
                        sept->prio[i] = sept->prio[i+1];
                        if (sept->hnidx[i] == -1) {
                            break;
                        }
                    }
                    
                    sept->hnidx[N-1] = -1;
                    
                    // Now if the node is underpopulated, we need to merge with or
                    // borrow from a sibling
                    if (i < N/2) {
                        repop_node(sept, i);
                    }
                    
                    return;
                }
            }
        }
    }
    
    int get_root() noexcept
    {
        if (left_sept == nullptr) return -1;
        return left_sept->hnidx[0];
    }
    
    P & get_root_priority() noexcept
    {
        return left_sept->prio[0];
    }
    
    void pull_root() noexcept
    {
        remove(get_root());
    }
    
    bool is_queued(int hndl) noexcept
    {
        return bvec[hndl].hn.prev_sibling != -1;
    }
    
    bool empty() noexcept
    {
        return root_sept == nullptr;
    }
    
    void check_consistency(SeptNode *node)
    {
        if (node->children[0] != nullptr) {
            // if the first child isn't a null pointer, no children should be:
            for (int i = 0; i < N; i++) {
                if (node->hnidx[i] == -1) break;
                if (node->children[i+1]->parent != node) {
                    abort();
                }
                if  (node->children[i] == node->children[i + 1]) {
                    abort();
                }
            }
        }
    }
    
    /*
    void dumpnode(SeptNode * node, std::vector<SeptNode *> &q)
    {
        using namespace std;
        cout << "Node @ " << (void *) node << endl;
        cout << "    parent = " << node->parent << endl;

        int i;
        for (i = 0; i < N; i++) {
            if (node->hnidx[i] != -1) {
                cout << "   child pri = " << node->prio[i] << "  first child data = " << node_data(node->hnidx[i]) <<  "  child tree = " << (void *)node->children[i];
                if (bvec[node->hnidx[i]].hn.parent != node) cout << " (CHILD VALUE MISMATCH)";
                if (node->children[i] && node->children[i]->parent != node) cout << " (CHILD TREE MISMATCH)";
                // cout << "  child parent = " << (void *)bvec[node->hnidx[i]].hn.parent;
                if (bvec[node->hnidx[i]].hn.prev_sibling != node->hnidx[i]) cout << " (*)";
                cout << endl;
                if (node->children[i]) q.push_back(node->children[i]);
            }
            else {
                cout << "  last child tree = " << (void *)node->children[i] << endl;
                break;
            }
            
            if (i == (N-1)) {
                cout << "  last child tree = " << (void *)node->children[i] << endl;            
            }
        }
        if (node->children[i]) q.push_back(node->children[i]);
        
        //int fc = bvec[n].hn.first_child;
        //while (fc != -1) {
        //    cout << "    child = " << fc << endl;
        //    q.push_back(fc);
        //    fc = bvec[fc].hn.next_sibling;
        //}
    }
    
    void dump()
    {
        using namespace std;
        cout << "root = " << (void *) root_sept << endl;
        if (root_sept != nullptr) {
            vector<SeptNode *> q;
            q.push_back(root_sept);
            while (! q.empty()) {
                auto b = q.back();
                q.pop_back();
                dumpnode(b, q);
            }
        }
    }
    */
};

}

#endif

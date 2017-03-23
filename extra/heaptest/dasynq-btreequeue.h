#ifndef DASYNQ_BTREEQUEUE_H
#define DASYNQ_BTREEQUEUE_H

#include <vector>
#include <functional>

namespace dasynq {

// Let this be a stable priority queue implementation...

template <typename T, typename P, typename Compare = std::less<P>, int N = 8>
class BTreeQueue
{
    struct HeapNode;
    
    public:
    using handle_t = HeapNode;
    using handle_t_r = HeapNode &;
    
    private:
    
    // used to inhibit SeptNode construction
    class no_sept_cons
    {
    };
    
    struct SeptNode
    {
        P prio[N];
        handle_t * hn_p[N];  // pointer to handle (linked list)
        SeptNode * children[N + 1];
        SeptNode * parent;
        
        SeptNode() : parent(nullptr)
        {
            for (int i = 0; i < N; i++) {
                hn_p[i] = nullptr;
                children[i] = nullptr;
            }
            children[N] = nullptr;
        }
        
        SeptNode(no_sept_cons &&nsc)
        {
            // Do nothing; constructor will be run later
        }
        
        int num_vals() noexcept
        {
            // We expect to be >50% full, so count backwards:
            for (int i = N - 1; i >= 0; i--) {
                if (hn_p[i] != nullptr) {
                    return i + 1;
                }
            }
            return 0;
        }
        
        bool is_leaf() noexcept
        {
            return children[0] == nullptr;
        }
        
        void shift_elems_left(int pos, int newpos, int num)
        {
            int diff = pos - newpos;
            int end = pos + num;
            
            for (int i = pos; i < end; i++) {
                prio[i - diff] = prio[i];
                hn_p[i - diff] = hn_p[i];
                children[i - diff] = children[i];
            }
            children[end - diff] = children[end];
        }
        
        void shift_elems_right(int pos, int newpos, int num)
        {
            int diff = newpos - pos;
            int end = pos + num;
            
            children[end + diff] = children[end];
            for (int i = (end - 1); i >= pos; i--) {
                prio[i + diff] = prio[i];
                hn_p[i + diff] = hn_p[i];
                children[i + diff] = children[i];
            }
        }
    };
    
    std::vector<SeptNode *> sn_reserve;
    
    struct HeapNode
    {
        T data;
        
        HeapNode * next_sibling;
        HeapNode * prev_sibling;
        
        SeptNode * parent; // only maintained for head of list
        
        HeapNode()
        {
        
        }
        
        template <typename ...U> HeapNode(U... u) : data(u...)
        {
            next_sibling = nullptr;
            prev_sibling = nullptr;
            parent = nullptr;
        }
    };

    // A slot in the backing vector can be free, or occupied. We use a union to cover
    // these cases; if the slot is free, it just contains a link to the next free slot.
    /*
    union HeapNodeU
    {
        HeapNode hn;
        int next_free;
        
        HeapNodeU() { }
    };
    */
    
    // std::vector<HeapNodeU> bvec;
    
    // int first_free = -1;
    
    // int root_node = -1;
    
    int num_alloced = 0;
    int num_septs = 0;
    int next_sept = 1;  // next num_allocd for which we need another SeptNode in reserve.
    
    SeptNode * root_sept = nullptr; // root of the B=Tree
    SeptNode * left_sept = nullptr; // leftmost child (cache)
    
    void alloc_slot()
    {
        num_alloced++;
        
        if (__builtin_expect(num_alloced == next_sept, 0)) {
            sn_reserve.push_back(new SeptNode(no_sept_cons()));
            // TODO properly handle allocation failure
            num_septs++;
            next_sept += N/2;
        }
        
        /*
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
        */
    }
    
    SeptNode * alloc_sept()
    {
        SeptNode * r = sn_reserve.back();
        sn_reserve.pop_back();
        new (r) SeptNode();
        return r;
    }
    
    void release_sept(SeptNode *s)
    {
        // delete if we have an overabundance of nodes
        if (__builtin_expect(num_alloced < next_sept - N/2, 0)) {
            delete s;
            num_septs--;
            next_sept -= N/2;
        }
        else {
            // TODO this can fail?
            sn_reserve.push_back(s);
        }
    }

    public:
    
    //using handle_t = HeapNode;
    
    T & node_data(handle_t & hn) noexcept
    {
        return hn.data;
    }
    
    static void init_handle(handle_t &hn) noexcept
    {
        // nothing to do
    }
    
    // Allocate a slot, but do not incorporate into the heap:
    template <typename ...U> void allocate(handle_t &hndl, U... u)
    {
        alloc_slot();
        new (& hndl) HeapNode(u...);
    }
    
    void deallocate(handle_t & hn) noexcept
    {
        hn.HeapNode::~HeapNode();
        num_alloced--;
        
        /*
        if (index == bvec.size() - 1) {
            bvec.pop_back();
        }
        else {
            bvec[index].next_free = first_free;
            first_free = index;
        }
        */
    }
    
    bool set_priority(handle_t & index, P & pval)
    {
        // TODO maybe rework this
        remove(index);
        return insert(index, pval);
    }

    // Insert an allocated slot into the heap
    bool insert(handle_t & hndl, P pval = P()) noexcept
    {
        if (root_sept == nullptr) {
            root_sept = alloc_sept();
            left_sept = root_sept;
        }
        
        SeptNode * srch_sept = root_sept;
        
        bool leftmost = true;

        while (! srch_sept->is_leaf()) {
            int min = 0;
            int max = N - 1;
            while (min <= max) {
                int i = (min + max) / 2;

                if (srch_sept->hn_p[i] == nullptr || pval < srch_sept->prio[i]) {
                    max = i - 1;
                }
                else if (srch_sept->prio[i] == pval) {
                    // insert into linked list
                    handle_t * hn_p = srch_sept->hn_p[i];
                    hndl.prev_sibling = hn_p->prev_sibling;
                    hndl.next_sibling = hn_p;
                    hn_p->prev_sibling->next_sibling = &hndl;
                    hn_p->prev_sibling = &hndl;
                    hndl.parent = nullptr;
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

                if (srch_sept->hn_p[i] == nullptr || pval < srch_sept->prio[i]) {
                    max = i - 1;
                }
                else if (srch_sept->prio[i] == pval) {
                    // insert into linked list
                    handle_t * hn_p = srch_sept->hn_p[i];
                    hndl.prev_sibling = hn_p->prev_sibling;
                    hndl.next_sibling = hn_p;
                    hn_p->prev_sibling->next_sibling = &hndl;
                    hn_p->prev_sibling = &hndl;
                    hndl.parent = nullptr;
                    return false;
                }
                else {
                    min = i + 1;
                }
            }
        }
        
        hndl.prev_sibling = &hndl;
        hndl.next_sibling = &hndl;
        
        SeptNode * left_down = nullptr; // left node going down
        SeptNode * right_down = nullptr; // right node going down
        leftmost = leftmost && pval < srch_sept->prio[0];
        
        handle_t * hndl_p = &hndl;
        
        while (children == N) {
            // split and push value towards root
            SeptNode * new_sibling = alloc_sept();
            new_sibling->parent = srch_sept->parent;

            // create new sibling to the right:
            for (int i = N/2; i < N; i++) {
                new_sibling->prio[i - N/2] = srch_sept->prio[i];  // new[0] = old[4]
                new_sibling->hn_p[i - N/2] = srch_sept->hn_p[i];
                new_sibling->children[i - N/2 + 1] = srch_sept->children[i + 1];
                if (new_sibling->children[i - N/2 + 1]) new_sibling->children[i - N/2 + 1]->parent = new_sibling;
                new_sibling->hn_p[i - N/2]->parent = new_sibling;
                srch_sept->hn_p[i] = nullptr;
            }
            // Note that new_sibling->children[0] has not yet been set.
            
            if (pval < srch_sept->prio[N/2 - 1])  {
                auto o_prio = srch_sept->prio[N/2 - 1];
                auto o_hidx = srch_sept->hn_p[N/2 - 1];
                
                new_sibling->children[0] = srch_sept->children[N/2];
                if (new_sibling->children[0]) new_sibling->children[0]->parent = new_sibling;
                
                int i = N/2 - 1;
                for ( ; i > 0 && pval < srch_sept->prio[i - 1]; i--) {
                    srch_sept->prio[i] = srch_sept->prio[i - 1];
                    srch_sept->children[i+1] = srch_sept->children[i];
                    srch_sept->hn_p[i] = srch_sept->hn_p[i - 1];
                }
                srch_sept->prio[i] = pval;
                srch_sept->hn_p[i] = hndl_p;
                hndl_p->parent = srch_sept;
                srch_sept->children[i] = left_down;
                srch_sept->children[i+1] = right_down;
                hndl_p = o_hidx;
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
                auto o_hidx = new_sibling->hn_p[0];
                int i = 0;
                for ( ; i < (N/2 - 1) && new_sibling->prio[i + 1] < pval; i++) {
                    new_sibling->prio[i] = new_sibling->prio[i + 1];
                    new_sibling->children[i] = new_sibling->children[i + 1];
                    new_sibling->hn_p[i] = new_sibling->hn_p[i + 1];
                }
                new_sibling->prio[i] = pval;
                new_sibling->hn_p[i] = hndl_p;
                hndl_p->parent = new_sibling;
                new_sibling->children[i] = left_down;
                new_sibling->children[i+1] = right_down;
                if (left_down) left_down->parent = new_sibling;
                if (right_down) right_down->parent = new_sibling;                
                hndl_p = o_hidx;
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
            srch_sept->hn_p[inspos] = srch_sept->hn_p[inspos-1];
            srch_sept->children[inspos+1] = srch_sept->children[inspos];
        }
        
        srch_sept->prio[inspos] = pval;
        srch_sept->hn_p[inspos] = hndl_p;
        srch_sept->children[inspos] = left_down;
        srch_sept->children[inspos+1] = right_down;
        hndl_p->parent = srch_sept;
        return leftmost;
    }
    
    // Merge rsibling, and one value from the parent, into lsibling.
    // Index is the index of the parent value.
    void merge(SeptNode *lsibling, SeptNode *rsibling, int index) noexcept
    {
        int lchildren = lsibling->num_vals();
        lsibling->hn_p[lchildren] = lsibling->parent->hn_p[index];
        lsibling->prio[lchildren] = lsibling->parent->prio[index];
        lsibling->hn_p[lchildren]->parent = lsibling;
        lchildren++;
        
        // bool leaf = lsibling->is_leaf();
        
        int ri = 0;
        for (ri = 0; rsibling->hn_p[ri] != nullptr; ri++) {
            lsibling->hn_p[lchildren] = rsibling->hn_p[ri];
            lsibling->prio[lchildren] = rsibling->prio[ri];
            lsibling->children[lchildren] = rsibling->children[ri];
            if (lsibling->children[lchildren]) lsibling->children[lchildren]->parent = lsibling;
            lsibling->hn_p[lchildren]->parent = lsibling;
            lchildren++;
        }
        lsibling->children[lchildren] = rsibling->children[ri];
        if (lsibling->children[lchildren]) lsibling->children[lchildren]->parent = lsibling;
        release_sept(rsibling);
        
        // Now delete in the parent:
        for (int i = index; i < (N-1); i++) {
            lsibling->parent->hn_p[i] = lsibling->parent->hn_p[i + 1];
            lsibling->parent->prio[i] = lsibling->parent->prio[i + 1];
            lsibling->parent->children[i + 1] = lsibling->parent->children[i + 2];
        }
        lsibling->parent->hn_p[N-1] = nullptr;
        
        if (lsibling->parent->hn_p[0] == nullptr) {
            // parent is now empty; it must be root. Make us the new root.
            release_sept(lsibling->parent);
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
            if (sept->hn_p[0] == nullptr) {
                root_sept = nullptr;
                left_sept = nullptr;
                release_sept(sept);
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
                sept->hn_p[children] = parent->hn_p[0];
                sept->prio[children] = parent->prio[0];
                sept->hn_p[children]->parent = sept;
                sept->children[children + 1] = rsibling->children[0];
                if (sept->children[children + 1]) sept->children[children + 1]->parent = sept;
                
                parent->hn_p[0] = rsibling->hn_p[0];
                parent->prio[0] = rsibling->prio[0];
                parent->hn_p[0]->parent = parent;
                
                rsibling->shift_elems_left(1, 0, N-1);
                rsibling->hn_p[N-1] = nullptr;
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
                sept->shift_elems_right(0, 1, children);
                
                sept->hn_p[0] = parent->hn_p[i - 1];
                sept->prio[0] = parent->prio[i - 1];
                sept->hn_p[0]->parent = sept;
                sept->children[0] = lsibling->children[lchildren];
                if (sept->children[0]) sept->children[0]->parent = sept;
                
                parent->hn_p[i - 1] = lsibling->hn_p[lchildren - 1];
                parent->prio[i - 1] = lsibling->prio[lchildren - 1];
                parent->hn_p[i - 1]->parent = parent;
                lsibling->hn_p[lchildren - 1] = nullptr;
                
                return;
            }
        }
    }
    
    void remove_from_root()
    {
        SeptNode *sept = left_sept;
        int i;
        for (i = 0; i < (N-1); i++) {
            sept->hn_p[i] = sept->hn_p[i+1];
            sept->prio[i] = sept->prio[i+1];
            if (sept->hn_p[i] == nullptr) {
                break;
            }
        }
        
        sept->hn_p[N-1] = nullptr;
        
        // Now if the node is underpopulated, we need to merge with or
        // borrow from a sibling
        if (i < N/2) {
            repop_node(sept, i);
        }
    }
    
    // Remove a slot from the heap (but don't deallocate it)
    void remove(handle_t & hndl) noexcept
    {
        if (hndl.prev_sibling != &hndl) {
            // we're lucky: it's part of a linked list
            auto prev = hndl.prev_sibling;
            auto next = hndl.next_sibling;
            prev->next_sibling = next;
            next->prev_sibling = prev;
            hndl.prev_sibling = nullptr; // mark as not in queue
            if (hndl.parent != nullptr) {
                next->parent = hndl.parent;
                SeptNode * sept = next->parent;
                for (int i = 0; i < N; i++) {
                    if (sept->hn_p[i] == & hndl) {
                        sept->hn_p[i] = next;
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
            SeptNode * sept = hndl.parent;
            
            int i;
            for (i = 0; i < N; i++) {
                if (sept->hn_p[i] == &hndl) {
                    // Ok, go right, then as far as we can to the left:
                    SeptNode * lsrch = sept->children[i+1];
                    SeptNode * prev = sept;
                    while (lsrch != nullptr) {
                        prev = lsrch;
                        lsrch = lsrch->children[0];
                    }
                    
                    if (prev != sept) {
                        sept->hn_p[i] = prev->hn_p[0];
                        sept->prio[i] = prev->prio[0];
                        sept->hn_p[i]->parent = sept;
                        prev->hn_p[0] = &hndl;
                        sept = prev;
                        i = 0;
                    }
                    
                    // Now we have:
                    // - sept is a leaf in the BTree
                    // - i is the index of the child to remove from it
                    
                    for ( ; i < (N-1); i++) {
                        sept->hn_p[i] = sept->hn_p[i+1];
                        sept->prio[i] = sept->prio[i+1];
                        if (sept->hn_p[i] == nullptr) {
                            break;
                        }
                    }
                    
                    sept->hn_p[N-1] = nullptr;
                    
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
    
    handle_t & get_root() noexcept
    {
        //if (left_sept == nullptr) return nullptr;
        return *left_sept->hn_p[0];
    }
    
    P & get_root_priority() noexcept
    {
        return left_sept->prio[0];
    }
    
    void pull_root() noexcept
    {
        // Effect is:
        //   remove(get_root());
        // ... but we can optimise slightly.
        
        handle_t & r = get_root();
        if (r.prev_sibling != &r) {
            remove(r);
        }
        else {
            remove_from_root();
        }
    }
    
    bool is_queued(handle_t & hndl) noexcept
    {
        return hndl.prev_sibling != nullptr;
    }
    
    bool empty() noexcept
    {
        return root_sept == nullptr;
    }
    
    void check_consistency()
    {
        check_consistency(root_sept);
    }
    
    void check_consistency(SeptNode *node, P * min = nullptr, P * max = nullptr)
    {
        if (node == nullptr) return;
        
        // count children
        int ch_count = 0;
        bool saw_last = false;
        for (int i = 0; i < N; i++) {
            if (node->hn_p[i] == nullptr) {
                saw_last = true;
            }
            else if (saw_last) {
                abort();
            }
            else {
                ch_count++;
            }
        }
        
        if (node->parent != nullptr && ch_count < N/2) abort();
        
        if (node->children[0] != nullptr) {
            // if the first child isn't a null pointer, no children should be:
            int i;
            for (i = 0; i < N; i++) {
                if (node->hn_p[i] == nullptr) break;
                
                // Priority checks
                if (i != 0 && node->prio[i] < node->prio[i-1]) {
                    abort();
                }
                if (min != nullptr && node->prio[i] < *min) {
                    abort();
                }
                if (min != nullptr && node->prio[i] < *min) {
                    abort();
                }
                
                // Link checks
                if (node->hn_p[i]->parent != node) {
                    abort();
                }
                if (node->children[i]->parent != node) {
                    abort();
                }
                if (node->children[i+1]->parent != node) {
                    abort();
                }
                if  (node->children[i] == node->children[i + 1]) {
                    abort();
                }
                
                P * rmin = (i > 0) ? (& node->prio[i-1]) : min;
                P * rmax = & node->prio[i];
                check_consistency(node->children[i], min, max);
            }
            check_consistency(node->children[i], & node->prio[i-1], max);
        }
    }
    
    /*
    void dumpnode(SeptNode * node, std::vector<SeptNode *> &q = {})
    {
        using namespace std;
        cout << "Node @ " << (void *) node << endl;
        cout << "    parent = " << node->parent << endl;

        int i;
        for (i = 0; i < N; i++) {
            if (node->hn_p[i] != nullptr) {
                cout << "   child pri = " << node->prio[i] << "  first child data = " << node_data(* node->hn_p[i]) <<  "  child tree = " << (void *)node->children[i];
                if (node->hn_p[i]->parent != node) cout << " (CHILD VALUE MISMATCH)";
                if (node->children[i] && node->children[i]->parent != node) cout << " (CHILD TREE MISMATCH)";
                // cout << "  child parent = " << (void *)bvec[node->hnidx[i]].hn.parent;
                if (node->hn_p[i]->prev_sibling != node->hn_p[i]) cout << " (*)";
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
    }
    
    void dump()
    {
        using namespace std;
        cout << "root = " << (void *) root_sept << " num_septs = " << num_septs << " sn_reserve.size() = " << sn_reserve.size() << endl;
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

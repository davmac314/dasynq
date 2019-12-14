#ifndef DASYNQ_BTREEQUEUE_H
#define DASYNQ_BTREEQUEUE_H

#include <vector>
#include <functional>

namespace dasynq {

// Let this be a stable priority queue implementation...

template <typename T, typename P, typename Compare = std::less<P>, int N = 8>
class btree_queue
{
    struct heap_node;
    
    public:
    using handle_t = heap_node;
    using handle_t_r = heap_node &;
    
    private:
    
    struct sept_node
    {
        P prio[N];
        handle_t * hn_p[N];  // pointer to handle (linked list)
        sept_node * children[N + 1];
        sept_node * parent;
        
        sept_node()
        {
            // Do nothing; constructor will be run later
        }

        void init()
        {
            for (int i = 0; i < N; i++) {
                hn_p[i] = nullptr;
                children[i] = nullptr;
            }
            children[N] = nullptr;
            parent = nullptr;
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
    
    // A marker type to disambiguate constructor calls:
    class no_data_marker { };

    struct heap_node
    {
        // We wrap the data in union so that it can have a lifetime shorter than the containing
        // heap_node:
        union u_data_t {
            T data;

            // Construct without data:
            u_data_t(no_data_marker) { }

            // Construct with data:
            template <typename ...U> u_data_t(U... u) : data(u...) { }
        } u_data;
        
        // heap_nodes with the same priority are stored in a circular, doubly-linked list:
        heap_node * next_sibling;
        heap_node * prev_sibling;
        
        sept_node * parent; // only maintained for head of list
        
        heap_node() : u_data(no_data_marker {})
        {
        	// do not initialise udata.data
        }
        
        template <typename ...U> heap_node(U&&... u) : u_data(std::forward<U>(u)...)
        {
            next_sibling = nullptr;
            prev_sibling = nullptr;
            parent = nullptr;
        }
    };

    sept_node * root_sept = nullptr; // root of the B-Tree
    sept_node * left_sept = nullptr; // leftmost child (cache)
    sept_node * sn_reserve = nullptr;

    int num_alloced = 0;
    int num_septs = 0;
    int num_septs_needed = 0;
    int next_sept = 1;  // next num_allocd for which we need another sept_node in reserve.
    
    // Note that sept nodes are always at least half full, except for the root sept node.
    // For up to N nodes, one sept node is needed;
    //        at N+1 nodes, three sept nodes are needed: a root and two leaves;
    //     for every (N+1)/2 nodes thereafter, an additional sept node may be required.
    // A simple approximation is, s(n) = (n - 1 + ((N+1)/2)) / ((N+1)/2).
    // With this approximation, s(0) == 0, s(1) == 1, and s(N+1) = 3.  s((N+1)/2+1 <= n <= N) == 2, but we
    // can live with that (conservative is ok). Remember that we use integer arithmetic.
    //
    // (We may actually we get away with much less, if nodes have the same priority, since they
    // are then linked in list and effectively become a single node; but, we need to allocate
    // sept nodes according to the formula in case priorities change).
    
    void alloc_slot()
    {
        num_alloced++;
        
        if (__builtin_expect(num_alloced == next_sept, 0)) {
            if (++num_septs_needed > num_septs) {
                try {
                    sept_node *new_res = new sept_node();
                    new_res->parent = sn_reserve;
                    sn_reserve = new_res;
                    num_septs++;
                }
                catch (...) {
                    num_septs_needed--;
                    num_alloced--;
                    throw;
                }
            }
            next_sept += (N+1)/2;
        }
    }
    
    sept_node * alloc_sept()
    {
        sept_node * r = sn_reserve;
        sn_reserve = r->parent;
        r->init();
        return r;
    }
    
    void release_sept(sept_node *s)
    {
        s->parent = sn_reserve;
        sn_reserve = s;
    }

    public:
    
    T & node_data(handle_t & hn) noexcept
    {
        return hn.u_data.data;
    }
    
    static void init_handle(handle_t &hn) noexcept
    {
        hn.prev_sibling = nullptr;
    }
    
    // Allocate a slot, but do not incorporate into the heap:
    template <typename ...U> void allocate(handle_t &hndl, U&&... u)
    {
        alloc_slot();
        new (& hndl.u_data.data) T(std::forward<U>(u)...);
    }
    
    void deallocate(handle_t & hn) noexcept
    {
        hn.u_data.data.T::~T();
        num_alloced--;
        
        // Potentially release reserved sept node
        if (__builtin_expect(num_alloced < next_sept - (N+1)/2, 0)) {
            num_septs_needed--;
            next_sept -= (N+1)/2;
            if (num_septs_needed < num_septs - 1) {
                // Note the "-1" margin is to alleviate bouncing allocation/deallocation
                sept_node * r = sn_reserve;
                sn_reserve = r->parent;
                delete r;
                num_septs--;
            }
        }
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
        
        sept_node * srch_sept = root_sept;
        
        bool leftmost = true;

        // Start at the root, and go down the tree until we find a leaf sept_node, or until we find
        // a node with the same priority value (in which case, we link the new node with it as
        // a linked list, and are done):
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
        
        sept_node * left_down = nullptr; // left node going down
        sept_node * right_down = nullptr; // right node going down
        leftmost = leftmost && pval < srch_sept->prio[0];
        
        handle_t * hndl_p = &hndl;
        
        // If the sept_node is full, we must split it, and push one node into its parent, which may
        // then also need to be split, and so on:
        while (children == N) {
            // split and push value towards root
            sept_node * new_sibling = alloc_sept();
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
    void merge(sept_node *lsibling, sept_node *rsibling, int index) noexcept
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
    void repop_node(sept_node *sept, int children) noexcept
    {
        start:
        sept_node *parent = sept->parent;
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
            sept_node *rsibling = parent->children[1];
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
            
            sept_node *lsibling = parent->children[i-1];
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
        sept_node *sept = left_sept;
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
                sept_node * sept = next->parent;
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
            sept_node * sept = hndl.parent;
            
            int i;
            for (i = 0; i < N; i++) {
                if (sept->hn_p[i] == &hndl) {
                    // Ok, go right, then as far as we can to the left:
                    sept_node * lsrch = sept->children[i+1];
                    sept_node * prev = sept;
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
    
    ~btree_queue()
    {
        while (sn_reserve != nullptr) {
            auto *next = sn_reserve->parent;
            delete sn_reserve;
            sn_reserve = next;
        }
    }

    /*
    void check_consistency()
    {
        check_consistency(root_sept);
    }
    
    void check_consistency(sept_node *node, P * min = nullptr, P * max = nullptr)
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
            if (max != nullptr && node->prio[i] > *max) {
                abort();
            }

            if (node->hn_p[i]->parent != node) {
                abort();
            }
        }

        if (node->children[0] != nullptr) {
            // if the first child isn't a null pointer, no children should be:
            for (i = 0; i < N; i++) {
                if (node->hn_p[i] == nullptr) break;

                // Link checks
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
                check_consistency(node->children[i], rmin, rmax);
            }
            check_consistency(node->children[i], & node->prio[i-1], max);
        }
    }
    
    void dumpnode(sept_node * node)
    {
        std::vector<sept_node *> bt;
        dumpnode(node, bt);
    }

    void dumpnode(sept_node * node, std::vector<sept_node *> &q)
    {
        using namespace std;
        cout << "Node @ " << (void *) node << endl;
        if (node == nullptr) return;
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
        cout << "root = " << (void *) root_sept << " num_septs = " << num_septs << endl;
        if (root_sept != nullptr) {
            vector<sept_node *> q;
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

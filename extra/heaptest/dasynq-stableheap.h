#include <functional>

template <typename P>
class StablePrio
{
    public:
    P p;
    unsigned long long order;
    
    template <typename ...U>
    StablePrio(unsigned long long o, U... u) : p(u...), order(o)
    {
    }
    
    // zero-argument constructor should not really be needed, but some
    // heap implementations aren't yet perfect.
    StablePrio()
    {
    }
};

template <typename P, typename C>
class CompareStablePrio
{
    public:
    bool operator()(StablePrio<P> &a, StablePrio<P> &b)
    {
        C lt;
        if (lt(a.p, b.p)) {
            return true;
        }
        if (lt(b.p, a.p)) {
            return false;
        }
        
        return a.order < b.order;
    }
};


template <template <typename H1, typename H2, typename H3> class H, typename T, typename P, typename C = std::less<P>>
class StableHeap : private H<T,StablePrio<P>,CompareStablePrio<P,C>>
{
    using Base = H<T,StablePrio<P>,CompareStablePrio<P,C>>;
    
    // using H<T,P,CompareStablePrio<P,C>>:H;  // inherit constructors
    using Base::Base;
    
    unsigned long long sequence = 0;
    
    public:
    
    using handle_t = typename Base::handle_t;
    using handle_t_r = typename Base::handle_t_r;
    
    bool insert(handle_t & index, P pval = P())
    {
        auto sp = StablePrio<P>(sequence++, pval);
        return Base::insert(index, sp);
    }

    template <typename ...U> void allocate(handle_t & hnd, U&& ...u)
    {
        return Base::allocate(hnd, u...);
    }

    decltype(std::declval<Base>().get_root()) get_root()
    {
        return Base::get_root();
    }
    
    void pull_root()
    {
        Base::pull_root();
    }
    
    void deallocate(handle_t_r index)
    {
        Base::deallocate(index);
    }
    
    void remove(handle_t_r hnd)
    {
        Base::remove(hnd);
    }
    
    bool empty()
    {
        return Base::empty();
    }
};

#ifndef DASYNQ_TIMERBASE_H_INCLUDED
#define DASYNQ_TIMERBASE_H_INCLUDED

namespace dasynq {

class TimerData
{
    public:
    struct timespec interval_time; // interval (if 0, one-off timer)
    int expiry_count;  // number of times expired
    bool enabled;   // whether timer reports events
    void *userdata;

    TimerData(void *udata = nullptr) : interval_time({0,0}), expiry_count(0), enabled(true), userdata(udata)
    {
        // constructor
    }
};

class CompareTimespec
{
    public:
    bool operator()(const struct timespec &a, const struct timespec &b)
    {
        if (a.tv_sec < b.tv_sec) {
            return true;
        }

        if (a.tv_sec == b.tv_sec) {
            return a.tv_nsec < b.tv_nsec;
        }

        return false;
    }
};

using timer_handle_t = BinaryHeap<TimerData, struct timespec, CompareTimespec>::handle_t;

static void init_timer_handle(timer_handle_t &hnd) noexcept
{
    BinaryHeap<TimerData, struct timespec, CompareTimespec>::init_handle(hnd);
}

static int divide_timespec(const struct timespec &num, const struct timespec &den, struct timespec &rem)
{
    if (num.tv_sec < den.tv_sec) {
        rem = num;
        return 0;
    }

    if (num.tv_sec == den.tv_sec) {
        if (num.tv_nsec < den.tv_nsec) {
            rem = num;
            return 0;
        }
        if (num.tv_sec == 0) {
            rem.tv_sec = 0;
            rem.tv_nsec = num.tv_nsec % den.tv_nsec;
            return num.tv_nsec / den.tv_nsec;
        }
        // num.tv_sec == den.tv_sec and both are >= 1.
        // The result can only be 1:
        rem.tv_sec = 0;
        rem.tv_nsec = num.tv_nsec - den.tv_nsec;
        return 1;
    }

    // At this point, num.tv_sec >= 1.

    auto &r_sec = rem.tv_sec;
    auto &r_nsec = rem.tv_nsec;
    r_sec = num.tv_sec;
    r_nsec = num.tv_nsec;
    auto d_sec = den.tv_sec;
    auto d_nsec = den.tv_nsec;

    r_sec -= d_sec;
    if (r_nsec >= d_nsec) {
        r_nsec -= d_nsec;
    }
    else {
        r_nsec += (1000000000ULL - d_nsec);
        r_sec -= 1;
    }

    // Check now for common case: one timer expiry with no overrun
    if (r_sec < d_sec || (r_sec == d_sec && r_nsec < d_nsec)) {
        return 1;
    }

    int nval = 1;
    int rval = 1; // we have subtracted 1*D already

    // shift denominator until it is greater than/equal to numerator:
    while (d_sec < r_sec) {
        d_sec *= 2;
        d_nsec *= 2;
        if (d_nsec >= 1000000000) {
            d_nsec -= 1000000000;
            d_sec++;
        }
        nval *= 2;
    }

    while (nval > 0) {
        if (d_sec < r_sec || (d_sec == r_sec && d_nsec <= r_nsec)) {
            // subtract:
            r_sec -= d_sec;
            if (d_nsec > r_nsec) {
                r_nsec += 1000000000;
                r_sec--;
            }
            r_nsec -= d_nsec;

            rval += nval;
        }

        bool low = d_sec & 1;
        d_nsec /= 2;
        d_nsec += low ? 500000000ULL : 0;
        d_sec /= 2;
        nval /= 2;
    }

    return rval;
}

}

#endif /* DASYNQ_TIMERBASE_H_INCLUDED */

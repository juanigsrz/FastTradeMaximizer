using namespace chrono;

#define CLOCK_RNG steady_clock::now().time_since_epoch().count()

class timer: high_resolution_clock {
    const time_point start_time;
public:
    timer(): start_time(now()) {}
    rep elapsed_time() const { return duration_cast<milliseconds>(now()-start_time).count(); }
};

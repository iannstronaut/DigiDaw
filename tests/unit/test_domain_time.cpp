#include "../test_framework.hpp"
#include "../../domain/time/time_map.hpp"

using namespace digidaw::domain;

TEST_CASE(DomainTime, DefaultBpmConversion) {
    TimeMap tm(120.0, DefaultPPQ); // 120 BPM, 960 PPQ -> 1 beat = 0.5s, 1 tick = 0.5/960 s
    ASSERT_EQ(tm.ppq(), DefaultPPQ);

    double sec_per_beat = tm.tick_to_seconds(DefaultPPQ);
    ASSERT_NEAR(sec_per_beat, 0.5, 0.0001);

    Tick tick_back = tm.seconds_to_tick(0.5);
    ASSERT_EQ(tick_back, DefaultPPQ);

    // 1 bar = 4 beats = 2.0s
    Tick bar1_tick = tm.bar_to_tick(1);
    ASSERT_EQ(bar1_tick, DefaultPPQ * 4);
    ASSERT_NEAR(tm.tick_to_seconds(bar1_tick), 2.0, 0.0001);

    ASSERT_EQ(tm.tick_to_bar(bar1_tick), 1);
}

TEST_CASE(DomainTime, TempoChangeMonotonic) {
    TimeMap tm(120.0, DefaultPPQ);
    // At bar 2 (8 * 960 ticks), tempo increases to 240 BPM (twice as fast)
    const Tick change_tick = DefaultPPQ * 8;
    tm.add_tempo_point(change_tick, 240.0);

    // Prior to change: 8 beats @ 120 BPM = 4.0s
    double sec_before = tm.tick_to_seconds(change_tick);
    ASSERT_NEAR(sec_before, 4.0, 0.001);

    // 2 beats after change: 2 beats @ 240 BPM = 0.5s -> total 4.5s
    double sec_after = tm.tick_to_seconds(change_tick + DefaultPPQ * 2);
    ASSERT_NEAR(sec_after, 4.5, 0.001);

    // Round-trip
    Tick t_roundtrip = tm.seconds_to_tick(4.5);
    ASSERT_EQ(t_roundtrip, change_tick + DefaultPPQ * 2);

    // Monotonicity check across 100 sample points
    double last_s = -1.0;
    for (Tick t = 0; t <= DefaultPPQ * 16; t += DefaultPPQ / 4) {
        double s = tm.tick_to_seconds(t);
        ASSERT_TRUE(s >= last_s);
        last_s = s;
    }
}

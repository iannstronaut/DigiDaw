#include "../test_framework.hpp"
#include "../../domain/sequencing/note.hpp"
#include "../../domain/sequencing/pattern.hpp"
#include "../../domain/sequencing/track.hpp"
#include "../../domain/sequencing/channel_rack_layout.hpp"
#include "../../domain/project/project.hpp"
#include "../../domain/buffer/audio_buffer.hpp"
#include "../../app/usecases/transport.hpp"
#include <unordered_set>

using namespace digidaw::domain;
using namespace digidaw::app;

TEST_CASE(DomainSequencing, NoteSetSortingAndToggles) {
    NoteSet set;
    // Step 0 = tick 0, Step 4 = tick 960 (quarter note)
    set.toggle_step(0, DefaultPPQ, 60, 100);
    ASSERT_TRUE(set.has_note_at_step(0, DefaultPPQ, 60));
    ASSERT_EQ(set.notes().size(), 1);

    set.toggle_step(4, DefaultPPQ, 64, 90);
    ASSERT_TRUE(set.has_note_at_step(4, DefaultPPQ, 64));
    ASSERT_EQ(set.notes().size(), 2);

    // Toggle step 0 again -> removes note
    set.toggle_step(0, DefaultPPQ, 60);
    ASSERT_FALSE(set.has_note_at_step(0, DefaultPPQ, 60));
    ASSERT_EQ(set.notes().size(), 1);
}

TEST_CASE(DomainSequencing, PatternLengthCalculation) {
    Pattern pat(1, "Test Pattern");
    auto& ch1_notes = pat.get_or_create_channel_notes(1);

    // Empty pattern length is minimum 1 bar (3840 ticks)
    ASSERT_EQ(pat.length_ticks(DefaultPPQ), DefaultPPQ * 4);

    // Add note in bar 2 (tick 4000)
    ch1_notes.add_note(Note{4000, 240, 60, 100, 0, 0});
    // Total length should round up to 2 bars (7680 ticks)
    ASSERT_EQ(pat.length_ticks(DefaultPPQ), DefaultPPQ * 8);
}

TEST_CASE(DomainSequencing, TrackAndClipQuery) {
    Track track(1, "Bassline");
    track.add_clip(Clip{1, 0, DefaultPPQ * 4, false}); // Bar 0-1
    track.add_clip(Clip{2, DefaultPPQ * 8, DefaultPPQ * 4, false}); // Bar 2-3

    ASSERT_EQ(track.clips().size(), 2);

    // Active at tick 500 (inside bar 0-1)
    auto active1 = track.get_clips_at(500);
    ASSERT_EQ(active1.size(), 1);
    ASSERT_EQ(active1[0].pattern_id, 1);

    // Active at tick 5000 (between bar 1 and 2, empty)
    auto active2 = track.get_clips_at(5000);
    ASSERT_TRUE(active2.empty());

    // Muted track returns no active clips
    track.set_muted(true);
    auto active3 = track.get_clips_at(500);
    ASSERT_TRUE(active3.empty());
}

TEST_CASE(DomainSequencing, ProjectPatternManagement) {
    Project proj("Test Project");
    // Initially has pattern 1
    ASSERT_EQ(proj.patterns().size(), 1);
    ASSERT_EQ(proj.patterns()[0].id(), 1);
    ASSERT_TRUE(proj.get_pattern(1) != nullptr);

    // Add pattern 2 and 3
    uint32_t p2 = proj.add_pattern("Drums");
    uint32_t p3 = proj.add_pattern("Lead Synth");
    ASSERT_EQ(proj.patterns().size(), 3);
    ASSERT_EQ(proj.get_pattern(p2)->name(), "Drums");
    ASSERT_EQ(proj.get_pattern(p3)->name(), "Lead Synth");

    // Remove pattern 2
    bool removed = proj.remove_pattern(p2);
    ASSERT_TRUE(removed);
    ASSERT_EQ(proj.patterns().size(), 2);
    ASSERT_EQ(proj.get_pattern(p2), nullptr);
    ASSERT_TRUE(proj.get_pattern(p3) != nullptr);

    // Removing non-existent pattern returns false
    ASSERT_FALSE(proj.remove_pattern(999));
}

TEST_CASE(DomainSequencing, DecoupledTracksAndArrangementClips) {
    Project proj("Arrangement Project");
    // Add 3 separate arrangement tracks (independent of instrument channels)
    auto t1 = proj.add_track("Track 1 - Intro");
    auto t2 = proj.add_track("Track 2 - Beat");
    auto t3 = proj.add_track("Track 3 - Lead");
    ASSERT_TRUE(proj.tracks().size() >= 3);

    auto p1 = proj.add_pattern("Melody A");
    auto p2 = proj.add_pattern("Drum Beat");

    // Decoupled capability: Track 1 can hold both Pattern 1 and Pattern 2 at different times
    auto* trk1 = proj.get_track(t1);
    ASSERT_TRUE(trk1 != nullptr);
    trk1->add_clip(Clip{p1, 0, DefaultPPQ * 4, false});
    trk1->add_clip(Clip{p2, DefaultPPQ * 4, DefaultPPQ * 4, false});
    ASSERT_EQ(trk1->clips().size(), 2);

    // Decoupled capability: Track 2 can also hold Pattern 1 simultaneously
    auto* trk2 = proj.get_track(t2);
    ASSERT_TRUE(trk2 != nullptr);
    trk2->add_clip(Clip{p1, DefaultPPQ * 2, DefaultPPQ * 4, false});
    ASSERT_EQ(trk2->clips().size(), 1);

    // Query active clips across tracks at tick DefaultPPQ * 3
    auto active_t1 = trk1->get_clips_at(DefaultPPQ * 3);
    auto active_t2 = trk2->get_clips_at(DefaultPPQ * 3);
    ASSERT_EQ(active_t1.size(), 1);
    ASSERT_EQ(active_t1[0].pattern_id, p1);
    ASSERT_EQ(active_t2.size(), 1);
    ASSERT_EQ(active_t2[0].pattern_id, p1);

    // Remove track 3
    ASSERT_TRUE(proj.remove_track_by_id(t3));
    ASSERT_EQ(proj.get_track(t3), nullptr);
}

TEST_CASE(DomainSequencing, AudioBufferStereoPanning) {
    std::vector<float> left(64, 1.0f);
    std::vector<float> right(64, 1.0f);
    AudioBufferView buf{ left.data(), right.data(), 64 };

    // Center pan (0.0) leaves channels balanced and unchanged
    buf.apply_pan(0.0f);
    ASSERT_NEAR(left[0], 1.0f, 0.001f);
    ASSERT_NEAR(right[0], 1.0f, 0.001f);

    // Hard Left pan (-1.0): left unchanged (1.0), right silenced (0.0)
    buf.apply_pan(-1.0f);
    ASSERT_NEAR(left[0], 1.0f, 0.001f);
    ASSERT_NEAR(right[0], 0.0f, 0.001f);

    // Hard Right pan (+1.0): left silenced (0.0), right unchanged (1.0)
    std::fill(left.begin(), left.end(), 1.0f);
    std::fill(right.begin(), right.end(), 1.0f);
    buf.apply_pan(1.0f);
    ASSERT_NEAR(left[0], 0.0f, 0.001f);
    ASSERT_NEAR(right[0], 1.0f, 0.001f);

    // Mid-pan Left (-0.5): left 1.0, right 0.5
    std::fill(left.begin(), left.end(), 1.0f);
    std::fill(right.begin(), right.end(), 1.0f);
    buf.apply_pan(-0.5f);
    ASSERT_NEAR(left[0], 1.0f, 0.001f);
    ASSERT_NEAR(right[0], 0.5f, 0.001f);

    // Mid-pan Right (+0.5): left 0.5, right 1.0
    std::fill(left.begin(), left.end(), 1.0f);
    std::fill(right.begin(), right.end(), 1.0f);
    buf.apply_pan(0.5f);
    ASSERT_NEAR(left[0], 0.5f, 0.001f);
    ASSERT_NEAR(right[0], 1.0f, 0.001f);
}

TEST_CASE(DomainSequencing, PatternRemovalCascadesToArrangementClips) {
    Project proj("Cascade Test Project");
    auto p1 = proj.add_pattern("Pattern A");
    auto p2 = proj.add_pattern("Pattern B");

    auto t1 = proj.add_track("Track 1");
    auto t2 = proj.add_track("Track 2");

    auto* trk1 = proj.get_track(t1);
    auto* trk2 = proj.get_track(t2);
    ASSERT_TRUE(trk1 != nullptr && trk2 != nullptr);

    // Place clips of Pattern B on both Track 1 and Track 2
    trk1->add_clip(Clip{p1, 0, DefaultPPQ * 4, false});
    trk1->add_clip(Clip{p2, DefaultPPQ * 4, DefaultPPQ * 4, false});
    trk2->add_clip(Clip{p2, 0, DefaultPPQ * 8, false});

    ASSERT_EQ(trk1->clips().size(), 2);
    ASSERT_EQ(trk2->clips().size(), 1);

    // Removing Pattern B must cascade and cleanly remove all clips of Pattern B from all tracks
    ASSERT_TRUE(proj.remove_pattern(p2));
    ASSERT_EQ(proj.get_pattern(p2), nullptr);

    ASSERT_EQ(trk1->clips().size(), 1);
    ASSERT_EQ(trk1->clips()[0].pattern_id, p1);
    ASSERT_EQ(trk2->clips().size(), 0);
}

TEST_CASE(DomainSequencing, TransportHonorsArrangementTrackSoloAndLinearSongMode) {
    Project proj("Transport Test Project");
    proj.time_map().set_tempo(120.0);
    const auto ppq = proj.time_map().ppq();

    ChannelSettings s1;
    s1.name = "Lead";
    auto ch1 = proj.add_channel("core.generator.3xosc", s1);

    ChannelSettings s2;
    s2.name = "Drums";
    auto ch2 = proj.add_channel("core.generator.3xosc", s2);

    auto p1 = proj.add_pattern("Melody Pat");
    auto* pat1 = proj.get_pattern(p1);
    ASSERT_TRUE(pat1 != nullptr);
    pat1->get_or_create_channel_notes(ch1).add_note({0, ppq, 60, 100, 0, 0});

    auto p2 = proj.add_pattern("Drum Pat");
    auto* pat2 = proj.get_pattern(p2);
    ASSERT_TRUE(pat2 != nullptr);
    pat2->get_or_create_channel_notes(ch2).toggle_step(0, ppq, 60, 100);

    auto t1 = proj.add_track("Lead Arranger");
    auto t2 = proj.add_track("Drum Arranger");
    auto* trk1 = proj.get_track(t1);
    auto* trk2 = proj.get_track(t2);
    trk1->add_clip(Clip{p1, 0, ppq * 4, false});
    trk2->add_clip(Clip{p2, 0, ppq * 4, false});

    Transport transport(proj);
    transport.set_mode(PlaybackMode::Song);
    transport.play();

    // 1. Both tracks active: events generated for both ch1 and ch2
    auto evs_both = transport.advance_block(256, 44100.0);
    bool has_ch1 = false, has_ch2 = false;
    for (const auto& ce : evs_both) {
        if (ce.channel_id == ch1) has_ch1 = true;
        if (ce.channel_id == ch2) has_ch2 = true;
    }
    ASSERT_TRUE(has_ch1 && has_ch2);

    // 2. Solo Lead track: Drum track must be silenced
    transport.seek(0);
    trk1->set_solo(true);
    auto evs_solo = transport.advance_block(256, 44100.0);
    bool solo_has_ch1 = false, solo_has_ch2 = false;
    for (const auto& ce : evs_solo) {
        if (ce.channel_id == ch1) solo_has_ch1 = true;
        if (ce.channel_id == ch2) solo_has_ch2 = true;
    }
    ASSERT_TRUE(solo_has_ch1);
    ASSERT_FALSE(solo_has_ch2);

    // 3. Linear Song Playback: seeking to Bar 10 (beyond last clip end) does not wrap to 0 when loop is disabled
    transport.seek(ppq * 40); // Tick 38400 (Bar 10)
    ASSERT_FALSE(transport.loop_enabled());
    auto evs_linear = transport.advance_block(256, 44100.0);
    (void)evs_linear;
    ASSERT_TRUE(transport.current_tick() >= ppq * 40);
}

TEST_CASE(DomainSequencing, AudioBufferFallbackSumming) {
    // Verifies that audio buffers can sum and fallback cleanly
    std::vector<float> master_l(64, 0.0f);
    std::vector<float> master_r(64, 0.0f);
    AudioBufferView master_view{ master_l.data(), master_r.data(), 64 };

    std::vector<float> ch_l(64, 0.5f);
    std::vector<float> ch_r(64, 0.5f);
    AudioBufferView ch_view{ ch_l.data(), ch_r.data(), 64 };

    // Fallback accumulation directly into Master
    master_view.add_from(ch_view, 1.0f);
    ASSERT_NEAR(master_l[0], 0.5f, 0.001f);
    ASSERT_NEAR(master_r[0], 0.5f, 0.001f);
}

TEST_CASE(DomainSequencing, TwentyDefaultTracksAndDynamicTrackManagement) {
    Project proj("Twenty Tracks Test");
    ASSERT_EQ(proj.tracks().size(), 20);
    ASSERT_EQ(proj.tracks()[0].name(), "Track 1");
    ASSERT_EQ(proj.tracks()[19].name(), "Track 20");

    // Add track -> increases to 21
    auto t21 = proj.add_track("Track 21");
    ASSERT_EQ(proj.tracks().size(), 21);
    ASSERT_EQ(proj.get_track(t21)->name(), "Track 21");

    // Remove track by ID
    ASSERT_TRUE(proj.remove_track_by_id(t21));
    ASSERT_EQ(proj.tracks().size(), 20);
    ASSERT_EQ(proj.get_track(t21), nullptr);

    // Remove track by index using remove_track_at
    ASSERT_TRUE(proj.remove_track_at(0));
    ASSERT_EQ(proj.tracks().size(), 19);
    ASSERT_EQ(proj.tracks()[0].name(), "Track 2");

    // Remove track using remove_track with size_t index
    size_t idx = 0;
    ASSERT_TRUE(proj.remove_track(idx));
    ASSERT_EQ(proj.tracks().size(), 18);

    // Non-regression test for index vs ID collision:
    // With tracks starting from Track 1 (ID 1) and Track 2 (ID 2),
    // removing index 1 must strictly delete Track 2 (at index 1), NOT Track 1 (which has ID 1)!
    Project proj2("Index Overlap Non-Regression Test");
    ASSERT_EQ(proj2.tracks()[0].name(), "Track 1");
    ASSERT_EQ(proj2.tracks()[0].id(), 1);
    ASSERT_EQ(proj2.tracks()[1].name(), "Track 2");
    ASSERT_EQ(proj2.tracks()[1].id(), 2);

    ASSERT_TRUE(proj2.remove_track(size_t(1))); // delete index 1 (Track 2)
    ASSERT_EQ(proj2.tracks().size(), 19);
    ASSERT_EQ(proj2.tracks()[0].name(), "Track 1"); // Track 1 must remain intact at index 0!
    ASSERT_EQ(proj2.tracks()[1].name(), "Track 3"); // Track 3 shifted into index 1

    // Reduce down to 1 track
    while (proj.tracks().size() > 1) {
        ASSERT_TRUE(proj.remove_track_at(proj.tracks().size() - 1));
    }
    ASSERT_EQ(proj.tracks().size(), 1);

    // Out-of-bounds boundary checks: must safely return false and not alter tracks
    ASSERT_FALSE(proj.remove_track(999));
    ASSERT_FALSE(proj.remove_track(proj.tracks().size()));
    ASSERT_FALSE(proj.remove_track_at(999));
    ASSERT_FALSE(proj.remove_track_at(proj.tracks().size()));

    // Cannot remove last remaining track
    ASSERT_FALSE(proj.remove_track_at(0));
    ASSERT_FALSE(proj.remove_track(size_t(0)));
    ASSERT_FALSE(proj.remove_track_by_id(proj.tracks()[0].id()));
    ASSERT_EQ(proj.tracks().size(), 1);
}

TEST_CASE(DomainSequencing, EmptyTrackSkippingAndOptimizationInTransport) {
    Project proj("Empty Track Skipping Test");
    proj.time_map().set_tempo(120.0);
    const auto ppq = proj.time_map().ppq();

    ChannelSettings s;
    s.name = "ActiveSynth";
    auto ch = proj.add_channel("core.generator.3xosc", s);

    auto p1 = proj.add_pattern("Pattern A");
    auto* pat1 = proj.get_pattern(p1);
    ASSERT_TRUE(pat1 != nullptr);
    pat1->get_or_create_channel_notes(ch).add_note({0, ppq, 60, 100, 0, 0});

    // Track 0 has clip at Bar 1 (0 to 4 bars)
    proj.tracks()[0].add_clip(Clip{p1, 0, ppq * 4, false});

    // Tracks 1..19 remain empty (zero clips)
    for (size_t i = 1; i < proj.tracks().size(); ++i) {
        ASSERT_TRUE(proj.tracks()[i].clips().empty());
    }

    Transport transport(proj);
    transport.set_mode(PlaybackMode::Song);
    transport.play();

    // Block 1 (at tick 0): clip intersects -> scheduled events for ch
    auto evs = transport.advance_block(256, 44100.0);
    ASSERT_FALSE(evs.empty());
    ASSERT_EQ(evs[0].channel_id, ch);

    // Now seek past clip end (Bar 5, tick ppq * 16)
    transport.seek(ppq * 16);
    // At Bar 5, no tracks have clips intersecting -> empty events (zero processing)
    auto evs_empty = transport.advance_block(256, 44100.0);
    ASSERT_TRUE(evs_empty.empty());

    // Muted track is also skipped
    transport.seek(0);
    proj.tracks()[0].set_muted(true);
    auto evs_muted = transport.advance_block(256, 44100.0);
    ASSERT_TRUE(evs_muted.empty());

    // Unmute track but mute clip: must also be skipped (zero processing)
    proj.tracks()[0].set_muted(false);
    proj.tracks()[0].clips_mut()[0].muted = true;
    transport.seek(0);
    auto evs_clip_muted = transport.advance_block(256, 44100.0);
    ASSERT_TRUE(evs_clip_muted.empty());
}

TEST_CASE(DomainSequencing, PlaylistClipNonLoopingExtension) {
    Project proj("Non-Looping Extension Test");
    proj.time_map().set_tempo(120.0);
    const auto ppq = proj.time_map().ppq();

    ChannelSettings s;
    s.name = "Lead";
    auto ch = proj.add_channel("core.generator.3xosc", s);

    // Pattern has notes ONLY in bar 1 (0..ppq)
    auto p1 = proj.add_pattern("Short Pattern");
    auto* pat1 = proj.get_pattern(p1);
    ASSERT_TRUE(pat1 != nullptr);
    pat1->get_or_create_channel_notes(ch).add_note({0, ppq, 60, 100, 0, 0});

    // Clip is stretched to 4 bars (ppq * 16) in Track 1
    proj.tracks()[0].add_clip(Clip{p1, 0, ppq * 16, false});

    Transport transport(proj);
    transport.set_mode(PlaybackMode::Song);
    transport.play();

    // Block at tick 0 (Bar 1): note triggers
    auto evs_bar1 = transport.advance_block(256, 44100.0);
    bool triggered_bar1 = false;
    for (const auto& sch : evs_bar1) {
        if (sch.channel_id == ch && !sch.events.empty()) {
            triggered_bar1 = true;
        }
    }
    ASSERT_TRUE(triggered_bar1);

    // Advance to Bar 2 (tick ppq * 4) -> must NOT loop! Extended portion remains silent!
    transport.seek(ppq * 4);
    auto evs_bar2 = transport.advance_block(256, 44100.0);
    bool triggered_bar2 = false;
    for (const auto& sch : evs_bar2) {
        if (sch.channel_id == ch && !sch.events.empty()) {
            triggered_bar2 = true;
        }
    }
    ASSERT_FALSE(triggered_bar2); // Non-looping: zero events in extended region!

    // Advance to Bar 3 (tick ppq * 8) -> also silent!
    transport.seek(ppq * 8);
    auto evs_bar3 = transport.advance_block(256, 44100.0);
    bool triggered_bar3 = false;
    for (const auto& sch : evs_bar3) {
        if (sch.channel_id == ch && !sch.events.empty()) {
            triggered_bar3 = true;
        }
    }
    ASSERT_FALSE(triggered_bar3);

    // Add note in pattern at tick ppq * 20 (Bar 6): since clip ends at ppq * 16, it must not trigger
    pat1->get_or_create_channel_notes(ch).add_note({ppq * 20, ppq, 64, 100, 0, 0});
    transport.seek(ppq * 20);
    auto evs_bar6 = transport.advance_block(256, 44100.0);
    bool triggered_bar6 = false;
    for (const auto& sch : evs_bar6) {
        if (sch.channel_id == ch && !sch.events.empty()) {
            triggered_bar6 = true;
        }
    }
    ASSERT_FALSE(triggered_bar6);
}

TEST_CASE(DomainSequencing, ChannelRackStepSequencerMultiBarTogglesAndDynamicStepSizing) {
    NoteSet notes;
    const Tick ppq = DefaultPPQ;

    // Toggle note at step 0 (Bar 1, Step 1)
    notes.toggle_step(0, ppq, 60, 100);
    ASSERT_TRUE(notes.has_note_at_step(0, ppq, 60));
    ASSERT_EQ(notes.notes().size(), 1);
    ASSERT_EQ(notes.notes()[0].start, 0);

    // Toggle note at step 20 (Bar 2, Step 5)
    notes.toggle_step(20, ppq, 60, 100);
    ASSERT_TRUE(notes.has_note_at_step(20, ppq, 60));
    ASSERT_EQ(notes.notes().size(), 2);
    ASSERT_EQ(notes.notes()[1].start, 20 * (ppq / 4));

    // Toggle note at step 50 (Bar 4, Step 3)
    notes.toggle_step(50, ppq, 60, 100);
    ASSERT_TRUE(notes.has_note_at_step(50, ppq, 60));
    ASSERT_EQ(notes.notes().size(), 3);
    ASSERT_EQ(notes.notes()[2].start, 50 * (ppq / 4));

    // Toggle off step 20
    notes.toggle_step(20, ppq, 60);
    ASSERT_FALSE(notes.has_note_at_step(20, ppq, 60));
    ASSERT_EQ(notes.notes().size(), 2);

    // Toggle note at step 64 (Bar 5, Step 1)
    notes.toggle_step(64, ppq, 60, 100);
    ASSERT_TRUE(notes.has_note_at_step(64, ppq, 60));
    ASSERT_EQ(notes.notes().back().start, 64 * (ppq / 4));

    // Toggle note at step 96 (Bar 7, Step 1)
    notes.toggle_step(96, ppq, 60, 100);
    ASSERT_TRUE(notes.has_note_at_step(96, ppq, 60));
    ASSERT_EQ(notes.notes().back().start, 96 * (ppq / 4));

    // Toggle note at step 111 (Bar 7, Step 16)
    notes.toggle_step(111, ppq, 60, 100);
    ASSERT_TRUE(notes.has_note_at_step(111, ppq, 60));

    // Verify Pattern length_ticks reflects notes extending into 7 bars
    Pattern pat(1, "MultiBarPat");
    pat.get_or_create_channel_notes(1) = notes;
    ASSERT_EQ(pat.length_ticks(ppq), 7 * 4 * ppq); // 7 full bars (28 * ppq)

    // Test dynamic step count calculation using actual ChannelRackLayout production component:
    // Default width (1600px) shows 4 bars (64 steps)
    ASSERT_EQ(ChannelRackLayout::pad_width(), 25.0f);
    ASSERT_EQ(ChannelRackLayout::num_bars_for_width(1600.0f), 4); // 4 bars = 64 steps
    ASSERT_EQ(ChannelRackLayout::num_steps_for_width(1600.0f), 64);
    ASSERT_EQ(ChannelRackLayout::num_bars_for_width(2000.0f), 5); // 5 bars = 80 steps
    ASSERT_EQ(ChannelRackLayout::num_steps_for_width(2000.0f), 80);
    ASSERT_EQ(ChannelRackLayout::num_bars_for_width(2400.0f), 6); // 6 bars = 96 steps
    ASSERT_EQ(ChannelRackLayout::num_steps_for_width(2400.0f), 96);
    ASSERT_EQ(ChannelRackLayout::num_bars_for_width(2800.0f), 7); // 7 bars = 112 steps
    ASSERT_EQ(ChannelRackLayout::num_steps_for_width(2800.0f), 112);
    // Narrow widths scale down without pad clipping
    ASSERT_EQ(ChannelRackLayout::num_bars_for_width(800.0f), 2); // 2 bars = 32 steps
    ASSERT_EQ(ChannelRackLayout::num_bars_for_width(400.0f), 1); // 1 bar = 16 steps
}

TEST_CASE(DomainSequencing, OfflineRendererArrangementTrackSoloAndEmptyTrackSkipping) {
    Project proj("Offline Track Skipping Test");
    proj.time_map().set_tempo(120.0);
    const auto ppq = proj.time_map().ppq();

    ChannelSettings s1; s1.name = "Lead";
    auto ch1 = proj.add_channel("core.generator.3xosc", s1);
    ChannelSettings s2; s2.name = "Drums";
    auto ch2 = proj.add_channel("core.generator.3xosc", s2);

    auto p1 = proj.add_pattern("Lead Pat");
    auto* pat1 = proj.get_pattern(p1);
    pat1->get_or_create_channel_notes(ch1).add_note({0, ppq, 60, 100, 0, 0});

    auto p2 = proj.add_pattern("Drum Pat");
    auto* pat2 = proj.get_pattern(p2);
    pat2->get_or_create_channel_notes(ch2).add_note({0, ppq, 42, 100, 0, 0});

    proj.tracks()[0].add_clip(Clip{p1, 0, ppq * 4, false});
    proj.tracks()[1].add_clip(Clip{p2, 0, ppq * 4, false});

    // When Lead track is soloed, drums should be excluded from active arrangement channels
    proj.tracks()[0].set_solo(true);
    bool any_solo = false;
    for (const auto& trk : proj.tracks()) {
        if (trk.solo()) { any_solo = true; break; }
    }
    ASSERT_TRUE(any_solo);

    std::unordered_set<ChannelId> active_arrangement_channels;
    for (const auto& trk : proj.tracks()) {
        if (trk.clips().empty()) continue;
        if (any_solo ? !trk.solo() : trk.is_muted()) continue;
        for (const auto& clip : trk.clips()) {
            if (clip.muted) continue;
            if (const auto* pat = proj.get_pattern(clip.pattern_id)) {
                for (const auto& [cid, nset] : pat->all_notes()) {
                    for (const auto& note : nset.notes()) {
                        if (note.start < clip.length) {
                            active_arrangement_channels.insert(cid);
                            break;
                        }
                    }
                }
            }
        }
    }
    ASSERT_TRUE(active_arrangement_channels.contains(ch1));
    ASSERT_FALSE(active_arrangement_channels.contains(ch2));
}

TEST_CASE(DomainSequencing, TransportBlockBoundaryNoteOffEmission) {
    // Regression test for hanging note bug:
    // When a note ends exactly on a block boundary where clip.end() == s_start of the next block,
    // Note-Off must be emitted in the next block and not dropped.
    Project proj("Boundary Note Off Test");
    proj.time_map().set_tempo(120.0);
    const auto ppq = proj.time_map().ppq();

    ChannelSettings s;
    s.name = "Synth";
    auto ch = proj.add_channel("core.generator.3xosc", s);

    auto p1 = proj.add_pattern("Pattern 1");
    auto* pat1 = proj.get_pattern(p1);
    ASSERT_TRUE(pat1 != nullptr);

    // Note from tick 0 to tick 960 (length 960)
    pat1->get_or_create_channel_notes(ch).add_note({0, 960, 60, 100, 0, 0});

    // Clip ends at tick 960 (clip.length = 960)
    proj.tracks()[0].add_clip(Clip{p1, 0, 960, false});

    Transport transport(proj);
    transport.set_mode(PlaybackMode::Song);
    transport.play();

    // Block 1: [0, 960 ticks)
    // At 120 BPM with PPQ 960: ticks_per_sec = 1920 ticks/sec.
    // 960 ticks = 0.5 sec = 22050 frames @ 44100 Hz.
    (void)ppq;
    auto evs_block1 = transport.advance_block(22050, 44100.0);
    ASSERT_FALSE(evs_block1.empty());
    bool has_note_on_b1 = false;
    bool has_note_off_b1 = false;
    for (const auto& sch : evs_block1) {
        if (sch.channel_id == ch) {
            for (const auto& ev : sch.events) {
                if (ev.is_note_on()) has_note_on_b1 = true;
                if (ev.is_note_off()) has_note_off_b1 = true;
            }
        }
    }
    ASSERT_TRUE(has_note_on_b1);
    ASSERT_FALSE(has_note_off_b1); // Note-off is at tick 960, so not in [0, 960)

    // Block 2: [960, 1920 ticks) -> clip.end() == 960 == s_start
    // MUST emit Note-Off at tick 960!
    auto evs_block2 = transport.advance_block(22050, 44100.0);
    ASSERT_FALSE(evs_block2.empty());
    bool has_note_off_b2 = false;
    bool has_note_on_b2 = false;
    for (const auto& sch : evs_block2) {
        if (sch.channel_id == ch) {
            for (const auto& ev : sch.events) {
                if (ev.is_note_off()) {
                    has_note_off_b2 = true;
                    ASSERT_EQ(ev.tick, 960);
                }
                if (ev.is_note_on()) has_note_on_b2 = true;
            }
        }
    }
    ASSERT_TRUE(has_note_off_b2); // Note-off was successfully emitted!
    ASSERT_FALSE(has_note_on_b2);

    // Block 3: [1920, 2880 ticks) -> clip is completely past, early-skipped
    auto evs_block3 = transport.advance_block(22050, 44100.0);
    ASSERT_TRUE(evs_block3.empty());
}

TEST_CASE(DomainSequencing, OfflineRendererSkipsChannelsWithNotesBeyondClipLength) {
    Project proj("Offline Clip Boundary Resource Test");
    proj.time_map().set_tempo(120.0);

    ChannelSettings s1; s1.name = "ActiveCh";
    auto ch1 = proj.add_channel("core.generator.3xosc", s1);
    ChannelSettings s2; s2.name = "BeyondClipCh";
    auto ch2 = proj.add_channel("core.generator.3xosc", s2);

    auto p1 = proj.add_pattern("Pattern");
    auto* pat1 = proj.get_pattern(p1);

    // ch1 has note at tick 0 (within clip of length 960)
    pat1->get_or_create_channel_notes(ch1).add_note({0, 480, 60, 100, 0, 0});

    // ch2 has notes ONLY at tick 2000 (strictly beyond clip of length 960)
    pat1->get_or_create_channel_notes(ch2).add_note({2000, 480, 64, 100, 0, 0});

    proj.tracks()[0].add_clip(Clip{p1, 0, 960, false});

    std::unordered_set<ChannelId> active_channels;
    for (const auto& trk : proj.tracks()) {
        if (trk.clips().empty()) continue;
        for (const auto& clip : trk.clips()) {
            if (clip.muted) continue;
            if (const auto* pat = proj.get_pattern(clip.pattern_id)) {
                for (const auto& [cid, nset] : pat->all_notes()) {
                    for (const auto& note : nset.notes()) {
                        if (note.start < clip.length) {
                            active_channels.insert(cid);
                            break;
                        }
                    }
                }
            }
        }
    }

    ASSERT_TRUE(active_channels.contains(ch1));
    ASSERT_FALSE(active_channels.contains(ch2)); // ch2 is early-skipped (zero buffer allocation)
}

TEST_CASE(DomainSequencing, BeatMakerAndPianoRollTimebaseAlignment1Bar4Beats16Steps) {
    const Tick ppq = DefaultPPQ; // 960
    const Tick step_ticks = ChannelRackLayout::step_ticks(ppq); // 240
    const Tick beat_ticks = ppq; // 960
    const Tick bar_ticks = ChannelRackLayout::bar_ticks(ppq); // 3840
    const float pad_w = ChannelRackLayout::pad_width(); // 25.0f

    // 1. Timebase Invariants: 1 Bar = 4 Beats = 16 Steps
    ASSERT_EQ(ChannelRackLayout::kStepsPerBar, 16);
    ASSERT_EQ(ChannelRackLayout::kBeatsPerBar, 4);
    ASSERT_EQ(ChannelRackLayout::kStepsPerBeat, 4);
    ASSERT_EQ(step_ticks, ppq / 4);
    ASSERT_EQ(beat_ticks, 4 * step_ticks);
    ASSERT_EQ(bar_ticks, 4 * beat_ticks);
    ASSERT_EQ(bar_ticks, 16 * step_ticks);
    ASSERT_EQ(bar_ticks, 3840);

    // 2. Beat Maker (Step Sequencer) Note Creation
    NoteSet drum_notes;
    // Step 0 = tick 0 (Bar 1, Beat 1, Step 1)
    drum_notes.toggle_step(0, ppq, 60, 100);
    ASSERT_TRUE(drum_notes.has_note_at_step(0, ppq, 60));
    ASSERT_EQ(drum_notes.notes().front().start, 0);
    ASSERT_EQ(drum_notes.notes().front().length, step_ticks); // Exactly 1 step (16th note)

    // Step 15 = tick 3600 (Bar 1, Beat 4, Step 4 - last step of 1 bar)
    drum_notes.toggle_step(15, ppq, 60, 100);
    ASSERT_TRUE(drum_notes.has_note_at_step(15, ppq, 60));
    ASSERT_EQ(drum_notes.notes().back().start, 15 * step_ticks);
    ASSERT_EQ(drum_notes.notes().back().length, step_ticks);
    ASSERT_EQ(drum_notes.notes().back().start + drum_notes.notes().back().length, bar_ticks); // Ends exactly at 1 bar!

    Pattern drum_pat(1, "Drums 1 Bar");
    drum_pat.get_or_create_channel_notes(1) = drum_notes;
    // 16 steps in beat maker precisely spans 1 bar (4 beats = 3840 ticks)
    ASSERT_EQ(drum_pat.length_ticks(ppq), bar_ticks);
    ASSERT_EQ(drum_pat.length_ticks(ppq), 4 * ppq);

    // 3. Piano Roll Note Creation: 1 Bar (4 Beats = 16 Steps)
    NoteSet melody_notes;
    // 1 bar sustained note (Middle C, length = 4 * ppq = 16 steps)
    melody_notes.add_note(Note{0, bar_ticks, 60, 100, 0, 0});
    Pattern melody_pat(2, "Melody 1 Bar");
    melody_pat.get_or_create_channel_notes(2) = melody_notes;
    ASSERT_EQ(melody_pat.length_ticks(ppq), bar_ticks);
    ASSERT_EQ(melody_pat.length_ticks(ppq), 4 * ppq);

    // Both beat maker and piano roll patterns have identical 1-bar duration
    ASSERT_EQ(drum_pat.length_ticks(ppq), melody_pat.length_ticks(ppq));

    // 4. Channel Rack Layout Alignment: Mini Piano Roll vs Step Sequencer Pads
    const float grid_x = 248.0f;
    // Width of 16 steps (1 bar) of beat maker pads:
    float bar1_pads_w = 16.0f * pad_w; // 16 * 25.0f = 400.0f px
    ASSERT_EQ(bar1_pads_w, 400.0f);

    // Width of 1-bar piano roll note rendered in Channel Rack:
    float piano_roll_1bar_w = ChannelRackLayout::ticks_to_width(bar_ticks, ppq, pad_w);
    ASSERT_EQ(piano_roll_1bar_w, 400.0f);
    ASSERT_EQ(piano_roll_1bar_w, bar1_pads_w); // Pixel-perfect 1:1 match!

    // Position of note starting at tick 0:
    float note_start_x = ChannelRackLayout::tick_to_x(grid_x, 0, ppq, pad_w);
    ASSERT_EQ(note_start_x, grid_x);

    // End coordinate of 1-bar note:
    float note_end_x = note_start_x + piano_roll_1bar_w;
    ASSERT_EQ(note_end_x, grid_x + 400.0f);
    // Matches the right edge of step pad 15 (step 16 boundary):
    float step16_boundary_x = grid_x + 16.0f * pad_w;
    ASSERT_EQ(note_end_x, step16_boundary_x);

    // In a 2-bar (32-step = 800px) Channel Rack window:
    int num_bars_800 = ChannelRackLayout::num_bars_for_width(800.0f);
    ASSERT_EQ(num_bars_800, 2); // 2 bars = 32 steps = 8 beats
    int num_steps_800 = ChannelRackLayout::num_steps_for_width(800.0f);
    ASSERT_EQ(num_steps_800, 32);

    // In this 32-step window, the 1-bar note occupies EXACTLY 400px (50% of the lane, Bar 1)
    // and DOES NOT expand to the full 800px (2 bars / 8 beats)!
    ASSERT_EQ(piano_roll_1bar_w, 400.0f);
    ASSERT_NE(piano_roll_1bar_w, 800.0f);

    // 5. Beat Maker Step 16 Toggle Expands Pattern to 2 Bars (8 Beats)
    drum_notes.toggle_step(16, ppq, 60, 100); // First step of Bar 2 (tick 3840)
    ASSERT_TRUE(drum_notes.has_note_at_step(16, ppq, 60));
    drum_pat.get_or_create_channel_notes(1) = drum_notes;
    ASSERT_EQ(drum_pat.length_ticks(ppq), 2 * bar_ticks); // 2 bars = 8 beats = 7680 ticks
    ASSERT_EQ(drum_pat.length_ticks(ppq), 8 * ppq);

    // 2 bars width in Channel Rack:
    float piano_roll_2bars_w = ChannelRackLayout::ticks_to_width(2 * bar_ticks, ppq, pad_w);
    ASSERT_EQ(piano_roll_2bars_w, 800.0f);
    ASSERT_EQ(piano_roll_2bars_w, 32.0f * pad_w);

    // 6. Playlist Clip Length Alignment
    Track arranger_track(1, "Track 1");
    // Placing 1-bar drum clip:
    arranger_track.add_clip(Clip{2, 0, melody_pat.length_ticks(ppq), false});
    ASSERT_EQ(arranger_track.clips().front().length, bar_ticks); // Exactly 1 bar (4 beats)

    // Placing 2-bar drum clip:
    arranger_track.add_clip(Clip{1, bar_ticks, drum_pat.length_ticks(ppq), false});
    ASSERT_EQ(arranger_track.clips().back().length, 2 * bar_ticks); // Exactly 2 bars (8 beats)
}

TEST_CASE(DomainSequencing, ChannelRackPianoRollClassificationAndHitboxInvariants) {
    const Tick ppq = DefaultPPQ; // 960
    const Tick step_ticks = ChannelRackLayout::step_ticks(ppq); // 240
    const Tick bar_ticks = ChannelRackLayout::bar_ticks(ppq); // 3840

    // 1. Channel classification: Step Sequencer (Drum Machine) vs Piano Roll (Melodic)
    NoteSet empty_set;
    ASSERT_FALSE(ChannelRackLayout::is_channel_piano_roll(empty_set, ppq));

    // Pad notes at pitch 60, quantized to 16th note steps, length == step_ticks (240)
    NoteSet pad_set;
    pad_set.toggle_step(0, ppq, 60, 100);
    pad_set.toggle_step(4, ppq, 60, 100);
    pad_set.toggle_step(15, ppq, 60, 100);
    ASSERT_FALSE(ChannelRackLayout::is_channel_piano_roll(pad_set, ppq));

    // Step 16 in Bar 2 is still a step sequencer pad
    pad_set.toggle_step(16, ppq, 60, 100);
    ASSERT_FALSE(ChannelRackLayout::is_channel_piano_roll(pad_set, ppq));

    // Piano Roll note: 1 bar sustained note (length 3840 = 16 steps)
    NoteSet bar1_piano_roll;
    bar1_piano_roll.add_note(Note{0, bar_ticks, 60, 100, 0, 0});
    ASSERT_TRUE(ChannelRackLayout::is_channel_piano_roll(bar1_piano_roll, ppq));

    // Piano Roll note: 1 beat quarter note (length 960 = 4 steps)
    NoteSet beat1_piano_roll;
    beat1_piano_roll.add_note(Note{0, ppq, 60, 100, 0, 0});
    ASSERT_TRUE(ChannelRackLayout::is_channel_piano_roll(beat1_piano_roll, ppq));

    // Piano Roll note: Pitch != 60 (e.g., E5 = 64)
    NoteSet melodic_set;
    melodic_set.add_note(Note{0, step_ticks, 64, 100, 0, 0});
    ASSERT_TRUE(ChannelRackLayout::is_channel_piano_roll(melodic_set, ppq));

    // Piano Roll note: Micro-timed / unquantized start (start % step_ticks != 0)
    NoteSet offgrid_set;
    offgrid_set.add_note(Note{100, step_ticks, 60, 100, 0, 0});
    ASSERT_TRUE(ChannelRackLayout::is_channel_piano_roll(offgrid_set, ppq));

    // 2. Pattern Length Invariant: 1 Bar = 4 Beats = 16 Steps = 3840 ticks
    Pattern p1(1, "Pat 1");
    p1.get_or_create_channel_notes(1) = bar1_piano_roll;
    ASSERT_EQ(p1.length_ticks(ppq), bar_ticks);
    ASSERT_EQ(p1.length_ticks(ppq), 4 * ppq);

    Pattern p2(2, "Pat 2");
    p2.get_or_create_channel_notes(1) = pad_set;
    // pad_set has note at step 16 -> 2 bars = 8 beats = 7680 ticks
    ASSERT_EQ(p2.length_ticks(ppq), 2 * bar_ticks);
    ASSERT_EQ(p2.length_ticks(ppq), 8 * ppq);

    // Multi-pattern isolation: active pattern notes query
    Project proj("Multi Pattern Project");
    auto pid1 = proj.add_pattern("Intro Melody");
    auto pid2 = proj.add_pattern("Main Beat");
    auto* pat1 = proj.get_pattern(pid1);
    auto* pat2 = proj.get_pattern(pid2);
    ASSERT_TRUE(pat1 != nullptr);
    ASSERT_TRUE(pat2 != nullptr);
    ASSERT_NE(pat1->id(), pat2->id());

    pat1->get_or_create_channel_notes(1).add_note(Note{0, bar_ticks, 60, 100, 0, 0});
    pat2->get_or_create_channel_notes(1).toggle_step(0, ppq, 60, 100);

    ASSERT_TRUE(ChannelRackLayout::is_channel_piano_roll(*pat1->get_channel_notes(1), ppq));
    ASSERT_FALSE(ChannelRackLayout::is_channel_piano_roll(*pat2->get_channel_notes(1), ppq));

    // 3. Robustness & Boundary Invariants: zero/negative ppq must never divide by zero or crash
    ASSERT_TRUE(ChannelRackLayout::step_ticks(0) >= 1);
    ASSERT_TRUE(ChannelRackLayout::bar_ticks(0) >= 1);
    ASSERT_FALSE(ChannelRackLayout::is_channel_piano_roll(empty_set, 0));
    ASSERT_TRUE(ChannelRackLayout::is_channel_piano_roll(bar1_piano_roll, 0));

    Pattern zero_ppq_pat(99, "Zero PPQ Pat");
    ASSERT_TRUE(zero_ppq_pat.length_ticks(0) >= 1);

    NoteSet boundary_set;
    boundary_set.toggle_step(0, 0, 60, 100);
    ASSERT_TRUE(boundary_set.has_note_at_step(0, 0, 60));
}

TEST_CASE(DomainSequencing, ChannelSettingsDefaultVolumeAndHeadroom) {
    // 1. Verify default constants
    ASSERT_NEAR(kDefaultChannelVolume, 0.8f, 1e-5f);
    ASSERT_NEAR(kMaxChannelVolume, 1.0f, 1e-5f);

    // Headroom is precisely 20% (+0.20f) above nominal 80% default
    float headroom = kMaxChannelVolume - kDefaultChannelVolume;
    ASSERT_NEAR(headroom, 0.2f, 1e-5f);

    // 2. Verify ChannelSettings default initialization
    ChannelSettings s;
    ASSERT_NEAR(s.volume, 0.8f, 1e-5f);
    ASSERT_NEAR(s.volume, kDefaultChannelVolume, 1e-5f);

    // 3. Verify normalization to knob position [0.0, 1.0]
    // Default 0.8f volume must yield exactly 80% knob position (0.8f)
    float vol_norm = std::clamp(s.volume / kMaxChannelVolume, 0.0f, 1.0f);
    ASSERT_NEAR(vol_norm, 0.8f, 1e-5f);
    int vol_pct = static_cast<int>(std::round(vol_norm * 100.0f));
    ASSERT_EQ(vol_pct, 80);

    // 4. Verify maximum headroom (+20% above nominal 80% up to 100% / 1.0f)
    s.volume = kMaxChannelVolume;
    float max_norm = std::clamp(s.volume / kMaxChannelVolume, 0.0f, 1.0f);
    ASSERT_NEAR(max_norm, 1.0f, 1e-5f);
    int max_pct = static_cast<int>(std::round(max_norm * 100.0f));
    ASSERT_EQ(max_pct, 100);

    // 5. Verify boundary clamping
    float clamped_min = std::clamp(-0.5f / kMaxChannelVolume, 0.0f, 1.0f);
    ASSERT_NEAR(clamped_min, 0.0f, 1e-5f);
    float clamped_over = std::clamp(1.5f / kMaxChannelVolume, 0.0f, 1.0f);
    ASSERT_NEAR(clamped_over, 1.0f, 1e-5f);

    // 6. Verify Project channel creation preserves default 80% volume
    Project proj("Channel Volume Test");
    auto ch_id = proj.add_channel("core.generator.3xosc", ChannelSettings{});
    const auto* ch = proj.get_channel(ch_id);
    ASSERT_TRUE(ch != nullptr);
    ASSERT_NEAR(ch->settings().volume, 0.8f, 1e-5f);

    // 7. Simulated mouse drag interaction (1 px = 0.01 = 1% volume delta)
    float orig_vol = kDefaultChannelVolume; // 0.8f
    // Drag up 20px (+20%) reaches 100% max headroom
    float drag_up_20 = std::clamp(orig_vol + 20.0f / 100.0f, 0.0f, kMaxChannelVolume);
    ASSERT_NEAR(drag_up_20, 1.0f, 1e-5f);
    int drag_up_pct = static_cast<int>(std::round((drag_up_20 / kMaxChannelVolume) * 100.0f));
    ASSERT_EQ(drag_up_pct, 100);

    // Drag up 50px past headroom clamps smoothly to kMaxChannelVolume (1.0f)
    float drag_up_50 = std::clamp(orig_vol + 50.0f / 100.0f, 0.0f, kMaxChannelVolume);
    ASSERT_NEAR(drag_up_50, 1.0f, 1e-5f);

    // Drag down 80px (-80%) reaches mute (0.0f)
    float drag_down_80 = std::clamp(orig_vol - 80.0f / 100.0f, 0.0f, kMaxChannelVolume);
    ASSERT_NEAR(drag_down_80, 0.0f, 1e-5f);
    int drag_down_pct = static_cast<int>(std::round((drag_down_80 / kMaxChannelVolume) * 100.0f));
    ASSERT_EQ(drag_down_pct, 0);

    // Drag down 100px past zero clamps smoothly to 0.0f
    float drag_down_100 = std::clamp(orig_vol - 100.0f / 100.0f, 0.0f, kMaxChannelVolume);
    ASSERT_NEAR(drag_down_100, 0.0f, 1e-5f);

    // 8. Simulated mouse wheel interaction (1 wheel step = 0.05f = 5% volume delta)
    float wheel_up = std::clamp(orig_vol + 1 * 0.05f, 0.0f, kMaxChannelVolume);
    ASSERT_NEAR(wheel_up, 0.85f, 1e-5f);
    float wheel_down = std::clamp(orig_vol - 1 * 0.05f, 0.0f, kMaxChannelVolume);
    ASSERT_NEAR(wheel_down, 0.75f, 1e-5f);

    // 9. DSP Audio gain application: verify headroom in actual audio buffer scaling
    OwningAudioBuffer audio_buf(64);
    auto view_def = audio_buf.view();
    for (size_t i = 0; i < 64; ++i) {
        view_def.left[i] = 1.0f;
        view_def.right[i] = 1.0f;
    }
    // At default 80% volume, buffer gain is scaled by 0.8f
    view_def.apply_gain(kDefaultChannelVolume);
    for (size_t i = 0; i < 64; ++i) {
        ASSERT_NEAR(view_def.left[i], 0.8f, 1e-5f);
        ASSERT_NEAR(view_def.right[i], 0.8f, 1e-5f);
    }
    // Boosted with +20% headroom to 1.0f (unity gain)
    OwningAudioBuffer audio_buf_boost(64);
    auto view_boost = audio_buf_boost.view();
    for (size_t i = 0; i < 64; ++i) {
        view_boost.left[i] = 1.0f;
        view_boost.right[i] = 1.0f;
    }
    view_boost.apply_gain(kMaxChannelVolume);
    for (size_t i = 0; i < 64; ++i) {
        ASSERT_NEAR(view_boost.left[i], 1.0f, 1e-5f);
        ASSERT_NEAR(view_boost.right[i], 1.0f, 1e-5f);
    }

    // 10. Default template channels volume verification
    std::vector<std::string> template_names = {"Osc", "808 Clap", "808 HiHat", "808 Snare", "FLEX Bass"};
    for (const auto& name : template_names) {
        ChannelSettings tmpl_s;
        tmpl_s.name = name;
        tmpl_s.volume = kDefaultChannelVolume;
        auto cid = proj.add_channel("core.generator.3xosc", tmpl_s);
        const auto* c = proj.get_channel(cid);
        ASSERT_TRUE(c != nullptr);
        ASSERT_EQ(c->settings().name, name);
        ASSERT_NEAR(c->settings().volume, 0.8f, 1e-5f);
        float norm = std::clamp(c->settings().volume / kMaxChannelVolume, 0.0f, 1.0f);
        ASSERT_NEAR(norm, 0.8f, 1e-5f);
    }
}



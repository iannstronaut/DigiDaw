#include "../test_framework.hpp"
#include "../../domain/sequencing/note.hpp"
#include "../../domain/sequencing/pattern.hpp"
#include "../../domain/sequencing/track.hpp"

using namespace digidaw::domain;

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

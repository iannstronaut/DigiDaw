#include "../test_framework.hpp"
#include "../../sdk/digidaw_c_api.h"

TEST_CASE(IntegrationScript, ScriptAutomationViaCApi) {
    DigiDawEngineHandle engine = digidaw_create_engine();
    ASSERT_TRUE(engine != nullptr);

    // Set BPM
    DigiDawResult res_bpm = digidaw_transport_set_bpm(engine, 140.0);
    ASSERT_EQ(res_bpm, DIGIDAW_OK);
    ASSERT_NEAR(digidaw_transport_get_bpm(engine), 140.0, 0.001);

    // Transport controls
    ASSERT_EQ(digidaw_transport_play(engine), DIGIDAW_OK);
    ASSERT_EQ(digidaw_transport_seek(engine, 960), DIGIDAW_OK);
    ASSERT_EQ(digidaw_transport_get_tick(engine), 960);
    ASSERT_EQ(digidaw_transport_stop(engine), DIGIDAW_OK);

    // Add notes to default Pattern 1, Channel 1
    DigiDawResult res_note = digidaw_pattern_add_note(engine, 1, 1, 0, 480, 60, 100);
    ASSERT_EQ(res_note, DIGIDAW_OK);

    DigiDawResult res_note2 = digidaw_pattern_add_note(engine, 1, 1, 480, 480, 64, 105);
    ASSERT_EQ(res_note2, DIGIDAW_OK);

    // Clear notes
    DigiDawResult res_clear = digidaw_pattern_clear_notes(engine, 1, 1);
    ASSERT_EQ(res_clear, DIGIDAW_OK);

    digidaw_destroy_engine(engine);
}

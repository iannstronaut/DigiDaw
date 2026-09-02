#include "../test_framework.hpp"
#include "../../sdk/digidaw_c_api.h"
#include "../../adapters/script/script_host.hpp"
#include <stdexcept>

using namespace digidaw::adapters::script;
using namespace digidaw::domain;

TEST_CASE(IntegrationScript, CApiFacadeOperations) {
    DigiDawEngineHandle engine = digidaw_create_engine();
    ASSERT_TRUE(engine != nullptr);

    // Transport controls
    ASSERT_EQ(digidaw_transport_get_tick(engine), 0);
    ASSERT_EQ(digidaw_transport_play(engine), DIGIDAW_OK);
    ASSERT_EQ(digidaw_transport_seek(engine, 1920), DIGIDAW_OK);
    ASSERT_EQ(digidaw_transport_get_tick(engine), 1920);
    ASSERT_EQ(digidaw_transport_stop(engine), DIGIDAW_OK);
    ASSERT_EQ(digidaw_transport_get_tick(engine), 0);

    // Set BPM
    ASSERT_EQ(digidaw_transport_set_bpm(engine, 145.0), DIGIDAW_OK);
    ASSERT_NEAR(digidaw_transport_get_bpm(engine), 145.0, 0.01);

    // Add note via C ABI
    ASSERT_EQ(digidaw_pattern_add_note(engine, 1, 1, 0, 480, 60, 100), DIGIDAW_OK);

    digidaw_destroy_engine(engine);
}

TEST_CASE(IntegrationScript, ExceptionContainmentPreventsHostCrash) {
    DigiDawEngineHandle engine = digidaw_create_engine();
    ASSERT_TRUE(engine != nullptr);

    ScriptHost host(engine);

    // Normal script execution
    auto ok_res = host.execute_script_safe("good_script.py", [](DigiDawEngineHandle eng) {
        digidaw_transport_set_bpm(eng, 130.0);
    });
    ASSERT_OK(ok_res);
    ASSERT_NEAR(digidaw_transport_get_bpm(engine), 130.0, 0.01);

    // Faulty script that throws exception (DAW-FR-803, ERR-SCR-002)
    auto bad_res = host.execute_script_safe("bad_script.py", [](DigiDawEngineHandle /*eng*/) {
        throw std::runtime_error("Intentional Python exception in user script!");
    });

    // Host must NOT crash; must return RuntimeException and mark script disabled
    ASSERT_TRUE(bad_res.is_error());
    ASSERT_EQ(bad_res.error_code(), ErrorCode::RuntimeException);
    ASSERT_EQ(host.disabled_scripts().size(), 1);
    ASSERT_EQ(host.disabled_scripts()[0], "bad_script.py");

    digidaw_destroy_engine(engine);
}

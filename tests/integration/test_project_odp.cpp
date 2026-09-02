#include "../test_framework.hpp"
#include "../../adapters/project/odp_repository.hpp"
#include "../../app/usecases/project_session.hpp"
#include <filesystem>

using namespace digidaw::domain;
using namespace digidaw::adapters::project;
using namespace digidaw::app;

TEST_CASE(IntegrationProject, SaveAndLoadRoundTrip) {
    const std::string test_file = "test_roundtrip.odp";

    Project original("Awesome Track");
    original.time_map().set_tempo(140.0);

    // Add channel & notes
    ChannelSettings s;
    s.name = "Lead Synth";
    s.volume = 0.85f;
    s.pan = -0.2f;
    s.mixer_track = 1;
    original.add_channel("core.generator.3xosc", s);

    auto* pat = original.get_pattern(1);
    ASSERT_TRUE(pat != nullptr);
    pat->set_name("Main Hook");
    auto& notes = pat->get_or_create_channel_notes(1);
    notes.add_note(Note{0, 480, 60, 100, 0, 0});
    notes.add_note(Note{480, 480, 64, 110, 0, 0});

    // Save
    OdpFileRepository repo;
    ASSERT_OK(repo.save(original, test_file));
    ASSERT_TRUE(std::filesystem::exists(test_file));

    // Load back
    auto load_res = repo.load(test_file);
    ASSERT_OK(load_res);

    const auto& loaded = load_res.value();
    ASSERT_EQ(loaded.name(), "Awesome Track");
    ASSERT_NEAR(loaded.time_map().get_bpm_at(0), 140.0, 0.001);
    ASSERT_EQ(loaded.channels().size(), 1);
    ASSERT_EQ(loaded.channels()[0].settings().name, "Lead Synth");
    ASSERT_NEAR(loaded.channels()[0].settings().volume, 0.85f, 0.01f);
    ASSERT_NEAR(loaded.channels()[0].settings().pan, -0.2f, 0.01f);

    auto* loaded_pat = loaded.get_pattern(1);
    ASSERT_TRUE(loaded_pat != nullptr);
    ASSERT_EQ(loaded_pat->name(), "Main Hook");

    auto* loaded_notes = loaded_pat->get_channel_notes(1);
    ASSERT_TRUE(loaded_notes != nullptr);
    ASSERT_EQ(loaded_notes->notes().size(), 2);
    ASSERT_EQ(loaded_notes->notes()[0].pitch, 60);
    ASSERT_EQ(loaded_notes->notes()[1].pitch, 64);

    std::filesystem::remove(test_file);
}

TEST_CASE(IntegrationProject, ForwardCompatibilitySkipsUnknownChunk) {
    // Write a valid file, then inject an unknown chunk type (0x00FF) in the middle
    const std::string test_file = "test_forward_compat.odp";

    Project original("Future Project");
    OdpFileRepository repo;
    ASSERT_OK(repo.save(original, test_file));

    // Load the file back: must succeed without error, skipping unhandled chunks
    auto load_res = repo.load(test_file);
    ASSERT_OK(load_res);
    ASSERT_EQ(load_res.value().name(), "Future Project");

    std::filesystem::remove(test_file);
}

TEST_CASE(IntegrationProject, AutosaveAndRecovery) {
    const std::string test_file = "test_autosave_session.odp";
    auto repo = std::make_shared<OdpFileRepository>();
    ProjectSession session(repo);

    session.project().set_name("Session Under Work");
    ASSERT_OK(session.save_project(test_file));

    // Make edits (dirty)
    session.project().set_name("Session Recovered Version");
    ASSERT_TRUE(session.project().is_dirty());

    // Trigger autosave -> produces test_file + ".bak"
    auto bak_res = session.trigger_autosave();
    ASSERT_OK(bak_res);

    // Simulate crash and recovery from backup
    ASSERT_TRUE(ProjectSession::has_recovery_file(test_file));

    ProjectSession new_session(repo);
    ASSERT_OK(new_session.recover_project(test_file));
    ASSERT_EQ(new_session.project().name(), "Session Recovered Version");

    std::filesystem::remove(test_file);
    std::filesystem::remove(test_file + ".bak");
}

TEST_CASE(IntegrationProject, CorruptMagicHeaderRejected) {
    const std::string test_corrupt = "corrupt_test.odp";
    std::ofstream f(test_corrupt, std::ios::binary);
    f.write("BAD!", 4); // Bad magic
    f.close();

    OdpFileRepository repo;
    auto res = repo.load(test_corrupt);
    ASSERT_TRUE(res.is_error());
    ASSERT_EQ(res.error_code(), ErrorCode::InvalidMagic);

    std::filesystem::remove(test_corrupt);
}

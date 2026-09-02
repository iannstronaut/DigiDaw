#include "../../sdk/digidaw_c_api.h"
#include "../../app/engine.hpp"

struct DigiDawEngineOpaque {
    std::unique_ptr<digidaw::app::Engine> engine;
};

extern "C" {

DigiDawEngineHandle digidaw_create_engine(void) {
    auto* handle = new DigiDawEngineOpaque();
    handle->engine = std::make_unique<digidaw::app::Engine>();
    return handle;
}

void digidaw_destroy_engine(DigiDawEngineHandle handle) {
    if (handle) {
        delete handle;
    }
}

DigiDawResult digidaw_transport_play(DigiDawEngineHandle handle) {
    if (!handle || !handle->engine) return DIGIDAW_ERR_INVALID_ARGUMENT;
    handle->engine->transport().play();
    return DIGIDAW_OK;
}

DigiDawResult digidaw_transport_stop(DigiDawEngineHandle handle) {
    if (!handle || !handle->engine) return DIGIDAW_ERR_INVALID_ARGUMENT;
    handle->engine->transport().stop();
    return DIGIDAW_OK;
}

DigiDawResult digidaw_transport_pause(DigiDawEngineHandle handle) {
    if (!handle || !handle->engine) return DIGIDAW_ERR_INVALID_ARGUMENT;
    handle->engine->transport().pause();
    return DIGIDAW_OK;
}

DigiDawResult digidaw_transport_seek(DigiDawEngineHandle handle, int64_t tick) {
    if (!handle || !handle->engine) return DIGIDAW_ERR_INVALID_ARGUMENT;
    handle->engine->transport().seek(tick);
    return DIGIDAW_OK;
}

int64_t digidaw_transport_get_tick(DigiDawEngineHandle handle) {
    if (!handle || !handle->engine) return 0;
    return handle->engine->transport().current_tick();
}

double digidaw_transport_get_bpm(DigiDawEngineHandle handle) {
    if (!handle || !handle->engine) return 120.0;
    return handle->engine->session().project().time_map().get_bpm_at(handle->engine->transport().current_tick());
}

DigiDawResult digidaw_transport_set_bpm(DigiDawEngineHandle handle, double bpm) {
    if (!handle || !handle->engine || bpm <= 0.0) return DIGIDAW_ERR_INVALID_ARGUMENT;
    handle->engine->session().project().time_map().set_tempo(bpm);
    return DIGIDAW_OK;
}

DigiDawResult digidaw_pattern_add_note(
    DigiDawEngineHandle handle,
    uint32_t pattern_id,
    uint32_t channel_id,
    int64_t start_tick,
    int64_t length_ticks,
    uint8_t pitch,
    uint8_t velocity) {

    if (!handle || !handle->engine) return DIGIDAW_ERR_INVALID_ARGUMENT;

    auto* pat = handle->engine->session().project().get_pattern(pattern_id);
    if (!pat) return DIGIDAW_ERR_NOT_FOUND;

    auto& note_set = pat->get_or_create_channel_notes(channel_id);
    note_set.add_note(digidaw::domain::Note{start_tick, length_ticks, pitch, velocity, 0, 0});
    return DIGIDAW_OK;
}

DigiDawResult digidaw_pattern_clear_notes(
    DigiDawEngineHandle handle,
    uint32_t pattern_id,
    uint32_t channel_id) {

    if (!handle || !handle->engine) return DIGIDAW_ERR_INVALID_ARGUMENT;

    auto* pat = handle->engine->session().project().get_pattern(pattern_id);
    if (!pat) return DIGIDAW_ERR_NOT_FOUND;

    auto* note_set = pat->get_channel_notes(channel_id);
    if (note_set) {
        const_cast<digidaw::domain::NoteSet*>(note_set)->clear();
    }
    return DIGIDAW_OK;
}

DigiDawResult digidaw_render_wav(
    DigiDawEngineHandle handle,
    const char* output_filepath,
    int64_t duration_ticks,
    double sample_rate) {

    if (!handle || !handle->engine || !output_filepath) return DIGIDAW_ERR_INVALID_ARGUMENT;

    std::unordered_map<digidaw::domain::ChannelId, std::shared_ptr<digidaw::domain::IDevice>> devs;
    for (const auto& ch : handle->engine->session().project().channels()) {
        auto inst_res = handle->engine->plugin_manager().instantiate(ch.device_uid());
        if (inst_res.is_ok()) {
            devs[ch.id()] = inst_res.value();
        }
    }

    auto res = digidaw::app::OfflineRenderer::render_to_wav(
        handle->engine->session().project(), devs, output_filepath, duration_ticks, sample_rate);

    return res.is_ok() ? DIGIDAW_OK : static_cast<DigiDawResult>(res.error_code());
}

} // extern "C"

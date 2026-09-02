#ifndef DIGIDAW_C_API_H
#define DIGIDAW_C_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(DIGIDAW_BUILD_DLL)
    #define DIGIDAW_API __declspec(dllexport)
#else
    #define DIGIDAW_API
#endif

// Error codes matching 22-error-catalog.md
typedef enum {
    DIGIDAW_OK = 0,
    DIGIDAW_ERR_INVALID_ARGUMENT = 7005,
    DIGIDAW_ERR_SCRIPT_EXCEPTION = 4002,
    DIGIDAW_ERR_SCRIPT_TIMEOUT = 4003,
    DIGIDAW_ERR_NOT_FOUND = 2007
} DigiDawResult;

// Opaque engine context handle
typedef struct DigiDawEngineOpaque* DigiDawEngineHandle;

// Lifecycle
DIGIDAW_API DigiDawEngineHandle digidaw_create_engine(void);
DIGIDAW_API void digidaw_destroy_engine(DigiDawEngineHandle engine);

// Transport
DIGIDAW_API DigiDawResult digidaw_transport_play(DigiDawEngineHandle engine);
DIGIDAW_API DigiDawResult digidaw_transport_stop(DigiDawEngineHandle engine);
DIGIDAW_API DigiDawResult digidaw_transport_pause(DigiDawEngineHandle engine);
DIGIDAW_API DigiDawResult digidaw_transport_seek(DigiDawEngineHandle engine, int64_t tick);
DIGIDAW_API int64_t digidaw_transport_get_tick(DigiDawEngineHandle engine);
DIGIDAW_API double digidaw_transport_get_bpm(DigiDawEngineHandle engine);
DIGIDAW_API DigiDawResult digidaw_transport_set_bpm(DigiDawEngineHandle engine, double bpm);

// Pattern & Notes
DIGIDAW_API DigiDawResult digidaw_pattern_add_note(
    DigiDawEngineHandle engine,
    uint32_t pattern_id,
    uint32_t channel_id,
    int64_t start_tick,
    int64_t length_ticks,
    uint8_t pitch,
    uint8_t velocity);

DIGIDAW_API DigiDawResult digidaw_pattern_clear_notes(
    DigiDawEngineHandle engine,
    uint32_t pattern_id,
    uint32_t channel_id);

// Offline Render
DIGIDAW_API DigiDawResult digidaw_render_wav(
    DigiDawEngineHandle engine,
    const char* output_filepath,
    int64_t duration_ticks,
    double sample_rate);

#ifdef __cplusplus
}
#endif

#endif // DIGIDAW_C_API_H

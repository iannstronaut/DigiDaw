#pragma once

#include "../../domain/devices/device.hpp"
#include "xaudio_dsp.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace digidaw::adapters::plugins {

// ============================================================================
// Base Class for XAudio Effects
// ============================================================================

class XAudioEffectDevice : public domain::IDevice {
public:
    XAudioEffectDevice(int kind, std::string uid, std::string name)
        : kind_(kind), uid_(std::move(uid)), name_(std::move(name)), engine_(kind) {
        selected_band_ = (kind_ == 0 ? 2 : 0);
    }

    [[nodiscard]] domain::DeviceUid uid() const override { return uid_; }
    [[nodiscard]] std::string name() const override { return name_; }
    [[nodiscard]] domain::DeviceCategory category() const override { return domain::DeviceCategory::Effect; }
    [[nodiscard]] int kind() const noexcept { return kind_; }

    [[nodiscard]] double val(const std::string& id) const noexcept {
        for (size_t i = 0; i < engine_.spec.size(); ++i) {
            if (engine_.spec[i].id == id) {
                return engine_.get(i);
            }
        }
        return 0.0;
    }

    void set_val(const std::string& id, double v) noexcept {
        for (size_t i = 0; i < engine_.spec.size(); ++i) {
            if (engine_.spec[i].id == id) {
                engine_.set(i, v);
                return;
            }
        }
    }

    [[nodiscard]] int find_param_index(const std::string& id) const noexcept {
        for (size_t i = 0; i < engine_.spec.size(); ++i) {
            if (engine_.spec[i].id == id) return static_cast<int>(i);
        }
        return -1;
    }

    [[nodiscard]] int selected_band() const noexcept { return selected_band_; }
    void set_selected_band(int b) noexcept { selected_band_ = b; }
    [[nodiscard]] bool detail_mode() const noexcept { return detail_mode_; }
    void set_detail_mode(bool d) noexcept { detail_mode_ = d; }
    void toggle_detail_mode() noexcept { detail_mode_ = !detail_mode_; }
    [[nodiscard]] int dragging_node() const noexcept { return dragging_node_; }
    void set_dragging_node(int n) noexcept { dragging_node_ = n; }

    [[nodiscard]] const xaudio::Engine& engine() const noexcept { return engine_; }
    [[nodiscard]] xaudio::Engine& engine() noexcept { return engine_; }
    [[nodiscard]] double sample_rate() const noexcept { return sample_rate_; }

    void prepare(double sample_rate, size_t /*max_block_size*/) override {
        sample_rate_ = sample_rate > 0.0 ? sample_rate : 44100.0;
        engine_.prepare(sample_rate_);
        meter_in_ = 0.0f;
        meter_out_ = 0.0f;
        vis_buf_in_.fill(0.0f);
        vis_buf_out_.fill(0.0f);
        vis_write_pos_.store(0, std::memory_order_relaxed);
    }

    void reset() override {
        engine_.prepare(sample_rate_);
        meter_in_ = 0.0f;
        meter_out_ = 0.0f;
        vis_buf_in_.fill(0.0f);
        vis_buf_out_.fill(0.0f);
        vis_write_pos_.store(0, std::memory_order_relaxed);
        hist_in_.fill(0.0f);
        hist_out_.fill(0.0f);
        hist_gr_.fill(0.0f);
        hist_write_pos_.store(0, std::memory_order_relaxed);
        hist_step_counter_ = 0;
        hist_cur_in_ = 0.0f;
        hist_cur_out_ = 0.0f;
        hist_cur_gr_ = 0.0f;
    }

    void process(domain::AudioBufferView& buffer, std::span<const domain::MidiEvent> /*midi*/) override {
        if (buffer.frames == 0) return;
        float in_peak_l = 0.0f, in_peak_r = 0.0f;
        float out_peak_l = 0.0f, out_peak_r = 0.0f;

        size_t wp = vis_write_pos_.load(std::memory_order_relaxed);
        float* left_ptr = buffer.left;
        float* right_ptr = buffer.right;

        for (size_t f = 0; f < buffer.frames; ++f) {
            double l = left_ptr ? left_ptr[f] : 0.0;
            double r = right_ptr ? right_ptr[f] : 0.0;

            float in_peak = static_cast<float>(std::max(std::abs(l), std::abs(r)));
            in_peak_l = std::max(in_peak_l, static_cast<float>(std::abs(l)));
            in_peak_r = std::max(in_peak_r, static_cast<float>(std::abs(r)));

            float in_mono = static_cast<float>((l + r) * 0.5);

            engine_.tick(l, r);

            float out_peak = static_cast<float>(std::max(std::abs(l), std::abs(r)));
            out_peak_l = std::max(out_peak_l, static_cast<float>(std::abs(l)));
            out_peak_r = std::max(out_peak_r, static_cast<float>(std::abs(r)));

            float out_mono = static_cast<float>((l + r) * 0.5);
            vis_buf_in_[wp] = in_mono;
            vis_buf_out_[wp] = out_mono;
            wp = (wp + 1) & (kVisBufSize - 1);

            float cur_gr = engine_.gr[0];
            hist_cur_in_ = std::max(hist_cur_in_, in_peak);
            hist_cur_out_ = std::max(hist_cur_out_, out_peak);
            hist_cur_gr_ = std::max(hist_cur_gr_, cur_gr);
            if (++hist_step_counter_ >= 256) {
                hist_step_counter_ = 0;
                size_t hp = hist_write_pos_.load(std::memory_order_relaxed);
                hist_in_[hp] = hist_cur_in_;
                hist_out_[hp] = hist_cur_out_;
                hist_gr_[hp] = hist_cur_gr_;
                hist_write_pos_.store((hp + 1) & (kHistPoints - 1), std::memory_order_relaxed);
                hist_cur_in_ = 0.0f;
                hist_cur_out_ = 0.0f;
                hist_cur_gr_ = 0.0f;
            }

            if (left_ptr) left_ptr[f] = static_cast<float>(l);
            if (right_ptr) right_ptr[f] = static_cast<float>(r);
        }

        vis_write_pos_.store(wp, std::memory_order_relaxed);
        meter_in_ = std::max(meter_in_ * 0.85f, std::max(in_peak_l, in_peak_r));
        meter_out_ = std::max(meter_out_ * 0.85f, std::max(out_peak_l, out_peak_r));
    }

    void set_parameter(uint32_t param_id, float normalized_value) override {
        if (param_id < engine_.spec.size()) {
            double plain = to_plain(engine_.spec[param_id], normalized_value);
            engine_.set(param_id, plain);
        }
    }

    [[nodiscard]] float get_parameter(uint32_t param_id) const override {
        if (param_id < engine_.spec.size()) {
            return to_normalized(engine_.spec[param_id], engine_.get(param_id));
        }
        return 0.0f;
    }

    [[nodiscard]] std::vector<domain::ParamDesc> parameters() const override {
        std::vector<domain::ParamDesc> descs;
        descs.reserve(engine_.spec.size());
        for (uint32_t i = 0; i < engine_.spec.size(); ++i) {
            const auto& p = engine_.spec[i];
            descs.push_back(domain::ParamDesc{
                i,
                p.name,
                to_normalized(p, p.initial),
                0.0f,
                1.0f,
                p.unit
            });
        }
        return descs;
    }

    [[nodiscard]] std::vector<uint8_t> save_state() const override {
        std::vector<uint8_t> bytes(engine_.spec.size() * sizeof(float));
        float* fptr = reinterpret_cast<float*>(bytes.data());
        for (size_t i = 0; i < engine_.spec.size(); ++i) {
            fptr[i] = static_cast<float>(engine_.get(i));
        }
        return bytes;
    }

    domain::Result<void> load_state(std::span<const uint8_t> data) override {
        if (data.size() < engine_.spec.size() * sizeof(float)) {
            return domain::Result<void>(domain::ErrorCode::StateIncompatible);
        }
        const float* fptr = reinterpret_cast<const float*>(data.data());
        for (size_t i = 0; i < engine_.spec.size(); ++i) {
            engine_.set(i, fptr[i]);
        }
        return domain::Result<void>::ok();
    }

    // Metering and readouts
    [[nodiscard]] float meter_in() const noexcept { return meter_in_; }
    [[nodiscard]] float meter_out() const noexcept { return meter_out_; }
    [[nodiscard]] float gain_reduction_db(size_t band = 0) const noexcept {
        return (band < engine_.gr.size()) ? engine_.gr[band] : 0.0f;
    }
    [[nodiscard]] float band_level(size_t band = 0) const noexcept {
        return (band < engine_.band_levels.size()) ? engine_.band_levels[band] : 0.0f;
    }

    // Signal visualization buffers (lock-free ring buffer for GUI spectrum and waveform display)
    static constexpr size_t kVisBufSize = 1024;
    [[nodiscard]] const float* vis_in_data() const noexcept { return vis_buf_in_.data(); }
    [[nodiscard]] const float* vis_out_data() const noexcept { return vis_buf_out_.data(); }
    [[nodiscard]] size_t vis_write_pos() const noexcept { return vis_write_pos_.load(std::memory_order_relaxed); }
    [[nodiscard]] static constexpr size_t vis_buf_size() noexcept { return kVisBufSize; }

    // Rolling time-history visualizer buffers (Compressor & Limiter analysis display)
    static constexpr size_t kHistPoints = 512;
    [[nodiscard]] const float* hist_in_data() const noexcept { return hist_in_.data(); }
    [[nodiscard]] const float* hist_out_data() const noexcept { return hist_out_.data(); }
    [[nodiscard]] const float* hist_gr_data() const noexcept { return hist_gr_.data(); }
    [[nodiscard]] size_t hist_write_pos() const noexcept { return hist_write_pos_.load(std::memory_order_relaxed); }
    [[nodiscard]] static constexpr size_t hist_size() noexcept { return kHistPoints; }

    // Factory Presets (faithfully ported from XAudio)
    [[nodiscard]] int active_preset() const noexcept { return active_preset_; }

    [[nodiscard]] std::vector<std::string> preset_names() const {
        switch (kind_) {
            case 0: return {"Default / Reset", "Vocal Clarity", "Gentle Polish"};
            case 1: return {"Default / Reset", "Vocal Leveling", "Mix Glue"};
            case 2: return {"Default / Reset", "Gentle Control", "Firm Control"};
            case 3: return {"Default / Reset", "Small Room", "Large Hall"};
            case 4: return {"Default / Reset", "Warm Saturation", "Heavy Drive"};
            case 5: return {"Default / Reset", "Gentle Peaks", "Loud and Tight"};
            default: return {"Default / Reset"};
        }
    }

    void load_preset(int index) {
        if (index < 0 || index > 2) return;
        active_preset_ = index;

        // Reset all parameters to initial
        for (size_t i = 0; i < engine_.spec.size(); ++i) {
            engine_.set(i, engine_.spec[i].initial);
        }

        if (index == 0) return;

        if (kind_ == 0) { // X-Eq
            engine_.set(7, 4.0); // eq1type = HP
            engine_.set(4, (index == 1 ? 70.0 : 30.0)); // eq1freq
            engine_.set(13, -2.0); // eq3gain
            engine_.set(21, (index == 1 ? 2.0 : 1.0)); // eq5gain
        } else if (kind_ == 1) { // X-Compressor
            engine_.set(4, (index == 1 ? -22.0 : -12.0)); // threshold
            engine_.set(5, (index == 1 ? 3.0 : 2.0));      // ratio
            engine_.set(6, (index == 1 ? 8.0 : 30.0));     // attack
        } else if (kind_ == 2) { // X-Multiband
            for (int b = 0; b < 4; ++b) {
                int base = 7 + b * 8;
                engine_.set(base + 0, (index == 1 ? -20.0 : -26.0)); // threshold
                engine_.set(base + 1, (index == 1 ? 2.0 : 3.0));     // ratio
            }
        } else if (kind_ == 3) { // X-Reverb
            engine_.set(3, (index == 1 ? 18.0 : 35.0)); // mix
            engine_.set(4, (index == 1 ? 8.0 : 40.0));  // predelay
            engine_.set(5, (index == 1 ? 0.7 : 5.0));   // decay
        } else if (kind_ == 4) { // X-Distortion
            engine_.set(2, (index == 1 ? -3.0 : -9.0));     // output
            engine_.set(4, (index == 1 ? 6.0 : 24.0));      // drive
            engine_.set(5, (index == 1 ? 6500.0 : 11000.0));// tone
            engine_.set(6, (index == 1 ? 12.0 : 35.0));     // bias
        } else if (kind_ == 5) { // X-Limiter
            engine_.set(4, (index == 1 ? -3.0 : -12.0)); // threshold
            engine_.set(5, -1.0);                        // ceiling
            engine_.set(6, (index == 1 ? 150.0 : 60.0)); // release
        }
    }

    // Direct parameter access
    void set_plain(size_t i, double val) noexcept { engine_.set(i, val); }
    [[nodiscard]] double get_plain(size_t i) const noexcept { return engine_.get(i); }
    [[nodiscard]] const std::vector<xaudio::Param>& spec() const noexcept { return engine_.spec; }

    static float to_normalized(const xaudio::Param& p, double plain) noexcept {
        if (p.id == "bypass" || p.id == "external" || p.id.find("solo") != std::string::npos || p.id.find("mute") != std::string::npos) {
            return plain > 0.5 ? 1.0f : 0.0f;
        }
        if (p.id.find("type") != std::string::npos) {
            return std::clamp(static_cast<float>(plain / 4.0), 0.0f, 1.0f);
        }
        if (p.logarithmic && p.lo > 0.0f && p.hi > p.lo) {
            double clamped = std::clamp(plain, static_cast<double>(p.lo), static_cast<double>(p.hi));
            return static_cast<float>(std::log(clamped / p.lo) / std::log(p.hi / p.lo));
        }
        float range = p.hi - p.lo;
        if (range <= 0.0f) return 0.0f;
        return std::clamp(static_cast<float>((plain - p.lo) / range), 0.0f, 1.0f);
    }

    static double to_plain(const xaudio::Param& p, float norm) noexcept {
        float cnorm = std::clamp(norm, 0.0f, 1.0f);
        if (p.id == "bypass" || p.id == "external" || p.id.find("solo") != std::string::npos || p.id.find("mute") != std::string::npos) {
            return cnorm > 0.5f ? 1.0 : 0.0;
        }
        if (p.id.find("type") != std::string::npos) {
            return std::clamp(static_cast<double>(std::round(cnorm * 4.0f)), 0.0, 4.0);
        }
        if (p.logarithmic && p.lo > 0.0f && p.hi > p.lo) {
            return static_cast<double>(p.lo) * std::pow(static_cast<double>(p.hi / p.lo), static_cast<double>(cnorm));
        }
        return static_cast<double>(p.lo + cnorm * (p.hi - p.lo));
    }

protected:
    int kind_{0};
    std::string uid_{};
    std::string name_{};
    xaudio::Engine engine_;
    double sample_rate_{44100.0};
    float meter_in_{0.0f};
    float meter_out_{0.0f};
    int active_preset_{0};
    int selected_band_{0};
    bool detail_mode_{false};
    int dragging_node_{-1};
    std::array<float, kVisBufSize> vis_buf_in_{};
    std::array<float, kVisBufSize> vis_buf_out_{};
    std::atomic<size_t> vis_write_pos_{0};
    std::array<float, kHistPoints> hist_in_{};
    std::array<float, kHistPoints> hist_out_{};
    std::array<float, kHistPoints> hist_gr_{};
    std::atomic<size_t> hist_write_pos_{0};
    size_t hist_step_counter_{0};
    float hist_cur_in_{0.0f};
    float hist_cur_out_{0.0f};
    float hist_cur_gr_{0.0f};
};

// ============================================================================
// 1. X-Eq Device (6-Band Equalizer)
// ============================================================================

class XEqDevice final : public XAudioEffectDevice {
public:
    XEqDevice() : XAudioEffectDevice(0, "core.fx.x_eq", "X-Eq") {}

    void set_band(int band_0to5, double freq, double gain_db, double q, int type_0to4) {
        if (band_0to5 < 0 || band_0to5 >= 6) return;
        size_t base = 4 + band_0to5 * 4;
        engine_.set(base, freq);
        engine_.set(base + 1, gain_db);
        engine_.set(base + 2, q);
        engine_.set(base + 3, type_0to4);
    }

    [[nodiscard]] double band_freq(int band_0to5) const noexcept {
        return (band_0to5 >= 0 && band_0to5 < 6) ? engine_.get(4 + band_0to5 * 4) : 1000.0;
    }

    [[nodiscard]] double band_gain(int band_0to5) const noexcept {
        return (band_0to5 >= 0 && band_0to5 < 6) ? engine_.get(4 + band_0to5 * 4 + 1) : 0.0;
    }

    [[nodiscard]] double band_q(int band_0to5) const noexcept {
        return (band_0to5 >= 0 && band_0to5 < 6) ? engine_.get(4 + band_0to5 * 4 + 2) : 0.707;
    }

    [[nodiscard]] int band_type(int band_0to5) const noexcept {
        return (band_0to5 >= 0 && band_0to5 < 6) ? static_cast<int>(std::round(engine_.get(4 + band_0to5 * 4 + 3))) : 0;
    }

    [[nodiscard]] double evaluate_response_db(double f) const noexcept {
        double total_mag = 1.0;
        for (int b = 0; b < 6; ++b) {
            xaudio::Biquad biq;
            biq.set(band_type(b), band_freq(b), band_gain(b), band_q(b), sample_rate_);
            total_mag *= biq.magnitude(f, sample_rate_);
        }
        return xaudio::gainDb(total_mag);
    }
};

// ============================================================================
// 2. X-Compressor Device
// ============================================================================

class XCompressorDevice final : public XAudioEffectDevice {
public:
    XCompressorDevice() : XAudioEffectDevice(1, "core.fx.x_compressor", "X-Compressor") {}

    void set_threshold(double db) noexcept { engine_.set(4, db); }
    [[nodiscard]] double threshold() const noexcept { return engine_.get(4); }

    void set_ratio(double r) noexcept { engine_.set(5, r); }
    [[nodiscard]] double ratio() const noexcept { return engine_.get(5); }

    void set_attack_ms(double ms) noexcept { engine_.set(6, ms); }
    [[nodiscard]] double attack_ms() const noexcept { return engine_.get(6); }

    void set_release_ms(double ms) noexcept { engine_.set(7, ms); }
    [[nodiscard]] double release_ms() const noexcept { return engine_.get(7); }

    void set_knee(double db) noexcept { engine_.set(8, db); }
    [[nodiscard]] double knee() const noexcept { return engine_.get(8); }

    void set_makeup(double db) noexcept { engine_.set(9, db); }
    [[nodiscard]] double makeup() const noexcept { return engine_.get(9); }

    void set_sidechain_hp(double hz) noexcept { engine_.set(10, hz); }
    [[nodiscard]] double sidechain_hp() const noexcept { return engine_.get(10); }

    void set_external_sc(bool en) noexcept { engine_.set(11, en ? 1.0 : 0.0); }
    [[nodiscard]] bool external_sc() const noexcept { return engine_.get(11) > 0.5; }
};

// ============================================================================
// 3. X-Multiband Device (4-Band Dynamics)
// ============================================================================

class XMultibandDevice final : public XAudioEffectDevice {
public:
    XMultibandDevice() : XAudioEffectDevice(2, "core.fx.x_multiband", "X-Multiband") {}

    void set_crossover(int idx_0to2, double hz) noexcept {
        if (idx_0to2 >= 0 && idx_0to2 < 3) engine_.set(4 + idx_0to2, hz);
    }
    [[nodiscard]] double crossover(int idx_0to2) const noexcept {
        return (idx_0to2 >= 0 && idx_0to2 < 3) ? engine_.get(4 + idx_0to2) : 1000.0;
    }

    void set_band_params(int band_0to3, double thresh, double ratio, double att, double rel, double knee, double makeup) {
        if (band_0to3 < 0 || band_0to3 >= 4) return;
        size_t base = 7 + band_0to3 * 8;
        engine_.set(base, thresh);
        engine_.set(base + 1, ratio);
        engine_.set(base + 2, att);
        engine_.set(base + 3, rel);
        engine_.set(base + 4, knee);
        engine_.set(base + 5, makeup);
    }

    void set_band_solo(int band_0to3, bool solo) noexcept {
        if (band_0to3 >= 0 && band_0to3 < 4) engine_.set(7 + band_0to3 * 8 + 6, solo ? 1.0 : 0.0);
    }
    [[nodiscard]] bool band_solo(int band_0to3) const noexcept {
        return (band_0to3 >= 0 && band_0to3 < 4) && (engine_.get(7 + band_0to3 * 8 + 6) > 0.5);
    }

    void set_band_mute(int band_0to3, bool mute) noexcept {
        if (band_0to3 >= 0 && band_0to3 < 4) engine_.set(7 + band_0to3 * 8 + 7, mute ? 1.0 : 0.0);
    }
    [[nodiscard]] bool band_mute(int band_0to3) const noexcept {
        return (band_0to3 >= 0 && band_0to3 < 4) && (engine_.get(7 + band_0to3 * 8 + 7) > 0.5);
    }

    [[nodiscard]] float band_gain_reduction_db(int band_0to3) const noexcept {
        return (band_0to3 >= 0 && band_0to3 < 4) ? engine_.gr[band_0to3] : 0.0f;
    }
};

// ============================================================================
// 4. X-Reverb Device
// ============================================================================

class XReverbDevice final : public XAudioEffectDevice {
public:
    XReverbDevice() : XAudioEffectDevice(3, "core.fx.x_reverb", "X-Reverb") {}

    void set_predelay_ms(double ms) noexcept { engine_.set(4, ms); }
    [[nodiscard]] double predelay_ms() const noexcept { return engine_.get(4); }

    void set_decay_s(double s) noexcept { engine_.set(5, s); }
    [[nodiscard]] double decay_s() const noexcept { return engine_.get(5); }

    void set_damping_hz(double hz) noexcept { engine_.set(6, hz); }
    [[nodiscard]] double damping_hz() const noexcept { return engine_.get(6); }

    void set_lowcut_hz(double hz) noexcept { engine_.set(7, hz); }
    [[nodiscard]] double lowcut_hz() const noexcept { return engine_.get(7); }

    void set_width_pct(double pct) noexcept { engine_.set(8, pct); }
    [[nodiscard]] double width_pct() const noexcept { return engine_.get(8); }

    void set_diffusion_pct(double pct) noexcept { engine_.set(9, pct); }
    [[nodiscard]] double diffusion_pct() const noexcept { return engine_.get(9); }
};

// ============================================================================
// 5. X-Distortion Device
// ============================================================================

class XDistortionDevice final : public XAudioEffectDevice {
public:
    XDistortionDevice() : XAudioEffectDevice(4, "core.fx.x_distortion", "X-Distortion") {}

    void set_drive_db(double db) noexcept { engine_.set(4, db); }
    [[nodiscard]] double drive_db() const noexcept { return engine_.get(4); }

    void set_tone_hz(double hz) noexcept { engine_.set(5, hz); }
    [[nodiscard]] double tone_hz() const noexcept { return engine_.get(5); }

    void set_bias_pct(double pct) noexcept { engine_.set(6, pct); }
    [[nodiscard]] double bias_pct() const noexcept { return engine_.get(6); }
};

// ============================================================================
// 6. X-Limiter Device
// ============================================================================

class XLimiterDevice final : public XAudioEffectDevice {
public:
    XLimiterDevice() : XAudioEffectDevice(5, "core.fx.x_limiter", "X-Limiter") {}

    void set_threshold_db(double db) noexcept { engine_.set(4, db); }
    [[nodiscard]] double threshold_db() const noexcept { return engine_.get(4); }

    void set_ceiling_db(double db) noexcept { engine_.set(5, db); }
    [[nodiscard]] double ceiling_db() const noexcept { return engine_.get(5); }

    void set_release_ms(double ms) noexcept { engine_.set(6, ms); }
    [[nodiscard]] double release_ms() const noexcept { return engine_.get(6); }
};

// ============================================================================
// 7. X-Synth Device (Generator / Instrument)
// ============================================================================

class XSynthDevice final : public domain::IDevice {
public:
    XSynthDevice() {
        update_filter();
    }

    [[nodiscard]] domain::DeviceUid uid() const override { return "core.generator.x_synth"; }
    [[nodiscard]] std::string name() const override { return "X-Synth"; }
    [[nodiscard]] domain::DeviceCategory category() const override { return domain::DeviceCategory::Generator; }

    void prepare(double sample_rate, size_t /*max_block_size*/) override {
        sample_rate_ = sample_rate > 0.0 ? sample_rate : 44100.0;
        update_filter();
        reset();
    }

    void reset() override {
        for (auto& v : voices_) {
            v.active = false;
            v.stage = Voice::Stage::Off;
            v.env_level = 0.0;
            v.age = 0;
            v.filter_l.reset();
            v.filter_r.reset();
        }
        voice_age_counter_ = 0;
    }

    void all_notes_off() noexcept {
        for (auto& v : voices_) {
            if (v.active) {
                v.stage = Voice::Stage::Release;
            }
        }
    }

    [[nodiscard]] size_t active_voices() const noexcept {
        size_t count = 0;
        for (const auto& v : voices_) {
            if (v.active && v.stage != Voice::Stage::Off) ++count;
        }
        return count;
    }

    void process(domain::AudioBufferView& buffer, std::span<const domain::MidiEvent> midi) override {
        if (buffer.frames == 0) return;

        // Process MIDI events
        for (const auto& ev : midi) {
            if (ev.is_note_on()) {
                trigger_note(ev.data1, ev.data2);
            } else if (ev.is_note_off()) {
                release_note(ev.data1);
            } else if (ev.is_control_change() && (ev.data1 == 120 || ev.data1 == 123)) {
                all_notes_off();
            }
        }

        const double dt = 1.0 / sample_rate_;
        const double att_rate = 1.0 / std::max(0.001, 0.001 * attack_ms_ * sample_rate_);
        const double dec_rate = (1.0 - sustain_level_) / std::max(0.001, 0.001 * decay_ms_ * sample_rate_);
        const double rel_rate = 1.0 / std::max(0.001, 0.001 * release_ms_ * sample_rate_);
        const double drive_gain = xaudio::dbGain(drive_db_);
        float* out_l = buffer.left;
        float* out_r = buffer.right;

        struct ActiveVoiceInfo {
            Voice* v;
            double step1;
            double step2;
            double sub_step;
        };
        std::array<ActiveVoiceInfo, 16> active_info{};
        size_t num_active = 0;
        for (auto& v : voices_) {
            if (!v.active) continue;
            double p1 = static_cast<double>(static_cast<int>(v.note) + (osc1_octave_ * 12) - 69);
            double p2 = static_cast<double>(static_cast<int>(v.note) + (osc2_octave_ * 12) + osc2_detune_semi_ - 69);
            double base_freq = 440.0 * std::pow(2.0, p1 / 12.0);
            double detune_freq = 440.0 * std::pow(2.0, p2 / 12.0);
            active_info[num_active++] = {&v, base_freq * dt, detune_freq * dt, (base_freq * 0.5) * dt};
        }

        if (num_active == 0) {
            buffer.clear();
            return;
        }

        for (size_t f = 0; f < buffer.frames; ++f) {
            double sample_l = 0.0;
            double sample_r = 0.0;

            for (size_t vi = 0; vi < num_active; ++vi) {
                auto& v = *active_info[vi].v;
                if (!v.active) continue;

                // ADSR envelope step
                switch (v.stage) {
                    case Voice::Stage::Attack:
                        v.env_level += att_rate;
                        if (v.env_level >= 1.0) {
                            v.env_level = 1.0;
                            v.stage = Voice::Stage::Decay;
                        }
                        break;
                    case Voice::Stage::Decay:
                        v.env_level -= dec_rate;
                        if (v.env_level <= sustain_level_) {
                            v.env_level = sustain_level_;
                            v.stage = Voice::Stage::Sustain;
                        }
                        break;
                    case Voice::Stage::Sustain:
                        v.env_level = sustain_level_;
                        break;
                    case Voice::Stage::Release:
                        v.env_level -= rel_rate;
                        if (v.env_level <= 0.0) {
                            v.env_level = 0.0;
                            v.stage = Voice::Stage::Off;
                            v.active = false;
                        }
                        break;
                    case Voice::Stage::Off:
                        v.active = false;
                        break;
                }

                if (!v.active) continue;

                // Oscillators
                double o1 = generate_wave(osc1_shape_, v.phase1) * osc1_vol_;
                double o2 = generate_wave(osc2_shape_, v.phase2) * osc2_vol_;
                double sub = (v.sub_phase < 0.5 ? 1.0 : -1.0) * sub_vol_;

                v.phase1 += active_info[vi].step1;
                if (v.phase1 >= 1.0) v.phase1 -= 1.0;

                v.phase2 += active_info[vi].step2;
                if (v.phase2 >= 1.0) v.phase2 -= 1.0;

                v.sub_phase += active_info[vi].sub_step;
                if (v.sub_phase >= 1.0) v.sub_phase -= 1.0;

                double raw_sig = (o1 + o2 + sub) * v.velocity * v.env_level;

                // Stereo spread & filtering
                double fl = v.filter_l.tick(raw_sig);
                double fr = v.filter_r.tick(raw_sig);

                sample_l += fl;
                sample_r += fr;
            }

            // Nonlinear Saturation stage using XAudio tanh waveshaper
            sample_l = std::tanh(sample_l * drive_gain) * master_vol_;
            sample_r = std::tanh(sample_r * drive_gain) * master_vol_;

            if (out_l) out_l[f] += static_cast<float>(sample_l);
            if (out_r) out_r[f] += static_cast<float>(sample_r);
        }
    }

    void set_parameter(uint32_t param_id, float val) override {
        float c = std::clamp(val, 0.0f, 1.0f);
        switch (param_id) {
            case 0: master_vol_ = c; break;
            case 1: osc1_shape_ = std::clamp(static_cast<int>(std::round(c * 3.0f)), 0, 3); break;
            case 2: osc1_octave_ = std::clamp(static_cast<int>(std::round((c - 0.5f) * 4.0f)), -2, 2); break;
            case 3: osc1_vol_ = c; break;
            case 4: osc2_shape_ = std::clamp(static_cast<int>(std::round(c * 3.0f)), 0, 3); break;
            case 5: osc2_octave_ = std::clamp(static_cast<int>(std::round((c - 0.5f) * 4.0f)), -2, 2); break;
            case 6: osc2_detune_semi_ = std::clamp(static_cast<int>(std::round((c - 0.5f) * 48.0f)), -24, 24); break;
            case 7: osc2_vol_ = c; break;
            case 8: sub_vol_ = c; break;
            case 9: filter_cutoff_hz_ = 20.0 * std::pow(1000.0, static_cast<double>(c)); update_filter(); break;
            case 10: filter_q_ = 0.15 + c * 10.0; update_filter(); break;
            case 11: filter_type_ = std::clamp(static_cast<int>(std::round(c * 2.0f)), 0, 2); update_filter(); break;
            case 12: drive_db_ = c * 36.0; break;
            case 13: attack_ms_ = 1.0 + c * 1999.0; break;
            case 14: decay_ms_ = 1.0 + c * 1999.0; break;
            case 15: sustain_level_ = c; break;
            case 16: release_ms_ = 1.0 + c * 4999.0; break;
            default: break;
        }
    }

    [[nodiscard]] float get_parameter(uint32_t param_id) const override {
        switch (param_id) {
            case 0: return static_cast<float>(master_vol_);
            case 1: return static_cast<float>(osc1_shape_) / 3.0f;
            case 2: return (static_cast<float>(osc1_octave_) / 4.0f) + 0.5f;
            case 3: return static_cast<float>(osc1_vol_);
            case 4: return static_cast<float>(osc2_shape_) / 3.0f;
            case 5: return (static_cast<float>(osc2_octave_) / 4.0f) + 0.5f;
            case 6: return (static_cast<float>(osc2_detune_semi_) / 48.0f) + 0.5f;
            case 7: return static_cast<float>(osc2_vol_);
            case 8: return static_cast<float>(sub_vol_);
            case 9: return static_cast<float>(std::log(filter_cutoff_hz_ / 20.0) / std::log(1000.0));
            case 10: return static_cast<float>((filter_q_ - 0.15) / 10.0);
            case 11: return static_cast<float>(filter_type_) / 2.0f;
            case 12: return static_cast<float>(drive_db_ / 36.0);
            case 13: return static_cast<float>((attack_ms_ - 1.0) / 1999.0);
            case 14: return static_cast<float>((decay_ms_ - 1.0) / 1999.0);
            case 15: return static_cast<float>(sustain_level_);
            case 16: return static_cast<float>((release_ms_ - 1.0) / 4999.0);
            default: return 0.0f;
        }
    }

    [[nodiscard]] std::vector<domain::ParamDesc> parameters() const override {
        return {
            {0, "Master Volume", static_cast<float>(master_vol_), 0.0f, 1.0f, "%"},
            {1, "Osc 1 Shape", static_cast<float>(osc1_shape_) / 3.0f, 0.0f, 1.0f, ""},
            {2, "Osc 1 Octave", (static_cast<float>(osc1_octave_) / 4.0f) + 0.5f, 0.0f, 1.0f, "oct"},
            {3, "Osc 1 Volume", static_cast<float>(osc1_vol_), 0.0f, 1.0f, "%"},
            {4, "Osc 2 Shape", static_cast<float>(osc2_shape_) / 3.0f, 0.0f, 1.0f, ""},
            {5, "Osc 2 Octave", (static_cast<float>(osc2_octave_) / 4.0f) + 0.5f, 0.0f, 1.0f, "oct"},
            {6, "Osc 2 Detune", (static_cast<float>(osc2_detune_semi_) / 48.0f) + 0.5f, 0.0f, 1.0f, "semi"},
            {7, "Osc 2 Volume", static_cast<float>(osc2_vol_), 0.0f, 1.0f, "%"},
            {8, "Sub Osc Volume", static_cast<float>(sub_vol_), 0.0f, 1.0f, "%"},
            {9, "Filter Cutoff", static_cast<float>(std::log(filter_cutoff_hz_ / 20.0) / std::log(1000.0)), 0.0f, 1.0f, "Hz"},
            {10, "Filter Resonance", static_cast<float>((filter_q_ - 0.15) / 10.0), 0.0f, 1.0f, "Q"},
            {11, "Filter Type", static_cast<float>(filter_type_) / 2.0f, 0.0f, 1.0f, ""},
            {12, "Drive Saturation", static_cast<float>(drive_db_ / 36.0), 0.0f, 1.0f, "dB"},
            {13, "Envelope Attack", static_cast<float>((attack_ms_ - 1.0) / 1999.0), 0.0f, 1.0f, "ms"},
            {14, "Envelope Decay", static_cast<float>((decay_ms_ - 1.0) / 1999.0), 0.0f, 1.0f, "ms"},
            {15, "Envelope Sustain", static_cast<float>(sustain_level_), 0.0f, 1.0f, "%"},
            {16, "Envelope Release", static_cast<float>((release_ms_ - 1.0) / 4999.0), 0.0f, 1.0f, "ms"}
        };
    }

    [[nodiscard]] std::vector<uint8_t> save_state() const override {
        std::vector<uint8_t> bytes(17 * sizeof(float));
        float* f = reinterpret_cast<float*>(bytes.data());
        for (uint32_t i = 0; i < 17; ++i) {
            f[i] = get_parameter(i);
        }
        return bytes;
    }

    domain::Result<void> load_state(std::span<const uint8_t> data) override {
        if (data.size() < 15 * sizeof(float)) {
            return domain::Result<void>(domain::ErrorCode::StateIncompatible);
        }
        const float* f = reinterpret_cast<const float*>(data.data());
        size_t count = std::min(data.size() / sizeof(float), size_t(17));
        for (uint32_t i = 0; i < count; ++i) {
            set_parameter(i, f[i]);
        }
        return domain::Result<void>::ok();
    }

    // Direct Parameter Setters for GUI / Scripting
    void set_master_vol(double v) noexcept { master_vol_ = std::clamp(v, 0.0, 1.0); }
    [[nodiscard]] double master_vol() const noexcept { return master_vol_; }

    void set_osc1(int shape, double vol, int octave = 0) noexcept {
        osc1_shape_ = std::clamp(shape, 0, 3);
        osc1_vol_ = std::clamp(vol, 0.0, 1.0);
        osc1_octave_ = std::clamp(octave, -2, 2);
    }
    [[nodiscard]] int osc1_shape() const noexcept { return osc1_shape_; }
    [[nodiscard]] double osc1_vol() const noexcept { return osc1_vol_; }
    [[nodiscard]] int osc1_octave() const noexcept { return osc1_octave_; }

    void set_osc2(int shape, double vol, int detune_semi, int octave = 0) noexcept {
        osc2_shape_ = std::clamp(shape, 0, 3);
        osc2_vol_ = std::clamp(vol, 0.0, 1.0);
        osc2_detune_semi_ = std::clamp(detune_semi, -24, 24);
        osc2_octave_ = std::clamp(octave, -2, 2);
    }
    [[nodiscard]] int osc2_shape() const noexcept { return osc2_shape_; }
    [[nodiscard]] double osc2_vol() const noexcept { return osc2_vol_; }
    [[nodiscard]] int osc2_detune() const noexcept { return osc2_detune_semi_; }
    [[nodiscard]] int osc2_octave() const noexcept { return osc2_octave_; }

    void set_filter(double cutoff_hz, double q, int type) noexcept {
        filter_cutoff_hz_ = std::clamp(cutoff_hz, 20.0, 20000.0);
        filter_q_ = std::clamp(q, 0.15, 12.0);
        filter_type_ = std::clamp(type, 0, 2);
        update_filter();
    }
    [[nodiscard]] double filter_cutoff() const noexcept { return filter_cutoff_hz_; }
    [[nodiscard]] double filter_q() const noexcept { return filter_q_; }
    [[nodiscard]] int filter_type() const noexcept { return filter_type_; }

    void set_drive(double db) noexcept { drive_db_ = std::clamp(db, 0.0, 36.0); }
    [[nodiscard]] double drive() const noexcept { return drive_db_; }

    void set_adsr(double a_ms, double d_ms, double s_lvl, double r_ms) noexcept {
        attack_ms_ = std::max(1.0, a_ms);
        decay_ms_ = std::max(1.0, d_ms);
        sustain_level_ = std::clamp(s_lvl, 0.0, 1.0);
        release_ms_ = std::max(1.0, r_ms);
    }

private:
    struct Voice {
        bool active{false};
        uint8_t note{60};
        double velocity{0.8};
        double phase1{0.0};
        double phase2{0.0};
        double sub_phase{0.0};
        double env_level{0.0};
        uint32_t age{0};
        enum class Stage { Attack, Decay, Sustain, Release, Off } stage{Stage::Off};
        xaudio::Biquad filter_l{};
        xaudio::Biquad filter_r{};
    };

    void trigger_note(uint8_t note, uint8_t vel) noexcept {
        if (vel == 0) {
            release_note(note);
            return;
        }

        Voice* target = nullptr;

        // 1. Check if same note is already sounding (retrigger)
        for (auto& v : voices_) {
            if (v.active && v.note == note) {
                target = &v;
                break;
            }
        }

        // 2. Look for an inactive / Off voice
        if (!target) {
            for (auto& v : voices_) {
                if (!v.active || v.stage == Voice::Stage::Off) {
                    target = &v;
                    break;
                }
            }
        }

        // 3. Voice steal priority 1: Steal released voice with lowest envelope level
        if (!target) {
            double lowest_rel_env = 1e9;
            for (auto& v : voices_) {
                if (v.stage == Voice::Stage::Release && v.env_level < lowest_rel_env) {
                    lowest_rel_env = v.env_level;
                    target = &v;
                }
            }
        }

        // 4. Voice steal priority 2: Steal oldest voice among non-attack voices
        if (!target) {
            uint32_t oldest_age = 0;
            for (auto& v : voices_) {
                if (v.stage != Voice::Stage::Attack && (voice_age_counter_ - v.age) >= oldest_age) {
                    oldest_age = voice_age_counter_ - v.age;
                    target = &v;
                }
            }
        }

        // 5. Fallback: steal oldest voice overall
        if (!target) {
            uint32_t oldest_age = 0;
            for (auto& v : voices_) {
                if ((voice_age_counter_ - v.age) >= oldest_age) {
                    oldest_age = voice_age_counter_ - v.age;
                    target = &v;
                }
            }
        }

        if (!target) target = &voices_[0];

        target->active = true;
        target->note = note;
        target->velocity = static_cast<double>(vel) / 127.0;
        target->stage = Voice::Stage::Attack;
        target->phase1 = 0.0;
        target->phase2 = 0.0;
        target->sub_phase = 0.0;
        target->env_level = 0.0;
        target->age = ++voice_age_counter_;
        target->filter_l.reset();
        target->filter_r.reset();
        apply_voice_filter(*target);
    }

    void release_note(uint8_t note) noexcept {
        for (auto& v : voices_) {
            if (v.active && v.note == note && v.stage != Voice::Stage::Release) {
                v.stage = Voice::Stage::Release;
            }
        }
    }

    static double generate_wave(int shape, double phase) noexcept {
        switch (shape) {
            case 0: // Sine
                return std::sin(2.0 * xaudio::pi * phase);
            case 1: // Triangle
                return 4.0 * std::abs(phase - 0.5) - 1.0;
            case 2: // Saw
                return 2.0 * phase - 1.0;
            case 3: // Square
                return (phase < 0.5) ? 1.0 : -1.0;
            default: {
                return 0.0;
            }
        }
    }

    void update_filter() noexcept {
        for (auto& v : voices_) {
            apply_voice_filter(v);
        }
    }

    void apply_voice_filter(Voice& v) noexcept {
        // filter_type_: 0: LowPass (type 3), 1: HighPass (type 4), 2: Peaking/Bandpass (type 0)
        int biquad_type = (filter_type_ == 0) ? 3 : ((filter_type_ == 1) ? 4 : 0);
        v.filter_l.set(biquad_type, filter_cutoff_hz_, 0.0, filter_q_, sample_rate_);
        v.filter_r.set(biquad_type, filter_cutoff_hz_, 0.0, filter_q_, sample_rate_);
    }

    double sample_rate_{44100.0};
    double master_vol_{0.85};

    int osc1_shape_{2}; // Saw
    int osc1_octave_{0}; // -2 to +2
    double osc1_vol_{0.8};

    int osc2_shape_{3}; // Square
    int osc2_octave_{0}; // -2 to +2
    double osc2_vol_{0.5};
    int osc2_detune_semi_{7}; // +7 semitones (fifth)

    double sub_vol_{0.3};

    double filter_cutoff_hz_{3500.0};
    double filter_q_{1.5};
    int filter_type_{0}; // LowPass

    double drive_db_{4.0};

    double attack_ms_{15.0};
    double decay_ms_{150.0};
    double sustain_level_{0.7};
    double release_ms_{250.0};

    uint32_t voice_age_counter_{0};
    std::array<Voice, 16> voices_{};
};

} // namespace digidaw::adapters::plugins

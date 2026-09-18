#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace digidaw::xosc {

// Registry order is persistent: append new parameters; never reorder released IDs.
struct Spec {
    std::string id, name, unit;
    float lo, hi, initial, skew;
    int kind; // 0 = float, 1 = bool, 2 = choice
    std::vector<std::string> choices;
};

inline const std::vector<Spec>& specs() {
    static const auto result = [] {
        std::vector<Spec> s;
        auto f = [&](std::string id, std::string name, float lo, float hi, float def,
                     std::string unit = "", float skew = 1.f) {
            s.push_back({id, name, unit, lo, hi, def, skew, 0, {}});
        };
        auto b = [&](std::string id, std::string name, bool def = false) {
            s.push_back({id, name, "", 0, 1, float(def), 1, 1, {}});
        };
        auto c = [&](std::string id, std::string name, std::vector<std::string> choices, int def = 0) {
            s.push_back({id, name, "", 0, float(choices.size() - 1), float(def), 1, 2, choices});
        };
        auto routes = [&](std::string p, bool first = false) {
            for (int j = 0; j < 4; ++j)
                b(p + "route" + std::to_string(j), "OSC " + std::to_string(j + 1), first && j == 0);
        };
        for (int i = 0; i < 4; ++i) {
            auto p = "o" + std::to_string(i) + "_";
            b(p + "on", "Power", i == 0);
            f(p + "vol", "Volume", -60, 6, -9, "dB");
            f(p + "phase", "Phase", 0, 360, 0, "deg");
            f(p + "detune", "Detune", 0, 50, 12, "ct");
            f(p + "stereo", "Stereo", 0, 1, .65f);
            f(p + "pan", "Pan", -1, 1, 0);
            c(p + "wave", "Wave", {"Sine", "Square", "Saw"}, 2);
            c(p + "voices", "Unison",
              {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"});
            f(p + "tune", "Tune", -24, 24, 0, "st");
        }
        for (int i = 0; i < 2; ++i) {
            auto p = "f" + std::to_string(i) + "_";
            b(p + "on", "Power");
            c(p + "type", "Type", {"Low pass", "High pass", "Band pass", "Notch"});
            f(p + "cutoff", "Cutoff", 20, 20000, 8000, "Hz", .23f);
            f(p + "res", "Resonance", .5f, 8, .707f, "Q", .5f);
            f(p + "drive", "Drive", 0, 24, 0, "dB");
            routes(p);
        }
        for (auto family : {'e', 'a'})
            for (int i = 0; i < (family == 'e' ? 4 : 2); ++i) {
                auto p = std::string(1, family) + std::to_string(i) + "_";
                b(p + "on", "Power", family == 'a' && i == 0);
                f(p + "attack", "Attack", .001f, 8, .01f, "s", .25f);
                f(p + "decay", "Decay", .001f, 8, .25f, "s", .25f);
                f(p + "sustain", "Sustain", 0, 1, family == 'a' ? .8f : .5f);
                f(p + "release", "Release", .005f, 12, .35f, "s", .25f);
                if (family == 'e') {
                    c(p + "target", "Target",
                      {"Osc Volume", "Osc Pitch", "Cutoff", "Flt 1 Cutoff", "Flt 1 Res",
                       "Flt 2 Cutoff", "Flt 2 Res", "Filter Drive", "Osc Pan"});
                    f(p + "mix", "Mix", -1, 1, .5f);
                }
                routes(p, family == 'a' && i == 0);
            }
        b("dist_on", "Distortion");
        f("dist_drive", "Drive", 0, 30, 9, "dB");
        f("dist_mix", "Mix", 0, 1, .2f);
        b("eq_on", "EQ");
        f("eq_low", "Low 180 Hz", -12, 12, 0, "dB");
        f("eq_mid", "Mid 1 kHz", -12, 12, 0, "dB");
        f("eq_high", "High 5 kHz", -12, 12, 0, "dB");
        b("comp_on", "Compressor");
        f("comp_threshold", "Threshold", -48, 0, -18, "dB");
        f("comp_ratio", "Ratio", 1, 12, 3);
        f("comp_attack", "Attack", .001f, .1f, .01f, "s", .5f);
        f("comp_release", "Release", .02f, 1, .15f, "s", .5f);
        f("comp_makeup", "Makeup", 0, 18, 0, "dB");
        b("delay_on", "Delay");
        f("delay_time", "Time", .02f, 1.5f, .375f, "s");
        f("delay_feedback", "Feedback", 0, .85f, .32f);
        f("delay_mix", "Mix", 0, 1, .2f);
        b("reverb_on", "Reverb");
        f("reverb_size", "Size", 0, 1, .65f);
        f("reverb_damp", "Damping", 0, 1, .5f);
        f("reverb_mix", "Mix", 0, 1, .18f);
        b("master_on", "Output", true);
        f("master_gain", "Output", -60, 6, -6, "dB");
        f("master_width", "Width", 0, 1.5f, 1);
        c("polyphony", "Polyphony", {"4", "8", "16", "32"}, 2);
        b("mono_legato", "Mono legato");
        b("glide_on", "Portamento");
        f("glide_time", "Glide time", 0, 2, .12f, "s", .5f);
        c("glide_mode", "Glide mode", {"N / Normal", "S / Slide"});
        return s;
    }();
    return result;
}

inline int index(const std::string& id) {
    const auto& s = specs();
    auto it = std::find_if(s.begin(), s.end(), [&](const Spec& p) { return p.id == id; });
    return it == s.end() ? -1 : int(it - s.begin());
}

inline float to_normalized(const Spec& s, float val) {
    if (s.kind == 1) {
        return val > 0.5f ? 1.0f : 0.0f;
    }
    if (s.kind == 2) {
        if (s.choices.size() <= 1) return 0.0f;
        return std::clamp(val / float(s.choices.size() - 1), 0.0f, 1.0f);
    }
    if (s.hi <= s.lo) return 0.0f;
    float norm = std::clamp((val - s.lo) / (s.hi - s.lo), 0.0f, 1.0f);
    if (s.skew != 1.0f && s.skew > 0.0f) {
        norm = std::pow(norm, s.skew);
    }
    return norm;
}

inline float to_plain(const Spec& s, float norm) {
    norm = std::clamp(norm, 0.0f, 1.0f);
    if (s.kind == 1) {
        return norm >= 0.5f ? 1.0f : 0.0f;
    }
    if (s.kind == 2) {
        if (s.choices.empty()) return 0.0f;
        int max_idx = static_cast<int>(s.choices.size()) - 1;
        int idx = std::clamp(static_cast<int>(std::round(norm * static_cast<float>(max_idx))), 0, max_idx);
        return static_cast<float>(idx);
    }
    float plain;
    if (s.skew != 1.0f && s.skew > 0.0f) {
        plain = s.lo + (s.hi - s.lo) * std::pow(norm, 1.0f / s.skew);
    } else {
        plain = s.lo + norm * (s.hi - s.lo);
    }
    if (s.unit == "st") {
        plain = std::round(plain);
    }
    return std::clamp(plain, s.lo, s.hi);
}

struct Osc {
    bool on = false;
    float vol = -9, phase = 0, detune = 12, stereo = .65f, pan = 0, tune = 0;
    int wave = 2, voices = 1;
};

struct Filter {
    bool on = false;
    int type = 0;
    float cutoff = 8000, res = .707f, drive = 0;
    std::array<bool, 4> route{};
};

struct Env {
    bool on = false;
    float attack = .01f, decay = .25f, sustain = .8f, release = .35f, mix = .5f;
    int target = 0;
    std::array<bool, 4> route{};
};

struct Patch {
    std::array<Osc, 4> osc{};
    std::array<Filter, 2> filter{};
    std::array<Env, 4> env{};
    std::array<Env, 2> amp{};
    bool distOn = false, eqOn = false, compOn = false, delayOn = false, reverbOn = false,
         masterOn = true;
    float distDrive = 9, distMix = .2f, eqLow = 0, eqMid = 0, eqHigh = 0, compThreshold = -18,
          compRatio = 3, compAttack = .01f, compRelease = .15f, compMakeup = 0, delayTime = .375f,
          delayFeedback = .32f, delayMix = .2f, reverbSize = .65f, reverbDamp = .5f,
          reverbMix = .18f, masterGain = -6, masterWidth = 1;
    int polyphony = 16;
    bool monoLegato = false, glideOn = false;
    float glideTime = .12f;
    int glideMode = 0; // N: every transition; S: overlapping keys only.
    Patch() {
        osc[0].on = true;
        amp[0].on = true;
        amp[0].route[0] = true;
    }
};

// Decode sequentially, with no strings or allocations in the audio callback.
template <class Read>
Patch decode(Read read) {
    Patch p;
    int k = 0;
    auto v = [&] { return read(k++); };
    auto routes = [&](auto& r) {
        for (auto& x : r)
            x = v() > .5f;
    };
    for (auto& o : p.osc) {
        o.on = v() > .5f;
        o.vol = v();
        o.phase = v();
        o.detune = v();
        o.stereo = v();
        o.pan = v();
        o.wave = std::clamp(int(v()), 0, 2);
        o.voices = std::clamp(int(v()) + 1, 1, 12);
        o.tune = v();
    }
    for (auto& f : p.filter) {
        f.on = v() > .5f;
        f.type = std::clamp(int(v()), 0, 3);
        f.cutoff = v();
        f.res = v();
        f.drive = v();
        routes(f.route);
    }
    auto env = [&](auto& list, bool mod) {
        for (auto& e : list) {
            e.on = v() > .5f;
            e.attack = v();
            e.decay = v();
            e.sustain = v();
            e.release = v();
            if (mod) {
                e.target = std::clamp(int(v()), 0, 8);
                e.mix = v();
            }
            routes(e.route);
        }
    };
    env(p.env, true);
    env(p.amp, false);
    p.distOn = v() > .5f;
    p.distDrive = v();
    p.distMix = v();
    p.eqOn = v() > .5f;
    p.eqLow = v();
    p.eqMid = v();
    p.eqHigh = v();
    p.compOn = v() > .5f;
    p.compThreshold = v();
    p.compRatio = v();
    p.compAttack = v();
    p.compRelease = v();
    p.compMakeup = v();
    p.delayOn = v() > .5f;
    p.delayTime = v();
    p.delayFeedback = v();
    p.delayMix = v();
    p.reverbOn = v() > .5f;
    p.reverbSize = v();
    p.reverbDamp = v();
    p.reverbMix = v();
    p.masterOn = v() > .5f;
    p.masterGain = v();
    p.masterWidth = v();
    p.polyphony = 4 << std::clamp(int(v()), 0, 3);
    p.monoLegato = v() > .5f;
    p.glideOn = v() > .5f;
    p.glideTime = v();
    p.glideMode = std::clamp(int(v()), 0, 1);
    return p;
}

// ============================================================================
// DSP Engine
// ============================================================================

constexpr float pi = 3.14159265358979323846f;

inline float db(float x) {
    return std::pow(10.f, x / 20.f);
}

inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

inline float blep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1;
    }
    if (t > 1 - dt) {
        t = (t - 1) / dt;
        return t * t + t + t + 1;
    }
    return 0;
}

inline float wave(float t, float dt, int w) {
    if (w == 0)
        return std::sin(2 * pi * t);
    if (w == 2)
        return 2 * t - 1 - blep(t, dt);
    float u = t + .5f;
    if (u >= 1)
        u -= 1;
    return (t < .5f ? 1.f : -1.f) + blep(t, dt) - blep(u, dt);
}

struct ADSR {
    enum Stage {
        idle,
        attack,
        decay,
        sustain,
        release
    };
    Stage stage = idle;
    float value = 0, releaseStep = 0;
    void on() {
        stage = attack;
        value = 0;
    }
    void off(const Env& e, float sr) {
        if (stage != idle && stage != release) {
            stage = release;
            float total = std::max(1.f, e.release * sr);
            releaseStep = std::max(value / total, 1.f / (12.f * sr));
        }
    }
    float tick(const Env& e, float sr) {
        switch (stage) {
        case idle:
            value = 0;
            break;
        case attack:
            value += 1 / std::max(1.f, e.attack * sr);
            if (value >= 1) {
                value = 1;
                stage = decay;
            }
            break;
        case decay:
            value -= (1 - e.sustain) / std::max(1.f, e.decay * sr);
            if (value <= e.sustain + 1e-5f) {
                value = e.sustain;
                stage = sustain;
            }
            break;
        case sustain:
            value = e.sustain;
            break;
        case release:
            value -= releaseStep;
            if (value <= 1e-5f) {
                value = 0;
                stage = idle;
            }
            break;
        }
        return value;
    }
};

struct SVF {
    float s1 = 0, s2 = 0, g = 0, k = 1.414f, a1 = 1, a2 = 0, a3 = 0;
    int type = 0;
    void set(float freq, float q, int t, float sr) {
        g = std::tan(pi * std::clamp(freq, 20.f, sr * .44f) / sr);
        k = 1 / std::clamp(q, .5f, 8.f);
        a1 = 1 / (1 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        type = t;
    }
    float tick(float x) {
        float v3 = x - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2 * v1 - s1;
        s2 = 2 * v2 - s2;
        float hp = x - k * v1 - v2;
        return type == 0 ? v2 : type == 1 ? hp : type == 2 ? v1 : v2 + hp;
    }
    void reset() {
        s1 = s2 = 0;
    }
};

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void peak(float hz, float gain, float sr) {
        float a = std::pow(10.f, gain / 40.f), w = 2 * pi * hz / sr, alpha = std::sin(w) / 1.4f,
              c = std::cos(w), norm = 1 / (1 + alpha / a);
        b0 = (1 + alpha * a) * norm;
        b1 = -2 * c * norm;
        b2 = (1 - alpha * a) * norm;
        a1 = b1;
        a2 = (1 - alpha / a) * norm;
    }
    float tick(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

struct DelayLine {
    std::vector<float> data;
    size_t pos = 0;
    void prepare(size_t n) {
        data.assign(n, 0);
        pos = 0;
    }
    float read(float samples) const {
        float p = float(pos) - samples;
        while (p < 0)
            p += float(data.size());
        auto i = size_t(p);
        auto j = (i + 1) % data.size();
        return lerp(data[i], data[j], p - float(i));
    }
    void push(float x) {
        data[pos] = x;
        pos = (pos + 1) % data.size();
    }
};

struct Reverb {
    std::array<DelayLine, 8> comb;
    std::array<DelayLine, 4> ap;
    std::array<float, 8> damp{};
    void prepare(float sr) {
        constexpr int lengths[] = {1116, 1188, 1277, 1356, 1139, 1211, 1300, 1379};
        for (int i = 0; i < 8; ++i)
            comb[i].prepare(size_t(lengths[i] * sr / 44100));
        for (int i = 0; i < 4; ++i)
            ap[i].prepare(size_t((i % 2 ? 441 : 556) * sr / 44100) + size_t(i / 2) * 23);
        damp.fill(0);
    }
    std::array<float, 2> tick(float l, float r, float size, float damping) {
        std::array<float, 2> out{};
        float in = (l + r) * .1f;
        for (int c = 0; c < 2; ++c) {
            float y = 0;
            for (int j = 0; j < 4; ++j) {
                int k = c * 4 + j;
                auto& d = comb[k];
                float delayed = d.data[d.pos];
                damp[k] = lerp(delayed, damp[k], damping * .8f);
                d.push(in + damp[k] * (.55f + size * .4f));
                y += delayed * .25f;
            }
            for (int j = 0; j < 2; ++j) {
                auto& d = ap[c * 2 + j];
                float delayed = d.data[d.pos], v = y + delayed * .5f;
                d.push(v);
                y = delayed - v * .5f;
            }
            out[c] = y;
        }
        return out;
    }
};

struct Voice {
    bool active = false, held = false, sustained = false;
    int note = 0, channel = 1;
    uint64_t age = 0;
    float velocity = 0, gate = 0, stealL = 0, stealR = 0, lastL = 0, lastR = 0;
    int stealRemaining = 0;
    float pitch = 0, glideStart = 0;
    int glideRemaining = 0, glideLength = 0;
    void setPitch(float from, float seconds, float sr) {
        glideStart = from;
        glideLength = glideRemaining = int(std::max(0.f, seconds) * sr);
        pitch = glideRemaining ? from : float(note);
    }
    void advancePitch() {
        if (glideRemaining > 0) {
            --glideRemaining;
            pitch = lerp(glideStart, float(note), 1.f - float(glideRemaining) / float(glideLength));
        }
    }
    std::array<ADSR, 4> env;
    std::array<ADSR, 2> amp;
    std::array<std::array<float, 12>, 4> phase{}, inc{}, left{}, right{};
    std::array<std::array<std::array<SVF, 2>, 4>, 2> filters{};
    std::array<float, 4> oscGate{};
    std::array<std::array<float, 4>, 2> filterWet{};
    std::array<float, 2> filterDrive{1.0f, 1.0f};
    void start(int n, int ch, float vel, uint64_t a, const Patch& p) {
        stealL = lastL;
        stealR = lastR;
        stealRemaining = active ? 64 : 0;
        active = held = true;
        sustained = false;
        note = n;
        pitch = float(n);
        glideRemaining = 0;
        channel = ch;
        age = a;
        velocity = vel;
        gate = 0;
        oscGate.fill(0);
        filterDrive.fill(1.0f);
        for (auto& w : filterWet)
            w.fill(0);
        for (auto& e : env)
            e.on();
        for (auto& e : amp)
            e.on();
        for (int o = 0; o < 4; ++o)
            for (int u = 0; u < 12; ++u) {
                float x = p.osc[o].phase / 360.f + (u ? float(u) * .61803398875f : 0);
                phase[o][u] = x - std::floor(x);
            }
        for (auto& f : filters)
            for (auto& o : f)
                for (auto& c : o)
                    c.reset();
    }
    void release(const Patch& p, float sr) {
        held = false;
        sustained = false;
        for (int i = 0; i < 4; ++i)
            env[i].off(p.env[i], sr);
        for (int i = 0; i < 2; ++i)
            amp[i].off(p.amp[i], sr);
    }
};

class Engine {
public:
    void prepare(float sampleRate) {
        sr = std::max(8000.f, sampleRate);
        for (auto& v : voices)
            v = Voice{};
        for (auto& d : delay)
            d.prepare(size_t(sr * 1.6f) + 4);
        reverb.prepare(sr);
        for (auto& c : eq)
            for (auto& b : c)
                b = Biquad{};
        sustain.fill(false);
        keys = {};
        lastPitch.fill(-1);
        bend.fill(0);
        previousX.fill(0);
        previousY.fill(0);
        fxMix.fill(0);
        smooth = p;
        ready = false;
        sampleCount = 0;
        controlCountdown = 0;
        age = 0;
        detector = 0;
        master = 0;
        delayTail = 0;
        reverbTail = 0;
        dcCoeff = std::exp(-2 * pi * 12 / sr);
    }
    void setPatch(const Patch& patch) {
        if (p.monoLegato != patch.monoLegato)
            allNotesOff(0, true);
        p = patch;
        if (!p.glideOn || p.glideTime <= 0)
            for (auto& v : voices)
                v.setPitch(float(v.note), 0, sr);
        if (!ready) {
            smooth = p;
            ready = true;
        }
    }
    void noteOn(int note, float velocity, int channel = 1) {
        channel = std::clamp(channel, 1, 16);
        note = std::clamp(note, 0, 127);
        if (velocity <= 0) {
            noteOff(note, channel);
            return;
        }
        if (p.monoLegato) {
            bool overlap = anyKeyDown();
            auto& key = keys[size_t((channel - 1) * 128 + note)];
            ++key.count;
            key.latched = false;
            key.velocity = velocity;
            key.order = ++age;
            selectMono(note, channel, velocity, overlap);
            return;
        }
        bool overlap = false;
        for (const auto& v : voices)
            overlap = overlap || (v.held && v.channel == channel);

        Voice* pick = nullptr;

        // 1. If this note is ALREADY playing on this channel (held or releasing or sustained),
        // retrigger that voice directly so we don't leak voices or dangle sustain.
        for (int i = 0; i < p.polyphony; ++i) {
            if (voices[i].active && voices[i].channel == channel && voices[i].note == note) {
                pick = &voices[i];
                break;
            }
        }

        // 2. Otherwise look for an inactive voice slot
        if (!pick) {
            for (int i = 0; i < p.polyphony; ++i) {
                if (!voices[i].active) {
                    pick = &voices[i];
                    break;
                }
            }
        }

        // 3. If all voices are active, steal the best candidate:
        // Priority:
        // Tier 2: Voices in release (!held && !sustained) -> steal oldest
        // Tier 1: Sustained voices (!held && sustained) -> steal oldest
        // Tier 0: Currently held voices (held) -> steal oldest
        if (!pick) {
            int best_tier = -1;
            int best_idx = 0;
            uint64_t oldest_age = UINT64_MAX;
            for (int i = 0; i < p.polyphony; ++i) {
                const auto& v = voices[i];
                int tier = 0;
                if (!v.held && !v.sustained) tier = 2;
                else if (!v.held && v.sustained) tier = 1;
                else tier = 0;

                if (tier > best_tier || (tier == best_tier && v.age < oldest_age)) {
                    best_tier = tier;
                    best_idx = i;
                    oldest_age = v.age;
                }
            }
            pick = &voices[best_idx];
        }

        // Terminate any other duplicate voices for this note/channel to avoid hanging states
        for (int i = 0; i < 32; ++i) {
            if (&voices[i] != pick && voices[i].active && voices[i].channel == channel && voices[i].note == note) {
                voices[i].active = false;
                voices[i].held = false;
                voices[i].sustained = false;
            }
        }

        float from = lastPitch[channel - 1];
        pick->start(note, channel, velocity, ++age, p);
        if (from >= 0 && p.glideOn && (p.glideMode == 0 || overlap))
            pick->setPitch(from, p.glideTime, sr);
        lastPitch[channel - 1] = float(note);
        controlCountdown = 0;
    }
    void noteOff(int note, int channel = 1) {
        if (note < 0 || note > 127 || channel < 1 || channel > 16)
            return;
        if (p.monoLegato) {
            auto& key = keys[size_t((channel - 1) * 128 + note)];
            if (key.count == 0)
                return;
            if (--key.count == 0)
                key.latched = sustain[channel - 1];
            refreshMono();
            return;
        }
        for (auto& v : voices)
            if (v.active && v.note == note && v.channel == channel) {
                if (v.held) {
                    v.held = false;
                    if (sustain[channel - 1])
                        v.sustained = true;
                    else
                        v.release(p, sr);
                } else if (v.sustained && !sustain[channel - 1]) {
                    v.sustained = false;
                    v.release(p, sr);
                }
            }
    }
    void pedal(bool down, int channel) {
        if (channel < 1 || channel > 16)
            return;
        sustain[channel - 1] = down;
        if (p.monoLegato) {
            if (!down)
                for (int n = 0; n < 128; ++n)
                    keys[size_t((channel - 1) * 128 + n)].latched = false;
            refreshMono();
            return;
        }
        if (!down) {
            for (auto& v : voices)
                if (v.active && v.channel == channel && v.sustained) {
                    v.sustained = false;
                    if (!v.held)
                        v.release(p, sr);
                }
        }
    }
    void pitchBend(float semitones, int channel) {
        if (channel < 1 || channel > 16)
            return;
        bend[channel - 1] = semitones;
        controlCountdown = 0;
    }
    void allNotesOff(int channel = 0, bool immediate = false) {
        if (channel < 0 || channel > 16)
            return;
        for (int ch = 1; ch <= 16; ++ch)
            if (!channel || channel == ch) {
                lastPitch[ch - 1] = -1;
                for (int n = 0; n < 128; ++n)
                    keys[size_t((ch - 1) * 128 + n)] = {};
            }
        for (auto& v : voices)
            if (!channel || v.channel == channel) {
                if (immediate) {
                    v = Voice{};
                } else {
                    v.held = false;
                    v.sustained = false;
                    v.release(p, sr);
                }
            }
        if (channel)
            sustain[channel - 1] = false;
        else
            sustain.fill(false);
        if (p.monoLegato && channel)
            refreshMono();
    }
    int activeVoices() const {
        int n = 0;
        for (const auto& v : voices)
            n += v.active;
        return n;
    }
    // Read-only diagnostics, also used by deterministic engine regression tests.
    float monoPitch() const { return voices[0].pitch; }
    int monoNote() const { return voices[0].note; }
    float monoEnvelope() const { return voices[0].amp[0].value; }
    std::array<float, 2> tick() {
        if (controlCountdown-- <= 0) {
            control();
            controlCountdown = 15;
        }
        float l = 0, r = 0;
        for (int vi = 0; vi < 32; ++vi) {
            auto& v = voices[vi];
            if (!v.active)
                continue;
            if (vi >= p.polyphony) {
                v.release(p, sr);
            }
            v.advancePitch();
            const float glideRatio = v.pitch == float(v.note) ? 1.f :
                                     std::exp2((v.pitch - float(v.note)) / 12.f);
            std::array<float, 4> ev{};
            std::array<float, 2> av{};
            for (int i = 0; i < 4; ++i)
                ev[i] = v.env[i].tick(smooth.env[i], sr);
            for (int i = 0; i < 2; ++i)
                av[i] = v.amp[i].tick(smooth.amp[i], sr);
            bool sounding = false;
            float vl = 0, vr = 0;
            bool key = v.held || v.sustained;
            v.gate += std::clamp((key ? 1.f : 0.f) - v.gate, -1 / (sr * .008f), 1 / (sr * .004f));
            if (v.gate <= 1e-5f && !key) v.gate = 0.0f;
            for (int o = 0; o < 4; ++o) {
                v.oscGate[o] += std::clamp((p.osc[o].on ? 1.f : 0.f) - v.oscGate[o],
                                           -1 / (sr * .005f), 1 / (sr * .005f));
                if (v.oscGate[o] <= 1e-5f && !p.osc[o].on) v.oscGate[o] = 0.0f;
                if (v.oscGate[o] <= 0)
                    continue;
                float amp = 1;
                int count = 0;
                for (int a = 0; a < 2; ++a)
                    if (p.amp[a].on && p.amp[a].route[o]) {
                        amp *= av[a];
                        ++count;
                    }
                if (!count)
                    amp = v.gate;
                if (amp > 1e-5f || key)
                    sounding = true;
                for (int e = 0; e < 4; ++e)
                    if (p.env[e].on && p.env[e].route[o] && p.env[e].target == 0)
                        amp *= std::clamp(1 + smooth.env[e].mix * (ev[e] - 1), 0.f, 2.f);
                float ol = 0, orr = 0;
                for (int u = 0; u < p.osc[o].voices; ++u) {
                    float dt = std::clamp(v.inc[o][u] * glideRatio, 1e-8f, .45f);
                    auto& ph = v.phase[o][u];
                    float s = wave(ph, dt, p.osc[o].wave);
                    ph += dt;
                    if (ph >= 1)
                        ph -= 1;
                    ol += s * v.left[o][u];
                    orr += s * v.right[o][u];
                }
                for (int f = 0; f < 2; ++f) {
                    auto& wet = v.filterWet[f][o];
                    bool enabled = p.filter[f].on && p.filter[f].route[o];
                    wet += std::clamp((enabled ? 1.f : 0.f) - wet, -1 / (sr * .005f),
                                      1 / (sr * .005f));
                    if (wet <= 0) {
                        for (auto& channel : v.filters[f][o])
                            channel.reset();
                        continue;
                    }
                    float dryL = ol, dryR = orr, g = v.filterDrive[f];
                    if (g > 1.0001f) {
                        ol = std::tanh(ol * g) / std::sqrt(g);
                        orr = std::tanh(orr * g) / std::sqrt(g);
                    }
                    ol = lerp(dryL, v.filters[f][o][0].tick(ol), wet);
                    orr = lerp(dryR, v.filters[f][o][1].tick(orr), wet);
                }
                float gain = oscGain[o] * amp * v.velocity * v.oscGate[o];
                vl += ol * gain;
                vr += orr * gain;
            }
            if (v.stealRemaining > 0) {
                float t = float(v.stealRemaining) / 64;
                vl = lerp(vl, v.stealL, t);
                vr = lerp(vr, v.stealR, t);
                --v.stealRemaining;
            }
            v.lastL = vl;
            v.lastR = vr;
            l += vl;
            r += vr;
            if (!key && !sounding && v.stealRemaining == 0) {
                v.active = false;
                v.lastL = v.lastR = 0;
            }
        }
        ++sampleCount;
        return effects(l, r);
    }

private:
    struct HeldKey {
        int count = 0;
        bool latched = false;
        float velocity = 0;
        uint64_t order = 0;
    };
    std::array<HeldKey, 16 * 128> keys{};
    std::array<float, 16> lastPitch{};
    bool anyKeyDown() const {
        for (const auto& key : keys)
            if (key.count > 0)
                return true;
        return false;
    }
    void selectMono(int note, int channel, float velocity, bool overlap) {
        auto& v = voices[0];
        const float from = v.active ? v.pitch : lastPitch[channel - 1];
        const bool legato = v.active && (v.held || v.sustained);
        if (!legato)
            v.start(note, channel, velocity, age, p);
        else {
            v.note = note;
            v.channel = channel;
            v.velocity = velocity;
            v.age = age;
        }
        const auto& key = keys[size_t((channel - 1) * 128 + note)];
        v.held = key.count > 0;
        v.sustained = key.latched;
        v.setPitch(from >= 0 ? from : float(note),
                   from >= 0 && p.glideOn && (p.glideMode == 0 || overlap) ? p.glideTime : 0, sr);
        lastPitch[channel - 1] = float(note);
        controlCountdown = 0;
    }
    void refreshMono() {
        int best = -1;
        for (int i = 0; i < int(keys.size()); ++i)
            if ((keys[i].count > 0 || keys[i].latched) &&
                (best < 0 || keys[i].order > keys[best].order))
                best = i;
        auto& v = voices[0];
        if (best < 0)
            v.release(p, sr);
        else if (!v.active || v.note != best % 128 || v.channel != best / 128 + 1)
            selectMono(best % 128, best / 128 + 1, keys[best].velocity, anyKeyDown());
        else {
            v.held = keys[best].count > 0;
            v.sustained = keys[best].latched;
        }
    }
    Patch p, smooth;
    float sr = 44100;
    bool ready = false;
    int controlCountdown = 0;
    uint64_t age = 0, sampleCount = 0;
    std::array<Voice, 32> voices{};
    std::array<bool, 16> sustain{};
    std::array<float, 16> bend{};
    std::array<float, 4> oscGain{};
    std::array<float, 2> filterDrive{};
    std::array<DelayLine, 2> delay;
    Reverb reverb;
    std::array<std::array<Biquad, 3>, 2> eq;
    std::array<float, 2> previousX{}, previousY{};
    std::array<float, 5> fxMix{};
    int delayTail = 0, reverbTail = 0;
    float dcCoeff = .995f, masterTarget = 0;
    float detector = 0, master = 0, distGain = 1, makeup = 1, threshold = 1, attackCoeff = 0,
          releaseCoeff = 0;
    void control() {
        float t = 1 - std::exp(-16 / (sr * .02f));
        auto sm = [&](float& a, float b) { a = lerp(a, b, t); };
        for (int o = 0; o < 4; ++o) {
            auto& a = smooth.osc[o];
            const auto& b = p.osc[o];
            sm(a.vol, b.vol);
            sm(a.detune, b.detune);
            sm(a.stereo, b.stereo);
            sm(a.pan, b.pan);
            sm(a.tune, b.tune);
            oscGain[o] = db(a.vol);
        }
        for (int f = 0; f < 2; ++f) {
            auto& a = smooth.filter[f];
            auto& b = p.filter[f];
            sm(a.cutoff, b.cutoff);
            sm(a.res, b.res);
            sm(a.drive, b.drive);
            filterDrive[f] = db(a.drive);
        }
        for (int e = 0; e < 4; ++e) {
            float mix = smooth.env[e].mix;
            smooth.env[e] = p.env[e];
            smooth.env[e].mix = lerp(mix, p.env[e].mix, t);
        }
        smooth.amp = p.amp;
        sm(smooth.distDrive, p.distDrive);
        sm(smooth.distMix, p.distMix);
        sm(smooth.delayTime, p.delayTime);
        sm(smooth.delayFeedback, p.delayFeedback);
        sm(smooth.delayMix, p.delayMix);
        sm(smooth.reverbSize, p.reverbSize);
        sm(smooth.reverbDamp, p.reverbDamp);
        sm(smooth.reverbMix, p.reverbMix);
        sm(smooth.masterWidth, p.masterWidth);
        sm(smooth.eqLow, p.eqLow);
        sm(smooth.eqMid, p.eqMid);
        sm(smooth.eqHigh, p.eqHigh);
        if (p.eqOn || fxMix[1] > 0)
            for (auto& c : eq) {
                c[0].peak(180, smooth.eqLow, sr);
                c[1].peak(1000, smooth.eqMid, sr);
                c[2].peak(std::min(5000.f, sr * .3f), smooth.eqHigh, sr);
            }
        masterTarget = p.masterOn ? db(p.masterGain) : 0.f;
        distGain = db(smooth.distDrive);
        threshold = db(p.compThreshold);
        makeup = db(p.compMakeup);
        attackCoeff = std::exp(-1 / (sr * p.compAttack));
        releaseCoeff = std::exp(-1 / (sr * p.compRelease));
        for (auto& v : voices)
            if (v.active) {
                float base = 440 * std::exp2((float(v.note) - 69 + bend[v.channel - 1]) / 12);
                float f1_cut_env = 0.f, f1_res_env = 0.f, f1_drv_env = 0.f;
                float f2_cut_env = 0.f, f2_res_env = 0.f, f2_drv_env = 0.f;
                for (int e = 0; e < 4; ++e) {
                    if (p.env[e].on) {
                        float m = v.env[e].value * smooth.env[e].mix;
                        if (p.env[e].target == 3) f1_cut_env += m * 6.0f;
                        else if (p.env[e].target == 4) f1_res_env += m * 4.0f;
                        else if (p.env[e].target == 5) f2_cut_env += m * 6.0f;
                        else if (p.env[e].target == 6) f2_res_env += m * 4.0f;
                        else if (p.env[e].target == 7) {
                            f1_drv_env += m * 18.0f;
                            f2_drv_env += m * 18.0f;
                        }
                    }
                }
                for (int f = 0; f < 2; ++f) {
                    float drv = smooth.filter[f].drive + (f == 0 ? f1_drv_env : f2_drv_env);
                    v.filterDrive[f] = db(std::clamp(drv, 0.f, 40.f));
                }
                for (int o = 0; o < 4; ++o) {
                    auto& osc = smooth.osc[o];
                    float pitch = osc.tune, cut = 0, pan_mod = 0;
                    for (int e = 0; e < 4; ++e)
                        if (p.env[e].on && p.env[e].route[o]) {
                            float m = v.env[e].value * smooth.env[e].mix;
                            if (p.env[e].target == 1)
                                pitch += m * 24;
                            else if (p.env[e].target == 2)
                                cut += m * 6;
                            else if (p.env[e].target == 8)
                                pan_mod += m;
                        }
                    int n = p.osc[o].voices;
                    float norm = 1 / std::sqrt(float(n));
                    for (int u = 0; u < n; ++u) {
                        float spread = n == 1 ? 0 : 2.f * float(u) / float(n - 1) - 1;
                        v.inc[o][u] = std::clamp(
                            base * std::exp2((pitch + spread * osc.detune * .01f) / 12) / sr, 1e-8f,
                            .45f);
                        float pan = std::clamp(osc.pan + pan_mod + spread * osc.stereo, -1.f, 1.f);
                        float angle = (pan + 1) * pi * .25f;
                        v.left[o][u] = std::cos(angle) * norm;
                        v.right[o][u] = std::sin(angle) * norm;
                    }
                    for (int f = 0; f < 2; ++f)
                        if ((p.filter[f].on && p.filter[f].route[o]) || v.filterWet[f][o] > 0) {
                            float f_cut = (f == 0 ? f1_cut_env : f2_cut_env) + cut;
                            float f_res = (f == 0 ? f1_res_env : f2_res_env);
                            float cutoff = std::clamp(smooth.filter[f].cutoff * std::exp2(f_cut), 20.0f, sr * 0.44f);
                            float res = std::clamp(smooth.filter[f].res + f_res, 0.5f, 8.0f);
                            for (auto& channel : v.filters[f][o])
                                channel.set(cutoff, res, p.filter[f].type, sr);
                        }
                }
            }
    }
    std::array<float, 2> effects(float l, float r) {
        const std::array<bool, 5> enabled{p.distOn, p.eqOn, p.compOn, p.delayOn, p.reverbOn};
        for (int i = 0; i < 5; ++i)
            fxMix[i] +=
                std::clamp((enabled[i] ? 1.f : 0.f) - fxMix[i], -1 / (sr * .01f), 1 / (sr * .01f));
        std::array<float, 2> a{l, r};
        if (fxMix[0] > 0)
            for (auto& x : a)
                x = lerp(x, std::tanh(x * distGain) / std::sqrt(distGain),
                         smooth.distMix * fxMix[0]);
        if (fxMix[1] > 0)
            for (int c = 0; c < 2; ++c) {
                float y = a[c];
                for (auto& b : eq[c])
                    y = b.tick(y);
                a[c] = lerp(a[c], y, fxMix[1]);
            }
        if (fxMix[2] > 0) {
            float level = std::max(std::abs(a[0]), std::abs(a[1]));
            float coef = level > detector ? attackCoeff : releaseCoeff;
            detector = lerp(level, detector, coef);
            float gain =
                detector > threshold ? std::pow(detector / threshold, 1 / p.compRatio - 1) : 1;
            for (auto& x : a)
                x *= lerp(1.f, gain * makeup, fxMix[2]);
        }
        // Delay and reverb receive silence while bypassed, so old tails decay naturally.
        if (p.delayOn)
            delayTail = int(sr * 60);
        if (p.reverbOn)
            reverbTail = int(sr * 60);
        if (delayTail > 0) {
            --delayTail;
            for (int c = 0; c < 2; ++c) {
                float d = delay[c].read(smooth.delayTime * sr);
                delay[c].push(a[c] * fxMix[3] + d * smooth.delayFeedback);
                a[c] = lerp(a[c], d, smooth.delayMix * fxMix[3]);
            }
        }
        if (reverbTail > 0) {
            --reverbTail;
            auto rev =
                reverb.tick(a[0] * fxMix[4], a[1] * fxMix[4], smooth.reverbSize, smooth.reverbDamp);
            for (int c = 0; c < 2; ++c)
                a[c] = lerp(a[c], rev[c], smooth.reverbMix * fxMix[4]);
        }
        float mid = (a[0] + a[1]) * .5f, side = (a[0] - a[1]) * .5f * smooth.masterWidth;
        a = {mid + side, mid - side};
        master += std::clamp(masterTarget - master, -1 / (sr * .02f), 1 / (sr * .02f));
        for (int c = 0; c < 2; ++c) {
            float x = a[c] * master, y = x - previousX[c] + dcCoeff * previousY[c];
            previousX[c] = x;
            previousY[c] = y;
            a[c] = std::tanh(y);
            if (!std::isfinite(a[c]))
                a[c] = 0;
        }
        return a;
    }
};

} // namespace digidaw::xosc

namespace xosc = digidaw::xosc;

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace digidaw::xaudio {

constexpr double pi = 3.14159265358979323846;

inline double dbGain(double x) {
    return std::pow(10.0, x / 20.0);
}

inline double gainDb(double x) {
    return 20.0 * std::log10(std::max(x, 1.e-12));
}

inline double clean(double x) {
    return std::isfinite(x) ? x : 0.0;
}

struct Param {
    std::string id{};
    std::string name{};
    std::string unit{};
    float lo{0.0f};
    float hi{1.0f};
    float initial{0.0f};
    bool logarithmic{false};
};

inline std::vector<Param> parameters(int kind) {
    std::vector<Param> p{
        {"bypass", "Bypass", "", 0.0f, 1.0f, 0.0f, false},
        {"input", "Input", "dB", -24.0f, 24.0f, 0.0f, false},
        {"output", "Output", "dB", -24.0f, 24.0f, 0.0f, false},
        {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f, false}
    };

    auto add = [&](std::string id, std::string name, std::string unit, float lo, float hi,
                   float def, bool log = false) {
        p.push_back({std::move(id), std::move(name), std::move(unit), lo, hi, def, log});
    };

    if (kind == 0) { // X-Eq (6 Bands)
        const float f[]{40.0f, 120.0f, 500.0f, 2000.0f, 6000.0f, 16000.0f};
        for (int b = 0; b < 6; ++b) {
            auto s = std::to_string(b + 1);
            add("eq" + s + "freq", "Band " + s + " frequency", "Hz", 20.0f, 20000.0f, f[b], true);
            add("eq" + s + "gain", "Band " + s + " gain", "dB", -18.0f, 18.0f, 0.0f, false);
            add("eq" + s + "q", "Band " + s + " Q", "", 0.15f, 12.0f, 0.707f, true);
            add("eq" + s + "type", "Band " + s + " type", "", 0.0f, 4.0f, 0.0f, false);
        }
    }

    auto dynamics = [&](const std::string& id, const std::string& label) {
        add(id + "threshold", label + " threshold", "dB", -60.0f, 0.0f, -18.0f, false);
        add(id + "ratio", label + " ratio", ":1", 1.0f, 20.0f, 4.0f, true);
        add(id + "attack", label + " attack", "ms", 0.1f, 200.0f, 15.0f, true);
        add(id + "release", label + " release", "ms", 10.0f, 2000.0f, 150.0f, true);
        add(id + "knee", label + " knee", "dB", 0.0f, 24.0f, 6.0f, false);
        add(id + "makeup", label + " makeup", "dB", -12.0f, 24.0f, 0.0f, false);
    };

    if (kind == 1) { // X-Compressor
        dynamics("", "Compressor");
        add("schp", "Sidechain high-pass", "Hz", 20.0f, 2000.0f, 80.0f, true);
        add("external", "External sidechain", "", 0.0f, 1.0f, 0.0f, false);
    }

    if (kind == 2) { // X-Multiband (4 Bands)
        add("cross1", "Crossover 1", "Hz", 40.0f, 350.0f, 150.0f, true);
        add("cross2", "Crossover 2", "Hz", 400.0f, 3000.0f, 1000.0f, true);
        add("cross3", "Crossover 3", "Hz", 3500.0f, 12000.0f, 5000.0f, true);
        for (int b = 0; b < 4; ++b) {
            auto s = std::to_string(b + 1);
            dynamics("mb" + s, "Band " + s);
            add("mb" + s + "solo", "Band " + s + " solo", "", 0.0f, 1.0f, 0.0f, false);
            add("mb" + s + "mute", "Band " + s + " mute", "", 0.0f, 1.0f, 0.0f, false);
        }
    }

    if (kind == 3) { // X-Reverb
        add("predelay", "Pre-delay", "ms", 0.0f, 200.0f, 20.0f, false);
        add("decay", "Decay RT60", "s", 0.2f, 12.0f, 2.4f, true);
        add("damping", "HF damping", "Hz", 800.0f, 18000.0f, 6500.0f, true);
        add("lowcut", "Wet low-cut", "Hz", 20.0f, 1000.0f, 120.0f, true);
        add("width", "Stereo width", "%", 0.0f, 150.0f, 100.0f, false);
        add("diffusion", "Diffusion", "%", 0.0f, 100.0f, 75.0f, false);
        p[3].initial = 25.0f; // Default Reverb mix = 25%
    }

    if (kind == 4) { // X-Distortion
        add("drive", "Drive", "dB", 0.0f, 36.0f, 12.0f, false);
        add("tone", "Tone", "Hz", 400.0f, 20000.0f, 9000.0f, true);
        add("bias", "Asymmetry", "%", -75.0f, 75.0f, 0.0f, false);
    }

    if (kind == 5) { // X-Limiter
        add("threshold", "Threshold", "dB", -36.0f, 0.0f, -6.0f, false);
        add("ceiling", "Ceiling", "dB", -24.0f, 0.0f, -1.0f, false);
        add("release", "Release", "ms", 5.0f, 1000.0f, 100.0f, true);
        p[2].name = "Pre-limit gain";
        p[3].name = "Drive mix";
    }

    return p;
}

struct Biquad {
    double b0{1.0}, b1{0.0}, b2{0.0}, a1{0.0}, a2{0.0}, z1{0.0}, z2{0.0};

    void reset() noexcept {
        z1 = 0.0;
        z2 = 0.0;
    }

    double tick(double x) noexcept {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void set(int type, double f, double gain, double q, double fs) noexcept {
        f = std::clamp(f, 5.0, fs * 0.45);
        q = std::max(0.15, q);
        double w = 2.0 * pi * f / fs;
        double c = std::cos(w);
        double s = std::sin(w);
        double alpha = s / (2.0 * q);
        double A = std::pow(10.0, gain / 40.0);
        double a0 = 1.0;

        if (type == 0) { // Peaking / Bell
            b0 = 1.0 + alpha * A;
            b1 = -2.0 * c;
            b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;
            a1 = -2.0 * c;
            a2 = 1.0 - alpha / A;
        } else if (type == 1 || type == 2) { // Shelving
            double t = 2.0 * std::sqrt(A) * alpha;
            if (type == 1) { // Low shelf
                b0 = A * ((A + 1.0) - (A - 1.0) * c + t);
                b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * c);
                b2 = A * ((A + 1.0) - (A - 1.0) * c - t);
                a0 = (A + 1.0) + (A - 1.0) * c + t;
                a1 = -2.0 * ((A - 1.0) + (A + 1.0) * c);
                a2 = (A + 1.0) + (A - 1.0) * c - t;
            } else { // High shelf
                b0 = A * ((A + 1.0) + (A - 1.0) * c + t);
                b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c);
                b2 = A * ((A + 1.0) + (A - 1.0) * c - t);
                a0 = (A + 1.0) - (A - 1.0) * c + t;
                a1 = 2.0 * ((A - 1.0) - (A + 1.0) * c);
                a2 = (A + 1.0) - (A - 1.0) * c - t;
            }
        } else { // Cut filters (3: Low pass, 4: High pass)
            double sign = (type == 3) ? -1.0 : 1.0;
            b0 = (1.0 + sign * c) / 2.0;
            b1 = -sign * (1.0 + sign * c);
            b2 = b0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * c;
            a2 = 1.0 - alpha;
        }

        b0 /= a0;
        b1 /= a0;
        b2 /= a0;
        a1 /= a0;
        a2 /= a0;
    }

    [[nodiscard]] double magnitude(double f, double fs) const noexcept {
        double w = 2.0 * pi * f / fs;
        double nr = b0 + b1 * std::cos(w) + b2 * std::cos(2.0 * w);
        double ni = -b1 * std::sin(w) - b2 * std::sin(2.0 * w);
        double dr = 1.0 + a1 * std::cos(w) + a2 * std::cos(2.0 * w);
        double di = -a1 * std::sin(w) - a2 * std::sin(2.0 * w);
        return std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
    }
};

struct Split {
    std::array<Biquad, 2> low{};
    std::array<Biquad, 2> high{};

    void set(double f, double fs) noexcept {
        for (auto& b : low) {
            b.set(3, f, 0.0, std::sqrt(0.5), fs);
        }
        for (auto& b : high) {
            b.set(4, f, 0.0, std::sqrt(0.5), fs);
        }
    }

    std::array<double, 2> tick(double x) noexcept {
        return {low[1].tick(low[0].tick(x)), high[1].tick(high[0].tick(x))};
    }

    double allpass(double x) noexcept {
        auto y = tick(x);
        return y[0] + y[1];
    }
};

inline double reduction(double level, double threshold, double ratio, double knee) noexcept {
    double over = level - threshold;
    double slope = 1.0 - 1.0 / ratio;
    if (knee > 0.0 && over > -knee / 2.0 && over < knee / 2.0) {
        return slope * (over + knee / 2.0) * (over + knee / 2.0) / (2.0 * knee);
    }
    return over > 0.0 ? slope * over : 0.0;
}

struct Compressor {
    double gr{0.0};

    double tick(double detector, double threshold, double ratio, double knee, double attack,
                double release, double fs) noexcept {
        double target = reduction(gainDb(detector), threshold, ratio, knee);
        double c = std::exp(-1.0 / (0.001 * (target > gr ? attack : release) * fs));
        gr = c * gr + (1.0 - c) * target;
        return dbGain(-gr);
    }
};

struct Delay {
    std::vector<double> data{};
    size_t pos{0};

    void prepare(size_t n) {
        data.assign(std::max(size_t(2), n), 0.0);
        pos = 0;
    }

    [[nodiscard]] double read(size_t n) const noexcept {
        if (data.empty()) return 0.0;
        n = std::min(n, data.size() - 1);
        return data[(pos + data.size() - n) % data.size()];
    }

    void push(double x) noexcept {
        if (data.empty()) return;
        data[pos] = x;
        pos = (pos + 1) % data.size();
    }

    double allpass(double x, double g) noexcept {
        if (data.empty()) return x;
        double z = read(data.size() - 1);
        double y = z - g * x;
        push(x + g * y);
        return y;
    }
};

inline double saturate(double sample, double driveDb, double biasPercent) noexcept {
    const double bias = biasPercent * 0.01;
    return std::tanh(sample * dbGain(driveDb) + bias) - std::tanh(bias);
}

class Engine {
public:
    explicit Engine(int k)
        : kind(k), spec(parameters(k)), target(spec.size()), value(spec.size()) {
        for (size_t i = 0; i < spec.size(); ++i) {
            target[i] = value[i] = spec[i].initial;
        }
    }

    void prepare(double sampleRate) {
        fs = std::clamp(sampleRate, 8000.0, 384000.0);
        smooth = std::exp(-1.0 / (0.02 * fs));
        value = target;
        eq = {};
        cross = {};
        phase = {};
        comp = {};
        sc = {};
        wetHP = {};
        damp.fill(0.0);
        gr.fill(0.0f);
        band_levels.fill(0.0f);
        counter = 0;
        toneState.fill(0.0);
        dcInput.fill(0.0);
        dcOutput.fill(0.0);
        limiterGain = 1.0;

        for (auto& d : pre) {
            d.prepare(static_cast<size_t>(fs * 0.21) + 2);
        }

        const double times[]{0.0297, 0.0371, 0.0411, 0.0437, 0.0531, 0.0617, 0.0719, 0.0793};
        for (size_t j = 0; j < 8; ++j) {
            length[j] = static_cast<size_t>(times[j] * fs);
            lines[j].prepare(length[j] + 1);
        }

        for (int c = 0; c < 2; ++c) {
            for (int j = 0; j < 2; ++j) {
                diff[c][j].prepare(static_cast<size_t>(fs * (0.0031 + 0.0017 * j + 0.00031 * c)) + 1);
            }
        }
        update();
    }

    void set(size_t i, double v) noexcept {
        if (i < target.size()) {
            target[i] = std::clamp(clean(v), static_cast<double>(spec[i].lo), static_cast<double>(spec[i].hi));
        }
    }

    [[nodiscard]] double get(size_t i) const noexcept {
        return (i < target.size()) ? target[i] : 0.0;
    }

    void tick(double& left, double& right, double sideL = 0.0, double sideR = 0.0, bool hasSide = false) noexcept {
        for (size_t i = 0; i < value.size(); ++i) {
            value[i] = smooth * value[i] + (1.0 - smooth) * target[i];
        }
        if ((counter++ % 32) == 0) {
            update();
        }

        double original[2]{clean(left), clean(right)};
        double x[2]{original[0] * dbGain(value[1]), original[1] * dbGain(value[1])};
        double dry[2]{x[0], x[1]};

        if (kind == 0) { // X-Eq
            for (int c = 0; c < 2; ++c) {
                for (auto& b : eq[c]) {
                    x[c] = b.tick(x[c]);
                }
            }
        }

        if (kind == 1) { // X-Compressor
            bool external = target[11] > 0.5 && hasSide;
            double dl = sc[0].tick(external ? clean(sideL) : x[0]);
            double dr = sc[1].tick(external ? clean(sideR) : x[1]);
            double g = comp[0].tick(std::max(std::abs(dl), std::abs(dr)), value[4], value[5],
                                    value[8], value[6], value[7], fs) *
                       dbGain(value[9]);
            x[0] *= g;
            x[1] *= g;
            gr[0] = static_cast<float>(comp[0].gr);
        }

        if (kind == 2) { // X-Multiband
            double bands[2][4]{};
            bool solo = false;
            for (int b = 0; b < 4; ++b) {
                solo |= (target[7 + b * 8 + 6] > 0.5);
            }
            for (int c = 0; c < 2; ++c) {
                auto a = cross[c][0].tick(x[c]);
                auto b = cross[c][1].tick(a[1]);
                auto d = cross[c][2].tick(b[1]);
                bands[c][0] = phase[c][1].allpass(phase[c][0].allpass(a[0]));
                bands[c][1] = phase[c][2].allpass(b[0]);
                bands[c][2] = d[0];
                bands[c][3] = d[1];
                dry[c] = bands[c][0] + bands[c][1] + bands[c][2] + bands[c][3];
                x[c] = 0.0;
            }
            for (int b = 0; b < 4; ++b) {
                int i = 7 + b * 8;
                double in_lvl = std::max(std::abs(bands[0][b]), std::abs(bands[1][b]));
                band_levels[b] = static_cast<float>(std::max(static_cast<double>(band_levels[b]) * 0.9995, in_lvl));
                double g = comp[b].tick(in_lvl,
                                        value[i], value[i + 1], value[i + 4], value[i + 2],
                                        value[i + 3], fs) *
                           dbGain(value[i + 5]);
                double gate = (1.0 - value[i + 7]) * (solo ? value[i + 6] : 1.0);
                for (int c = 0; c < 2; ++c) {
                    x[c] += bands[c][b] * g * gate;
                }
                gr[b] = static_cast<float>(comp[b].gr);
            }
        }

        if (kind == 3) { // X-Reverb
            double input[2]{};
            for (int c = 0; c < 2; ++c) {
                double n = value[4] * 0.001 * fs;
                size_t a = static_cast<size_t>(n);
                double frac = n - static_cast<double>(a);
                double v = (a == 0) ? x[c] : pre[c].read(a);
                double w = pre[c].read(a + 1);
                input[c] = v + (w - v) * frac;
                pre[c].push(x[c]);
                for (auto& d : diff[c]) {
                    input[c] = d.allpass(input[c], value[9] * 0.007);
                }
            }
            std::array<double, 8> v{}, h{};
            double dc = std::exp(-2.0 * pi * std::min(value[6], fs * 0.45) / fs);
            for (int j = 0; j < 8; ++j) {
                double z = lines[j].read(length[j]);
                damp[j] = (1.0 - dc) * z + dc * damp[j];
                v[j] = damp[j];
                h[j] = v[j];
            }
            for (int stride = 1; stride < 8; stride *= 2) {
                for (int base = 0; base < 8; base += 2 * stride) {
                    for (int k = 0; k < stride; ++k) {
                        double a = h[base + k];
                        double b = h[base + k + stride];
                        h[base + k] = a + b;
                        h[base + k + stride] = a - b;
                    }
                }
            }
            for (int j = 0; j < 8; ++j) {
                double feedback = std::pow(10.0, -3.0 * static_cast<double>(length[j]) / (fs * value[5]));
                double injection = (input[0] + ((j % 2) ? -input[1] : input[1])) * 0.25;
                lines[j].push(injection + h[j] * 0.3535533905932738 * feedback);
            }
            double l = (v[0] + v[1] - v[2] - v[3] + v[4] + v[5] - v[6] - v[7]) * 0.5;
            double r = (v[0] - v[1] - v[2] + v[3] + v[4] - v[5] - v[6] + v[7]) * 0.5;
            double mid = (l + r) * 0.5;
            double side = (l - r) * 0.5 * value[8] * 0.01;
            x[0] = wetHP[0].tick(mid + side);
            x[1] = wetHP[1].tick(mid - side);
        }

        if (kind == 4) { // X-Distortion
            const double toneC = std::exp(-2.0 * pi * std::min(value[5], fs * 0.45) / fs);
            const double dcC = std::exp(-2.0 * pi * 20.0 / fs);
            for (int c = 0; c < 2; ++c) {
                const double shaped = saturate(x[c], value[4], value[6]);
                toneState[c] = (1.0 - toneC) * shaped + toneC * toneState[c];
                const double blocked = toneState[c] - dcInput[c] + dcC * dcOutput[c];
                dcInput[c] = toneState[c];
                dcOutput[c] = blocked;
                x[c] = blocked;
            }
        }

        if (kind == 5) { // X-Limiter
            const double boost = dbGain(-value[4]);
            const double blend = value[3] * 0.01;
            const double gain = ((1.0 - blend) + blend * boost) * dbGain(value[2]);
            x[0] *= gain;
            x[1] *= gain;

            const double ceiling = dbGain(target[5]);
            const double peak = std::max(std::abs(x[0]), std::abs(x[1]));
            const double required = (peak > ceiling) ? (ceiling / peak) : 1.0;
            const double releaseC = std::exp(-1.0 / (0.001 * value[6] * fs));
            limiterGain = std::min(required, releaseC * limiterGain + (1.0 - releaseC));
            gr[0] = static_cast<float>(-gainDb(limiterGain));

            const double bypass = value[0];
            left = clean(x[0] * limiterGain * (1.0 - bypass) + original[0] * bypass);
            right = clean(x[1] * limiterGain * (1.0 - bypass) + original[1] * bypass);
            return;
        }

        double mix = value[3] * 0.01;
        double bypass = value[0];
        double out = dbGain(value[2]);
        left = clean(((dry[0] * (1.0 - mix) + x[0] * mix) * out) * (1.0 - bypass) + original[0] * bypass);
        right = clean(((dry[1] * (1.0 - mix) + x[1] * mix) * out) * (1.0 - bypass) + original[0] * bypass);
    }

    int kind{0};
    std::vector<Param> spec{};
    std::array<float, 4> gr{};
    std::array<float, 4> band_levels{};

private:
    void update() noexcept {
        if (kind == 0) {
            for (int c = 0; c < 2; ++c) {
                for (int b = 0; b < 6; ++b) {
                    int i = 4 + b * 4;
                    eq[c][b].set(static_cast<int>(std::round(target[i + 3])), value[i], value[i + 1],
                                 value[i + 2], fs);
                }
            }
        }
        if (kind == 1) {
            for (auto& b : sc) {
                b.set(4, value[10], 0.0, 0.70710678, fs);
            }
        }
        if (kind == 2) {
            for (int c = 0; c < 2; ++c) {
                for (int i = 0; i < 3; ++i) {
                    cross[c][i].set(value[4 + i], fs);
                }
                phase[c][0].set(value[5], fs);
                phase[c][1].set(value[6], fs);
                phase[c][2].set(value[6], fs);
            }
        }
        if (kind == 3) {
            for (auto& b : wetHP) {
                b.set(4, value[7], 0.0, 0.70710678, fs);
            }
        }
    }

    double fs{48000.0};
    double smooth{0.0};
    double limiterGain{1.0};
    std::array<double, 2> toneState{}, dcInput{}, dcOutput{};
    unsigned counter{0};
    std::vector<double> target{}, value{};
    std::array<std::array<Biquad, 6>, 2> eq{};
    std::array<std::array<Split, 3>, 2> cross{}, phase{};
    std::array<Compressor, 4> comp{};
    std::array<Biquad, 2> sc{}, wetHP{};
    std::array<Delay, 2> pre{};
    std::array<std::array<Delay, 2>, 2> diff{};
    std::array<Delay, 8> lines{};
    std::array<size_t, 8> length{};
    std::array<double, 8> damp{};
};

} // namespace digidaw::xaudio

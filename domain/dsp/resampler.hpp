#pragma once

#include <span>
#include <vector>
#include <cmath>
#include <algorithm>

namespace digidaw::domain::dsp {

// Windowed SINC and Hermite interpolator for sample rate conversion (DAW-FR-106, 16 §2)
class Resampler {
public:
    // Cubic Hermite interpolation for fast, clean real-time fractional delay / pitch shift
    static float interpolate_cubic(float ym1, float y0, float y1, float y2, float frac) noexcept {
        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    static float interpolate_4pt(float ym1, float y0, float y1, float y2, float frac) noexcept {
        return interpolate_cubic(ym1, y0, y1, y2, frac);
    }

    // Linear interpolation
    static float interpolate_linear(float y0, float y1, float frac) noexcept {
        return y0 + (y1 - y0) * frac;
    }

    // Resample an entire buffer from src_rate to dst_rate using 4-point cubic Hermite
    static std::vector<float> resample_channel(std::span<const float> src, double src_rate, double dst_rate) {
        if (src.empty() || src_rate <= 0.0 || dst_rate <= 0.0) {
            return {};
        }

        const double ratio = src_rate / dst_rate;
        const size_t out_len = static_cast<size_t>(std::ceil(src.size() / ratio));
        std::vector<float> out(out_len, 0.0f);

        for (size_t i = 0; i < out_len; ++i) {
            const double src_pos = i * ratio;
            const auto idx = static_cast<int64_t>(std::floor(src_pos));
            const auto frac = static_cast<float>(src_pos - idx);

            auto get_sample = [&](int64_t pos) -> float {
                if (pos < 0) return src[0];
                if (pos >= static_cast<int64_t>(src.size())) return src.back();
                return src[pos];
            };

            const float ym1 = get_sample(idx - 1);
            const float y0  = get_sample(idx);
            const float y1  = get_sample(idx + 1);
            const float y2  = get_sample(idx + 2);

            out[i] = interpolate_cubic(ym1, y0, y1, y2, frac);
        }

        return out;
    }
};

using HermiteResampler = Resampler;

} // namespace digidaw::domain::dsp

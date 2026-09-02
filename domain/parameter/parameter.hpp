#pragma once

#include "formula_evaluator.hpp"
#include "../time/time_map.hpp"
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace digidaw::domain {

using ParamId = uint32_t;

enum class DisplayType : uint8_t {
    Linear,
    Logarithmic,
    Decibel,
    Percentage,
    Integer,
    Boolean
};

struct ParameterMapping {
    DisplayType type{DisplayType::Linear};
    float min_val{0.0f};
    float max_val{1.0f};
    std::string unit{""};

    [[nodiscard]] float to_plain(float norm) const noexcept {
        norm = std::clamp(norm, 0.0f, 1.0f);
        switch (type) {
            case DisplayType::Logarithmic: {
                // e.g. 20 Hz to 20000 Hz
                if (min_val <= 0.0f) return norm * max_val;
                return min_val * std::pow(max_val / min_val, norm);
            }
            case DisplayType::Decibel: {
                // norm 0.0 -> -inf / min_val (-60dB), norm 1.0 -> max_val (0dB or +6dB)
                if (norm <= 0.0001f) return min_val;
                return min_val + norm * (max_val - min_val);
            }
            default:
                return min_val + norm * (max_val - min_val);
        }
    }

    [[nodiscard]] float to_normalized(float plain) const noexcept {
        switch (type) {
            case DisplayType::Logarithmic: {
                if (min_val <= 0.0f || plain <= min_val) return 0.0f;
                if (plain >= max_val) return 1.0f;
                return std::log(plain / min_val) / std::log(max_val / min_val);
            }
            default: {
                if (std::abs(max_val - min_val) < 1e-6f) return 0.0f;
                return std::clamp((plain - min_val) / (max_val - min_val), 0.0f, 1.0f);
            }
        }
    }

    [[nodiscard]] std::string format_display(float norm) const {
        float val = to_plain(norm);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);

        switch (type) {
            case DisplayType::Decibel:
                if (norm <= 0.0001f) return "-inf dB";
                oss << (val >= 0.0f ? "+" : "") << val << " dB";
                return oss.str();
            case DisplayType::Percentage:
                oss << (norm * 100.0f) << " %";
                return oss.str();
            case DisplayType::Logarithmic:
                if (val >= 1000.0f) {
                    oss << (val / 1000.0f) << " kHz";
                } else {
                    oss << static_cast<int>(val) << " Hz";
                }
                return oss.str();
            case DisplayType::Integer:
                return std::to_string(static_cast<int>(std::round(val)));
            case DisplayType::Boolean:
                return (norm >= 0.5f) ? "On" : "Off";
            default:
                oss << val << (unit.empty() ? "" : " " + unit);
                return oss.str();
        }
    }
};

class Parameter {
public:
    Parameter(ParamId id, std::string name, ParameterMapping mapping, float default_normalized = 0.5f)
        : id_(id), name_(std::move(name)), mapping_(std::move(mapping)),
          normalized_(std::clamp(default_normalized, 0.0f, 1.0f)),
          default_normalized_(std::clamp(default_normalized, 0.0f, 1.0f)) {}

    [[nodiscard]] ParamId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] float normalized() const noexcept { return normalized_; }
    [[nodiscard]] float default_normalized() const noexcept { return default_normalized_; }
    [[nodiscard]] const ParameterMapping& mapping() const noexcept { return mapping_; }

    void set_normalized(float val) noexcept {
        normalized_ = std::clamp(val, 0.0f, 1.0f);
    }

    void reset_to_default() noexcept {
        normalized_ = default_normalized_;
    }

    [[nodiscard]] float plain_value() const noexcept {
        return mapping_.to_plain(normalized_);
    }

    [[nodiscard]] std::string display_value() const {
        return mapping_.format_display(normalized_);
    }

private:
    ParamId id_;
    std::string name_;
    ParameterMapping mapping_;
    float normalized_;
    float default_normalized_;
};

enum class CurveInterpolation : uint8_t {
    Linear = 0,
    Step = 1,
    Smooth = 2,
    Exponential = 3
};

struct AutomationPoint {
    Tick tick{0};
    float value{0.0f}; // Normalized 0..1
    CurveInterpolation interp{CurveInterpolation::Linear};
    float tension{0.0f}; // -1.0 to +1.0 curve bend
};

class AutomationCurve {
public:
    explicit AutomationCurve(ParamId target_param) : target_param_(target_param) {}

    [[nodiscard]] ParamId target_param() const noexcept { return target_param_; }
    [[nodiscard]] const std::string& formula() const noexcept { return formula_; }
    void set_formula(std::string formula) { formula_ = std::move(formula); }

    void add_point(Tick t, float val, CurveInterpolation interp = CurveInterpolation::Linear, float tension = 0.0f) {
        val = std::clamp(val, 0.0f, 1.0f);
        points_.erase(
            std::remove_if(points_.begin(), points_.end(), [t](const AutomationPoint& p) { return p.tick == t; }),
            points_.end());
        points_.push_back(AutomationPoint{t, val, interp, tension});
        std::sort(points_.begin(), points_.end(), [](const AutomationPoint& a, const AutomationPoint& b) {
            return a.tick < b.tick;
        });
    }

    void clear_points() noexcept {
        points_.clear();
    }

    [[nodiscard]] const std::vector<AutomationPoint>& points() const noexcept {
        return points_;
    }

    [[nodiscard]] float evaluate(Tick t) const noexcept {
        if (points_.empty()) return 0.5f;
        if (points_.size() == 1 || t <= points_.front().tick) {
            float base = points_.front().value;
            return apply_formula(base);
        }
        if (t >= points_.back().tick) {
            float base = points_.back().value;
            return apply_formula(base);
        }

        // Binary search to find segment
        auto it = std::upper_bound(
            points_.begin(), points_.end(), t,
            [](Tick val, const AutomationPoint& p) { return val < p.tick; });

        auto prev = it - 1;
        auto next = it;

        const Tick duration = next->tick - prev->tick;
        if (duration <= 0) return apply_formula(prev->value);

        const float progress = static_cast<float>(t - prev->tick) / static_cast<float>(duration);
        float raw_value = interpolate(prev->value, next->value, progress, prev->interp, prev->tension);
        return apply_formula(raw_value);
    }

private:
    float interpolate(float y0, float y1, float x, CurveInterpolation interp, float tension) const noexcept {
        switch (interp) {
            case CurveInterpolation::Step:
                return y0;
            case CurveInterpolation::Smooth: {
                // Cosine smooth step
                float ft = x * 3.14159265f;
                float f = (1.0f - std::cos(ft)) * 0.5f;
                return y0 * (1.0f - f) + y1 * f;
            }
            case CurveInterpolation::Exponential: {
                float bent_x = (tension >= 0.0f)
                    ? std::pow(x, 1.0f + tension * 3.0f)
                    : 1.0f - std::pow(1.0f - x, 1.0f - tension * 3.0f);
                return y0 + (y1 - y0) * bent_x;
            }
            case CurveInterpolation::Linear:
            default:
                return y0 + (y1 - y0) * x;
        }
    }

    float apply_formula(float x) const noexcept {
        if (formula_.empty()) {
            return std::clamp(x, 0.0f, 1.0f);
        }
        double res = FormulaEvaluator::evaluate(formula_, static_cast<double>(x));
        return static_cast<float>(std::clamp(res, 0.0, 1.0));
    }

    ParamId target_param_{0};
    std::string formula_{};
    std::vector<AutomationPoint> points_{};
};

} // namespace digidaw::domain

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace valcraft {

enum class RendererQuality : std::uint8_t {
    High = 0,
    Medium,
    Low,
    Dynamic,
};

struct RendererQualitySettings {
    RendererQuality resolved_quality = RendererQuality::High;
    int cloud_steps = 7;
    float cloud_detail = 1.0F;
    int glow_downsample = 2;
    float post_detail_scale = 1.0F;
    bool high_precision_hdr = true;
    int ocean_wave_count = 6;
    float ocean_detail_scale = 1.0F;
    std::size_t precipitation_drop_budget = 6'000U;
    std::size_t precipitation_impact_budget = 96U;
    float precipitation_radius = 38.0F;

    auto operator==(const RendererQualitySettings&) const -> bool = default;
};

[[nodiscard]] constexpr auto resolve_renderer_quality_settings(
    RendererQuality quality,
    int width,
    int height) noexcept -> RendererQualitySettings {
    auto resolved = quality;
    if (resolved == RendererQuality::Dynamic) {
        const auto safe_width = static_cast<std::int64_t>(width > 0 ? width : 1);
        const auto safe_height = static_cast<std::int64_t>(height > 0 ? height : 1);
        const auto pixel_count = safe_width * safe_height;
        if (pixel_count <= 1920LL * 1080LL) {
            resolved = RendererQuality::High;
        } else if (pixel_count <= 2560LL * 1440LL) {
            resolved = RendererQuality::Medium;
        } else {
            resolved = RendererQuality::Low;
        }
    }

    switch (resolved) {
    case RendererQuality::Medium:
        return {
            resolved,
            4,
            0.60F,
            3,
            0.65F,
            false,
            4,      // Quatre vagues géométriques.
            0.70F,  // Petites rides réduites.
            3'000U,
            48U,
            28.0F,
        };

    case RendererQuality::Low:
        return {
            resolved,
            2,
            0.0F,
            4,
            0.0F,
            false,
            3,      // Uniquement les trois grandes vagues.
            0.0F,   // Aucun calcul de ride par fragment.
            1'200U,
            16U,
            18.0F,
        };

    case RendererQuality::High:
    case RendererQuality::Dynamic:
    default:
        return {
            RendererQuality::High,
            7,
            1.0F,
            2,
            1.0F,
            true,
            6,
            1.0F,
            6'000U,
            96U,
            38.0F,
        };
    }
}

[[nodiscard]] constexpr auto gpu_elapsed_nanoseconds_to_milliseconds(std::uint64_t elapsed_nanoseconds) noexcept
    -> double {
    return static_cast<double>(elapsed_nanoseconds) / 1'000'000.0;
}

struct RendererAdaptiveQualityState {
    RendererQuality resolved_quality = RendererQuality::High;
    double frame_time_ema_ms = 0.0;
    double frame_time_p95_ms = 0.0;
    std::size_t sample_count = 0;
};

class RendererAdaptiveQualityController {
public:
    static constexpr double kFrameBudgetMs = 1000.0 / 60.0;
    static constexpr std::size_t kSampleWindowSize = 60U;

    void reset(RendererQuality configured_quality, int width, int height) noexcept {
        configured_quality_ = configured_quality;
        resolved_quality_ = resolve_renderer_quality_settings(configured_quality, width, height).resolved_quality;
        viewport_pixel_count_ = pixel_count(width, height);
        initialized_ = true;
        clear_history(true);
    }

    [[nodiscard]] auto settings(RendererQuality configured_quality, int width, int height) noexcept
        -> RendererQualitySettings {
        configure(configured_quality, width, height);
        return resolve_renderer_quality_settings(resolved_quality_, width, height);
    }

    [[nodiscard]] auto update(
        RendererQuality configured_quality,
        int width,
        int height,
        double frame_time_ms,
        bool sample_valid = true) noexcept -> RendererQualitySettings {
        configure(configured_quality, width, height);
        if (configured_quality_ != RendererQuality::Dynamic || !sample_valid || !std::isfinite(frame_time_ms) ||
            frame_time_ms <= 0.0) {
            return resolve_renderer_quality_settings(resolved_quality_, width, height);
        }

        const auto sample = std::clamp(frame_time_ms, 0.05, 250.0);
        add_sample(sample);
        ++samples_since_change_;

        constexpr auto kDowngradeP95Ms = kFrameBudgetMs * 1.10;
        constexpr auto kDowngradeEmaMs = kFrameBudgetMs * 1.14;
        constexpr auto kSevereFrameMs = kFrameBudgetMs * 1.65;
        constexpr auto kUpgradeP95Ms = kFrameBudgetMs * 0.78;
        constexpr auto kUpgradeEmaMs = kFrameBudgetMs * 0.72;

        const auto overloaded = sample_count_ >= 4U &&
                                (frame_time_p95_ms_ > kDowngradeP95Ms || frame_time_ema_ms_ > kDowngradeEmaMs);
        const auto severely_overloaded = sample > kSevereFrameMs || frame_time_ema_ms_ > kSevereFrameMs;
        const auto comfortably_under_budget = sample_count_ >= 30U &&
                                              frame_time_p95_ms_ < kUpgradeP95Ms &&
                                              frame_time_ema_ms_ < kUpgradeEmaMs;

        over_budget_samples_ = overloaded ? over_budget_samples_ + 1U : 0U;
        severe_samples_ = severely_overloaded ? severe_samples_ + 1U : 0U;
        under_budget_samples_ = comfortably_under_budget ? under_budget_samples_ + 1U : 0U;

        if (resolved_quality_ != RendererQuality::Low &&
            (severe_samples_ >= 2U || over_budget_samples_ >= 8U)) {
            set_resolved_quality(lower_quality(resolved_quality_));
        } else if (resolved_quality_ != RendererQuality::High &&
                   samples_since_change_ >= 240U && under_budget_samples_ >= 210U) {
            set_resolved_quality(higher_quality(resolved_quality_));
        }

        return resolve_renderer_quality_settings(resolved_quality_, width, height);
    }

    [[nodiscard]] auto state() const noexcept -> RendererAdaptiveQualityState {
        return {resolved_quality_, frame_time_ema_ms_, frame_time_p95_ms_, sample_count_};
    }

private:
    [[nodiscard]] static constexpr auto pixel_count(int width, int height) noexcept -> std::uint64_t {
        const auto safe_width = static_cast<std::uint64_t>(width > 0 ? width : 1);
        const auto safe_height = static_cast<std::uint64_t>(height > 0 ? height : 1);
        return safe_width * safe_height;
    }

    [[nodiscard]] static constexpr auto lower_quality(RendererQuality quality) noexcept -> RendererQuality {
        return quality == RendererQuality::High ? RendererQuality::Medium : RendererQuality::Low;
    }

    [[nodiscard]] static constexpr auto higher_quality(RendererQuality quality) noexcept -> RendererQuality {
        return quality == RendererQuality::Low ? RendererQuality::Medium : RendererQuality::High;
    }

    void configure(RendererQuality configured_quality, int width, int height) noexcept {
        const auto new_pixel_count = pixel_count(width, height);
        if (!initialized_ || configured_quality_ != configured_quality) {
            reset(configured_quality, width, height);
            return;
        }

        if (configured_quality_ != RendererQuality::Dynamic) {
            resolved_quality_ = configured_quality_;
            viewport_pixel_count_ = new_pixel_count;
            return;
        }

        const auto previous_pixels = static_cast<double>(std::max<std::uint64_t>(viewport_pixel_count_, 1U));
        const auto resolution_ratio = static_cast<double>(new_pixel_count) / previous_pixels;
        viewport_pixel_count_ = new_pixel_count;
        if (resolution_ratio < 0.80 || resolution_ratio > 1.25) {
            // Je repars d'un niveau cohérent après un changement important de résolution.
            resolved_quality_ = resolve_renderer_quality_settings(RendererQuality::Dynamic, width, height).resolved_quality;
            clear_history(true);
        }
    }

    void add_sample(double sample) noexcept {
        samples_[sample_cursor_] = sample;
        sample_cursor_ = (sample_cursor_ + 1U) % samples_.size();
        sample_count_ = std::min(sample_count_ + 1U, samples_.size());
        if (sample_count_ == 1U) {
            frame_time_ema_ms_ = sample;
        } else {
            constexpr auto kEmaAlpha = 0.12;
            frame_time_ema_ms_ += (sample - frame_time_ema_ms_) * kEmaAlpha;
        }

        auto sorted_samples = samples_;
        std::sort(sorted_samples.begin(), sorted_samples.begin() + static_cast<std::ptrdiff_t>(sample_count_));
        const auto percentile_rank = (sample_count_ * 95U + 99U) / 100U;
        const auto percentile_index = std::max<std::size_t>(percentile_rank, 1U) - 1U;
        frame_time_p95_ms_ = sorted_samples[percentile_index];
    }

    void set_resolved_quality(RendererQuality quality) noexcept {
        resolved_quality_ = quality;
        // Je vide la fenêtre pour mesurer le coût réel du nouveau niveau avant toute autre décision.
        clear_history(false);
    }

    void clear_history(bool clear_metrics) noexcept {
        samples_.fill(0.0);
        sample_cursor_ = 0U;
        sample_count_ = 0U;
        samples_since_change_ = 0U;
        over_budget_samples_ = 0U;
        severe_samples_ = 0U;
        under_budget_samples_ = 0U;
        if (clear_metrics) {
            frame_time_ema_ms_ = 0.0;
            frame_time_p95_ms_ = 0.0;
        }
    }

    std::array<double, kSampleWindowSize> samples_ {};
    RendererQuality configured_quality_ = RendererQuality::High;
    RendererQuality resolved_quality_ = RendererQuality::High;
    std::uint64_t viewport_pixel_count_ = 1U;
    std::size_t sample_cursor_ = 0U;
    std::size_t sample_count_ = 0U;
    std::size_t samples_since_change_ = 0U;
    std::size_t over_budget_samples_ = 0U;
    std::size_t severe_samples_ = 0U;
    std::size_t under_budget_samples_ = 0U;
    double frame_time_ema_ms_ = 0.0;
    double frame_time_p95_ms_ = 0.0;
    bool initialized_ = false;
};

} // namespace valcraft

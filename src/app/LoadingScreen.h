#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace valcraft {

enum class LoadingScreenTheme : std::uint8_t {
    Standard,
    Maritime,
};

enum class LoadingPhase : std::uint8_t {
    Preparation,
    SaveRead,
    SaveRestore,
    LegacyMigration,
    Generation,
    Fluids,
    Lighting,
    Meshing,
    GpuUpload,
    ShipPreparation,
    Finalization,
    Complete,
};

struct LoadingQuoteView {
    std::string_view line1 {};
    std::string_view line2 {};
    std::string_view author {};

    auto operator==(const LoadingQuoteView&) const -> bool = default;
};

struct LoadingQuoteSelection {
    LoadingQuoteView current {};
    LoadingQuoteView next {};
    float blend = 0.0F;
    std::size_t current_index = 0U;
    std::uint64_t cycle = 0U;
};

struct LoadingScreenView {
    std::string_view title {"VALCRAFT"};
    std::string_view detail {"CHARGEMENT DU MONDE"};
    float progress = 0.0F;
    LoadingScreenTheme theme = LoadingScreenTheme::Standard;
    LoadingQuoteView current_quote {};
    LoadingQuoteView next_quote {};
    float quote_blend = 0.0F;
    float animation_phase = 0.0F;
};

struct LoadingScreenLayout {
    float viewport_width = 1.0F;
    float viewport_height = 1.0F;
    float content_x = 0.0F;
    float content_width = 1.0F;
    float panel_y = 0.0F;
    float panel_height = 1.0F;
    float track_x = 0.0F;
    float track_y = 0.0F;
    float track_width = 1.0F;
    float track_height = 1.0F;
    float title_y = 0.0F;
    float detail_y = 0.0F;
    float horizon_y = 0.0F;
    float quote_y = 0.0F;
    float author_y = 0.0F;
    float title_pixel_size = 1.0F;
    float detail_pixel_size = 1.0F;
    float quote_pixel_size = 1.0F;
    bool compact = false;
};

namespace loading_screen_detail {

struct PhaseRange {
    float begin = 0.0F;
    float end = 1.0F;
};

[[nodiscard]] constexpr auto phase_range(LoadingPhase phase) noexcept -> PhaseRange {
    switch (phase) {
    case LoadingPhase::Preparation: return {0.00F, 0.05F};
    case LoadingPhase::SaveRead: return {0.05F, 0.18F};
    case LoadingPhase::SaveRestore: return {0.18F, 0.23F};
    case LoadingPhase::LegacyMigration: return {0.23F, 0.28F};
    case LoadingPhase::Generation: return {0.28F, 0.50F};
    case LoadingPhase::Fluids: return {0.50F, 0.56F};
    case LoadingPhase::Lighting: return {0.56F, 0.72F};
    case LoadingPhase::Meshing: return {0.72F, 0.87F};
    case LoadingPhase::GpuUpload: return {0.87F, 0.96F};
    case LoadingPhase::ShipPreparation: return {0.96F, 0.99F};
    case LoadingPhase::Finalization: return {0.99F, 1.00F};
    case LoadingPhase::Complete: return {1.00F, 1.00F};
    }
    return {0.0F, 1.0F};
}

[[nodiscard]] constexpr auto mix_seed(std::uint64_t value) noexcept -> std::uint64_t {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

} // namespace loading_screen_detail

class LoadingProgressTracker {
public:
    void reset() noexcept {
        progress_ = 0.0F;
        phase_ = LoadingPhase::Preparation;
        completed_ = false;
    }

    [[nodiscard]] auto update(LoadingPhase phase, float local_progress) noexcept -> float {
        if (completed_ || !std::isfinite(local_progress)) {
            return progress_;
        }
        if (phase == LoadingPhase::Complete) {
            return complete();
        }

        const auto range = loading_screen_detail::phase_range(phase);
        const auto normalized = std::clamp(local_progress, 0.0F, 1.0F);
        const auto candidate = range.begin + (range.end - range.begin) * normalized;
        // Je reserve la valeur 100 % a la validation explicite de toutes les ressources.
        progress_ = std::max(progress_, std::clamp(candidate, 0.0F, kIncompleteProgressCeiling));
        if (static_cast<std::uint8_t>(phase) > static_cast<std::uint8_t>(phase_)) {
            phase_ = phase;
        }
        return progress_;
    }

    [[nodiscard]] auto update_absolute(float progress) noexcept -> float {
        if (completed_ || !std::isfinite(progress)) {
            return progress_;
        }
        progress_ = std::max(progress_, std::clamp(progress, 0.0F, kIncompleteProgressCeiling));
        return progress_;
    }

    [[nodiscard]] auto complete() noexcept -> float {
        progress_ = 1.0F;
        phase_ = LoadingPhase::Complete;
        completed_ = true;
        return progress_;
    }

    [[nodiscard]] auto progress() const noexcept -> float {
        return progress_;
    }

    [[nodiscard]] auto phase() const noexcept -> LoadingPhase {
        return phase_;
    }

    [[nodiscard]] auto completed() const noexcept -> bool {
        return completed_;
    }

private:
    static constexpr float kIncompleteProgressCeiling = 0.999F;
    float progress_ = 0.0F;
    LoadingPhase phase_ = LoadingPhase::Preparation;
    bool completed_ = false;
};

[[nodiscard]] constexpr auto is_loading_screen_character_supported(char character) noexcept -> bool {
    if ((character >= 'A' && character <= 'Z') ||
        (character >= 'a' && character <= 'z') ||
        (character >= '0' && character <= '9')) {
        return true;
    }
    constexpr std::string_view kSupportedPunctuation {" ()+%',.:!?-"};
    return kSupportedPunctuation.find(character) != std::string_view::npos;
}

[[nodiscard]] inline auto maritime_loading_quotes() noexcept -> std::span<const LoadingQuoteView> {
    // Je garde le catalogue en stockage statique pour ne rien allouer pendant l'animation.
    static constexpr std::array<LoadingQuoteView, 8> kQuotes {{
        {"A CEUX QUI SONGENT A UN VOYAGE,", "JE DIRAIS : PARTEZ.", "JOSHUA SLOCUM"},
        {"IL FAUT CONNAITRE LA MER,", "ET SAVOIR QU'ON LA CONNAIT.", "JOSHUA SLOCUM"},
        {"CONNAITRE LES LOIS DU VENT", "APAISE L'ESPRIT EN MER.", "JOSHUA SLOCUM"},
        {"AUCUNE AVENTURE NE VIENT", "A CELUI QUI LA RECLAME.", "JOSEPH CONRAD"},
        {"LA MER, IL FAUT L'AVOUER,", "N'A PAS DE GENEROSITE.", "JOSEPH CONRAD"},
        {"NOUS AVIONS ATTEINT", "L'AME NUE DE L'HOMME.", "ERNEST SHACKLETON"},
        {"ATTENDRE ET LAISSER LA NATURE AGIR", "EXIGE UNE GRANDE FORCE D'ESPRIT.", "FRIDTJOF NANSEN"},
        {"AUSSI GRAND QUE FUT CE DESSEIN,", "JE LE PENSAIS POSSIBLE.", "JAMES COOK"},
    }};
    return kQuotes;
}

[[nodiscard]] inline auto make_maritime_loading_quote_view(
    std::uint64_t seed,
    double elapsed_seconds) noexcept -> LoadingQuoteSelection {
    constexpr auto kQuoteDurationSeconds = 5.0;
    constexpr auto kCrossFadeSeconds = 0.6;
    const auto quotes = maritime_loading_quotes();
    if (quotes.empty()) {
        return {};
    }

    const auto safe_elapsed = std::isfinite(elapsed_seconds) && elapsed_seconds > 0.0 ? elapsed_seconds : 0.0;
    const auto raw_cycle = std::floor(safe_elapsed / kQuoteDurationSeconds);
    const auto maximum_cycle = static_cast<double>(std::numeric_limits<std::uint64_t>::max());
    const auto cycle = raw_cycle >= maximum_cycle
                           ? std::numeric_limits<std::uint64_t>::max()
                           : static_cast<std::uint64_t>(raw_cycle);
    const auto quote_count = static_cast<std::uint64_t>(quotes.size());
    const auto first_index = loading_screen_detail::mix_seed(seed) % quote_count;
    const auto current_index = static_cast<std::size_t>((first_index + (cycle % quote_count)) % quote_count);
    const auto next_index = (current_index + 1U) % quotes.size();
    const auto elapsed_in_cycle = std::fmod(safe_elapsed, kQuoteDurationSeconds);
    const auto blend_begin = kQuoteDurationSeconds - kCrossFadeSeconds;
    const auto blend = static_cast<float>(std::clamp(
        (elapsed_in_cycle - blend_begin) / kCrossFadeSeconds,
        0.0,
        1.0));

    return {quotes[current_index], quotes[next_index], blend, current_index, cycle};
}

[[nodiscard]] inline auto loading_animation_phase(double elapsed_seconds, double period_seconds = 8.0) noexcept
    -> float {
    if (!std::isfinite(elapsed_seconds) || elapsed_seconds <= 0.0 ||
        !std::isfinite(period_seconds) || period_seconds <= 0.0) {
        return 0.0F;
    }
    return static_cast<float>(std::fmod(elapsed_seconds, period_seconds) / period_seconds);
}

[[nodiscard]] inline auto make_loading_screen_layout(
    LoadingScreenTheme theme,
    int width,
    int height) noexcept -> LoadingScreenLayout {
    const auto viewport_width = static_cast<float>(std::max(width, 1));
    const auto viewport_height = static_cast<float>(std::max(height, 1));
    const auto shortest_side = std::min(viewport_width, viewport_height);
    const auto margin = std::clamp(shortest_side * 0.04F, 8.0F, 48.0F);
    const auto maximum_content_width = std::max(1.0F, viewport_width - margin * 2.0F);
    const auto minimum_content_width = std::min(360.0F, maximum_content_width);
    const auto desired_content_width = theme == LoadingScreenTheme::Maritime
                                           ? viewport_width * 0.72F
                                           : viewport_width * 0.52F;
    const auto content_width = std::clamp(
        desired_content_width,
        minimum_content_width,
        std::min(900.0F, maximum_content_width));
    const auto content_x = (viewport_width - content_width) * 0.5F;
    const auto maximum_panel_height = std::max(1.0F, viewport_height - margin * 2.0F);
    const auto minimum_panel_height = std::min(theme == LoadingScreenTheme::Maritime ? 150.0F : 180.0F, maximum_panel_height);
    const auto panel_height = std::clamp(
        viewport_height * (theme == LoadingScreenTheme::Maritime ? 0.34F : 0.30F),
        minimum_panel_height,
        std::min(theme == LoadingScreenTheme::Maritime ? 360.0F : 300.0F, maximum_panel_height));
    const auto panel_y = theme == LoadingScreenTheme::Maritime
                             ? viewport_height - margin - panel_height
                             : (viewport_height - panel_height) * 0.5F;
    const auto horizontal_padding = std::clamp(content_width * 0.07F, 18.0F, 54.0F);
    const auto track_height = std::clamp(viewport_height * 0.032F, 18.0F, 34.0F);
    const auto title_pixel_size = std::floor(std::clamp(
        std::min(viewport_width / 145.0F, viewport_height / 82.0F),
        2.0F,
        8.0F));
    const auto detail_pixel_size = std::floor(std::clamp(title_pixel_size * 0.50F, 1.0F, 4.0F));
    const auto quote_pixel_size = std::floor(std::clamp(
        std::min(viewport_width / 300.0F, viewport_height / 230.0F),
        1.0F,
        4.0F));
    const auto track_y = theme == LoadingScreenTheme::Maritime
                             ? panel_y + panel_height * 0.28F
                             : panel_y + panel_height * 0.56F;
    const auto quote_y = theme == LoadingScreenTheme::Maritime
                             ? panel_y + panel_height * 0.59F
                             : track_y + track_height + detail_pixel_size * 12.0F;

    return {
        viewport_width,
        viewport_height,
        content_x,
        content_width,
        panel_y,
        panel_height,
        content_x + horizontal_padding,
        track_y,
        std::max(1.0F, content_width - horizontal_padding * 2.0F),
        track_height,
        theme == LoadingScreenTheme::Maritime ? std::max(margin, viewport_height * 0.10F) : panel_y + 24.0F,
        theme == LoadingScreenTheme::Maritime
            ? std::max(margin, viewport_height * 0.10F) + title_pixel_size * 8.0F + 8.0F
            : panel_y + 24.0F + title_pixel_size * 8.0F + 6.0F,
        viewport_height * 0.43F,
        quote_y,
        quote_y + quote_pixel_size * 17.0F,
        title_pixel_size,
        detail_pixel_size,
        quote_pixel_size,
        width < 800 || height < 450,
    };
}

} // namespace valcraft

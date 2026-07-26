#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace valcraft {

enum class VisualPipeline : std::uint8_t {
    LegacyVoxel = 0,
    ModernStylized,
};

[[nodiscard]] constexpr auto is_modern_visual_pipeline(
    VisualPipeline pipeline) noexcept -> bool {
    return pipeline == VisualPipeline::ModernStylized;
}

// Je calibre seulement la sortie moderne : LegacyVoxel conserve exactement
// les valeurs historiques utilisees comme reference differentielle.
[[nodiscard]] constexpr auto visual_pipeline_post_contrast(
    VisualPipeline pipeline,
    float contrast) noexcept -> float {
    return is_modern_visual_pipeline(pipeline)
               ? 1.0F + (contrast - 1.0F) / 3.0F
               : contrast;
}

[[nodiscard]] constexpr auto visual_pipeline_glow_threshold(
    VisualPipeline pipeline,
    float threshold) noexcept -> float {
    if (!is_modern_visual_pipeline(pipeline)) {
        return threshold;
    }
    const auto raised = threshold + 0.12F;
    return raised < 1.0F ? raised : 1.0F;
}

[[nodiscard]] constexpr auto visual_pipeline_glow_strength(
    VisualPipeline pipeline,
    float strength) noexcept -> float {
    return is_modern_visual_pipeline(pipeline)
               ? strength * 0.55F
               : strength;
}

// Je centralise les noms publics pour garder la ligne de commande et l'audit
// strictement alignes sur le meme contrat.
[[nodiscard]] constexpr auto visual_pipeline_name(VisualPipeline pipeline) noexcept
    -> std::string_view {
    switch (pipeline) {
    case VisualPipeline::ModernStylized:
        return "modern";
    case VisualPipeline::LegacyVoxel:
    default:
        return "legacy";
    }
}

[[nodiscard]] constexpr auto parse_visual_pipeline(std::string_view name) noexcept
    -> std::optional<VisualPipeline> {
    if (name == "legacy") {
        return VisualPipeline::LegacyVoxel;
    }
    if (name == "modern") {
        return VisualPipeline::ModernStylized;
    }
    return std::nullopt;
}

} // namespace valcraft

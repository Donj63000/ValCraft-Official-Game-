#include "render/ArchitecturalMesher.h"
#include "render/BackroomsVisibility.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace valcraft {

namespace {

// Je garde toute la classification dans ArchitecturalMesher.h : ce fichier
// consomme ainsi le meme contrat que ChunkMesher et le remeshing du monde.

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::size_t kMaximumSampledCells = 4U * 1024U * 1024U;
constexpr std::uint16_t kUvUnitsPerBlock = 256U;

struct SampledVolume {
    BlockCoord min {};
    BlockCoord max {};
    std::size_t size_x = 0;
    std::size_t size_y = 0;
    std::size_t size_z = 0;
    std::vector<ArchitecturalCellSample> cells;

    [[nodiscard]] auto contains(BlockCoord coordinate) const noexcept -> bool {
        return coordinate.x >= min.x && coordinate.x <= max.x &&
               coordinate.y >= min.y && coordinate.y <= max.y &&
               coordinate.z >= min.z && coordinate.z <= max.z;
    }

    [[nodiscard]] auto index(BlockCoord coordinate) const noexcept -> std::size_t {
        const auto local_x = static_cast<std::size_t>(coordinate.x - min.x);
        const auto local_y = static_cast<std::size_t>(coordinate.y - min.y);
        const auto local_z = static_cast<std::size_t>(coordinate.z - min.z);
        return (local_y * size_z + local_z) * size_x + local_x;
    }

    [[nodiscard]] auto get(BlockCoord coordinate) const noexcept
        -> const ArchitecturalCellSample& {
        return cells[index(coordinate)];
    }
};

struct FaceDefinition {
    ArchitecturalFace face = ArchitecturalFace::PositiveX;
    int normal_axis = 0;
    int normal_sign = 1;
    int u_axis = 1;
    int v_axis = 2;
    std::array<float, 3> normal {1.0F, 0.0F, 0.0F};
};

// Je choisis U et V de sorte que U x V pointe toujours vers la normale. Les
// indices produits gardent donc le meme winding sur les six orientations.
constexpr std::array<FaceDefinition, 6> kFaceDefinitions {{
    {ArchitecturalFace::PositiveX, 0, 1, 1, 2, {1.0F, 0.0F, 0.0F}},
    {ArchitecturalFace::NegativeX, 0, -1, 2, 1, {-1.0F, 0.0F, 0.0F}},
    {ArchitecturalFace::PositiveY, 1, 1, 2, 0, {0.0F, 1.0F, 0.0F}},
    {ArchitecturalFace::NegativeY, 1, -1, 0, 2, {0.0F, -1.0F, 0.0F}},
    {ArchitecturalFace::PositiveZ, 2, 1, 0, 1, {0.0F, 0.0F, 1.0F}},
    {ArchitecturalFace::NegativeZ, 2, -1, 1, 0, {0.0F, 0.0F, -1.0F}},
}};

struct FaceMaskCell {
    bool visible = false;
    BlockId material_block = to_block_id(BlockType::Air);
    std::uint8_t sky_light = 15;
    std::uint8_t block_light = 0;

    auto operator==(const FaceMaskCell&) const -> bool = default;
};

struct FaceLightSample {
    std::uint8_t sky_light = 15;
    std::uint8_t block_light = 0;
};

inline constexpr int kBackroomsLightPatchSpan = 4;
inline constexpr float
    kBackroomsLightPatchMaximumVisibilityError = 0.06F;

[[nodiscard]] auto axis_value(BlockCoord coordinate, int axis) noexcept -> int {
    if (axis == 0) {
        return coordinate.x;
    }
    if (axis == 1) {
        return coordinate.y;
    }
    return coordinate.z;
}

void set_axis_value(BlockCoord& coordinate, int axis, int value) noexcept {
    if (axis == 0) {
        coordinate.x = value;
    } else if (axis == 1) {
        coordinate.y = value;
    } else {
        coordinate.z = value;
    }
}

[[nodiscard]] auto axis_min(const ArchitecturalSection& section, int axis) noexcept -> int {
    return axis_value(section.min, axis);
}

[[nodiscard]] auto axis_max(const ArchitecturalSection& section, int axis) noexcept -> int {
    return axis_value(section.max, axis);
}

[[nodiscard]] auto checked_expanded_coordinate(int value, int delta) -> int {
    const auto result = static_cast<std::int64_t>(value) +
                        static_cast<std::int64_t>(delta);
    if (result < static_cast<std::int64_t>(std::numeric_limits<int>::min()) ||
        result > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
        throw std::overflow_error("architectural halo exceeds integer coordinates");
    }
    return static_cast<int>(result);
}

[[nodiscard]] auto checked_extent(int minimum, int maximum) -> std::size_t {
    const auto extent = static_cast<std::int64_t>(maximum) -
                        static_cast<std::int64_t>(minimum) + 1;
    if (extent <= 0) {
        throw std::invalid_argument("architectural section has invalid bounds");
    }
    return static_cast<std::size_t>(extent);
}

[[nodiscard]] auto sample_volume(
    const ArchitecturalSection& section,
    const ArchitecturalSampler& sampler) -> SampledVolume {
    if (!sampler) {
        throw std::invalid_argument("architectural sampler is empty");
    }
    if (!section.valid()) {
        throw std::invalid_argument("architectural section has invalid bounds");
    }
    if (section.halo < 1 || section.halo > 8) {
        throw std::invalid_argument("architectural halo must be between one and eight");
    }

    for (int axis = 0; axis < 3; ++axis) {
        const auto core_extent = checked_extent(
            axis_min(section, axis),
            axis_max(section, axis));
        if (core_extent > 255U) {
            throw std::length_error("architectural section exceeds compact UV range");
        }
    }

    SampledVolume volume {};
    volume.min = {
        checked_expanded_coordinate(section.min.x, -section.halo),
        checked_expanded_coordinate(section.min.y, -section.halo),
        checked_expanded_coordinate(section.min.z, -section.halo),
    };
    volume.max = {
        checked_expanded_coordinate(section.max.x, section.halo),
        checked_expanded_coordinate(section.max.y, section.halo),
        checked_expanded_coordinate(section.max.z, section.halo),
    };
    volume.size_x = checked_extent(volume.min.x, volume.max.x);
    volume.size_y = checked_extent(volume.min.y, volume.max.y);
    volume.size_z = checked_extent(volume.min.z, volume.max.z);
    if (volume.size_x > kMaximumSampledCells / volume.size_y ||
        volume.size_x * volume.size_y > kMaximumSampledCells / volume.size_z) {
        throw std::length_error("architectural section is too large");
    }

    const auto cell_count = volume.size_x * volume.size_y * volume.size_z;
    volume.cells.reserve(cell_count);
    for (int y = volume.min.y; y <= volume.max.y; ++y) {
        for (int z = volume.min.z; z <= volume.max.z; ++z) {
            for (int x = volume.min.x; x <= volume.max.x; ++x) {
                auto sample = sampler(x, y, z);
                sample.sky_light = std::min<std::uint8_t>(sample.sky_light, 15U);
                sample.block_light = std::min<std::uint8_t>(sample.block_light, 15U);
                volume.cells.push_back(sample);
            }
        }
    }
    return volume;
}

[[nodiscard]] auto face_is_visible(BlockId block, BlockId neighbour) noexcept -> bool {
    if (neighbour == to_block_id(BlockType::BackroomsDesk) ||
        neighbour == to_block_id(BlockType::BackroomsChair)) {
        // Je garde le sol et le mur visibles entre les pieds des meubles. Leur
        // collision reste cubique, mais leur silhouette moderne est ajouree.
        return true;
    }
    if (!is_block_solid(neighbour)) {
        return true;
    }

    const auto glass = to_block_id(BlockType::Glass);
    if (block == glass) {
        // Je supprime aussi bien les faces verre-verre que la face du verre
        // collee a un materiau opaque.
        return false;
    }
    // Je conserve la face opaque visible a travers un bloc de verre.
    return neighbour == glass;
}

[[nodiscard]] auto make_cell_coordinate(
    const FaceDefinition& definition,
    int slice,
    int u,
    int v) noexcept -> BlockCoord {
    BlockCoord result {};
    set_axis_value(result, definition.normal_axis, slice);
    set_axis_value(result, definition.u_axis, u);
    set_axis_value(result, definition.v_axis, v);
    return result;
}

[[nodiscard]] auto offset_axis(BlockCoord coordinate, int axis, int delta) noexcept
    -> BlockCoord {
    set_axis_value(coordinate, axis, axis_value(coordinate, axis) + delta);
    return coordinate;
}

[[nodiscard]] auto face_light_sample(
    const SampledVolume& volume,
    const FaceDefinition& definition,
    BlockCoord owner) noexcept -> FaceLightSample {
    const auto& sample = volume.get(owner);
    if (!is_backrooms_architectural_block(sample.block_id)) {
        return {sample.sky_light, sample.block_light};
    }

    const auto neighbour = offset_axis(
        owner,
        definition.normal_axis,
        definition.normal_sign);
    const auto& exterior = volume.get(neighbour);
    // Je prends le ciel du volume expose et je garde l'energie propre d'un
    // luminaire emissif. Le choix reste lie a la face : aucune lumiere ne peut
    // ainsi traverser un mur depuis la piece situee de l'autre cote.
    return {
        exterior.sky_light,
        std::max(sample.block_light, exterior.block_light),
    };
}

[[nodiscard]] auto face_mask_cells_can_merge(
    const FaceMaskCell& lhs,
    const FaceMaskCell& rhs) noexcept -> bool {
    if (lhs.visible != rhs.visible ||
        lhs.material_block != rhs.material_block) {
        return false;
    }
    if (!lhs.visible) {
        return true;
    }
    if (is_backrooms_architectural_block(
            lhs.material_block)) {
        // Je fusionne les paliers d'eclairage voxel puis je les interpole aux
        // sommets. Les murs restent continus au lieu de redevenir un damier.
        return true;
    }
    return lhs.sky_light == rhs.sky_light &&
           lhs.block_light == rhs.block_light;
}

[[nodiscard]] auto surface_vertex_light_sample(
    const SampledVolume& volume,
    const FaceDefinition& definition,
    int slice,
    int vertex_u,
    int vertex_v) noexcept -> FaceLightSample {
    FaceLightSample result {0U, 0U};
    auto found = false;
    for (int delta_v = -1; delta_v <= 0; ++delta_v) {
        for (int delta_u = -1; delta_u <= 0; ++delta_u) {
            const auto owner = make_cell_coordinate(
                definition,
                slice,
                vertex_u + delta_u,
                vertex_v + delta_v);
            if (!volume.contains(owner)) {
                continue;
            }
            const auto& sample = volume.get(owner);
            if (!is_backrooms_architectural_block(
                    sample.block_id)) {
                continue;
            }
            const auto neighbour = offset_axis(
                owner,
                definition.normal_axis,
                definition.normal_sign);
            if (!volume.contains(neighbour) ||
                !face_is_visible(
                    sample.block_id,
                    volume.get(neighbour).block_id)) {
                continue;
            }
            const auto light = face_light_sample(
                volume,
                definition,
                owner);
            result.sky_light = std::max(
                result.sky_light,
                light.sky_light);
            result.block_light = std::max(
                result.block_light,
                light.block_light);
            found = true;
        }
    }
    if (found) {
        return result;
    }

    // Ce repli ne devrait concerner qu'une entree manipulee sans halo. Je
    // garde alors le coin sombre au lieu de lire hors du volume compacte.
    return {0U, 0U};
}

[[nodiscard]] auto backrooms_patch_vertex_lights(
    const SampledVolume& volume,
    const FaceDefinition& definition,
    int slice,
    int u_start,
    int v_start,
    int width,
    int height) noexcept -> std::array<FaceLightSample, 4> {
    return {{
        surface_vertex_light_sample(
            volume,
            definition,
            slice,
            u_start,
            v_start),
        surface_vertex_light_sample(
            volume,
            definition,
            slice,
            u_start + width,
            v_start),
        surface_vertex_light_sample(
            volume,
            definition,
            slice,
            u_start + width,
            v_start + height),
        surface_vertex_light_sample(
            volume,
            definition,
            slice,
            u_start,
        v_start + height),
    }};
}

[[nodiscard]] auto backrooms_patch_lighting_is_affine(
    const std::array<FaceLightSample, 4>& lights) noexcept -> bool {
    const auto sky_diagonal_a =
        static_cast<int>(lights[0].sky_light) +
        static_cast<int>(lights[2].sky_light);
    const auto sky_diagonal_b =
        static_cast<int>(lights[1].sky_light) +
        static_cast<int>(lights[3].sky_light);
    const auto block_diagonal_a =
        static_cast<int>(lights[0].block_light) +
        static_cast<int>(lights[2].block_light);
    const auto block_diagonal_b =
        static_cast<int>(lights[1].block_light) +
        static_cast<int>(lights[3].block_light);

    // Je ne conserve un grand quad que si ses deux triangles interpolent le
    // même plan sur chacun des canaux. La courbe d'obscurité ne peut alors
    // plus amplifier une cassure artificielle le long de leur diagonale.
    return sky_diagonal_a == sky_diagonal_b &&
           block_diagonal_a == block_diagonal_b;
}

[[nodiscard]] auto face_light_score(
    const FaceLightSample& light) noexcept -> int {
    return static_cast<int>(light.block_light) * 4 +
           static_cast<int>(light.sky_light);
}

[[nodiscard]] auto backrooms_patch_flips_diagonal(
    const std::array<FaceLightSample, 4>& lights) noexcept -> bool {
    return face_light_score(lights[0]) +
               face_light_score(lights[2]) >
           face_light_score(lights[1]) +
               face_light_score(lights[3]);
}

[[nodiscard]] auto interpolated_light_channel(
    const std::array<FaceLightSample, 4>& lights,
    bool sky_channel,
    bool flipped_diagonal,
    float normalized_u,
    float normalized_v) noexcept -> float {
    const auto value = [&](std::size_t index) noexcept {
        return static_cast<float>(
            sky_channel
                ? lights[index].sky_light
                : lights[index].block_light);
    };

    if (flipped_diagonal) {
        if (normalized_u + normalized_v <= 1.0F) {
            return
                value(0U) * (1.0F - normalized_u - normalized_v) +
                value(1U) * normalized_u +
                value(3U) * normalized_v;
        }
        return
            value(1U) * (1.0F - normalized_v) +
            value(2U) * (normalized_u + normalized_v - 1.0F) +
            value(3U) * (1.0F - normalized_u);
    }

    if (normalized_v <= normalized_u) {
        return
            value(0U) * (1.0F - normalized_u) +
            value(1U) * (normalized_u - normalized_v) +
            value(2U) * normalized_v;
    }
    return
        value(0U) * (1.0F - normalized_v) +
        value(2U) * normalized_u +
        value(3U) * (normalized_v - normalized_u);
}

[[nodiscard]] auto backrooms_perceptual_visibility(
    float block_light) noexcept -> float {
    const auto normalized_light = std::clamp(
        block_light / 15.0F,
        0.0F,
        1.0F);
    const auto normalized_ramp = std::clamp(
        (normalized_light -
         kBackroomsDarknessBlockLightBlackThreshold) /
            (kBackroomsDarknessBlockLightFullVisibilityThreshold -
             kBackroomsDarknessBlockLightBlackThreshold),
        0.0F,
        1.0F);

    // Je mesure l'erreur avec le meme smoothstep que le shader. Je conserve
    // volontairement ici un plancher nul : le maillage reste assez fin pour
    // un Blackout, donc tous les profils plus lisibles sont automatiquement
    // couverts sans devoir reconstruire la geometrie lors d'une transition.
    return
        normalized_ramp *
        normalized_ramp *
        (3.0F - 2.0F * normalized_ramp);
}

[[nodiscard]] auto backrooms_patch_block_samples_follow_affine_plane(
    const SampledVolume& volume,
    const FaceDefinition& definition,
    int slice,
    int u_start,
    int v_start,
    int width,
    int height,
    const std::array<FaceLightSample, 4>& corners) noexcept -> bool {
    const auto denominator = width * height;
    const auto full_visibility_light =
        15.0F *
        kBackroomsDarknessBlockLightFullVisibilityThreshold;

    for (int offset_v = 0; offset_v <= height; ++offset_v) {
        for (int offset_u = 0; offset_u <= width; ++offset_u) {
            const auto actual = surface_vertex_light_sample(
                volume,
                definition,
                slice,
                u_start + offset_u,
                v_start + offset_v);
            const auto expected_numerator =
                static_cast<int>(corners[0].block_light) * denominator +
                (static_cast<int>(corners[1].block_light) -
                 static_cast<int>(corners[0].block_light)) *
                    offset_u * height +
                (static_cast<int>(corners[3].block_light) -
                 static_cast<int>(corners[0].block_light)) *
                    offset_v * width;
            if (static_cast<int>(actual.block_light) * denominator ==
                expected_numerator) {
                continue;
            }

            // Je tolere les paliers bruts uniquement lorsque le shader les a
            // deja tous transformes en visibilite complete. Sous ce seuil, je
            // force le raffinement afin qu'un raccord de patch ne puisse plus
            // dessiner une grande couture dans la penombre.
            if (static_cast<float>(actual.block_light) >=
                    full_visibility_light &&
                static_cast<float>(expected_numerator) >=
                    full_visibility_light *
                        static_cast<float>(denominator)) {
                continue;
            }
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto backrooms_patch_needs_subdivision(
    const SampledVolume& volume,
    const FaceDefinition& definition,
    int slice,
    int u_start,
    int v_start,
    int width,
    int height) noexcept -> bool {
    if (width <= 1 && height <= 1) {
        return false;
    }

    const auto vertex_lights = backrooms_patch_vertex_lights(
        volume,
        definition,
        slice,
        u_start,
        v_start,
        width,
        height);
    if (!backrooms_patch_lighting_is_affine(
            vertex_lights)) {
        return true;
    }
    if (!backrooms_patch_block_samples_follow_affine_plane(
            volume,
            definition,
            slice,
            u_start,
            v_start,
            width,
            height,
            vertex_lights)) {
        return true;
    }
    const auto flipped_diagonal =
        backrooms_patch_flips_diagonal(vertex_lights);
    const auto inverse_width = 1.0F / static_cast<float>(width);
    const auto inverse_height = 1.0F / static_cast<float>(height);

    for (int offset_v = 0; offset_v < height; ++offset_v) {
        for (int offset_u = 0; offset_u < width; ++offset_u) {
            const auto owner = make_cell_coordinate(
                definition,
                slice,
                u_start + offset_u,
                v_start + offset_v);
            const auto actual = face_light_sample(
                volume,
                definition,
                owner);
            const auto normalized_u =
                (static_cast<float>(offset_u) + 0.5F) * inverse_width;
            const auto normalized_v =
                (static_cast<float>(offset_v) + 0.5F) * inverse_height;
            const auto predicted_block = interpolated_light_channel(
                vertex_lights,
                false,
                flipped_diagonal,
                normalized_u,
                normalized_v);
            if (std::fabs(
                    backrooms_perceptual_visibility(predicted_block) -
                    backrooms_perceptual_visibility(
                        static_cast<float>(actual.block_light))) >
                kBackroomsLightPatchMaximumVisibilityError) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] auto edge_is_exposed(
    const SampledVolume& volume,
    const FaceDefinition& definition,
    int slice,
    int u_start,
    int v_start,
    int width,
    int height,
    int edge_axis,
    int edge_sign) noexcept -> bool {
    const auto count = edge_axis == definition.u_axis ? height : width;
    for (int offset = 0; offset < count; ++offset) {
        auto cell = make_cell_coordinate(
            definition,
            slice,
            edge_axis == definition.u_axis
                ? u_start
                : u_start + offset,
            edge_axis == definition.v_axis
                ? v_start
                : v_start + offset);
        cell = offset_axis(cell, edge_axis, edge_sign);
        if (!volume.contains(cell) || is_block_solid(volume.get(cell).block_id)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto bevel_flags(
    const SampledVolume& volume,
    const FaceDefinition& definition,
    int slice,
    int u_start,
    int v_start,
    int width,
    int height,
    bool enabled) noexcept -> std::uint8_t {
    if (!enabled) {
        return 0U;
    }
    std::uint8_t flags = 0U;
    if (edge_is_exposed(
            volume,
            definition,
            slice,
            u_start,
            v_start,
            width,
            height,
            definition.u_axis,
            -1)) {
        flags |= ArchitecturalBevelNegativeU;
    }
    if (edge_is_exposed(
            volume,
            definition,
            slice,
            u_start + width - 1,
            v_start,
            1,
            height,
            definition.u_axis,
            1)) {
        flags |= ArchitecturalBevelPositiveU;
    }
    if (edge_is_exposed(
            volume,
            definition,
            slice,
            u_start,
            v_start,
            width,
            height,
            definition.v_axis,
            -1)) {
        flags |= ArchitecturalBevelNegativeV;
    }
    if (edge_is_exposed(
            volume,
            definition,
            slice,
            u_start,
            v_start + height - 1,
            width,
            1,
            definition.v_axis,
            1)) {
        flags |= ArchitecturalBevelPositiveV;
    }
    return flags;
}

void include_point(ArchitecturalBounds& bounds, float x, float y, float z) noexcept {
    if (!bounds.valid) {
        bounds = {x, y, z, x, y, z, true};
        return;
    }
    bounds.min_x = std::min(bounds.min_x, x);
    bounds.min_y = std::min(bounds.min_y, y);
    bounds.min_z = std::min(bounds.min_z, z);
    bounds.max_x = std::max(bounds.max_x, x);
    bounds.max_y = std::max(bounds.max_y, y);
    bounds.max_z = std::max(bounds.max_z, z);
}

void include_bounds(
    ArchitecturalBounds& destination,
    const ArchitecturalBounds& source) noexcept {
    if (!source.valid) {
        return;
    }
    include_point(destination, source.min_x, source.min_y, source.min_z);
    include_point(destination, source.max_x, source.max_y, source.max_z);
}

[[nodiscard]] auto fixed_uv(int blocks) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(
        static_cast<unsigned int>(blocks) *
        static_cast<unsigned int>(kUvUnitsPerBlock));
}

[[nodiscard]] auto vertex_position(
    const FaceDefinition& definition,
    int slice,
    int u,
    int v,
    int plane_offset) noexcept -> std::array<float, 3> {
    std::array<float, 3> position {};
    position[static_cast<std::size_t>(definition.normal_axis)] =
        static_cast<float>(slice + plane_offset);
    position[static_cast<std::size_t>(definition.u_axis)] =
        static_cast<float>(u);
    position[static_cast<std::size_t>(definition.v_axis)] =
        static_cast<float>(v);
    return position;
}

void append_quad(
    ArchitecturalMesh& mesh,
    const SampledVolume& volume,
    const FaceDefinition& definition,
    int slice,
    int u_start,
    int v_start,
    int width,
    int height,
    const FaceMaskCell& mask,
    bool mark_silhouette_bevels) {
    if (is_backrooms_architectural_block(mask.material_block) &&
        backrooms_patch_needs_subdivision(
            volume,
            definition,
            slice,
            u_start,
            v_start,
            width,
            height)) {
        // Je coupe seulement le rectangle dont l'interpolation perdrait une
        // source locale ou garderait deux plans lumineux différents. La
        // profondeur reste bornée par le patch de quatre cellules et chaque
        // enfant réutilise exactement les mêmes sommets que son voisin.
        if (width >= height && width > 1) {
            const auto first_width = width / 2;
            append_quad(
                mesh,
                volume,
                definition,
                slice,
                u_start,
                v_start,
                first_width,
                height,
                mask,
                mark_silhouette_bevels);
            append_quad(
                mesh,
                volume,
                definition,
                slice,
                u_start + first_width,
                v_start,
                width - first_width,
                height,
                mask,
                mark_silhouette_bevels);
            return;
        }

        const auto first_height = height / 2;
        append_quad(
            mesh,
            volume,
            definition,
            slice,
            u_start,
            v_start,
            width,
            first_height,
            mask,
            mark_silhouette_bevels);
        append_quad(
            mesh,
            volume,
            definition,
            slice,
            u_start,
            v_start + first_height,
            width,
            height - first_height,
            mask,
            mark_silhouette_bevels);
        return;
    }

    const auto plane_offset = definition.normal_sign > 0 ? 1 : 0;
    const auto p0 = vertex_position(
        definition,
        slice,
        u_start,
        v_start,
        plane_offset);
    const auto p1 = vertex_position(
        definition,
        slice,
        u_start + width,
        v_start,
        plane_offset);
    const auto p2 = vertex_position(
        definition,
        slice,
        u_start + width,
        v_start + height,
        plane_offset);
    const auto p3 = vertex_position(
        definition,
        slice,
        u_start,
        v_start + height,
        plane_offset);

    auto flags = bevel_flags(
        volume,
        definition,
        slice,
        u_start,
        v_start,
        width,
        height,
        mark_silhouette_bevels);
    if (mask.material_block == to_block_id(BlockType::Glass)) {
        flags |= ArchitecturalTransparent;
    }

    const auto first_vertex = static_cast<std::uint32_t>(mesh.vertices.size());
    const auto first_index = static_cast<std::uint32_t>(mesh.indices.size());
    const auto u_max = fixed_uv(width);
    const auto v_max = fixed_uv(height);
    const auto flat_light = FaceLightSample {
        mask.sky_light,
        mask.block_light,
    };
    auto vertex_lights = std::array<FaceLightSample, 4> {{
        flat_light,
        flat_light,
        flat_light,
        flat_light,
    }};
    if (is_backrooms_architectural_block(
            mask.material_block)) {
        vertex_lights = backrooms_patch_vertex_lights(
            volume,
            definition,
            slice,
            u_start,
            v_start,
            width,
            height);
    }
    const auto make_vertex = [&](const std::array<float, 3>& position,
                                 std::uint16_t u,
                                 std::uint16_t v,
                                 const FaceLightSample& light) {
        HardSurfaceVertex vertex {};
        vertex.x = position[0];
        vertex.y = position[1];
        vertex.z = position[2];
        vertex.nx = definition.normal[0];
        vertex.ny = definition.normal[1];
        vertex.nz = definition.normal[2];
        vertex.u_fixed = u;
        vertex.v_fixed = v;
        vertex.material_block = mask.material_block;
        vertex.sky_light = light.sky_light;
        vertex.block_light = light.block_light;
        vertex.surface_flags = flags;
        return vertex;
    };
    mesh.vertices.push_back(make_vertex(p0, 0U, 0U, vertex_lights[0]));
    mesh.vertices.push_back(make_vertex(p1, u_max, 0U, vertex_lights[1]));
    mesh.vertices.push_back(make_vertex(p2, u_max, v_max, vertex_lights[2]));
    mesh.vertices.push_back(make_vertex(p3, 0U, v_max, vertex_lights[3]));
    const auto flip_backrooms_diagonal =
        is_backrooms_architectural_block(mask.material_block) &&
        backrooms_patch_flips_diagonal(vertex_lights);
    if (flip_backrooms_diagonal) {
        // Je relie les deux coins les moins lumineux : une variation locale
        // ne dessine plus une grande diagonale claire au milieu de la nappe.
        mesh.indices.insert(
            mesh.indices.end(),
            {
                first_vertex,
                first_vertex + 1U,
                first_vertex + 3U,
                first_vertex + 1U,
                first_vertex + 2U,
                first_vertex + 3U,
            });
    } else {
        mesh.indices.insert(
            mesh.indices.end(),
            {
                first_vertex,
                first_vertex + 1U,
                first_vertex + 2U,
                first_vertex,
                first_vertex + 2U,
                first_vertex + 3U,
            });
    }

    mesh.quads.push_back({
        first_vertex,
        first_index,
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height),
        make_cell_coordinate(definition, slice, u_start, v_start),
        definition.face,
        mask.material_block,
        flags,
    });
    for (const auto& position : {p0, p1, p2, p3}) {
        include_point(mesh.bounds, position[0], position[1], position[2]);
    }
}

void build_face_direction(
    ArchitecturalMesh& mesh,
    const ArchitecturalSection& section,
    const SampledVolume& volume,
    const FaceDefinition& definition,
    bool mark_silhouette_bevels) {
    const auto normal_min = axis_min(section, definition.normal_axis);
    const auto normal_max = axis_max(section, definition.normal_axis);
    const auto u_min = axis_min(section, definition.u_axis);
    const auto u_max = axis_max(section, definition.u_axis);
    const auto v_min = axis_min(section, definition.v_axis);
    const auto v_max = axis_max(section, definition.v_axis);
    const auto u_size = static_cast<std::size_t>(u_max - u_min + 1);
    const auto v_size = static_cast<std::size_t>(v_max - v_min + 1);
    std::vector<FaceMaskCell> mask(u_size * v_size);
    std::vector<bool> consumed(u_size * v_size, false);

    for (int slice = normal_min; slice <= normal_max; ++slice) {
        std::fill(mask.begin(), mask.end(), FaceMaskCell {});
        std::fill(consumed.begin(), consumed.end(), false);
        for (int v = v_min; v <= v_max; ++v) {
            for (int u = u_min; u <= u_max; ++u) {
                const auto coordinate =
                    make_cell_coordinate(definition, slice, u, v);
                const auto& sample = volume.get(coordinate);
                if (!is_architectural_solid_block(sample.block_id)) {
                    continue;
                }
                const auto neighbour = offset_axis(
                    coordinate,
                    definition.normal_axis,
                    definition.normal_sign);
                const auto& neighbour_sample =
                    volume.get(neighbour);
                if (!face_is_visible(
                        sample.block_id,
                        neighbour_sample.block_id)) {
                    continue;
                }
                const auto surface_light = face_light_sample(
                    volume,
                    definition,
                    coordinate);
                const auto mask_index =
                    static_cast<std::size_t>(v - v_min) * u_size +
                    static_cast<std::size_t>(u - u_min);
                mask[mask_index] = {
                    true,
                    sample.block_id,
                    surface_light.sky_light,
                    surface_light.block_light,
                };
            }
        }

        for (int v = v_min; v <= v_max; ++v) {
            for (int u = u_min; u <= u_max; ++u) {
                const auto local_u = static_cast<std::size_t>(u - u_min);
                const auto local_v = static_cast<std::size_t>(v - v_min);
                const auto start_index = local_v * u_size + local_u;
                if (consumed[start_index] || !mask[start_index].visible) {
                    continue;
                }
                const auto key = mask[start_index];
                const auto maximum_span =
                    is_backrooms_architectural_block(
                        key.material_block)
                        ? kBackroomsLightPatchSpan
                        : std::numeric_limits<int>::max();
                int width = 1;
                while (u + width <= u_max &&
                       width < maximum_span) {
                    const auto candidate_index =
                        local_v * u_size +
                        static_cast<std::size_t>(u + width - u_min);
                    if (consumed[candidate_index] ||
                        !face_mask_cells_can_merge(
                            mask[candidate_index],
                            key)) {
                        break;
                    }
                    ++width;
                }

                int height = 1;
                bool can_grow = true;
                while (v + height <= v_max && can_grow &&
                       height < maximum_span) {
                    const auto candidate_v =
                        static_cast<std::size_t>(v + height - v_min);
                    for (int width_offset = 0;
                         width_offset < width;
                         ++width_offset) {
                        const auto candidate_index =
                            candidate_v * u_size +
                            static_cast<std::size_t>(
                                u + width_offset - u_min);
                        if (consumed[candidate_index] ||
                            !face_mask_cells_can_merge(
                                mask[candidate_index],
                                key)) {
                            can_grow = false;
                            break;
                        }
                    }
                    if (can_grow) {
                        ++height;
                    }
                }

                for (int height_offset = 0;
                     height_offset < height;
                     ++height_offset) {
                    const auto consumed_v =
                        static_cast<std::size_t>(
                            v + height_offset - v_min);
                    for (int width_offset = 0;
                         width_offset < width;
                         ++width_offset) {
                        const auto consumed_index =
                            consumed_v * u_size +
                            static_cast<std::size_t>(
                                u + width_offset - u_min);
                        consumed[consumed_index] = true;
                    }
                }
                append_quad(
                    mesh,
                    volume,
                    definition,
                    slice,
                    u,
                    v,
                    width,
                    height,
                    key,
                    mark_silhouette_bevels);
            }
        }
    }
}

[[nodiscard]] auto fixture_kind(BlockId block) noexcept
    -> ArchitecturalFixtureKind {
    switch (static_cast<BlockType>(block)) {
    case BlockType::TorchWallPositiveX:
        return ArchitecturalFixtureKind::WallTorchPositiveX;
    case BlockType::TorchWallNegativeX:
        return ArchitecturalFixtureKind::WallTorchNegativeX;
    case BlockType::TorchWallPositiveZ:
        return ArchitecturalFixtureKind::WallTorchPositiveZ;
    case BlockType::TorchWallNegativeZ:
        return ArchitecturalFixtureKind::WallTorchNegativeZ;
    case BlockType::Torch:
    default:
        return ArchitecturalFixtureKind::FloorTorch;
    }
}

void append_fixture(
    ArchitecturalMesh& mesh,
    BlockCoord coordinate,
    const ArchitecturalCellSample& sample) {
    const auto support = torch_support_offset(sample.block_id);
    ArchitecturalFixtureInstance fixture {};
    fixture.position_x =
        static_cast<float>(coordinate.x) + 0.5F +
        static_cast<float>(support.x) * 0.28125F;
    fixture.position_y =
        static_cast<float>(coordinate.y) +
        (is_wall_torch_block(sample.block_id) ? 0.35F : 0.44F);
    fixture.position_z =
        static_cast<float>(coordinate.z) + 0.5F +
        static_cast<float>(support.z) * 0.28125F;
    if (is_wall_torch_block(sample.block_id)) {
        constexpr float kWallHorizontal = 0.3826834324F;
        constexpr float kWallVertical = 0.9238795325F;
        fixture.direction_x =
            -static_cast<float>(support.x) * kWallHorizontal;
        fixture.direction_y = kWallVertical;
        fixture.direction_z =
            -static_cast<float>(support.z) * kWallHorizontal;
    }
    fixture.owner_cell = coordinate;
    fixture.bounds = {
        static_cast<float>(coordinate.x),
        static_cast<float>(coordinate.y),
        static_cast<float>(coordinate.z),
        static_cast<float>(coordinate.x + 1),
        static_cast<float>(coordinate.y + 1),
        static_cast<float>(coordinate.z + 1),
        true,
    };
    fixture.kind = fixture_kind(sample.block_id);
    fixture.source_block = sample.block_id;
    fixture.sky_light = sample.sky_light;
    fixture.block_light = std::max<std::uint8_t>(sample.block_light, 14U);
    mesh.fixtures.push_back(fixture);
    include_bounds(mesh.bounds, fixture.bounds);
}

void append_fixtures(
    ArchitecturalMesh& mesh,
    const ArchitecturalSection& section,
    const SampledVolume& volume) {
    for (int y = section.min.y; y <= section.max.y; ++y) {
        for (int z = section.min.z; z <= section.max.z; ++z) {
            for (int x = section.min.x; x <= section.max.x; ++x) {
                const BlockCoord coordinate {x, y, z};
                const auto& sample = volume.get(coordinate);
                if (is_architectural_fixture_block(sample.block_id)) {
                    append_fixture(mesh, coordinate, sample);
                }
            }
        }
    }
}

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= static_cast<std::uint64_t>(value);
    hash *= kFnvPrime;
}

void hash_u16(std::uint64_t& hash, std::uint16_t value) noexcept {
    hash_byte(hash, static_cast<std::uint8_t>(value & 0xFFU));
    hash_byte(hash, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        hash_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
        hash_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFULL));
    }
}

void hash_float(std::uint64_t& hash, float value) noexcept {
    hash_u32(hash, std::bit_cast<std::uint32_t>(value));
}

void hash_coordinate(std::uint64_t& hash, BlockCoord coordinate) noexcept {
    hash_u32(hash, static_cast<std::uint32_t>(coordinate.x));
    hash_u32(hash, static_cast<std::uint32_t>(coordinate.y));
    hash_u32(hash, static_cast<std::uint32_t>(coordinate.z));
}

void hash_bounds(std::uint64_t& hash, const ArchitecturalBounds& bounds) noexcept {
    hash_float(hash, bounds.min_x);
    hash_float(hash, bounds.min_y);
    hash_float(hash, bounds.min_z);
    hash_float(hash, bounds.max_x);
    hash_float(hash, bounds.max_y);
    hash_float(hash, bounds.max_z);
    hash_byte(hash, bounds.valid ? 1U : 0U);
}

} // namespace

auto ArchitecturalSection::valid() const noexcept -> bool {
    return min.x <= max.x && min.y <= max.y && min.z <= max.z;
}

auto ArchitecturalSection::contains(BlockCoord coordinate) const noexcept -> bool {
    return coordinate.x >= min.x && coordinate.x <= max.x &&
           coordinate.y >= min.y && coordinate.y <= max.y &&
           coordinate.z >= min.z && coordinate.z <= max.z;
}

ArchitecturalMesher::ArchitecturalMesher(
    ArchitecturalMesherSettings settings) noexcept
    : settings_(settings) {}

auto ArchitecturalMesher::build_mesh(
    const ArchitecturalSection& section,
    const ArchitecturalSampler& sampler,
    std::size_t vertex_reserve_hint,
    std::size_t index_reserve_hint) const -> ArchitecturalMesh {
    const auto volume = sample_volume(section, sampler);
    ArchitecturalMesh mesh {};
    mesh.vertices.reserve(vertex_reserve_hint);
    mesh.indices.reserve(index_reserve_hint);
    for (const auto& definition : kFaceDefinitions) {
        build_face_direction(
            mesh,
            section,
            volume,
            definition,
            settings_.mark_silhouette_bevels);
    }
    append_fixtures(mesh, section, volume);
    return mesh;
}

auto ArchitecturalMesher::settings() const noexcept
    -> const ArchitecturalMesherSettings& {
    return settings_;
}

auto architectural_mesh_deterministic_hash(
    const ArchitecturalMesh& mesh) noexcept -> std::uint64_t {
    auto hash = kFnvOffset;
    hash_u64(hash, static_cast<std::uint64_t>(mesh.vertices.size()));
    for (const auto& vertex : mesh.vertices) {
        hash_float(hash, vertex.x);
        hash_float(hash, vertex.y);
        hash_float(hash, vertex.z);
        hash_float(hash, vertex.nx);
        hash_float(hash, vertex.ny);
        hash_float(hash, vertex.nz);
        hash_u16(hash, vertex.u_fixed);
        hash_u16(hash, vertex.v_fixed);
        hash_byte(hash, vertex.material_block);
        hash_byte(hash, vertex.sky_light);
        hash_byte(hash, vertex.block_light);
        hash_byte(hash, vertex.surface_flags);
    }
    hash_u64(hash, static_cast<std::uint64_t>(mesh.indices.size()));
    for (const auto index : mesh.indices) {
        hash_u32(hash, index);
    }
    hash_u64(hash, static_cast<std::uint64_t>(mesh.quads.size()));
    for (const auto& quad : mesh.quads) {
        hash_u32(hash, quad.first_vertex);
        hash_u32(hash, quad.first_index);
        hash_u16(hash, quad.width);
        hash_u16(hash, quad.height);
        hash_coordinate(hash, quad.owner_cell);
        hash_byte(hash, static_cast<std::uint8_t>(quad.face));
        hash_byte(hash, quad.material_block);
        hash_byte(hash, quad.surface_flags);
    }
    hash_u64(hash, static_cast<std::uint64_t>(mesh.fixtures.size()));
    for (const auto& fixture : mesh.fixtures) {
        hash_float(hash, fixture.position_x);
        hash_float(hash, fixture.position_y);
        hash_float(hash, fixture.position_z);
        hash_float(hash, fixture.direction_x);
        hash_float(hash, fixture.direction_y);
        hash_float(hash, fixture.direction_z);
        hash_coordinate(hash, fixture.owner_cell);
        hash_bounds(hash, fixture.bounds);
        hash_byte(hash, static_cast<std::uint8_t>(fixture.kind));
        hash_byte(hash, fixture.source_block);
        hash_byte(hash, fixture.sky_light);
        hash_byte(hash, fixture.block_light);
    }
    hash_bounds(hash, mesh.bounds);
    return hash;
}

auto order_architectural_indices_for_render(
    const ArchitecturalMesh& mesh,
    std::vector<std::uint32_t>& ordered_indices,
    std::vector<std::uint8_t>& quad_index_coverage) -> std::size_t {
    ordered_indices.clear();
    ordered_indices.reserve(mesh.indices.size());
    quad_index_coverage.assign(mesh.indices.size(), 0U);

    constexpr std::size_t kQuadIndexCount = 6U;
    for (const auto& quad : mesh.quads) {
        const auto first = static_cast<std::size_t>(quad.first_index);
        if (first > mesh.indices.size() ||
            mesh.indices.size() - first < kQuadIndexCount) {
            continue;
        }
        std::fill_n(
            quad_index_coverage.begin() +
                static_cast<std::ptrdiff_t>(first),
            kQuadIndexCount,
            static_cast<std::uint8_t>(1U));
        if ((quad.surface_flags & ArchitecturalTransparent) != 0U) {
            continue;
        }
        ordered_indices.insert(
            ordered_indices.end(),
            mesh.indices.begin() + static_cast<std::ptrdiff_t>(first),
            mesh.indices.begin() +
                static_cast<std::ptrdiff_t>(first + kQuadIndexCount));
    }

    // Les meubles, rampes et fixtures peuvent se trouver entre deux groupes
    // de quads apres la fusion verticale. Je conserve chaque index non couvert
    // dans son ordre d'origine afin de ne casser aucun triangle.
    for (std::size_t index = 0U; index < mesh.indices.size(); ++index) {
        if (quad_index_coverage[index] == 0U) {
            ordered_indices.push_back(mesh.indices[index]);
        }
    }
    const auto opaque_index_count = ordered_indices.size();

    for (const auto& quad : mesh.quads) {
        if ((quad.surface_flags & ArchitecturalTransparent) == 0U) {
            continue;
        }
        const auto first = static_cast<std::size_t>(quad.first_index);
        if (first > mesh.indices.size() ||
            mesh.indices.size() - first < kQuadIndexCount) {
            continue;
        }
        ordered_indices.insert(
            ordered_indices.end(),
            mesh.indices.begin() + static_cast<std::ptrdiff_t>(first),
            mesh.indices.begin() +
                static_cast<std::ptrdiff_t>(first + kQuadIndexCount));
    }
    return opaque_index_count;
}

} // namespace valcraft

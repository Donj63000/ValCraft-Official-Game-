#pragma once

#include "creatures/CreatureGeometry.h"
#include "render/StylizedPrimitives.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace valcraft {

// Je distingue l'origine de l'atlas sans l'ajouter aux instances historiques :
// les rigs, les sockets et le format d'instance GPU restent ainsi inchanges.
enum class VisualEntityContext : std::uint8_t {
    Generic = 0,
    Creature,
    Crew,
    PlayerWorld,
    PlayerViewModel,
};

enum class VisualEntitySemanticRole : std::uint8_t {
    OrganicMass = 0,
    HeadOrJoint,
    Limb,
    HornOrClaw,
    ToolShaft,
    Blade,
    ClothPanel,
    FlexibleDetail,
    HardProp,
};

enum class VisualEntityLocalAxis : std::uint8_t {
    X = 0,
    Y,
    Z,
};

struct VisualEntityPrimitiveClassification {
    StylizedPrimitiveType primitive = StylizedPrimitiveType::RoundedBox;
    VisualEntitySemanticRole role = VisualEntitySemanticRole::HardProp;
    VisualEntityLocalAxis major_axis = VisualEntityLocalAxis::Y;
    VisualEntityLocalAxis minor_axis = VisualEntityLocalAxis::Z;
    glm::vec3 local_dimensions {1.0F};
    // Cette matrice ne remplace pas la transformation du rig. Elle oriente
    // uniquement le gabarit canonique a l'interieur de son volume historique.
    glm::mat4 primitive_to_part_local {1.0F};
    bool valid_transform = true;
};

struct VisualEntityPrimitiveInstance {
    // Je conserve une copie exacte de toutes les donnees produites par le rig.
    CreaturePartInstance source {};
    glm::mat4 primitive_to_part_local {1.0F};
    StylizedPrimitiveType primitive = StylizedPrimitiveType::RoundedBox;
    VisualEntitySemanticRole role = VisualEntitySemanticRole::HardProp;
    std::size_t source_index = 0U;

    [[nodiscard]] auto render_transform() const noexcept -> glm::mat4 {
        return source.transform * primitive_to_part_local;
    }
};

inline constexpr std::size_t kVisualEntityPrimitiveTypeCount = 6U;
inline constexpr std::size_t kVisualEntityPrimitiveLodCount = 3U;

// Je construis les dix-huit gabarits une seule fois. Le cache est immuable
// apres sa construction et peut donc etre partage entre tous les renderers.
class VisualEntityPrimitiveCache final {
public:
    VisualEntityPrimitiveCache();

    [[nodiscard]] auto mesh(
        StylizedPrimitiveType primitive,
        StylizedPrimitiveLod lod) const noexcept -> const StylizedPrimitiveMesh&;
    [[nodiscard]] auto fingerprint() const noexcept -> std::uint64_t;

private:
    std::array<StylizedPrimitiveMesh,
               kVisualEntityPrimitiveTypeCount * kVisualEntityPrimitiveLodCount>
        meshes_ {};
    std::uint64_t fingerprint_ = 0U;
};

[[nodiscard]] auto visual_entity_primitive_cache() noexcept
    -> const VisualEntityPrimitiveCache&;

// Le masque expose les tuiles reellement utilisees par les six faces. Il est
// volontairement limite a l'atlas 8 x 8 commun aux entites actuelles.
[[nodiscard]] auto visual_entity_atlas_tile_mask(
    const CreaturePartInstance& part) noexcept -> std::uint64_t;

[[nodiscard]] auto classify_visual_entity_part(
    const CreaturePartInstance& part,
    VisualEntityContext context) noexcept -> VisualEntityPrimitiveClassification;

[[nodiscard]] auto build_visual_entity_primitive_instances(
    std::span<const CreaturePartInstance> parts,
    VisualEntityContext context) -> std::vector<VisualEntityPrimitiveInstance>;

// Je centralise le LOD par distance et par taille projetée approximative afin
// que les micro-détails ne paient jamais la tessellation d'un torse.
[[nodiscard]] auto select_visual_entity_primitive_lod(
    float distance_squared,
    float maximum_dimension,
    int available_lod_count,
    bool simplified_shadow,
    bool viewmodel) noexcept -> StylizedPrimitiveLod;

[[nodiscard]] auto visual_entity_part_casts_simplified_shadow(
    float maximum_dimension) noexcept -> bool;

} // namespace valcraft

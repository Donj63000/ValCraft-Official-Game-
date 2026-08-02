#include "render/Renderer.h"
#include "app/GameBranding.h"
#include "creatures/CreatureGeometry.h"
#include "creatures/OldGuardGeometry.h"
#include "gameplay/SeaAdventure.h"
#include "gameplay/progression/AbilitySystem.h"
#include "render/BackroomsJackScreamer.h"
#include "render/BackroomsJackVisual.h"
#include "render/BackroomsMarlowVisual.h"
#include "render/BackroomsVisibility.h"
#include "render/HotbarLayout.h"
#include "render/ItemDropGeometry.h"
#include "render/ModelIconAtlas.h"
#include "render/ModernHudStyle.h"
#include "render/ModernTerrainShaderSource.h"
#include "render/ModernWaterShaderSource.h"
#include "render/MsdfFontAtlas.h"
#include "render/MusketHudLayout.h"
#include "render/OceanVisuals.h"
#include "render/SceneSamplerBindings.h"
#include "render/ShadowCascades.h"
#include "render/ShadowCulling.h"
#include "render/ShipMesh.h"
#include "render/ShipProtectionShaderSource.h"
#include "render/SkyShaderSource.h"
#include "render/StylizedPrimitives.h"
#include "render/StylizedShipMesh.h"
#include "render/VisualEntityPrimitives.h"
#include "world/BlockVisuals.h"
#include "world/OceanAdventureLayout.h"
#include "world/OceanSimulation.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace valcraft {

namespace {

constexpr auto kShadowDistance = 96.0F;
constexpr auto kInitialVertexBufferBytes =
    static_cast<GLsizeiptr>(sizeof(ChunkVertex) * 256U);
constexpr auto kInitialWaterVertexBufferBytes =
    static_cast<GLsizeiptr>(sizeof(WaterVertex) * 256U);
constexpr auto kInitialIndexBufferBytes =
    static_cast<GLsizeiptr>(sizeof(std::uint32_t) * 384U);
constexpr auto kInitialTerrainVertexBufferBytes =
    static_cast<GLsizeiptr>(sizeof(TerrainVertex) * 256U);
constexpr auto kInitialTerrainIndexBufferBytes =
    static_cast<GLsizeiptr>(sizeof(std::uint32_t) * 384U);
constexpr std::size_t kCreatureVerticesPerBox = 24U;
constexpr std::size_t kCreatureIndicesPerBox = 36U;
constexpr std::size_t kCreatureDayBoxBudget = 30U;
constexpr std::size_t kCreatureNightBoxBudget = 96U;
constexpr std::size_t kCreatureMaxBoxBudget =
    kCreatureDayBoxBudget > kCreatureNightBoxBudget ? kCreatureDayBoxBudget
                                                    : kCreatureNightBoxBudget;
constexpr std::size_t kCreatureMaxRenderedCount = 12U;
constexpr auto kInitialCreatureInstanceBufferBytes =
    static_cast<GLsizeiptr>(sizeof(CreaturePartInstance) *
                            (kCreatureMaxBoxBudget * kCreatureMaxRenderedCount +
                             kCrewVisualPartBudget * kCrewVisualRenderCapacity +
                             kOldGuardVisualPartBudget * kOldGuardMemberCount));
constexpr auto kInitialItemDropInstanceBufferBytes =
    static_cast<GLsizeiptr>(sizeof(ItemDropGpuInstance) * 512U);
constexpr auto kInitialPrecipitationInstanceBufferBytes =
    static_cast<GLsizeiptr>(sizeof(float) * 12U * (6000U + 96U));
constexpr auto kInitialOldGuardEffectInstanceBufferBytes =
    static_cast<GLsizeiptr>(
        sizeof(OldGuardMuzzleFlashInstance) * kOldGuardFlashCapacity +
        sizeof(OldGuardSmokeInstance) * kOldGuardSmokeCapacity);
constexpr auto kInitialHudBufferBytes =
    static_cast<GLsizeiptr>(sizeof(float) * 9U * 6U * 32U);
constexpr std::size_t kMaxGpuMeshEventsPerFrame = 8;
constexpr double kMaxGpuMeshSyncMsPerFrame = 1.0;
constexpr GLenum kTextureMaxAnisotropyExt = 0x84FE;
constexpr GLenum kMaxTextureMaxAnisotropyExt = 0x84FF;
constexpr float kLegendaryWorldDrawDistance = 160.0F;
constexpr float kLegendaryMinimumPartExtent = 1.0e-4F;
constexpr float kLegendaryPi = 3.14159265358979323846F;
constexpr std::uint8_t kAstralBossArchetypeValue = 6U;

[[nodiscard]] auto legendary_finite_vec3(const glm::vec3 &value) noexcept
    -> bool {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] auto legendary_finite_vec4(const glm::vec4 &value) noexcept
    -> bool {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] auto legendary_finite_matrix(const glm::mat4 &value) noexcept
    -> bool {
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      if (!std::isfinite(value[column][row])) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] auto legendary_finite_clamped(float value, float minimum,
                                            float maximum,
                                            float fallback = 0.0F) noexcept
    -> float {
  return std::isfinite(value) ? std::clamp(value, minimum, maximum)
                              : std::clamp(fallback, minimum, maximum);
}

[[nodiscard]] auto valid_issou_hud_kind(IssouHudElementKind kind) noexcept
    -> bool {
  switch (kind) {
  case IssouHudElementKind::BossHealth:
  case IssouHudElementKind::BossStagger:
  case IssouHudElementKind::WeaponStability:
  case IssouHudElementKind::Momentum:
  case IssouHudElementKind::Charge:
  case IssouHudElementKind::Countdown:
    return true;
  }
  return false;
}

[[nodiscard]] auto valid_issou_result_metric(IssouResultMetric metric) noexcept
    -> bool {
  switch (metric) {
  case IssouResultMetric::CombatSeconds:
  case IssouResultMetric::DamageDealt:
  case IssouResultMetric::LimbsSevered:
  case IssouResultMetric::PerfectGuards:
  case IssouResultMetric::MissedAttacks:
  case IssouResultMetric::MaximumMomentum:
  case IssouResultMetric::MaximumTargetsHit:
    return true;
  }
  return false;
}

[[nodiscard]] auto sanitize_issou_hud_element(
    IssouHudElement element) noexcept -> std::optional<IssouHudElement> {
  constexpr auto kMaximumCoordinate = 32'768.0F;
  if (!valid_issou_hud_kind(element.kind) ||
      !std::isfinite(element.rect.x) || !std::isfinite(element.rect.y) ||
      !std::isfinite(element.rect.width) ||
      !std::isfinite(element.rect.height) ||
      element.rect.width <= 0.0F || element.rect.height <= 0.0F ||
      element.rect.x < 0.0F || element.rect.y < 0.0F ||
      element.rect.width > kMaximumCoordinate ||
      element.rect.height > kMaximumCoordinate ||
      element.rect.x > kMaximumCoordinate ||
      element.rect.y > kMaximumCoordinate ||
      !legendary_finite_vec4(element.foreground) ||
      !legendary_finite_vec4(element.background) ||
      !std::isfinite(element.value) ||
      !std::isfinite(element.secondary_value)) {
    return std::nullopt;
  }

  element.foreground = glm::clamp(element.foreground, glm::vec4{0.0F},
                                  glm::vec4{1.0F});
  element.background = glm::clamp(element.background, glm::vec4{0.0F},
                                  glm::vec4{1.0F});
  element.value = std::clamp(element.value, 0.0F, 1.0F);
  switch (element.kind) {
  case IssouHudElementKind::Momentum:
    element.secondary_value =
        std::clamp(element.secondary_value, 0.0F, 3.0F);
    break;
  case IssouHudElementKind::Countdown:
    element.secondary_value =
        std::clamp(element.secondary_value, 0.0F, 3'600.0F);
    break;
  default:
    element.secondary_value =
        std::clamp(element.secondary_value, 0.0F, 1.0F);
    break;
  }
  return element;
}

[[nodiscard]] auto sanitize_issou_result_line(
    IssouResultLine line) noexcept -> std::optional<IssouResultLine> {
  if (!valid_issou_result_metric(line.metric) || !std::isfinite(line.value)) {
    return std::nullopt;
  }
  line.value = std::clamp(line.value, 0.0F, 1'000'000'000.0F);
  return line;
}

[[nodiscard]] auto legendary_creature_uvs(CreatureAtlasTile tile) noexcept
    -> std::array<BoxUvRect, 6U> {
  const auto coordinates = creature_atlas_tile_coordinates(tile);
  const auto step = 1.0F / kCreatureAtlasTilesPerAxis;
  const auto rectangle = BoxUvRect{
      static_cast<float>(coordinates[0]) * step,
      static_cast<float>(coordinates[1]) * step,
      (static_cast<float>(coordinates[0]) + 1.0F) * step,
      (static_cast<float>(coordinates[1]) + 1.0F) * step,
  };
  std::array<BoxUvRect, 6U> result{};
  result.fill(rectangle);
  return result;
}

[[nodiscard]] auto legendary_player_uvs(PlayerAtlasTile tile) noexcept
    -> std::array<BoxUvRect, 6U> {
  const auto coordinates = player_atlas_tile_coordinates(tile);
  const auto step = 1.0F / kPlayerAtlasTilesPerAxis;
  const auto rectangle = BoxUvRect{
      static_cast<float>(coordinates[0]) * step,
      static_cast<float>(coordinates[1]) * step,
      (static_cast<float>(coordinates[0]) + 1.0F) * step,
      (static_cast<float>(coordinates[1]) + 1.0F) * step,
  };
  std::array<BoxUvRect, 6U> result{};
  result.fill(rectangle);
  return result;
}

[[nodiscard]] auto
legendary_uvs_are_finite(const std::array<BoxUvRect, 6U> &face_uvs) noexcept
    -> bool {
  return std::all_of(face_uvs.begin(), face_uvs.end(),
                     [](const BoxUvRect &rectangle) noexcept {
                       return std::isfinite(rectangle.u0) &&
                              std::isfinite(rectangle.v0) &&
                              std::isfinite(rectangle.u1) &&
                              std::isfinite(rectangle.v1);
                     });
}

[[nodiscard]] auto sanitize_legendary_part(
    CreaturePartInstance part,
    CreatureAtlasTile fallback_tile = CreatureAtlasTile::ZombieFlesh) noexcept
    -> std::optional<CreaturePartInstance> {
  if (!legendary_finite_matrix(part.transform)) {
    return std::nullopt;
  }
  const auto extent_x = glm::length(glm::vec3{part.transform[0]});
  const auto extent_y = glm::length(glm::vec3{part.transform[1]});
  const auto extent_z = glm::length(glm::vec3{part.transform[2]});
  if (!std::isfinite(extent_x) || !std::isfinite(extent_y) ||
      !std::isfinite(extent_z) || extent_x <= kLegendaryMinimumPartExtent ||
      extent_y <= kLegendaryMinimumPartExtent ||
      extent_z <= kLegendaryMinimumPartExtent) {
    return std::nullopt;
  }
  if (!legendary_uvs_are_finite(part.face_uvs)) {
    part.face_uvs = legendary_creature_uvs(fallback_tile);
  }
  part.nightmare_factor =
      legendary_finite_clamped(part.nightmare_factor, 0.0F, 1.0F);
  part.tension = legendary_finite_clamped(part.tension, 0.0F, 1.0F);
  part.material_class =
      legendary_finite_clamped(part.material_class, 0.0F, 4.0F);
  part.cavity_mask = legendary_finite_clamped(part.cavity_mask, 0.0F, 1.0F);
  part.emissive_strength =
      legendary_finite_clamped(part.emissive_strength, 0.0F, 4.0F);
  part.sky_light = legendary_finite_clamped(part.sky_light, 0.0F, 1.0F, 1.0F);
  part.block_light = legendary_finite_clamped(part.block_light, 0.0F, 1.0F);
  part.precipitation_exposure =
      legendary_finite_clamped(part.precipitation_exposure, 0.0F, 1.0F, 1.0F);
  return part;
}

[[nodiscard]] auto make_legendary_box(
    const glm::mat4 &root, const glm::vec3 &center, const glm::vec3 &dimensions,
    CreatureAtlasTile tile, float nightmare_factor = 0.0F, float tension = 0.0F,
    float material_class = 0.0F, float cavity_mask = 0.0F,
    float emissive_strength = 0.0F) -> std::optional<CreaturePartInstance> {
  if (!legendary_finite_matrix(root) || !legendary_finite_vec3(center) ||
      !legendary_finite_vec3(dimensions) ||
      dimensions.x <= kLegendaryMinimumPartExtent ||
      dimensions.y <= kLegendaryMinimumPartExtent ||
      dimensions.z <= kLegendaryMinimumPartExtent) {
    return std::nullopt;
  }
  auto transform = glm::translate(root, center);
  transform = glm::scale(transform, dimensions);
  return sanitize_legendary_part(
      CreaturePartInstance{
          transform,
          legendary_creature_uvs(tile),
          nightmare_factor,
          tension,
          material_class,
          cavity_mask,
          emissive_strength,
      },
      tile);
}

[[nodiscard]] auto make_legendary_segment(
    const glm::vec3 &start, const glm::vec3 &end, float width,
    CreatureAtlasTile tile, float nightmare_factor, float tension,
    float material_class, float cavity_mask, float emissive_strength)
    -> std::optional<CreaturePartInstance> {
  if (!legendary_finite_vec3(start) || !legendary_finite_vec3(end) ||
      !std::isfinite(width) || width <= kLegendaryMinimumPartExtent) {
    return std::nullopt;
  }
  const auto delta = end - start;
  const auto length = glm::length(delta);
  if (!std::isfinite(length) || length <= kLegendaryMinimumPartExtent) {
    return std::nullopt;
  }
  const auto up = delta / length;
  const auto reference = std::abs(up.y) > 0.94F ? glm::vec3{1.0F, 0.0F, 0.0F}
                                                : glm::vec3{0.0F, 1.0F, 0.0F};
  const auto right = glm::normalize(glm::cross(reference, up));
  const auto forward = glm::normalize(glm::cross(up, right));
  if (!legendary_finite_vec3(right) || !legendary_finite_vec3(forward)) {
    return std::nullopt;
  }

  glm::mat4 transform{1.0F};
  transform[0] = glm::vec4{right * width, 0.0F};
  transform[1] = glm::vec4{up * length, 0.0F};
  transform[2] = glm::vec4{forward * width, 0.0F};
  transform[3] = glm::vec4{(start + end) * 0.5F, 1.0F};
  return sanitize_legendary_part(
      CreaturePartInstance{
          transform,
          legendary_creature_uvs(tile),
          nightmare_factor,
          tension,
          material_class,
          cavity_mask,
          emissive_strength,
      },
      tile);
}

[[nodiscard]] auto
leviathan_player_tile(LeviathanVisualMaterial material) noexcept
    -> PlayerAtlasTile {
  switch (material) {
  case LeviathanVisualMaterial::AncientBone:
    return PlayerAtlasTile::SwordBlade;
  case LeviathanVisualMaterial::DarkIron:
    return PlayerAtlasTile::SwordGuard;
  case LeviathanVisualMaterial::LeatherGrip:
    return PlayerAtlasTile::SwordGrip;
  case LeviathanVisualMaterial::CorruptedVein:
    return PlayerAtlasTile::Hurt;
  case LeviathanVisualMaterial::AstralRune:
    return PlayerAtlasTile::SwordEdge;
  case LeviathanVisualMaterial::SovereignCore:
    return PlayerAtlasTile::MusketBrass;
  }
  return PlayerAtlasTile::SwordBlade;
}

[[nodiscard]] auto
valid_leviathan_material(LeviathanVisualMaterial material) noexcept -> bool {
  switch (material) {
  case LeviathanVisualMaterial::AncientBone:
  case LeviathanVisualMaterial::DarkIron:
  case LeviathanVisualMaterial::LeatherGrip:
  case LeviathanVisualMaterial::CorruptedVein:
  case LeviathanVisualMaterial::AstralRune:
  case LeviathanVisualMaterial::SovereignCore:
    return true;
  }
  return false;
}

[[nodiscard]] auto
valid_leviathan_part_kind(LeviathanWeaponPartKind kind) noexcept -> bool {
  switch (kind) {
  case LeviathanWeaponPartKind::Pommel:
  case LeviathanWeaponPartKind::Grip:
  case LeviathanWeaponPartKind::LowerSpine:
  case LeviathanWeaponPartKind::UpperSpine:
  case LeviathanWeaponPartKind::Crown:
  case LeviathanWeaponPartKind::IronBand:
  case LeviathanWeaponPartKind::AwakeningInlay:
    return true;
  }
  return false;
}

[[nodiscard]] auto convert_leviathan_weapon_part(
    const LeviathanWeaponPartInstance &source) noexcept
    -> std::optional<CreaturePartInstance> {
  if (!legendary_finite_matrix(source.transform) ||
      !legendary_finite_vec4(source.color_tint) ||
      !valid_leviathan_material(source.material) ||
      !valid_leviathan_part_kind(source.kind)) {
    return std::nullopt;
  }
  const auto tile = leviathan_player_tile(source.material);
  auto nightmare_factor = 0.0F;
  if (source.material == LeviathanVisualMaterial::CorruptedVein) {
    nightmare_factor = 0.92F;
  } else if (source.material == LeviathanVisualMaterial::AstralRune) {
    nightmare_factor = 0.26F;
  }
  return sanitize_legendary_part(
      CreaturePartInstance{
          source.transform,
          legendary_player_uvs(tile),
          nightmare_factor,
          0.28F,
          source.material == LeviathanVisualMaterial::LeatherGrip ? 0.35F
                                                                  : 1.0F,
          legendary_finite_clamped(source.roughness, 0.0F, 1.0F, 0.75F),
          legendary_finite_clamped(source.emissive_strength, 0.0F, 4.0F),
      },
      CreatureAtlasTile::ZombieBone);
}

[[nodiscard]] auto valid_leviathan_visual_event_kind(
    LeviathanVisualEventKind kind) noexcept -> bool {
  switch (kind) {
  case LeviathanVisualEventKind::Trail:
  case LeviathanVisualEventKind::ImpactBurst:
  case LeviathanVisualEventKind::Shockwave:
  case LeviathanVisualEventKind::CameraImpulse:
  case LeviathanVisualEventKind::VisualHitStop:
    return true;
  }
  return false;
}

[[nodiscard]] auto sanitize_leviathan_visual_event(
    LeviathanVisualEvent event) noexcept
    -> std::optional<LeviathanVisualEvent> {
  constexpr auto kMaximumWorldCoordinate = 1'000'000.0F;
  if (!valid_leviathan_visual_event_kind(event.kind) || !event.visual_only ||
      !legendary_finite_vec3(event.position) ||
      !legendary_finite_vec3(event.direction) ||
      !legendary_finite_vec4(event.color) || !std::isfinite(event.radius) ||
      !std::isfinite(event.duration_seconds) ||
      !std::isfinite(event.intensity) || event.duration_seconds <= 0.0F ||
      std::abs(event.position.x) > kMaximumWorldCoordinate ||
      std::abs(event.position.y) > kMaximumWorldCoordinate ||
      std::abs(event.position.z) > kMaximumWorldCoordinate) {
    return std::nullopt;
  }

  auto direction_length = glm::length(event.direction);
  if (!std::isfinite(direction_length) ||
      direction_length <= kLegendaryMinimumPartExtent) {
    event.direction = {0.0F, 1.0F, 0.0F};
  } else {
    event.direction /= direction_length;
  }
  event.color =
      glm::clamp(event.color, glm::vec4{0.0F}, glm::vec4{1.0F});
  event.duration_seconds =
      std::clamp(event.duration_seconds, 0.001F, 2.0F);
  event.intensity = std::clamp(event.intensity, 0.0F, 2.0F);
  event.particle_count = std::min<std::uint16_t>(event.particle_count, 64U);

  switch (event.kind) {
  case LeviathanVisualEventKind::Trail:
    if (event.radius <= 0.0F) {
      return std::nullopt;
    }
    event.radius = std::clamp(event.radius, 0.05F, 8.0F);
    event.particle_count = 0U;
    break;
  case LeviathanVisualEventKind::ImpactBurst:
    if (event.radius <= 0.0F) {
      return std::nullopt;
    }
    event.radius = std::clamp(event.radius, 0.02F, 8.0F);
    break;
  case LeviathanVisualEventKind::Shockwave:
    if (event.radius <= 0.0F) {
      return std::nullopt;
    }
    event.radius = std::clamp(event.radius, 0.05F, 16.0F);
    break;
  case LeviathanVisualEventKind::CameraImpulse:
  case LeviathanVisualEventKind::VisualHitStop:
    event.radius = 0.0F;
    event.particle_count = 0U;
    break;
  }
  return event;
}

[[nodiscard]] auto leviathan_visual_event_tile(
    const LeviathanVisualEvent &event) noexcept -> CreatureAtlasTile {
  if (event.color.r > event.color.g * 1.25F &&
      event.color.r > event.color.b * 1.25F) {
    return CreatureAtlasTile::ZombieVein;
  }
  if (event.color.b > event.color.r * 1.12F) {
    return CreatureAtlasTile::CrewWater;
  }
  if (event.color.r > 0.72F && event.color.g > 0.52F &&
      event.color.b < 0.48F) {
    return CreatureAtlasTile::CrewGold;
  }
  return CreatureAtlasTile::TransformGlow;
}

[[nodiscard]] auto build_leviathan_visual_event_parts(
    const LeviathanVisualEvent &event) -> std::vector<CreaturePartInstance> {
  std::vector<CreaturePartInstance> result{};
  if (event.kind == LeviathanVisualEventKind::CameraImpulse ||
      event.kind == LeviathanVisualEventKind::VisualHitStop ||
      event.intensity <= 0.001F) {
    return result;
  }

  const auto direction = event.direction;
  const auto reference =
      std::abs(direction.y) > 0.92F ? glm::vec3{1.0F, 0.0F, 0.0F}
                                    : glm::vec3{0.0F, 1.0F, 0.0F};
  const auto tangent = glm::normalize(glm::cross(direction, reference));
  const auto bitangent = glm::normalize(glm::cross(direction, tangent));
  if (!legendary_finite_vec3(tangent) || !legendary_finite_vec3(bitangent)) {
    return result;
  }
  const auto tile = leviathan_visual_event_tile(event);
  const auto emissive = std::clamp(event.intensity * 1.85F, 0.05F, 4.0F);
  const auto append = [&](std::optional<CreaturePartInstance> part) {
    if (part.has_value()) {
      result.push_back(*part);
    }
  };

  if (event.kind == LeviathanVisualEventKind::Trail) {
    result.reserve(3U);
    const auto width = std::clamp(0.025F + event.intensity * 0.035F, 0.025F,
                                  0.11F);
    for (const auto lane : std::array<float, 3U>{-1.0F, 0.0F, 1.0F}) {
      const auto offset = tangent * lane * event.radius * 0.075F;
      append(make_legendary_segment(
          event.position - direction * event.radius * 0.46F + offset,
          event.position + direction * event.radius * 0.54F + offset, width,
          tile, 0.22F, event.intensity, 0.10F, 0.0F, emissive));
    }
    return result;
  }

  if (event.kind == LeviathanVisualEventKind::ImpactBurst) {
    const auto ray_count =
        std::min<std::size_t>(event.particle_count, 24U);
    result.reserve(ray_count + 1U);
    const auto root = glm::translate(glm::mat4{1.0F}, event.position);
    append(make_legendary_box(
        root, {0.0F, 0.0F, 0.0F},
        glm::vec3{std::max(0.06F, event.radius * 0.18F)}, tile, 0.28F,
        event.intensity, 0.12F, 0.0F, emissive));
    for (std::size_t index = 0U; index < ray_count; ++index) {
      const auto normalized =
          static_cast<float>(index) / static_cast<float>(ray_count);
      const auto angle = normalized * kLegendaryPi * 2.0F;
      const auto lift =
          (static_cast<float>(index % 5U) - 2.0F) * 0.16F;
      const auto ray_direction = glm::normalize(
          tangent * std::cos(angle) + bitangent * std::sin(angle) +
          direction * lift);
      const auto length =
          event.radius * (0.46F + static_cast<float>(index % 4U) * 0.13F);
      append(make_legendary_segment(
          event.position + direction * 0.025F,
          event.position + ray_direction * length,
          std::clamp(0.018F + event.intensity * 0.018F, 0.018F, 0.06F), tile,
          0.34F, event.intensity, 0.08F, 0.0F, emissive));
    }
    return result;
  }

  if (event.kind == LeviathanVisualEventKind::Shockwave) {
    const auto segment_count = std::clamp<std::size_t>(
        static_cast<std::size_t>(event.particle_count) / 2U, 12U, 32U);
    result.reserve(segment_count);
    for (std::size_t index = 0U; index < segment_count; ++index) {
      const auto first_angle =
          static_cast<float>(index) / static_cast<float>(segment_count) *
          kLegendaryPi * 2.0F;
      const auto second_angle =
          static_cast<float>(index + 1U) /
          static_cast<float>(segment_count) * kLegendaryPi * 2.0F;
      const auto first =
          event.position +
          (tangent * std::cos(first_angle) +
           bitangent * std::sin(first_angle)) *
              event.radius;
      const auto second =
          event.position +
          (tangent * std::cos(second_angle) +
           bitangent * std::sin(second_angle)) *
              event.radius;
      append(make_legendary_segment(
          first, second,
          std::clamp(0.035F + event.intensity * 0.035F, 0.035F, 0.12F), tile,
          0.24F, event.intensity, 0.08F, 0.0F, emissive));
    }
  }
  return result;
}

[[nodiscard]] auto
legendary_enemy_root_scale(LegendaryEnemyArchetype archetype) noexcept
    -> float {
  // Je réserve la valeur append-only suivante au boss astral afin que son
  // rendu soit prêt avant même que le symbole soit ajouté au module gameplay.
  if (static_cast<std::uint8_t>(archetype) ==
      kAstralBossArchetypeValue) {
    return 1.62F;
  }
  switch (archetype) {
  case LegendaryEnemyArchetype::CorruptedBrute:
    return 1.34F;
  case LegendaryEnemyArchetype::SwiftHunter:
    return 0.86F;
  case LegendaryEnemyArchetype::ArmoredGuard:
    return 1.05F;
  case LegendaryEnemyArchetype::AstralCreature:
    return 0.96F;
  case LegendaryEnemyArchetype::ForgeGuardian:
    return 1.18F;
  case LegendaryEnemyArchetype::ArenaMinion:
    return 0.92F;
  case LegendaryEnemyArchetype::AstralBoss:
    return 1.62F;
  }
  return 1.0F;
}

[[nodiscard]] auto
valid_legendary_enemy_archetype(LegendaryEnemyArchetype archetype) noexcept
    -> bool {
  if (static_cast<std::uint8_t>(archetype) ==
      kAstralBossArchetypeValue) {
    return true;
  }
  switch (archetype) {
  case LegendaryEnemyArchetype::CorruptedBrute:
  case LegendaryEnemyArchetype::SwiftHunter:
  case LegendaryEnemyArchetype::ArmoredGuard:
  case LegendaryEnemyArchetype::AstralCreature:
  case LegendaryEnemyArchetype::ForgeGuardian:
  case LegendaryEnemyArchetype::ArenaMinion:
  case LegendaryEnemyArchetype::AstralBoss:
    return true;
  }
  return false;
}

[[nodiscard]] auto
valid_legendary_enemy_behavior(LegendaryEnemyBehavior behavior) noexcept
    -> bool {
  switch (behavior) {
  case LegendaryEnemyBehavior::Idle:
  case LegendaryEnemyBehavior::Approach:
  case LegendaryEnemyBehavior::Strafe:
  case LegendaryEnemyBehavior::Telegraph:
  case LegendaryEnemyBehavior::Attack:
  case LegendaryEnemyBehavior::Recover:
  case LegendaryEnemyBehavior::Staggered:
  case LegendaryEnemyBehavior::Dead:
    return true;
  }
  return false;
}

[[nodiscard]] auto
legendary_enemy_primary_tile(LegendaryEnemyArchetype archetype) noexcept
    -> CreatureAtlasTile {
  if (static_cast<std::uint8_t>(archetype) ==
      kAstralBossArchetypeValue) {
    return CreatureAtlasTile::TransformGlow;
  }
  switch (archetype) {
  case LegendaryEnemyArchetype::CorruptedBrute:
    return CreatureAtlasTile::ZombieFlesh;
  case LegendaryEnemyArchetype::SwiftHunter:
    return CreatureAtlasTile::ZombieScar;
  case LegendaryEnemyArchetype::ArmoredGuard:
    return CreatureAtlasTile::CrewIron;
  case LegendaryEnemyArchetype::AstralCreature:
    return CreatureAtlasTile::TransformGlow;
  case LegendaryEnemyArchetype::ForgeGuardian:
    return CreatureAtlasTile::CrewIron;
  case LegendaryEnemyArchetype::ArenaMinion:
    return CreatureAtlasTile::CrewBurgundyCloth;
  case LegendaryEnemyArchetype::AstralBoss:
    return CreatureAtlasTile::TransformGlow;
  }
  return CreatureAtlasTile::ZombieFlesh;
}

[[nodiscard]] auto
build_legendary_enemy_render_parts(const LegendaryEnemyRenderSnapshot &enemy)
    -> std::vector<CreaturePartInstance> {
  std::vector<CreaturePartInstance> result{};
  if (!enemy.alive || !legendary_finite_vec3(enemy.position) ||
      !std::isfinite(enemy.facing_yaw_radians)) {
    return result;
  }

  const auto scale = legendary_enemy_root_scale(enemy.archetype);
  const auto health = legendary_finite_clamped(enemy.health_ratio, 0.0F, 1.0F);
  const auto armor = legendary_finite_clamped(enemy.armor_ratio, 0.0F, 1.0F);
  const auto stagger =
      legendary_finite_clamped(enemy.stagger_ratio, 0.0F, 1.0F);
  const auto astral_boss =
      static_cast<std::uint8_t>(enemy.archetype) ==
      kAstralBossArchetypeValue;
  const auto astral =
      enemy.archetype == LegendaryEnemyArchetype::AstralCreature ||
      astral_boss || enemy.astral_intangible;
  const auto nightmare =
      enemy.archetype == LegendaryEnemyArchetype::CorruptedBrute
          ? 0.88F
          : (astral_boss ? 0.52F : (astral ? 0.32F : 0.08F));
  const auto emissive = astral_boss ? 2.25F : (astral ? 1.35F : 0.0F);
  const auto tile = legendary_enemy_primary_tile(enemy.archetype);
  const auto attack_pose =
      enemy.behavior == LegendaryEnemyBehavior::Attack ||
              enemy.behavior == LegendaryEnemyBehavior::Telegraph
          ? 0.42F
          : 0.0F;
  const auto stagger_drop =
      enemy.behavior == LegendaryEnemyBehavior::Staggered ? 0.22F : 0.0F;

  auto root = glm::translate(
      glm::mat4{1.0F}, enemy.position - glm::vec3{0.0F, stagger_drop, 0.0F});
  root =
      glm::rotate(root, enemy.facing_yaw_radians, glm::vec3{0.0F, 1.0F, 0.0F});
  result.reserve(10U);
  const auto append_box =
      [&](const glm::vec3 &center, const glm::vec3 &dimensions,
          CreatureAtlasTile part_tile, float part_emissive = 0.0F) {
        const auto part = make_legendary_box(
            root, center * scale, dimensions * scale, part_tile, nightmare,
            stagger, armor > 0.0F ? 1.0F : 0.25F, 1.0F - health, part_emissive);
        if (part.has_value()) {
          result.push_back(*part);
        }
      };

  append_box({0.0F, 1.15F, 0.0F}, {0.72F, 0.92F, 0.42F}, tile, emissive);
  append_box({0.0F, 1.88F, -0.015F}, {0.48F, 0.48F, 0.46F},
             astral ? CreatureAtlasTile::TransformGlow
                    : CreatureAtlasTile::ZombieFlesh,
             emissive);
  append_box({-0.48F, 1.20F, -attack_pose * 0.20F}, {0.22F, 0.82F, 0.22F}, tile,
             emissive * 0.68F);
  append_box({0.48F, 1.20F, -attack_pose * 0.55F}, {0.22F, 0.82F, 0.22F}, tile,
             emissive * 0.68F);
  append_box({-0.22F, 0.43F, 0.0F}, {0.27F, 0.86F, 0.30F}, tile,
             emissive * 0.48F);
  append_box({0.22F, 0.43F, 0.0F}, {0.27F, 0.86F, 0.30F}, tile,
             emissive * 0.48F);
  if (armor > 0.02F ||
      enemy.archetype == LegendaryEnemyArchetype::ForgeGuardian) {
    append_box({0.0F, 1.28F, -0.25F}, {0.78F, 0.70F, 0.12F},
               CreatureAtlasTile::CrewIron,
               enemy.archetype == LegendaryEnemyArchetype::ForgeGuardian
                   ? 0.20F
                   : 0.0F);
  }
  if (enemy.behavior == LegendaryEnemyBehavior::Telegraph) {
    append_box({0.0F, 2.28F, 0.0F}, {0.18F, 0.18F, 0.18F},
               CreatureAtlasTile::TransformGlow, 1.8F);
  }
  if (astral_boss) {
    // Je lui donne une silhouette royale immédiatement identifiable : noyau,
    // épaulières et couronne restent tous liés à la palette astrale.
    append_box({0.0F, 1.20F, -0.29F}, {0.36F, 0.52F, 0.10F},
               CreatureAtlasTile::TransformGlow, emissive);
    append_box({-0.62F, 1.48F, 0.0F}, {0.34F, 0.24F, 0.52F},
               CreatureAtlasTile::CrewGold, emissive * 0.48F);
    append_box({0.62F, 1.48F, 0.0F}, {0.34F, 0.24F, 0.52F},
               CreatureAtlasTile::CrewGold, emissive * 0.48F);
    append_box({0.0F, 2.24F, 0.0F}, {0.78F, 0.10F, 0.18F},
               CreatureAtlasTile::TransformGlow, emissive);
  }
  return result;
}

[[nodiscard]] auto
build_sea_leviathan_render_parts(const SeaLeviathanRenderSnapshot &leviathan)
    -> std::vector<CreaturePartInstance> {
  std::vector<CreaturePartInstance> result{};
  if (!leviathan.active ||
      !legendary_finite_vec3(leviathan.body_anchor_world) ||
      !legendary_finite_vec3(leviathan.core_world)) {
    return result;
  }
  result.reserve(28U);
  const auto health =
      legendary_finite_clamped(leviathan.health_ratio, 0.0F, 1.0F);
  const auto carapace =
      legendary_finite_clamped(leviathan.carapace_ratio, 0.0F, 1.0F);
  const auto stagger =
      legendary_finite_clamped(leviathan.stagger_ratio, 0.0F, 1.0F);
  const auto exposure =
      legendary_finite_clamped(leviathan.core_exposure_ratio, 0.0F, 1.0F);
  auto body_root = glm::translate(glm::mat4{1.0F}, leviathan.body_anchor_world);

  const auto append = [&](std::optional<CreaturePartInstance> part) {
    if (part.has_value()) {
      result.push_back(*part);
    }
  };
  append(make_legendary_box(body_root, {0.0F, 0.0F, 0.0F}, {3.8F, 1.7F, 3.1F},
                            CreatureAtlasTile::TransformSinew, 0.74F, stagger,
                            0.36F, 1.0F - health, 0.0F));
  if (carapace > 0.01F) {
    append(make_legendary_box(body_root, {0.0F, 0.72F, 0.0F},
                              {3.45F, 0.55F, 2.75F},
                              CreatureAtlasTile::ZombieHorn, 0.42F, 0.18F, 1.0F,
                              1.0F - carapace, 0.0F));
  }
  const auto core_root = glm::translate(glm::mat4{1.0F}, leviathan.core_world);
  append(make_legendary_box(
      core_root, {0.0F, 0.0F, 0.0F}, glm::vec3{0.62F + exposure * 0.42F},
      CreatureAtlasTile::TransformGlow, 0.58F, 0.25F, 0.20F, 0.0F,
      leviathan.core_exposed ? 1.8F + exposure : 0.32F));

  for (std::size_t index = 0U; index < leviathan.tentacles.size(); ++index) {
    const auto &tentacle = leviathan.tentacles[index];
    if (!legendary_finite_vec3(tentacle.anchor_world)) {
      continue;
    }
    const auto side = index % 2U == 0U ? -1.0F : 1.0F;
    const auto longitudinal = index < 2U ? -0.55F : 0.75F;
    const auto start = leviathan.body_anchor_world + glm::vec3{
                                                         side * 1.32F,
                                                         0.10F,
                                                         longitudinal,
                                                     };
    auto previous = start;
    const auto segment_count = tentacle.severed ? 1U : 4U;
    for (std::size_t segment = 1U; segment <= segment_count; ++segment) {
      const auto normalized =
          static_cast<float>(segment) /
          static_cast<float>(tentacle.severed ? 4U : segment_count);
      auto point = glm::mix(start, tentacle.anchor_world, normalized);
      point.y += std::sin(normalized * kLegendaryPi) *
                 (tentacle.attacking ? 1.55F : 0.88F);
      const auto width =
          (0.62F - normalized * 0.28F) * (tentacle.severed ? 0.72F : 1.0F);
      append(make_legendary_segment(
          previous, point, std::max(width, 0.18F),
          tentacle.severed ? CreatureAtlasTile::ZombieScar
                           : CreatureAtlasTile::TransformSinew,
          0.82F, tentacle.attacking ? 0.92F : 0.35F, 0.24F,
          1.0F -
              legendary_finite_clamped(tentacle.resistance_ratio, 0.0F, 1.0F),
          tentacle.attacking ? 0.18F : 0.0F));
      previous = point;
    }
  }

  if (leviathan.active_attack != SeaLeviathanAttack::None &&
      legendary_finite_vec3(leviathan.telegraph_world)) {
    const auto telegraph_root =
        glm::translate(glm::mat4{1.0F}, leviathan.telegraph_world);
    append(make_legendary_box(
        telegraph_root, {0.0F, 0.035F, 0.0F}, {1.4F, 0.07F, 1.4F},
        CreatureAtlasTile::TransformGlow, 0.36F, 1.0F, 0.15F, 0.0F, 1.65F));
  }
  return result;
}

[[nodiscard]] auto issou_decor_tile(IssouArenaDecorKind kind) noexcept
    -> CreatureAtlasTile {
  switch (kind) {
  case IssouArenaDecorKind::Stand:
    return CreatureAtlasTile::CrewWood;
  case IssouArenaDecorKind::ChainLink:
    return CreatureAtlasTile::CrewIron;
  case IssouArenaDecorKind::Brazier:
    return CreatureAtlasTile::TransformGlow;
  case IssouArenaDecorKind::Banner:
    return CreatureAtlasTile::CrewBurgundyCloth;
  case IssouArenaDecorKind::Gate:
    return CreatureAtlasTile::CrewIron;
  }
  return CreatureAtlasTile::CrewWood;
}

[[nodiscard]] auto valid_issou_decor_kind(IssouArenaDecorKind kind) noexcept
    -> bool {
  switch (kind) {
  case IssouArenaDecorKind::Stand:
  case IssouArenaDecorKind::ChainLink:
  case IssouArenaDecorKind::Brazier:
  case IssouArenaDecorKind::Banner:
  case IssouArenaDecorKind::Gate:
    return true;
  }
  return false;
}

[[nodiscard]] auto valid_issou_crowd_lod(IssouCrowdLod lod) noexcept -> bool {
  switch (lod) {
  case IssouCrowdLod::Full:
  case IssouCrowdLod::Simplified:
  case IssouCrowdLod::Impostor:
  case IssouCrowdLod::Culled:
    return true;
  }
  return false;
}

[[nodiscard]] auto
valid_sea_leviathan_attack(SeaLeviathanAttack attack) noexcept -> bool {
  switch (attack) {
  case SeaLeviathanAttack::None:
  case SeaLeviathanAttack::TentacleSweep:
  case SeaLeviathanAttack::DeckSmash:
    return true;
  }
  return false;
}

[[nodiscard]] auto
convert_issou_decor(const IssouArenaDecorInstance &decor) noexcept
    -> std::optional<CreaturePartInstance> {
  if (!legendary_finite_matrix(decor.transform) ||
      !legendary_finite_vec4(decor.color) ||
      !valid_issou_decor_kind(decor.kind)) {
    return std::nullopt;
  }
  const auto tile = issou_decor_tile(decor.kind);
  return sanitize_legendary_part(
      CreaturePartInstance{
          decor.transform,
          legendary_creature_uvs(tile),
          0.0F,
          decor.kind == IssouArenaDecorKind::ChainLink ? 0.48F : 0.12F,
          decor.kind == IssouArenaDecorKind::Banner ? 0.24F : 1.0F,
          0.24F,
          legendary_finite_clamped(decor.emissive_strength, 0.0F, 4.0F),
      },
      tile);
}

[[nodiscard]] auto
convert_colossus_blood_trace(const ColossusBloodTrace &trace) noexcept
    -> std::optional<CreaturePartInstance> {
  if (!legendary_finite_vec3(trace.position) ||
      !legendary_finite_vec3(trace.normal) || !std::isfinite(trace.radius) ||
      !std::isfinite(trace.opacity) || trace.radius <= 0.0F ||
      trace.opacity <= 0.0F) {
    return std::nullopt;
  }
  auto normal = trace.normal;
  const auto normal_length = glm::length(normal);
  if (!std::isfinite(normal_length) ||
      normal_length <= kLegendaryMinimumPartExtent) {
    normal = {0.0F, 1.0F, 0.0F};
  } else {
    normal /= normal_length;
  }
  return make_legendary_segment(
      trace.position, trace.position + normal * 0.022F,
      legendary_finite_clamped(trace.radius * 2.0F, 0.025F, 1.4F, 0.08F),
      trace.muted ? CreatureAtlasTile::ZombieScar
                  : CreatureAtlasTile::ZombieVein,
      trace.muted ? 0.20F : 0.90F,
      legendary_finite_clamped(trace.opacity, 0.0F, 1.0F), 0.10F, 0.0F,
      trace.muted ? 0.0F : 0.08F);
}

void add_legendary_dropped_submissions(
    RendererLegendaryPresentationStats &stats, std::size_t count) noexcept {
  const auto remaining =
      std::numeric_limits<std::size_t>::max() - stats.dropped_submissions;
  stats.dropped_submissions += std::min(count, remaining);
}

[[nodiscard]] auto
exterior_lantern_radiance(std::span<const ShipExteriorLight> lights,
                          float scale) noexcept -> glm::vec3 {

  constexpr auto fallback_color = glm::vec3{
      1.00F,
      0.62F,
      0.30F,
  };
  auto color = lights.empty() ? fallback_color : lights.front().color;
  for (auto axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(color[axis])) {
      color[axis] = fallback_color[axis];
    }
  }
  // Je tire la radiance des mêmes données que les fanaux : leur couleur
  // visuelle, le pont et les uniformes des soldats ne peuvent plus diverger.
  return glm::clamp(color, glm::vec3{0.0F}, glm::vec3{1.0F}) *
         std::clamp(scale, 0.0F, 4.0F);
}

[[nodiscard]] constexpr auto modern_ship_material_layers() noexcept
    -> std::array<float, 18>;

// Je garde les métriques CPU près des générateurs de géométrie HUD : le
// renderer OpenGL reste l'unique propriétaire de la texture correspondante.
std::optional<MsdfFontAtlas> g_modern_hud_font_atlas{};
bool g_modern_hud_font_enabled = false;

[[nodiscard]] auto active_modern_hud_font() noexcept -> const MsdfFontAtlas * {
  return g_modern_hud_font_enabled && g_modern_hud_font_atlas.has_value()
             ? &*g_modern_hud_font_atlas
             : nullptr;
}

[[nodiscard]] constexpr auto
visual_entity_primitive_slot(StylizedPrimitiveType primitive) noexcept
    -> std::size_t {
  switch (primitive) {
  case StylizedPrimitiveType::RoundedBox:
    return 0U;
  case StylizedPrimitiveType::Capsule:
    return 1U;
  case StylizedPrimitiveType::Ellipsoid:
    return 2U;
  case StylizedPrimitiveType::TaperedCylinder:
    return 3U;
  case StylizedPrimitiveType::Panel:
    return 4U;
  case StylizedPrimitiveType::Ribbon:
    return 5U;
  }
  return 0U;
}

[[nodiscard]] constexpr auto
visual_entity_lod_slot(StylizedPrimitiveLod lod) noexcept -> std::size_t {
  switch (lod) {
  case StylizedPrimitiveLod::Low:
    return 0U;
  case StylizedPrimitiveLod::Medium:
    return 1U;
  case StylizedPrimitiveLod::High:
    return 2U;
  }
  return 1U;
}

[[nodiscard]] constexpr auto
visual_entity_batch_slot(StylizedPrimitiveType primitive,
                         StylizedPrimitiveLod lod) noexcept -> std::size_t {
  return visual_entity_lod_slot(lod) * kVisualEntityPrimitiveTypeCount +
         visual_entity_primitive_slot(primitive);
}

[[nodiscard]] constexpr auto
visual_entity_primitive_for_slot(std::size_t slot) noexcept
    -> StylizedPrimitiveType {
  switch (slot % kVisualEntityPrimitiveTypeCount) {
  case 1U:
    return StylizedPrimitiveType::Capsule;
  case 2U:
    return StylizedPrimitiveType::Ellipsoid;
  case 3U:
    return StylizedPrimitiveType::TaperedCylinder;
  case 4U:
    return StylizedPrimitiveType::Panel;
  case 5U:
    return StylizedPrimitiveType::Ribbon;
  case 0U:
  default:
    return StylizedPrimitiveType::RoundedBox;
  }
}

[[nodiscard]] constexpr auto
visual_entity_lod_for_slot(std::size_t slot) noexcept -> StylizedPrimitiveLod {
  switch (slot / kVisualEntityPrimitiveTypeCount) {
  case 0U:
    return StylizedPrimitiveLod::Low;
  case 2U:
    return StylizedPrimitiveLod::High;
  case 1U:
  default:
    return StylizedPrimitiveLod::Medium;
  }
}

[[nodiscard]] auto supports_gl_extension(std::string_view requested) noexcept
    -> bool {
  GLint extension_count = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &extension_count);
  for (GLint index = 0; index < extension_count; ++index) {
    const auto *extension = reinterpret_cast<const char *>(
        glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(index)));
    if (extension != nullptr && requested == extension) {
      return true;
    }
  }
  return false;
}

struct BoxTemplateVertex {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  float nx = 0.0F;
  float ny = 1.0F;
  float nz = 0.0F;
  float u = 0.0F;
  float v = 0.0F;
  float face_index = 0.0F;
};

auto box_template_vertices()
    -> const std::array<BoxTemplateVertex, kCreatureVerticesPerBox> & {
  static const std::array<BoxTemplateVertex, kCreatureVerticesPerBox> kVertices{
      {
          {0.5F, -0.5F, -0.5F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F},
          {0.5F, 0.5F, -0.5F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F},
          {0.5F, 0.5F, 0.5F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F},
          {0.5F, -0.5F, 0.5F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F},

          {-0.5F, -0.5F, 0.5F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F},
          {-0.5F, 0.5F, 0.5F, -1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F},
          {-0.5F, 0.5F, -0.5F, -1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F},
          {-0.5F, -0.5F, -0.5F, -1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F},

          {-0.5F, 0.5F, 0.5F, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 2.0F},
          {0.5F, 0.5F, 0.5F, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 2.0F},
          {0.5F, 0.5F, -0.5F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 2.0F},
          {-0.5F, 0.5F, -0.5F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 2.0F},

          {-0.5F, -0.5F, -0.5F, 0.0F, -1.0F, 0.0F, 1.0F, 0.0F, 3.0F},
          {0.5F, -0.5F, -0.5F, 0.0F, -1.0F, 0.0F, 1.0F, 1.0F, 3.0F},
          {0.5F, -0.5F, 0.5F, 0.0F, -1.0F, 0.0F, 0.0F, 1.0F, 3.0F},
          {-0.5F, -0.5F, 0.5F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F, 3.0F},

          {0.5F, -0.5F, 0.5F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 4.0F},
          {0.5F, 0.5F, 0.5F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 4.0F},
          {-0.5F, 0.5F, 0.5F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 4.0F},
          {-0.5F, -0.5F, 0.5F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 4.0F},

          {-0.5F, -0.5F, -0.5F, 0.0F, 0.0F, -1.0F, 1.0F, 0.0F, 5.0F},
          {-0.5F, 0.5F, -0.5F, 0.0F, 0.0F, -1.0F, 1.0F, 1.0F, 5.0F},
          {0.5F, 0.5F, -0.5F, 0.0F, 0.0F, -1.0F, 0.0F, 1.0F, 5.0F},
          {0.5F, -0.5F, -0.5F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 5.0F},
      }};
  return kVertices;
}

auto box_template_indices()
    -> const std::array<std::uint32_t, kCreatureIndicesPerBox> & {
  static const std::array<std::uint32_t, kCreatureIndicesPerBox> kIndices{{
      0U,  1U,  2U,  0U,  2U,  3U,  4U,  5U,  6U,  4U,  6U,  7U,
      8U,  9U,  10U, 8U,  10U, 11U, 12U, 13U, 14U, 12U, 14U, 15U,
      16U, 17U, 18U, 16U, 18U, 19U, 20U, 21U, 22U, 20U, 22U, 23U,
  }};
  return kIndices;
}

auto grow_buffer_capacity(GLsizeiptr current_bytes, GLsizeiptr required_bytes,
                          GLsizeiptr minimum_bytes) -> GLsizeiptr {
  auto capacity = std::max(current_bytes, minimum_bytes);
  while (capacity < required_bytes) {
    capacity = std::max(capacity * 2, required_bytes);
  }
  return capacity;
}

struct ColorTargetFormat {
  GLint internal_format = GL_RGBA16F;
  GLenum pixel_format = GL_RGBA;
  GLenum pixel_type = GL_FLOAT;
};

auto color_target_format(
    const RendererQualitySettings &quality_settings) noexcept
    -> ColorTargetFormat {
  if (quality_settings.high_precision_hdr) {
    return {};
  }
  return {GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV};
}

auto finite_vec3(const glm::vec3 &value) noexcept -> bool {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

auto finite_matrix(const glm::mat4 &value) noexcept -> bool {
  for (glm::length_t column = 0; column < 4; ++column) {
    for (glm::length_t row = 0; row < 4; ++row) {
      if (!std::isfinite(value[column][row])) {
        return false;
      }
    }
  }
  return true;
}

auto finite_saturate(float value) noexcept -> float {
  return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
}

auto safe_direction(const glm::vec3 &direction,
                    const glm::vec3 &fallback) noexcept -> glm::vec3 {
  if (!finite_vec3(direction)) {
    return fallback;
  }
  const auto length_squared = glm::dot(direction, direction);
  if (!std::isfinite(length_squared) || length_squared <= 1.0e-6F) {
    return fallback;
  }
  return direction / std::sqrt(length_squared);
}

auto sanitize_weather_for_rendering(const EnvironmentState &source) noexcept
    -> EnvironmentState {
  auto state = source;
  state.cloud_intensity = finite_saturate(source.cloud_intensity);
  state.overcast_intensity = finite_saturate(source.overcast_intensity);
  state.precipitation_intensity =
      finite_saturate(source.precipitation_intensity);
  state.storm_intensity = finite_saturate(source.storm_intensity);
  state.violent_storm_intensity =
      finite_saturate(source.violent_storm_intensity);
  state.lightning_intensity = finite_saturate(source.lightning_intensity);
  state.lightning_bolt_intensity =
      finite_saturate(source.lightning_bolt_intensity);
  state.lightning_shape_seed = finite_saturate(source.lightning_shape_seed);
  state.weather_transition_factor =
      finite_saturate(source.weather_transition_factor);
  state.cloud_shadow_strength = finite_saturate(source.cloud_shadow_strength);
  state.wind_strength = finite_saturate(source.wind_strength);
  state.weather_time_seconds = std::isfinite(source.weather_time_seconds)
                                   ? std::max(source.weather_time_seconds, 0.0F)
                                   : 0.0F;
  state.lightning_direction =
      safe_direction(source.lightning_direction, {0.0F, 0.35F, 0.93675F});
  state.sun_direction =
      safe_direction(source.sun_direction, {0.0F, 1.0F, 0.0F});

  const auto wind_length_squared =
      glm::dot(source.wind_direction_xz, source.wind_direction_xz);
  state.wind_direction_xz =
      std::isfinite(source.wind_direction_xz.x) &&
              std::isfinite(source.wind_direction_xz.y) &&
              std::isfinite(wind_length_squared) &&
              wind_length_squared > 1.0e-6F
          ? source.wind_direction_xz / std::sqrt(wind_length_squared)
          : glm::vec2{0.0F, 1.0F};
  return state;
}

struct ScopedPrecipitationGlState {
  GLboolean depth_test_enabled = GL_FALSE;
  GLboolean cull_face_enabled = GL_FALSE;
  GLboolean blend_enabled = GL_FALSE;
  GLboolean depth_write_enabled = GL_TRUE;
  GLint blend_source_rgb = GL_ONE;
  GLint blend_destination_rgb = GL_ZERO;
  GLint blend_source_alpha = GL_ONE;
  GLint blend_destination_alpha = GL_ZERO;
  GLint current_program = 0;
  GLint vertex_array = 0;
  GLint array_buffer = 0;

  ScopedPrecipitationGlState() noexcept {
    depth_test_enabled = glIsEnabled(GL_DEPTH_TEST);
    cull_face_enabled = glIsEnabled(GL_CULL_FACE);
    blend_enabled = glIsEnabled(GL_BLEND);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write_enabled);
    glGetIntegerv(GL_BLEND_SRC_RGB, &blend_source_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blend_destination_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_source_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_destination_alpha);
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertex_array);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
  }

  ScopedPrecipitationGlState(const ScopedPrecipitationGlState &) = delete;
  auto operator=(const ScopedPrecipitationGlState &)
      -> ScopedPrecipitationGlState & = delete;

  ~ScopedPrecipitationGlState() noexcept {
    // Je restitue chaque etat que la passe modifie afin que les objets
    // suivants ne dependent jamais d'une hypothese sur l'etat precedent.
    set_capability(GL_DEPTH_TEST, depth_test_enabled);
    set_capability(GL_CULL_FACE, cull_face_enabled);
    set_capability(GL_BLEND, blend_enabled);
    glDepthMask(depth_write_enabled);
    glBlendFuncSeparate(static_cast<GLenum>(blend_source_rgb),
                        static_cast<GLenum>(blend_destination_rgb),
                        static_cast<GLenum>(blend_source_alpha),
                        static_cast<GLenum>(blend_destination_alpha));
    glUseProgram(static_cast<GLuint>(current_program));
    glBindVertexArray(static_cast<GLuint>(vertex_array));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(array_buffer));
  }

private:
  static void set_capability(GLenum capability, GLboolean enabled) noexcept {
    if (enabled == GL_TRUE) {
      glEnable(capability);
    } else {
      glDisable(capability);
    }
  }
};

auto ship_protection_is_renderable(const ShipRenderState &ship) noexcept
    -> bool {
  if (!ship.visible || ship.blueprint == nullptr ||
      !finite_matrix(ship.model_matrix) ||
      !finite_vec3(ship.world_bounds.min) ||
      !finite_vec3(ship.world_bounds.max)) {
    return false;
  }
  const auto &profile = ship.blueprint->protection_profile;
  return profile.maximum_half_width > 0.0F && profile.stern_z < profile.bow_z &&
         profile.lower_hull_min_y < profile.main_deck_top_y;
}

auto ray_aabb_entry_distance(const glm::vec3 &origin,
                             const glm::vec3 &direction,
                             const glm::vec3 &min_corner,
                             const glm::vec3 &max_corner,
                             float max_distance) noexcept
    -> std::optional<float> {
  auto entry = 0.0F;
  auto exit = max_distance;
  for (glm::length_t axis = 0; axis < 3; ++axis) {
    if (std::abs(direction[axis]) <= 1.0e-6F) {
      if (origin[axis] < min_corner[axis] || origin[axis] > max_corner[axis]) {
        return std::nullopt;
      }
      continue;
    }

    const auto inverse_direction = 1.0F / direction[axis];
    auto first = (min_corner[axis] - origin[axis]) * inverse_direction;
    auto second = (max_corner[axis] - origin[axis]) * inverse_direction;
    if (first > second) {
      std::swap(first, second);
    }
    entry = std::max(entry, first);
    exit = std::min(exit, second);
    if (entry > exit) {
      return std::nullopt;
    }
  }
  return entry >= 0.0F && entry <= max_distance ? std::optional<float>{entry}
                                                : std::nullopt;
}

void orphan_bound_buffer(GLenum target, GLsizeiptr capacity,
                         GLenum usage = GL_STREAM_DRAW) {
  if (capacity > 0) {
    // Je donne un nouveau stockage au pilote avant chaque écriture dynamique
    // pour éviter d'attendre le GPU.
    glBufferData(target, capacity, nullptr, usage);
  }
}

void merge_chunk_mesh_sections_into(
    const std::array<ChunkMeshData, kChunkSectionCount> &sections,
    ChunkMeshData &merged) {
  merged.vertices.clear();
  merged.indices.clear();
  merged.water_vertices.clear();
  merged.water_indices.clear();
  merged.face_count = 0U;
  merged.water_face_count = 0U;

  std::size_t vertex_count = 0U;
  std::size_t index_count = 0U;
  std::size_t water_vertex_count = 0U;
  std::size_t water_index_count = 0U;
  for (const auto &section : sections) {
    vertex_count += section.vertices.size();
    index_count += section.indices.size();
    water_vertex_count += section.water_vertices.size();
    water_index_count += section.water_indices.size();
  }
  merged.vertices.reserve(vertex_count);
  merged.indices.reserve(index_count);
  merged.water_vertices.reserve(water_vertex_count);
  merged.water_indices.reserve(water_index_count);

  for (const auto &section : sections) {
    const auto vertex_offset =
        static_cast<std::uint32_t>(merged.vertices.size());
    merged.vertices.insert(merged.vertices.end(), section.vertices.begin(),
                           section.vertices.end());
    for (const auto index : section.indices) {
      merged.indices.push_back(index + vertex_offset);
    }

    const auto water_vertex_offset =
        static_cast<std::uint32_t>(merged.water_vertices.size());
    merged.water_vertices.insert(merged.water_vertices.end(),
                                 section.water_vertices.begin(),
                                 section.water_vertices.end());
    for (const auto index : section.water_indices) {
      merged.water_indices.push_back(index + water_vertex_offset);
    }
    merged.face_count += section.face_count;
    merged.water_face_count += section.water_face_count;
  }
}

void merge_organic_terrain_sections_into(
    const std::array<OrganicTerrainMesh, kChunkSectionCount> &sections,
    OrganicTerrainMesh &merged) {
  merged.vertices.clear();
  merged.indices.clear();
  merged.quad_count = 0U;

  std::size_t vertex_count = 0U;
  std::size_t index_count = 0U;
  for (const auto &section : sections) {
    vertex_count += section.vertices.size();
    index_count += section.indices.size();
  }
  merged.vertices.reserve(vertex_count);
  merged.indices.reserve(index_count);

  for (const auto &section : sections) {
    const auto vertex_offset =
        static_cast<std::uint32_t>(merged.vertices.size());
    merged.vertices.insert(merged.vertices.end(), section.vertices.begin(),
                           section.vertices.end());
    for (const auto index : section.indices) {
      merged.indices.push_back(index + vertex_offset);
    }
    merged.quad_count += section.quad_count;
  }
}

void merge_architectural_sections_into(
    const std::array<ArchitecturalMesh, kChunkSectionCount> &sections,
    ArchitecturalMesh &merged) {

  merged = {};
  std::size_t vertex_count = 0U;
  std::size_t index_count = 0U;
  std::size_t quad_count = 0U;
  std::size_t fixture_count = 0U;
  for (const auto &section : sections) {
    vertex_count += section.vertices.size();
    index_count += section.indices.size();
    quad_count += section.quads.size();
    fixture_count += section.fixtures.size();
  }
  merged.vertices.reserve(vertex_count);
  merged.indices.reserve(index_count);
  merged.quads.reserve(quad_count);
  merged.fixtures.reserve(fixture_count);

  for (const auto &section : sections) {
    const auto vertex_offset =
        static_cast<std::uint32_t>(merged.vertices.size());
    const auto index_offset = static_cast<std::uint32_t>(merged.indices.size());
    merged.vertices.insert(merged.vertices.end(), section.vertices.begin(),
                           section.vertices.end());
    for (const auto index : section.indices) {
      merged.indices.push_back(index + vertex_offset);
    }
    for (auto quad : section.quads) {
      quad.first_vertex += vertex_offset;
      quad.first_index += index_offset;
      merged.quads.push_back(quad);
    }
    merged.fixtures.insert(merged.fixtures.end(), section.fixtures.begin(),
                           section.fixtures.end());

    if (!section.bounds.valid) {
      continue;
    }
    if (!merged.bounds.valid) {
      merged.bounds = section.bounds;
      continue;
    }
    merged.bounds.min_x = std::min(merged.bounds.min_x, section.bounds.min_x);
    merged.bounds.min_y = std::min(merged.bounds.min_y, section.bounds.min_y);
    merged.bounds.min_z = std::min(merged.bounds.min_z, section.bounds.min_z);
    merged.bounds.max_x = std::max(merged.bounds.max_x, section.bounds.max_x);
    merged.bounds.max_y = std::max(merged.bounds.max_y, section.bounds.max_y);
    merged.bounds.max_z = std::max(merged.bounds.max_z, section.bounds.max_z);
  }
}

[[nodiscard]] constexpr auto
color_target_bytes_per_pixel(GLint internal_format) noexcept -> std::uint64_t {
  return internal_format == GL_RGBA16F ? 8U : 4U;
}

void configure_box_template_attributes(GLuint vao, GLuint vbo, GLuint ebo) {
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(
      0, 3, GL_FLOAT, GL_FALSE, sizeof(BoxTemplateVertex),
      reinterpret_cast<void *>(offsetof(BoxTemplateVertex, x)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(
      1, 3, GL_FLOAT, GL_FALSE, sizeof(BoxTemplateVertex),
      reinterpret_cast<void *>(offsetof(BoxTemplateVertex, nx)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(
      2, 2, GL_FLOAT, GL_FALSE, sizeof(BoxTemplateVertex),
      reinterpret_cast<void *>(offsetof(BoxTemplateVertex, u)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(
      3, 1, GL_FLOAT, GL_FALSE, sizeof(BoxTemplateVertex),
      reinterpret_cast<void *>(offsetof(BoxTemplateVertex, face_index)));
}

void configure_creature_instance_attributes(GLuint vao, GLuint instance_vbo) {
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, instance_vbo);

  constexpr GLuint kTransformLocation = 4;
  for (GLuint column = 0; column < 4; ++column) {
    const auto location = kTransformLocation + column;
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(
        location, 4, GL_FLOAT, GL_FALSE, sizeof(CreaturePartInstance),
        reinterpret_cast<void *>(offsetof(CreaturePartInstance, transform) +
                                 sizeof(glm::vec4) * column));
    glVertexAttribDivisor(location, 1);
  }

  constexpr GLuint kUvLocation = 8;
  for (GLuint face_index = 0; face_index < 6; ++face_index) {
    const auto location = kUvLocation + face_index;
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(
        location, 4, GL_FLOAT, GL_FALSE, sizeof(CreaturePartInstance),
        reinterpret_cast<void *>(offsetof(CreaturePartInstance, face_uvs) +
                                 sizeof(BoxUvRect) * face_index));
    glVertexAttribDivisor(location, 1);
  }

  glEnableVertexAttribArray(14);
  glVertexAttribPointer(14, 4, GL_FLOAT, GL_FALSE, sizeof(CreaturePartInstance),
                        reinterpret_cast<void *>(
                            offsetof(CreaturePartInstance, nightmare_factor)));
  glVertexAttribDivisor(14, 1);

  glEnableVertexAttribArray(15);
  glVertexAttribPointer(15, 4, GL_FLOAT, GL_FALSE, sizeof(CreaturePartInstance),
                        reinterpret_cast<void *>(
                            offsetof(CreaturePartInstance, emissive_strength)));
  glVertexAttribDivisor(15, 1);
}

void configure_item_drop_instance_attributes(GLuint vao, GLuint instance_vbo) {
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, instance_vbo);

  constexpr GLuint kTransformLocation = 4;
  for (GLuint column = 0; column < 4; ++column) {
    const auto location = kTransformLocation + column;
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(
        location, 4, GL_FLOAT, GL_FALSE, sizeof(ItemDropGpuInstance),
        reinterpret_cast<void *>(offsetof(ItemDropGpuInstance, transform) +
                                 sizeof(glm::vec4) * column));
    glVertexAttribDivisor(location, 1);
  }

  glEnableVertexAttribArray(8);
  glVertexAttribIPointer(
      8, 1, GL_UNSIGNED_BYTE, sizeof(ItemDropGpuInstance),
      reinterpret_cast<void *>(offsetof(ItemDropGpuInstance, block_id)));
  glVertexAttribDivisor(8, 1);

  glEnableVertexAttribArray(9);
  glVertexAttribPointer(
      9, 1, GL_FLOAT, GL_FALSE, sizeof(ItemDropGpuInstance),
      reinterpret_cast<void *>(offsetof(ItemDropGpuInstance, sky_light)));
  glVertexAttribDivisor(9, 1);

  glEnableVertexAttribArray(10);
  glVertexAttribPointer(
      10, 1, GL_FLOAT, GL_FALSE, sizeof(ItemDropGpuInstance),
      reinterpret_cast<void *>(offsetof(ItemDropGpuInstance, block_light)));
  glVertexAttribDivisor(10, 1);

  glEnableVertexAttribArray(11);
  glVertexAttribPointer(
      11, 1, GL_FLOAT, GL_FALSE, sizeof(ItemDropGpuInstance),
      reinterpret_cast<void *>(offsetof(ItemDropGpuInstance, material_class)));
  glVertexAttribDivisor(11, 1);

  glEnableVertexAttribArray(12);
  glVertexAttribPointer(
      12, 4, GL_FLOAT, GL_FALSE, sizeof(ItemDropGpuInstance),
      reinterpret_cast<void *>(offsetof(ItemDropGpuInstance, face_tiles_0_1)));
  glVertexAttribDivisor(12, 1);

  glEnableVertexAttribArray(13);
  glVertexAttribPointer(
      13, 4, GL_FLOAT, GL_FALSE, sizeof(ItemDropGpuInstance),
      reinterpret_cast<void *>(offsetof(ItemDropGpuInstance, face_tiles_2_3)));
  glVertexAttribDivisor(13, 1);

  glEnableVertexAttribArray(14);
  glVertexAttribPointer(
      14, 4, GL_FLOAT, GL_FALSE, sizeof(ItemDropGpuInstance),
      reinterpret_cast<void *>(offsetof(ItemDropGpuInstance, face_tiles_4_5)));
  glVertexAttribDivisor(14, 1);
}

auto quantize_hud_value(float value, float steps_per_unit) -> int {
  return static_cast<int>(std::lround(value * steps_per_unit));
}

auto format_save_slot_timestamp(std::uint64_t unix_seconds) -> std::string {
  if (unix_seconds == 0) {
    return "AUCUNE SAUVEGARDE";
  }

  const auto time_value = static_cast<std::time_t>(unix_seconds);
  std::tm local_time{};
#ifdef _WIN32
  localtime_s(&local_time, &time_value);
#else
  localtime_r(&time_value, &local_time);
#endif

  std::ostringstream stream;
  stream << std::setfill('0') << std::setw(2) << local_time.tm_mday << "/"
         << std::setw(2) << (local_time.tm_mon + 1) << "  " << std::setw(2)
         << local_time.tm_hour << ":" << std::setw(2) << local_time.tm_min;
  return stream.str();
}

auto format_save_slot_seed(int seed) -> std::string {
  return std::string("SEED ") + std::to_string(seed);
}

auto format_save_slot_time(float time_of_day) -> std::string {
  const auto hours = static_cast<int>(std::floor(time_of_day));
  const auto minutes = static_cast<int>(
      std::round((time_of_day - static_cast<float>(hours)) * 60.0F));
  std::ostringstream stream;
  stream << "HEURE " << std::setfill('0') << std::setw(2) << hours << ":"
         << std::setfill('0') << std::setw(2) << (minutes % 60);
  return stream.str();
}

auto format_save_slot_mode(GameMode mode) -> std::string {
  return std::string(game_mode_label(mode));
}

auto pixel_to_ndc_x(float x, float viewport_width) -> float {
  return (x / viewport_width) * 2.0F - 1.0F;
}

auto pixel_to_ndc_y(float y, float viewport_height) -> float {
  return (y / viewport_height) * 2.0F - 1.0F;
}

auto atlas_uv_rect(const HotbarAtlasTile &tile) -> std::array<float, 4> {
  const auto uv_step = 1.0F / kBlockAtlasTilesPerAxis;
  const auto u0 = static_cast<float>(tile.x) * uv_step;
  const auto v0 = static_cast<float>(tile.y) * uv_step;
  return {u0, v0, u0 + uv_step, v0 + uv_step};
}

[[maybe_unused]] auto accent_uv_rect(const AccentAtlasTile &tile)
    -> std::array<float, 4> {
  const auto uv_step = 1.0F / kAccentAtlasTilesPerAxis;
  const auto u0 = static_cast<float>(tile.x) * uv_step;
  const auto v0 = static_cast<float>(tile.y) * uv_step;
  return {u0, v0, u0 + uv_step, v0 + uv_step};
}

void append_hud_quad(std::vector<HudVertex> &vertices, float viewport_width,
                     float viewport_height, float x, float y, float width,
                     float height, const std::array<float, 4> &color,
                     const std::array<float, 4> &uv_rect, float textured) {
  const auto left = pixel_to_ndc_x(x, viewport_width);
  const auto right = pixel_to_ndc_x(x + width, viewport_width);
  const auto bottom = pixel_to_ndc_y(y, viewport_height);
  const auto top = pixel_to_ndc_y(y + height, viewport_height);
  const auto u0 = uv_rect[0];
  const auto v0 = uv_rect[1];
  const auto u1 = uv_rect[2];
  const auto v1 = uv_rect[3];

  vertices.insert(vertices.end(), {
                                      {left, bottom, u0, v0, color[0], color[1],
                                       color[2], color[3], textured},
                                      {right, bottom, u1, v0, color[0],
                                       color[1], color[2], color[3], textured},
                                      {right, top, u1, v1, color[0], color[1],
                                       color[2], color[3], textured},
                                      {left, bottom, u0, v0, color[0], color[1],
                                       color[2], color[3], textured},
                                      {right, top, u1, v1, color[0], color[1],
                                       color[2], color[3], textured},
                                      {left, top, u0, v1, color[0], color[1],
                                       color[2], color[3], textured},
                                  });
}

void append_hud_rect(std::vector<HudVertex> &vertices, float viewport_width,
                     float viewport_height, float x, float y, float width,
                     float height, const std::array<float, 4> &color) {
  append_hud_quad(vertices, viewport_width, viewport_height, x, y, width,
                  height, color, {0.0F, 0.0F, 0.0F, 0.0F}, 0.0F);
}

auto bottom_to_top_left_y(float viewport_height, float bottom, float height)
    -> float {
  return viewport_height - bottom - height;
}

void append_hud_quad_top_left(std::vector<HudVertex> &vertices,
                              float viewport_width, float viewport_height,
                              float x, float y, float width, float height,
                              const std::array<float, 4> &color,
                              const std::array<float, 4> &uv_rect,
                              float textured) {
  append_hud_quad(vertices, viewport_width, viewport_height, x,
                  viewport_height - y - height, width, height, color, uv_rect,
                  textured);
}

void append_hud_rect_top_left(std::vector<HudVertex> &vertices,
                              float viewport_width, float viewport_height,
                              float x, float y, float width, float height,
                              const std::array<float, 4> &color) {
  append_hud_quad_top_left(vertices, viewport_width, viewport_height, x, y,
                           width, height, color, {0.0F, 0.0F, 0.0F, 0.0F},
                           0.0F);
}

void append_hud_solid_triangle_top_left(
    std::vector<HudVertex> &vertices, float viewport_width,
    float viewport_height, const glm::vec2 &first, const glm::vec2 &second,
    const glm::vec2 &third, const std::array<float, 4> &color) {
  const auto make_vertex = [&](const glm::vec2 &point) {
    return HudVertex{
        pixel_to_ndc_x(point.x, viewport_width),
        pixel_to_ndc_y(viewport_height - point.y, viewport_height),
        0.0F,
        0.0F,
        color[0],
        color[1],
        color[2],
        color[3],
        0.0F,
    };
  };
  vertices.push_back(make_vertex(first));
  vertices.push_back(make_vertex(second));
  vertices.push_back(make_vertex(third));
}

void append_hud_rounded_rect_top_left(std::vector<HudVertex> &vertices,
                                      float viewport_width,
                                      float viewport_height, float x, float y,
                                      float width, float height,
                                      float preferred_radius,
                                      const std::array<float, 4> &color) {
  if (viewport_width <= 0.0F || viewport_height <= 0.0F || width <= 0.0F ||
      height <= 0.0F || color[3] <= 0.0F) {
    return;
  }

  const auto metrics =
      modern_hud_rounded_rect_metrics(width, height, preferred_radius);
  if (metrics.corner_segments <= 0 || metrics.radius <= 0.0F) {
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, x, y,
                             width, height, color);
    return;
  }

  const auto radius = metrics.radius;
  const auto center_width = std::max(0.0F, width - radius * 2.0F);
  const auto middle_height = std::max(0.0F, height - radius * 2.0F);
  if (center_width > 0.0F) {
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             x + radius, y, center_width, height, color);
  }
  if (middle_height > 0.0F) {
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, x,
                             y + radius, radius, middle_height, color);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             x + width - radius, y + radius, radius,
                             middle_height, color);
  }

  constexpr float kPi = 3.14159265358979323846F;
  const std::array<glm::vec2, 4> centers{{
      {x + radius, y + radius},
      {x + width - radius, y + radius},
      {x + width - radius, y + height - radius},
      {x + radius, y + height - radius},
  }};
  constexpr std::array<float, 4> start_angles{{
      kPi,
      kPi * 1.5F,
      0.0F,
      kPi * 0.5F,
  }};
  const auto angle_step =
      (kPi * 0.5F) / static_cast<float>(metrics.corner_segments);
  for (std::size_t corner = 0U; corner < centers.size(); ++corner) {
    const auto center = centers[corner];
    for (int segment = 0; segment < metrics.corner_segments; ++segment) {
      const auto first_angle =
          start_angles[corner] + angle_step * static_cast<float>(segment);
      const auto second_angle = first_angle + angle_step;
      const auto first = center + glm::vec2{
                                      std::cos(first_angle) * radius,
                                      std::sin(first_angle) * radius,
                                  };
      const auto second = center + glm::vec2{
                                       std::cos(second_angle) * radius,
                                       std::sin(second_angle) * radius,
                                   };
      append_hud_solid_triangle_top_left(vertices, viewport_width,
                                         viewport_height, center, first, second,
                                         color);
    }
  }
}

[[maybe_unused]] void append_hud_rounded_rect_bottom_left(
    std::vector<HudVertex> &vertices, float viewport_width,
    float viewport_height, float x, float bottom, float width, float height,
    float radius, const std::array<float, 4> &color) {
  append_hud_rounded_rect_top_left(
      vertices, viewport_width, viewport_height, x,
      bottom_to_top_left_y(viewport_height, bottom, height), width, height,
      radius, color);
}

void append_hud_frame_top_left(std::vector<HudVertex> &vertices,
                               float viewport_width, float viewport_height,
                               float x, float y, float width, float height,
                               float border_thickness,
                               const std::array<float, 4> &border_color,
                               const std::array<float, 4> &fill_color) {
  append_hud_rect_top_left(vertices, viewport_width, viewport_height, x, y,
                           width, height, border_color);

  const auto inner_x = x + border_thickness;
  const auto inner_y = y + border_thickness;
  const auto inner_width = std::max(0.0F, width - border_thickness * 2.0F);
  const auto inner_height = std::max(0.0F, height - border_thickness * 2.0F);
  if (inner_width > 0.0F && inner_height > 0.0F) {
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, inner_x,
                             inner_y, inner_width, inner_height, fill_color);
  }
}

void append_hud_beveled_panel_top_left(
    std::vector<HudVertex> &vertices, float viewport_width,
    float viewport_height, float x, float y, float width, float height,
    float border_thickness, const std::array<float, 4> &border_color,
    const std::array<float, 4> &fill_color,
    const std::array<float, 4> &highlight_color,
    const std::array<float, 4> &shadow_color) {
  append_hud_frame_top_left(vertices, viewport_width, viewport_height, x, y,
                            width, height, border_thickness, border_color,
                            fill_color);

  const auto inner_x = x + border_thickness;
  const auto inner_y = y + border_thickness;
  const auto inner_width = std::max(0.0F, width - border_thickness * 2.0F);
  const auto inner_height = std::max(0.0F, height - border_thickness * 2.0F);
  if (inner_width <= 2.0F || inner_height <= 2.0F) {
    return;
  }

  const auto bevel =
      std::max(1.0F, static_cast<float>(std::floor(border_thickness * 0.55F)));
  append_hud_rect_top_left(vertices, viewport_width, viewport_height, inner_x,
                           inner_y, inner_width, bevel, highlight_color);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height, inner_x,
                           inner_y, bevel, inner_height, highlight_color);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height, inner_x,
                           inner_y + std::max(0.0F, inner_height - bevel),
                           inner_width, bevel, shadow_color);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           inner_x + std::max(0.0F, inner_width - bevel),
                           inner_y, bevel, inner_height, shadow_color);
}

[[maybe_unused]] void append_segmented_meter_top_left(
    std::vector<HudVertex> &vertices, float viewport_width,
    float viewport_height, float x, float y, float width, float height,
    std::size_t segments, float fill_ratio,
    const std::array<float, 4> &border_color,
    const std::array<float, 4> &background_color,
    const std::array<float, 4> &empty_segment_color,
    const std::array<float, 4> &fill_segment_color) {
  append_hud_beveled_panel_top_left(vertices, viewport_width, viewport_height,
                                    x, y, width, height, 3.0F, border_color,
                                    background_color, {1.0F, 1.0F, 1.0F, 0.10F},
                                    {0.0F, 0.0F, 0.0F, 0.34F});

  const auto inner_x = x + 6.0F;
  const auto inner_y = y + 5.0F;
  const auto inner_width = std::max(0.0F, width - 12.0F);
  const auto inner_height = std::max(0.0F, height - 10.0F);
  const auto gap = std::max(2.0F, inner_height * 0.18F);
  const auto segment_width =
      (inner_width -
       gap * static_cast<float>(segments > 0 ? segments - 1 : 0)) /
      static_cast<float>(std::max<std::size_t>(segments, 1));
  const auto filled_segments =
      glm::clamp(fill_ratio, 0.0F, 1.0F) * static_cast<float>(segments);

  for (std::size_t index = 0; index < segments; ++index) {
    const auto segment_x =
        inner_x + static_cast<float>(index) * (segment_width + gap);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             segment_x, inner_y, segment_width, inner_height,
                             empty_segment_color);

    const auto segment_fill =
        glm::clamp(filled_segments - static_cast<float>(index), 0.0F, 1.0F);
    if (segment_fill <= 0.0F) {
      continue;
    }

    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             segment_x, inner_y, segment_width * segment_fill,
                             inner_height, fill_segment_color);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             segment_x, inner_y, segment_width * segment_fill,
                             std::max(1.0F, inner_height * 0.18F),
                             {1.0F, 1.0F, 1.0F, fill_segment_color[3] * 0.18F});
  }
}

auto glyph_rows(char character) -> std::array<std::uint8_t, 7> {
  switch (
      static_cast<char>(std::toupper(static_cast<unsigned char>(character)))) {
  case '0':
    return {{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}};
  case '1':
    return {{0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}};
  case '2':
    return {{0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}};
  case '3':
    return {{0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}};
  case '4':
    return {{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}};
  case '5':
    return {{0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}};
  case '6':
    return {{0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}};
  case '7':
    return {{0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}};
  case '8':
    return {{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}};
  case '9':
    return {{0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};
  case 'A':
    return {{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
  case 'B':
    return {{0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}};
  case 'C':
    return {{0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}};
  case 'D':
    return {{0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}};
  case 'E':
    return {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}};
  case 'F':
    return {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}};
  case 'G':
    return {{0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}};
  case 'H':
    return {{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
  case 'I':
    return {{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}};
  case 'J':
    return {{0x1F, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}};
  case 'K':
    return {{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}};
  case 'L':
    return {{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}};
  case 'M':
    return {{0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}};
  case 'N':
    return {{0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}};
  case 'O':
    return {{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
  case 'P':
    return {{0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}};
  case 'Q':
    return {{0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}};
  case 'R':
    return {{0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}};
  case 'S':
    return {{0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}};
  case 'T':
    return {{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}};
  case 'U':
    return {{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
  case 'V':
    return {{0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}};
  case 'W':
    return {{0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}};
  case 'X':
    return {{0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}};
  case 'Y':
    return {{0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}};
  case 'Z':
    return {{0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}};
  case '(':
    return {{0x03, 0x06, 0x0C, 0x0C, 0x0C, 0x06, 0x03}};
  case ')':
    return {{0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18}};
  case '+':
    return {{0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}};
  case '%':
    return {{0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}};
  case '\'':
    return {{0x06, 0x06, 0x04, 0x08, 0x00, 0x00, 0x00}};
  case ',':
    return {{0x00, 0x00, 0x00, 0x00, 0x06, 0x06, 0x04}};
  case ':':
    return {{0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00}};
  case '!':
    return {{0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}};
  case '?':
    return {{0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}};
  case '.':
    return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}};
  case '-':
    return {{0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}};
  case '/':
    return {{0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}};
  case '>':
    return {{0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10}};
  case '_':
    return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}};
  default:
    return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  }
}

auto measure_pixel_text(std::string_view text, float pixel_size) -> float {
  if (const auto *font = active_modern_hud_font();
      font != nullptr && pixel_size > 0.0F) {
    const auto layout = font->build_quads(text, 0.0F, 0.0F, pixel_size * 7.0F);
    return layout.width;
  }

  float width = 0.0F;
  bool first = true;
  for (const auto character : text) {
    if (!first) {
      width += pixel_size;
    }
    width += (character == ' ' ? 3.0F : 5.0F) * pixel_size;
    first = false;
  }
  return width;
}

void append_pixel_text(std::vector<HudVertex> &vertices, float viewport_width,
                       float viewport_height, float x, float y,
                       float pixel_size, std::string_view text,
                       const std::array<float, 4> &color,
                       bool centered = false) {
  if (const auto *font = active_modern_hud_font();
      font != nullptr && pixel_size > 0.0F) {
    const auto requested_height = pixel_size * 7.0F;
    const auto scale = requested_height / font->metadata().font_em_pixels;
    auto origin_x = x;
    const auto measured = font->build_quads(text, 0.0F, 0.0F, requested_height);
    if (centered) {
      origin_x -= measured.width * 0.5F;
    }
    const auto baseline_y = y + font->metadata().ascent * scale;
    const auto layout =
        font->build_quads(text, origin_x, baseline_y, requested_height);
    for (const auto &quad : layout.quads) {
      const auto quad_width = quad.x1 - quad.x0;
      const auto quad_height = quad.y1 - quad.y0;
      if (quad_width <= 0.0F || quad_height <= 0.0F) {
        continue;
      }
      // L'asset est rangé ligne par ligne depuis le haut. J'inverse V
      // ici une seule fois afin de respecter l'origine OpenGL.
      append_hud_quad_top_left(vertices, viewport_width, viewport_height,
                               quad.x0, quad.y0, quad_width, quad_height, color,
                               {quad.u0, quad.v1, quad.u1, quad.v0}, 2.0F);
    }
    return;
  }

  auto cursor_x = x;
  if (centered) {
    cursor_x -= measure_pixel_text(text, pixel_size) * 0.5F;
  }

  for (const auto character : text) {
    if (character == ' ') {
      cursor_x += pixel_size * 4.0F;
      continue;
    }

    const auto rows = glyph_rows(character);
    for (std::size_t row = 0; row < rows.size(); ++row) {
      for (int column = 0; column < 5; ++column) {
        const auto bit = static_cast<std::uint8_t>(1U << (4 - column));
        if ((rows[row] & bit) == 0U) {
          continue;
        }
        append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                                 cursor_x +
                                     static_cast<float>(column) * pixel_size,
                                 y + static_cast<float>(row) * pixel_size,
                                 pixel_size, pixel_size, color);
      }
    }

    cursor_x += pixel_size * 6.0F;
  }
}

void append_pixel_text_bottom_left(std::vector<HudVertex> &vertices,
                                   float viewport_width, float viewport_height,
                                   float x, float bottom, float pixel_size,
                                   std::string_view text,
                                   const std::array<float, 4> &color,
                                   bool centered = false) {
  const auto text_height = pixel_size * 7.0F;
  append_pixel_text(vertices, viewport_width, viewport_height, x,
                    bottom_to_top_left_y(viewport_height, bottom, text_height),
                    pixel_size, text, color, centered);
}

using HudColor = std::array<float, 4>;

struct HudPanelPalette {
  HudColor frame{};
  HudColor fill{};
  HudColor highlight{};
  HudColor shadow{};
  HudColor trim{};
};

struct HudSlotPalette {
  HudPanelPalette shell{};
  HudColor accent{};
  HudColor glow{};
  HudColor motif{};
};

struct InventoryFocusItem {
  HotbarSlot slot{};
  bool has_item = false;
  bool from_carried_slot = false;
  InventorySlotGroup group = InventorySlotGroup::Storage;
};

auto hud_with_alpha(const HudColor &color, float alpha) -> HudColor {
  return {color[0], color[1], color[2], alpha};
}

auto hud_scale_rgb(const HudColor &color, float factor) -> HudColor {
  return {
      std::clamp(color[0] * factor, 0.0F, 1.0F),
      std::clamp(color[1] * factor, 0.0F, 1.0F),
      std::clamp(color[2] * factor, 0.0F, 1.0F),
      color[3],
  };
}

auto hud_mix(const HudColor &lhs, const HudColor &rhs, float t) -> HudColor {
  const auto blend = std::clamp(t, 0.0F, 1.0F);
  const auto inverse = 1.0F - blend;
  return {
      lhs[0] * inverse + rhs[0] * blend,
      lhs[1] * inverse + rhs[1] * blend,
      lhs[2] * inverse + rhs[2] * blend,
      lhs[3] * inverse + rhs[3] * blend,
  };
}

auto make_slate_panel_palette() -> HudPanelPalette {
  return {{0.06F, 0.07F, 0.08F, 0.98F},
          {0.15F, 0.16F, 0.18F, 0.94F},
          {0.44F, 0.46F, 0.50F, 0.22F},
          {0.02F, 0.02F, 0.03F, 0.58F},
          {0.72F, 0.74F, 0.78F, 0.10F}};
}

auto make_stone_panel_palette() -> HudPanelPalette {
  return {{0.08F, 0.08F, 0.10F, 0.98F},
          {0.22F, 0.23F, 0.26F, 0.95F},
          {0.66F, 0.68F, 0.72F, 0.18F},
          {0.03F, 0.03F, 0.04F, 0.62F},
          {0.86F, 0.88F, 0.92F, 0.07F}};
}

auto make_header_panel_palette() -> HudPanelPalette {
  return {{0.05F, 0.05F, 0.06F, 0.98F},
          {0.28F, 0.29F, 0.32F, 0.96F},
          {0.90F, 0.92F, 0.96F, 0.20F},
          {0.03F, 0.03F, 0.04F, 0.68F},
          {0.98F, 0.88F, 0.62F, 0.12F}};
}

auto make_warm_panel_palette(const HudColor &accent) -> HudPanelPalette {
  return {
      {0.10F, 0.08F, 0.05F, 0.98F},
      hud_mix(HudColor{0.20F, 0.16F, 0.12F, 0.96F},
              hud_with_alpha(accent, 0.96F), 0.18F),
      hud_with_alpha(hud_scale_rgb(accent, 1.20F), 0.24F),
      {0.02F, 0.02F, 0.02F, 0.66F},
      hud_with_alpha(hud_scale_rgb(accent, 1.10F), 0.14F),
  };
}

auto make_modern_glass_panel_palette(const HudColor &accent,
                                     float accent_strength = 0.12F)
    -> HudPanelPalette {
  const auto clamped_strength = std::clamp(accent_strength, 0.0F, 0.35F);
  return {
      hud_mix(HudColor{0.16F, 0.20F, 0.25F, 0.82F},
              hud_with_alpha(accent, 0.82F), clamped_strength * 0.55F),
      hud_mix(HudColor{0.075F, 0.10F, 0.14F, 0.78F},
              hud_with_alpha(accent, 0.78F), clamped_strength),
      {0.82F, 0.90F, 0.98F, 0.12F},
      {0.01F, 0.02F, 0.035F, 0.32F},
      hud_with_alpha(hud_scale_rgb(accent, 1.08F), 0.28F),
  };
}

auto make_modern_neutral_panel_palette() -> HudPanelPalette {
  return make_modern_glass_panel_palette({0.48F, 0.68F, 0.86F, 1.0F}, 0.08F);
}

auto ui_material_accent(BlockId block_id) -> HudColor {
  if (block_id == to_block_id(BlockType::Air)) {
    return {0.56F, 0.60F, 0.66F, 1.0F};
  }
  if (is_resource_ore(block_id)) {
    switch (static_cast<BlockType>(block_item_id(block_id))) {
    case BlockType::CoalOre:
      return {0.32F, 0.32F, 0.34F, 1.0F};
    case BlockType::IronOre:
      return {0.86F, 0.50F, 0.28F, 1.0F};
    case BlockType::GoldOre:
      return {0.98F, 0.76F, 0.28F, 1.0F};
    case BlockType::DiamondOre:
      return {0.36F, 0.82F, 0.90F, 1.0F};
    case BlockType::MetallicAlloyOre:
      return {0.74F, 0.68F, 0.94F, 1.0F};
    case BlockType::Air:
    default:
      return {0.63F, 0.67F, 0.74F, 1.0F};
    }
  }
  if (is_weapon_item(block_id)) {
    return {0.82F, 0.80F, 0.74F, 1.0F};
  }
  if (is_tool_item(block_id)) {
    return {0.72F, 0.76F, 0.74F, 1.0F};
  }
  if (is_inventory_only_item(block_id)) {
    switch (static_cast<BlockType>(block_item_id(block_id))) {
    case BlockType::Pastron:
    case BlockType::RoundShield:
      return {0.86F, 0.60F, 0.28F, 1.0F};
    case BlockType::Shoes:
    case BlockType::Pants:
      return {0.50F, 0.62F, 0.78F, 1.0F};
    case BlockType::Sword:
    case BlockType::Spear:
    case BlockType::Air:
    default:
      return {0.82F, 0.80F, 0.74F, 1.0F};
    }
  }

  switch (block_visual_material(block_id)) {
  case BlockVisualMaterial::Terrain:
    return {0.46F, 0.66F, 0.34F, 1.0F};
  case BlockVisualMaterial::Rock:
    return {0.63F, 0.67F, 0.74F, 1.0F};
  case BlockVisualMaterial::Sand:
    return {0.83F, 0.75F, 0.49F, 1.0F};
  case BlockVisualMaterial::Wood:
    return {0.71F, 0.52F, 0.29F, 1.0F};
  case BlockVisualMaterial::Foliage:
    return {0.36F, 0.72F, 0.38F, 1.0F};
  case BlockVisualMaterial::Flora:
    return {0.86F, 0.48F, 0.36F, 1.0F};
  case BlockVisualMaterial::Water:
    return {0.33F, 0.60F, 0.96F, 1.0F};
  case BlockVisualMaterial::Emissive:
    return {0.98F, 0.78F, 0.30F, 1.0F};
  case BlockVisualMaterial::Snow:
    return {0.90F, 0.93F, 0.98F, 1.0F};
  case BlockVisualMaterial::Glass:
    return {0.62F, 0.84F, 0.98F, 1.0F};
  default:
    return {0.56F, 0.60F, 0.66F, 1.0F};
  }
}

auto item_material_label(BlockId block_id) -> std::string_view {
  if (block_id == to_block_id(BlockType::Air)) {
    return "VIDE";
  }
  if (is_weapon_item(block_id)) {
    return "ARME";
  }
  if (is_tool_item(block_id)) {
    return "OUTIL";
  }
  if (is_inventory_only_item(block_id)) {
    return "EQUIPEMENT";
  }
  if (is_resource_ore(block_id)) {
    return "MINERAI";
  }

  switch (block_visual_material(block_id)) {
  case BlockVisualMaterial::Terrain:
    return "SOL";
  case BlockVisualMaterial::Rock:
    return "ROCHE";
  case BlockVisualMaterial::Sand:
    return "SABLE";
  case BlockVisualMaterial::Wood:
    return "BOIS";
  case BlockVisualMaterial::Foliage:
    return "FEUILLAGE";
  case BlockVisualMaterial::Flora:
    return "FLORE";
  case BlockVisualMaterial::Water:
    return "EAU";
  case BlockVisualMaterial::Emissive:
    return "LUMIERE";
  case BlockVisualMaterial::Snow:
    return "NEIGE";
  case BlockVisualMaterial::Glass:
    return "VERRE";
  default:
    return "VIDE";
  }
}

auto inventory_slot_group_label(InventorySlotGroup group) -> std::string_view {
  switch (group) {
  case InventorySlotGroup::Hotbar:
    return "BARRE RAPIDE";
  case InventorySlotGroup::Equipment:
    return "EQUIPEMENT";
  case InventorySlotGroup::Storage:
  default:
    return "SAC";
  }
}

void append_hud_shadow_top_left(std::vector<HudVertex> &vertices,
                                float viewport_width, float viewport_height,
                                float x, float y, float width, float height,
                                float spread, const HudColor &color) {
  if (width <= 0.0F || height <= 0.0F || spread <= 0.0F || color[3] <= 0.0F) {
    return;
  }

  constexpr int kShadowLayerCount = 3;
  for (int layer = 0; layer < kShadowLayerCount; ++layer) {
    const auto layer_factor =
        static_cast<float>(layer + 1) / static_cast<float>(kShadowLayerCount);
    const auto pad = spread * layer_factor;
    const auto offset_x = pad * 0.18F;
    const auto offset_y = pad * 0.30F;
    const auto alpha = color[3] * (1.0F - static_cast<float>(layer) * 0.24F);
    append_hud_rect_top_left(
        vertices, viewport_width, viewport_height, x - pad * 0.35F + offset_x,
        y - pad * 0.20F + offset_y, width + pad * 0.70F, height + pad * 0.70F,
        {color[0], color[1], color[2], alpha});
  }
}

void append_hud_shadow_bottom_left(std::vector<HudVertex> &vertices,
                                   float viewport_width, float viewport_height,
                                   float x, float bottom, float width,
                                   float height, float spread,
                                   const HudColor &color) {
  append_hud_shadow_top_left(
      vertices, viewport_width, viewport_height, x,
      bottom_to_top_left_y(viewport_height, bottom, height), width, height,
      spread, color);
}

void append_corner_brackets_top_left(std::vector<HudVertex> &vertices,
                                     float viewport_width,
                                     float viewport_height, float x, float y,
                                     float width, float height, float size,
                                     const HudColor &color) {
  if (width <= 0.0F || height <= 0.0F || size <= 0.0F || color[3] <= 0.0F) {
    return;
  }

  const auto arm = std::max(1.0F, size);
  const auto arm_length =
      std::min(std::min(width, height),
               std::max(arm * 2.4F, std::min(width, height) * 0.18F));

  append_hud_rect_top_left(vertices, viewport_width, viewport_height, x, y,
                           arm_length, arm, color);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height, x, y, arm,
                           arm_length, color);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           x + width - arm_length, y, arm_length, arm, color);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           x + width - arm, y, arm, arm_length, color);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height, x,
                           y + height - arm, arm_length, arm, color);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height, x,
                           y + height - arm_length, arm, arm_length, color);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           x + width - arm_length, y + height - arm, arm_length,
                           arm, color);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           x + width - arm, y + height - arm_length, arm,
                           arm_length, color);
}

void append_empty_slot_motif_top_left(std::vector<HudVertex> &vertices,
                                      float viewport_width,
                                      float viewport_height, float x, float y,
                                      float size, const HudColor &color) {
  if (size <= 0.0F || color[3] <= 0.0F) {
    return;
  }

  const auto thickness = std::max(1.0F, size * 0.06F);
  const auto pad = size * 0.26F;
  append_hud_rect_top_left(vertices, viewport_width, viewport_height, x + pad,
                           y + size * 0.5F - thickness * 0.5F,
                           std::max(0.0F, size - pad * 2.0F), thickness, color);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           x + size * 0.5F - thickness * 0.5F, y + pad,
                           thickness, std::max(0.0F, size - pad * 2.0F), color);
  append_hud_rect_top_left(
      vertices, viewport_width, viewport_height, x + size * 0.5F - thickness,
      y + size * 0.5F - thickness, thickness * 2.0F, thickness * 2.0F,
      hud_with_alpha(color, color[3] * 0.75F));
}

void append_stylized_panel_top_left(std::vector<HudVertex> &vertices,
                                    float viewport_width, float viewport_height,
                                    float x, float y, float width, float height,
                                    float border_thickness,
                                    const HudPanelPalette &palette,
                                    bool cast_shadow = true) {
  if (width <= 0.0F || height <= 0.0F) {
    return;
  }

  if (cast_shadow) {
    append_hud_shadow_top_left(
        vertices, viewport_width, viewport_height, x, y, width, height,
        std::max(4.0F, border_thickness * 2.2F), {0.0F, 0.0F, 0.0F, 0.18F});
  }

  append_hud_beveled_panel_top_left(vertices, viewport_width, viewport_height,
                                    x, y, width, height, border_thickness,
                                    palette.frame, palette.fill,
                                    palette.highlight, palette.shadow);

  const auto inner_x = x + border_thickness;
  const auto inner_y = y + border_thickness;
  const auto inner_width = std::max(0.0F, width - border_thickness * 2.0F);
  const auto inner_height = std::max(0.0F, height - border_thickness * 2.0F);
  if (inner_width <= 0.0F || inner_height <= 0.0F) {
    return;
  }

  const auto trim_height = std::max(1.0F, border_thickness * 0.72F);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height, inner_x,
                           inner_y, inner_width, trim_height, palette.trim);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height, inner_x,
                           inner_y + std::max(0.0F, inner_height - trim_height),
                           inner_width, trim_height,
                           {0.0F, 0.0F, 0.0F, palette.shadow[3] * 0.40F});
  append_hud_rect_top_left(vertices, viewport_width, viewport_height, inner_x,
                           inner_y + trim_height + 1.0F, inner_width,
                           std::max(1.0F, trim_height * 0.75F),
                           {1.0F, 1.0F, 1.0F, palette.highlight[3] * 0.28F});
  append_corner_brackets_top_left(
      vertices, viewport_width, viewport_height, x + border_thickness * 0.5F,
      y + border_thickness * 0.5F, std::max(0.0F, width - border_thickness),
      std::max(0.0F, height - border_thickness),
      std::max(2.0F, border_thickness * 0.75F),
      hud_with_alpha(palette.trim, palette.trim[3] * 0.85F));
}

void append_stylized_panel_bottom_left(std::vector<HudVertex> &vertices,
                                       float viewport_width,
                                       float viewport_height, float x,
                                       float bottom, float width, float height,
                                       float border_thickness,
                                       const HudPanelPalette &palette,
                                       bool cast_shadow = true) {
  append_stylized_panel_top_left(
      vertices, viewport_width, viewport_height, x,
      bottom_to_top_left_y(viewport_height, bottom, height), width, height,
      border_thickness, palette, cast_shadow);
}

void append_modern_panel_top_left(std::vector<HudVertex> &vertices,
                                  float viewport_width, float viewport_height,
                                  float x, float y, float width, float height,
                                  float border_thickness,
                                  const HudPanelPalette &palette,
                                  bool cast_shadow = true) {
  if (width <= 0.0F || height <= 0.0F) {
    return;
  }

  const auto border =
      std::clamp(border_thickness, 1.0F, std::min(width, height) * 0.25F);
  const auto radius = modern_hud_panel_radius(width, height, border);

  // Je compose le panneau avec des courbes simples et bornées : le HUD
  // moderne reste doux sans dépendre d'un shader ou d'une texture dédiée.
  if (cast_shadow) {
    append_hud_rounded_rect_top_left(vertices, viewport_width, viewport_height,
                                     x + std::max(1.0F, border * 0.45F),
                                     y + std::max(2.0F, border * 0.85F), width,
                                     height, radius, {0.0F, 0.0F, 0.0F, 0.24F});
  }

  append_hud_rounded_rect_top_left(vertices, viewport_width, viewport_height, x,
                                   y, width, height, radius, palette.frame);

  const auto inner_x = x + border;
  const auto inner_y = y + border;
  const auto inner_width = std::max(0.0F, width - border * 2.0F);
  const auto inner_height = std::max(0.0F, height - border * 2.0F);
  if (inner_width <= 0.0F || inner_height <= 0.0F) {
    return;
  }

  const auto inner_radius = std::max(0.0F, radius - border);
  append_hud_rounded_rect_top_left(vertices, viewport_width, viewport_height,
                                   inner_x, inner_y, inner_width, inner_height,
                                   inner_radius, palette.fill);

  const auto highlight_height =
      std::clamp(border * 0.70F, 1.0F, inner_height * 0.22F);
  append_hud_rounded_rect_top_left(
      vertices, viewport_width, viewport_height, inner_x + inner_radius * 0.25F,
      inner_y + std::max(1.0F, border * 0.24F),
      std::max(0.0F, inner_width - inner_radius * 0.50F), highlight_height,
      highlight_height * 0.50F, palette.highlight);

  const auto trim_width = std::clamp(width * 0.28F, 12.0F, 72.0F);
  append_hud_rounded_rect_top_left(
      vertices, viewport_width, viewport_height,
      x + (width - trim_width) * 0.5F, y + border * 0.42F, trim_width,
      std::max(1.0F, border * 0.55F), std::max(0.5F, border * 0.28F),
      palette.trim);
}

void append_modern_panel_bottom_left(std::vector<HudVertex> &vertices,
                                     float viewport_width,
                                     float viewport_height, float x,
                                     float bottom, float width, float height,
                                     float border_thickness,
                                     const HudPanelPalette &palette,
                                     bool cast_shadow = true) {
  append_modern_panel_top_left(
      vertices, viewport_width, viewport_height, x,
      bottom_to_top_left_y(viewport_height, bottom, height), width, height,
      border_thickness, palette, cast_shadow);
}

void append_hud_scanlines_top_left(std::vector<HudVertex> &vertices,
                                   float viewport_width, float viewport_height,
                                   float x, float y, float width, float height,
                                   float spacing, const HudColor &color) {
  if (width <= 0.0F || height <= 0.0F || spacing <= 0.0F || color[3] <= 0.0F) {
    return;
  }

  for (float line_y = y + spacing; line_y < y + height - 1.0F;
       line_y += spacing) {
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, x,
                             line_y, width, 1.0F, color);
  }
}

auto build_slot_palette(const HotbarSlot &slot, bool selected, bool hovered,
                        bool hotbar_slot) -> HudSlotPalette {
  const auto accent = hotbar_slot_has_item(slot)
                          ? ui_material_accent(slot.block_id)
                          : HudColor{0.42F, 0.45F, 0.50F, 1.0F};
  const auto base_frame = hotbar_slot ? HudColor{0.07F, 0.08F, 0.09F, 0.98F}
                                      : HudColor{0.08F, 0.08F, 0.10F, 0.98F};
  const auto base_fill = hotbar_slot ? HudColor{0.16F, 0.17F, 0.19F, 0.94F}
                                     : HudColor{0.15F, 0.16F, 0.18F, 0.94F};
  const auto empty_fill = hotbar_slot ? HudColor{0.10F, 0.11F, 0.13F, 0.86F}
                                      : HudColor{0.09F, 0.10F, 0.12F, 0.82F};

  HudSlotPalette palette{};
  palette.accent = accent;
  palette.glow =
      selected ? HudColor{1.0F, 0.88F, 0.48F, hotbar_slot ? 0.16F : 0.14F}
               : (hovered ? hud_with_alpha(hud_scale_rgb(accent, 1.05F), 0.12F)
                          : HudColor{0.0F, 0.0F, 0.0F, 0.0F});
  palette.motif = hotbar_slot_has_item(slot)
                      ? hud_with_alpha(hud_scale_rgb(accent, 1.08F), 0.16F)
                      : HudColor{0.38F, 0.40F, 0.45F, 0.10F};

  auto frame = base_frame;
  auto fill = hotbar_slot_has_item(slot)
                  ? hud_mix(base_fill, hud_with_alpha(accent, base_fill[3]),
                            hotbar_slot ? 0.12F : 0.16F)
                  : empty_fill;
  auto highlight = hotbar_slot_has_item(slot)
                       ? hud_with_alpha(hud_scale_rgb(accent, 1.18F),
                                        hovered ? 0.24F : 0.18F)
                       : HudColor{0.55F, 0.58F, 0.64F, hovered ? 0.16F : 0.10F};
  auto trim = hotbar_slot_has_item(slot)
                  ? hud_with_alpha(hud_scale_rgb(accent, 1.08F),
                                   hotbar_slot ? 0.18F : 0.20F)
                  : HudColor{0.42F, 0.44F, 0.48F, 0.08F};

  if (selected) {
    frame = {0.98F, 0.89F, 0.58F, 1.0F};
    fill = hud_mix(fill, HudColor{0.28F, 0.22F, 0.15F, fill[3]}, 0.30F);
    highlight = {1.0F, 0.97F, 0.82F, 0.28F};
    trim = {1.0F, 0.90F, 0.58F, 0.28F};
    palette.motif = hud_with_alpha(hud_scale_rgb(accent, 1.12F), 0.22F);
  } else if (hovered) {
    frame = {0.92F, 0.94F, 0.98F, 0.98F};
    fill = hud_mix(fill, HudColor{0.22F, 0.24F, 0.28F, fill[3]}, 0.22F);
    trim = hud_with_alpha(hud_scale_rgb(accent, 1.15F), 0.24F);
  }

  palette.shell = {frame, fill, highlight, {0.02F, 0.02F, 0.03F, 0.60F}, trim};
  return palette;
}

void append_stylized_slot_top_left(std::vector<HudVertex> &vertices,
                                   float viewport_width, float viewport_height,
                                   float x, float y, float size,
                                   const HudSlotPalette &palette,
                                   bool has_item) {
  const auto border = std::max(2.0F, size * 0.08F);
  const auto glow_pad = std::max(2.0F, size * 0.08F);

  if (palette.glow[3] > 0.0F) {
    append_hud_shadow_top_left(
        vertices, viewport_width, viewport_height, x - glow_pad, y - glow_pad,
        size + glow_pad * 2.0F, size + glow_pad * 2.0F, glow_pad * 1.8F,
        hud_with_alpha(palette.glow, palette.glow[3] * 0.65F));
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             x - glow_pad, y - glow_pad, size + glow_pad * 2.0F,
                             size + glow_pad * 2.0F, palette.glow);
  }

  append_stylized_panel_top_left(vertices, viewport_width, viewport_height, x,
                                 y, size, size, border, palette.shell);

  const auto inset = border + std::max(1.0F, size * 0.06F);
  const auto inner_size = std::max(0.0F, size - inset * 2.0F);
  const auto inner_border = std::max(1.0F, border * 0.55F);
  const auto well_frame =
      hud_with_alpha(hud_scale_rgb(palette.accent, has_item ? 0.74F : 0.45F),
                     has_item ? 0.38F : 0.16F);
  const auto well_fill =
      has_item ? hud_with_alpha(hud_scale_rgb(palette.accent, 0.28F), 0.22F)
               : HudColor{0.05F, 0.06F, 0.08F, 0.62F};
  append_hud_beveled_panel_top_left(
      vertices, viewport_width, viewport_height, x + inset, y + inset,
      inner_size, inner_size, inner_border, well_frame, well_fill,
      hud_with_alpha(palette.accent, has_item ? 0.12F : 0.04F),
      {0.0F, 0.0F, 0.0F, 0.34F});

  const auto trim_height = std::max(1.0F, size * 0.06F);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           x + border + 1.0F, y + border + 1.0F,
                           std::max(0.0F, size - border * 2.0F - 2.0F),
                           trim_height, palette.shell.trim);
  append_corner_brackets_top_left(
      vertices, viewport_width, viewport_height, x + 1.0F, y + 1.0F,
      std::max(0.0F, size - 2.0F), std::max(0.0F, size - 2.0F),
      std::max(2.0F, border * 0.85F),
      hud_with_alpha(palette.accent, has_item ? 0.20F : 0.08F));

  if (!has_item) {
    append_empty_slot_motif_top_left(vertices, viewport_width, viewport_height,
                                     x + inset, y + inset, inner_size,
                                     palette.motif);
  }
}

void append_stylized_slot_bottom_left(std::vector<HudVertex> &vertices,
                                      float viewport_width,
                                      float viewport_height, float x,
                                      float bottom, float size,
                                      const HudSlotPalette &palette,
                                      bool has_item) {
  append_stylized_slot_top_left(
      vertices, viewport_width, viewport_height, x,
      bottom_to_top_left_y(viewport_height, bottom, size), size, palette,
      has_item);
}

void append_modern_slot_top_left(std::vector<HudVertex> &vertices,
                                 float viewport_width, float viewport_height,
                                 float x, float y, float size,
                                 const HudSlotPalette &palette, bool has_item) {
  const auto border = std::max(2.0F, size * 0.065F);
  const auto glow_pad = std::max(2.0F, size * 0.055F);

  if (palette.glow[3] > 0.0F) {
    append_hud_rounded_rect_top_left(
        vertices, viewport_width, viewport_height, x - glow_pad, y - glow_pad,
        size + glow_pad * 2.0F, size + glow_pad * 2.0F,
        modern_hud_panel_radius(size + glow_pad * 2.0F, size + glow_pad * 2.0F,
                                border),
        palette.glow);
  }

  append_modern_panel_top_left(vertices, viewport_width, viewport_height, x, y,
                               size, size, border, palette.shell, false);

  const auto inset = border + std::max(1.0F, size * 0.055F);
  const auto inner_size = std::max(0.0F, size - inset * 2.0F);
  if (inner_size <= 0.0F) {
    return;
  }

  const auto well_frame =
      hud_with_alpha(hud_scale_rgb(palette.accent, has_item ? 0.74F : 0.45F),
                     has_item ? 0.38F : 0.16F);
  const auto well_fill =
      has_item ? hud_with_alpha(hud_scale_rgb(palette.accent, 0.28F), 0.22F)
               : HudColor{
                     0.05F,
                     0.06F,
                     0.08F,
                     0.62F,
                 };
  const auto inner_radius = modern_hud_panel_radius(
      inner_size, inner_size, std::max(1.0F, border * 0.45F));
  append_hud_rounded_rect_top_left(vertices, viewport_width, viewport_height,
                                   x + inset, y + inset, inner_size, inner_size,
                                   inner_radius, well_frame);

  const auto well_border = std::max(1.0F, border * 0.42F);
  append_hud_rounded_rect_top_left(
      vertices, viewport_width, viewport_height, x + inset + well_border,
      y + inset + well_border, std::max(0.0F, inner_size - well_border * 2.0F),
      std::max(0.0F, inner_size - well_border * 2.0F),
      std::max(0.0F, inner_radius - well_border), well_fill);

  if (!has_item) {
    append_empty_slot_motif_top_left(vertices, viewport_width, viewport_height,
                                     x + inset, y + inset, inner_size,
                                     palette.motif);
  }
}

void append_modern_slot_bottom_left(std::vector<HudVertex> &vertices,
                                    float viewport_width, float viewport_height,
                                    float x, float bottom, float size,
                                    const HudSlotPalette &palette,
                                    bool has_item) {
  append_modern_slot_top_left(
      vertices, viewport_width, viewport_height, x,
      bottom_to_top_left_y(viewport_height, bottom, size), size, palette,
      has_item);
}

void append_avatar_preview_art(std::vector<HudVertex> &vertices,
                               float viewport_width, float viewport_height,
                               const InventoryMenuLayout &layout) {
  const auto scale = layout.silhouette_scale;
  const auto center_x = layout.preview_center_x;
  const auto base_y = layout.preview_base_y;
  const auto panel_x = layout.preview_panel_x;
  const auto panel_y = layout.preview_panel_y;
  const auto panel_width = layout.preview_panel_width;
  const auto panel_height = layout.preview_panel_height;
  const auto inner_pad = std::max(12.0F, layout.slot_size * 0.26F);
  const auto beam_width = std::clamp(panel_width * 0.38F, 42.0F, 84.0F);
  const auto beam_x = center_x - beam_width * 0.5F;
  const auto beam_y = panel_y + inner_pad + 10.0F;
  const auto beam_height =
      std::max(0.0F, panel_height - inner_pad * 2.0F - 36.0F);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height, beam_x,
                           beam_y, beam_width, beam_height,
                           {1.0F, 1.0F, 1.0F, 0.04F});
  append_hud_rect_top_left(
      vertices, viewport_width, viewport_height, beam_x + beam_width * 0.16F,
      beam_y, beam_width * 0.68F, beam_height, {0.82F, 0.90F, 1.0F, 0.06F});

  const auto pedestal_width = std::clamp(panel_width * 0.54F, 74.0F, 124.0F);
  const auto pedestal_height = std::max(8.0F, layout.slot_size * 0.30F);
  const auto pedestal_x = center_x - pedestal_width * 0.5F;
  const auto pedestal_y = base_y + scale * 0.50F;
  append_stylized_panel_top_left(vertices, viewport_width, viewport_height,
                                 pedestal_x, pedestal_y, pedestal_width,
                                 pedestal_height + 8.0F, 3.0F,
                                 make_slate_panel_palette(), false);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           center_x - pedestal_width * 0.34F,
                           pedestal_y - scale * 0.34F, pedestal_width * 0.68F,
                           std::max(2.0F, scale * 0.32F),
                           {0.0F, 0.0F, 0.0F, 0.18F});

  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           center_x - scale * 1.40F, base_y - scale * 0.05F,
                           scale * 2.80F, scale * 0.24F,
                           {0.0F, 0.0F, 0.0F, 0.14F});

  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           center_x - scale * 0.76F, base_y - scale * 6.90F,
                           scale * 1.52F, scale * 1.52F,
                           {0.20F, 0.14F, 0.10F, 1.0F});
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           center_x - scale * 0.68F, base_y - scale * 6.58F,
                           scale * 1.36F, scale * 1.16F,
                           {0.93F, 0.79F, 0.62F, 1.0F});
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           center_x - scale * 0.56F, base_y - scale * 6.20F,
                           scale * 1.12F, scale * 0.16F,
                           {1.0F, 1.0F, 1.0F, 0.06F});

  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           center_x - scale * 0.96F, base_y - scale * 5.25F,
                           scale * 1.92F, scale * 2.48F,
                           {0.30F, 0.54F, 0.90F, 1.0F});
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           center_x - scale * 0.96F, base_y - scale * 5.25F,
                           scale * 1.92F, scale * 0.42F,
                           {0.48F, 0.72F, 0.98F, 0.64F});
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           center_x - scale * 0.72F, base_y - scale * 3.12F,
                           scale * 1.44F, scale * 0.34F,
                           {0.17F, 0.24F, 0.42F, 1.0F});

  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           center_x - scale * 1.64F, base_y - scale * 5.08F,
                           scale * 0.56F, scale * 1.98F,
                           {0.93F, 0.79F, 0.62F, 1.0F});
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           center_x + scale * 1.08F, base_y - scale * 5.08F,
                           scale * 0.56F, scale * 1.98F,
                           {0.93F, 0.79F, 0.62F, 1.0F});

  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           center_x - scale * 0.82F, base_y - scale * 2.90F,
                           scale * 0.66F, scale * 2.52F,
                           {0.21F, 0.26F, 0.44F, 1.0F});
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           center_x + scale * 0.16F, base_y - scale * 2.90F,
                           scale * 0.66F, scale * 2.52F,
                           {0.21F, 0.26F, 0.44F, 1.0F});
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           center_x - scale * 0.82F, base_y - scale * 0.54F,
                           scale * 0.66F, scale * 0.26F,
                           {0.10F, 0.12F, 0.16F, 1.0F});
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           center_x + scale * 0.16F, base_y - scale * 0.54F,
                           scale * 0.66F, scale * 0.26F,
                           {0.10F, 0.12F, 0.16F, 1.0F});

  append_corner_brackets_top_left(
      vertices, viewport_width, viewport_height, panel_x + inner_pad * 0.55F,
      panel_y + inner_pad * 0.65F,
      std::max(0.0F, panel_width - inner_pad * 1.10F),
      std::max(0.0F, panel_height - inner_pad * 1.30F),
      std::max(2.0F, scale * 0.22F), {1.0F, 1.0F, 1.0F, 0.08F});
}

void append_keycap_top_left(std::vector<HudVertex> &vertices,
                            float viewport_width, float viewport_height,
                            const InventoryKeycapLayout &keycap,
                            float pixel_size) {
  const auto palette =
      keycap.selected ? make_warm_panel_palette({0.98F, 0.84F, 0.46F, 1.0F})
                      : make_slate_panel_palette();
  append_stylized_panel_top_left(
      vertices, viewport_width, viewport_height, keycap.x, keycap.y,
      keycap.width, keycap.height, std::max(1.0F, keycap.height * 0.18F),
      palette, false);

  const auto label = std::to_string(keycap.number);
  const auto text_y =
      keycap.y + std::max(0.0F, (keycap.height - pixel_size * 7.0F) * 0.5F);
  append_pixel_text(vertices, viewport_width, viewport_height,
                    keycap.x + keycap.width * 0.5F + pixel_size,
                    text_y + pixel_size, pixel_size, label,
                    {0.0F, 0.0F, 0.0F, 0.52F}, true);
  append_pixel_text(vertices, viewport_width, viewport_height,
                    keycap.x + keycap.width * 0.5F, text_y, pixel_size, label,
                    keycap.selected ? HudColor{0.99F, 0.96F, 0.88F, 1.0F}
                                    : HudColor{0.90F, 0.92F, 0.96F, 0.94F},
                    true);
}

auto resolve_inventory_focus_item(const InventoryMenuState &inventory,
                                  const HotbarState &hotbar)
    -> InventoryFocusItem {
  InventoryFocusItem focus{};
  if (inventory.carrying_item &&
      inventory_slot_has_item(inventory.carried_slot)) {
    focus.slot = inventory.carried_slot;
    focus.has_item = true;
    focus.from_carried_slot = true;
    return focus;
  }

  if (inventory.hovered_slot.has_value()) {
    if (const auto *hovered_slot =
            inventory_slot_ptr(inventory, hotbar, *inventory.hovered_slot);
        hovered_slot != nullptr && inventory_slot_has_item(*hovered_slot)) {
      focus.slot = *hovered_slot;
      focus.has_item = true;
      focus.group = inventory.hovered_slot->group;
      return focus;
    }
  }

  if (inventory_slot_has_item(hotbar.selected_slot())) {
    focus.slot = hotbar.selected_slot();
    focus.has_item = true;
    focus.group = InventorySlotGroup::Hotbar;
  }
  return focus;
}

auto resolve_viewmodel_held_item(const InventoryMenuState &inventory,
                                 const HotbarState &hotbar) noexcept
    -> BlockId {
  const auto &selected_slot = hotbar.selected_slot();
  if (is_musket_item(selected_slot)) {
    return to_block_id(BlockType::Musket);
  }
  if (hotbar_slot_has_item(selected_slot) &&
      is_tool_item(selected_slot.block_id)) {
    return to_block_id(BlockType::Air);
  }

  const auto &equipped_weapon =
      inventory.equipment_slots[equipment_slot_index(EquipmentSlot::Weapon)];
  if (hotbar_slot_has_item(equipped_weapon) &&
      is_weapon_item(equipped_weapon.block_id)) {
    return block_item_id(equipped_weapon.block_id);
  }

  if (hotbar_slot_has_item(selected_slot) &&
      is_weapon_item(selected_slot.block_id)) {
    return block_item_id(selected_slot.block_id);
  }

  return to_block_id(BlockType::Air);
}

void append_stack_count(std::vector<HudVertex> &vertices, float viewport_width,
                        float viewport_height, float right_x, float bottom_y,
                        float pixel_size, std::uint8_t count) {
  if (count <= 1) {
    return;
  }

  const auto count_text = std::to_string(count);
  const auto text_width = measure_pixel_text(count_text, pixel_size);
  const auto padding_x = std::max(2.0F, pixel_size * 1.15F);
  const auto padding_y = std::max(1.0F, pixel_size * 0.78F);
  const auto badge_width = text_width + padding_x * 2.0F;
  const auto badge_height = pixel_size * 7.0F + padding_y * 2.0F;
  const auto badge_x = right_x - badge_width;
  const auto badge_y = bottom_y - badge_height;
  const auto badge_border = std::max(1.0F, pixel_size * 0.55F);

  append_hud_shadow_top_left(vertices, viewport_width, viewport_height, badge_x,
                             badge_y, badge_width, badge_height,
                             std::max(2.0F, pixel_size * 1.6F),
                             {0.0F, 0.0F, 0.0F, 0.20F});
  append_hud_beveled_panel_top_left(
      vertices, viewport_width, viewport_height, badge_x, badge_y, badge_width,
      badge_height, badge_border, {0.04F, 0.04F, 0.05F, 0.98F},
      {0.16F, 0.17F, 0.19F, 0.96F}, {0.90F, 0.92F, 0.96F, 0.10F},
      {0.0F, 0.0F, 0.0F, 0.42F});
  append_hud_rect_top_left(
      vertices, viewport_width, viewport_height, badge_x + badge_border,
      badge_y + badge_border, std::max(0.0F, badge_width - badge_border * 2.0F),
      std::max(1.0F, badge_border * 0.75F), {1.0F, 0.92F, 0.72F, 0.08F});
  append_pixel_text(vertices, viewport_width, viewport_height,
                    badge_x + padding_x + pixel_size,
                    badge_y + padding_y + pixel_size, pixel_size, count_text,
                    {0.0F, 0.0F, 0.0F, 0.58F});
  append_pixel_text(vertices, viewport_width, viewport_height,
                    badge_x + padding_x, badge_y + padding_y, pixel_size,
                    count_text, {0.98F, 0.98F, 0.98F, 0.98F});
}

void append_stack_count_bottom_left(std::vector<HudVertex> &vertices,
                                    float viewport_width, float viewport_height,
                                    float right_x, float bottom,
                                    float pixel_size, std::uint8_t count) {
  append_stack_count(vertices, viewport_width, viewport_height, right_x,
                     viewport_height - bottom, pixel_size, count);
}

template <std::size_t RowCount>
void append_pixel_mask_bottom_left(
    std::vector<HudVertex> &vertices, float viewport_width,
    float viewport_height, float x, float bottom, float pixel_size,
    const std::array<std::uint8_t, RowCount> &rows, int columns,
    const std::array<float, 4> &color, int max_fill_columns = -1) {
  const auto mask_height = pixel_size * static_cast<float>(rows.size());
  const auto top_left_y =
      bottom_to_top_left_y(viewport_height, bottom, mask_height);
  for (std::size_t row = 0; row < rows.size(); ++row) {
    for (int column = 0; column < columns; ++column) {
      if (max_fill_columns >= 0 && column >= max_fill_columns) {
        continue;
      }

      const auto bit = static_cast<std::uint8_t>(1U << (columns - 1 - column));
      if ((rows[row] & bit) == 0U) {
        continue;
      }

      append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                               x + static_cast<float>(column) * pixel_size,
                               top_left_y +
                                   static_cast<float>(row) * pixel_size,
                               pixel_size, pixel_size, color);
    }
  }
}

void append_vital_glyph_bottom_left(std::vector<HudVertex> &vertices,
                                    float viewport_width, float viewport_height,
                                    float x, float bottom, float size,
                                    HudGlyphFill fill,
                                    const std::array<std::uint8_t, 8> &rows,
                                    const std::array<float, 4> &empty_color,
                                    const std::array<float, 4> &fill_color,
                                    const std::array<float, 4> &shine_color) {
  constexpr int kGlyphColumns = 8;
  const auto pixel_size = size / static_cast<float>(kGlyphColumns);
  const auto shadow_offset = std::max(1.0F, pixel_size * 0.55F);

  append_pixel_mask_bottom_left(vertices, viewport_width, viewport_height,
                                x + shadow_offset, bottom - shadow_offset,
                                pixel_size, rows, kGlyphColumns,
                                {0.0F, 0.0F, 0.0F, 0.46F});
  append_pixel_mask_bottom_left(vertices, viewport_width, viewport_height, x,
                                bottom, pixel_size, rows, kGlyphColumns,
                                empty_color);

  const auto fill_columns =
      fill == HudGlyphFill::Full
          ? kGlyphColumns
          : (fill == HudGlyphFill::Half ? kGlyphColumns / 2 : 0);
  if (fill_columns > 0) {
    append_pixel_mask_bottom_left(vertices, viewport_width, viewport_height, x,
                                  bottom, pixel_size, rows, kGlyphColumns,
                                  fill_color, fill_columns);
    append_pixel_mask_bottom_left(
        vertices, viewport_width, viewport_height, x, bottom, pixel_size, rows,
        kGlyphColumns, shine_color, std::min(fill_columns, kGlyphColumns / 2));
  }
}

void append_heart_glyph_bottom_left(std::vector<HudVertex> &vertices,
                                    float viewport_width, float viewport_height,
                                    const VitalGlyphLayout &glyph) {
  constexpr std::array<std::uint8_t, 8> kHeartRows{
      0b01100110, 0b11111111, 0b11111111, 0b11111111,
      0b01111110, 0b00111100, 0b00011000, 0b00000000,
  };

  append_vital_glyph_bottom_left(
      vertices, viewport_width, viewport_height, glyph.x, glyph.bottom,
      glyph.size, glyph.fill, kHeartRows, {0.24F, 0.08F, 0.10F, 0.80F},
      {0.86F, 0.18F, 0.24F, 0.98F}, {1.0F, 0.56F, 0.60F, 0.34F});
}

void append_bubble_glyph_bottom_left(std::vector<HudVertex> &vertices,
                                     float viewport_width,
                                     float viewport_height,
                                     const VitalGlyphLayout &glyph) {
  constexpr std::array<std::uint8_t, 8> kBubbleRows{
      0b00111100, 0b01111110, 0b11100111, 0b11111111,
      0b11111111, 0b01111110, 0b00111100, 0b00010000,
  };

  append_vital_glyph_bottom_left(
      vertices, viewport_width, viewport_height, glyph.x, glyph.bottom,
      glyph.size, glyph.fill, kBubbleRows, {0.08F, 0.17F, 0.26F, 0.74F},
      {0.42F, 0.80F, 0.98F, 0.96F}, {0.92F, 0.98F, 1.0F, 0.28F});
}

auto item_stack_display_label(const HotbarSlot &slot) -> std::string {
  if (!inventory_slot_has_item(slot)) {
    return "MAINS VIDES";
  }

  std::string label(inventory_item_label(slot.block_id));
  if (slot.count > 1) {
    label += " X";
    label += std::to_string(slot.count);
  }
  return label;
}

constexpr float kBlockBreakOverlayMaterialClass = 9.0F;
constexpr float kBlockBreakOverlaySkyLight = 1.0F;
constexpr float kBlockBreakOverlayBlockLight = 0.22F;
constexpr float kBlockBreakOverlayAo = 1.0F;
constexpr float kBlockBreakOverlayBaseInflate = 0.0035F;
constexpr float kBlockBreakOverlayProgressInflate = 0.0105F;

using OverlayQuad = std::array<std::array<float, 3>, 4>;
using OverlayUvs = std::array<std::array<float, 2>, 4>;

auto overlay_tile_uvs(const BlockAtlasTile &tile, float min_u = 0.0F,
                      float min_v = 0.0F, float max_u = 1.0F,
                      float max_v = 1.0F) -> OverlayUvs {
  const auto uv_step = 1.0F / kBlockAtlasTilesPerAxis;
  const auto tile_u0 = static_cast<float>(tile.x) * uv_step;
  const auto tile_v0 = static_cast<float>(tile.y) * uv_step;
  const auto u0 = tile_u0 + min_u * uv_step;
  const auto v0 = tile_v0 + min_v * uv_step;
  const auto u1 = tile_u0 + max_u * uv_step;
  const auto v1 = tile_v0 + max_v * uv_step;
  return {{
      {u1, v0},
      {u1, v1},
      {u0, v1},
      {u0, v0},
  }};
}

void append_block_break_quad(std::vector<ChunkVertex> &vertices,
                             std::vector<std::uint32_t> &indices,
                             const OverlayQuad &positions,
                             const OverlayUvs &uvs,
                             const std::array<float, 3> &normal,
                             float face_shade) {
  const auto base_index = static_cast<std::uint32_t>(vertices.size());
  for (std::size_t index = 0; index < positions.size(); ++index) {
    vertices.push_back({
        positions[index][0],
        positions[index][1],
        positions[index][2],
        uvs[index][0],
        uvs[index][1],
        normal[0],
        normal[1],
        normal[2],
        face_shade,
        kBlockBreakOverlayAo,
        kBlockBreakOverlaySkyLight,
        kBlockBreakOverlayBlockLight,
        kBlockBreakOverlayMaterialClass,
        0.0F,
    });
  }

  indices.insert(indices.end(), {
                                    base_index + 0U,
                                    base_index + 1U,
                                    base_index + 2U,
                                    base_index + 0U,
                                    base_index + 2U,
                                    base_index + 3U,
                                });
}

void append_block_break_double_sided_quad(std::vector<ChunkVertex> &vertices,
                                          std::vector<std::uint32_t> &indices,
                                          const OverlayQuad &positions,
                                          const OverlayUvs &uvs,
                                          const std::array<float, 3> &normal,
                                          float face_shade) {
  append_block_break_quad(vertices, indices, positions, uvs, normal,
                          face_shade);

  const OverlayQuad reversed_positions{{
      positions[3],
      positions[2],
      positions[1],
      positions[0],
  }};
  const std::array<float, 3> reversed_normal{{
      -normal[0],
      -normal[1],
      -normal[2],
  }};
  append_block_break_quad(vertices, indices, reversed_positions, uvs,
                          reversed_normal, face_shade * 0.96F);
}

void append_block_break_cube_mesh(std::vector<ChunkVertex> &vertices,
                                  std::vector<std::uint32_t> &indices,
                                  const BlockCoord &block,
                                  const BlockAtlasTile &tile, float inflate,
                                  float top_height = 1.0F) {
  const auto min_x = static_cast<float>(block.x) - inflate;
  const auto max_x = static_cast<float>(block.x + 1) + inflate;
  const auto min_y = static_cast<float>(block.y) - inflate;
  const auto max_y = static_cast<float>(block.y) + top_height + inflate;
  const auto min_z = static_cast<float>(block.z) - inflate;
  const auto max_z = static_cast<float>(block.z + 1) + inflate;
  const auto uvs = overlay_tile_uvs(tile);

  append_block_break_quad(vertices, indices,
                          {{{max_x, min_y, min_z},
                            {max_x, max_y, min_z},
                            {max_x, max_y, max_z},
                            {max_x, min_y, max_z}}},
                          uvs, {1.0F, 0.0F, 0.0F}, 0.85F);
  append_block_break_quad(vertices, indices,
                          {{{min_x, min_y, max_z},
                            {min_x, max_y, max_z},
                            {min_x, max_y, min_z},
                            {min_x, min_y, min_z}}},
                          uvs, {-1.0F, 0.0F, 0.0F}, 0.85F);
  append_block_break_quad(vertices, indices,
                          {{{min_x, max_y, max_z},
                            {max_x, max_y, max_z},
                            {max_x, max_y, min_z},
                            {min_x, max_y, min_z}}},
                          uvs, {0.0F, 1.0F, 0.0F}, 1.0F);
  append_block_break_quad(vertices, indices,
                          {{{min_x, min_y, min_z},
                            {max_x, min_y, min_z},
                            {max_x, min_y, max_z},
                            {min_x, min_y, max_z}}},
                          uvs, {0.0F, -1.0F, 0.0F}, 0.65F);
  append_block_break_quad(vertices, indices,
                          {{{max_x, min_y, max_z},
                            {max_x, max_y, max_z},
                            {min_x, max_y, max_z},
                            {min_x, min_y, max_z}}},
                          uvs, {0.0F, 0.0F, 1.0F}, 0.75F);
  append_block_break_quad(vertices, indices,
                          {{{min_x, min_y, min_z},
                            {min_x, max_y, min_z},
                            {max_x, max_y, min_z},
                            {max_x, min_y, min_z}}},
                          uvs, {0.0F, 0.0F, -1.0F}, 0.75F);
}

void append_block_break_cross_mesh(std::vector<ChunkVertex> &vertices,
                                   std::vector<std::uint32_t> &indices,
                                   const BlockCoord &block,
                                   const BlockAtlasTile &tile, float inflate) {
  const auto min_edge = 0.18F - inflate;
  const auto max_edge = 0.82F + inflate;
  const auto min_y = static_cast<float>(block.y) - inflate;
  const auto max_y = static_cast<float>(block.y) + 0.95F + inflate;
  const auto world_x = static_cast<float>(block.x);
  const auto world_z = static_cast<float>(block.z);
  const auto uvs = overlay_tile_uvs(tile);

  append_block_break_double_sided_quad(
      vertices, indices,
      {{
          {world_x + min_edge, min_y, world_z + min_edge},
          {world_x + min_edge, max_y, world_z + min_edge},
          {world_x + max_edge, max_y, world_z + max_edge},
          {world_x + max_edge, min_y, world_z + max_edge},
      }},
      uvs, {0.70710677F, 0.0F, -0.70710677F}, 0.95F);
  append_block_break_double_sided_quad(
      vertices, indices,
      {{
          {world_x + max_edge, min_y, world_z + min_edge},
          {world_x + max_edge, max_y, world_z + min_edge},
          {world_x + min_edge, max_y, world_z + max_edge},
          {world_x + min_edge, min_y, world_z + max_edge},
      }},
      uvs, {0.70710677F, 0.0F, 0.70710677F}, 0.95F);
}

void append_block_break_torch_mesh(std::vector<ChunkVertex> &vertices,
                                   std::vector<std::uint32_t> &indices,
                                   const BlockCoord &block, BlockId block_id,
                                   const BlockAtlasTile &tile, float inflate) {
  constexpr float base_min_x = 6.0F / 16.0F;
  constexpr float base_max_x = 10.0F / 16.0F;
  constexpr float base_min_z = 6.0F / 16.0F;
  constexpr float base_max_z = 10.0F / 16.0F;
  constexpr float base_min_y = 0.0F;
  constexpr float base_shaft_max_y = 10.0F / 16.0F;
  constexpr float base_head_max_y = 14.0F / 16.0F;
  constexpr float wall_mount_offset = 4.5F / 16.0F;
  constexpr float wall_pivot_y = 3.5F / 16.0F;
  constexpr float wall_tilt_radians = 22.5F * 3.14159265358979323846F / 180.0F;

  const auto support_offset = torch_support_offset(block_id);
  const auto wall_torch = is_wall_torch_block(block_id);
  const auto uvs = overlay_tile_uvs(tile);

  const auto transform_local_position =
      [&](const std::array<float, 3> &local_position) {
        if (!wall_torch) {
          return std::array<float, 3>{
              static_cast<float>(block.x) + local_position[0],
              static_cast<float>(block.y) + local_position[1],
              static_cast<float>(block.z) + local_position[2],
          };
        }

        auto x = local_position[0] +
                 static_cast<float>(support_offset.x) * wall_mount_offset;
        auto y = local_position[1];
        auto z = local_position[2] +
                 static_cast<float>(support_offset.z) * wall_mount_offset;
        const auto pivot_x =
            0.5F + static_cast<float>(support_offset.x) * wall_mount_offset;
        const auto pivot_z =
            0.5F + static_cast<float>(support_offset.z) * wall_mount_offset;

        x -= pivot_x;
        y -= wall_pivot_y;
        z -= pivot_z;

        if (support_offset.x != 0) {
          const auto tilt =
              static_cast<float>(support_offset.x) * wall_tilt_radians;
          const auto cos_tilt = std::cos(tilt);
          const auto sin_tilt = std::sin(tilt);
          const auto rotated_x = x * cos_tilt - y * sin_tilt;
          const auto rotated_y = x * sin_tilt + y * cos_tilt;
          x = rotated_x;
          y = rotated_y;
        } else if (support_offset.z != 0) {
          const auto tilt =
              -static_cast<float>(support_offset.z) * wall_tilt_radians;
          const auto cos_tilt = std::cos(tilt);
          const auto sin_tilt = std::sin(tilt);
          const auto rotated_y = y * cos_tilt - z * sin_tilt;
          const auto rotated_z = y * sin_tilt + z * cos_tilt;
          y = rotated_y;
          z = rotated_z;
        }

        return std::array<float, 3>{
            static_cast<float>(block.x) + x + pivot_x,
            static_cast<float>(block.y) + y + wall_pivot_y,
            static_cast<float>(block.z) + z + pivot_z,
        };
      };

  const auto inflate_local_x = inflate;
  const auto inflate_local_z = inflate;
  const auto inflate_min_y = inflate;
  const auto inflate_shaft_y = inflate * 0.45F;
  const auto inflate_head_y = inflate;

  const auto make_face =
      [&](const std::array<std::array<float, 3>, 4> &local_positions) {
        std::array<std::array<float, 3>, 4> positions{};
        for (std::size_t i = 0; i < local_positions.size(); ++i) {
          positions[i] = transform_local_position(local_positions[i]);
        }
        return positions;
      };

  append_block_break_quad(
      vertices, indices,
      make_face(
          {{{base_max_x + inflate_local_x, base_min_y - inflate_min_y,
             base_min_z - inflate_local_z},
            {base_max_x + inflate_local_x, base_shaft_max_y + inflate_shaft_y,
             base_min_z - inflate_local_z},
            {base_max_x + inflate_local_x, base_shaft_max_y + inflate_shaft_y,
             base_max_z + inflate_local_z},
            {base_max_x + inflate_local_x, base_min_y - inflate_min_y,
             base_max_z + inflate_local_z}}}),
      uvs, {1.0F, 0.0F, 0.0F}, 0.85F);
  append_block_break_quad(
      vertices, indices,
      make_face(
          {{{base_min_x - inflate_local_x, base_min_y - inflate_min_y,
             base_max_z + inflate_local_z},
            {base_min_x - inflate_local_x, base_shaft_max_y + inflate_shaft_y,
             base_max_z + inflate_local_z},
            {base_min_x - inflate_local_x, base_shaft_max_y + inflate_shaft_y,
             base_min_z - inflate_local_z},
            {base_min_x - inflate_local_x, base_min_y - inflate_min_y,
             base_min_z - inflate_local_z}}}),
      uvs, {-1.0F, 0.0F, 0.0F}, 0.85F);
  append_block_break_quad(
      vertices, indices,
      make_face(
          {{{base_max_x + inflate_local_x, base_min_y - inflate_min_y,
             base_max_z + inflate_local_z},
            {base_max_x + inflate_local_x, base_shaft_max_y + inflate_shaft_y,
             base_max_z + inflate_local_z},
            {base_min_x - inflate_local_x, base_shaft_max_y + inflate_shaft_y,
             base_max_z + inflate_local_z},
            {base_min_x - inflate_local_x, base_min_y - inflate_min_y,
             base_max_z + inflate_local_z}}}),
      uvs, {0.0F, 0.0F, 1.0F}, 0.75F);
  append_block_break_quad(
      vertices, indices,
      make_face(
          {{{base_min_x - inflate_local_x, base_min_y - inflate_min_y,
             base_min_z - inflate_local_z},
            {base_min_x - inflate_local_x, base_shaft_max_y + inflate_shaft_y,
             base_min_z - inflate_local_z},
            {base_max_x + inflate_local_x, base_shaft_max_y + inflate_shaft_y,
             base_min_z - inflate_local_z},
            {base_max_x + inflate_local_x, base_min_y - inflate_min_y,
             base_min_z - inflate_local_z}}}),
      uvs, {0.0F, 0.0F, -1.0F}, 0.75F);
  append_block_break_quad(
      vertices, indices,
      make_face({{{base_min_x - inflate_local_x, base_min_y - inflate_min_y,
                   base_min_z - inflate_local_z},
                  {base_max_x + inflate_local_x, base_min_y - inflate_min_y,
                   base_min_z - inflate_local_z},
                  {base_max_x + inflate_local_x, base_min_y - inflate_min_y,
                   base_max_z + inflate_local_z},
                  {base_min_x - inflate_local_x, base_min_y - inflate_min_y,
                   base_max_z + inflate_local_z}}}),
      uvs, {0.0F, -1.0F, 0.0F}, 0.65F);

  append_block_break_quad(
      vertices, indices,
      make_face(
          {{{base_max_x + inflate_local_x, base_shaft_max_y + inflate_shaft_y,
             base_min_z - inflate_local_z},
            {base_max_x + inflate_local_x, base_head_max_y + inflate_head_y,
             base_min_z - inflate_local_z},
            {base_max_x + inflate_local_x, base_head_max_y + inflate_head_y,
             base_max_z + inflate_local_z},
            {base_max_x + inflate_local_x, base_shaft_max_y + inflate_shaft_y,
             base_max_z + inflate_local_z}}}),
      uvs, {1.0F, 0.0F, 0.0F}, 0.85F);
  append_block_break_quad(
      vertices, indices,
      make_face(
          {{{base_min_x - inflate_local_x, base_shaft_max_y + inflate_shaft_y,
             base_max_z + inflate_local_z},
            {base_min_x - inflate_local_x, base_head_max_y + inflate_head_y,
             base_max_z + inflate_local_z},
            {base_min_x - inflate_local_x, base_head_max_y + inflate_head_y,
             base_min_z - inflate_local_z},
            {base_min_x - inflate_local_x, base_shaft_max_y + inflate_shaft_y,
             base_min_z - inflate_local_z}}}),
      uvs, {-1.0F, 0.0F, 0.0F}, 0.85F);
  append_block_break_quad(
      vertices, indices,
      make_face(
          {{{base_max_x + inflate_local_x, base_shaft_max_y + inflate_shaft_y,
             base_max_z + inflate_local_z},
            {base_max_x + inflate_local_x, base_head_max_y + inflate_head_y,
             base_max_z + inflate_local_z},
            {base_min_x - inflate_local_x, base_head_max_y + inflate_head_y,
             base_max_z + inflate_local_z},
            {base_min_x - inflate_local_x, base_shaft_max_y + inflate_shaft_y,
             base_max_z + inflate_local_z}}}),
      uvs, {0.0F, 0.0F, 1.0F}, 0.75F);
  append_block_break_quad(
      vertices, indices,
      make_face(
          {{{base_min_x - inflate_local_x, base_shaft_max_y + inflate_shaft_y,
             base_min_z - inflate_local_z},
            {base_min_x - inflate_local_x, base_head_max_y + inflate_head_y,
             base_min_z - inflate_local_z},
            {base_max_x + inflate_local_x, base_head_max_y + inflate_head_y,
             base_min_z - inflate_local_z},
            {base_max_x + inflate_local_x, base_shaft_max_y + inflate_shaft_y,
             base_min_z - inflate_local_z}}}),
      uvs, {0.0F, 0.0F, -1.0F}, 0.75F);
  append_block_break_quad(
      vertices, indices,
      make_face(
          {{{base_min_x - inflate_local_x, base_head_max_y + inflate_head_y,
             base_max_z + inflate_local_z},
            {base_max_x + inflate_local_x, base_head_max_y + inflate_head_y,
             base_max_z + inflate_local_z},
            {base_max_x + inflate_local_x, base_head_max_y + inflate_head_y,
             base_min_z - inflate_local_z},
            {base_min_x - inflate_local_x, base_head_max_y + inflate_head_y,
             base_min_z - inflate_local_z}}}),
      uvs, {0.0F, 1.0F, 0.0F}, 1.0F);
}

void build_block_break_overlay_mesh_data_into(
    const BlockBreakProgress &break_progress, ChunkMeshData &mesh) {
  mesh.vertices.clear();
  mesh.indices.clear();
  mesh.water_vertices.clear();
  mesh.water_indices.clear();
  mesh.face_count = 0;
  mesh.water_face_count = 0;
  if (mesh.vertices.capacity() < 64U) {
    mesh.vertices.reserve(64U);
  }
  if (mesh.indices.capacity() < 96U) {
    mesh.indices.reserve(96U);
  }
  if (!break_progress.active ||
      !is_block_breakable_at(break_progress.block, break_progress.block_id)) {
    return;
  }

  const auto tile = block_break_crack_tile(break_progress.crack_stage);
  const auto inflate =
      kBlockBreakOverlayBaseInflate +
      break_progress.progress * kBlockBreakOverlayProgressInflate;

  mesh.vertices.reserve(64U);
  mesh.indices.reserve(96U);

  switch (block_mesh_type(break_progress.block_id)) {
  case BlockMeshType::Cross:
    append_block_break_cross_mesh(mesh.vertices, mesh.indices,
                                  break_progress.block, tile, inflate);
    break;
  case BlockMeshType::Torch:
    append_block_break_torch_mesh(mesh.vertices, mesh.indices,
                                  break_progress.block, break_progress.block_id,
                                  tile, inflate);
    break;
  case BlockMeshType::Water:
    append_block_break_cube_mesh(mesh.vertices, mesh.indices,
                                 break_progress.block, tile, inflate,
                                 15.0F / 16.0F);
    break;
  case BlockMeshType::Ramp:
  case BlockMeshType::FullCube:
  default:
    append_block_break_cube_mesh(mesh.vertices, mesh.indices,
                                 break_progress.block, tile, inflate);
    break;
  }

  mesh.face_count = mesh.indices.size() / 6U;
}

void build_organic_block_break_overlay_mesh_data_into(
    const World &world, const BlockBreakProgress &break_progress,
    ChunkMeshData &mesh) {
  mesh.vertices.clear();
  mesh.indices.clear();
  mesh.water_vertices.clear();
  mesh.water_indices.clear();
  mesh.face_count = 0U;
  mesh.water_face_count = 0U;
  if (!break_progress.active ||
      !is_organic_terrain_block(break_progress.block_id) ||
      !is_block_breakable_at(break_progress.block, break_progress.block_id)) {
    return;
  }

  const OrganicTerrainSection target_section{
      break_progress.block,
      break_progress.block,
  };
  const OrganicTerrainMesher mesher{};
  const auto surface = mesher.build_mesh(
      target_section,
      [&world](int x, int y, int z) {
        return OrganicTerrainCellSample{
            world.peek_block_or_generated(x, y, z),
            world.get_sky_light(x, y, z),
            world.get_block_light(x, y, z),
        };
      },
      32U, 48U);
  if (surface.empty()) {
    return;
  }

  const auto tile = block_break_crack_tile(break_progress.crack_stage);
  const auto tile_size = 1.0F / kBlockAtlasTilesPerAxis;
  const auto tile_u = static_cast<float>(tile.x) * tile_size;
  const auto tile_v = static_cast<float>(tile.y) * tile_size;
  const auto inflate =
      kBlockBreakOverlayBaseInflate +
      break_progress.progress * kBlockBreakOverlayProgressInflate;
  mesh.vertices.reserve(surface.vertices.size());
  mesh.indices = surface.indices;

  for (const auto &source : surface.vertices) {
    const auto normal =
        glm::normalize(glm::vec3{source.nx, source.ny, source.nz});
    const auto absolute_normal = glm::abs(normal);
    float local_u = 0.0F;
    float local_v = 0.0F;
    if (absolute_normal.x >= absolute_normal.y &&
        absolute_normal.x >= absolute_normal.z) {
      local_u = source.z - static_cast<float>(break_progress.block.z);
      local_v = source.y - static_cast<float>(break_progress.block.y);
    } else if (absolute_normal.y >= absolute_normal.z) {
      local_u = source.x - static_cast<float>(break_progress.block.x);
      local_v = source.z - static_cast<float>(break_progress.block.z);
    } else {
      local_u = source.x - static_cast<float>(break_progress.block.x);
      local_v = source.y - static_cast<float>(break_progress.block.y);
    }
    local_u = glm::clamp(local_u, 0.0F, 1.0F);
    local_v = glm::clamp(local_v, 0.0F, 1.0F);

    mesh.vertices.push_back({
        source.x + normal.x * inflate,
        source.y + normal.y * inflate,
        source.z + normal.z * inflate,
        tile_u + local_u * tile_size,
        tile_v + local_v * tile_size,
        normal.x,
        normal.y,
        normal.z,
        1.0F,
        kBlockBreakOverlayAo,
        kBlockBreakOverlaySkyLight,
        kBlockBreakOverlayBlockLight,
        kBlockBreakOverlayMaterialClass,
        0.0F,
    });
  }
  mesh.face_count = mesh.indices.size() / 6U;
}

[[nodiscard]] auto issou_hud_color(const glm::vec4 &color) noexcept
    -> HudColor {
  return {color.r, color.g, color.b, color.a};
}

[[nodiscard]] auto issou_hud_label(IssouHudElementKind kind) noexcept
    -> std::string_view {
  switch (kind) {
  case IssouHudElementKind::BossHealth:
    return "COLOSSE";
  case IssouHudElementKind::BossStagger:
    return "SEQUENCEMENT";
  case IssouHudElementKind::WeaponStability:
    return "STABILITE";
  case IssouHudElementKind::Momentum:
    return "MOMENTUM";
  case IssouHudElementKind::Charge:
    return "CHARGE";
  case IssouHudElementKind::Countdown:
    return "COMBAT";
  }
  return {};
}

[[nodiscard]] auto issou_result_label(IssouResultMetric metric) noexcept
    -> std::string_view {
  switch (metric) {
  case IssouResultMetric::CombatSeconds:
    return "TEMPS DE COMBAT";
  case IssouResultMetric::DamageDealt:
    return "DEGATS INFLIGES";
  case IssouResultMetric::LimbsSevered:
    return "MEMBRES TRANCHES";
  case IssouResultMetric::PerfectGuards:
    return "GARDES PARFAITES";
  case IssouResultMetric::MissedAttacks:
    return "ATTAQUES MANQUEES";
  case IssouResultMetric::MaximumMomentum:
    return "MOMENTUM MAX";
  case IssouResultMetric::MaximumTargetsHit:
    return "CIBLES TOUCHEES MAX";
  }
  return {};
}

[[nodiscard]] auto format_issou_result_value(
    const IssouResultLine &line) -> std::string {
  std::ostringstream stream;
  if (line.metric == IssouResultMetric::CombatSeconds) {
    stream << std::fixed << std::setprecision(1) << line.value << " S";
  } else {
    stream << std::fixed << std::setprecision(0) << line.value;
  }
  return stream.str();
}

[[nodiscard]] auto format_issou_hud_caption(
    const IssouHudElement &element) -> std::string {
  std::ostringstream stream;
  stream << issou_hud_label(element.kind);
  if (element.kind == IssouHudElementKind::Momentum) {
    stream << " X" << static_cast<int>(std::lround(element.secondary_value));
  } else if (element.kind != IssouHudElementKind::Countdown) {
    stream << ' ' << static_cast<int>(std::lround(element.value * 100.0F))
           << '%';
  }
  return stream.str();
}

struct RendererIssouResolvedStyle {
  float scale = 1.0F;
  float opacity = 0.88F;
  bool high_contrast = false;
};

[[nodiscard]] auto resolve_renderer_issou_style(
    const RendererIssouHudSnapshot &snapshot) noexcept
    -> RendererIssouResolvedStyle {
  RendererIssouResolvedStyle style{};
  auto found_scale = false;
  auto found_opacity = false;
  for (const auto &element : snapshot.element_view()) {
    if (!found_scale &&
        element.kind == IssouHudElementKind::BossHealth &&
        element.rect.height > 0.0F) {
      style.scale = std::clamp(element.rect.height / 14.0F, 0.50F, 1.60F);
      found_scale = true;
    } else if (!found_scale &&
               element.kind == IssouHudElementKind::Countdown &&
               element.rect.height > 0.0F) {
      style.scale = std::clamp(element.rect.height / 42.0F, 0.50F, 1.60F);
      found_scale = true;
    }
    const auto element_opacity =
        std::max(element.foreground.a, element.background.a);
    if (element_opacity > 0.0F) {
      if (!found_opacity) {
        style.opacity = 0.0F;
      }
      style.opacity = std::max(style.opacity, element_opacity);
      found_opacity = true;
    }
    if (element.background.a > 0.0F &&
        element.background.r <= 0.01F &&
        element.background.g <= 0.01F &&
        element.background.b <= 0.01F) {
      style.high_contrast = true;
    }
  }
  style.opacity =
      found_opacity ? std::clamp(style.opacity, 0.0F, 1.0F) : 0.88F;
  return style;
}

void append_issou_hud_text(std::vector<HudVertex> &vertices,
                           float viewport_width, float viewport_height,
                           float center_x, float y, float pixel_size,
                           std::string_view text, const HudColor &color,
                           bool centered = true) {
  if (text.empty() || pixel_size <= 0.0F || color[3] <= 0.0F) {
    return;
  }
  append_pixel_text(vertices, viewport_width, viewport_height,
                    center_x + std::max(1.0F, pixel_size * 0.55F),
                    y + std::max(1.0F, pixel_size * 0.55F), pixel_size, text,
                    {0.0F, 0.0F, 0.0F, color[3] * 0.72F}, centered);
  append_pixel_text(vertices, viewport_width, viewport_height, center_x, y,
                    pixel_size, text, color, centered);
}

void append_issou_hud_gauge(std::vector<HudVertex> &vertices,
                            float viewport_width, float viewport_height,
                            const IssouHudElement &element) {
  if (!element.visible) {
    return;
  }

  const auto foreground = issou_hud_color(element.foreground);
  const auto background = issou_hud_color(element.background);
  const auto &rect = element.rect;
  const auto radius = std::max(1.0F, rect.height * 0.28F);
  append_hud_rounded_rect_top_left(vertices, viewport_width, viewport_height,
                                   rect.x, rect.y, rect.width, rect.height,
                                   radius, background);

  if (element.kind == IssouHudElementKind::Countdown) {
    const auto border = std::max(1.0F, rect.height * 0.06F);
    append_corner_brackets_top_left(
        vertices, viewport_width, viewport_height, rect.x, rect.y, rect.width,
        rect.height, border,
        hud_with_alpha(foreground, foreground[3] * 0.82F));
    const auto seconds =
        std::max(0, static_cast<int>(std::ceil(element.secondary_value)));
    const auto text = std::to_string(seconds);
    auto pixel_size = std::clamp(rect.height / 9.0F, 1.0F, 7.0F);
    const auto available_width = std::max(1.0F, rect.width - border * 4.0F);
    const auto measured = measure_pixel_text(text, pixel_size);
    if (measured > available_width) {
      pixel_size *= available_width / measured;
    }
    append_issou_hud_text(
        vertices, viewport_width, viewport_height,
        rect.x + rect.width * 0.5F,
        rect.y + (rect.height - pixel_size * 7.0F) * 0.5F, pixel_size, text,
        foreground);
    return;
  }

  const auto inset = std::clamp(rect.height * 0.16F, 1.0F, 3.0F);
  const auto inner_x = rect.x + inset;
  const auto inner_y = rect.y + inset;
  const auto inner_width = std::max(0.0F, rect.width - inset * 2.0F);
  const auto inner_height = std::max(0.0F, rect.height - inset * 2.0F);
  const auto fill_width = inner_width * std::clamp(element.value, 0.0F, 1.0F);
  if (fill_width > 0.0F && inner_height > 0.0F) {
    append_hud_rounded_rect_top_left(
        vertices, viewport_width, viewport_height, inner_x, inner_y, fill_width,
        inner_height, std::max(0.5F, radius - inset), foreground);
    append_hud_rect_top_left(
        vertices, viewport_width, viewport_height, inner_x, inner_y, fill_width,
        std::max(1.0F, inner_height * 0.18F),
        {1.0F, 1.0F, 1.0F, foreground[3] * 0.18F});
  }

  const auto caption = format_issou_hud_caption(element);
  auto pixel_size = std::clamp(rect.height / 9.0F, 0.72F, 2.2F);
  const auto available_width = std::max(1.0F, rect.width - inset * 4.0F);
  const auto measured = measure_pixel_text(caption, pixel_size);
  if (measured > available_width) {
    pixel_size *= available_width / measured;
  }
  const auto text_color =
      HudColor{0.98F, 0.98F, 0.96F, std::max(foreground[3], background[3])};
  append_issou_hud_text(
      vertices, viewport_width, viewport_height,
      rect.x + rect.width * 0.5F,
      rect.y + (rect.height - pixel_size * 7.0F) * 0.5F, pixel_size, caption,
      text_color);
}

void append_issou_results(std::vector<HudVertex> &vertices,
                          float viewport_width, float viewport_height,
                          const RendererIssouHudSnapshot &snapshot) {
  const auto style = resolve_renderer_issou_style(snapshot);
  const auto margin =
      std::clamp(std::min(viewport_width, viewport_height) * 0.035F, 8.0F,
                 32.0F);
  const auto line_count = snapshot.result_line_view().size();
  const auto panel_width =
      std::max(1.0F, std::min(640.0F * style.scale,
                             viewport_width - margin * 2.0F));
  const auto header_height =
      std::clamp(78.0F * style.scale, 52.0F, 116.0F);
  const auto footer_height =
      std::clamp(24.0F * style.scale, 16.0F, 36.0F);
  const auto available_rows =
      std::max(0.0F, viewport_height - margin * 2.0F - header_height -
                         footer_height);
  const auto preferred_row_height =
      std::clamp(37.0F * style.scale, 24.0F, 54.0F);
  const auto row_height =
      line_count == 0U
          ? 0.0F
          : std::min(preferred_row_height,
                     available_rows / static_cast<float>(line_count));
  const auto panel_height =
      std::min(viewport_height - margin * 2.0F,
               header_height + row_height * static_cast<float>(line_count) +
                   footer_height);
  const auto panel_x = (viewport_width - panel_width) * 0.5F;
  const auto panel_y = (viewport_height - panel_height) * 0.5F;
  const auto accent =
      snapshot.victory
          ? HudColor{0.40F, 0.88F, 0.48F, style.opacity}
          : HudColor{0.92F, 0.26F, 0.18F, style.opacity};

  append_hud_rect_top_left(
      vertices, viewport_width, viewport_height, 0.0F, 0.0F, viewport_width,
      viewport_height,
      {0.0F, 0.0F, 0.0F, std::clamp(style.opacity * 0.72F, 0.0F, 0.86F)});

  auto palette = make_warm_panel_palette(accent);
  if (style.high_contrast) {
    palette = {
        {1.0F, 1.0F, 1.0F, style.opacity},
        {0.0F, 0.0F, 0.0F, style.opacity},
        {1.0F, 1.0F, 1.0F, style.opacity * 0.28F},
        {0.0F, 0.0F, 0.0F, style.opacity},
        accent,
    };
  } else {
    palette.frame =
        hud_with_alpha(palette.frame, palette.frame[3] * style.opacity);
    palette.fill =
        hud_with_alpha(palette.fill, palette.fill[3] * style.opacity);
    palette.highlight = hud_with_alpha(
        palette.highlight, palette.highlight[3] * style.opacity);
    palette.shadow =
        hud_with_alpha(palette.shadow, palette.shadow[3] * style.opacity);
    palette.trim =
        hud_with_alpha(palette.trim, palette.trim[3] * style.opacity);
  }
  append_stylized_panel_top_left(
      vertices, viewport_width, viewport_height, panel_x, panel_y, panel_width,
      panel_height, std::clamp(4.0F * style.scale, 2.0F, 7.0F), palette, true);

  const auto title = snapshot.victory ? std::string_view{"VICTOIRE"}
                                      : std::string_view{"DEFAITE"};
  const auto subtitle =
      snapshot.executed
          ? std::string_view{"EXECUTION DU COLOSSE"}
          : (snapshot.victory ? std::string_view{"COLOSSE ABATTU"}
                              : std::string_view{"LE COLOSSE RESISTE"});
  const auto title_pixel = std::clamp(4.0F * style.scale, 2.2F, 5.5F);
  const auto subtitle_pixel = std::clamp(2.0F * style.scale, 1.2F, 3.0F);
  append_issou_hud_text(
      vertices, viewport_width, viewport_height,
      panel_x + panel_width * 0.5F,
      panel_y + std::max(8.0F, header_height * 0.15F), title_pixel, title,
      accent);
  append_issou_hud_text(
      vertices, viewport_width, viewport_height,
      panel_x + panel_width * 0.5F,
      panel_y + std::max(8.0F, header_height * 0.15F) +
          title_pixel * 7.0F + std::max(4.0F, style.scale * 4.0F),
      subtitle_pixel, subtitle,
      {0.94F, 0.94F, 0.92F, style.opacity});

  const auto row_x = panel_x + std::clamp(18.0F * style.scale, 10.0F, 28.0F);
  const auto row_width =
      std::max(1.0F, panel_width - (row_x - panel_x) * 2.0F);
  auto row_y = panel_y + header_height;
  for (std::size_t index = 0U; index < line_count; ++index) {
    const auto &line = snapshot.result_line_view()[index];
    const auto row_alpha = style.opacity * (index % 2U == 0U ? 0.16F : 0.09F);
    append_hud_rounded_rect_top_left(
        vertices, viewport_width, viewport_height, row_x, row_y, row_width,
        std::max(1.0F, row_height - 2.0F),
        std::max(1.0F, row_height * 0.10F),
        {1.0F, 1.0F, 1.0F, row_alpha});
    if (line.highlight) {
      append_hud_rect_top_left(
          vertices, viewport_width, viewport_height, row_x, row_y,
          std::max(2.0F, 3.0F * style.scale),
          std::max(1.0F, row_height - 2.0F), accent);
    }

    const auto pixel_size = std::clamp(row_height / 15.0F, 0.72F, 2.6F);
    const auto text_y =
        row_y + std::max(0.0F, (row_height - pixel_size * 7.0F) * 0.5F);
    const auto label = issou_result_label(line.metric);
    const auto value = format_issou_result_value(line);
    const auto horizontal_padding =
        std::clamp(12.0F * style.scale, 7.0F, 18.0F);
    const auto label_x = row_x + horizontal_padding;
    const auto value_width = measure_pixel_text(value, pixel_size);
    const auto value_x =
        row_x + row_width - horizontal_padding - value_width;
    append_issou_hud_text(
        vertices, viewport_width, viewport_height, label_x, text_y, pixel_size,
        label, {0.92F, 0.92F, 0.90F, style.opacity}, false);
    append_issou_hud_text(
        vertices, viewport_width, viewport_height, value_x, text_y, pixel_size,
        value, line.highlight ? accent
                              : HudColor{0.98F, 0.96F, 0.90F, style.opacity},
        false);
    row_y += row_height;
  }
}

void append_renderer_issou_hud_geometry(
    std::vector<HudVertex> &vertices,
    const RendererIssouHudSnapshot &snapshot, int viewport_width,
    int viewport_height) {
  vertices.clear();
  if (viewport_width <= 0 || viewport_height <= 0 ||
      !snapshot.has_visible_content()) {
    return;
  }
  const auto width = static_cast<float>(std::min(viewport_width, 16'384));
  const auto height = static_cast<float>(std::min(viewport_height, 16'384));
  if (vertices.capacity() < 8'192U) {
    vertices.reserve(8'192U);
  }
  if (snapshot.results_visible) {
    append_issou_results(vertices, width, height, snapshot);
    return;
  }
  for (const auto &element : snapshot.element_view()) {
    append_issou_hud_gauge(vertices, width, height, element);
  }
}

void append_backrooms_flashlight_hud_geometry(
    std::vector<HudVertex> &vertices,
    const BackroomsFlashlightHudView &view,
    int viewport_width,
    int viewport_height) {
  vertices.clear();
  if (!view.visible || viewport_width <= 0 ||
      viewport_height <= 0) {
    return;
  }

  const auto width =
      static_cast<float>(
          std::min(viewport_width, 16'384));
  const auto height =
      static_cast<float>(
          std::min(viewport_height, 16'384));
  const auto ratio =
      std::isfinite(view.battery_ratio)
          ? std::clamp(view.battery_ratio, 0.0F, 1.0F)
          : 0.0F;
  const auto panel_width =
      std::min(244.0F, std::max(180.0F, width - 24.0F));
  constexpr auto panel_height = 72.0F;
  const auto panel_x =
      std::max(12.0F, width - panel_width - 18.0F);
  constexpr auto panel_y = 18.0F;
  constexpr auto padding = 12.0F;
  constexpr auto pixel_size = 2.0F;

  if (vertices.capacity() < 2'048U) {
    vertices.reserve(2'048U);
  }

  const auto accent =
      view.active
          ? HudColor{1.0F, 0.78F, 0.34F, 1.0F}
          : ratio <
                    kBackroomsFlashlightMinimumActivationCharge
                ? HudColor{0.92F, 0.20F, 0.14F, 1.0F}
                : HudColor{0.62F, 0.66F, 0.58F, 1.0F};
  auto palette =
      make_modern_glass_panel_palette(
          accent,
          0.18F);
  palette.fill =
      {0.018F, 0.022F, 0.020F, 0.82F};
  palette.frame =
      {0.055F, 0.065F, 0.058F, 0.94F};
  palette.trim =
      hud_with_alpha(accent, 0.40F);
  append_modern_panel_top_left(
      vertices, width, height,
      panel_x, panel_y,
      panel_width, panel_height,
      3.0F, palette, false);

  const auto draw_text =
      [&](float x, float y,
          std::string_view text,
          const HudColor &color) {
        append_pixel_text(
            vertices, width, height,
            x + pixel_size,
            y + pixel_size,
            pixel_size, text,
            {0.0F, 0.0F, 0.0F, 0.56F},
            false);
        append_pixel_text(
            vertices, width, height,
            x, y, pixel_size, text,
            color, false);
      };

  draw_text(
      panel_x + padding,
      panel_y + 10.0F,
      "[F] LAMPE",
      {0.96F, 0.94F, 0.84F, 0.98F});

  const auto state_label =
      view.active
          ? std::string_view{"ALLUMEE"}
          : ratio <
                    kBackroomsFlashlightMinimumActivationCharge
                ? std::string_view{"VIDE"}
                : ratio < 0.999F
                      ? std::string_view{"RECHARGE"}
                      : std::string_view{"ETEINTE"};
  draw_text(
      panel_x + padding,
      panel_y + 30.0F,
      state_label,
      hud_with_alpha(accent, 0.96F));

  std::array<char, 8> percent_buffer {};
  const auto percent =
      static_cast<int>(
          std::lround(ratio * 100.0F));
  const auto percent_result =
      std::to_chars(
          percent_buffer.data(),
          percent_buffer.data() +
              percent_buffer.size() - 2,
          percent);
  auto percent_end = percent_result.ptr;
  if (percent_result.ec == std::errc{} &&
      percent_end + 2 <=
          percent_buffer.data() +
              percent_buffer.size()) {
    *percent_end++ = ' ';
    *percent_end++ = '%';
  }
  const auto percent_text =
      std::string_view(
          percent_buffer.data(),
          static_cast<std::size_t>(
              percent_end -
              percent_buffer.data()));
  draw_text(
      panel_x + panel_width - padding -
          measure_pixel_text(
              percent_text,
              pixel_size),
      panel_y + 30.0F,
      percent_text,
      {0.92F, 0.92F, 0.86F, 0.94F});

  const auto track_x =
      panel_x + padding;
  const auto track_y =
      panel_y + 52.0F;
  const auto track_width =
      panel_width - padding * 2.0F;
  constexpr auto track_height = 8.0F;
  append_hud_frame_top_left(
      vertices, width, height,
      track_x, track_y,
      track_width, track_height,
      1.0F,
      {0.0F, 0.0F, 0.0F, 0.88F},
      {0.035F, 0.040F, 0.036F, 0.92F});
  const auto fill_width =
      std::max(
          0.0F,
          (track_width - 4.0F) * ratio);
  if (fill_width > 0.0F) {
    append_hud_rect_top_left(
        vertices, width, height,
        track_x + 2.0F,
        track_y + 2.0F,
        fill_width,
        track_height - 4.0F,
        hud_with_alpha(accent, 0.94F));
    append_hud_rect_top_left(
        vertices, width, height,
        track_x + 2.0F,
        track_y + 2.0F,
        fill_width,
        1.0F,
        {1.0F, 0.96F, 0.72F, 0.32F});
  }
}

} // namespace

auto make_backrooms_interference_fixture_cache_key(
    int world_seed, int logical_level,
    const BackroomsJackLightInterferenceView &interference) noexcept
    -> std::optional<BackroomsInterferenceFixtureCacheKey> {
  const auto supported_mode =
      interference.mode == BackroomsJackLightInterferenceMode::Flicker ||
      interference.mode ==
          BackroomsJackLightInterferenceMode::BlackoutPulse;
  if (!interference.active || !supported_mode ||
      !std::isfinite(interference.position.x) ||
      !std::isfinite(interference.position.z) ||
      !std::isfinite(interference.radius) ||
      !std::isfinite(interference.intensity)) {
    return std::nullopt;
  }

  const auto cell_size =
      static_cast<double>(kBackroomsFlickerCacheCellSize);
  const auto anchor_cell_x =
      std::floor(static_cast<double>(interference.position.x) / cell_size);
  const auto anchor_cell_z =
      std::floor(static_cast<double>(interference.position.z) / cell_size);
  constexpr auto minimum_cell =
      static_cast<double>(std::numeric_limits<int>::min());
  constexpr auto maximum_cell =
      static_cast<double>(std::numeric_limits<int>::max());
  if (anchor_cell_x < minimum_cell || anchor_cell_x > maximum_cell ||
      anchor_cell_z < minimum_cell || anchor_cell_z > maximum_cell) {
    return std::nullopt;
  }

  const auto search_radius =
      static_cast<int>(std::ceil(std::clamp(
          interference.radius, 1.0F,
          static_cast<float>(kMaximumBackroomsFixtureSearchRadius))));
  return BackroomsInterferenceFixtureCacheKey{
      .world_seed = world_seed,
      .logical_level = logical_level,
      .anchor_cell_x = static_cast<int>(anchor_cell_x),
      .anchor_cell_z = static_cast<int>(anchor_cell_z),
      .search_radius = search_radius,
      .mode = interference.mode,
  };
}

auto backrooms_blackout_pulse_fallback_intensity(
    float interference_strength) noexcept -> float {
  const auto safe_strength =
      std::isfinite(interference_strength)
          ? std::clamp(interference_strength, 0.0F, 1.0F)
          : 0.0F;
  return std::clamp(
      1.0F -
          safe_strength *
              (1.0F - kBackroomsBlackoutPulseFallbackOutput),
      kBackroomsBlackoutPulseFallbackOutput, 1.0F);
}

auto build_renderer_issou_hud_geometry(
    const RendererIssouHudSnapshot &snapshot, int viewport_width,
    int viewport_height) -> std::vector<HudVertex> {
  std::vector<HudVertex> vertices{};
  append_renderer_issou_hud_geometry(vertices, snapshot, viewport_width,
                                     viewport_height);
  return vertices;
}

auto build_backrooms_flashlight_hud_geometry(
    const BackroomsFlashlightHudView &view,
    int viewport_width,
    int viewport_height) -> std::vector<HudVertex> {
  std::vector<HudVertex> vertices {};
  append_backrooms_flashlight_hud_geometry(
      vertices,
      view,
      viewport_width,
      viewport_height);
  return vertices;
}

auto resolve_ability_feedback_assets(AbilityId ability,
                                     std::string_view visual_id,
                                     std::string_view sfx_id) noexcept
    -> AbilityFeedbackAssetView {
  const auto resolved_visual =
      visual_id.empty() ? resolved_ability_visual_id(ability) : visual_id;
  const auto resolved_sfx =
      sfx_id.empty() ? resolved_ability_sfx_id(ability) : sfx_id;
  return {
      resolved_visual.empty() ? kGenericAbilityVisualId : resolved_visual,
      resolved_sfx.empty() ? kGenericAbilitySfxId : resolved_sfx,
  };
}

auto make_progression_experience_hud_snapshot(
    const PlayerProgressionState &progression,
    std::uint64_t aggregated_experience_gain) noexcept
    -> ProgressionExperienceHudSnapshot {
  const auto normalized = sanitize_player_progression_state(progression);
  const auto threshold = player_experience_for_next_level(normalized.level);
  const auto maximum_level =
      normalized.level >= kPlayerProgressionMaxLevel || threshold == 0ULL;
  const auto ratio =
      maximum_level
          ? 1.0F
          : std::clamp(static_cast<float>(normalized.experience) /
                           static_cast<float>(std::max(threshold, 1ULL)),
                       0.0F, 1.0F);
  return {
      normalized.level,
      normalized.experience,
      maximum_level ? 0ULL : threshold,
      aggregated_experience_gain,
      maximum_level,
      ratio,
  };
}

auto make_progression_ability_hud_snapshot(
    const PlayerBuildState &build,
    const ProgressionRuntimeHudView &runtime) noexcept
    -> ProgressionAbilityHudSnapshot {
  ProgressionAbilityHudSnapshot snapshot{};
  snapshot.visible = runtime.visible;
  const auto energy = player_ability_energy_parameters(build);
  snapshot.maximum_energy = std::isfinite(energy.maximum_energy)
                                ? std::max(energy.maximum_energy, 1.0F)
                                : kPlayerBaseMaximumValEnergy;
  snapshot.current_energy =
      std::isfinite(build.val_energy)
          ? std::clamp(build.val_energy, 0.0F, snapshot.maximum_energy)
          : 0.0F;
  snapshot.global_cooldown_remaining =
      std::isfinite(build.global_cooldown_remaining)
          ? std::max(build.global_cooldown_remaining, 0.0F)
          : 0.0F;
  snapshot.active_duration_remaining =
      std::isfinite(runtime.active_duration_remaining)
          ? std::clamp(runtime.active_duration_remaining, 0.0F, 86'400.0F)
          : 0.0F;
  snapshot.wind_blade_armed = runtime.wind_blade_armed;
  snapshot.wind_dodge_ready = runtime.wind_dodge_ready;
  snapshot.iron_guard_active = runtime.iron_guard_active;
  snapshot.active_footmen = std::min<std::uint8_t>(runtime.active_footmen, 8U);

  auto focused = ability_id_is_valid(runtime.focused_ability)
                     ? runtime.focused_ability
                     : AbilityId::None;
  if (!ability_id_is_valid(focused)) {
    for (const auto equipped : build.equipped_abilities) {
      if (ability_id_is_valid(equipped)) {
        focused = equipped;
        break;
      }
    }
  }
  snapshot.ability = focused;
  snapshot.feedback_assets = resolve_ability_feedback_assets(
      focused, runtime.visual_id, runtime.sfx_id);
  if (!ability_id_is_valid(focused)) {
    snapshot.display_name = "AUCUN SORT";
    return snapshot;
  }

  snapshot.display_name = progression_ability_display_name(focused);
  const auto rank = player_ability_rank(build, focused);
  const auto effective_rank = std::max<std::uint8_t>(rank, 1U);
  if (const auto *rank_definition =
          ability_rank_definition(focused, effective_rank);
      rank_definition != nullptr) {
    snapshot.energy_cost = std::isfinite(rank_definition->energy_cost)
                               ? std::max(rank_definition->energy_cost, 0.0F)
                               : 0.0F;
  }
  snapshot.energy_insufficient =
      snapshot.current_energy + 1.0e-4F < snapshot.energy_cost;
  const auto index = ability_index(focused);
  snapshot.cooldown_remaining =
      index < build.cooldowns_remaining.size() &&
              std::isfinite(build.cooldowns_remaining[index])
          ? std::max(build.cooldowns_remaining[index], 0.0F)
          : 0.0F;
  const auto *definition = ability_definition(focused);
  snapshot.maximum_charges =
      definition != nullptr ? definition->maximum_charges : 0U;
  snapshot.charges =
      index < build.charges.size()
          ? std::min(build.charges[index], snapshot.maximum_charges)
          : 0U;
  return snapshot;
}

auto make_progression_ability_hud_layout(int viewport_width,
                                         int viewport_height) noexcept
    -> ProgressionAbilityHudLayout {
  ProgressionAbilityHudLayout layout{};
  if (viewport_width <= 0 || viewport_height <= 0) {
    return layout;
  }
  const auto width = static_cast<float>(viewport_width);
  const auto height = static_cast<float>(viewport_height);
  const auto safe_margin =
      std::clamp(std::min(width, height) * 0.025F, 8.0F, 24.0F);
  const auto panel_width = std::min(std::clamp(width * 0.44F, 272.0F, 440.0F),
                                    width - safe_margin * 2.0F);
  const auto panel_height = std::min(126.0F, height - safe_margin * 2.0F);
  const auto preferred_y =
      safe_margin + std::clamp(height * 0.085F, 54.0F, 82.0F);
  const auto panel_y =
      std::min(preferred_y, height - safe_margin - panel_height);
  layout.panel = {
      width - safe_margin - panel_width,
      std::max(safe_margin, panel_y),
      panel_width,
      panel_height,
  };
  const auto inset = 12.0F;
  const auto row_width = std::max(0.0F, panel_width - inset * 2.0F);
  layout.energy = {
      layout.panel.x + inset,
      layout.panel.y + 10.0F,
      row_width,
      12.0F,
  };
  layout.ability = {
      layout.panel.x + inset,
      layout.energy.bottom() + 7.0F,
      row_width,
      20.0F,
  };
  layout.timers = {
      layout.panel.x + inset,
      layout.ability.bottom() + 5.0F,
      row_width,
      20.0F,
  };
  layout.effects = {
      layout.panel.x + inset,
      layout.timers.bottom() + 5.0F,
      row_width,
      std::max(0.0F,
               layout.panel.bottom() - inset - layout.timers.bottom() - 5.0F),
  };
  return layout;
}

Renderer::~Renderer() { shutdown(); }

void Renderer::set_progression_hud(
    const ProgressionMenuViewModel &menu, const PlayerBuildState &build,
    const ProgressionRuntimeHudView &runtime,
    const ConstructionPlanEditorViewModel &construction_plan) noexcept {
  progression_menu_view_ = menu;
  progression_build_view_ = build;
  progression_runtime_hud_view_ = runtime;
  progression_runtime_hud_view_.active_footmen =
      std::min<std::uint8_t>(runtime.active_footmen, 8U);
  progression_runtime_hud_view_.active_duration_remaining =
      std::isfinite(runtime.active_duration_remaining)
          ? std::clamp(runtime.active_duration_remaining, 0.0F, 86'400.0F)
          : 0.0F;
  const auto feedback = resolve_ability_feedback_assets(
      runtime.focused_ability, runtime.visual_id, runtime.sfx_id);
  progression_runtime_hud_view_.visual_id = feedback.visual_id;
  progression_runtime_hud_view_.sfx_id = feedback.sfx_id;
  construction_plan_view_ = construction_plan;
}

void Renderer::set_legendary_presentation(
    const RendererLegendaryPresentationFrame &frame) {
  // Je remplace entièrement la soumission précédente : deux simulations
  // successives ne peuvent jamais cumuler une foule, un boss ou un monstre.
  clear_legendary_presentation();
  legendary_presentation_stats_ = {};

  std::vector<CreaturePartInstance> viewmodel_parts{};
  std::vector<CreaturePartInstance> world_parts{};
  std::vector<VisualEntityContext> world_contexts{};
  RendererLeviathanVisualEventSnapshot visual_event_snapshot{};
  RendererIssouHudSnapshot hud_snapshot{};
  viewmodel_parts.reserve(kRendererMaximumLeviathanWeaponParts);
  world_parts.reserve(kRendererMaximumLegendaryWorldParts);
  world_contexts.reserve(kRendererMaximumLegendaryWorldParts);

  auto stats = RendererLegendaryPresentationStats{};
  const auto append_world = [&](const CreaturePartInstance &source,
                                VisualEntityContext context) -> bool {
    if (world_parts.size() >= kRendererMaximumLegendaryWorldParts) {
      return false;
    }
    const auto sanitized = sanitize_legendary_part(source);
    if (!sanitized.has_value()) {
      return false;
    }
    world_parts.push_back(*sanitized);
    world_contexts.push_back(context);
    return true;
  };
  const auto append_generated_source =
      [&](std::span<const CreaturePartInstance> source,
          VisualEntityContext context) -> std::size_t {
    auto appended = std::size_t{0U};
    for (const auto &part : source) {
      if (append_world(part, context)) {
        ++appended;
      } else {
        add_legendary_dropped_submissions(stats, 1U);
      }
    }
    return appended;
  };

  const auto weapon_count = std::min(frame.first_person_weapon_parts.size(),
                                     kRendererMaximumLeviathanWeaponParts);
  add_legendary_dropped_submissions(
      stats, frame.first_person_weapon_parts.size() - weapon_count);
  for (std::size_t index = 0U; index < weapon_count; ++index) {
    const auto converted =
        convert_leviathan_weapon_part(frame.first_person_weapon_parts[index]);
    if (!converted.has_value()) {
      add_legendary_dropped_submissions(stats, 1U);
      continue;
    }
    viewmodel_parts.push_back(*converted);
    ++stats.accepted_weapon_parts;
  }

  // Je donne la priorité aux acteurs de combat avant la foule : un budget
  // saturé ne doit jamais faire disparaître le Colosse ou une hitbox lisible.
  const auto colossus_count = std::min(frame.chained_colossus_parts.size(),
                                       kRendererMaximumChainedColossusParts);
  add_legendary_dropped_submissions(stats, frame.chained_colossus_parts.size() -
                                               colossus_count);
  for (std::size_t index = 0U; index < colossus_count; ++index) {
    if (append_world(frame.chained_colossus_parts[index].geometry,
                     VisualEntityContext::Creature)) {
      ++stats.accepted_colossus_parts;
    } else {
      add_legendary_dropped_submissions(stats, 1U);
    }
  }

  const auto enemy_count = std::min(frame.legendary_enemies.size(),
                                    kRendererMaximumLegendaryEnemySnapshots);
  add_legendary_dropped_submissions(stats, frame.legendary_enemies.size() -
                                               enemy_count);
  for (std::size_t index = 0U; index < enemy_count; ++index) {
    const auto &enemy = frame.legendary_enemies[index];
    if (!valid_legendary_enemy_archetype(enemy.archetype) ||
        !valid_legendary_enemy_behavior(enemy.behavior)) {
      add_legendary_dropped_submissions(stats, 1U);
      continue;
    }
    const auto parts = build_legendary_enemy_render_parts(enemy);
    if (!enemy.alive) {
      // Je considère un cadavre déjà retiré comme une entrée valide,
      // mais il ne consomme aucun budget de rendu.
      ++stats.accepted_enemy_snapshots;
      continue;
    }
    if (parts.empty()) {
      add_legendary_dropped_submissions(stats, 1U);
      continue;
    }
    const auto appended =
        append_generated_source(parts, VisualEntityContext::Creature);
    if (appended > 0U) {
      ++stats.accepted_enemy_snapshots;
    } else {
      add_legendary_dropped_submissions(stats, 1U);
    }
  }

  if (frame.sea_leviathan.has_value()) {
    const auto &leviathan = *frame.sea_leviathan;
    if (!valid_sea_leviathan_attack(leviathan.active_attack)) {
      add_legendary_dropped_submissions(stats, 1U);
    } else if (!leviathan.active) {
      // Une rencontre dormante est une soumission valide et invisible.
      stats.accepted_sea_leviathans = 1U;
    } else {
      const auto parts = build_sea_leviathan_render_parts(leviathan);
      const auto appended =
          append_generated_source(parts, VisualEntityContext::Creature);
      if (!parts.empty() && appended > 0U) {
        stats.accepted_sea_leviathans = 1U;
      } else {
        add_legendary_dropped_submissions(stats, 1U);
      }
    }
  }

  const auto visual_event_count =
      std::min(frame.leviathan_visual_events.size(),
               kRendererMaximumLeviathanVisualEvents);
  add_legendary_dropped_submissions(
      stats, frame.leviathan_visual_events.size() - visual_event_count);
  for (std::size_t index = 0U; index < visual_event_count; ++index) {
    const auto sanitized =
        sanitize_leviathan_visual_event(frame.leviathan_visual_events[index]);
    if (!sanitized.has_value()) {
      add_legendary_dropped_submissions(stats, 1U);
      continue;
    }

    visual_event_snapshot
        .events[visual_event_snapshot.event_count++] = *sanitized;
    ++stats.accepted_visual_events;
    if (sanitized->kind == LeviathanVisualEventKind::CameraImpulse) {
      ++stats.accepted_camera_impulses;
    } else if (sanitized->kind ==
               LeviathanVisualEventKind::VisualHitStop) {
      ++stats.accepted_visual_hit_stops;
    }

    const auto parts = build_leviathan_visual_event_parts(*sanitized);
    if (parts.empty()) {
      continue;
    }
    const auto appended =
        append_generated_source(parts, VisualEntityContext::Generic);
    stats.staged_visual_event_parts += appended;
    if (appended > 0U) {
      ++stats.rendered_visual_events;
    }
  }

  const auto decor_count =
      std::min(frame.issou_decor.size(), kRendererMaximumIssouDecorInstances);
  add_legendary_dropped_submissions(stats,
                                    frame.issou_decor.size() - decor_count);
  for (std::size_t index = 0U; index < decor_count; ++index) {
    const auto converted = convert_issou_decor(frame.issou_decor[index]);
    if (!converted.has_value() ||
        !append_world(*converted, VisualEntityContext::Generic)) {
      add_legendary_dropped_submissions(stats, 1U);
      continue;
    }
    ++stats.accepted_decor_instances;
  }

  const auto trace_count = std::min(frame.colossus_blood_traces.size(),
                                    kRendererMaximumColossusBloodTraces);
  add_legendary_dropped_submissions(stats, frame.colossus_blood_traces.size() -
                                               trace_count);
  for (std::size_t index = 0U; index < trace_count; ++index) {
    const auto converted =
        convert_colossus_blood_trace(frame.colossus_blood_traces[index]);
    if (!converted.has_value() ||
        !append_world(*converted, VisualEntityContext::Generic)) {
      add_legendary_dropped_submissions(stats, 1U);
      continue;
    }
    ++stats.accepted_blood_traces;
  }

  const auto crowd_count =
      std::min(frame.issou_crowd.size(), kRendererMaximumIssouCrowdInstances);
  add_legendary_dropped_submissions(stats,
                                    frame.issou_crowd.size() - crowd_count);
  for (std::size_t index = 0U; index < crowd_count; ++index) {
    const auto &crowd_member = frame.issou_crowd[index];
    if (!legendary_finite_vec3(crowd_member.position) ||
        !std::isfinite(crowd_member.yaw_radians) ||
        !std::isfinite(crowd_member.animation_phase) ||
        !std::isfinite(crowd_member.reaction_amount) ||
        !legendary_finite_vec4(crowd_member.cloth_color) ||
        !valid_issou_crowd_lod(crowd_member.lod)) {
      add_legendary_dropped_submissions(stats, 1U);
      continue;
    }
    if (crowd_member.lod == IssouCrowdLod::Culled) {
      ++stats.accepted_crowd_instances;
      continue;
    }
    const auto parts = build_issou_crowd_member_parts(crowd_member);
    const auto appended =
        append_generated_source(parts, VisualEntityContext::Creature);
    if (!parts.empty() && appended > 0U) {
      ++stats.accepted_crowd_instances;
    } else {
      add_legendary_dropped_submissions(stats, 1U);
    }
  }

  const auto hud_count =
      std::min(frame.issou_hud.size(), kRendererMaximumIssouHudElements);
  add_legendary_dropped_submissions(stats,
                                    frame.issou_hud.size() - hud_count);
  std::array<bool, kRendererMaximumIssouHudElements> seen_hud_kinds{};
  for (std::size_t index = 0U; index < hud_count; ++index) {
    const auto sanitized = sanitize_issou_hud_element(frame.issou_hud[index]);
    if (!sanitized.has_value()) {
      add_legendary_dropped_submissions(stats, 1U);
      continue;
    }
    const auto kind_index = static_cast<std::size_t>(sanitized->kind);
    if (kind_index >= seen_hud_kinds.size() || seen_hud_kinds[kind_index]) {
      add_legendary_dropped_submissions(stats, 1U);
      continue;
    }
    seen_hud_kinds[kind_index] = true;
    hud_snapshot.elements[hud_snapshot.element_count++] = *sanitized;
    ++stats.accepted_hud_elements;
  }

  if (frame.issou_results.has_value()) {
    const auto &results = *frame.issou_results;
    hud_snapshot.results_visible = true;
    hud_snapshot.victory = results.victory;
    hud_snapshot.executed = results.executed;
    stats.accepted_result_presentations = 1U;

    const auto result_count =
        std::min(results.lines.size(), kRendererMaximumIssouResultLines);
    add_legendary_dropped_submissions(stats,
                                      results.lines.size() - result_count);
    std::array<bool, kRendererMaximumIssouResultLines> seen_metrics{};
    for (std::size_t index = 0U; index < result_count; ++index) {
      const auto sanitized = sanitize_issou_result_line(results.lines[index]);
      if (!sanitized.has_value()) {
        add_legendary_dropped_submissions(stats, 1U);
        continue;
      }
      const auto metric_index =
          static_cast<std::size_t>(sanitized->metric);
      if (metric_index >= seen_metrics.size() || seen_metrics[metric_index]) {
        add_legendary_dropped_submissions(stats, 1U);
        continue;
      }
      seen_metrics[metric_index] = true;
      hud_snapshot.result_lines[hud_snapshot.result_line_count++] = *sanitized;
      ++stats.accepted_result_lines;
    }
  }

  stats.staged_viewmodel_parts = viewmodel_parts.size();
  stats.staged_world_parts = world_parts.size();
  stats.staged_visual_events = visual_event_snapshot.event_count;
  stats.staged_hud_elements = hud_snapshot.element_count;
  stats.staged_result_lines = hud_snapshot.result_line_count;
  stats.pending = !viewmodel_parts.empty() || !world_parts.empty() ||
                  visual_event_snapshot.event_count > 0U ||
                  hud_snapshot.has_visible_content();

  legendary_viewmodel_parts_ = std::move(viewmodel_parts);
  legendary_world_parts_ = std::move(world_parts);
  legendary_world_part_contexts_ = std::move(world_contexts);
  leviathan_visual_event_snapshot_ = visual_event_snapshot;
  issou_hud_snapshot_ = hud_snapshot;
  legendary_presentation_stats_ = stats;
}

void Renderer::clear_legendary_presentation() noexcept {
  legendary_viewmodel_parts_.clear();
  legendary_world_parts_.clear();
  legendary_world_part_contexts_.clear();
  leviathan_visual_event_snapshot_ = {};
  issou_hud_snapshot_ = {};
  issou_hud_vertices_scratch_.clear();
  legendary_presentation_stats_.staged_viewmodel_parts = 0U;
  legendary_presentation_stats_.staged_world_parts = 0U;
  legendary_presentation_stats_.staged_visual_events = 0U;
  legendary_presentation_stats_.staged_visual_event_parts = 0U;
  legendary_presentation_stats_.staged_hud_elements = 0U;
  legendary_presentation_stats_.staged_result_lines = 0U;
  legendary_presentation_stats_.pending = false;
}

void Renderer::set_backrooms_jack(
    const BackroomsJackRenderView &render,
    const BackroomsJackLightInterferenceView &light_interference) {
  backrooms_jack_render_view_ = render;
  backrooms_jack_light_interference_ = light_interference;
  if (!render.visible ||
      !std::isfinite(render.position.x) ||
      !std::isfinite(render.position.y) ||
      !std::isfinite(render.position.z)) {
    backrooms_jack_parts_.clear();
    return;
  }
  backrooms_jack_parts_ =
      build_backrooms_jack_visual_parts({
          .position = render.position,
          .yaw_radians =
              backrooms_jack_visual_body_yaw_radians(
                  render.body_yaw_degrees),
          .animation_time =
              render.animation_time_seconds,
          .hunch_ratio = render.hunch_ratio,
          .head_scan_radians =
              backrooms_jack_visual_head_yaw_radians(
                  render.head_yaw_degrees),
          .motion_amount = render.motion_amount,
          .chasing = render.chasing,
          .jumpscare = render.jumpscare,
          .sky_light =
              std::clamp(
                  render.sky_light,
                  0.0F,
                  1.0F),
          .block_light =
              std::clamp(
                  render.block_light,
                  0.0F,
                  1.0F),
      });
}

void Renderer::set_backrooms_marlow(
    const BackroomsMarlowUpdateResult &result,
    float animation_time_seconds,
    float sky_light,
    float block_light) {
  backrooms_marlow_result_ = result;
  backrooms_marlow_parts_.clear();
  const auto safe_sky = std::clamp(
      std::isfinite(sky_light) ? sky_light : 0.0F, 0.0F, 1.0F);
  const auto safe_block = std::clamp(
      std::isfinite(block_light) ? block_light : 0.0F, 0.0F, 1.0F);
  const auto safe_time = std::isfinite(animation_time_seconds)
                             ? animation_time_seconds
                             : 0.0F;

  if (result.render.visible &&
      std::isfinite(result.render.position.x) &&
      std::isfinite(result.render.position.y) &&
      std::isfinite(result.render.position.z)) {
    const auto phase = result.render.phase;
    auto body_parts = build_backrooms_marlow_visual_parts({
        .position = result.render.position,
        .yaw_radians = backrooms_marlow_visual_body_yaw_radians(
            result.render.body_yaw_degrees),
        .animation_time = safe_time,
        .motion_amount =
            phase == BackroomsMarlowPhase::Emerging ||
                    phase == BackroomsMarlowPhase::Blocking ||
                    phase == BackroomsMarlowPhase::Dragging
                ? 1.0F
                : 0.18F,
        .submersion_ratio =
            backrooms_marlow_visual_submersion_ratio(
                result.render.immersion_ratio,
                result.render.reveal_amount),
        .peek_amount =
            phase == BackroomsMarlowPhase::CornerPeek
                ? std::clamp(result.render.peek_side, -1.0F, 1.0F) * 0.92F
                : 0.0F,
        .head_scan_radians =
            std::sin(safe_time * 0.47F) * 0.10F,
        .reach_amount =
            phase == BackroomsMarlowPhase::Dragging ||
                    phase == BackroomsMarlowPhase::Drowning ||
                    phase == BackroomsMarlowPhase::Screamer
                ? 1.0F
                : 0.0F,
        .jumpscare = phase == BackroomsMarlowPhase::Screamer,
        .sky_light = safe_sky,
        .block_light = safe_block,
    });
    backrooms_marlow_visual_anchor_ = result.render.position;
    backrooms_marlow_parts_.insert(
        backrooms_marlow_parts_.end(),
        body_parts.begin(),
        body_parts.end());
  }
  if (result.buoy.visible &&
      std::isfinite(result.buoy.position.x) &&
      std::isfinite(result.buoy.position.y) &&
      std::isfinite(result.buoy.position.z)) {
    auto buoy_parts = build_backrooms_marlow_buoy_visual_parts({
        .water_surface_position = result.buoy.position,
        .yaw_radians = safe_time * 0.19F,
        .animation_time = safe_time,
        .disturbance = result.buoy.warning_amount,
        .sky_light = safe_sky,
        .block_light = safe_block,
    });
    if (!result.render.visible) {
      backrooms_marlow_visual_anchor_ = result.buoy.position;
    }
    backrooms_marlow_parts_.insert(
        backrooms_marlow_parts_.end(),
        buoy_parts.begin(),
        buoy_parts.end());
  }
}

auto Renderer::backrooms_terminal_fog_snapshot() const noexcept
    -> BackroomsTerminalFogSnapshot {
  // Je retourne une copie : le gameplay peut lire la derniere frontiere GPU
  // engagee, mais il ne peut jamais modifier l'etat de securite du renderer.
  return backrooms_terminal_fog_snapshot_;
}

auto Renderer::legendary_presentation_stats() const noexcept
    -> const RendererLegendaryPresentationStats & {
  return legendary_presentation_stats_;
}

auto Renderer::issou_hud_snapshot() const noexcept
    -> const RendererIssouHudSnapshot & {
  return issou_hud_snapshot_;
}

auto Renderer::leviathan_visual_event_snapshot() const noexcept
    -> const RendererLeviathanVisualEventSnapshot & {
  return leviathan_visual_event_snapshot_;
}

auto Renderer::initialize(const RendererOptions &options) -> bool {
  last_initialization_error_.clear();
  auto normalized_options = options;
  normalized_options.shadow_map_size =
      std::max(normalized_options.shadow_map_size, 1);
  normalized_options.viewmodel_fov_degrees =
      glm::clamp(normalized_options.viewmodel_fov_degrees, 35.0F, 100.0F);

  if (initialized_) {
    const auto visual_pipeline_changed =
        options_.visual_pipeline != normalized_options.visual_pipeline;
    const auto shadow_resource_changed =
        options_.shadows_enabled != normalized_options.shadows_enabled ||
        options_.shadow_map_size != normalized_options.shadow_map_size;
    const auto target_format_changed =
        options_.quality != normalized_options.quality;
    const auto post_process_disabled = options_.post_process_enabled &&
                                       !normalized_options.post_process_enabled;
    options_ = normalized_options;
    if (target_format_changed) {
      adaptive_quality_controller_.reset(options_.quality, 1, 1);
      active_quality_settings_ =
          adaptive_quality_controller_.settings(options_.quality, 1, 1);
      adaptive_gpu_sample_consumed_ = false;
      pending_cpu_frame_time_ms_ = 0.0;
      pending_cpu_frame_time_valid_ = false;
    }

    try {
      // Je ne reconstruis que les ressources réellement dépendantes des
      // options.
      if (shadow_resource_changed) {
        destroy_shadow_map();
        create_shadow_map();
      }
      if (target_format_changed) {
        destroy_water_scene_targets();
        destroy_post_process_targets();
      } else if (post_process_disabled) {
        destroy_glow_targets();
      }
      if (visual_pipeline_changed) {
        if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
          if (!create_modern_material_textures()) {
            throw std::runtime_error(
                last_initialization_error_.empty()
                    ? "Unable to load the modern visual material pack"
                    : last_initialization_error_);
          }
          if (!create_model_icon_texture()) {
            throw std::runtime_error(
                last_initialization_error_.empty()
                    ? "Unable to load the modern model icon atlas"
                    : last_initialization_error_);
          }
          if (!create_msdf_font_texture()) {
            throw std::runtime_error(
                last_initialization_error_.empty()
                    ? "Unable to load the modern UI font atlas"
                    : last_initialization_error_);
          }
        } else {
          destroy_modern_material_textures();
          destroy_model_icon_texture();
          destroy_msdf_font_texture();
        }

        // Je reconstruis les atlas colores lors d'un changement de
        // pipeline : le rendu moderne les decode en sRGB, tandis que
        // LegacyVoxel conserve exactement son ancien format lineaire.
        if (creature_atlas_texture_ != 0) {
          glDeleteTextures(1, &creature_atlas_texture_);
          creature_atlas_texture_ = 0;
        }
        if (player_atlas_texture_ != 0) {
          glDeleteTextures(1, &player_atlas_texture_);
          player_atlas_texture_ = 0;
        }
        create_creature_atlas_texture();
        create_player_atlas_texture();

        // Les caches incorporent les UV et le mode de texture du
        // pipeline actif : je les invalide sans modifier leurs états.
        hotbar_cache_.valid = false;
        inventory_cache_.valid = false;
        death_cache_.valid = false;
        pause_cache_.valid = false;
        main_menu_cache_.valid = false;
        save_slot_cache_.valid = false;
        options_cache_.valid = false;
        confirm_cache_.valid = false;
        maritime_cache_.valid = false;

        // Je reconstruis les gabarits partagés sans toucher aux rigs,
        // sockets ni instances qui portent le gameplay.
        glDeleteBuffers(1, &viewmodel_instance_vbo_);
        glDeleteVertexArrays(1, &viewmodel_vao_);
        glDeleteBuffers(1, &creature_instance_vbo_);
        glDeleteBuffers(1, &creature_ebo_);
        glDeleteBuffers(1, &creature_vbo_);
        glDeleteVertexArrays(1, &creature_vao_);
        glDeleteBuffers(1, &item_drop_instance_vbo_);
        glDeleteBuffers(1, &item_drop_ebo_);
        glDeleteBuffers(1, &item_drop_vbo_);
        glDeleteVertexArrays(1, &item_drop_vao_);
        viewmodel_instance_vbo_ = 0;
        viewmodel_vao_ = 0;
        creature_instance_vbo_ = 0;
        creature_ebo_ = 0;
        creature_vbo_ = 0;
        creature_vao_ = 0;
        item_drop_instance_vbo_ = 0;
        item_drop_ebo_ = 0;
        item_drop_vbo_ = 0;
        item_drop_vao_ = 0;
        create_creature_geometry();
        create_item_drop_geometry();

        // Je force aussi le navire a changer de representation : sa
        // revision logique ne varie pas lors d'un basculement visuel.
        for (auto &ship_gpu_mesh : ship_gpu_meshes_) {
          destroy_gpu_mesh(ship_gpu_mesh);
        }
        ship_mesh_cache_.reset();
        active_ship_lod_ = StylizedShipLod::Near;
      }
      return true;
    } catch (const std::exception &exception) {
      last_initialization_error_ = exception.what();
      return false;
    } catch (...) {
      last_initialization_error_ =
          "Unknown exception while reconfiguring renderer resources";
      return false;
    }
  }

  options_ = normalized_options;
  try {
    adaptive_quality_controller_.reset(options_.quality, 1, 1);
    active_quality_settings_ =
        adaptive_quality_controller_.settings(options_.quality, 1, 1);
    gl_api_ready_ = true;
    create_programs();
    create_atlas_texture();
    if (!create_backrooms_jack_screamer_texture()) {
      throw std::runtime_error(
          last_initialization_error_.empty()
              ? "Unable to load Jack the pirate screamer texture"
              : last_initialization_error_);
    }
    if (!create_backrooms_marlow_screamer_texture()) {
      throw std::runtime_error(
          last_initialization_error_.empty()
              ? "Unable to load Marlow the drowned screamer texture"
              : last_initialization_error_);
    }
    if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
      if (!create_modern_material_textures()) {
        throw std::runtime_error(
            last_initialization_error_.empty()
                ? "Unable to load the modern visual material pack"
                : last_initialization_error_);
      }
      if (!create_model_icon_texture()) {
        throw std::runtime_error(
            last_initialization_error_.empty()
                ? "Unable to load the modern model icon atlas"
                : last_initialization_error_);
      }
      if (!create_msdf_font_texture()) {
        throw std::runtime_error(last_initialization_error_.empty()
                                     ? "Unable to load the modern UI font atlas"
                                     : last_initialization_error_);
      }
    }
    create_accent_texture();
    create_creature_atlas_texture();
    create_player_atlas_texture();
    create_shadow_map();
    create_scene_sampler_fallback_textures();
    create_creature_geometry();
    create_item_drop_geometry();
    create_precipitation_geometry();
    create_old_guard_effect_geometry();
    create_sea_horizon_geometry();
    create_hud_geometry();
    create_screen_quad_geometry();
    create_crosshair_geometry();
    create_gpu_timers();
    initialized_ = true;
    return true;
  } catch (const std::exception &exception) {
    last_initialization_error_ = exception.what();
    shutdown();
    return false;
  } catch (...) {
    last_initialization_error_ =
        "Unknown exception while initializing renderer resources";
    shutdown();
    return false;
  }
}

void Renderer::shutdown() {
  if (gl_api_ready_) {
    destroy_gpu_timers();
    reset_world_resources();
    for (auto &ship_gpu_mesh : ship_gpu_meshes_) {
      destroy_gpu_mesh(ship_gpu_mesh);
    }
    destroy_sea_horizon_geometry();

    destroy_water_scene_targets();
    destroy_post_process_targets();
    destroy_modern_material_textures();
    destroy_model_icon_texture();
    destroy_msdf_font_texture();
    destroy_backrooms_jack_screamer_texture();
    destroy_backrooms_marlow_screamer_texture();

    if (screen_quad_vao_ != 0) {
      glDeleteVertexArrays(1, &screen_quad_vao_);
    }

    if (crosshair_vbo_ != 0) {
      glDeleteBuffers(1, &crosshair_vbo_);
    }
    if (crosshair_vao_ != 0) {
      glDeleteVertexArrays(1, &crosshair_vao_);
    }
    if (hud_vbo_ != 0) {
      glDeleteBuffers(1, &hud_vbo_);
    }
    if (hud_vao_ != 0) {
      glDeleteVertexArrays(1, &hud_vao_);
    }
    if (atlas_texture_ != 0) {
      glDeleteTextures(1, &atlas_texture_);
    }
    if (accent_texture_ != 0) {
      glDeleteTextures(1, &accent_texture_);
    }
    if (creature_atlas_texture_ != 0) {
      glDeleteTextures(1, &creature_atlas_texture_);
    }
    if (player_atlas_texture_ != 0) {
      glDeleteTextures(1, &player_atlas_texture_);
    }
    destroy_shadow_map();
    if (scene_fallback_depth_texture_ != 0) {
      glDeleteTextures(1, &scene_fallback_depth_texture_);
    }
    if (scene_fallback_color_texture_ != 0) {
      glDeleteTextures(1, &scene_fallback_color_texture_);
    }
    if (viewmodel_instance_vbo_ != 0) {
      glDeleteBuffers(1, &viewmodel_instance_vbo_);
    }
    if (viewmodel_vao_ != 0) {
      glDeleteVertexArrays(1, &viewmodel_vao_);
    }
    if (creature_instance_vbo_ != 0) {
      glDeleteBuffers(1, &creature_instance_vbo_);
    }
    if (creature_ebo_ != 0) {
      glDeleteBuffers(1, &creature_ebo_);
    }
    if (creature_vbo_ != 0) {
      glDeleteBuffers(1, &creature_vbo_);
    }
    if (creature_vao_ != 0) {
      glDeleteVertexArrays(1, &creature_vao_);
    }
    if (item_drop_instance_vbo_ != 0) {
      glDeleteBuffers(1, &item_drop_instance_vbo_);
    }
    if (item_drop_ebo_ != 0) {
      glDeleteBuffers(1, &item_drop_ebo_);
    }
    if (item_drop_vbo_ != 0) {
      glDeleteBuffers(1, &item_drop_vbo_);
    }
    if (item_drop_vao_ != 0) {
      glDeleteVertexArrays(1, &item_drop_vao_);
    }
    if (precipitation_instance_vbo_ != 0) {
      glDeleteBuffers(1, &precipitation_instance_vbo_);
    }
    if (precipitation_vbo_ != 0) {
      glDeleteBuffers(1, &precipitation_vbo_);
    }
    if (precipitation_vao_ != 0) {
      glDeleteVertexArrays(1, &precipitation_vao_);
    }
    if (old_guard_effect_instance_vbo_ != 0) {
      glDeleteBuffers(1, &old_guard_effect_instance_vbo_);
    }
    if (old_guard_effect_vbo_ != 0) {
      glDeleteBuffers(1, &old_guard_effect_vbo_);
    }
    if (old_guard_effect_vao_ != 0) {
      glDeleteVertexArrays(1, &old_guard_effect_vao_);
    }
    if (world_program_ != 0) {
      glDeleteProgram(world_program_);
    }
    if (modern_water_program_ != 0) {
      glDeleteProgram(modern_water_program_);
    }
    if (modern_terrain_program_ != 0) {
      glDeleteProgram(modern_terrain_program_);
    }
    if (modern_architecture_program_ != 0) {
      glDeleteProgram(modern_architecture_program_);
    }
    if (modern_terrain_shadow_program_ != 0) {
      glDeleteProgram(modern_terrain_shadow_program_);
    }
    if (modern_ship_program_ != 0) {
      glDeleteProgram(modern_ship_program_);
    }
    if (modern_ship_shadow_program_ != 0) {
      glDeleteProgram(modern_ship_shadow_program_);
    }
    if (item_drop_program_ != 0) {
      glDeleteProgram(item_drop_program_);
    }
    if (precipitation_program_ != 0) {
      glDeleteProgram(precipitation_program_);
    }
    if (old_guard_effect_program_ != 0) {
      glDeleteProgram(old_guard_effect_program_);
    }
    if (creature_program_ != 0) {
      glDeleteProgram(creature_program_);
    }
    if (creature_shadow_program_ != 0) {
      glDeleteProgram(creature_shadow_program_);
    }
    if (shadow_program_ != 0) {
      glDeleteProgram(shadow_program_);
    }
    if (hud_program_ != 0) {
      glDeleteProgram(hud_program_);
    }
    if (crosshair_program_ != 0) {
      glDeleteProgram(crosshair_program_);
    }
    if (sky_program_ != 0) {
      glDeleteProgram(sky_program_);
    }
    if (sea_horizon_program_ != 0) {
      glDeleteProgram(sea_horizon_program_);
    }
    if (post_process_program_ != 0) {
      glDeleteProgram(post_process_program_);
    }
    if (glow_extract_program_ != 0) {
      glDeleteProgram(glow_extract_program_);
    }
    if (glow_blur_program_ != 0) {
      glDeleteProgram(glow_blur_program_);
    }
    if (menu_background_program_ != 0) {
      glDeleteProgram(menu_background_program_);
    }
  }

  gpu_meshes_.clear();
  world_resource_reset_queue_.clear();
  block_break_overlay_mesh_ = {};
  ship_gpu_meshes_ = {};
  marine_decor_gpu_mesh_ = {};
  ocean_life_gpu_mesh_ = {};
  visible_chunks_cache_.clear();
  shadow_chunks_cache_.clear();
  visible_creatures_cache_.clear();
  visible_crew_cache_.clear();
  visible_old_guard_cache_.clear();
  creature_parts_scratch_.clear();
  creature_part_contexts_scratch_.clear();
  backrooms_jack_parts_.clear();
  backrooms_marlow_parts_.clear();
  clear_legendary_presentation();
  legendary_presentation_stats_ = {};
  for (auto &batch : visual_entity_batches_) {
    batch.clear();
  }
  screen_quad_vao_ = 0;
  crosshair_vbo_ = 0;
  crosshair_vao_ = 0;
  hud_vbo_ = 0;
  hud_vao_ = 0;
  atlas_texture_ = 0;
  msdf_font_texture_ = 0;
  model_icon_texture_ = 0;
  backrooms_jack_screamer_texture_ = 0;
  backrooms_marlow_screamer_texture_ = 0;
  modern_material_albedo_texture_ = 0;
  modern_material_normal_height_texture_ = 0;
  modern_material_orm_emission_texture_ = 0;
  accent_texture_ = 0;
  creature_atlas_texture_ = 0;
  player_atlas_texture_ = 0;
  shadow_map_ = 0;
  shadow_framebuffer_ = 0;
  shadow_map_far_ = 0;
  shadow_framebuffer_far_ = 0;
  scene_fallback_color_texture_ = 0;
  scene_fallback_depth_texture_ = 0;
  water_scene_framebuffer_ = 0;
  water_scene_color_texture_ = 0;
  water_scene_depth_texture_ = 0;
  creature_vao_ = 0;
  creature_vbo_ = 0;
  creature_ebo_ = 0;
  creature_instance_vbo_ = 0;
  viewmodel_vao_ = 0;
  viewmodel_instance_vbo_ = 0;
  item_drop_vao_ = 0;
  item_drop_vbo_ = 0;
  item_drop_ebo_ = 0;
  item_drop_instance_vbo_ = 0;
  precipitation_vao_ = 0;
  precipitation_vbo_ = 0;
  precipitation_instance_vbo_ = 0;
  old_guard_effect_vao_ = 0;
  old_guard_effect_vbo_ = 0;
  old_guard_effect_instance_vbo_ = 0;
  world_program_ = 0;
  modern_water_program_ = 0;
  modern_terrain_program_ = 0;
  modern_architecture_program_ = 0;
  modern_terrain_shadow_program_ = 0;
  modern_ship_program_ = 0;
  modern_ship_shadow_program_ = 0;
  item_drop_program_ = 0;
  precipitation_program_ = 0;
  old_guard_effect_program_ = 0;
  creature_program_ = 0;
  creature_shadow_program_ = 0;
  shadow_program_ = 0;
  hud_program_ = 0;
  crosshair_program_ = 0;
  sky_program_ = 0;
  sea_horizon_program_ = 0;
  post_process_program_ = 0;
  glow_extract_program_ = 0;
  glow_blur_program_ = 0;
  menu_background_program_ = 0;
  world_uniforms_ = {};
  modern_water_uniforms_ = {};
  modern_terrain_uniforms_ = {};
  modern_architecture_uniforms_ = {};
  modern_terrain_shadow_uniforms_ = {};
  modern_ship_uniforms_ = {};
  modern_ship_shadow_uniforms_ = {};
  creature_uniforms_ = {};
  creature_shadow_light_view_projection_ = -1;
  item_drop_uniforms_ = {};
  shadow_uniforms_ = {};
  hud_uniforms_ = {};
  sky_uniforms_ = {};
  sea_horizon_uniforms_ = {};
  post_process_uniforms_ = {};
  precipitation_uniforms_ = {};
  old_guard_effect_uniforms_ = {};
  glow_extract_uniforms_ = {};
  glow_blur_uniforms_ = {};
  menu_background_uniforms_ = {};
  creature_instance_buffer_bytes_ = 0;
  viewmodel_instance_buffer_bytes_ = 0;
  item_drop_instance_buffer_bytes_ = 0;
  creature_template_vertex_buffer_bytes_ = 0;
  creature_template_index_buffer_bytes_ = 0;
  item_drop_template_vertex_buffer_bytes_ = 0;
  item_drop_template_index_buffer_bytes_ = 0;
  creature_template_index_count_ = 0;
  item_drop_template_index_count_ = 0;
  precipitation_instance_buffer_bytes_ = 0;
  old_guard_effect_instance_buffer_bytes_ = 0;
  hud_vertex_buffer_bytes_ = 0;
  last_frame_stats_ = {};
  water_scene_target_width_ = 0;
  water_scene_target_height_ = 0;
  scene_target_width_ = 0;
  scene_target_height_ = 0;
  glow_target_width_ = 0;
  glow_target_height_ = 0;
  water_scene_color_internal_format_ = 0;
  scene_color_internal_format_ = 0;
  glow_color_internal_format_ = 0;
  precipitation_field_.clear();
  precipitation_instances_scratch_.clear();
  old_guard_effect_instances_scratch_.clear();
  chunk_upload_scratch_ = {};
  terrain_upload_scratch_ = {};
  architecture_upload_scratch_ = {};
  architecture_indices_scratch_.clear();
  architecture_index_coverage_scratch_.clear();
  block_break_overlay_scratch_ = {};
  loading_vertices_scratch_.clear();
  gameplay_announcement_vertices_scratch_.clear();
  backrooms_flashlight_hud_vertices_scratch_.clear();
  backrooms_jack_screamer_vertices_scratch_.clear();
  backrooms_marlow_screamer_vertices_scratch_.clear();
  command_console_vertices_scratch_.clear();
  last_gpu_timings_ = {};
  gpu_frame_index_ = 0;
  adaptive_last_gpu_source_frame_ = 0;
  pending_cpu_frame_time_ms_ = 0.0;
  material_pack_checksum_ = 0U;
  material_pack_version_ = 0U;
  material_pack_width_ = 0U;
  material_pack_height_ = 0U;
  material_pack_layers_ = 0U;
  material_pack_mips_ = 0U;
  msdf_font_width_ = 0U;
  msdf_font_height_ = 0U;
  msdf_font_mips_ = 0U;
  backrooms_jack_screamer_width_ = 0U;
  backrooms_jack_screamer_height_ = 0U;
  backrooms_marlow_screamer_width_ = 0U;
  backrooms_marlow_screamer_height_ = 0U;
  frame_draw_calls_ = 0U;
  frame_triangles_ = 0U;
  frame_uploaded_bytes_ = 0U;
  world_resource_reset_progress_.finish();
  backrooms_jack_render_view_ = {};
  backrooms_jack_light_interference_ = {};
  backrooms_marlow_result_ = {};
  backrooms_marlow_visual_anchor_ = {};
  backrooms_jack_interference_fixture_cache_.reset();
  ship_mesh_cache_.reset();
  active_ship_lod_ = StylizedShipLod::Near;
  active_gpu_query_frame_ = -1;
  active_gpu_pass_ = -1;
  gpu_timers_supported_ = false;
  adaptive_gpu_sample_consumed_ = false;
  pending_cpu_frame_time_valid_ = false;
  adaptive_quality_controller_.reset(options_.quality, 1, 1);
  active_quality_settings_ =
      resolve_renderer_quality_settings(options_.quality, 1, 1);
  gl_api_ready_ = false;
  initialized_ = false;
}

void Renderer::render_frame(
    World &world, const PlayerController &player,
    const PlayerMusketView &player_musket, const HotbarState &hotbar,
    const InventoryMenuState &inventory_menu,
    const DeathScreenState &death_screen, const PauseMenuState &pause_menu,
    const MainMenuState &main_menu, const SaveSlotMenuState &save_slot_menu,
    const OptionsMenuState &options_menu,
    const ConfirmDialogState &confirm_dialog,
    std::span<const CreatureRenderInstance> creatures,
    std::span<const ItemDropRenderInstance> item_drops,
    const ShipRenderState &ship, const PlayerProgressionState &progression,
    bool super_vision_active,
    const BackroomsFlashlightHudView &backrooms_flashlight,
    const GameplayHudAnnouncementView &gameplay_announcement,
    const MaritimeHudView &maritime_hud,
    const CommandConsoleView &command_console,
    const EnvironmentState &environment, int width, int height) {
  render_frame(world, player, player_musket, hotbar, inventory_menu,
               death_screen, pause_menu, main_menu, save_slot_menu,
               options_menu, confirm_dialog, creatures,
               std::span<const CrewRenderInstance>{},
               std::span<const OldGuardRenderInstance>{},
               std::span<const OldGuardMuzzleFlashInstance>{},
               std::span<const OldGuardSmokeInstance>{},
               std::span<const OldGuardMuzzleFlashInstance>{},
               std::span<const OldGuardSmokeInstance>{}, item_drops, ship,
               progression, super_vision_active, backrooms_flashlight,
               gameplay_announcement, maritime_hud, command_console,
               environment, width, height);
}

void Renderer::render_frame(
    World &world, const PlayerController &player,
    const PlayerMusketView &player_musket, const HotbarState &hotbar,
    const InventoryMenuState &inventory_menu,
    const DeathScreenState &death_screen, const PauseMenuState &pause_menu,
    const MainMenuState &main_menu, const SaveSlotMenuState &save_slot_menu,
    const OptionsMenuState &options_menu,
    const ConfirmDialogState &confirm_dialog,
    std::span<const CreatureRenderInstance> creatures,
    std::span<const CrewRenderInstance> crew,
    std::span<const OldGuardRenderInstance> old_guard,
    std::span<const OldGuardMuzzleFlashInstance> old_guard_flashes,
    std::span<const OldGuardSmokeInstance> old_guard_smoke,
    std::span<const OldGuardMuzzleFlashInstance> player_musket_flashes,
    std::span<const OldGuardSmokeInstance> player_musket_smoke,
    std::span<const ItemDropRenderInstance> item_drops,
    const ShipRenderState &ship, const PlayerProgressionState &progression,
    bool super_vision_active,
    const BackroomsFlashlightHudView &backrooms_flashlight,
    const GameplayHudAnnouncementView &gameplay_announcement,
    const MaritimeHudView &maritime_hud,
    const CommandConsoleView &command_console,
    const EnvironmentState &raw_environment, int width, int height) {
  if (!initialized_) {
    // Je consomme aussi la soumission lorsque le contexte GL est absent :
    // une frame de chargement ne doit pas ressusciter au prochain rendu.
    clear_legendary_presentation();
    return;
  }

  const auto environment = sanitize_weather_for_rendering(raw_environment);
  using clock = std::chrono::steady_clock;
  RendererFrameStats frame_stats{};
  frame_stats.visual_pipeline = options_.visual_pipeline;
  frame_draw_calls_ = 0U;
  frame_triangles_ = 0U;
  frame_uploaded_bytes_ = 0U;
  begin_gpu_frame(frame_stats);
  const auto render_width = std::max(width, 1);
  const auto render_height = std::max(height, 1);
  auto adaptive_sample_ms = 0.0;
  auto adaptive_sample_valid = false;
  if (frame_stats.gpu.valid &&
      (!adaptive_gpu_sample_consumed_ ||
       frame_stats.gpu.source_frame != adaptive_last_gpu_source_frame_)) {
    adaptive_sample_ms = frame_stats.gpu.total_ms();
    adaptive_sample_valid = adaptive_sample_ms > 0.0;
    adaptive_last_gpu_source_frame_ = frame_stats.gpu.source_frame;
    adaptive_gpu_sample_consumed_ = true;
  } else if (!gpu_timers_supported_) {
    adaptive_sample_ms =
        last_frame_stats_.upload_ms + last_frame_stats_.world_ms;
    adaptive_sample_valid = adaptive_sample_ms > 0.0;
  }
  const auto resolved_adaptive_sample = resolve_adaptive_frame_time_sample(
      adaptive_sample_ms, adaptive_sample_valid, pending_cpu_frame_time_ms_,
      pending_cpu_frame_time_valid_);
  pending_cpu_frame_time_ms_ = 0.0;
  pending_cpu_frame_time_valid_ = false;

  const auto previous_quality_settings = active_quality_settings_;
  active_quality_settings_ =
      resolved_adaptive_sample.valid
          ? adaptive_quality_controller_.update(
                options_.quality, render_width, render_height,
                resolved_adaptive_sample.frame_time_ms)
          : adaptive_quality_controller_.settings(options_.quality,
                                                  render_width, render_height);
  if (active_quality_settings_ != previous_quality_settings) {
    if (active_quality_settings_.high_precision_hdr !=
        previous_quality_settings.high_precision_hdr) {
      destroy_water_scene_targets();
      destroy_post_process_targets();
    } else if (active_quality_settings_.glow_downsample !=
               previous_quality_settings.glow_downsample) {
      destroy_glow_targets();
    }
  }
  const auto quality_settings = active_quality_settings_;

  const auto ocean_profile =
      OceanSimulation::surface_profile_for_world(world.generation_profile());

  const auto ocean = OceanSimulation::evaluate(environment, ocean_profile);
  const auto maritime_horizon_enabled =
      options_.visual_pipeline == VisualPipeline::ModernStylized &&
      world.generation_profile() == WorldGenerationProfile::OceanAdventure;
  const auto maritime_fog_range =
      sea_horizon_fog_range(environment.storm_intensity);

  std::array<glm::vec4, kOceanMaxWaveCount> ocean_wave_uniforms{};

  std::array<glm::vec2, kOceanMaxWaveCount> ocean_phase_uniforms{};

  for (std::size_t index = 0; index < ocean.waves.size(); ++index) {

    const auto &wave = ocean.waves[index];

    ocean_wave_uniforms[index] = {
        wave.direction.x,
        wave.direction.y,
        wave.wave_number,
        wave.amplitude,
    };

    ocean_phase_uniforms[index] = {
        wave.phase,
        wave.steepness,
    };
  }

  const auto adaptive_state = adaptive_quality_controller_.state();
  frame_stats.resolved_quality = quality_settings.resolved_quality;
  frame_stats.adaptive_frame_ema_ms = adaptive_state.frame_time_ema_ms;
  frame_stats.adaptive_frame_p95_ms = adaptive_state.frame_time_p95_ms;

  const auto upload_start = clock::now();
  if (ship.visible) {
    if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
      const auto ship_center =
          (ship.world_bounds.min + ship.world_bounds.max) * 0.5F;
      const auto distance_squared =
          glm::dot(ship_center - player.eye_position(),
                   ship_center - player.eye_position());
      constexpr float kShipFarLodEnterDistance = 176.0F;
      constexpr float kShipFarLodExitDistance = 144.0F;
      if (active_ship_lod_ == StylizedShipLod::Near &&
          distance_squared >
              kShipFarLodEnterDistance * kShipFarLodEnterDistance) {
        active_ship_lod_ = StylizedShipLod::Far;
      } else if (active_ship_lod_ == StylizedShipLod::Far &&
                 distance_squared <
                     kShipFarLodExitDistance * kShipFarLodExitDistance) {
        active_ship_lod_ = StylizedShipLod::Near;
      }
    } else {
      active_ship_lod_ = StylizedShipLod::Near;
    }
    ensure_ship_mesh(ship, active_ship_lod_);
  }
  const auto &active_ship_gpu_mesh =
      ship_gpu_meshes_[stylized_ship_lod_index(active_ship_lod_)];
  const auto far_ship_shadow_ready =
      options_.visual_pipeline == VisualPipeline::ModernStylized &&
      ship_mesh_ready(ship, StylizedShipLod::Far);
  sync_gpu_meshes(world, frame_stats, kMaxGpuMeshEventsPerFrame,
                  kMaxGpuMeshSyncMsPerFrame);
  frame_stats.upload_ms =
      std::chrono::duration<double, std::milli>(clock::now() - upload_start)
          .count();

  const auto aspect =
      static_cast<float>(render_width) / static_cast<float>(render_height);
  const auto musket_aim_ratio =
      player_musket.active ? std::clamp(player_musket.aim_ratio, 0.0F, 1.0F)
                           : 0.0F;
  const auto world_fov = player_musket_world_fov(musket_aim_ratio);
  const auto projection_far_distance =
      maritime_horizon_enabled ? kSeaHorizonProjectionFarPlane : 320.0F;
  const auto projection = glm::perspective(glm::radians(world_fov), aspect,
                                           0.1F, projection_far_distance);
  const auto base_viewmodel_fov =
      glm::clamp(options_.viewmodel_fov_degrees, 35.0F, 100.0F);
  const auto musket_viewmodel_fov =
      player_musket_viewmodel_fov(base_viewmodel_fov, musket_aim_ratio);
  const auto viewmodel_projection =
      glm::perspective(glm::radians(musket_viewmodel_fov), aspect, 0.02F, 8.0F);
  const auto view = player.view_matrix();
  const auto inverse_view = glm::inverse(view);
  const auto view_projection = projection * view;
  const glm::mat4 identity_model{1.0F};
  auto sky_view = view;
  sky_view[3] = glm::vec4{0.0F, 0.0F, 0.0F, 1.0F};
  const auto inverse_sky_view_projection = glm::inverse(projection * sky_view);
  const auto frustum_planes = extract_frustum_planes(view_projection);
  const auto eye = player.eye_position();
  auto camera_excluded_from_ocean = false;
  if (ship_protection_is_renderable(ship)) {
    const auto local_eye = glm::vec3{
        glm::inverse(ship.model_matrix) * glm::vec4{eye, 1.0F},
    };
    camera_excluded_from_ocean =
        ship.blueprint->protection_profile.excludes_ocean_local(local_eye);
  }
  const auto sampled_ocean_surface =
      static_cast<float>(kSeaLevel + 1) +
      OceanSimulation::sample(ocean, glm::vec2{eye.x, eye.z},
                              kOceanBuoyancyWaveCount)
          .height;
  // Je calcule une seule fois l'etat reel de la camera. Le ciel, le terrain
  // et le post-traitement suivent ainsi la meme vague et les memes volumes
  // etanches du navire.
  auto maritime_submersion = resolve_maritime_submersion_state(
      options_.visual_pipeline == VisualPipeline::ModernStylized,
      world.generation_profile() == WorldGenerationProfile::OceanAdventure,
      player.state().head_underwater && !camera_excluded_from_ocean, eye.y,
      sampled_ocean_surface);
  const auto marlow_drowning_amount = std::clamp(
      std::isfinite(backrooms_marlow_result_.capture.drowning_amount)
          ? backrooms_marlow_result_.capture.drowning_amount
          : 0.0F,
      0.0F,
      1.0F);
  if (marlow_drowning_amount > 0.0F) {
    // Je reutilise l'absorption aquatique deja coherente avec les pipelines
    // moderne et legacy, puis je la pousse progressivement pendant la noyade.
    maritime_submersion.active = true;
    maritime_submersion.depth =
        0.25F + marlow_drowning_amount * 4.0F;
    maritime_submersion.blend =
        0.38F + marlow_drowning_amount * 0.62F;
  }
  auto camera_forward = player.look_direction();
  if (glm::dot(camera_forward, camera_forward) > 1.0e-6F) {
    camera_forward = glm::normalize(camera_forward);
  } else {
    camera_forward = {0.0F, 0.0F, -1.0F};
  }
  auto forward = camera_forward;
  forward.y = 0.0F;
  if (glm::dot(forward, forward) > 1.0e-6F) {
    forward = glm::normalize(forward);
  } else {
    forward = {0.0F, 0.0F, -1.0F};
  }

  const auto backrooms_interior =
      environment.enclosed_interior &&
      world.generation_profile() == WorldGenerationProfile::Backrooms;
  const auto poolrooms_interior =
      backrooms_interior &&
      environment.poolrooms;
  const auto backrooms_level =
      world.backrooms_level_at_y(player.position().y);
  const auto resident_stream_radius = std::max(world.stream_radius(), 0);
  const auto visible_stream_radius =
      backrooms_interior
          ? std::max(
                resident_stream_radius -
                    kBackroomsStreamingSafetyChunks,
                0)
          : resident_stream_radius;
  const auto active_stream_radius = resolve_adaptive_stream_radius(
      visible_stream_radius, quality_settings.resolved_quality);
  const auto streamed_draw_distance =
      static_cast<float>((active_stream_radius + 2) * kChunkSizeX);
  const auto draw_distance =
      std::min(streamed_draw_distance, quality_settings.terrain_lod_distance);
  const auto draw_distance_sq = draw_distance * draw_distance;
  auto backrooms_fog_target =
      backrooms_interior
          ? BackroomsTerminalFogRange{0.0F, 0.0F}
          : BackroomsTerminalFogRange{};
  if (backrooms_interior && std::isfinite(eye.x) && std::isfinite(eye.z)) {
    constexpr auto kCoverageSide =
        kBackroomsCoverageScanRadius * 2 + 1;
    std::array<ChunkCoord,
               static_cast<std::size_t>(kCoverageSide * kCoverageSide)>
        uploaded_chunks{};
    auto uploaded_chunk_count = std::size_t{0U};
    const auto coverage_radius =
        std::min(resident_stream_radius,
                 kBackroomsCoverageScanRadius);
    const auto camera_chunk_x =
        std::floor(
            static_cast<double>(eye.x) /
            static_cast<double>(kChunkSizeX));
    const auto camera_chunk_z =
        std::floor(
            static_cast<double>(eye.z) /
            static_cast<double>(kChunkSizeZ));
    const auto minimum_safe_chunk =
        static_cast<double>(
            std::numeric_limits<int>::lowest() +
            coverage_radius);
    const auto maximum_safe_chunk =
        static_cast<double>(
            std::numeric_limits<int>::max() -
            coverage_radius);
    if (camera_chunk_x >= minimum_safe_chunk &&
        camera_chunk_x <= maximum_safe_chunk &&
        camera_chunk_z >= minimum_safe_chunk &&
        camera_chunk_z <= maximum_safe_chunk) {
      const ChunkCoord camera_chunk{
          static_cast<int>(camera_chunk_x),
          static_cast<int>(camera_chunk_z),
      };
      for (auto dz = -coverage_radius; dz <= coverage_radius; ++dz) {
        for (auto dx = -coverage_radius; dx <= coverage_radius; ++dx) {
          const ChunkCoord candidate{
              camera_chunk.x + dx,
              camera_chunk.z + dz,
          };
          const auto* chunk = world.find_chunk(candidate);
          const auto mesh = gpu_meshes_.find(candidate);
          // Je mesure la couverture réellement visible sur le GPU. Lorsqu'un
          // remeshing de couture ou de lumière est en attente, l'ancien mesh
          // complet reste affiché : l'écarter refermerait tout le brouillard
          // alors qu'aucune salle ne manque réellement à l'écran.
          if (chunk == nullptr ||
              mesh == gpu_meshes_.end() ||
              mesh->second.revision == 0U) {
            continue;
          }
          uploaded_chunks[uploaded_chunk_count++] = candidate;
        }
      }

      const auto coverage_distance =
          backrooms_contiguous_chunk_coverage_distance(
              eye,
              std::span<const ChunkCoord>{
                  uploaded_chunks.data(),
                  uploaded_chunk_count,
              },
              coverage_radius);
      backrooms_fog_target =
          backrooms_terminal_fog_range(
              draw_distance,
              coverage_distance);
    }
  }

  auto backrooms_fog_range = backrooms_fog_target;
  if (backrooms_interior) {
    const auto fog_update_time = clock::now();
    const auto continues_same_world_and_level =
        backrooms_terminal_fog_snapshot_.valid &&
        backrooms_terminal_fog_snapshot_.world_seed == world.seed() &&
        backrooms_terminal_fog_snapshot_.logical_level == backrooms_level;
    if (continues_same_world_and_level) {
      const auto fog_delta_seconds =
          std::chrono::duration<float>(
              fog_update_time -
              backrooms_terminal_fog_update_time_)
              .count();
      backrooms_fog_range =
          backrooms_advance_terminal_fog_range(
              backrooms_terminal_fog_snapshot_.range,
              backrooms_fog_target,
              fog_delta_seconds);
    }
    backrooms_terminal_fog_snapshot_ = {
        .valid = true,
        .world_seed = world.seed(),
        .logical_level = backrooms_level,
        .range = backrooms_fog_range,
    };
    backrooms_terminal_fog_update_time_ = fog_update_time;
  } else {
    backrooms_terminal_fog_snapshot_ = {};
  }
  std::array<glm::vec4, kMaximumBackroomsFlickerLights>
      backrooms_flicker_uniforms{};
  auto backrooms_flicker_count = std::size_t{0U};
  if (backrooms_interior && std::isfinite(eye.x) && std::isfinite(eye.z)) {
    constexpr auto minimum_safe_world_coordinate =
        static_cast<double>(
            std::numeric_limits<int>::lowest() +
            kBackroomsFlickerSearchRadius);
    constexpr auto maximum_safe_world_coordinate =
        static_cast<double>(
            std::numeric_limits<int>::max() -
            kBackroomsFlickerSearchRadius);
    const auto eye_x = static_cast<double>(eye.x);
    const auto eye_z = static_cast<double>(eye.z);
    if (eye_x >= minimum_safe_world_coordinate &&
        eye_x <= maximum_safe_world_coordinate &&
        eye_z >= minimum_safe_world_coordinate &&
        eye_z <= maximum_safe_world_coordinate) {
      const auto camera_block_x =
          static_cast<int>(std::floor(eye_x));
      const auto camera_block_z =
          static_cast<int>(std::floor(eye_z));
      const auto camera_cache_x =
          static_cast<int>(
              std::floor(
                  static_cast<double>(camera_block_x) /
                  static_cast<double>(kBackroomsFlickerCacheCellSize)));
      const auto camera_cache_z =
          static_cast<int>(
              std::floor(
                  static_cast<double>(camera_block_z) /
                  static_cast<double>(kBackroomsFlickerCacheCellSize)));
      const auto cache_matches =
          backrooms_flicker_cache_valid_ &&
          backrooms_flicker_world_seed_ == world.seed() &&
          backrooms_flicker_level_ == backrooms_level &&
          backrooms_flicker_cache_x_ == camera_cache_x &&
          backrooms_flicker_cache_z_ == camera_cache_z;
      if (!cache_matches) {
        // Je recherche les rares rampes candidates hors de la zone de brume
        // visible. Le cache peut ainsi changer sans faire apparaitre un
        // clignotement au milieu de l'ecran.
        backrooms_flicker_field_ =
            collect_backrooms_flicker_field(
                world.seed(),
                camera_block_x,
                camera_block_z,
                backrooms_level);
        const auto physical_level_offset =
            world.backrooms_spawn_block(backrooms_level).y -
            world.backrooms_spawn_block(
                world.backrooms_level()).y;
        if (physical_level_offset != 0) {
          // Je translate les luminaires du générateur local vers leur étage
          // physique. Le cache conserve ainsi une seule identité logique sans
          // faire clignoter une source dans la dalle voisine.
          for (std::size_t index = 0U;
               index < backrooms_flicker_field_.count;
               ++index) {
            backrooms_flicker_field_.anchors[index].position_y +=
                static_cast<float>(physical_level_offset);
          }
        }
        backrooms_flicker_world_seed_ = world.seed();
        backrooms_flicker_level_ = backrooms_level;
        backrooms_flicker_cache_x_ = camera_cache_x;
        backrooms_flicker_cache_z_ = camera_cache_z;
        backrooms_flicker_cache_valid_ = true;
      }
      const auto sampled_lights =
          sample_backrooms_flicker_lights(
              backrooms_flicker_field_,
              world.seed(),
              environment.weather_time_seconds,
              backrooms_level);
      backrooms_flicker_count =
          std::min(
              backrooms_flicker_field_.count,
              kMaximumBackroomsFlickerLights);
      for (std::size_t index = 0U;
           index < backrooms_flicker_count;
           ++index) {
        const auto &light = sampled_lights[index];
        backrooms_flicker_uniforms[index] = {
            light.position_x,
            light.position_y,
            light.position_z,
            light.intensity,
        };
      }
    } else {
      backrooms_flicker_field_ = {};
      backrooms_flicker_cache_valid_ = false;
    }
  } else {
    backrooms_flicker_field_ = {};
    backrooms_flicker_cache_valid_ = false;
  }
  auto effective_backrooms_interference =
      backrooms_jack_light_interference_;
  const auto &marlow_interference =
      backrooms_marlow_result_.interference;
  if (std::isfinite(marlow_interference.intensity) &&
      marlow_interference.intensity >
          effective_backrooms_interference.intensity) {
    // Je partage le meme budget de rampe entre les deux monstres. L'arbitre
    // evite les manifestations simultanees et je conserve ici la perturbation
    // la plus forte pendant les courtes trainees de disparition.
    effective_backrooms_interference.position =
        marlow_interference.position;
    effective_backrooms_interference.radius =
        marlow_interference.radius;
    effective_backrooms_interference.intensity =
        marlow_interference.intensity;
    effective_backrooms_interference.active =
        marlow_interference.intensity > 0.0F;
    effective_backrooms_interference.mode =
        marlow_interference.blackout_pulse
            ? BackroomsJackLightInterferenceMode::BlackoutPulse
            : BackroomsJackLightInterferenceMode::Flicker;
  }
  const auto interference_cache_key =
      backrooms_interior
          ? make_backrooms_interference_fixture_cache_key(
                world.seed(), backrooms_level,
                effective_backrooms_interference)
          : std::nullopt;
  std::optional<glm::vec4> forced_interference_uniform{};
  if (interference_cache_key.has_value()) {
    if (!backrooms_jack_interference_fixture_cache_.matches(
            *interference_cache_key)) {
      // Je sonde depuis le centre canonique de la cellule : le resultat reste
      // deterministe pendant tout son parcours et le cout n'est paye qu'a la
      // frontiere suivante.
      auto fixture = find_nearest_backrooms_light_fixture(
          interference_cache_key->world_seed,
          interference_cache_key->query_position_x(),
          interference_cache_key->query_position_z(),
          interference_cache_key->search_radius,
          interference_cache_key->logical_level);
      if (fixture.has_value()) {
        const auto physical_level_offset =
            world.backrooms_spawn_block(
                     interference_cache_key->logical_level)
                .y -
            world.backrooms_spawn_block(world.backrooms_level()).y;
        fixture->position_y +=
            static_cast<float>(physical_level_offset);
      }
      backrooms_jack_interference_fixture_cache_.key =
          *interference_cache_key;
      backrooms_jack_interference_fixture_cache_.fixture =
          std::move(fixture);
      backrooms_jack_interference_fixture_cache_.valid = true;
    }

    const auto strength = std::clamp(
        effective_backrooms_interference.intensity, 0.0F, 1.0F);
    const auto &forced_fixture =
        backrooms_jack_interference_fixture_cache_.fixture;
    if (forced_fixture.has_value()) {
      const auto phase =
          environment.weather_time_seconds * 47.0F +
          forced_fixture->position_x * 0.73F +
          forced_fixture->position_z * 1.17F;
      const auto ballast_a = 0.5F + 0.5F * std::sin(phase);
      const auto ballast_b =
          0.5F + 0.5F * std::sin(phase * 1.91F + 2.4F);
      const auto forced_output =
          effective_backrooms_interference.mode ==
                  BackroomsJackLightInterferenceMode::BlackoutPulse
              ? 0.05F
              : ballast_a > 0.42F && ballast_b > 0.30F
                    ? 0.04F
                    : 0.42F + ballast_b * 0.42F;
      // Je melange la panne avec l'intensite courante : le pic Blackout atteint
      // exactement 0,05, puis la rampe recupere selon la trainee de Jack.
      const auto forced_intensity = std::clamp(
          1.0F - strength * (1.0F - forced_output), 0.05F, 1.0F);
      forced_interference_uniform = glm::vec4{
          forced_fixture->position_x,
          forced_fixture->position_y,
          forced_fixture->position_z,
          forced_intensity,
      };
    } else if (effective_backrooms_interference.mode ==
               BackroomsJackLightInterferenceMode::BlackoutPulse) {
      // Je masque aussi l'apparition sans rampe reelle : le plancher interieur
      // conserve les volumes, tandis que ce repli ne simule aucun son ni
      // evenement Notice/Chase depuis le renderer.
      const auto fallback_y =
          std::isfinite(effective_backrooms_interference.position.y)
              ? effective_backrooms_interference.position.y
              : 0.0F;
      forced_interference_uniform = glm::vec4{
          effective_backrooms_interference.position.x,
          fallback_y,
          effective_backrooms_interference.position.z,
          backrooms_blackout_pulse_fallback_intensity(strength),
      };
    }
  } else {
    // Je jette aussi les echecs memorises en quittant l'etage ou a la fin du
    // pulse afin qu'aucun monde precedent ne puisse reutiliser son ancre.
    backrooms_jack_interference_fixture_cache_.reset();
  }

  if (forced_interference_uniform.has_value()) {
    const auto &forced_uniform = *forced_interference_uniform;
    const auto forced_intensity = forced_uniform.w;
    auto duplicate_index = kMaximumBackroomsFlickerLights;
    for (std::size_t index = 0U; index < backrooms_flicker_count; ++index) {
      const auto delta_x =
          backrooms_flicker_uniforms[index].x - forced_uniform.x;
      const auto delta_z =
          backrooms_flicker_uniforms[index].z - forced_uniform.z;
      if (delta_x * delta_x + delta_z * delta_z < 0.25F) {
        duplicate_index = index;
        break;
      }
    }
    if (duplicate_index < kMaximumBackroomsFlickerLights) {
      backrooms_flicker_uniforms[duplicate_index].w = std::min(
          backrooms_flicker_uniforms[duplicate_index].w, forced_intensity);
    } else if (backrooms_flicker_count <
               kMaximumBackroomsFlickerLights) {
      backrooms_flicker_uniforms[backrooms_flicker_count++] = forced_uniform;
    } else {
      // Je garantis la panne la plus proche de Jack, meme si les six
      // emplacements rares autour de la camera sont deja occupes.
      auto replacement = std::size_t{0U};
      auto farthest_distance_squared = -1.0F;
      for (std::size_t index = 0U; index < backrooms_flicker_count; ++index) {
        const auto delta_x =
            backrooms_flicker_uniforms[index].x -
            effective_backrooms_interference.position.x;
        const auto delta_z =
            backrooms_flicker_uniforms[index].z -
            effective_backrooms_interference.position.z;
        const auto distance_squared =
            delta_x * delta_x + delta_z * delta_z;
        if (distance_squared > farthest_distance_squared) {
          farthest_distance_squared = distance_squared;
          replacement = index;
        }
      }
      backrooms_flicker_uniforms[replacement] = forced_uniform;
    }
  }
  if (maritime_horizon_enabled) {
    const auto horizon_sync_start = clock::now();
    sync_sea_horizon_terrain(world, eye, draw_distance);
    frame_stats.upload_ms += std::chrono::duration<double, std::milli>(
                                 clock::now() - horizon_sync_start)
                                 .count();
  }
  constexpr float kBackCullStartDistance = 20.0F;
  constexpr float kBackCullStartDistanceSq =
      kBackCullStartDistance * kBackCullStartDistance;
  const auto sun_visible = environment.sun_direction.y > 0.0F;
  const auto super_vision_strength = super_vision_active ? 1.0F : 0.0F;
  const auto backrooms_flashlight_strength =
      backrooms_interior &&
              backrooms_flashlight.active &&
              std::isfinite(
                  backrooms_flashlight.beam_intensity)
          ? std::clamp(
                backrooms_flashlight.beam_intensity,
                0.0F,
                1.0F)
          : 0.0F;
  glm::mat4 light_view_projection(1.0F);
  glm::mat4 light_view_projection_far(1.0F);
  ShadowCascadeSet shadow_cascades{};
  auto shadow_cascade_count = 1;
  auto shadow_split_distance = 320.0F;
  auto shadow_transition_width = 0.0F;
  ShadowPassContext shadow_context{};
  std::array<ShadowPassContext, kMaximumShadowCascadeCount>
      shadow_cascade_contexts{};
  auto shadow_map_size = 0;

  if (options_.shadows_enabled && sun_visible) {
    shadow_map_size = std::max(options_.shadow_map_size, 1);
    if (!is_modern_visual_pipeline(options_.visual_pipeline)) {
      // Je garde la cascade historique à l'identique : le pipeline de
      // repli doit pouvoir servir de témoin visuel au même commit.
      const auto snap =
          (kShadowDistance * 2.0F) / static_cast<float>(shadow_map_size);
      const auto focus = player.position() + glm::vec3{0.0F, 18.0F, 0.0F};
      const auto snapped_focus = glm::vec3{
          std::floor(focus.x / snap) * snap,
          std::floor(focus.y / snap) * snap,
          std::floor(focus.z / snap) * snap,
      };
      const auto light_position =
          snapped_focus +
          glm::normalize(environment.sun_direction) * (kShadowDistance * 0.85F);
      const auto up = std::abs(environment.sun_direction.y) > 0.95F
                          ? glm::vec3{0.0F, 0.0F, 1.0F}
                          : glm::vec3{0.0F, 1.0F, 0.0F};
      const auto light_view = glm::lookAt(light_position, snapped_focus, up);
      const auto light_projection =
          glm::ortho(-kShadowDistance, kShadowDistance, -kShadowDistance,
                     kShadowDistance, 1.0F, kShadowDistance * 3.0F);
      light_view_projection = light_projection * light_view;
      light_view_projection_far = light_view_projection;
      shadow_cascade_count = 1;
      shadow_split_distance = 320.0F;
      shadow_transition_width = 0.0F;
      shadow_context.frustum = extract_frustum_planes(light_view_projection);
      shadow_context.focus = focus;
      const auto max_shadow_distance =
          kShadowDistance + static_cast<float>(kChunkSizeX);
      shadow_context.max_distance_sq =
          max_shadow_distance * max_shadow_distance;
      shadow_context.enabled = true;
      shadow_cascade_contexts[0] = shadow_context;
    } else {
      ShadowCascadeBuildParameters cascade_parameters{};
      cascade_parameters.quality = quality_settings.resolved_quality;
      cascade_parameters.cascade_count =
          std::clamp(quality_settings.shadow_cascade_count, 1,
                     static_cast<int>(kMaximumShadowCascadeCount));
      cascade_parameters.camera_position = eye;
      cascade_parameters.camera_forward = camera_forward;
      cascade_parameters.camera_up = glm::vec3{inverse_view[1]};
      cascade_parameters.vertical_fov_radians = glm::radians(75.0F);
      cascade_parameters.aspect_ratio = aspect;
      cascade_parameters.near_distance = 0.1F;
      cascade_parameters.far_distance =
          std::clamp(draw_distance + static_cast<float>(kChunkSizeX),
                     kShadowDistance, 320.0F);
      cascade_parameters.sun_direction = environment.sun_direction;
      cascade_parameters.shadow_map_resolution = shadow_map_size;
      cascade_parameters.split_lambda = 0.65F;
      cascade_parameters.caster_depth_padding =
          static_cast<float>(kChunkSizeX) * 1.5F;
      shadow_cascades = build_shadow_cascade_set(cascade_parameters);
      shadow_cascade_count = static_cast<int>(shadow_cascades.cascade_count);
      light_view_projection = shadow_cascades.cascades[0].light_view_projection;
      light_view_projection_far =
          shadow_cascades.cascades[shadow_cascades.cascade_count > 1U ? 1U : 0U]
              .light_view_projection;
      shadow_split_distance = shadow_cascades.split_distances[1];
      shadow_transition_width = shadow_cascades.transition_width;

      const auto &widest_cascade =
          shadow_cascades.cascades[shadow_cascades.cascade_count - 1U];
      shadow_context.frustum = widest_cascade.frustum;
      shadow_context.focus = widest_cascade.bounds.world_center;
      const auto max_shadow_distance = widest_cascade.bounds.bounding_radius +
                                       static_cast<float>(kChunkSizeX);
      shadow_context.max_distance_sq =
          max_shadow_distance * max_shadow_distance;
      shadow_context.enabled = true;
      for (std::size_t cascade_index = 0U;
           cascade_index < shadow_cascades.cascade_count; ++cascade_index) {
        const auto &cascade = shadow_cascades.cascades[cascade_index];
        const auto cascade_distance =
            cascade.bounds.bounding_radius + static_cast<float>(kChunkSizeX);
        shadow_cascade_contexts[cascade_index] = {
            cascade.frustum,
            cascade.bounds.world_center,
            cascade_distance * cascade_distance,
            true,
        };
      }
    }
  }

  auto &visible_chunks = visible_chunks_cache_;
  auto &shadow_chunks = shadow_chunks_cache_;
  visible_chunks.clear();
  shadow_chunks.clear();
  if (visible_chunks.capacity() < gpu_meshes_.size()) {
    visible_chunks.reserve(gpu_meshes_.size());
  }
  if (shadow_chunks.capacity() < gpu_meshes_.size()) {
    shadow_chunks.reserve(gpu_meshes_.size());
  }

  for (const auto &[coord, gpu_mesh] : gpu_meshes_) {
    if (gpu_mesh.opaque_index_count == 0 && gpu_mesh.terrain_index_count == 0 &&
        gpu_mesh.architecture_opaque_index_count == 0 &&
        gpu_mesh.architecture_transparent_index_count == 0 &&
        gpu_mesh.water_index_count == 0) {
      continue;
    }

    auto draw_bounds = gpu_mesh.bounds;

    if (gpu_mesh.water_index_count > 0) {
      // La géométrie CPU décrit la surface au repos. Cette marge évite que les
      // crêtes disparaissent prématurément au bord du frustum.
      const auto water_margin =
          std::max(ocean.maximum_displacement, 0.0F) + 0.02F;

      draw_bounds.min_corner.y -= water_margin;
      draw_bounds.max_corner.y += water_margin;

      draw_bounds.center =
          (draw_bounds.min_corner + draw_bounds.max_corner) * 0.5F;
    }

    const auto visibility = classify_chunk_visibility(
        draw_bounds, frustum_planes, eye, forward, draw_distance_sq,
        kBackCullStartDistanceSq, shadow_context,
        gpu_mesh.opaque_index_count > 0 || gpu_mesh.terrain_index_count > 0 ||
            gpu_mesh.architecture_opaque_index_count > 0);
    if (visibility.camera) {
      visible_chunks.push_back({
          coord,
          &gpu_mesh,
          gpu_mesh.bounds.center,
          visibility.distance_squared,
      });
    }
    if (visibility.shadow) {
      shadow_chunks.push_back({&gpu_mesh});
    }
  }

  ChunkPassVisibility ship_visibility{};
  ChunkBounds ship_world_bounds{};
  auto ship_world_bounds_valid = false;
  if (ship.visible && ship_mesh_ready(ship, active_ship_lod_)) {
    // Je transforme les limites exactes du plan en espace monde pour ne
    // conserver le navire que dans les passes camera et ombre utiles.
    ship_world_bounds = {
        ship.world_bounds.min,
        ship.world_bounds.max,
        (ship.world_bounds.min + ship.world_bounds.max) * 0.5F,
    };
    ship_world_bounds_valid = true;
    ship_visibility = classify_large_bounds_visibility(
        ship_world_bounds, frustum_planes, eye, draw_distance_sq,
        shadow_context, active_ship_gpu_mesh.opaque_index_count > 0);
  }
  const auto preserve_near_ship_shadow_details =
      options_.visual_pipeline == VisualPipeline::ModernStylized &&
      ship_protection_is_renderable(ship) &&
      ship.blueprint->protection_profile.shelters_from_weather_local(glm::vec3{
          glm::inverse(ship.model_matrix) * glm::vec4{eye, 1.0F},
      });
  std::sort(visible_chunks.begin(), visible_chunks.end(),
            [](const VisibleChunk &lhs, const VisibleChunk &rhs) {
              return lhs.distance_squared < rhs.distance_squared;
            });
  frame_stats.visible_chunks = visible_chunks.size();

  marine_requested_chunks_scratch_.clear();
  marine_requested_chunks_scratch_.reserve(visible_chunks.size());
  for (const auto &visible_chunk : visible_chunks) {
    marine_requested_chunks_scratch_.push_back(visible_chunk.coord);
  }
  sync_marine_visuals(world, marine_requested_chunks_scratch_, eye, ship,
                      quality_settings.resolved_quality,
                      environment.weather_time_seconds);

  if (shadow_context.enabled) {
    const auto shadow_start = clock::now();
    begin_gpu_pass(GpuTimedPass::Shadow);
    const std::array<glm::mat4, kMaximumShadowCascadeCount> cascade_matrices{{
        light_view_projection,
        light_view_projection_far,
    }};
    const std::array<GLuint, kMaximumShadowCascadeCount> cascade_framebuffers{{
        shadow_framebuffer_,
        shadow_framebuffer_far_,
    }};

    // Je rends une cascade en qualité basse/moyenne et deux en haute.
    // Les candidats restent conservateurs pour préserver les ombres des
    // objets placés juste avant la coupure entre les deux volumes.
    for (auto cascade_index = 0; cascade_index < shadow_cascade_count;
         ++cascade_index) {
      const auto &cascade_light_view_projection =
          cascade_matrices[static_cast<std::size_t>(cascade_index)];
      const auto &cascade_shadow_context =
          shadow_cascade_contexts[static_cast<std::size_t>(cascade_index)];
      const auto renders_in_cascade =
          [&cascade_shadow_context](const GpuMesh &mesh) {
            return should_render_chunk_in_shadow_pass(
                mesh.bounds, cascade_shadow_context.frustum,
                cascade_shadow_context.focus,
                cascade_shadow_context.max_distance_sq);
          };
      glViewport(0, 0, shadow_map_size, shadow_map_size);
      glBindFramebuffer(
          GL_FRAMEBUFFER,
          cascade_framebuffers[static_cast<std::size_t>(cascade_index)]);
      glClear(GL_DEPTH_BUFFER_BIT);
      glEnable(GL_DEPTH_TEST);
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(2.0F, 4.0F);

      glUseProgram(shadow_program_);
      glUniformMatrix4fv(shadow_uniforms_.model, 1, GL_FALSE,
                         glm::value_ptr(identity_model));
      glUniformMatrix4fv(shadow_uniforms_.light_view_projection, 1, GL_FALSE,
                         glm::value_ptr(cascade_light_view_projection));
      glUniform1f(shadow_uniforms_.time_of_day, environment.time_of_day);
      glUniform1f(shadow_uniforms_.wind_strength, environment.wind_strength);
      glUniform1i(shadow_uniforms_.atlas, 0);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, atlas_texture_);

      for (const auto &shadow_chunk : shadow_chunks) {
        if (shadow_chunk.mesh->opaque_index_count == 0 ||
            !renders_in_cascade(*shadow_chunk.mesh)) {
          continue;
        }
        glBindVertexArray(shadow_chunk.mesh->vao);
        glDrawElements(GL_TRIANGLES, shadow_chunk.mesh->opaque_index_count,
                       GL_UNSIGNED_INT, nullptr);
        record_triangle_draw(shadow_chunk.mesh->opaque_index_count);
        ++frame_stats.shadow_chunks;
      }
      if (options_.visual_pipeline == VisualPipeline::ModernStylized &&
          modern_terrain_shadow_program_ != 0) {
        glUseProgram(modern_terrain_shadow_program_);
        glUniformMatrix4fv(modern_terrain_shadow_uniforms_.model, 1, GL_FALSE,
                           glm::value_ptr(identity_model));
        glUniformMatrix4fv(
            modern_terrain_shadow_uniforms_.light_view_projection, 1, GL_FALSE,
            glm::value_ptr(cascade_light_view_projection));
        glUniform1i(modern_terrain_shadow_uniforms_.material_albedo, 4);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D_ARRAY, modern_material_albedo_texture_);

        for (const auto &shadow_chunk : shadow_chunks) {
          if (shadow_chunk.mesh->terrain_index_count == 0 ||
              !renders_in_cascade(*shadow_chunk.mesh)) {
            continue;
          }
          glBindVertexArray(shadow_chunk.mesh->terrain_vao);
          glDrawElements(GL_TRIANGLES, shadow_chunk.mesh->terrain_index_count,
                         GL_UNSIGNED_INT, nullptr);
          record_triangle_draw(shadow_chunk.mesh->terrain_index_count);
          if (shadow_chunk.mesh->opaque_index_count == 0) {
            ++frame_stats.shadow_chunks;
          }
        }
        // Le VAO architectural n'expose pas l'attribut de flags en
        // location 4. Sa valeur générique doit donc rester à zéro, sinon
        // une valeur résiduelle pourrait transformer un mur opaque en
        // carte alpha dans le shader d'ombre partagé.
        glVertexAttribI1ui(4, 0U);
        for (const auto &shadow_chunk : shadow_chunks) {
          if (shadow_chunk.mesh->architecture_opaque_index_count == 0 ||
              !renders_in_cascade(*shadow_chunk.mesh)) {
            continue;
          }
          glBindVertexArray(shadow_chunk.mesh->architecture_vao);
          glDrawElements(GL_TRIANGLES,
                         shadow_chunk.mesh->architecture_opaque_index_count,
                         GL_UNSIGNED_INT, nullptr);
          record_triangle_draw(
              shadow_chunk.mesh->architecture_opaque_index_count);
          if (shadow_chunk.mesh->opaque_index_count == 0 &&
              shadow_chunk.mesh->terrain_index_count == 0) {
            ++frame_stats.shadow_chunks;
          }
        }

        // Je restaure le programme historique avant les ombres du navire
        // et des entités, qui conservent encore leur format de sommet.
        glUseProgram(shadow_program_);
        glUniformMatrix4fv(shadow_uniforms_.model, 1, GL_FALSE,
                           glm::value_ptr(identity_model));
        glUniformMatrix4fv(shadow_uniforms_.light_view_projection, 1, GL_FALSE,
                           glm::value_ptr(cascade_light_view_projection));
        glUniform1f(shadow_uniforms_.time_of_day, environment.time_of_day);
        glUniform1f(shadow_uniforms_.wind_strength, environment.wind_strength);
        glUniform1i(shadow_uniforms_.atlas, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlas_texture_);
      }
      const auto ship_visible_in_cascade =
          ship_visibility.shadow && ship_world_bounds_valid &&
          should_render_chunk_in_shadow_pass(
              ship_world_bounds, cascade_shadow_context.frustum,
              cascade_shadow_context.focus,
              cascade_shadow_context.max_distance_sq);
      if (ship_visible_in_cascade) {
        const auto cascade_ship_lod =
            options_.visual_pipeline == VisualPipeline::ModernStylized
                ? stylized_ship_shadow_lod(cascade_index, active_ship_lod_,
                                           far_ship_shadow_ready,
                                           preserve_near_ship_shadow_details)
                : StylizedShipLod::Near;
        const auto &cascade_ship_gpu_mesh =
            ship_gpu_meshes_[stylized_ship_lod_index(cascade_ship_lod)];
        const auto modern_ship_shadow =
            options_.visual_pipeline == VisualPipeline::ModernStylized &&
            modern_ship_shadow_program_ != 0U &&
            modern_material_albedo_texture_ != 0U;
        if (modern_ship_shadow) {
          const auto material_layers = modern_ship_material_layers();
          glUseProgram(modern_ship_shadow_program_);
          glUniformMatrix4fv(modern_ship_shadow_uniforms_.model, 1, GL_FALSE,
                             glm::value_ptr(ship.model_matrix));
          glUniformMatrix4fv(modern_ship_shadow_uniforms_.light_view_projection,
                             1, GL_FALSE,
                             glm::value_ptr(cascade_light_view_projection));
          glUniform1i(modern_ship_shadow_uniforms_.material_albedo, 4);
          glUniform1fv(modern_ship_shadow_uniforms_.material_layers,
                       static_cast<GLsizei>(material_layers.size()),
                       material_layers.data());
          glUniform1f(modern_ship_shadow_uniforms_.time_seconds,
                      environment.weather_time_seconds);
          glUniform1f(modern_ship_shadow_uniforms_.wind_strength,
                      environment.wind_strength);
          glActiveTexture(GL_TEXTURE4);
          glBindTexture(GL_TEXTURE_2D_ARRAY, modern_material_albedo_texture_);
        } else {
          glUseProgram(shadow_program_);
          glUniformMatrix4fv(shadow_uniforms_.model, 1, GL_FALSE,
                             glm::value_ptr(ship.model_matrix));
        }
        glBindVertexArray(cascade_ship_gpu_mesh.vao);
        glDrawElements(GL_TRIANGLES, cascade_ship_gpu_mesh.opaque_index_count,
                       GL_UNSIGNED_INT, nullptr);
        record_triangle_draw(cascade_ship_gpu_mesh.opaque_index_count);
        if (!modern_ship_shadow) {
          glUniformMatrix4fv(shadow_uniforms_.model, 1, GL_FALSE,
                             glm::value_ptr(identity_model));
        }
      }
      draw_creature_shadows(creatures, crew, old_guard,
                            cascade_light_view_projection,
                            cascade_shadow_context.focus);
    }

    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    end_gpu_pass(GpuTimedPass::Shadow);
    frame_stats.shadow_ms =
        std::chrono::duration<double, std::milli>(clock::now() - shadow_start)
            .count();
  }

  const auto world_start = clock::now();
  const auto optional_post_process_enabled =
      options_.post_process_enabled && width > 0 && height > 0;
  const auto menu_preview_visible =
      main_menu.visible ||
      (save_slot_menu.visible &&
       save_slot_menu.parent == SaveSlotMenuParent::MainMenu) ||
      (options_menu.visible &&
       options_menu.parent == OptionsMenuParent::MainMenu);
  const auto has_visible_water =
      std::any_of(visible_chunks.begin(), visible_chunks.end(),
                  [](const VisibleChunk &visible_chunk) {
                    return visible_chunk.mesh->water_index_count > 0;
                  });
  const auto modern_output_resolve_required =
      is_modern_visual_pipeline(options_.visual_pipeline) && width > 0 &&
      height > 0;
  const auto requires_scene_target = optional_post_process_enabled ||
                                     modern_output_resolve_required ||
                                     has_visible_water || menu_preview_visible;

  if (has_visible_water) {
    ensure_water_scene_targets(render_width, render_height);
  }
  if (requires_scene_target) {
    ensure_post_process_targets(render_width, render_height,
                                optional_post_process_enabled ||
                                    menu_preview_visible);
  }

  const auto final_target_framebuffer =
      requires_scene_target ? scene_framebuffer_ : 0U;
  const auto opaque_target_framebuffer =
      has_visible_water ? water_scene_framebuffer_ : final_target_framebuffer;
  const auto inverse_view_projection = glm::inverse(view_projection);

  glBindFramebuffer(GL_FRAMEBUFFER, opaque_target_framebuffer);
  glViewport(0, 0, render_width, render_height);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glDisable(GL_BLEND);
  const auto clear_color =
      backrooms_interior
          ? glm::vec3 {0.0F}
          : environment.sky_zenith_color;
  glClearColor(
      clear_color.r,
      clear_color.g,
      clear_color.b,
      1.0F);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  begin_gpu_pass(GpuTimedPass::Opaque);

  if (maritime_horizon_enabled) {
    draw_sea_horizon_terrain(view_projection, eye, environment,
                             maritime_fog_range);
  }

  glUseProgram(world_program_);
  upload_world_ship_protection(ship);
  glUniform4fv(world_uniforms_.ocean_waves,
               static_cast<GLsizei>(ocean_wave_uniforms.size()),
               glm::value_ptr(ocean_wave_uniforms.front()));

  glUniform2fv(world_uniforms_.ocean_wave_phases,
               static_cast<GLsizei>(ocean_phase_uniforms.size()),
               glm::value_ptr(ocean_phase_uniforms.front()));

  glUniform1i(world_uniforms_.ocean_wave_count,
              std::clamp(quality_settings.ocean_wave_count, 1,
                         static_cast<int>(kOceanMaxWaveCount)));

  glUniform1f(world_uniforms_.ocean_foam_threshold, ocean.foam_threshold);

  glUniform1f(world_uniforms_.ocean_detail_strength,
              ocean.detail_strength * quality_settings.ocean_detail_scale);

  glUniform1f(world_uniforms_.ocean_detail_phase, ocean.detail_phase);

  glUniform1f(world_uniforms_.ocean_severity, ocean.severity);

  glUniform1f(world_uniforms_.ocean_tempest_factor, ocean.tempest_factor);

  glUniform1f(world_uniforms_.ocean_open_sea,
              ocean_profile == OceanSurfaceProfile::OpenSea ? 1.0F : 0.0F);
  constexpr auto water_coverage_side =
      kSeaHorizonWaterCoverageScanRadius * 2 + 1;
  std::array<ChunkCoord, static_cast<std::size_t>(water_coverage_side *
                                                  water_coverage_side)>
      uploaded_water_chunks{};
  auto uploaded_water_chunk_count = std::size_t{0U};
  auto water_coverage_radius = 0;
  if (std::isfinite(eye.x) && std::isfinite(eye.z) &&
      eye.x >= static_cast<float>(std::numeric_limits<int>::lowest()) &&
      eye.x <= static_cast<float>(std::numeric_limits<int>::max()) &&
      eye.z >= static_cast<float>(std::numeric_limits<int>::lowest()) &&
      eye.z <= static_cast<float>(std::numeric_limits<int>::max())) {
    const auto camera_chunk =
        world.world_to_chunk(static_cast<int>(std::floor(eye.x)),
                             static_cast<int>(std::floor(eye.z)));
    water_coverage_radius =
        std::min(active_stream_radius + 1, kSeaHorizonWaterCoverageScanRadius);
    // Je mesure les anneaux réellement publiés sur le GPU. Pendant un
    // chargement progressif, le raccord se replie donc avant le premier
    // chunk absent au lieu d'en révéler le bord.
    for (auto dz = -water_coverage_radius; dz <= water_coverage_radius; ++dz) {
      for (auto dx = -water_coverage_radius; dx <= water_coverage_radius;
           ++dx) {
        const ChunkCoord candidate{
            camera_chunk.x + dx,
            camera_chunk.z + dz,
        };
        const auto mesh = gpu_meshes_.find(candidate);
        if (mesh == gpu_meshes_.end() || mesh->second.revision == 0U) {
          continue;
        }
        uploaded_water_chunks[uploaded_water_chunk_count++] = candidate;
      }
    }
  }
  const auto uploaded_water_coverage =
      sea_horizon_contiguous_chunk_coverage_distance(
          eye,
          std::span<const ChunkCoord>{
              uploaded_water_chunks.data(),
              uploaded_water_chunk_count,
          },
          water_coverage_radius);
  const auto maritime_water_blend_range =
      sea_horizon_water_blend_range(draw_distance, uploaded_water_coverage);
  glUniform1i(world_uniforms_.maritime_horizon_enabled,
              maritime_horizon_enabled ? 1 : 0);
  glUniform2f(world_uniforms_.maritime_water_blend_range,
              maritime_water_blend_range.start_distance,
              maritime_water_blend_range.end_distance);
  glUniform2f(world_uniforms_.maritime_far_fog_range,
              maritime_fog_range.start_distance,
              maritime_fog_range.end_distance);
  glUniform1f(world_uniforms_.maritime_sea_level,
              static_cast<float>(kSeaLevel + 1));
  glUniformMatrix4fv(world_uniforms_.model, 1, GL_FALSE,
                     glm::value_ptr(identity_model));
  glUniformMatrix4fv(world_uniforms_.view_projection, 1, GL_FALSE,
                     glm::value_ptr(view_projection));
  glUniformMatrix4fv(world_uniforms_.light_view_projection, 1, GL_FALSE,
                     glm::value_ptr(light_view_projection));
  glUniformMatrix4fv(world_uniforms_.light_view_projection_far, 1, GL_FALSE,
                     glm::value_ptr(light_view_projection_far));
  glUniformMatrix4fv(world_uniforms_.inverse_view_projection, 1, GL_FALSE,
                     glm::value_ptr(inverse_view_projection));
  glUniform3fv(world_uniforms_.camera_position, 1, glm::value_ptr(eye));
  glUniform3fv(world_uniforms_.camera_forward, 1,
               glm::value_ptr(camera_forward));
  glUniform3fv(world_uniforms_.sun_direction, 1,
               glm::value_ptr(environment.sun_direction));
  glUniform3fv(world_uniforms_.sun_color, 1,
               glm::value_ptr(environment.sun_color));
  glUniform3fv(world_uniforms_.ambient_color, 1,
               glm::value_ptr(environment.ambient_color));
  glUniform3fv(world_uniforms_.block_light_color, 1,
               glm::value_ptr(environment.block_light_color));
  glUniform1i(world_uniforms_.enclosed_interior,
              environment.enclosed_interior ? 1 : 0);
  glUniform1f(world_uniforms_.interior_visibility_floor,
              environment.interior_visibility_floor);
  glUniform1i(
      world_uniforms_.backrooms_flicker_count,
      static_cast<GLint>(backrooms_flicker_count));
  if (backrooms_flicker_count > 0U) {
    glUniform4fv(
        world_uniforms_.backrooms_flicker_lights,
        static_cast<GLsizei>(backrooms_flicker_count),
        glm::value_ptr(backrooms_flicker_uniforms.front()));
  }
  glUniform1f(
      world_uniforms_.backrooms_flashlight_intensity,
      backrooms_flashlight_strength);
  glUniform3fv(world_uniforms_.fog_color, 1,
               glm::value_ptr(environment.fog_color));
  glUniform3fv(world_uniforms_.distant_fog_color, 1,
               glm::value_ptr(environment.distant_fog_color));
  glUniform2f(world_uniforms_.interior_fog_range,
              backrooms_fog_range.start_distance,
              backrooms_fog_range.end_distance);
  glUniform3fv(world_uniforms_.horizon_glow_color, 1,
               glm::value_ptr(environment.horizon_glow_color));
  glUniform3fv(world_uniforms_.night_tint_color, 1,
               glm::value_ptr(environment.night_tint_color));
  glUniform1f(world_uniforms_.daylight_factor, environment.daylight_factor);
  glUniform1f(world_uniforms_.sun_visibility, sun_visible ? 1.0F : 0.0F);
  glUniform1f(world_uniforms_.time_of_day, environment.time_of_day);
  glUniform1f(world_uniforms_.cloud_intensity, environment.cloud_intensity);
  glUniform1f(world_uniforms_.cloud_shadow_strength,
              environment.cloud_shadow_strength);
  glUniform1f(world_uniforms_.wind_strength, environment.wind_strength);
  glUniform1f(world_uniforms_.atmospheric_scatter_strength,
              environment.atmospheric_scatter_strength);
  glUniform1f(world_uniforms_.height_fog_density,
              environment.height_fog_density);
  glUniform1f(world_uniforms_.precipitation_intensity,
              environment.precipitation_intensity);
  glUniform1f(world_uniforms_.storm_intensity, environment.storm_intensity);
  glUniform1f(world_uniforms_.lightning_intensity,
              environment.lightning_intensity);
  glUniform1f(world_uniforms_.super_vision_strength, super_vision_strength);
  glUniform1i(world_uniforms_.atlas, 0);
  glUniform1i(world_uniforms_.shadow_map, 1);
  glUniform1i(world_uniforms_.shadow_map_far, 7);
  glUniform1i(world_uniforms_.shadow_cascade_count, shadow_cascade_count);
  glUniform1f(world_uniforms_.shadow_split_distance, shadow_split_distance);
  glUniform1f(world_uniforms_.shadow_transition_width, shadow_transition_width);
  glUniform1i(world_uniforms_.scene_color, 2);
  glUniform1i(world_uniforms_.scene_depth, 3);
  glUniform1i(world_uniforms_.shadows_enabled,
              options_.shadows_enabled ? 1 : 0);

  // Je force des textures neutres hors passe de refraction pour que les
  // samplers scene/depth ne pointent jamais vers des ressources sans lien.
  const auto opaque_scene_bindings = select_scene_sampler_bindings(
      false, scene_fallback_color_texture_, scene_fallback_depth_texture_,
      scene_color_texture_, scene_depth_texture_);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, atlas_texture_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, shadow_map_);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, opaque_scene_bindings.color_texture);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, opaque_scene_bindings.depth_texture);
  glActiveTexture(GL_TEXTURE7);
  glBindTexture(GL_TEXTURE_2D, shadow_map_far_);

  for (const auto &visible_chunk : visible_chunks) {
    if (visible_chunk.mesh->opaque_index_count == 0) {
      continue;
    }
    glBindVertexArray(visible_chunk.mesh->vao);
    glDrawElements(GL_TRIANGLES, visible_chunk.mesh->opaque_index_count,
                   GL_UNSIGNED_INT, nullptr);
    record_triangle_draw(visible_chunk.mesh->opaque_index_count);
    ++frame_stats.world_chunks;
  }

  const auto bind_modern_surface_program =
      [&](GLuint program, const ModernTerrainUniformLocations &uniforms,
          float triplanar_sharpness) {
        glUseProgram(program);
        glUniformMatrix4fv(uniforms.model, 1, GL_FALSE,
                           glm::value_ptr(identity_model));
        glUniformMatrix4fv(uniforms.view_projection, 1, GL_FALSE,
                           glm::value_ptr(view_projection));
        glUniformMatrix4fv(uniforms.light_view_projection, 1, GL_FALSE,
                           glm::value_ptr(light_view_projection));
        glUniformMatrix4fv(uniforms.light_view_projection_far, 1, GL_FALSE,
                           glm::value_ptr(light_view_projection_far));
        glUniform3fv(uniforms.camera_position, 1, glm::value_ptr(eye));
        glUniform3fv(uniforms.camera_forward, 1,
                     glm::value_ptr(camera_forward));
        glUniform3fv(uniforms.sun_direction, 1,
                     glm::value_ptr(environment.sun_direction));
        glUniform3fv(uniforms.sun_color, 1,
                     glm::value_ptr(environment.sun_color));
        glUniform3fv(uniforms.ambient_color, 1,
                     glm::value_ptr(environment.ambient_color));
        glUniform3fv(uniforms.block_light_color, 1,
                     glm::value_ptr(environment.block_light_color));
        glUniform1i(uniforms.enclosed_interior,
                    environment.enclosed_interior ? 1 : 0);
        glUniform1f(uniforms.interior_visibility_floor,
                    environment.interior_visibility_floor);
        glUniform1i(
            uniforms.backrooms_flicker_count,
            static_cast<GLint>(
                backrooms_flicker_count));
        if (backrooms_flicker_count > 0U) {
          glUniform4fv(
              uniforms.backrooms_flicker_lights,
              static_cast<GLsizei>(
                  backrooms_flicker_count),
              glm::value_ptr(
                  backrooms_flicker_uniforms.front()));
        }
        glUniform1f(uniforms.backrooms_flashlight_intensity,
                    backrooms_flashlight_strength);
        glUniform3fv(uniforms.fog_color, 1,
                     glm::value_ptr(environment.fog_color));
        glUniform3fv(uniforms.distant_fog_color, 1,
                     glm::value_ptr(environment.distant_fog_color));
        glUniform2f(uniforms.interior_fog_range,
                    backrooms_fog_range.start_distance,
                    backrooms_fog_range.end_distance);
        glUniform3fv(uniforms.night_tint_color, 1,
                     glm::value_ptr(environment.night_tint_color));
        glUniform1f(uniforms.daylight_factor, environment.daylight_factor);
        glUniform1f(uniforms.sun_visibility, sun_visible ? 1.0F : 0.0F);
        glUniform1f(uniforms.cloud_intensity, environment.cloud_intensity);
        glUniform1f(uniforms.overcast_intensity,
                    environment.overcast_intensity);
        glUniform1f(uniforms.precipitation_intensity,
                    environment.precipitation_intensity);
        glUniform1f(uniforms.storm_intensity, environment.storm_intensity);
        glUniform1f(uniforms.lightning_intensity,
                    environment.lightning_intensity);
        glUniform1f(uniforms.triplanar_sharpness, triplanar_sharpness);
        glUniform1f(uniforms.material_detail_scale,
                    quality_settings.material_detail_scale);
        glUniform1i(uniforms.shadows_enabled, options_.shadows_enabled ? 1 : 0);
        glUniform1i(uniforms.material_albedo, 4);
        glUniform1i(uniforms.material_normal_height, 5);
        glUniform1i(uniforms.material_orm_emission, 6);
        glUniform1i(uniforms.shadow_map, 1);
        glUniform1i(uniforms.shadow_map_far, 7);
        glUniform1i(uniforms.shadow_cascade_count, shadow_cascade_count);
        glUniform1f(uniforms.shadow_split_distance, shadow_split_distance);
        glUniform1f(uniforms.shadow_transition_width, shadow_transition_width);
        glUniform1i(uniforms.maritime_horizon_enabled, 0);
        glUniform2f(uniforms.maritime_detail_transition_range, 0.0F, 0.0F);
        glUniform1f(uniforms.maritime_sea_level,
                    static_cast<float>(kSeaLevel + 1));
        glUniform1i(uniforms.maritime_submersion_active,
                    maritime_submersion.active ? 1 : 0);
        glUniform1f(uniforms.time_seconds, environment.weather_time_seconds);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, shadow_map_);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, shadow_map_far_);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D_ARRAY, modern_material_albedo_texture_);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D_ARRAY,
                      modern_material_normal_height_texture_);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D_ARRAY,
                      modern_material_orm_emission_texture_);
      };

  if (options_.visual_pipeline == VisualPipeline::ModernStylized &&
      modern_terrain_program_ != 0 && modern_architecture_program_ != 0 &&
      modern_material_albedo_texture_ != 0 &&
      modern_material_normal_height_texture_ != 0 &&
      modern_material_orm_emission_texture_ != 0) {
    glUseProgram(modern_terrain_program_);
    glUniformMatrix4fv(modern_terrain_uniforms_.model, 1, GL_FALSE,
                       glm::value_ptr(identity_model));
    glUniformMatrix4fv(modern_terrain_uniforms_.view_projection, 1, GL_FALSE,
                       glm::value_ptr(view_projection));
    glUniformMatrix4fv(modern_terrain_uniforms_.light_view_projection, 1,
                       GL_FALSE, glm::value_ptr(light_view_projection));
    glUniformMatrix4fv(modern_terrain_uniforms_.light_view_projection_far, 1,
                       GL_FALSE, glm::value_ptr(light_view_projection_far));
    glUniform3fv(modern_terrain_uniforms_.camera_position, 1,
                 glm::value_ptr(eye));
    glUniform3fv(modern_terrain_uniforms_.camera_forward, 1,
                 glm::value_ptr(camera_forward));
    glUniform3fv(modern_terrain_uniforms_.sun_direction, 1,
                 glm::value_ptr(environment.sun_direction));
    glUniform3fv(modern_terrain_uniforms_.sun_color, 1,
                 glm::value_ptr(environment.sun_color));
    glUniform3fv(modern_terrain_uniforms_.ambient_color, 1,
                 glm::value_ptr(environment.ambient_color));
    glUniform3fv(modern_terrain_uniforms_.block_light_color, 1,
                 glm::value_ptr(environment.block_light_color));
    glUniform1i(modern_terrain_uniforms_.enclosed_interior,
                environment.enclosed_interior ? 1 : 0);
    glUniform1f(modern_terrain_uniforms_.interior_visibility_floor,
                environment.interior_visibility_floor);
    glUniform1i(
        modern_terrain_uniforms_.backrooms_flicker_count,
        static_cast<GLint>(
            backrooms_flicker_count));
    if (backrooms_flicker_count > 0U) {
      glUniform4fv(
          modern_terrain_uniforms_.backrooms_flicker_lights,
          static_cast<GLsizei>(
              backrooms_flicker_count),
          glm::value_ptr(
              backrooms_flicker_uniforms.front()));
    }
    glUniform1f(
        modern_terrain_uniforms_.backrooms_flashlight_intensity,
        backrooms_flashlight_strength);
    glUniform3fv(modern_terrain_uniforms_.fog_color, 1,
                 glm::value_ptr(environment.fog_color));
    glUniform3fv(modern_terrain_uniforms_.distant_fog_color, 1,
                 glm::value_ptr(environment.distant_fog_color));
    glUniform2f(modern_terrain_uniforms_.interior_fog_range,
                backrooms_fog_range.start_distance,
                backrooms_fog_range.end_distance);
    glUniform3fv(modern_terrain_uniforms_.night_tint_color, 1,
                 glm::value_ptr(environment.night_tint_color));
    glUniform1f(modern_terrain_uniforms_.daylight_factor,
                environment.daylight_factor);
    glUniform1f(modern_terrain_uniforms_.sun_visibility,
                sun_visible ? 1.0F : 0.0F);
    glUniform1f(modern_terrain_uniforms_.cloud_intensity,
                environment.cloud_intensity);
    glUniform1f(modern_terrain_uniforms_.overcast_intensity,
                environment.overcast_intensity);
    glUniform1f(modern_terrain_uniforms_.precipitation_intensity,
                environment.precipitation_intensity);
    glUniform1f(modern_terrain_uniforms_.storm_intensity,
                environment.storm_intensity);
    glUniform1f(modern_terrain_uniforms_.lightning_intensity,
                environment.lightning_intensity);
    glUniform1f(modern_terrain_uniforms_.triplanar_sharpness, 5.5F);
    glUniform1f(modern_terrain_uniforms_.material_detail_scale,
                quality_settings.material_detail_scale);
    glUniform1i(modern_terrain_uniforms_.shadows_enabled,
                options_.shadows_enabled ? 1 : 0);
    glUniform1i(modern_terrain_uniforms_.material_albedo, 4);
    glUniform1i(modern_terrain_uniforms_.material_normal_height, 5);
    glUniform1i(modern_terrain_uniforms_.material_orm_emission, 6);
    glUniform1i(modern_terrain_uniforms_.shadow_map, 1);
    glUniform1i(modern_terrain_uniforms_.shadow_map_far, 7);
    glUniform1i(modern_terrain_uniforms_.shadow_cascade_count,
                shadow_cascade_count);
    glUniform1f(modern_terrain_uniforms_.shadow_split_distance,
                shadow_split_distance);
    glUniform1f(modern_terrain_uniforms_.shadow_transition_width,
                shadow_transition_width);
    glUniform1i(modern_terrain_uniforms_.maritime_horizon_enabled,
                maritime_horizon_enabled ? 1 : 0);
    glUniform2f(modern_terrain_uniforms_.maritime_detail_transition_range,
                sea_horizon_detail_transition_range_.start_distance,
                sea_horizon_detail_transition_range_.end_distance);
    glUniform1f(modern_terrain_uniforms_.maritime_sea_level,
                static_cast<float>(kSeaLevel + 1));
    glUniform1i(modern_terrain_uniforms_.maritime_submersion_active,
                maritime_submersion.active ? 1 : 0);
    glUniform1f(modern_terrain_uniforms_.time_seconds,
                environment.weather_time_seconds);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadow_map_);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, shadow_map_far_);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D_ARRAY, modern_material_albedo_texture_);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, modern_material_normal_height_texture_);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY, modern_material_orm_emission_texture_);

    for (const auto &visible_chunk : visible_chunks) {
      if (visible_chunk.mesh->terrain_index_count == 0) {
        continue;
      }
      glBindVertexArray(visible_chunk.mesh->terrain_vao);
      glDrawElements(GL_TRIANGLES, visible_chunk.mesh->terrain_index_count,
                     GL_UNSIGNED_INT, nullptr);
      record_triangle_draw(visible_chunk.mesh->terrain_index_count);
      if (visible_chunk.mesh->opaque_index_count == 0) {
        ++frame_stats.world_chunks;
      }
    }

    bind_modern_surface_program(modern_architecture_program_,
                                modern_architecture_uniforms_, 8.0F);
    for (const auto &visible_chunk : visible_chunks) {
      if (visible_chunk.mesh->architecture_opaque_index_count == 0) {
        continue;
      }
      glBindVertexArray(visible_chunk.mesh->architecture_vao);
      glDrawElements(GL_TRIANGLES,
                     visible_chunk.mesh->architecture_opaque_index_count,
                     GL_UNSIGNED_INT, nullptr);
      record_triangle_draw(visible_chunk.mesh->architecture_opaque_index_count);
      if (visible_chunk.mesh->opaque_index_count == 0 &&
          visible_chunk.mesh->terrain_index_count == 0) {
        ++frame_stats.world_chunks;
      }
    }

    if (marine_decor_gpu_mesh_.terrain_index_count > 0 ||
        ocean_life_gpu_mesh_.terrain_index_count > 0) {
      bind_modern_surface_program(modern_terrain_program_,
                                  modern_terrain_uniforms_, 5.5F);
      glDisable(GL_CULL_FACE);
      const std::array<const GpuMesh *, 2U> marine_meshes{{
          &marine_decor_gpu_mesh_,
          &ocean_life_gpu_mesh_,
      }};
      for (const auto *marine_mesh : marine_meshes) {
        if (marine_mesh->terrain_index_count <= 0) {
          continue;
        }
        glBindVertexArray(marine_mesh->terrain_vao);
        glDrawElements(GL_TRIANGLES, marine_mesh->terrain_index_count,
                       GL_UNSIGNED_INT, nullptr);
        record_triangle_draw(marine_mesh->terrain_index_count);
      }
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
    }

    // Je restaure le programme du monde pour le navire et l'eau.
    glUseProgram(world_program_);
  }
  if (ship_visibility.camera) {
    const auto modern_ship_render =
        options_.visual_pipeline == VisualPipeline::ModernStylized &&
        modern_ship_program_ != 0U && modern_material_albedo_texture_ != 0U &&
        modern_material_normal_height_texture_ != 0U &&
        modern_material_orm_emission_texture_ != 0U;
    if (modern_ship_render) {
      const auto material_layers = modern_ship_material_layers();
      std::array<glm::vec4, kMaximumShipInteriorLights> light_position_radius{};
      std::array<glm::vec4, kMaximumShipInteriorLights> light_color_intensity{};
      std::array<glm::vec4, kMaximumShipInteriorLights> light_zone_min_spill{};
      std::array<glm::vec4, kMaximumShipInteriorLights> light_zone_max_seed{};
      std::array<glm::vec4, kMaximumShipInteriorLights> light_doorways{};
      const auto interior_lights = ship.blueprint != nullptr
                                       ? ship.blueprint->interior_lanterns
                                       : std::span<const ShipInteriorLight>{};
      const auto exterior_lights = ship.blueprint != nullptr
                                       ? ship.blueprint->exterior_lanterns
                                       : std::span<const ShipExteriorLight>{};
      const auto exterior_light_radiance =
          exterior_lantern_radiance(exterior_lights, 1.18F);
      const auto light_count =
          std::min(interior_lights.size(), kMaximumShipInteriorLights);
      for (std::size_t index = 0U; index < light_count; ++index) {
        const auto &light = interior_lights[index];
        light_position_radius[index] = {
            light.local_position,
            std::max(light.radius, 0.01F),
        };
        light_color_intensity[index] = {
            glm::max(light.color, glm::vec3{0.0F}),
            std::max(light.intensity, 0.0F),
        };
        light_zone_min_spill[index] = {
            glm::min(light.zone_min, light.zone_max),
            std::max(light.zone_spill, 0.001F),
        };
        light_zone_max_seed[index] = {
            glm::max(light.zone_min, light.zone_max),
            light.flicker_seed,
        };
        light_doorways[index] = {
            light.minimum_z_door,
            light.maximum_z_door,
        };
      }

      glUseProgram(modern_ship_program_);
      const auto camera_local_position = glm::vec3{
          glm::inverse(ship.model_matrix) *
              glm::vec4{
                  eye,
                  1.0F,
              },
      };
      glUniformMatrix4fv(modern_ship_uniforms_.model, 1, GL_FALSE,
                         glm::value_ptr(ship.model_matrix));
      glUniformMatrix4fv(modern_ship_uniforms_.view_projection, 1, GL_FALSE,
                         glm::value_ptr(view_projection));
      glUniformMatrix4fv(modern_ship_uniforms_.light_view_projection, 1,
                         GL_FALSE, glm::value_ptr(light_view_projection));
      glUniformMatrix4fv(modern_ship_uniforms_.light_view_projection_far, 1,
                         GL_FALSE, glm::value_ptr(light_view_projection_far));
      glUniform3fv(modern_ship_uniforms_.camera_position, 1,
                   glm::value_ptr(eye));
      glUniform3fv(modern_ship_uniforms_.camera_local_position, 1,
                   glm::value_ptr(camera_local_position));
      glUniform3fv(modern_ship_uniforms_.camera_forward, 1,
                   glm::value_ptr(camera_forward));
      glUniform3fv(modern_ship_uniforms_.sun_direction, 1,
                   glm::value_ptr(environment.sun_direction));
      glUniform3fv(modern_ship_uniforms_.sun_color, 1,
                   glm::value_ptr(environment.sun_color));
      glUniform3fv(modern_ship_uniforms_.ambient_color, 1,
                   glm::value_ptr(environment.ambient_color));
      glUniform3fv(modern_ship_uniforms_.fog_color, 1,
                   glm::value_ptr(environment.fog_color));
      glUniform3fv(modern_ship_uniforms_.distant_fog_color, 1,
                   glm::value_ptr(environment.distant_fog_color));
      glUniform3fv(modern_ship_uniforms_.night_tint_color, 1,
                   glm::value_ptr(environment.night_tint_color));
      glUniform1f(modern_ship_uniforms_.daylight_factor,
                  environment.daylight_factor);
      glUniform1f(modern_ship_uniforms_.sun_visibility,
                  sun_visible ? 1.0F : 0.0F);
      glUniform1f(modern_ship_uniforms_.precipitation_intensity,
                  environment.precipitation_intensity);
      glUniform1f(modern_ship_uniforms_.storm_intensity,
                  environment.storm_intensity);
      glUniform1f(modern_ship_uniforms_.exterior_light_activation,
                  ship_exterior_light_activation(
                      environment.daylight_factor, environment.storm_intensity,
                      environment.cloud_intensity,
                      environment.overcast_intensity));
      glUniform3fv(modern_ship_uniforms_.exterior_light_radiance, 1,
                   glm::value_ptr(exterior_light_radiance));
      glUniform1f(modern_ship_uniforms_.lightning_intensity,
                  environment.lightning_intensity);
      glUniform1f(modern_ship_uniforms_.material_detail_scale,
                  quality_settings.material_detail_scale);
      glUniform1i(modern_ship_uniforms_.shadows_enabled,
                  options_.shadows_enabled ? 1 : 0);
      glUniform1i(modern_ship_uniforms_.material_albedo, 4);
      glUniform1i(modern_ship_uniforms_.material_normal_height, 5);
      glUniform1i(modern_ship_uniforms_.material_orm_emission, 6);
      glUniform1i(modern_ship_uniforms_.shadow_map, 1);
      glUniform1i(modern_ship_uniforms_.shadow_map_far, 7);
      glUniform1i(modern_ship_uniforms_.shadow_cascade_count,
                  shadow_cascade_count);
      glUniform1f(modern_ship_uniforms_.shadow_split_distance,
                  shadow_split_distance);
      glUniform1f(modern_ship_uniforms_.shadow_transition_width,
                  shadow_transition_width);
      glUniform1f(modern_ship_uniforms_.time_seconds,
                  environment.weather_time_seconds);
      glUniform1f(modern_ship_uniforms_.wind_strength,
                  environment.wind_strength);
      glUniform1fv(modern_ship_uniforms_.material_layers,
                   static_cast<GLsizei>(material_layers.size()),
                   material_layers.data());
      glUniform1i(modern_ship_uniforms_.light_count,
                  static_cast<GLint>(light_count));
      if (light_count > 0U) {
        glUniform4fv(modern_ship_uniforms_.light_position_radius,
                     static_cast<GLsizei>(light_count),
                     glm::value_ptr(light_position_radius[0]));
        glUniform4fv(modern_ship_uniforms_.light_color_intensity,
                     static_cast<GLsizei>(light_count),
                     glm::value_ptr(light_color_intensity[0]));
        glUniform4fv(modern_ship_uniforms_.light_zone_min_spill,
                     static_cast<GLsizei>(light_count),
                     glm::value_ptr(light_zone_min_spill[0]));
        glUniform4fv(modern_ship_uniforms_.light_zone_max_seed,
                     static_cast<GLsizei>(light_count),
                     glm::value_ptr(light_zone_max_seed[0]));
        glUniform4fv(modern_ship_uniforms_.light_doorways,
                     static_cast<GLsizei>(light_count),
                     glm::value_ptr(light_doorways[0]));
      }
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, shadow_map_);
      glActiveTexture(GL_TEXTURE7);
      glBindTexture(GL_TEXTURE_2D, shadow_map_far_);
      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D_ARRAY, modern_material_albedo_texture_);
      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_2D_ARRAY,
                    modern_material_normal_height_texture_);
      glActiveTexture(GL_TEXTURE6);
      glBindTexture(GL_TEXTURE_2D_ARRAY, modern_material_orm_emission_texture_);
    } else {
      glUseProgram(world_program_);
      glUniformMatrix4fv(world_uniforms_.model, 1, GL_FALSE,
                         glm::value_ptr(ship.model_matrix));
    }
    glBindVertexArray(active_ship_gpu_mesh.vao);
    glDrawElements(GL_TRIANGLES, active_ship_gpu_mesh.opaque_index_count,
                   GL_UNSIGNED_INT, nullptr);
    record_triangle_draw(active_ship_gpu_mesh.opaque_index_count);
    glUseProgram(world_program_);
    glUniformMatrix4fv(world_uniforms_.model, 1, GL_FALSE,
                       glm::value_ptr(identity_model));
  }
  end_gpu_pass(GpuTimedPass::Opaque);

  begin_gpu_pass(GpuTimedPass::Entities);
  draw_item_drops(item_drops, view_projection, light_view_projection,
                  light_view_projection_far, shadow_cascade_count,
                  shadow_split_distance, shadow_transition_width,
                  inverse_view_projection, eye, camera_forward, environment,
                  sun_visible,
                  std::span<const glm::vec4> {
                      backrooms_flicker_uniforms.data(),
                      backrooms_flicker_count,
                  },
                  backrooms_fog_range,
                  backrooms_flashlight_strength);
  draw_creatures(
      creatures, crew, old_guard, view_projection, light_view_projection,
      light_view_projection_far, shadow_cascade_count, shadow_split_distance,
      shadow_transition_width, eye, camera_forward, environment,
      selected_hotbar_emits_local_light(hotbar),
      std::span<const glm::vec4> {
          backrooms_flicker_uniforms.data(),
          backrooms_flicker_count,
      },
      backrooms_flashlight_strength,
      super_vision_strength);
  end_gpu_pass(GpuTimedPass::Entities);

  begin_gpu_pass(GpuTimedPass::Sky);
  if (!backrooms_interior) {
    draw_sky(inverse_sky_view_projection, eye, environment, quality_settings,
             maritime_horizon_enabled, maritime_submersion, ocean,
             ocean_wave_uniforms, ocean_phase_uniforms);
  }
  end_gpu_pass(GpuTimedPass::Sky);

  if (has_visible_water) {
    begin_gpu_pass(GpuTimedPass::WaterResolve);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, water_scene_framebuffer_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, final_target_framebuffer);
    glBlitFramebuffer(0, 0, render_width, render_height, 0, 0, render_width,
                      render_height, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT,
                      GL_NEAREST);
    end_gpu_pass(GpuTimedPass::WaterResolve);

    glBindFramebuffer(GL_FRAMEBUFFER, final_target_framebuffer);
    glViewport(0, 0, render_width, render_height);
    const auto water_scene_bindings = select_scene_sampler_bindings(
        true, scene_fallback_color_texture_, scene_fallback_depth_texture_,
        water_scene_color_texture_, water_scene_depth_texture_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, water_scene_bindings.color_texture);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, water_scene_bindings.depth_texture);
    begin_gpu_pass(GpuTimedPass::WaterSurface);
    const auto modern_water_enabled =
        options_.visual_pipeline == VisualPipeline::ModernStylized &&
        (maritime_horizon_enabled || poolrooms_interior) &&
        modern_water_program_ != 0;
    if (modern_water_enabled) {
      glUseProgram(modern_water_program_);
      glUniformMatrix4fv(modern_water_uniforms_.model, 1, GL_FALSE,
                         glm::value_ptr(identity_model));
      glUniformMatrix4fv(modern_water_uniforms_.view_projection, 1, GL_FALSE,
                         glm::value_ptr(view_projection));
      glUniformMatrix4fv(modern_water_uniforms_.inverse_view_projection, 1,
                         GL_FALSE, glm::value_ptr(inverse_view_projection));
      glUniform3fv(modern_water_uniforms_.camera_position, 1,
                   glm::value_ptr(eye));
      glUniform3fv(modern_water_uniforms_.camera_forward, 1,
                   glm::value_ptr(camera_forward));
      glUniform3fv(modern_water_uniforms_.sun_direction, 1,
                   glm::value_ptr(environment.sun_direction));
      glUniform3fv(modern_water_uniforms_.sun_color, 1,
                   glm::value_ptr(environment.sun_color));
      glUniform3fv(modern_water_uniforms_.moon_disk_color, 1,
                   glm::value_ptr(environment.moon_disk_color));
      glUniform3fv(modern_water_uniforms_.ambient_color, 1,
                   glm::value_ptr(environment.ambient_color));
      glUniform3fv(modern_water_uniforms_.block_light_color, 1,
                   glm::value_ptr(environment.block_light_color));
      glUniform3fv(modern_water_uniforms_.fog_color, 1,
                   glm::value_ptr(environment.fog_color));
      glUniform3fv(modern_water_uniforms_.distant_fog_color, 1,
                   glm::value_ptr(environment.distant_fog_color));
      glUniform3fv(modern_water_uniforms_.horizon_glow_color, 1,
                   glm::value_ptr(environment.horizon_glow_color));
      glUniform3fv(modern_water_uniforms_.night_tint_color, 1,
                   glm::value_ptr(environment.night_tint_color));
      glUniform3fv(modern_water_uniforms_.sky_zenith_color, 1,
                   glm::value_ptr(environment.sky_zenith_color));
      glUniform3fv(modern_water_uniforms_.sky_horizon_color, 1,
                   glm::value_ptr(environment.sky_horizon_color));
      glUniform1f(modern_water_uniforms_.daylight_factor,
                  environment.daylight_factor);
      glUniform1f(modern_water_uniforms_.sun_visibility,
                  sun_visible ? 1.0F : 0.0F);
      glUniform1f(modern_water_uniforms_.cloud_intensity,
                  environment.cloud_intensity);
      glUniform1f(modern_water_uniforms_.overcast_intensity,
                  environment.overcast_intensity);
      glUniform1f(modern_water_uniforms_.precipitation_intensity,
                  environment.precipitation_intensity);
      glUniform1f(modern_water_uniforms_.storm_intensity,
                  environment.storm_intensity);
      glUniform1f(modern_water_uniforms_.lightning_intensity,
                  environment.lightning_intensity);
      glUniform4fv(modern_water_uniforms_.ocean_waves,
                   static_cast<GLsizei>(ocean_wave_uniforms.size()),
                   glm::value_ptr(ocean_wave_uniforms.front()));
      glUniform2fv(modern_water_uniforms_.ocean_wave_phases,
                   static_cast<GLsizei>(ocean_phase_uniforms.size()),
                   glm::value_ptr(ocean_phase_uniforms.front()));
      glUniform1i(modern_water_uniforms_.ocean_wave_count,
                  poolrooms_interior
                      ? 0
                      : std::clamp(
                            quality_settings.ocean_wave_count, 1,
                            static_cast<int>(kOceanMaxWaveCount)));
      glUniform1f(modern_water_uniforms_.ocean_foam_threshold,
                  ocean.foam_threshold);
      glUniform1f(modern_water_uniforms_.ocean_detail_strength,
                  poolrooms_interior
                      ? 0.0F
                      : ocean.detail_strength *
                            quality_settings.ocean_detail_scale);
      glUniform1f(modern_water_uniforms_.ocean_detail_phase,
                  ocean.detail_phase);
      glUniform1f(modern_water_uniforms_.water_animation_time,
                  environment.weather_time_seconds);
      glUniform1f(modern_water_uniforms_.ocean_severity,
                  poolrooms_interior ? 0.0F : ocean.severity);
      glUniform1f(modern_water_uniforms_.ocean_tempest_factor,
                  poolrooms_interior ? 0.0F : ocean.tempest_factor);
      glUniform1f(modern_water_uniforms_.ocean_open_sea,
                  !poolrooms_interior &&
                          ocean_profile == OceanSurfaceProfile::OpenSea
                      ? 1.0F
                      : 0.0F);
      const auto water_surface_detail =
          poolrooms_interior
              ? std::min(
                    quality_settings.water_surface_detail,
                    0.42F)
              : quality_settings.water_surface_detail;
      glUniform1f(modern_water_uniforms_.water_surface_detail,
                  water_surface_detail);
      glUniform1i(
          modern_water_uniforms_.water_detail_samples,
          water_detail_sample_count(water_surface_detail));
      constexpr auto clear_water_definition =
          visual_material_definition(VisualMaterialId::ClearWater);
      const auto has_water_material =
          modern_material_normal_height_texture_ != 0 &&
          clear_water_definition.pack_layer != kInvalidVisualMaterialLayer &&
          clear_water_definition.pack_layer < material_pack_layers_;
      glUniform1i(modern_water_uniforms_.has_water_material,
                  has_water_material ? 1 : 0);
      glUniform1f(modern_water_uniforms_.water_normal_layer,
                  static_cast<float>(clear_water_definition.pack_layer));
      glUniform1i(modern_water_uniforms_.scene_color, 2);
      glUniform1i(modern_water_uniforms_.scene_depth, 3);
      glUniform1i(modern_water_uniforms_.material_normal_height, 5);
      glUniform1i(modern_water_uniforms_.maritime_horizon_enabled,
                  maritime_horizon_enabled ? 1 : 0);
      glUniform2f(modern_water_uniforms_.maritime_water_blend_range,
                  maritime_water_blend_range.start_distance,
                  maritime_water_blend_range.end_distance);
      glUniform2f(modern_water_uniforms_.maritime_far_fog_range,
                  maritime_fog_range.start_distance,
                  maritime_fog_range.end_distance);
      glUniform1f(modern_water_uniforms_.maritime_sea_level,
                  static_cast<float>(kSeaLevel + 1));
      glUniform1i(modern_water_uniforms_.enclosed_interior,
                  environment.enclosed_interior ? 1 : 0);
      glUniform1f(modern_water_uniforms_.interior_visibility_floor,
                  environment.interior_visibility_floor);
      glUniform1i(modern_water_uniforms_.poolrooms_interior,
                  poolrooms_interior ? 1 : 0);
      glUniform1i(
          modern_water_uniforms_.backrooms_flicker_count,
          static_cast<GLint>(backrooms_flicker_count));
      if (backrooms_flicker_count > 0U) {
        glUniform4fv(
            modern_water_uniforms_.backrooms_flicker_lights,
            static_cast<GLsizei>(backrooms_flicker_count),
            glm::value_ptr(backrooms_flicker_uniforms.front()));
      }
      glUniform1f(
          modern_water_uniforms_.backrooms_flashlight_intensity,
          backrooms_flashlight_strength);
      glUniform2f(
          modern_water_uniforms_.interior_fog_range,
          backrooms_fog_range.start_distance,
          backrooms_fog_range.end_distance);
      upload_modern_water_ship_protection(ship);
      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_2D_ARRAY,
                    has_water_material ? modern_material_normal_height_texture_
                                       : 0U);
    } else {
      glUseProgram(world_program_);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, atlas_texture_);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, shadow_map_);
    }
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);

    for (const auto &visible_chunk : visible_chunks) {
      if (visible_chunk.mesh->water_index_count == 0) {
        continue;
      }
      glBindVertexArray(visible_chunk.mesh->water_vao);
      glDrawElements(GL_TRIANGLES, visible_chunk.mesh->water_index_count,
                     GL_UNSIGNED_INT, nullptr);
      record_triangle_draw(visible_chunk.mesh->water_index_count);
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    end_gpu_pass(GpuTimedPass::WaterSurface);
  }

  begin_gpu_pass(GpuTimedPass::TransparentWeather);

  // Le verre est composé après l'eau. Il n'écrit volontairement pas dans le
  // depth buffer ; lorsqu'il était dessiné avant l'eau, l'écriture de
  // profondeur de cette dernière pouvait l'effacer alors qu'il se trouvait
  // pourtant devant la surface.
  if (options_.visual_pipeline == VisualPipeline::ModernStylized &&
      modern_architecture_program_ != 0) {
    const auto has_transparent_architecture = std::any_of(
        visible_chunks.begin(), visible_chunks.end(),
        [](const VisibleChunk &visible_chunk) {
          return visible_chunk.mesh->architecture_transparent_index_count > 0;
        });
    if (has_transparent_architecture) {
      bind_modern_surface_program(modern_architecture_program_,
                                  modern_architecture_uniforms_, 8.0F);
      glEnable(GL_DEPTH_TEST);
      glDepthMask(GL_FALSE);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);

      // Le tri reste effectué du chunk le plus lointain au plus proche.
      // Cela stabilise la majorité des bâtiments sans coût de tri par
      // triangle à chaque frame.
      for (auto iterator = visible_chunks.rbegin();
           iterator != visible_chunks.rend(); ++iterator) {
        const auto *mesh = iterator->mesh;
        if (mesh->architecture_transparent_index_count == 0) {
          continue;
        }
        glBindVertexArray(mesh->architecture_vao);
        glDrawElements(
            GL_TRIANGLES, mesh->architecture_transparent_index_count,
            GL_UNSIGNED_INT,
            reinterpret_cast<const void *>(static_cast<std::uintptr_t>(
                mesh->architecture_transparent_index_offset_bytes)));
        record_triangle_draw(mesh->architecture_transparent_index_count);
      }

      glDepthMask(GL_TRUE);
      glDisable(GL_BLEND);
    }
  }

  draw_precipitation(view_projection, inverse_view, eye, environment, ocean,
                     ship, quality_settings, frame_stats);
  draw_old_guard_effects(old_guard_flashes, old_guard_smoke, view_projection,
                         inverse_view, eye);
  draw_old_guard_effects(std::span<const OldGuardMuzzleFlashInstance>{},
                         player_musket_smoke, view_projection, inverse_view,
                         eye);
  end_gpu_pass(GpuTimedPass::TransparentWeather);

  if (!environment.suppress_gameplay_hud) {
    draw_block_break_overlay(world, player);
  }

  if (!menu_preview_visible &&
      !environment.suppress_gameplay_hud) {
    const auto viewmodel_pose = draw_player_viewmodel(
        player, resolve_viewmodel_held_item(inventory_menu, hotbar),
        player_musket, viewmodel_projection * view, light_view_projection, eye,
        environment);
    draw_old_guard_effects(
        player_musket_flashes, std::span<const OldGuardSmokeInstance>{},
        viewmodel_projection * view, inverse_view, eye, true, &viewmodel_pose);
  }

  auto camera_weather_exposure = 1.0F;
  if (ship_protection_is_renderable(ship)) {
    const auto local_eye = glm::vec3{
        glm::inverse(ship.model_matrix) * glm::vec4{eye, 1.0F},
    };
    if (ship.blueprint->protection_profile.shelters_from_weather_local(
            local_eye)) {
      camera_weather_exposure = 0.0F;
    }
  }
  begin_gpu_pass(GpuTimedPass::PostProcess);
  if (menu_preview_visible) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    run_menu_background_pass(render_width, render_height, environment.exposure);
  } else if (optional_post_process_enabled || modern_output_resolve_required ||
             marlow_drowning_amount > 0.0F) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    run_post_process(environment, camera_weather_exposure, maritime_submersion,
                     render_width, render_height, projection_far_distance,
                     optional_post_process_enabled);
  } else if (has_visible_water) {
    // Le pipeline Legacy conserve son comportement historique. Le pipeline
    // moderne passe toujours par run_post_process afin de convertir sa
    // cible HDR linéaire vers l'écran SDR, même lorsque les effets sont
    // désactivés dans les options.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, scene_framebuffer_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, render_width, render_height, 0, 0, render_width,
                      render_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  } else {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  end_gpu_pass(GpuTimedPass::PostProcess);

  begin_gpu_pass(GpuTimedPass::Ui);
  if (backrooms_jack_render_view_.jumpscare) {
    // Je place Jack au-dessus de toute interface : le screamer ne doit pas
    // etre adouci par un menu, une batterie ou une boite de dialogue.
    draw_backrooms_jack_screamer(
        backrooms_jack_render_view_,
        environment.weather_time_seconds,
        width,
        height);
  } else if (backrooms_marlow_result_.render.phase ==
             BackroomsMarlowPhase::Screamer) {
    draw_backrooms_marlow_screamer(
        environment.weather_time_seconds,
        width,
        height);
  } else if (main_menu.visible) {
    draw_main_menu(main_menu, width, height);
  } else if (save_slot_menu.visible) {
    draw_save_slot_menu(save_slot_menu, width, height);
  } else if (options_menu.visible) {
    draw_options_menu(options_menu, width, height);
  } else if (death_screen.visible) {
    draw_death_screen(death_screen, width, height);
  } else if (progression_menu_view_.visible) {
    draw_progression_menu(progression_menu_view_, progression_build_view_,
                          width, height);
  } else if (pause_menu.visible) {
    draw_pause_menu(pause_menu, width, height);
  } else if (inventory_menu.visible) {
    draw_inventory_menu(inventory_menu, hotbar, width, height);
  } else if (
      backrooms_flashlight.visible &&
      !command_console.visible) {
    // Le HUD général est volontairement supprimé dans les Backrooms. Je
    // conserve uniquement l'information indispensable pour gérer la batterie.
    draw_backrooms_flashlight_hud(
        backrooms_flashlight,
        width,
        height);
  } else if (!environment.suppress_gameplay_hud) {
    if (!issou_hud_snapshot_.results_visible) {
      draw_hotbar(player, hotbar, progression, environment, width, height);
      draw_progression_ability_hud(progression_build_view_, width, height);
      draw_maritime_hud(maritime_hud, width, height);
      if (player_musket.active) {
        draw_musket_hud(player_musket, width, height);
      } else {
        draw_crosshair();
      }
      draw_gameplay_announcement(gameplay_announcement, width, height);
    }
    draw_issou_legendary_hud(width, height);
  }
  if (confirm_dialog.visible &&
      !backrooms_jack_render_view_.jumpscare &&
      backrooms_marlow_result_.render.phase !=
          BackroomsMarlowPhase::Screamer) {
    draw_confirm_dialog(confirm_dialog, width, height);
  }
  if (!backrooms_jack_render_view_.jumpscare) {
    draw_command_console(command_console, width, height);
  }
  end_gpu_pass(GpuTimedPass::Ui);
  end_gpu_frame();
  frame_stats.world_ms =
      std::chrono::duration<double, std::milli>(clock::now() - world_start)
          .count();
  frame_stats.draw_calls = frame_draw_calls_;
  frame_stats.triangles = frame_triangles_;
  frame_stats.uploaded_bytes = frame_uploaded_bytes_;
  if (options_.collect_detailed_stats) {
    frame_stats.gpu_buffer_bytes = estimate_gpu_buffer_bytes();
    frame_stats.gpu_texture_bytes = estimate_gpu_texture_bytes();
  }
  last_frame_stats_ = frame_stats;
  // Je rends la soumission volontairement mono-frame. Game doit fournir un
  // état frais au prochain tick, ce qui exclut tout fantôme après reset.
  clear_legendary_presentation();
}

void Renderer::render_loading_screen(std::string_view title,
                                     std::string_view detail, float progress,
                                     int width, int height) {
  LoadingScreenView view{};
  view.title = title;
  view.detail = detail;
  view.progress = progress;
  render_loading_screen(view, width, height);
}

void Renderer::render_loading_screen(const LoadingScreenView &view, int width,
                                     int height) {
  if (!initialized_ || width <= 0 || height <= 0 || hud_program_ == 0 ||
      hud_vao_ == 0 || hud_vbo_ == 0) {
    return;
  }

  const auto layout = make_loading_screen_layout(view.theme, width, height);
  const auto viewport_width = layout.viewport_width;
  const auto viewport_height = layout.viewport_height;
  const auto clamped_progress = std::isfinite(view.progress)
                                    ? std::clamp(view.progress, 0.0F, 1.0F)
                                    : 0.0F;
  const auto animation_phase =
      std::isfinite(view.animation_phase)
          ? view.animation_phase - std::floor(view.animation_phase)
          : 0.0F;
  const auto title_text =
      view.title.empty() ? std::string_view("VALCRAFT") : view.title;
  const auto detail_text = view.detail.empty()
                               ? std::string_view("CHARGEMENT DU MONDE")
                               : view.detail;
  auto &vertices = loading_vertices_scratch_;
  vertices.clear();
  if (vertices.capacity() < 32768U) {
    vertices.reserve(32768U);
  }

  const auto fitted_pixel_size = [](std::string_view text, float preferred,
                                    float maximum_width) {
    const auto unit_width = measure_pixel_text(text, 1.0F);
    if (unit_width <= 0.0F) {
      return std::max(1.0F, preferred);
    }
    return std::max(
        1.0F, std::floor(std::min(preferred, maximum_width / unit_width)));
  };
  const auto title_pixel_size = fitted_pixel_size(
      title_text, layout.title_pixel_size, layout.content_width - 24.0F);
  const auto detail_pixel_size = fitted_pixel_size(
      detail_text, layout.detail_pixel_size, layout.content_width - 24.0F);
  const auto percent_pixel_size = std::max(2.0F, layout.detail_pixel_size);

  const auto draw_text = [&](float x, float y, float pixel_size,
                             std::string_view text, const HudColor &color,
                             bool centered = false) {
    if (text.empty() || color[3] <= 0.0F) {
      return;
    }
    append_pixel_text(vertices, viewport_width, viewport_height, x + pixel_size,
                      y + pixel_size, pixel_size, text,
                      {0.0F, 0.0F, 0.0F, color[3] * 0.48F}, centered);
    append_pixel_text(vertices, viewport_width, viewport_height, x, y,
                      pixel_size, text, color, centered);
  };

  if (view.theme == LoadingScreenTheme::Maritime) {
    // Je construis le decor maritime avec des primitives deja presentes dans le
    // HUD.
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             0.0F, viewport_width, viewport_height,
                             {0.024F, 0.094F, 0.153F, 1.0F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             viewport_height * 0.18F, viewport_width,
                             viewport_height * 0.34F,
                             {0.035F, 0.176F, 0.250F, 0.52F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             layout.horizon_y - viewport_height * 0.06F,
                             viewport_width, viewport_height * 0.12F,
                             {0.36F, 0.53F, 0.55F, 0.12F});

    constexpr std::array<std::array<float, 2>, 14> kStars{{
        {{0.08F, 0.13F}},
        {{0.16F, 0.24F}},
        {{0.24F, 0.09F}},
        {{0.32F, 0.19F}},
        {{0.41F, 0.12F}},
        {{0.49F, 0.27F}},
        {{0.57F, 0.08F}},
        {{0.64F, 0.22F}},
        {{0.72F, 0.14F}},
        {{0.79F, 0.28F}},
        {{0.86F, 0.10F}},
        {{0.92F, 0.21F}},
        {{0.37F, 0.30F}},
        {{0.68F, 0.32F}},
    }};
    for (std::size_t index = 0; index < kStars.size(); ++index) {
      const auto twinkle =
          0.38F + 0.30F * std::sin(animation_phase * 6.2831853F +
                                   static_cast<float>(index) * 1.73F);
      const auto star_size = index % 3U == 0U ? 2.0F : 1.0F;
      append_hud_rect_top_left(
          vertices, viewport_width, viewport_height,
          kStars[index][0] * viewport_width, kStars[index][1] * viewport_height,
          star_size, star_size,
          {0.90F, 0.90F, 0.78F, std::clamp(twinkle, 0.10F, 0.72F)});
    }

    const auto moon_size = std::clamp(
        std::min(viewport_width, viewport_height) * 0.036F, 14.0F, 44.0F);
    const auto moon_x = viewport_width * 0.80F;
    const auto moon_y = viewport_height * 0.13F;
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, moon_x,
                             moon_y + moon_size * 0.18F, moon_size,
                             moon_size * 0.64F, {0.88F, 0.79F, 0.58F, 0.32F});
    append_hud_rect_top_left(
        vertices, viewport_width, viewport_height, moon_x + moon_size * 0.18F,
        moon_y, moon_size * 0.64F, moon_size, {0.94F, 0.86F, 0.64F, 0.32F});

    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             layout.horizon_y, viewport_width,
                             viewport_height - layout.horizon_y,
                             {0.025F, 0.225F, 0.290F, 1.0F});

    const auto ship_unit = std::clamp(
        std::min(viewport_width, viewport_height) * 0.012F, 3.0F, 12.0F);
    const auto ship_center_x = viewport_width * 0.5F;
    const auto ship_hull_y = layout.horizon_y - ship_unit * 0.25F;
    const auto silhouette = HudColor{0.015F, 0.050F, 0.070F, 0.98F};
    const auto sail_color = HudColor{
        0.008F,
        0.010F,
        0.016F,
        0.98F,
    };

    const auto mast_color = HudColor{
        0.86F,
        0.60F,
        0.16F,
        0.96F,
    };
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             ship_center_x - ship_unit * 6.8F, ship_hull_y,
                             ship_unit * 13.6F, ship_unit, silhouette);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             ship_center_x - ship_unit * 5.9F,
                             ship_hull_y + ship_unit, ship_unit * 11.8F,
                             ship_unit, silhouette);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             ship_center_x - ship_unit * 4.6F,
                             ship_hull_y + ship_unit * 2.0F, ship_unit * 9.2F,
                             ship_unit * 0.8F, silhouette);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             ship_center_x + ship_unit * 6.5F,
                             ship_hull_y - ship_unit * 0.42F, ship_unit * 2.0F,
                             ship_unit * 0.24F, mast_color);

    // Je sépare nettement les trois gréements pour garder la silhouette lisible
    // même en petite résolution.
    const auto append_square_rig = [&](float mast_offset, float mast_height,
                                       float yard_height, float sail_width,
                                       int sail_steps) {
      const auto mast_x = ship_center_x + ship_unit * mast_offset;
      const auto mast_width = std::max(1.0F, ship_unit * 0.28F);
      const auto yard_thickness = std::max(1.0F, ship_unit * 0.22F);
      for (int step = 0; step < sail_steps; ++step) {
        const auto progress =
            sail_steps > 1
                ? static_cast<float>(step) / static_cast<float>(sail_steps - 1)
                : 1.0F;
        const auto row_width =
            ship_unit * sail_width * (0.30F + progress * 0.70F);
        append_hud_rect_top_left(
            vertices, viewport_width, viewport_height,
            mast_x - row_width * 0.5F,
            ship_hull_y - ship_unit * yard_height +
                ship_unit * (0.45F + static_cast<float>(step) * 0.68F),
            row_width, ship_unit * 0.62F, sail_color);
      }
      append_hud_rect_top_left(
          vertices, viewport_width, viewport_height, mast_x - mast_width * 0.5F,
          ship_hull_y - ship_unit * mast_height, mast_width,
          ship_unit * (mast_height + 0.2F), mast_color);
      append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                               mast_x - ship_unit * sail_width * 0.58F,
                               ship_hull_y - ship_unit * yard_height,
                               ship_unit * sail_width * 1.16F, yard_thickness,
                               mast_color);
    };
    append_square_rig(-3.8F, 7.1F, 6.2F, 3.0F, 6);
    append_square_rig(0.0F, 9.2F, 8.0F, 3.7F, 8);
    append_square_rig(3.8F, 7.5F, 6.5F, 2.8F, 6);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             ship_center_x - ship_unit * 0.14F,
                             ship_hull_y - ship_unit * 9.7F, ship_unit * 2.4F,
                             ship_unit * 0.55F, {0.72F, 0.51F, 0.21F, 0.82F});

    const auto append_wave_layer = [&](float baseline, float amplitude,
                                       float segment_width, float phase_offset,
                                       const HudColor &color) {
      const auto segment_count =
          static_cast<int>(std::ceil(viewport_width / segment_width)) + 2;
      for (int segment = -1; segment < segment_count; ++segment) {
        const auto segment_value = static_cast<float>(segment);
        const auto wave = std::sin(segment_value * 0.72F +
                                   animation_phase * 6.2831853F + phase_offset);
        const auto y = baseline + wave * amplitude;
        append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                                 segment_value * segment_width, y,
                                 segment_width + 1.0F,
                                 std::max(0.0F, viewport_height - y), color);
      }
    };
    append_wave_layer(layout.horizon_y + ship_unit * 1.2F, ship_unit * 0.65F,
                      ship_unit * 2.4F, 1.8F, {0.025F, 0.300F, 0.355F, 0.96F});
    append_wave_layer(layout.horizon_y + ship_unit * 2.2F, ship_unit * 0.85F,
                      ship_unit * 2.8F, 3.9F, {0.018F, 0.235F, 0.310F, 0.98F});
  } else {
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             0.0F, viewport_width, viewport_height,
                             {0.04F, 0.05F, 0.06F, 1.0F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             0.0F, viewport_width, viewport_height * 0.36F,
                             {0.07F, 0.08F, 0.09F, 0.72F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             viewport_height * 0.64F, viewport_width,
                             viewport_height * 0.36F,
                             {0.02F, 0.03F, 0.04F, 0.84F});
  }

  const auto panel_palette = view.theme == LoadingScreenTheme::Maritime
                                   ? HudPanelPalette {
                                         {0.025F, 0.145F, 0.195F, 0.98F},
                                         {0.018F, 0.075F, 0.110F, 0.91F},
                                         {0.29F, 0.62F, 0.68F, 0.20F},
                                         {0.005F, 0.020F, 0.035F, 0.72F},
                                         {0.84F, 0.68F, 0.36F, 0.28F},
                                     }
                                   : make_stone_panel_palette();
  append_stylized_panel_top_left(
      vertices, viewport_width, viewport_height, layout.content_x,
      layout.panel_y, layout.content_width, layout.panel_height,
      view.theme == LoadingScreenTheme::Maritime ? 3.0F : 5.0F, panel_palette,
      false);

  draw_text(viewport_width * 0.5F, layout.title_y, title_pixel_size, title_text,
            view.theme == LoadingScreenTheme::Maritime
                ? HudColor{0.96F, 0.92F, 0.80F, 1.0F}
                : HudColor{0.98F, 0.95F, 0.88F, 1.0F},
            true);
  draw_text(viewport_width * 0.5F, layout.detail_y, detail_pixel_size,
            detail_text,
            view.theme == LoadingScreenTheme::Maritime
                ? HudColor{0.67F, 0.84F, 0.85F, 0.96F}
                : HudColor{0.84F, 0.86F, 0.90F, 0.96F},
            true);

  const auto track_border = std::clamp(layout.track_height * 0.22F, 3.0F, 7.0F);
  const auto track_inner_x = layout.track_x + track_border;
  const auto track_inner_y = layout.track_y + track_border;
  const auto track_inner_width =
      std::max(0.0F, layout.track_width - track_border * 2.0F);
  const auto track_inner_height =
      std::max(0.0F, layout.track_height - track_border * 2.0F);
  const auto fill_width = track_inner_width * clamped_progress;
  const auto accent = view.theme == LoadingScreenTheme::Maritime
                          ? HudColor{0.84F, 0.68F, 0.36F, 1.0F}
                          : HudColor{0.94F, 0.76F, 0.32F, 1.0F};

  append_hud_frame_top_left(vertices, viewport_width, viewport_height,
                            layout.track_x, layout.track_y, layout.track_width,
                            layout.track_height, track_border,
                            view.theme == LoadingScreenTheme::Maritime
                                ? HudColor{0.52F, 0.43F, 0.26F, 0.88F}
                                : HudColor{0.08F, 0.09F, 0.10F, 0.98F},
                            view.theme == LoadingScreenTheme::Maritime
                                ? HudColor{0.012F, 0.055F, 0.078F, 0.96F}
                                : HudColor{0.08F, 0.10F, 0.12F, 0.88F});
  if (fill_width > 0.0F) {
    append_hud_rect_top_left(
        vertices, viewport_width, viewport_height, track_inner_x, track_inner_y,
        fill_width, track_inner_height,
        hud_with_alpha(hud_scale_rgb(accent, 0.88F), 0.96F));
    append_hud_rect_top_left(
        vertices, viewport_width, viewport_height, track_inner_x, track_inner_y,
        fill_width, std::max(2.0F, track_inner_height * 0.36F),
        hud_with_alpha(hud_scale_rgb(accent, 1.18F), 0.42F));

    const auto shine_width =
        std::clamp(track_inner_width * 0.08F, 10.0F, 42.0F);
    const auto desired_shine_x =
        track_inner_x - shine_width +
        (track_inner_width + shine_width) * animation_phase;
    const auto shine_x = std::max(track_inner_x, desired_shine_x);
    const auto shine_right =
        std::min(track_inner_x + fill_width, desired_shine_x + shine_width);
    if (shine_right > shine_x) {
      append_hud_rect_top_left(
          vertices, viewport_width, viewport_height, shine_x, track_inner_y,
          shine_right - shine_x, track_inner_height,
          hud_with_alpha(hud_scale_rgb(accent, 1.28F), 0.42F));
    }
  }

  std::array<char, 8> percent_buffer{};
  const auto percent = static_cast<int>(std::lround(clamped_progress * 100.0F));
  const auto percent_result =
      std::to_chars(percent_buffer.data(),
                    percent_buffer.data() + percent_buffer.size() - 2, percent);
  auto percent_end = percent_result.ptr;
  if (percent_result.ec == std::errc{} &&
      percent_end + 2 <= percent_buffer.data() + percent_buffer.size()) {
    *percent_end = ' ';
    ++percent_end;
    *percent_end = '%';
    ++percent_end;
  }
  const auto percent_text = std::string_view(
      percent_buffer.data(),
      static_cast<std::size_t>(percent_end - percent_buffer.data()));
  draw_text(
      viewport_width * 0.5F,
      layout.track_y +
          std::max(1.0F,
                   (layout.track_height - percent_pixel_size * 7.0F) * 0.5F),
      percent_pixel_size, percent_text, {0.96F, 0.97F, 0.99F, 0.98F}, true);

  if (view.theme == LoadingScreenTheme::Maritime) {
    auto current_quote = view.current_quote;
    auto next_quote = view.next_quote;
    if (current_quote.line1.empty() && current_quote.line2.empty()) {
      const auto quotes = maritime_loading_quotes();
      if (!quotes.empty()) {
        current_quote = quotes.front();
      }
    }
    if (next_quote.line1.empty() && next_quote.line2.empty()) {
      next_quote = current_quote;
    }
    const auto quote_blend = std::isfinite(view.quote_blend)
                                 ? std::clamp(view.quote_blend, 0.0F, 1.0F)
                                 : 0.0F;
    const auto quote_pixel_size =
        std::min(fitted_pixel_size(current_quote.line1, layout.quote_pixel_size,
                                   layout.content_width - 36.0F),
                 fitted_pixel_size(current_quote.line2, layout.quote_pixel_size,
                                   layout.content_width - 36.0F));
    const auto draw_quote = [&](const LoadingQuoteView &quote, float alpha) {
      if (alpha <= 0.0F) {
        return;
      }
      const auto quote_color = HudColor{0.89F, 0.91F, 0.86F, 0.88F * alpha};
      draw_text(viewport_width * 0.5F, layout.quote_y, quote_pixel_size,
                quote.line1, quote_color, true);
      draw_text(viewport_width * 0.5F, layout.quote_y + quote_pixel_size * 9.0F,
                quote_pixel_size, quote.line2, quote_color, true);
      const auto author_pixel_size =
          fitted_pixel_size(quote.author, std::max(1.0F, quote_pixel_size),
                            layout.content_width - 36.0F);
      draw_text(viewport_width * 0.5F, layout.author_y, author_pixel_size,
                quote.author, {0.84F, 0.68F, 0.36F, 0.92F * alpha}, true);
    };
    draw_quote(current_quote, 1.0F - quote_blend);
    if (next_quote != current_quote) {
      draw_quote(next_quote, quote_blend);
    }
  } else {
    draw_text(viewport_width * 0.5F, layout.quote_y, detail_pixel_size,
              "PREPARATION EN COURS", {0.76F, 0.79F, 0.84F, 0.90F}, true);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, std::max(width, 1), std::max(height, 1));
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  if (view.theme == LoadingScreenTheme::Maritime) {
    glClearColor(0.024F, 0.094F, 0.153F, 1.0F);
  } else {
    glClearColor(0.04F, 0.05F, 0.06F, 1.0F);
  }
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(hud_program_);
  bind_hud_textures();
  upload_hud_vertices(vertices);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

auto Renderer::last_frame_stats() const noexcept -> const RendererFrameStats & {
  return last_frame_stats_;
}

void Renderer::submit_cpu_frame_time_sample(
    double active_frame_time_ms) noexcept {
  pending_cpu_frame_time_ms_ = active_frame_time_ms;
  pending_cpu_frame_time_valid_ =
      std::isfinite(active_frame_time_ms) && active_frame_time_ms > 0.0;
}

auto Renderer::material_pack_version() const noexcept -> std::uint16_t {
  return material_pack_version_;
}

auto Renderer::material_pack_checksum() const noexcept -> std::uint64_t {
  return material_pack_checksum_;
}

auto Renderer::last_initialization_error() const noexcept -> std::string_view {
  return last_initialization_error_;
}

void Renderer::create_gpu_timers() {
  gpu_timers_supported_ = GLAD_GL_VERSION_3_3 != 0 && glGenQueries != nullptr &&
                          glDeleteQueries != nullptr &&
                          glBeginQuery != nullptr && glEndQuery != nullptr &&
                          glGetQueryObjectiv != nullptr &&
                          glGetQueryObjectui64v != nullptr;
  if (!gpu_timers_supported_) {
    return;
  }

  for (auto &frame : gpu_query_frames_) {
    glGenQueries(static_cast<GLsizei>(frame.queries.size()),
                 frame.queries.data());
    frame.issued.fill(false);
    frame.pending = false;
  }
}

void Renderer::destroy_gpu_timers() {
  if (gpu_timers_supported_ && glDeleteQueries != nullptr) {
    for (auto &frame : gpu_query_frames_) {
      glDeleteQueries(static_cast<GLsizei>(frame.queries.size()),
                      frame.queries.data());
    }
  }
  gpu_query_frames_ = {};
  active_gpu_query_frame_ = -1;
  active_gpu_pass_ = -1;
  gpu_timers_supported_ = false;
}

void Renderer::begin_gpu_frame(RendererFrameStats &frame_stats) {
  active_gpu_query_frame_ = -1;
  active_gpu_pass_ = -1;
  if (!gpu_timers_supported_) {
    frame_stats.gpu = {};
    return;
  }

  RendererGpuTimings newest_resolved{};
  for (auto &query_frame : gpu_query_frames_) {
    if (!query_frame.pending) {
      continue;
    }

    auto all_available = true;
    for (std::size_t pass_index = 0; pass_index < kGpuTimedPassCount;
         ++pass_index) {
      if (!query_frame.issued[pass_index]) {
        continue;
      }
      GLint available = GL_FALSE;
      glGetQueryObjectiv(query_frame.queries[pass_index],
                         GL_QUERY_RESULT_AVAILABLE, &available);
      if (available != GL_TRUE) {
        all_available = false;
        break;
      }
    }
    if (!all_available) {
      continue;
    }

    RendererGpuTimings resolved{};
    resolved.valid = true;
    resolved.source_frame = query_frame.frame_index;
    const auto latency = gpu_frame_index_ >= query_frame.frame_index
                             ? gpu_frame_index_ - query_frame.frame_index
                             : 0U;
    resolved.latency_frames = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(latency, UINT32_MAX));
    for (std::size_t pass_index = 0; pass_index < kGpuTimedPassCount;
         ++pass_index) {
      if (!query_frame.issued[pass_index]) {
        continue;
      }

      GLuint64 elapsed_nanoseconds = 0;
      glGetQueryObjectui64v(query_frame.queries[pass_index], GL_QUERY_RESULT,
                            &elapsed_nanoseconds);
      const auto elapsed_ms =
          gpu_elapsed_nanoseconds_to_milliseconds(elapsed_nanoseconds);
      switch (static_cast<GpuTimedPass>(pass_index)) {
      case GpuTimedPass::Shadow:
        resolved.shadow_ms = elapsed_ms;
        break;
      case GpuTimedPass::Opaque:
        resolved.opaque_ms = elapsed_ms;
        break;
      case GpuTimedPass::Sky:
        resolved.sky_ms = elapsed_ms;
        break;
      case GpuTimedPass::Entities:
        resolved.entities_ms = elapsed_ms;
        break;
      case GpuTimedPass::Water:
        resolved.water_ms = elapsed_ms;
        break;
      case GpuTimedPass::PostProcess:
        resolved.post_process_ms = elapsed_ms;
        break;
      case GpuTimedPass::Ui:
        resolved.ui_ms = elapsed_ms;
        break;
      case GpuTimedPass::WaterResolve:
        resolved.water_resolve_ms = elapsed_ms;
        break;
      case GpuTimedPass::WaterSurface:
        resolved.water_surface_ms = elapsed_ms;
        break;
      case GpuTimedPass::TransparentWeather:
        resolved.transparent_weather_ms = elapsed_ms;
        break;
      case GpuTimedPass::Count:
        break;
      }
    }
    if (resolved.water_resolve_ms > 0.0 || resolved.water_surface_ms > 0.0 ||
        resolved.transparent_weather_ms > 0.0) {
      resolved.water_ms = resolved.water_resolve_ms +
                          resolved.water_surface_ms +
                          resolved.transparent_weather_ms;
    }

    if (!last_gpu_timings_.valid ||
        resolved.source_frame >= last_gpu_timings_.source_frame) {
      last_gpu_timings_ = resolved;
    }
    if (!newest_resolved.valid ||
        resolved.source_frame >= newest_resolved.source_frame) {
      newest_resolved = resolved;
    }
    query_frame.pending = false;
    query_frame.issued.fill(false);
  }

  // Je publie uniquement un résultat fraîchement résolu pour ne pas biaiser les
  // agrégats de télémétrie.
  frame_stats.gpu = newest_resolved;
  for (std::size_t frame_index = 0; frame_index < gpu_query_frames_.size();
       ++frame_index) {
    if (!gpu_query_frames_[frame_index].pending) {
      active_gpu_query_frame_ = static_cast<int>(frame_index);
      auto &query_frame = gpu_query_frames_[frame_index];
      query_frame.frame_index = gpu_frame_index_;
      query_frame.issued.fill(false);
      break;
    }
  }
}

void Renderer::end_gpu_frame() {
  if (active_gpu_pass_ >= 0) {
    glEndQuery(GL_TIME_ELAPSED);
    active_gpu_pass_ = -1;
  }
  if (active_gpu_query_frame_ >= 0) {
    auto &query_frame =
        gpu_query_frames_[static_cast<std::size_t>(active_gpu_query_frame_)];
    query_frame.pending =
        std::any_of(query_frame.issued.begin(), query_frame.issued.end(),
                    [](bool issued) { return issued; });
  }
  active_gpu_query_frame_ = -1;
  ++gpu_frame_index_;
}

void Renderer::begin_gpu_pass(GpuTimedPass pass) {
  if (active_gpu_query_frame_ < 0 || active_gpu_pass_ >= 0) {
    return;
  }
  const auto pass_index = static_cast<std::size_t>(pass);
  auto &query_frame =
      gpu_query_frames_[static_cast<std::size_t>(active_gpu_query_frame_)];
  glBeginQuery(GL_TIME_ELAPSED, query_frame.queries[pass_index]);
  query_frame.issued[pass_index] = true;
  active_gpu_pass_ = static_cast<int>(pass_index);
}

void Renderer::end_gpu_pass(GpuTimedPass pass) {
  if (active_gpu_pass_ != static_cast<int>(pass)) {
    return;
  }
  glEndQuery(GL_TIME_ELAPSED);
  active_gpu_pass_ = -1;
}

void Renderer::record_triangle_draw(GLsizei index_or_vertex_count,
                                    GLsizei instance_count) noexcept {
  ++frame_draw_calls_;
  if (index_or_vertex_count <= 0 || instance_count <= 0) {
    return;
  }
  const auto triangle_count =
      static_cast<std::uint64_t>(index_or_vertex_count / 3);
  frame_triangles_ +=
      triangle_count * static_cast<std::uint64_t>(instance_count);
}

void Renderer::record_draw_call() noexcept { ++frame_draw_calls_; }

auto Renderer::estimate_gpu_buffer_bytes() const noexcept -> std::uint64_t {
  auto total = std::uint64_t{0};
  const auto add_mesh = [&total](const GpuMesh &mesh) {
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(mesh.vertex_buffer_bytes, 0));
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(mesh.index_buffer_bytes, 0));
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(mesh.water_vertex_buffer_bytes, 0));
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(mesh.water_index_buffer_bytes, 0));
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(mesh.terrain_vertex_buffer_bytes, 0));
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(mesh.terrain_index_buffer_bytes, 0));
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(mesh.architecture_vertex_buffer_bytes, 0));
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(mesh.architecture_index_buffer_bytes, 0));
  };
  for (const auto &[coord, mesh] : gpu_meshes_) {
    static_cast<void>(coord);
    add_mesh(mesh);
  }
  add_mesh(block_break_overlay_mesh_);
  for (const auto &ship_gpu_mesh : ship_gpu_meshes_) {
    add_mesh(ship_gpu_mesh);
  }
  add_mesh(marine_decor_gpu_mesh_);
  add_mesh(ocean_life_gpu_mesh_);

  if (creature_vbo_ != 0) {
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(creature_template_vertex_buffer_bytes_, 0));
  }
  if (creature_ebo_ != 0) {
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(creature_template_index_buffer_bytes_, 0));
  }
  if (item_drop_vbo_ != 0) {
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(item_drop_template_vertex_buffer_bytes_, 0));
  }
  if (item_drop_ebo_ != 0) {
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(item_drop_template_index_buffer_bytes_, 0));
  }
  if (creature_instance_vbo_ != 0) {
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(creature_instance_buffer_bytes_, 0));
  }
  if (viewmodel_instance_vbo_ != 0) {
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(viewmodel_instance_buffer_bytes_, 0));
  }
  if (item_drop_instance_vbo_ != 0) {
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(item_drop_instance_buffer_bytes_, 0));
  }
  if (precipitation_vbo_ != 0) {
    total += sizeof(float) * 8U;
  }
  if (precipitation_instance_vbo_ != 0) {
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(precipitation_instance_buffer_bytes_, 0));
  }
  if (old_guard_effect_vbo_ != 0) {
    total += sizeof(float) * 8U;
  }
  if (old_guard_effect_instance_vbo_ != 0) {
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(old_guard_effect_instance_buffer_bytes_, 0));
  }
  if (sea_horizon_terrain_vbo_ != 0U) {
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(sea_horizon_terrain_vertex_buffer_bytes_, 0));
  }
  if (sea_horizon_terrain_ebo_ != 0U) {
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(sea_horizon_terrain_index_buffer_bytes_, 0));
  }
  if (hud_vbo_ != 0) {
    total += static_cast<std::uint64_t>(
        std::max<GLsizeiptr>(hud_vertex_buffer_bytes_, 0));
  }
  if (crosshair_vbo_ != 0) {
    total += sizeof(float) * 8U;
  }
  return total;
}

auto Renderer::estimate_gpu_texture_bytes() const noexcept -> std::uint64_t {
  auto total = std::uint64_t{0};
  const auto image_bytes = [](int width, int height,
                              std::uint64_t bytes_per_pixel) {
    return static_cast<std::uint64_t>(std::max(width, 0)) *
           static_cast<std::uint64_t>(std::max(height, 0)) * bytes_per_pixel;
  };
  if (atlas_texture_ != 0) {
    total += image_bytes(kBlockAtlasSize, kBlockAtlasSize, 4U);
  }
  if (msdf_font_texture_ != 0) {
    auto mip_texel_count = std::uint64_t{0U};
    auto mip_width = std::max(msdf_font_width_, 1U);
    auto mip_height = std::max(msdf_font_height_, 1U);
    for (std::uint32_t mip = 0U; mip < msdf_font_mips_; ++mip) {
      mip_texel_count += static_cast<std::uint64_t>(mip_width) *
                         static_cast<std::uint64_t>(mip_height);
      mip_width = std::max(mip_width / 2U, 1U);
      mip_height = std::max(mip_height / 2U, 1U);
    }
    total += mip_texel_count * 3U;
  }
  if (model_icon_texture_ != 0) {
    auto mip_texel_count = std::uint64_t{0U};
    auto mip_width = std::max<std::uint16_t>(model_icon_width_, 1U);
    auto mip_height = std::max<std::uint16_t>(model_icon_height_, 1U);
    for (std::uint16_t mip = 0U; mip < model_icon_mips_; ++mip) {
      mip_texel_count += static_cast<std::uint64_t>(mip_width) *
                         static_cast<std::uint64_t>(mip_height);
      mip_width = std::max<std::uint16_t>(mip_width / 2U, 1U);
      mip_height = std::max<std::uint16_t>(mip_height / 2U, 1U);
    }
    total +=
        mip_texel_count * static_cast<std::uint64_t>(model_icon_layers_) * 4U;
  }
  if (backrooms_jack_screamer_texture_ != 0) {
    total += image_bytes(
        static_cast<int>(backrooms_jack_screamer_width_),
        static_cast<int>(backrooms_jack_screamer_height_),
        4U);
  }
  if (backrooms_marlow_screamer_texture_ != 0) {
    total += image_bytes(
        static_cast<int>(backrooms_marlow_screamer_width_),
        static_cast<int>(backrooms_marlow_screamer_height_),
        4U);
  }
  if (modern_material_albedo_texture_ != 0 ||
      modern_material_normal_height_texture_ != 0 ||
      modern_material_orm_emission_texture_ != 0) {
    auto mip_texel_count = std::uint64_t{0U};
    auto mip_width = std::max<std::uint16_t>(material_pack_width_, 1U);
    auto mip_height = std::max<std::uint16_t>(material_pack_height_, 1U);
    for (std::uint16_t mip = 0U; mip < material_pack_mips_; ++mip) {
      mip_texel_count += static_cast<std::uint64_t>(mip_width) *
                         static_cast<std::uint64_t>(mip_height);
      mip_width = std::max<std::uint16_t>(mip_width / 2U, 1U);
      mip_height = std::max<std::uint16_t>(mip_height / 2U, 1U);
    }
    total += mip_texel_count *
             static_cast<std::uint64_t>(material_pack_layers_) * 4U * 3U;
  }
  if (accent_texture_ != 0) {
    total += image_bytes(kAccentAtlasSize, kAccentAtlasSize, 4U);
  }
  if (creature_atlas_texture_ != 0) {
    total += image_bytes(kCreatureAtlasSize, kCreatureAtlasSize, 4U);
  }
  if (player_atlas_texture_ != 0) {
    total += image_bytes(kPlayerAtlasSize, kPlayerAtlasSize, 4U);
  }
  if (shadow_map_ != 0) {
    const auto size =
        options_.shadows_enabled ? std::max(options_.shadow_map_size, 1) : 1;
    total += image_bytes(size, size, 4U);
  }
  if (shadow_map_far_ != 0) {
    const auto size =
        options_.shadows_enabled ? std::max(options_.shadow_map_size, 1) : 1;
    total += image_bytes(size, size, 4U);
  }
  if (scene_fallback_color_texture_ != 0) {
    total += 4U;
  }
  if (scene_fallback_depth_texture_ != 0) {
    total += 4U;
  }
  if (water_scene_color_texture_ != 0) {
    total += image_bytes(
        water_scene_target_width_, water_scene_target_height_,
        color_target_bytes_per_pixel(water_scene_color_internal_format_));
  }
  if (water_scene_depth_texture_ != 0) {
    total +=
        image_bytes(water_scene_target_width_, water_scene_target_height_, 4U);
  }
  if (scene_color_texture_ != 0) {
    total +=
        image_bytes(scene_target_width_, scene_target_height_,
                    color_target_bytes_per_pixel(scene_color_internal_format_));
  }
  if (scene_depth_texture_ != 0) {
    total += image_bytes(scene_target_width_, scene_target_height_, 4U);
  }
  if (glow_extract_texture_ != 0) {
    total +=
        image_bytes(glow_target_width_, glow_target_height_,
                    color_target_bytes_per_pixel(glow_color_internal_format_));
  }
  if (glow_ping_texture_ != 0) {
    total +=
        image_bytes(glow_target_width_, glow_target_height_,
                    color_target_bytes_per_pixel(glow_color_internal_format_));
  }
  return total;
}

void Renderer::drain_pending_world_meshes(World &world, std::size_t max_events,
                                          double max_ms) {
  RendererFrameStats ignored_stats{};
  sync_gpu_meshes(world, ignored_stats, max_events, max_ms);
}

void Renderer::begin_world_resource_reset() {
  if (world_resource_reset_progress_.active()) {
    return;
  }

  backrooms_terminal_fog_snapshot_ = {};
  backrooms_flicker_field_ = {};
  backrooms_flicker_cache_valid_ = false;
  backrooms_jack_interference_fixture_cache_.reset();

  // Je rends immediatement l'ancien monde invisible, puis je libere ses objets
  // GPU par tranches.
  const auto overlay_has_resources =
      block_break_overlay_mesh_.vao != 0U ||
      block_break_overlay_mesh_.vbo != 0U ||
      block_break_overlay_mesh_.ebo != 0U ||
      block_break_overlay_mesh_.revision != 0U ||
      block_break_overlay_mesh_.opaque_index_count > 0 ||
      block_break_overlay_mesh_.water_index_count > 0;
  const auto terrain_mesh_has_resources = [](const GpuMesh &mesh) noexcept {
    return mesh.terrain_vao != 0U || mesh.terrain_vbo != 0U ||
           mesh.terrain_ebo != 0U;
  };
  const auto marine_decor_has_resources =
      terrain_mesh_has_resources(marine_decor_gpu_mesh_);
  const auto ocean_life_has_resources =
      terrain_mesh_has_resources(ocean_life_gpu_mesh_);
  const auto required_queue_capacity = gpu_meshes_.size() +
                                       (overlay_has_resources ? 1U : 0U) +
                                       (marine_decor_has_resources ? 1U : 0U) +
                                       (ocean_life_has_resources ? 1U : 0U);
  world_resource_reset_queue_.clear();
  if (world_resource_reset_queue_.capacity() < required_queue_capacity) {
    world_resource_reset_queue_.reserve(required_queue_capacity);
  }
  for (auto &[coord, mesh] : gpu_meshes_) {
    static_cast<void>(coord);
    world_resource_reset_queue_.push_back(mesh);
    mesh = {};
  }
  gpu_meshes_.clear();
  visible_chunks_cache_.clear();
  shadow_chunks_cache_.clear();
  chunk_upload_scratch_.vertices.clear();
  chunk_upload_scratch_.indices.clear();
  chunk_upload_scratch_.water_vertices.clear();
  chunk_upload_scratch_.water_indices.clear();
  chunk_upload_scratch_.face_count = 0U;
  chunk_upload_scratch_.water_face_count = 0U;
  block_break_overlay_scratch_.vertices.clear();
  block_break_overlay_scratch_.indices.clear();
  block_break_overlay_scratch_.water_vertices.clear();
  block_break_overlay_scratch_.water_indices.clear();
  block_break_overlay_scratch_.face_count = 0U;
  block_break_overlay_scratch_.water_face_count = 0U;
  last_frame_stats_.uploaded_meshes = 0U;
  last_frame_stats_.visible_chunks = 0U;
  last_frame_stats_.shadow_chunks = 0U;
  last_frame_stats_.world_chunks = 0U;

  if (overlay_has_resources) {
    world_resource_reset_queue_.push_back(block_break_overlay_mesh_);
  }
  block_break_overlay_mesh_ = {};
  if (marine_decor_has_resources) {
    world_resource_reset_queue_.push_back(marine_decor_gpu_mesh_);
  }
  if (ocean_life_has_resources) {
    world_resource_reset_queue_.push_back(ocean_life_gpu_mesh_);
  }
  marine_decor_gpu_mesh_ = {};
  ocean_life_gpu_mesh_ = {};
  marine_decor_cache_.clear();
  marine_decor_instances_scratch_.clear();
  marine_visible_chunks_scratch_.clear();
  marine_requested_chunks_scratch_.clear();
  marine_decor_mesh_scratch_ = {};
  ocean_life_mesh_scratch_ = {};
  ocean_life_field_.clear();
  marine_visual_cache_valid_ = false;
  marine_visible_signature_ = 0U;
  world_resource_reset_progress_.begin(world_resource_reset_queue_.size(),
                                       false);
}

auto Renderer::process_world_resource_reset(std::size_t max_events,
                                            double max_ms) -> bool {
  using clock = std::chrono::steady_clock;

  if (!world_resource_reset_progress_.active()) {
    return true;
  }
  if (max_events == 0U) {
    return false;
  }

  const auto time_limited = std::isfinite(max_ms);
  const auto deadline =
      time_limited
          ? clock::now() +
                std::chrono::duration<double, std::milli>(std::max(0.0, max_ms))
          : clock::time_point::max();
  std::size_t processed_events = 0U;
  while (processed_events < max_events && clock::now() < deadline) {
    if (!world_resource_reset_queue_.empty()) {
      auto &mesh = world_resource_reset_queue_.back();
      if (gl_api_ready_) {
        destroy_gpu_mesh(mesh);
      }
      world_resource_reset_queue_.pop_back();
      world_resource_reset_progress_.consume_one();
      ++processed_events;
      continue;
    }

    break;
  }

  if (world_resource_reset_queue_.empty()) {
    world_resource_reset_progress_.finish();
  }
  return world_resource_reset_progress_.complete();
}

auto Renderer::pending_world_resource_reset_count() const noexcept
    -> std::size_t {
  return world_resource_reset_progress_.remaining();
}

void Renderer::reset_world_resources() {
  begin_world_resource_reset();
  while (
      !process_world_resource_reset(std::numeric_limits<std::size_t>::max(),
                                    std::numeric_limits<double>::infinity())) {
    // Je draine sans limite uniquement pendant l'arret complet du renderer.
  }
}

auto Renderer::world_mesh_uploaded(const ChunkCoord &coord,
                                   std::uint64_t revision) const noexcept
    -> bool {
  if (revision == 0U || world_resource_reset_progress_.active()) {
    return false;
  }
  const auto iterator = gpu_meshes_.find(coord);
  return iterator != gpu_meshes_.end() && iterator->second.revision == revision;
}

void Renderer::sync_gpu_meshes(World &world, RendererFrameStats &frame_stats,
                               std::size_t max_events, double max_ms) {
  using clock = std::chrono::steady_clock;

  if (max_events == 0 || world_resource_reset_progress_.active()) {
    return;
  }

  const auto time_limited = std::isfinite(max_ms);
  const auto deadline =
      time_limited
          ? clock::now() +
                std::chrono::duration<double, std::milli>(std::max(0.0, max_ms))
          : clock::time_point::max();
  std::size_t processed_events = 0;
  while (processed_events < max_events && clock::now() < deadline) {
    const auto unloads = world.consume_pending_gpu_unloads(1);
    if (!unloads.empty()) {
      const auto iterator = gpu_meshes_.find(unloads.front());
      if (iterator != gpu_meshes_.end()) {
        destroy_gpu_mesh(iterator->second);
        gpu_meshes_.erase(iterator);
      }
      ++processed_events;
      continue;
    }

    const auto uploads = world.consume_pending_gpu_uploads(1);
    if (uploads.empty()) {
      break;
    }

    const auto coord = uploads.front();
    const auto revision = world.mesh_revision(coord);
    const auto *section_meshes = world.section_meshes_for(coord);
    const auto *organic_section_meshes =
        options_.visual_pipeline == VisualPipeline::ModernStylized
            ? world.organic_section_meshes_for(coord)
            : nullptr;
    const auto *architectural_section_meshes =
        options_.visual_pipeline == VisualPipeline::ModernStylized
            ? world.architectural_section_meshes_for(coord)
            : nullptr;
    if (revision == 0 || section_meshes == nullptr) {
      ++processed_events;
      continue;
    }

    const auto existing_gpu_mesh = gpu_meshes_.find(coord);
    if (existing_gpu_mesh != gpu_meshes_.end() &&
        existing_gpu_mesh->second.revision == revision) {
      // Je conserve les meshes lors d'une reconfiguration et j'ignore les
      // ré-enqueues identiques.
      ++processed_events;
      continue;
    }

    merge_chunk_mesh_sections_into(*section_meshes, chunk_upload_scratch_);
    const OrganicTerrainMesh *organic_mesh = nullptr;
    if (organic_section_meshes != nullptr) {
      merge_organic_terrain_sections_into(*organic_section_meshes,
                                          terrain_upload_scratch_);
      organic_mesh = &terrain_upload_scratch_;
    }
    const ArchitecturalMesh *architectural_mesh = nullptr;
    if (architectural_section_meshes != nullptr) {
      merge_architectural_sections_into(*architectural_section_meshes,
                                        architecture_upload_scratch_);
      architectural_mesh = &architecture_upload_scratch_;
    }
    upload_mesh(coord, chunk_upload_scratch_, organic_mesh, architectural_mesh,
                revision);
    ++frame_stats.uploaded_meshes;
    ++processed_events;
  }
}

void Renderer::upload_mesh(const ChunkCoord &coord, const ChunkMeshData &mesh,
                           const OrganicTerrainMesh *terrain_mesh,
                           const ArchitecturalMesh *architectural_mesh,
                           std::uint64_t revision) {
  auto &gpu_mesh = gpu_meshes_[coord];
  ExactAabbAccumulator exact_bounds{};
  const auto include_vertices = [&exact_bounds](const auto &vertices) {
    for (const auto &vertex : vertices) {
      exact_bounds.add(vertex.x, vertex.y, vertex.z);
    }
  };
  include_vertices(mesh.vertices);
  include_vertices(mesh.water_vertices);
  if (terrain_mesh != nullptr) {
    include_vertices(terrain_mesh->vertices);
  }
  if (architectural_mesh != nullptr) {
    include_vertices(architectural_mesh->vertices);
  }

  // Je remplace la boite haute de 128 blocs par les limites reelles du
  // maillage. Le frustum et chaque cascade rejettent ainsi les chunks vides
  // en altitude sans toucher a la geometrie envoyee au GPU.
  upload_mesh_data(gpu_mesh, mesh, revision,
                   exact_bounds.bounds_or(make_chunk_bounds(coord)));
  if (terrain_mesh != nullptr) {
    upload_terrain_mesh_data(gpu_mesh, *terrain_mesh);
  } else {
    gpu_mesh.terrain_index_count = 0;
  }
  if (architectural_mesh != nullptr) {
    upload_architectural_mesh_data(gpu_mesh, *architectural_mesh);
  } else {
    gpu_mesh.architecture_opaque_index_count = 0;
    gpu_mesh.architecture_transparent_index_count = 0;
    gpu_mesh.architecture_transparent_index_offset_bytes = 0;
  }
}

void Renderer::upload_mesh_data(GpuMesh &gpu_mesh, const ChunkMeshData &mesh,
                                std::uint64_t revision,
                                const ChunkBounds &bounds) {
  gpu_mesh.revision = revision;
  gpu_mesh.bounds = bounds;
  gpu_mesh.opaque_index_count = 0;
  gpu_mesh.water_index_count = 0;

  if (mesh.total_index_count() == 0 || mesh.total_vertex_count() == 0) {
    return;
  }

  const auto opaque_vertex_bytes =
      static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(ChunkVertex));
  const auto opaque_index_bytes =
      static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t));
  if (opaque_vertex_bytes > 0 && opaque_index_bytes > 0) {
    if (gpu_mesh.vao == 0) {
      glGenVertexArrays(1, &gpu_mesh.vao);
      glGenBuffers(1, &gpu_mesh.vbo);
      glGenBuffers(1, &gpu_mesh.ebo);

      glBindVertexArray(gpu_mesh.vao);
      glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.vbo);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.ebo);

      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                            reinterpret_cast<void *>(offsetof(ChunkVertex, x)));
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                            reinterpret_cast<void *>(offsetof(ChunkVertex, u)));
      glEnableVertexAttribArray(2);
      glVertexAttribPointer(
          2, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
          reinterpret_cast<void *>(offsetof(ChunkVertex, nx)));
      glEnableVertexAttribArray(3);
      glVertexAttribPointer(
          3, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
          reinterpret_cast<void *>(offsetof(ChunkVertex, face_shade)));
      glEnableVertexAttribArray(4);
      glVertexAttribPointer(
          4, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
          reinterpret_cast<void *>(offsetof(ChunkVertex, ao)));
      glEnableVertexAttribArray(5);
      glVertexAttribPointer(
          5, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
          reinterpret_cast<void *>(offsetof(ChunkVertex, sky_light)));
      glEnableVertexAttribArray(6);
      glVertexAttribPointer(
          6, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
          reinterpret_cast<void *>(offsetof(ChunkVertex, block_light)));
      glEnableVertexAttribArray(7);
      glVertexAttribPointer(
          7, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
          reinterpret_cast<void *>(offsetof(ChunkVertex, material_class)));
      glEnableVertexAttribArray(8);
      glVertexAttribPointer(
          8, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
          reinterpret_cast<void *>(offsetof(ChunkVertex, wave_weight)));
    } else {
      glBindVertexArray(gpu_mesh.vao);
      glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.vbo);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.ebo);
    }

    if (gpu_mesh.vertex_buffer_bytes < opaque_vertex_bytes) {
      gpu_mesh.vertex_buffer_bytes =
          grow_buffer_capacity(gpu_mesh.vertex_buffer_bytes,
                               opaque_vertex_bytes, kInitialVertexBufferBytes);
    }
    if (gpu_mesh.index_buffer_bytes < opaque_index_bytes) {
      gpu_mesh.index_buffer_bytes =
          grow_buffer_capacity(gpu_mesh.index_buffer_bytes, opaque_index_bytes,
                               kInitialIndexBufferBytes);
    }

    orphan_bound_buffer(GL_ARRAY_BUFFER, gpu_mesh.vertex_buffer_bytes,
                        GL_DYNAMIC_DRAW);
    orphan_bound_buffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.index_buffer_bytes,
                        GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, opaque_vertex_bytes,
                    mesh.vertices.data());
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, opaque_index_bytes,
                    mesh.indices.data());
    gpu_mesh.opaque_index_count = static_cast<GLsizei>(mesh.indices.size());
    frame_uploaded_bytes_ += static_cast<std::uint64_t>(opaque_vertex_bytes);
    frame_uploaded_bytes_ += static_cast<std::uint64_t>(opaque_index_bytes);
  }

  const auto water_vertex_bytes =
      static_cast<GLsizeiptr>(mesh.water_vertices.size() * sizeof(WaterVertex));
  const auto water_index_bytes = static_cast<GLsizeiptr>(
      mesh.water_indices.size() * sizeof(std::uint32_t));
  if (water_vertex_bytes > 0 && water_index_bytes > 0) {
    if (gpu_mesh.water_vao == 0) {
      glGenVertexArrays(1, &gpu_mesh.water_vao);
      glGenBuffers(1, &gpu_mesh.water_vbo);
      glGenBuffers(1, &gpu_mesh.water_ebo);

      glBindVertexArray(gpu_mesh.water_vao);
      glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.water_vbo);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.water_ebo);

      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WaterVertex),
                            reinterpret_cast<void *>(offsetof(WaterVertex, x)));
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(WaterVertex),
                            reinterpret_cast<void *>(offsetof(WaterVertex, u)));
      glEnableVertexAttribArray(2);
      glVertexAttribPointer(
          2, 3, GL_BYTE, GL_TRUE, sizeof(WaterVertex),
          reinterpret_cast<void *>(offsetof(WaterVertex, nx)));
      glEnableVertexAttribArray(3);
      glVertexAttribPointer(
          3, 1, GL_HALF_FLOAT, GL_FALSE, sizeof(WaterVertex),
          reinterpret_cast<void *>(offsetof(WaterVertex, face_shade_half)));
      glEnableVertexAttribArray(4);
      glVertexAttribPointer(
          4, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(WaterVertex),
          reinterpret_cast<void *>(offsetof(WaterVertex, ao)));
      glEnableVertexAttribArray(5);
      glVertexAttribPointer(
          5, 1, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(WaterVertex),
          reinterpret_cast<void *>(offsetof(WaterVertex, sky_light)));
      glEnableVertexAttribArray(6);
      glVertexAttribPointer(
          6, 1, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(WaterVertex),
          reinterpret_cast<void *>(offsetof(WaterVertex, block_light)));
      glEnableVertexAttribArray(7);
      glVertexAttribPointer(
          7, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(WaterVertex),
          reinterpret_cast<void *>(offsetof(WaterVertex, material_class)));
      glEnableVertexAttribArray(8);
      glVertexAttribPointer(
          8, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(WaterVertex),
          reinterpret_cast<void *>(offsetof(WaterVertex, wave_weight)));
    } else {
      glBindVertexArray(gpu_mesh.water_vao);
      glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.water_vbo);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.water_ebo);
    }

    if (gpu_mesh.water_vertex_buffer_bytes < water_vertex_bytes) {
      gpu_mesh.water_vertex_buffer_bytes = grow_buffer_capacity(
          gpu_mesh.water_vertex_buffer_bytes, water_vertex_bytes,
          kInitialWaterVertexBufferBytes);
    }
    if (gpu_mesh.water_index_buffer_bytes < water_index_bytes) {
      gpu_mesh.water_index_buffer_bytes =
          grow_buffer_capacity(gpu_mesh.water_index_buffer_bytes,
                               water_index_bytes, kInitialIndexBufferBytes);
    }

    orphan_bound_buffer(GL_ARRAY_BUFFER, gpu_mesh.water_vertex_buffer_bytes,
                        GL_DYNAMIC_DRAW);
    orphan_bound_buffer(GL_ELEMENT_ARRAY_BUFFER,
                        gpu_mesh.water_index_buffer_bytes, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, water_vertex_bytes,
                    mesh.water_vertices.data());
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, water_index_bytes,
                    mesh.water_indices.data());
    gpu_mesh.water_index_count =
        static_cast<GLsizei>(mesh.water_indices.size());
    frame_uploaded_bytes_ += static_cast<std::uint64_t>(water_vertex_bytes);
    frame_uploaded_bytes_ += static_cast<std::uint64_t>(water_index_bytes);
  }
}

void Renderer::upload_terrain_mesh_data(GpuMesh &gpu_mesh,
                                        const OrganicTerrainMesh &mesh) {
  gpu_mesh.terrain_index_count = 0;
  if (mesh.vertices.empty() || mesh.indices.empty()) {
    return;
  }

  if (gpu_mesh.terrain_vao == 0) {
    glGenVertexArrays(1, &gpu_mesh.terrain_vao);
    glGenBuffers(1, &gpu_mesh.terrain_vbo);
    glGenBuffers(1, &gpu_mesh.terrain_ebo);

    glBindVertexArray(gpu_mesh.terrain_vao);
    glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.terrain_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.terrain_ebo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                          reinterpret_cast<void *>(offsetof(TerrainVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
        reinterpret_cast<void *>(offsetof(TerrainVertex, nx)));
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(
        2, 4, GL_UNSIGNED_BYTE, sizeof(TerrainVertex),
        reinterpret_cast<void *>(offsetof(TerrainVertex, primary_block_id)));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(
        3, 2, GL_UNSIGNED_BYTE, sizeof(TerrainVertex),
        reinterpret_cast<void *>(offsetof(TerrainVertex, sky_light)));
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(
        4, 1, GL_UNSIGNED_SHORT, sizeof(TerrainVertex),
        reinterpret_cast<void *>(offsetof(TerrainVertex, surface_flags)));
  } else {
    glBindVertexArray(gpu_mesh.terrain_vao);
    glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.terrain_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.terrain_ebo);
  }

  const auto vertex_bytes =
      static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(TerrainVertex));
  const auto index_bytes =
      static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t));
  if (gpu_mesh.terrain_vertex_buffer_bytes < vertex_bytes) {
    gpu_mesh.terrain_vertex_buffer_bytes =
        grow_buffer_capacity(gpu_mesh.terrain_vertex_buffer_bytes, vertex_bytes,
                             kInitialTerrainVertexBufferBytes);
  }
  if (gpu_mesh.terrain_index_buffer_bytes < index_bytes) {
    gpu_mesh.terrain_index_buffer_bytes =
        grow_buffer_capacity(gpu_mesh.terrain_index_buffer_bytes, index_bytes,
                             kInitialTerrainIndexBufferBytes);
  }

  orphan_bound_buffer(GL_ARRAY_BUFFER, gpu_mesh.terrain_vertex_buffer_bytes,
                      GL_DYNAMIC_DRAW);
  orphan_bound_buffer(GL_ELEMENT_ARRAY_BUFFER,
                      gpu_mesh.terrain_index_buffer_bytes, GL_DYNAMIC_DRAW);
  glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, mesh.vertices.data());
  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_bytes, mesh.indices.data());

  gpu_mesh.terrain_index_count = static_cast<GLsizei>(mesh.indices.size());
  frame_uploaded_bytes_ +=
      static_cast<std::uint64_t>(std::max<GLsizeiptr>(vertex_bytes, 0));
  frame_uploaded_bytes_ +=
      static_cast<std::uint64_t>(std::max<GLsizeiptr>(index_bytes, 0));
}

void Renderer::upload_architectural_mesh_data(GpuMesh &gpu_mesh,
                                              const ArchitecturalMesh &mesh) {

  gpu_mesh.architecture_opaque_index_count = 0;
  gpu_mesh.architecture_transparent_index_count = 0;
  gpu_mesh.architecture_transparent_index_offset_bytes = 0;
  if (mesh.vertices.empty() || mesh.indices.empty()) {
    return;
  }

  if (gpu_mesh.architecture_vao == 0U) {
    glGenVertexArrays(1, &gpu_mesh.architecture_vao);
    glGenBuffers(1, &gpu_mesh.architecture_vbo);
    glGenBuffers(1, &gpu_mesh.architecture_ebo);

    glBindVertexArray(gpu_mesh.architecture_vao);
    glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.architecture_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.architecture_ebo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE, sizeof(HardSurfaceVertex),
        reinterpret_cast<void *>(offsetof(HardSurfaceVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE, sizeof(HardSurfaceVertex),
        reinterpret_cast<void *>(offsetof(HardSurfaceVertex, nx)));
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(
        2, 2, GL_UNSIGNED_SHORT, sizeof(HardSurfaceVertex),
        reinterpret_cast<void *>(offsetof(HardSurfaceVertex, u_fixed)));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(
        3, 4, GL_UNSIGNED_BYTE, sizeof(HardSurfaceVertex),
        reinterpret_cast<void *>(offsetof(HardSurfaceVertex, material_block)));
  } else {
    glBindVertexArray(gpu_mesh.architecture_vao);
    glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.architecture_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.architecture_ebo);
  }

  auto &ordered_indices = architecture_indices_scratch_;
  const auto opaque_index_count = order_architectural_indices_for_render(
      mesh, ordered_indices, architecture_index_coverage_scratch_);

  const auto vertex_bytes =
      static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(HardSurfaceVertex));
  const auto index_bytes =
      static_cast<GLsizeiptr>(ordered_indices.size() * sizeof(std::uint32_t));
  if (gpu_mesh.architecture_vertex_buffer_bytes < vertex_bytes) {
    gpu_mesh.architecture_vertex_buffer_bytes =
        grow_buffer_capacity(gpu_mesh.architecture_vertex_buffer_bytes,
                             vertex_bytes, kInitialTerrainVertexBufferBytes);
  }
  if (gpu_mesh.architecture_index_buffer_bytes < index_bytes) {
    gpu_mesh.architecture_index_buffer_bytes =
        grow_buffer_capacity(gpu_mesh.architecture_index_buffer_bytes,
                             index_bytes, kInitialTerrainIndexBufferBytes);
  }

  orphan_bound_buffer(GL_ARRAY_BUFFER,
                      gpu_mesh.architecture_vertex_buffer_bytes,
                      GL_DYNAMIC_DRAW);
  orphan_bound_buffer(GL_ELEMENT_ARRAY_BUFFER,
                      gpu_mesh.architecture_index_buffer_bytes,
                      GL_DYNAMIC_DRAW);
  glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, mesh.vertices.data());
  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_bytes,
                  ordered_indices.data());

  gpu_mesh.architecture_opaque_index_count =
      static_cast<GLsizei>(opaque_index_count);
  gpu_mesh.architecture_transparent_index_count =
      static_cast<GLsizei>(ordered_indices.size() - opaque_index_count);
  gpu_mesh.architecture_transparent_index_offset_bytes =
      static_cast<GLsizeiptr>(opaque_index_count * sizeof(std::uint32_t));
  frame_uploaded_bytes_ +=
      static_cast<std::uint64_t>(std::max<GLsizeiptr>(vertex_bytes, 0));
  frame_uploaded_bytes_ +=
      static_cast<std::uint64_t>(std::max<GLsizeiptr>(index_bytes, 0));
}

namespace {

[[nodiscard]] constexpr auto ship_visual_variant(VisualPipeline pipeline,
                                                 StylizedShipLod lod) noexcept
    -> std::uint8_t {

  if (pipeline == VisualPipeline::LegacyVoxel) {
    return 0U;
  }
  return lod == StylizedShipLod::Near ? 1U : 2U;
}

[[nodiscard]] constexpr auto modern_ship_material_layers() noexcept
    -> std::array<float, 18> {

  const auto layer = [](VisualMaterialId material) constexpr {
    return static_cast<float>(visual_material_definition(material).pack_layer);
  };
  return {{
      layer(VisualMaterialId::ShipDarkHull),
      layer(VisualMaterialId::ShipDeckOak),
      layer(VisualMaterialId::ShipOiledOak),
      layer(VisualMaterialId::ShipLinen),
      layer(VisualMaterialId::ShipRope),
      layer(VisualMaterialId::ShipIron),
      layer(VisualMaterialId::ShipPatinatedBrass),
      layer(VisualMaterialId::ShipLantern),
      layer(VisualMaterialId::ShipGlass),
      layer(VisualMaterialId::ShipNavyTextile),
      layer(VisualMaterialId::ShipGold),
      layer(VisualMaterialId::ShipOiledOak),
      layer(VisualMaterialId::ShipLinen),
      layer(VisualMaterialId::ShipBurgundyTextile),
      layer(VisualMaterialId::ShipNavyTextile),
      layer(VisualMaterialId::ShipLeather),
      layer(VisualMaterialId::ShipPaper),
      layer(VisualMaterialId::ShipCeramic),
  }};
}

} // namespace

void Renderer::ensure_ship_mesh(const ShipRenderState &ship,
                                StylizedShipLod lod) {

  if (!ship.visible || ship.parts.empty() || ship.geometry_revision == 0U) {
    return;
  }

  if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
    if (ship_mesh_ready(ship)) {
      return;
    }

    // Je prépare le LOD demandé en premier puis son voisin dans un buffer
    // distinct. Une fois ce préchauffage terminé, franchir un seuil ne
    // déclenche plus ni génération CPU ni upload OpenGL.
    const std::array<StylizedShipLod, kStylizedShipLodCount> lods{{
        lod,
        lod == StylizedShipLod::Near ? StylizedShipLod::Far
                                     : StylizedShipLod::Near,
    }};
    for (const auto candidate_lod : lods) {
      if (ship_mesh_ready(ship, candidate_lod)) {
        continue;
      }
      const auto stylized_mesh = build_stylized_ship_mesh(ship, candidate_lod);
      if (stylized_mesh.empty()) {
        return;
      }
      const ChunkBounds local_bounds{
          stylized_mesh.metrics.bounds.min,
          stylized_mesh.metrics.bounds.max,
          (stylized_mesh.metrics.bounds.min +
           stylized_mesh.metrics.bounds.max) *
              0.5F,
      };
      upload_mesh_data(ship_gpu_meshes_[stylized_ship_lod_index(candidate_lod)],
                       stylized_mesh.mesh, ship.geometry_revision,
                       local_bounds);
      ship_mesh_cache_.remember(
          ship.geometry_revision, ship.parts.size(),
          ship_visual_variant(options_.visual_pipeline, candidate_lod));
    }
    return;
  }

  if (ship_mesh_ready(ship, StylizedShipLod::Near)) {
    return;
  }
  const auto legacy_parts =
      ship.blueprint != nullptr && !ship.blueprint->legacy_visual_parts.empty()
          ? ship.blueprint->legacy_visual_parts
          : ship.parts;
  // Je reconstruis le pipeline Legacy depuis sa photographie historique :
  // les meubles modernes ne peuvent ainsi modifier ni sa géométrie ni son
  // atlas.
  const auto legacy_mesh = build_ship_mesh_data(
      legacy_parts, {}, ShipMeshLightingModel::LegacyHistorical);
  (void)upload_prepared_ship_mesh(ship, legacy_mesh);
}

auto Renderer::upload_prepared_ship_mesh(const ShipRenderState &ship,
                                         const ChunkMeshData &mesh) -> bool {
  if (!initialized_ || !ship.visible || ship.parts.empty() ||
      ship.geometry_revision == 0U || mesh.vertices.empty() ||
      mesh.indices.empty()) {
    return false;
  }
  if (ship_mesh_ready(ship, StylizedShipLod::Near)) {
    return true;
  }

  // Je garde exclusivement l'upload OpenGL sur le thread du renderer. La
  // construction CPU peut ainsi etre preparee ailleurs sans partager d'etat GL.
  const ChunkBounds local_bounds{
      ship.local_bounds.min,
      ship.local_bounds.max,
      (ship.local_bounds.min + ship.local_bounds.max) * 0.5F,
  };
  upload_mesh_data(
      ship_gpu_meshes_[stylized_ship_lod_index(StylizedShipLod::Near)], mesh,
      ship.geometry_revision, local_bounds);
  active_ship_lod_ = StylizedShipLod::Near;
  ship_mesh_cache_.remember(
      ship.geometry_revision, ship.parts.size(),
      ship_visual_variant(options_.visual_pipeline, active_ship_lod_));
  return ship_mesh_ready(ship, active_ship_lod_);
}

auto Renderer::upload_prepared_ship_mesh(const ShipRenderState &ship,
                                         const ChunkMeshData &near_mesh,
                                         const ChunkMeshData &far_mesh)
    -> bool {

  if (!initialized_ ||
      options_.visual_pipeline != VisualPipeline::ModernStylized ||
      !ship.visible || ship.parts.empty() || ship.geometry_revision == 0U ||
      near_mesh.vertices.empty() || near_mesh.indices.empty() ||
      far_mesh.vertices.empty() || far_mesh.indices.empty()) {
    return false;
  }
  if (ship_mesh_ready(ship)) {
    return true;
  }

  // Je réalise les deux uploads pendant le chargement, exclusivement sur le
  // thread OpenGL. Les seuils de distance ne feront ensuite que sélectionner
  // l'un des deux VAO déjà résidents.
  const ChunkBounds local_bounds{
      ship.local_bounds.min,
      ship.local_bounds.max,
      (ship.local_bounds.min + ship.local_bounds.max) * 0.5F,
  };
  const std::array<std::pair<StylizedShipLod, const ChunkMeshData *>,
                   kStylizedShipLodCount>
      prepared_lods{{
          {StylizedShipLod::Near, &near_mesh},
          {StylizedShipLod::Far, &far_mesh},
      }};
  for (const auto &[lod, prepared_mesh] : prepared_lods) {
    if (ship_mesh_ready(ship, lod)) {
      continue;
    }
    upload_mesh_data(ship_gpu_meshes_[stylized_ship_lod_index(lod)],
                     *prepared_mesh, ship.geometry_revision, local_bounds);
    ship_mesh_cache_.remember(
        ship.geometry_revision, ship.parts.size(),
        ship_visual_variant(options_.visual_pipeline, lod));
  }
  active_ship_lod_ = StylizedShipLod::Near;
  return ship_mesh_ready(ship);
}

auto Renderer::prepare_ship_mesh(const ShipRenderState &ship) -> bool {
  if (!initialized_ || !ship.visible || ship.parts.empty() ||
      ship.geometry_revision == 0U) {
    return false;
  }
  ensure_ship_mesh(ship, StylizedShipLod::Near);
  return ship_mesh_ready(ship);
}

auto Renderer::ship_mesh_ready(const ShipRenderState &ship) const noexcept
    -> bool {
  if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
    return ship_mesh_ready(ship, StylizedShipLod::Near) &&
           ship_mesh_ready(ship, StylizedShipLod::Far);
  }
  return ship_mesh_ready(ship, StylizedShipLod::Near);
}

auto Renderer::ship_mesh_ready(const ShipRenderState &ship,
                               StylizedShipLod lod) const noexcept -> bool {

  const auto resident_lod =
      options_.visual_pipeline == VisualPipeline::ModernStylized
          ? lod
          : StylizedShipLod::Near;
  const auto &ship_gpu_mesh =
      ship_gpu_meshes_[stylized_ship_lod_index(resident_lod)];
  const auto gpu_ready = ship_gpu_mesh.vao != 0U && ship_gpu_mesh.vbo != 0U &&
                         ship_gpu_mesh.ebo != 0U &&
                         ship_gpu_mesh.opaque_index_count > 0 &&
                         ship_gpu_mesh.revision == ship.geometry_revision;
  return ship_mesh_cache_.ready(
      ship.geometry_revision, ship.parts.size(), initialized_, gpu_ready,
      ship_visual_variant(options_.visual_pipeline, lod));
}

void Renderer::upload_block_break_overlay_mesh(
    const World &world, const BlockBreakProgress &break_progress) {
  auto &mesh = block_break_overlay_scratch_;
  if (options_.visual_pipeline == VisualPipeline::ModernStylized &&
      is_organic_terrain_block(break_progress.block_id)) {
    build_organic_block_break_overlay_mesh_data_into(world, break_progress,
                                                     mesh);
  } else {
    build_block_break_overlay_mesh_data_into(break_progress, mesh);
  }
  auto &gpu_mesh = block_break_overlay_mesh_;
  gpu_mesh.opaque_index_count = 0;
  gpu_mesh.water_index_count = 0;

  if (mesh.indices.empty() || mesh.vertices.empty()) {
    return;
  }

  if (gpu_mesh.vao == 0) {
    glGenVertexArrays(1, &gpu_mesh.vao);
    glGenBuffers(1, &gpu_mesh.vbo);
    glGenBuffers(1, &gpu_mesh.ebo);

    glBindVertexArray(gpu_mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.ebo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                          reinterpret_cast<void *>(offsetof(ChunkVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                          reinterpret_cast<void *>(offsetof(ChunkVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                          reinterpret_cast<void *>(offsetof(ChunkVertex, nx)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(
        3, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
        reinterpret_cast<void *>(offsetof(ChunkVertex, face_shade)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                          reinterpret_cast<void *>(offsetof(ChunkVertex, ao)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(
        5, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
        reinterpret_cast<void *>(offsetof(ChunkVertex, sky_light)));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(
        6, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
        reinterpret_cast<void *>(offsetof(ChunkVertex, block_light)));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(
        7, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
        reinterpret_cast<void *>(offsetof(ChunkVertex, material_class)));
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(
        8, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
        reinterpret_cast<void *>(offsetof(ChunkVertex, wave_weight)));
  } else {
    glBindVertexArray(gpu_mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.ebo);
  }

  const auto vertex_bytes =
      static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(ChunkVertex));
  const auto index_bytes =
      static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t));

  if (gpu_mesh.vertex_buffer_bytes < vertex_bytes) {
    gpu_mesh.vertex_buffer_bytes = grow_buffer_capacity(
        gpu_mesh.vertex_buffer_bytes, vertex_bytes, kInitialVertexBufferBytes);
  }
  if (gpu_mesh.index_buffer_bytes < index_bytes) {
    gpu_mesh.index_buffer_bytes = grow_buffer_capacity(
        gpu_mesh.index_buffer_bytes, index_bytes, kInitialIndexBufferBytes);
  }

  orphan_bound_buffer(GL_ARRAY_BUFFER, gpu_mesh.vertex_buffer_bytes);
  orphan_bound_buffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.index_buffer_bytes);
  glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, mesh.vertices.data());
  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_bytes, mesh.indices.data());
  frame_uploaded_bytes_ +=
      static_cast<std::uint64_t>(std::max<GLsizeiptr>(vertex_bytes, 0));
  frame_uploaded_bytes_ +=
      static_cast<std::uint64_t>(std::max<GLsizeiptr>(index_bytes, 0));
  gpu_mesh.opaque_index_count = static_cast<GLsizei>(mesh.indices.size());
}

void Renderer::destroy_gpu_mesh(GpuMesh &mesh) {
  if (mesh.water_ebo != 0U) {
    glDeleteBuffers(1, &mesh.water_ebo);
    mesh.water_ebo = 0U;
  }
  if (mesh.water_vbo != 0U) {
    glDeleteBuffers(1, &mesh.water_vbo);
    mesh.water_vbo = 0U;
  }
  if (mesh.water_vao != 0U) {
    glDeleteVertexArrays(1, &mesh.water_vao);
    mesh.water_vao = 0U;
  }
  if (mesh.architecture_ebo != 0U) {
    glDeleteBuffers(1, &mesh.architecture_ebo);
    mesh.architecture_ebo = 0U;
  }
  if (mesh.architecture_vbo != 0U) {
    glDeleteBuffers(1, &mesh.architecture_vbo);
    mesh.architecture_vbo = 0U;
  }
  if (mesh.architecture_vao != 0U) {
    glDeleteVertexArrays(1, &mesh.architecture_vao);
    mesh.architecture_vao = 0U;
  }
  if (mesh.terrain_ebo != 0) {
    glDeleteBuffers(1, &mesh.terrain_ebo);
    mesh.terrain_ebo = 0;
  }
  if (mesh.terrain_vbo != 0) {
    glDeleteBuffers(1, &mesh.terrain_vbo);
    mesh.terrain_vbo = 0;
  }
  if (mesh.terrain_vao != 0) {
    glDeleteVertexArrays(1, &mesh.terrain_vao);
    mesh.terrain_vao = 0;
  }
  if (mesh.ebo != 0) {
    glDeleteBuffers(1, &mesh.ebo);
    mesh.ebo = 0;
  }
  if (mesh.vbo != 0) {
    glDeleteBuffers(1, &mesh.vbo);
    mesh.vbo = 0;
  }
  if (mesh.vao != 0) {
    glDeleteVertexArrays(1, &mesh.vao);
    mesh.vao = 0;
  }
  mesh.opaque_index_count = 0;
  mesh.water_index_count = 0;
  mesh.revision = 0;
  mesh.vertex_buffer_bytes = 0;
  mesh.index_buffer_bytes = 0;
  mesh.water_vertex_buffer_bytes = 0;
  mesh.water_index_buffer_bytes = 0;
  mesh.terrain_index_count = 0;
  mesh.terrain_vertex_buffer_bytes = 0;
  mesh.terrain_index_buffer_bytes = 0;
  mesh.architecture_opaque_index_count = 0;
  mesh.architecture_transparent_index_count = 0;
  mesh.architecture_transparent_index_offset_bytes = 0;
  mesh.architecture_vertex_buffer_bytes = 0;
  mesh.architecture_index_buffer_bytes = 0;
}

void Renderer::upload_world_ship_protection(const ShipRenderState &ship) {
  const auto enabled = ship_protection_is_renderable(ship);
  glUniform1i(world_uniforms_.ship_protection_enabled, enabled ? 1 : 0);
  if (!enabled) {
    return;
  }

  const auto inverse_model = glm::inverse(ship.model_matrix);
  const auto &profile = ship.blueprint->protection_profile;
  glUniformMatrix4fv(world_uniforms_.ship_inverse_model, 1, GL_FALSE,
                     glm::value_ptr(inverse_model));
  glUniform3fv(world_uniforms_.ship_bounds_min, 1,
               glm::value_ptr(ship.world_bounds.min));
  glUniform3fv(world_uniforms_.ship_bounds_max, 1,
               glm::value_ptr(ship.world_bounds.max));
  glUniform4f(world_uniforms_.ship_profile_longitudinal, profile.stern_z,
              profile.bow_z, profile.maximum_half_width,
              profile.boundary_margin);
  glUniform4f(world_uniforms_.ship_profile_taper, profile.stern_width_loss,
              profile.bow_width_loss, profile.stern_taper_exponent,
              profile.bow_taper_exponent);
  glUniform4f(world_uniforms_.ship_profile_heights, profile.lower_hull_min_y,
              profile.middle_hull_min_y, profile.upper_hull_min_y,
              profile.main_deck_top_y);
  glUniform4f(world_uniforms_.ship_profile_widths, profile.lower_width_inset,
              profile.middle_width_inset, profile.lower_minimum_half_width,
              profile.middle_minimum_half_width);
  glUniform1f(world_uniforms_.ship_sheltered_floor, profile.sheltered_floor_y);
}

void Renderer::upload_modern_water_ship_protection(
    const ShipRenderState &ship) {
  const auto enabled = ship_protection_is_renderable(ship);
  glUniform1i(modern_water_uniforms_.ship_protection_enabled, enabled ? 1 : 0);
  glUniform1f(modern_water_uniforms_.ship_speed,
              enabled ? sanitized_ship_speed(glm::length(glm::vec2{
                            ship.linear_velocity.x,
                            ship.linear_velocity.z,
                        }))
                      : 0.0F);
  if (!enabled) {
    return;
  }

  const auto inverse_model = glm::inverse(ship.model_matrix);
  const auto &profile = ship.blueprint->protection_profile;
  glUniformMatrix4fv(modern_water_uniforms_.ship_inverse_model, 1, GL_FALSE,
                     glm::value_ptr(inverse_model));
  glUniform3fv(modern_water_uniforms_.ship_bounds_min, 1,
               glm::value_ptr(ship.world_bounds.min));
  glUniform3fv(modern_water_uniforms_.ship_bounds_max, 1,
               glm::value_ptr(ship.world_bounds.max));
  glUniform4f(modern_water_uniforms_.ship_profile_longitudinal, profile.stern_z,
              profile.bow_z, profile.maximum_half_width,
              profile.boundary_margin);
  glUniform4f(modern_water_uniforms_.ship_profile_taper,
              profile.stern_width_loss, profile.bow_width_loss,
              profile.stern_taper_exponent, profile.bow_taper_exponent);
  glUniform4f(modern_water_uniforms_.ship_profile_heights,
              profile.lower_hull_min_y, profile.middle_hull_min_y,
              profile.upper_hull_min_y, profile.main_deck_top_y);
  glUniform4f(modern_water_uniforms_.ship_profile_widths,
              profile.lower_width_inset, profile.middle_width_inset,
              profile.lower_minimum_half_width,
              profile.middle_minimum_half_width);
}

void Renderer::upload_precipitation_ship_protection(
    const ShipRenderState &ship) {
  const auto enabled = ship_protection_is_renderable(ship);
  glUniform1i(precipitation_uniforms_.ship_protection_enabled, enabled ? 1 : 0);
  if (!enabled) {
    return;
  }

  const auto inverse_model = glm::inverse(ship.model_matrix);
  const auto &profile = ship.blueprint->protection_profile;
  glUniformMatrix4fv(precipitation_uniforms_.ship_inverse_model, 1, GL_FALSE,
                     glm::value_ptr(inverse_model));
  glUniform3fv(precipitation_uniforms_.ship_bounds_min, 1,
               glm::value_ptr(ship.world_bounds.min));
  glUniform3fv(precipitation_uniforms_.ship_bounds_max, 1,
               glm::value_ptr(ship.world_bounds.max));
  glUniform4f(precipitation_uniforms_.ship_profile_longitudinal,
              profile.stern_z, profile.bow_z, profile.maximum_half_width,
              profile.boundary_margin);
  glUniform4f(precipitation_uniforms_.ship_profile_taper,
              profile.stern_width_loss, profile.bow_width_loss,
              profile.stern_taper_exponent, profile.bow_taper_exponent);
  glUniform4f(precipitation_uniforms_.ship_profile_heights,
              profile.lower_hull_min_y, profile.middle_hull_min_y,
              profile.upper_hull_min_y, profile.main_deck_top_y);
  glUniform4f(precipitation_uniforms_.ship_profile_widths,
              profile.lower_width_inset, profile.middle_width_inset,
              profile.lower_minimum_half_width,
              profile.middle_minimum_half_width);
  glUniform1f(precipitation_uniforms_.ship_sheltered_floor,
              profile.sheltered_floor_y);
}

auto Renderer::compile_shader(GLenum type, const char *source) -> GLuint {
  const auto shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint success = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (success == GL_TRUE) {
    return shader;
  }

  GLint log_length = 0;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
  std::string log(static_cast<std::size_t>(std::max(log_length, 1)), '\0');
  glGetShaderInfoLog(shader, log_length, nullptr, log.data());
  glDeleteShader(shader);
  throw std::runtime_error("Shader compilation failed: " + log);
}

auto Renderer::link_program(GLuint vertex_shader, GLuint fragment_shader)
    -> GLuint {
  const auto program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glLinkProgram(program);

  GLint success = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (success == GL_TRUE) {
    glDetachShader(program, vertex_shader);
    glDetachShader(program, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return program;
  }

  GLint log_length = 0;
  glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
  std::string log(static_cast<std::size_t>(std::max(log_length, 1)), '\0');
  glGetProgramInfoLog(program, log_length, nullptr, log.data());
  glDeleteProgram(program);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
  throw std::runtime_error("Program link failed: " + log);
}

void Renderer::create_programs() {
  static_assert(
      kOceanMaxWaveCount == 6U,
      "Mettre a jour la taille des tableaux d'ondes dans le shader GLSL.");

  static constexpr auto *world_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_normal;
layout(location = 3) in float a_face_shade;
layout(location = 4) in float a_ao;
layout(location = 5) in float a_sky_light;
layout(location = 6) in float a_block_light;
layout(location = 7) in float a_material_class;
layout(location = 8) in float a_wave_weight;

uniform mat4 u_model;
uniform mat4 u_view_projection;
uniform mat4 u_light_view_projection;
uniform vec3 u_camera_position;
uniform float u_time_of_day;
uniform float u_wind_strength;
uniform vec4 u_ocean_waves[6];
uniform vec2 u_ocean_wave_phases[6];
uniform int u_ocean_wave_count;

out vec2 v_uv;
out vec3 v_normal;
out float v_face_shade;
out float v_ao;
out float v_sky_light;
out float v_block_light;
out float v_material_class;
out float v_wave_weight;
out float v_distance;
out vec3 v_world_position;
out vec4 v_light_position;
out vec3 v_ocean_normal;
out float v_ocean_crest;

float material_mask(float material, float expected) {
    return 1.0 - step(0.25, abs(material - expected));
}

void sample_ocean(
    vec2 world_xz,
    out float height,
    out vec2 gradient,
    out float crest
) {
    height = 0.0;
    gradient = vec2(0.0);
    crest = 0.0;

    // La boucle possède une limite compile-time compatible OpenGL 3.3.
    // u_ocean_wave_count sélectionne le niveau de qualité.
    for (int index = 0; index < 6; ++index) {
        if (index >= u_ocean_wave_count) {
            break;
        }

        vec4 geometry = u_ocean_waves[index];
        vec2 phase_data = u_ocean_wave_phases[index];

        vec2 direction = geometry.xy;
        float wave_number = geometry.z;
        float amplitude = geometry.w;

        float theta =
            dot(direction, world_xz) *
                wave_number +
            phase_data.x;

        float harmonic =
            0.14 *
            clamp(phase_data.y, 0.0, 1.0);

        float sine = sin(theta);
        float cosine = cos(theta);
        float double_sine = sin(theta * 2.0);
        float double_cosine = cos(theta * 2.0);

        height +=
            amplitude *
            (sine +
             harmonic * double_sine);

        float derivative =
            amplitude *
            wave_number *
            (cosine +
             2.0 * harmonic * double_cosine);

        gradient +=
            direction * derivative;

        // Je garde la crete dominante : la moyenne des six directions
        // supprimait les lignes de houle visibles lorsque la mer etait calme.
        float normalized_wave_height =
            clamp(
                0.5 +
                    0.5 *
                        (sine +
                         harmonic * double_sine) /
                        (1.0 + harmonic),
                0.0,
                1.0);

        crest = max(
            crest,
            normalized_wave_height *
                normalized_wave_height *
                normalized_wave_height);
    }
}

vec2 vegetation_wind_offset(vec3 world_position, float material_class, float time_phase) {
    float foliage_mask = material_mask(material_class, 4.0);
    float flora_mask = material_mask(material_class, 5.0);
    float wind_mask = max(foliage_mask * 0.35, flora_mask);
    if (wind_mask <= 0.0) {
        return vec2(0.0);
    }

    float gust_a = sin(world_position.x * 0.18 + world_position.z * 0.11 + time_phase * 1.35);
    float gust_b = cos(world_position.x * -0.13 + world_position.z * 0.21 + time_phase * 1.65);
    float flutter = sin((world_position.x + world_position.z) * 0.75 + world_position.y * 0.45 + time_phase * 2.40);
    float local_height = clamp(fract(world_position.y), 0.0, 1.0);
    local_height = mix(1.0, smoothstep(0.02, 0.98, local_height), flora_mask);
    float amplitude = u_wind_strength * wind_mask * mix(0.010, 0.032, flora_mask);
    return vec2(gust_a * 0.70 + flutter * 0.30, gust_b * 0.60 - gust_a * 0.22) * amplitude * local_height;
}

vec3 fabric_wind_offset(
    vec3 world_position,
    float material_class,
    float vertex_weight,
    float time_phase
) {
    float fabric_mask = material_mask(material_class, 9.0);
    float flexibility = clamp(vertex_weight, 0.0, 1.0) * fabric_mask;
    if (flexibility <= 0.0) {
        return vec3(0.0);
    }

    // Je deplace les deux faces d'une voile dans la meme direction monde :
    // elles restent jointives, tandis que les sommets ancres (poids nul)
    // demeurent exactement alignes sur les vergues et les mats.
    float wind = clamp(u_wind_strength, 0.0, 1.0);
    vec2 wind_direction = normalize(vec2(0.82, 0.57));
    vec2 transverse = vec2(-wind_direction.y, wind_direction.x);
    float phase =
        dot(world_position.xz, vec2(0.17, 0.11)) +
        world_position.y * 0.19 +
        time_phase * 1.24;
    float billow = sin(phase) + sin(phase * 2.13 + 0.7) * 0.28;
    float flutter = sin(phase * 3.71 - world_position.y * 0.31);
    float amplitude = flexibility * (0.014 + wind * 0.082);
    vec2 horizontal =
        wind_direction * billow * amplitude +
        transverse * flutter * amplitude * 0.24;
    return vec3(
        horizontal.x,
        flutter * amplitude * 0.055,
        horizontal.y);
}

void main() {
    vec4 world_position = u_model * vec4(a_position, 1.0);
    world_position.xz += vegetation_wind_offset(world_position.xyz, a_material_class, u_time_of_day * 8.0);
    world_position.xyz += fabric_wind_offset(
        world_position.xyz,
        a_material_class,
        a_wave_weight,
        u_time_of_day * 8.0);

    float water_mask =
        material_mask(a_material_class, 6.0);

    float wave_weight =
        clamp(a_wave_weight, 0.0, 1.0) *
        water_mask;

    vec2 ocean_gradient = vec2(0.0);
    float ocean_crest = 0.0;

    if (wave_weight > 0.0) {
        float ocean_height = 0.0;

        sample_ocean(
            world_position.xz,
            ocean_height,
            ocean_gradient,
            ocean_crest);

        // Déplacement vertical uniquement : les limites entre chunks restent
        // parfaitement raccordées et les côtes voxelisées sont conservées.
        world_position.y +=
            ocean_height * wave_weight;
    }

    v_normal =
        normalize(mat3(u_model) * a_normal);

    v_ocean_normal = normalize(
        vec3(
            -ocean_gradient.x,
            1.0,
            -ocean_gradient.y));

    v_ocean_crest = ocean_crest;

    gl_Position = u_view_projection * world_position;
    v_uv = a_uv;
    v_face_shade = a_face_shade;
    v_ao = a_ao;
    v_sky_light = a_sky_light;
    v_block_light = a_block_light;
    v_material_class = a_material_class;
    v_wave_weight = wave_weight;
    v_distance = distance(world_position.xyz, u_camera_position);
    v_world_position = world_position.xyz;
    v_light_position = u_light_view_projection * world_position;
}
)";

  static constexpr auto *item_drop_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_face_uv;
layout(location = 3) in float a_face_index;
layout(location = 4) in mat4 i_transform;
layout(location = 8) in uint i_block_id;
layout(location = 9) in float i_sky_light;
layout(location = 10) in float i_block_light;
layout(location = 11) in float i_material_class;
layout(location = 12) in vec4 i_face_tiles_0_1;
layout(location = 13) in vec4 i_face_tiles_2_3;
layout(location = 14) in vec4 i_face_tiles_4_5;

uniform mat4 u_view_projection;
uniform mat4 u_light_view_projection;
uniform vec3 u_camera_position;

out vec2 v_uv;
out vec3 v_normal;
out float v_face_shade;
out float v_ao;
out float v_sky_light;
out float v_block_light;
out float v_material_class;
out float v_wave_weight;
out float v_distance;
out vec3 v_world_position;
out vec4 v_light_position;
out vec3 v_ocean_normal;
out float v_ocean_crest;

vec4 atlas_uv_rect(vec2 tile) {
    float uv_step = 1.0 / 16.0;
    float u0 = tile.x * uv_step;
    float v0 = tile.y * uv_step;
    return vec4(u0, v0, u0 + uv_step, v0 + uv_step);
}

vec4 block_uv_rect(uint face_index) {
    if (face_index == 0u) {
        return atlas_uv_rect(i_face_tiles_0_1.xy);
    }
    if (face_index == 1u) {
        return atlas_uv_rect(i_face_tiles_0_1.zw);
    }
    if (face_index == 2u) {
        return atlas_uv_rect(i_face_tiles_2_3.xy);
    }
    if (face_index == 3u) {
        return atlas_uv_rect(i_face_tiles_2_3.zw);
    }
    if (face_index == 4u) {
        return atlas_uv_rect(i_face_tiles_4_5.xy);
    }
    return atlas_uv_rect(i_face_tiles_4_5.zw);
}

float face_shade(float face_index) {
    if (face_index < 1.5) {
        return 0.85;
    }
    if (face_index < 2.5) {
        return 1.0;
    }
    if (face_index < 3.5) {
        return 0.65;
    }
    return 0.75;
}

void main() {
    vec3 world_position3 =
        (i_transform * vec4(a_position, 1.0)).xyz;
    mat3 normal_matrix =
        transpose(inverse(mat3(i_transform)));
    vec3 world_normal =
        normalize(normal_matrix * a_normal);
    vec4 uv_rect = block_uv_rect(uint(a_face_index + 0.5));
    vec2 uv = mix(uv_rect.xy, uv_rect.zw, a_face_uv);
    vec4 world_position = vec4(world_position3, 1.0);

    gl_Position = u_view_projection * world_position;
    v_uv = uv;
    v_normal = world_normal;
    v_face_shade = face_shade(a_face_index);
    v_ao = 1.0;
    v_sky_light = i_sky_light;
    v_block_light = i_block_light;
    v_material_class = i_material_class;
    v_wave_weight = 0.0;
    v_distance = distance(world_position3, u_camera_position);
    v_world_position = world_position3;
    v_light_position = u_light_view_projection * world_position;
    v_ocean_normal = world_normal;
    v_ocean_crest = 0.0;
}
)";

  static constexpr auto *world_fragment_shader_part1 = R"(#version 330 core
in vec2 v_uv;
in vec3 v_normal;
in float v_face_shade;
in float v_ao;
in float v_sky_light;
in float v_block_light;
in float v_material_class;
in float v_wave_weight;
in float v_distance;
in vec3 v_world_position;
in vec4 v_light_position;
in vec3 v_ocean_normal;
in float v_ocean_crest;

uniform float u_ocean_foam_threshold;
uniform float u_ocean_detail_strength;
uniform float u_ocean_detail_phase;
uniform float u_ocean_severity;
uniform float u_ocean_tempest_factor;
uniform float u_ocean_open_sea;
uniform int u_maritime_horizon_enabled;
uniform vec2 u_maritime_water_blend_range;
uniform vec2 u_maritime_far_fog_range;
uniform float u_maritime_sea_level;

uniform sampler2D u_atlas;
uniform sampler2D u_shadow_map;
uniform sampler2D u_shadow_map_far;
uniform sampler2D u_scene_color;
uniform sampler2D u_scene_depth;
uniform mat4 u_light_view_projection_far;
uniform vec3 u_camera_position;
uniform vec3 u_camera_forward;
uniform vec3 u_sun_direction;
uniform vec3 u_sun_color;
uniform vec3 u_ambient_color;
uniform vec3 u_block_light_color;
uniform int u_enclosed_interior;
uniform float u_interior_visibility_floor;
uniform int u_backrooms_flicker_count;
uniform vec4 u_backrooms_flicker_lights[6];
uniform float u_backrooms_flashlight_intensity;
uniform vec3 u_fog_color;
uniform vec3 u_distant_fog_color;
uniform vec2 u_interior_fog_range;
uniform vec3 u_horizon_glow_color;
uniform vec3 u_night_tint_color;
uniform mat4 u_inverse_view_projection;
uniform float u_daylight_factor;
uniform float u_sun_visibility;
uniform float u_time_of_day;
uniform float u_cloud_intensity;
uniform float u_cloud_shadow_strength;
uniform float u_atmospheric_scatter_strength;
uniform float u_height_fog_density;
uniform float u_precipitation_intensity;
uniform float u_storm_intensity;
uniform float u_lightning_intensity;
uniform float u_super_vision_strength;
uniform int u_shadows_enabled;
uniform int u_shadow_cascade_count;
uniform float u_shadow_split_distance;
uniform float u_shadow_transition_width;

out vec4 frag_color;
)" VALCRAFT_SHIP_PROTECTION_GLSL_SOURCE R"(

float material_mask(float material, float expected) {
    return 1.0 - step(0.25, abs(material - expected));
}

float backrooms_flashlight_irradiance(vec3 world_position) {
    float intensity =
        max(u_backrooms_flashlight_intensity, 0.0);
    if (u_enclosed_interior == 0 || intensity <= 0.0001) {
        return 0.0;
    }

    vec3 camera_to_fragment =
        world_position - u_camera_position;
    float distance_squared =
        dot(camera_to_fragment, camera_to_fragment);
    const float inverse_range_squared =
        1.0 / (34.0 * 34.0);
    float normalized_distance_squared =
        distance_squared * inverse_range_squared;
    if (normalized_distance_squared >= 1.0) {
        return 0.0;
    }

    vec3 ray_direction =
        camera_to_fragment *
        inversesqrt(max(distance_squared, 0.000001));
    float angle_cosine =
        dot(ray_direction, normalize(u_camera_forward));
    const float outer_cone_cosine = 0.913545;
    const float inner_cone_cosine = 0.974370;
    const float hotspot_cosine = 0.994522;
    const float penumbra_cone_cosine = 0.887011;
    if (angle_cosine <= penumbra_cone_cosine) {
        return 0.0;
    }

    float penumbra =
        smoothstep(
            penumbra_cone_cosine,
            outer_cone_cosine,
            angle_cosine);
    float spill =
        smoothstep(
            outer_cone_cosine,
            inner_cone_cosine,
            angle_cosine);
    float hotspot =
        smoothstep(
            inner_cone_cosine,
            hotspot_cosine,
            angle_cosine);
    float angular_profile =
        penumbra *
        mix(
            0.035,
            mix(0.30, 1.0, hotspot),
            spill);
    float range_window =
        clamp(
            1.0 -
                normalized_distance_squared *
                normalized_distance_squared,
            0.0,
            1.0);
    range_window *= range_window;
    float distance_falloff =
        range_window /
        (1.0 + 0.012 * distance_squared);
    return
        intensity *
        angular_profile *
        distance_falloff;
}

float backrooms_darkness_visibility(
    float local_light,
    float flashlight_energy) {
    if (u_enclosed_interior == 0) {
        return 1.0;
    }

    // Je laisse le noir atteindre exactement zero sans recreer les marches
    // voxel : seule une vraie rampe ou la Maglite restaure la visibilite.
    float safe_local_light =
        (isnan(local_light) ||
         isinf(local_light))
            ? 0.0
            : clamp(
                  local_light,
                  0.0,
                  1.0);
    float safe_flashlight_energy =
        (isnan(flashlight_energy) ||
         isinf(flashlight_energy))
            ? 0.0
            : clamp(
                  flashlight_energy,
                  0.0,
                  1.0);
    float fixture_visibility =
        smoothstep(
            0.000,
            0.620,
            safe_local_light);
    float flashlight_visibility =
        smoothstep(
            0.000,
            0.180,
            safe_flashlight_energy);
    float combined_visibility = clamp(
        1.0 -
            (1.0 - fixture_visibility) *
            (1.0 - flashlight_visibility),
        0.0,
        1.0);
    float safe_visibility_floor =
        (isnan(u_interior_visibility_floor) ||
         isinf(u_interior_visibility_floor))
            ? 0.0
            : clamp(
                  u_interior_visibility_floor,
                  0.0,
                  1.0);
    return mix(
        safe_visibility_floor,
        1.0,
        combined_visibility);
}

float shadow_visibility_at(
    vec2 uv,
    float receiver_depth,
    float bias,
    bool far_cascade
) {
    float sampled_depth = far_cascade
        ? texture(u_shadow_map_far, uv).r
        : texture(u_shadow_map, uv).r;
    return (receiver_depth - bias) <= sampled_depth ? 1.0 : 0.0;
}

float sample_shadow_cascade(vec3 normal, bool far_cascade) {
    vec4 light_position = far_cascade
        ? u_light_view_projection_far * vec4(v_world_position, 1.0)
        : v_light_position;
    vec3 projected = light_position.xyz / max(light_position.w, 0.0001);
    projected = projected * 0.5 + 0.5;
    if (projected.z < 0.0 || projected.z > 1.0 || projected.x < 0.0 || projected.x > 1.0 || projected.y < 0.0 || projected.y > 1.0) {
        return 1.0;
    }

    vec2 texel_size = far_cascade
        ? 1.0 / vec2(textureSize(u_shadow_map_far, 0))
        : 1.0 / vec2(textureSize(u_shadow_map, 0));
    float ndotl = max(dot(normalize(normal), normalize(u_sun_direction)), 0.0);
    float bias =
        max(0.00065 * (1.0 - ndotl), 0.00012) *
        (far_cascade ? 1.35 : 1.0);
    // Je garde un PCF en croix pour lisser les ombres sans payer neuf lectures texture par fragment.
    float visibility = shadow_visibility_at(projected.xy, projected.z, bias, far_cascade) * 0.36;
    visibility += shadow_visibility_at(projected.xy + vec2(texel_size.x, 0.0), projected.z, bias, far_cascade) * 0.16;
    visibility += shadow_visibility_at(projected.xy - vec2(texel_size.x, 0.0), projected.z, bias, far_cascade) * 0.16;
    visibility += shadow_visibility_at(projected.xy + vec2(0.0, texel_size.y), projected.z, bias, far_cascade) * 0.16;
    visibility += shadow_visibility_at(projected.xy - vec2(0.0, texel_size.y), projected.z, bias, far_cascade) * 0.16;
    return visibility;
}

float sample_shadow(vec3 normal) {
    if (u_sun_visibility < 0.5 || u_shadows_enabled == 0) {
        return 1.0;
    }
    if (u_shadow_cascade_count <= 1) {
        return sample_shadow_cascade(normal, false);
    }

    float view_depth = max(
        dot(v_world_position - u_camera_position, u_camera_forward),
        0.0);
    float transition_width = max(u_shadow_transition_width, 0.0);
    if (transition_width <= 0.0001) {
        return sample_shadow_cascade(
            normal,
            view_depth > u_shadow_split_distance);
    }

    float half_width = transition_width * 0.5;
    if (view_depth <= u_shadow_split_distance - half_width) {
        return sample_shadow_cascade(normal, false);
    }
    if (view_depth >= u_shadow_split_distance + half_width) {
        return sample_shadow_cascade(normal, true);
    }

    float blend = smoothstep(
        u_shadow_split_distance - half_width,
        u_shadow_split_distance + half_width,
        view_depth);
    return mix(
        sample_shadow_cascade(normal, false),
        sample_shadow_cascade(normal, true),
        blend);
}

float hash12(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float value_noise2(vec2 p) {
    vec2 cell = floor(p);
    vec2 local = fract(p);
    vec2 blend = local * local * (3.0 - 2.0 * local);

    float n00 = hash12(cell);
    float n10 = hash12(cell + vec2(1.0, 0.0));
    float n01 = hash12(cell + vec2(0.0, 1.0));
    float n11 = hash12(cell + vec2(1.0, 1.0));
    float nx0 = mix(n00, n10, blend.x);
    float nx1 = mix(n01, n11, blend.x);
    return mix(nx0, nx1, blend.y);
}

float sample_cloud_shadow(vec3 world_position, vec3 sun_direction) {
    float cloud_factor = clamp(u_cloud_intensity, 0.0, 1.0);
    float daylight = clamp(u_daylight_factor, 0.0, 1.0);
    if (cloud_factor <= 0.01 || u_cloud_shadow_strength <= 0.001 || daylight <= 0.20 || sun_direction.y <= 0.02) {
        return 1.0;
    }

    float projection_scale = (96.0 - world_position.y) / max(sun_direction.y, 0.12);
    vec2 projected = world_position.xz + sun_direction.xz * projection_scale;
    vec2 flow = projected * 0.0032 + vec2(u_time_of_day * 0.085, -u_time_of_day * 0.061);
    float base = value_noise2(flow);
    float detail = value_noise2(flow * 2.17 + vec2(9.3, 4.7));
    float cloud = smoothstep(0.52, 0.84, base * 0.68 + detail * 0.32);
    float coverage = smoothstep(0.10, 0.58, cloud_factor);
    return 1.0 - cloud * coverage * u_cloud_shadow_strength;
}

vec2 water_detail_gradient(
    vec2 world_xz,
    float time_phase
) {
    // Multiplicateurs entiers afin que le bouclage de phase à 2*pi soit
    // parfaitement continu après une longue session.
    float phase_d =
        world_xz.x * 1.08 -
        world_xz.y * 0.74 +
        time_phase * 2.0;

    float phase_e =
        world_xz.x * 0.72 +
        world_xz.y * 1.16 -
        time_phase * 3.0;

    float strength =
        max(u_ocean_detail_strength, 0.0);

    float d_height_dx =
        (cos(phase_d) * 1.08 +
         cos(phase_e) * 0.72 * 0.68) *
        strength;

    float d_height_dz =
        (-cos(phase_d) * 0.74 +
         cos(phase_e) * 1.16 * 0.68) *
        strength;

    return vec2(
        d_height_dx,
        d_height_dz);
}

vec2 rain_dimple_gradient(
    vec2 world_xz,
    float time_phase
) {
    float rain = clamp(u_precipitation_intensity, 0.0, 1.0);
    if (rain <= 0.001) {
        return vec2(0.0);
    }

    vec2 cell_position = world_xz * 1.85;
    vec2 cell = floor(cell_position);
    vec2 local = fract(cell_position) - vec2(0.5);
    float random_phase = hash12(cell + vec2(43.0, 17.0));
    float age = fract(time_phase * 0.42 + random_phase);
    float radius = length(local);
    float front = age * 0.58;
    float ring = exp(-pow((radius - front) * 22.0, 2.0));
    float pulse = cos((radius - front) * 46.0) * (1.0 - age);
    vec2 direction = local / max(radius, 0.035);
    return direction * ring * pulse * rain * 0.085;
}

vec3 reconstruct_world_position(vec2 screen_uv, float depth_sample) {
    vec4 clip_position = vec4(screen_uv * 2.0 - 1.0, depth_sample * 2.0 - 1.0, 1.0);
    vec4 world_position = u_inverse_view_projection * clip_position;
    return world_position.xyz / max(world_position.w, 0.0001);
}

float atlas_tile_edge_factor(vec2 uv) {
    vec2 local_uv = fract(uv * 16.0);
    float edge_distance = min(min(local_uv.x, 1.0 - local_uv.x), min(local_uv.y, 1.0 - local_uv.y));
    return 1.0 - smoothstep(0.018, 0.092, edge_distance);
}

float material_grain(vec3 world_position, float material_class) {
    float height_slice = floor(world_position.y * 0.53);
    float coarse = hash12(floor(world_position.xz * 0.72) + vec2(height_slice * 0.37, material_class * 11.17));
    float fine = hash12(floor(world_position.xy * 2.35) + vec2(floor(world_position.z * 0.41), material_class * 7.91));
    return coarse * 0.66 + fine * 0.34 - 0.5;
}

float ordered_alpha_threshold(vec2 pixel_position) {
    const float pattern[16] = float[](
         0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
         3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0);
    ivec2 cell = ivec2(mod(floor(pixel_position), 4.0));
    return (pattern[cell.x + cell.y * 4] + 0.5) / 16.0;
}
)";

  static constexpr auto *world_fragment_shader_part2 = R"(
void main() {
    float water_mask = material_mask(v_material_class, 6.0);
    if (water_mask > 0.5 &&
        ship_excludes_ocean(v_world_position)) {
        // Je supprime l'ocean avant toute lecture de refraction afin que la
        // coque reste etanche sans payer le shader d'eau sous le navire.
        discard;
    }
    float weather_exposure =
        ship_shelters_weather(v_world_position)
            ? 0.0
            : 1.0;
    // Je separe le masque de surface du poids de houle : les eaux locales
    // gardent leurs petites normales animees sans subir le deplacement marin.
    float water_surface_mask =
        water_mask *
        clamp(
            max(v_normal.y, 0.0),
            0.0,
            1.0);

    // Je ne decale pas les UV partages de l'atlas dans le fragment pour eviter
    // d'echantillonner les sprites transparents voisins et d'ouvrir des trous.
    vec2 animated_uv = v_uv;

    vec4 sampled = texture(u_atlas, animated_uv);
    float early_glass_mask = material_mask(v_material_class, 10.0);
    if (early_glass_mask > 0.5) {
        // Je ne paie le motif Bayer que pour le verre; les fragments opaques
        // du monde conservent le test alpha simple et peu couteux.
        if (sampled.a < ordered_alpha_threshold(gl_FragCoord.xy)) {
            discard;
        }
    } else if (sampled.a < 0.1) {
        discard;
    }

    vec3 albedo = sampled.rgb;
    vec3 normal = normalize(v_normal);

    if (water_surface_mask > 0.001) {
        vec2 detail_gradient = vec2(0.0);

        if (u_ocean_detail_strength > 0.000001) {
            // La condition dépend d'un uniform. En qualité basse, le pilote peut
            // éliminer entièrement ces évaluations trigonométriques.
            detail_gradient = water_detail_gradient(
                v_world_position.xz,
                u_ocean_detail_phase);
        }
        detail_gradient += rain_dimple_gradient(
            v_world_position.xz,
            u_ocean_detail_phase);

        vec3 ocean_normal = normalize(
            vec3(
                v_ocean_normal.x - detail_gradient.x,
                max(v_ocean_normal.y, 0.08),
                v_ocean_normal.z - detail_gradient.y));

        normal = normalize(
            mix(
                normal,
                ocean_normal,
                water_surface_mask));
    }
    if (!gl_FrontFacing && water_mask > 0.5) {
        normal = -normal;
    }
)"
                                                       R"(

    vec3 view_direction = normalize(u_camera_position - v_world_position);
    vec3 sun_direction = normalize(u_sun_direction);
    float daylight = clamp(u_daylight_factor, 0.0, 1.0);
    float sky_light = clamp(v_sky_light, 0.0, 1.0);
    float block_light = clamp(v_block_light, 0.0, 1.0);
    float enclosed_interior =
        u_enclosed_interior != 0 ? 1.0 : 0.0;
    float backrooms_light_scale = 1.0;
    float backrooms_source_scale = 1.0;
    if (u_enclosed_interior != 0) {
        for (int light_index = 0; light_index < 6; ++light_index) {
            if (light_index >= u_backrooms_flicker_count) {
                break;
            }
            vec2 light_delta =
                v_world_position.xz -
                u_backrooms_flicker_lights[light_index].xz;
            float light_distance_squared =
                dot(light_delta, light_delta);
            float light_influence =
                1.0 - smoothstep(9.0, 100.0, light_distance_squared);
            float source_influence =
                1.0 - smoothstep(4.0, 12.25, light_distance_squared);
            float flicker_intensity =
                clamp(
                    u_backrooms_flicker_lights[light_index].w,
                    0.05,
                    1.0);
            backrooms_light_scale = min(
                backrooms_light_scale,
                mix(1.0, flicker_intensity, light_influence));
            backrooms_source_scale = min(
                backrooms_source_scale,
                mix(1.0, flicker_intensity, source_influence));
        }
    }
    block_light *= mix(
        1.0,
        backrooms_light_scale,
        enclosed_interior);
    // Je tasse réellement la dernière marche de la lumière voxel : la dérivée
    // nulle aux deux extrémités efface le contour en losange au lieu de
    // l'amplifier, sans modifier les torches du monde ouvert.
    float smooth_block_light =
        block_light * block_light * (3.0 - 2.0 * block_light);
    float deepened_block_light =
        smooth_block_light * smooth_block_light;
    float softened_block_light = mix(
        block_light,
        deepened_block_light,
        enclosed_interior);
    float shadow = sample_shadow(normal);
    float cloud_shadow = sample_cloud_shadow(v_world_position + normal * 0.35, sun_direction);

    float terrain_mask = material_mask(v_material_class, 0.0);
    float rock_mask = material_mask(v_material_class, 1.0);
    float sand_mask = material_mask(v_material_class, 2.0);
    float wood_mask = material_mask(v_material_class, 3.0);
    float foliage_mask = material_mask(v_material_class, 4.0);
    float flora_mask = material_mask(v_material_class, 5.0);
    float emissive_mask = material_mask(v_material_class, 7.0);
    float snow_mask = material_mask(v_material_class, 8.0);
    float fabric_mask = material_mask(v_material_class, 9.0);
    float glass_mask = material_mask(v_material_class, 10.0);
    float iron_mask = material_mask(v_material_class, 11.0);
    float brass_mask = material_mask(v_material_class, 12.0);
    float metal_mask = clamp(iron_mask + brass_mask, 0.0, 1.0);

    float view_alignment = mix(max(dot(view_direction, normal), 0.0), abs(dot(view_direction, normal)), water_mask);
    float sun_alignment = mix(max(dot(normal, sun_direction), 0.0), abs(dot(normal, sun_direction)), water_mask);

    float face_light =
        mix(0.82, 1.10, clamp(v_face_shade, 0.0, 1.0)) *
        mix(0.78, 1.00, clamp(v_ao, 0.0, 1.0));
    float interior_face_light =
        mix(0.72, 1.04, clamp(v_face_shade, 0.0, 1.0)) *
        mix(0.60, 1.00, clamp(v_ao, 0.0, 1.0));
    face_light = mix(face_light, interior_face_light, enclosed_interior);
    float hemisphere = smoothstep(-0.25, 1.0, normal.y);
    float ambient_distribution = mix(
        mix(0.40, 1.12, sky_light),
        mix(0.62, 0.88, hemisphere),
        enclosed_interior);
    vec3 ambient = u_ambient_color * ambient_distribution;
    ambient *= mix(
        mix(0.88, 1.08, hemisphere),
        1.0,
        enclosed_interior);
    float interior_shadow_reveal =
        smoothstep(0.08, 0.62, softened_block_light);
    ambient *= mix(
        1.0,
        mix(0.36, 1.0, interior_shadow_reveal),
        enclosed_interior);

    float direct = mix(sun_alignment, sun_alignment * sun_alignment, 0.45);
    vec3 sunlight = u_sun_color * direct * shadow * cloud_shadow * u_sun_visibility * daylight * (0.72 + 0.28 * sky_light);

    float bounce_factor = smoothstep(-0.35, 1.0, normal.y) * sky_light;
    vec3 bounce_light = mix(u_fog_color, u_distant_fog_color, 0.42) * bounce_factor * (0.12 + 0.12 * daylight);
    float interior_distribution =
        mix(0.72, 1.12, smoothstep(-0.20, 1.0, normal.y));
    bounce_light +=
        u_block_light_color * enclosed_interior *
        (0.070 * softened_block_light) *
        interior_distribution;
    vec3 torch_light =
        u_block_light_color * softened_block_light *
        mix(1.18, 0.96, enclosed_interior) *
        (1.0 + emissive_mask * 0.42);

    float rim = pow(1.0 - view_alignment, mix(3.0, 1.7, water_mask + foliage_mask * 0.35 + flora_mask * 0.45 + fabric_mask * 0.40 + glass_mask * 0.55));
    vec3 rim_color =
        mix(u_fog_color, u_sun_color, 0.55) * rim * (0.02 + 0.08 * daylight + 0.04 * foliage_mask + 0.05 * flora_mask + 0.04 * fabric_mask + 0.10 * glass_mask);

    vec3 reflected = reflect(-sun_direction, normal);
    float specular_power = mix(11.0, 34.0, rock_mask + snow_mask * 0.3 + sand_mask * 0.1 + glass_mask * 0.55);
    specular_power = mix(specular_power, 18.0, wood_mask);
    specular_power = mix(specular_power, 42.0, glass_mask);
    specular_power = mix(specular_power, 72.0, metal_mask);
    float specular = pow(max(dot(reflected, view_direction), 0.0), specular_power);
    vec3 specular_color =
        u_sun_color * specular * shadow * cloud_shadow *
        (0.12 * rock_mask + 0.08 * wood_mask + 0.05 * snow_mask + 0.22 * glass_mask + 0.34 * iron_mask + 0.48 * brass_mask);

    float leaf_backlight = pow(max(dot(-normal, sun_direction), 0.0), 1.8);
    vec3 leaf_translucency =
        albedo * u_sun_color * leaf_backlight * mix(0.0, 0.06, foliage_mask) * u_sun_visibility * daylight * (0.35 + 0.65 * sky_light);
    leaf_translucency +=
        albedo * u_sun_color * leaf_backlight * mix(0.0, 0.10, flora_mask) * u_sun_visibility * daylight * (0.40 + 0.60 * sky_light);
    leaf_translucency +=
        albedo * u_sun_color * leaf_backlight * mix(0.0, 0.07, fabric_mask) * u_sun_visibility * daylight * (0.42 + 0.58 * sky_light);
    leaf_translucency *= mix(0.75, 1.0, cloud_shadow);

    vec3 material_tint = vec3(1.0);
    material_tint = mix(material_tint, vec3(0.70, 0.82, 0.58), terrain_mask * smoothstep(0.15, 1.0, normal.y) * 0.52);
    material_tint = mix(material_tint, vec3(1.03, 0.99, 0.92), sand_mask);
    material_tint = mix(material_tint, vec3(0.94, 0.98, 1.06), snow_mask);
    material_tint = mix(material_tint, vec3(1.06, 1.00, 0.84), fabric_mask);
    material_tint = mix(material_tint, vec3(0.84, 0.94, 1.08), glass_mask);
    material_tint = mix(material_tint, vec3(0.72, 0.78, 0.84), iron_mask);
    material_tint = mix(material_tint, vec3(1.16, 0.83, 0.38), brass_mask);
    material_tint = mix(material_tint, vec3(0.58, 0.72, 0.44), foliage_mask * 0.84);
    material_tint = mix(material_tint, vec3(0.90, 1.00, 0.84), flora_mask * 0.54);
    material_tint = mix(material_tint, vec3(1.02, 0.98, 0.94), wood_mask * 0.45);

    float natural_material_mask = clamp(terrain_mask + rock_mask + sand_mask + wood_mask + foliage_mask + flora_mask + snow_mask + fabric_mask, 0.0, 1.0);
    float grain = material_grain(v_world_position, v_material_class);
    float grain_strength =
        terrain_mask * 0.032 + rock_mask * 0.045 + sand_mask * 0.030 + wood_mask * 0.038 +
        foliage_mask * 0.034 + flora_mask * 0.026 + snow_mask * 0.020 + fabric_mask * 0.018;
    albedo *= 1.0 + grain * grain_strength * (0.55 + 0.45 * sky_light);

    vec3 grain_tint = mix(vec3(0.97, 1.02, 0.98), vec3(1.04, 0.98, 0.92), smoothstep(-0.22, 0.26, grain));
    material_tint = mix(material_tint, material_tint * grain_tint, natural_material_mask * 0.22);

    float solid_edge_mask = clamp(terrain_mask + rock_mask + sand_mask + wood_mask + snow_mask + fabric_mask * 0.25 + glass_mask * 0.35 + metal_mask, 0.0, 1.0);
    float tile_edge = atlas_tile_edge_factor(v_uv) * solid_edge_mask;
    float bevel_shadow = tile_edge * (0.020 + 0.030 * (1.0 - smoothstep(-0.25, 0.85, normal.y)));
    material_tint *= 1.0 - bevel_shadow;

    vec3 lit_color = albedo * material_tint * face_light * (ambient + bounce_light + sunlight + torch_light);
    float flashlight_energy =
        backrooms_flashlight_irradiance(v_world_position);
    if (flashlight_energy > 0.0) {
        float flashlight_incidence =
            max(dot(normal, view_direction), 0.0);
        float contact_visibility =
            mix(0.78, 1.0, clamp(v_ao, 0.0, 1.0));
        vec3 flashlight_radiance =
            vec3(1.00, 0.92, 0.76) *
            (3.60 * flashlight_energy);
        // Je traite la Maglite comme une lumière directe : l'AO et la teinte
        // de face ne doivent pas étouffer son cœur dans les zones noires.
        lit_color +=
            albedo *
            material_tint *
            flashlight_radiance *
            mix(0.20, 1.0, flashlight_incidence) *
            contact_visibility;
    }
    lit_color += leaf_translucency;
    lit_color += rim_color + specular_color;
    lit_color += u_night_tint_color * (0.05 + 0.05 * sky_light) * (1.0 - daylight);

    float output_alpha = 1.0;
    if (water_mask > 0.5) {
        float fresnel = pow(1.0 - view_alignment, 4.5);
        float water_time = u_time_of_day * 20.0;
        vec2 detail_flow = vec2(
            sin(v_world_position.x * 0.31 + v_world_position.z * 0.17 + water_time * 0.75),
            cos(v_world_position.z * 0.29 - v_world_position.x * 0.21 - water_time * 0.66));

        vec2 scene_texel = 1.0 / vec2(textureSize(u_scene_color, 0));
        vec2 scene_uv = gl_FragCoord.xy * scene_texel;
        vec2 refraction_offset = (normal.xz * (0.010 + 0.006 * water_surface_mask) + detail_flow * 0.0015) *
                                 (0.28 + 0.72 * (1.0 - view_alignment));
        vec2 refracted_uv = clamp(scene_uv + refraction_offset, scene_texel * 0.5, vec2(1.0) - scene_texel * 0.5);

        float base_scene_depth = texture(u_scene_depth, scene_uv).r;
        float refracted_scene_depth = texture(u_scene_depth, refracted_uv).r;
        if (refracted_scene_depth + 0.00005 < gl_FragCoord.z) {
            refracted_uv = scene_uv;
            refracted_scene_depth = base_scene_depth;
        }

        vec3 scene_color = texture(u_scene_color, refracted_uv).rgb;
        float water_depth = 0.0;
        if (refracted_scene_depth < 0.9999) {
            vec3 background_position = reconstruct_world_position(refracted_uv, refracted_scene_depth);
            water_depth = max(distance(background_position, v_world_position), 0.0);
        } else {
            water_depth = 7.0 + 12.0 * fresnel;
        }
        water_depth = clamp(water_depth, 0.0, 48.0);

        float body_depth = max(water_depth, 0.32 + 0.18 * water_surface_mask);
        vec3 absorption = mix(vec3(0.90, 0.34, 0.12), vec3(0.72, 0.28, 0.10), daylight);
        vec3 transmittance = exp(-absorption * body_depth);

        vec3 shallow_color = mix(vec3(0.07, 0.20, 0.26), vec3(0.10, 0.42, 0.55), daylight);
        vec3 deep_color = mix(vec3(0.02, 0.08, 0.13), vec3(0.04, 0.19, 0.30), daylight);
        vec3 water_volume_color = mix(shallow_color, deep_color, smoothstep(0.25, 6.0, body_depth));
        float tempest_factor = clamp(u_ocean_tempest_factor, 0.0, 1.0);
        water_volume_color = mix(
            water_volume_color,
            water_volume_color * vec3(0.54, 0.66, 0.72),
            tempest_factor * 0.34);
        vec3 water_light = ambient * 0.82 + bounce_light * 0.95 + sunlight * 0.40 + torch_light * 0.55;
        vec3 water_body = scene_color * transmittance + water_volume_color * water_light * (1.0 - transmittance);

        vec3 reflected_view = reflect(-view_direction, normal);
        float horizon = clamp(reflected_view.y * 0.5 + 0.5, 0.0, 1.0);
        vec3 sky_reflection = mix(u_fog_color, u_distant_fog_color, horizon);
        sky_reflection = mix(sky_reflection, u_sun_color, 0.08 + 0.10 * daylight);

        vec3 sun_reflection = reflect(-sun_direction, normal);
        float open_sea = clamp(u_ocean_open_sea, 0.0, 1.0);
        float sparkle_power = mix(72.0, 42.0, open_sea);
        float sparkle = pow(max(dot(sun_reflection, view_direction), 0.0), sparkle_power);
        vec3 reflection = sky_reflection * fresnel * (0.18 + 0.16 * daylight);
        reflection += u_sun_color * sparkle * shadow * cloud_shadow * (0.12 + 0.18 * daylight) *
                      (1.0 - tempest_factor * 0.78);

        float shallow_foam = (1.0 - smoothstep(0.08, 0.70, body_depth)) * (0.40 + 0.60 * water_surface_mask);

        float crest_noise = 0.5;

        if (u_ocean_detail_strength > 0.000001) {
            crest_noise = value_noise2(
                v_world_position.xz * 0.54 +
                vec2(
                    cos(u_ocean_detail_phase),
                    sin(u_ocean_detail_phase * 2.0)) *
                0.34);
        }

        float slope_energy =
            clamp(
                (1.0 - normal.y) * 3.4,
                0.0,
                1.0);

        float crest_signal =
            v_ocean_crest * 0.82 +
            slope_energy * 0.28 +
            crest_noise * 0.08;

        // Je separe le reflet fin d'une crete calme de l'ecume epaisse. Cette
        // ligne bleu clair rend la houle lisible au soleil sans blanchir la mer.
        float crest_sheen =
            smoothstep(
                clamp(
                    u_ocean_foam_threshold - 0.16,
                    0.52,
                    0.78),
                0.98,
                crest_signal) *
            water_surface_mask *
            (0.18 + 0.82 * fresnel) *
            open_sea;

        float crest_foam =
            smoothstep(
                clamp(
                    u_ocean_foam_threshold,
                    0.55,
                    0.98),
                1.06,
                crest_signal) *
            water_surface_mask *
            (0.24 + 0.76 * fresnel) *
            (mix(0.20, 0.30, open_sea) +
             mix(0.80, 0.70, open_sea) *
                 clamp(
                     u_ocean_severity,
                     0.0,
                     1.0));

        // Je fais apparaître les déferlantes sur les pentes fortes de la houle
        // extrême, sans blanchir les mers ordinaires ni les eaux intérieures.
        float breaking_foam =
            smoothstep(
                0.34,
                0.92,
                slope_energy * 0.72 +
                    v_ocean_crest * 0.46 +
                    crest_noise * 0.10) *
            water_surface_mask *
            open_sea *
            tempest_factor;

        vec3 foam_color = mix(
            u_fog_color,
            vec3(0.86, 0.94, 1.0),
            0.65);

        vec3 foam =
            foam_color *
            (
                shallow_foam *
                    (0.10 + 0.06 * daylight) +
                crest_foam *
                    (0.12 + 0.11 * daylight +
                     tempest_factor * 0.10) +
                breaking_foam *
                    (0.12 + 0.10 * daylight)
            );

        foam +=
            mix(
                water_volume_color,
                foam_color,
                0.62) *
            crest_sheen *
            (0.035 + 0.040 * daylight);

        float shimmer = 0.5 + 0.5 * sin(v_world_position.x * 0.26 + v_world_position.z * 0.30 + u_time_of_day * 21.0);
        lit_color = water_body + reflection + foam;
        lit_color += water_volume_color * shimmer * (0.018 + 0.025 * daylight) * water_surface_mask;
        output_alpha = 1.0;
    }
)";

  static constexpr auto *world_fragment_shader_part3 = R"(
    float wetness = clamp(u_precipitation_intensity, 0.0, 1.0) *
                    sky_light *
                    weather_exposure *
                    (1.0 - water_mask);
    wetness *= 0.45 + 0.55 * smoothstep(-0.10, 1.0, normal.y);
    lit_color = mix(lit_color, lit_color * vec3(0.72, 0.78, 0.86), wetness * (0.16 + 0.14 * clamp(u_storm_intensity, 0.0, 1.0)));

    float lightning_surface = clamp(u_lightning_intensity, 0.0, 1.0) *
                              sky_light *
                              mix(0.16, 1.0, weather_exposure) *
                              (0.35 + 0.65 * smoothstep(-0.20, 1.0, normal.y));
    lit_color += albedo * vec3(0.62, 0.72, 1.00) * lightning_surface * (0.24 + 0.22 * clamp(u_storm_intensity, 0.0, 1.0));

    // Je fournis au pipeline Legacy le meme rebond de lecture que le pipeline
    // moderne. Le plancher de visibilite peut alors assombrir une matiere
    // encore detaillee au lieu de multiplier une couleur deja presque nulle.
    float interior_readability_energy =
        (0.15 + 0.08 * softened_block_light) *
        enclosed_interior;
    vec3 interior_readability_floor =
        albedo *
        material_tint *
        interior_readability_energy *
        mix(0.78, 1.0, clamp(v_ao, 0.0, 1.0)) *
        (1.0 - water_mask);
    lit_color = max(
        lit_color,
        interior_readability_floor);

    // Je garde les torches et lanternes historiques intactes hors Backrooms.
    // Dans l'intérieur, seule la partie réellement claire du texel émet :
    // le cadre sombre d'une rampe ne devient donc jamais une seconde ampoule.
    vec3 legacy_emission =
        vec3(1.24, 0.68, 0.24) * emissive_mask *
        (0.32 + 0.90 * block_light);
    float source_peak =
        max(max(albedo.r, albedo.g), albedo.b);
    float source_mask =
        smoothstep(0.52, 0.82, source_peak);
    vec3 backrooms_emission =
        albedo * emissive_mask * source_mask *
        (0.36 + 0.82 * softened_block_light) *
        backrooms_source_scale;
    vec3 surface_emission = mix(
        legacy_emission,
        backrooms_emission,
        enclosed_interior);
    lit_color += surface_emission;
    float super_vision = clamp(u_super_vision_strength, 0.0, 1.0) * (1.0 - daylight);
    vec3 super_floor = albedo * vec3(0.58, 0.70, 0.78);
    lit_color = mix(lit_color, max(lit_color, super_floor), super_vision * 0.72);
    lit_color += vec3(0.05, 0.13, 0.16) * super_vision * (0.50 + 0.50 * sky_light);
    float darkness_visibility =
        backrooms_darkness_visibility(
            block_light,
            flashlight_energy);
    // Je masque la matiere avant le brouillard et je restitue uniquement la
    // part emissive deja presente dans lit_color.
    lit_color =
        lit_color * darkness_visibility +
        surface_emission * (1.0 - darkness_visibility);

    vec3 view_ray = normalize(v_world_position - u_camera_position);
    float fog_distance =
        v_distance;
    if (u_maritime_horizon_enabled != 0 &&
        water_mask > 0.5 &&
        u_camera_position.y >=
            u_maritime_sea_level) {
        // Je calcule la distance sur le fragment lui-même : l'interpolation
        // par sommet dessinait sinon les deux triangles des grands quads d'eau.
        fog_distance =
            distance(
                v_world_position,
                u_camera_position);
    }
    float weather_fog = 1.0 + clamp(u_precipitation_intensity, 0.0, 1.0) * 0.42 + clamp(u_storm_intensity, 0.0, 1.0) * 0.38;
    float distance_fog = 1.0 - exp(-fog_distance * fog_distance * 0.000008 * weather_fog);
    float height_haze = 1.0 - exp(-max(30.0 - v_world_position.y, 0.0) * u_height_fog_density);
    height_haze *= clamp(fog_distance / 140.0, 0.0, 1.0) * (0.10 + 0.18 * (1.0 - daylight));
    // Dans les Backrooms, le sol est au-dessus du seuil historique y=30.
    // J'emploie donc la même densité comme brume volumétrique de profondeur,
    // ce qui différencie enfin un secteur familier d'une zone en panne.
    float interior_haze =
        (1.0 - exp(
            -fog_distance *
            u_height_fog_density *
            0.55)) *
        enclosed_interior;
    height_haze = mix(
        height_haze,
        interior_haze,
        enclosed_interior);
    float fog = clamp(distance_fog + height_haze, 0.0, 1.0);
    fog = mix(fog, fog * 0.45, super_vision);
    if (u_interior_fog_range.x >= 0.0) {
        // Je ferme la profondeur avant toute frontiere de chunk non garantie.
        // Une plage nulle masque tout pendant une couverture GPU incomplete.
        float interior_distance =
            length(v_world_position.xz - u_camera_position.xz);
        float interior_terminal_fog =
            u_interior_fog_range.y > u_interior_fog_range.x
                ? smoothstep(
                      u_interior_fog_range.x,
                      u_interior_fog_range.y,
                      interior_distance)
                : 1.0;
        fog = max(fog, interior_terminal_fog);
    }
    float sun_scatter = pow(max(dot(view_ray, sun_direction), 0.0), 6.0);
    float horizon = 1.0 - clamp(abs(view_ray.y), 0.0, 1.0);
    vec3 fog_color = mix(u_fog_color, u_distant_fog_color, sqrt(fog));
    fog_color += mix(u_horizon_glow_color, u_sun_color, 0.35 + 0.20 * daylight) *
                 sun_scatter * horizon * u_atmospheric_scatter_strength * (0.18 + 0.82 * daylight);
    vec3 final_color = mix(lit_color, fog_color, fog);
    if (u_maritime_horizon_enabled != 0 &&
        water_mask > 0.5 &&
        u_camera_position.y >=
            u_maritime_sea_level) {
        // Je réemploie la distance du même plan marin que le ciel analytique.
        // La houle ne peut donc plus changer la couleur au dernier triangle et
        // révéler la frontière diagonale du maillage d'eau détaillé.
        float maritime_plane_distance =
            fog_distance;
        if (u_camera_position.y >
            u_maritime_sea_level) {
            float maritime_eye_height =
                max(
                    u_camera_position.y -
                        u_maritime_sea_level,
                    0.35);
            maritime_plane_distance =
                min(
                    maritime_eye_height /
                        max(
                            -view_ray.y,
                            0.001),
                    4096.0);
        }
        float maritime_blend =
            u_maritime_water_blend_range.y >
                    u_maritime_water_blend_range.x
                ? smoothstep(
                      u_maritime_water_blend_range.x,
                      u_maritime_water_blend_range.y,
                      maritime_plane_distance)
                : 1.0;
        float maritime_storm =
            clamp(
                u_storm_intensity,
                0.0,
                1.0);
        vec3 maritime_clear_ocean =
            mix(
                vec3(0.018, 0.055, 0.090),
                vec3(0.045, 0.200, 0.310),
                daylight);
        vec3 maritime_ocean =
            mix(
                maritime_clear_ocean,
                vec3(0.040, 0.060, 0.085),
                maritime_storm *
                    0.58);
        float maritime_weather_fog =
            1.0 +
            clamp(
                u_precipitation_intensity,
                0.0,
                1.0) *
                0.42 +
            maritime_storm *
                0.38;
        float maritime_atmospheric_fog =
            1.0 -
            exp(
                -maritime_plane_distance *
                maritime_plane_distance *
                0.000008 *
                maritime_weather_fog);
        float maritime_terminal_fog =
            smoothstep(
                u_maritime_far_fog_range.x,
                max(
                    u_maritime_far_fog_range.y,
                    u_maritime_far_fog_range.x +
                        0.001),
                maritime_plane_distance);
        float maritime_fog =
            clamp(
                max(
                    maritime_atmospheric_fog,
                    maritime_terminal_fog),
                0.0,
                1.0);
        vec3 maritime_haze =
            mix(
                u_fog_color,
                u_distant_fog_color,
                sqrt(
                    maritime_fog));
        maritime_ocean =
            mix(
                maritime_ocean,
                maritime_haze,
                maritime_fog);
        maritime_ocean =
            mix(
                maritime_ocean,
                u_distant_fog_color,
                maritime_terminal_fog);
        final_color =
            mix(
                final_color,
                maritime_ocean,
                maritime_blend);
    }
    frag_color = vec4(final_color, output_alpha);
}
)";
  const std::string world_fragment_shader =
      std::string{world_fragment_shader_part1} + world_fragment_shader_part2 +
      world_fragment_shader_part3;

  static constexpr auto *creature_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_face_uv;
layout(location = 3) in float a_face_index;
layout(location = 4) in mat4 i_transform;
layout(location = 8) in vec4 i_uv_pos_x;
layout(location = 9) in vec4 i_uv_neg_x;
layout(location = 10) in vec4 i_uv_pos_y;
layout(location = 11) in vec4 i_uv_neg_y;
layout(location = 12) in vec4 i_uv_pos_z;
layout(location = 13) in vec4 i_uv_neg_z;
layout(location = 14) in vec4 i_surface;
layout(location = 15) in vec4 i_lighting;

uniform mat4 u_view_projection;
uniform mat4 u_light_view_projection;
uniform vec3 u_camera_position;
uniform int u_modern_pipeline;

out vec2 v_uv;
out vec3 v_normal;
out vec3 v_local_position;
out vec3 v_world_position;
out float v_distance;
out float v_nightmare_factor;
out float v_tension;
out float v_material_class;
out float v_cavity_mask;
out float v_emissive_strength;
out float v_sky_light;
out float v_block_light;
out float v_precipitation_exposure;
out vec4 v_light_position;

vec4 face_uv_rect(float face_index) {
    if (face_index < 0.5) {
        return i_uv_pos_x;
    }
    if (face_index < 1.5) {
        return i_uv_neg_x;
    }
    if (face_index < 2.5) {
        return i_uv_pos_y;
    }
    if (face_index < 3.5) {
        return i_uv_neg_y;
    }
    if (face_index < 4.5) {
        return i_uv_pos_z;
    }
    return i_uv_neg_z;
}

void main() {
    vec4 world_position = i_transform * vec4(a_position, 1.0);
    mat3 normal_matrix = transpose(inverse(mat3(i_transform)));
    vec3 world_normal = normalize(normal_matrix * a_normal);
    vec4 uv_rect = face_uv_rect(a_face_index);
    if (u_modern_pipeline != 0) {
        // Les atlas joueur et créatures font 128 px. Je reste au centre des
        // texels de bord pour profiter du filtrage linéaire sans lire la tuile
        // voisine.
        vec2 half_texel = vec2(0.5 / 128.0);
        uv_rect = vec4(
            uv_rect.xy + half_texel,
            uv_rect.zw - half_texel);
    }

    gl_Position = u_view_projection * world_position;
    v_uv = mix(uv_rect.xy, uv_rect.zw, a_face_uv);
    v_normal = world_normal;
    v_local_position = a_position;
    v_world_position = world_position.xyz;
    v_distance = distance(world_position.xyz, u_camera_position);
    v_nightmare_factor = i_surface.x;
    v_tension = i_surface.y;
    v_material_class = i_surface.z;
    v_cavity_mask = i_surface.w;
    v_emissive_strength = i_lighting.x;
    v_sky_light = i_lighting.y;
    v_block_light = i_lighting.z;
    v_precipitation_exposure = i_lighting.w;
    v_light_position = u_light_view_projection * world_position;
}
)";

  static constexpr auto *creature_fragment_shader_part1 = R"(#version 330 core
in vec2 v_uv;
in vec3 v_normal;
in vec3 v_local_position;
in vec3 v_world_position;
in float v_distance;
in float v_nightmare_factor;
in float v_tension;
in float v_material_class;
in float v_cavity_mask;
in float v_emissive_strength;
in float v_sky_light;
in float v_block_light;
in float v_precipitation_exposure;
in vec4 v_light_position;

uniform sampler2D u_atlas;
uniform sampler2D u_shadow_map;
uniform sampler2D u_shadow_map_far;
uniform mat4 u_light_view_projection_far;
uniform vec3 u_camera_position;
uniform vec3 u_camera_forward;
uniform vec3 u_sun_direction;
uniform vec3 u_sun_color;
uniform vec3 u_ambient_color;
uniform vec3 u_block_light_color;
uniform vec3 u_fog_color;
uniform vec3 u_distant_fog_color;
uniform vec3 u_horizon_glow_color;
uniform vec3 u_night_tint_color;
uniform float u_daylight_factor;
uniform float u_sun_visibility;
uniform float u_time_of_day;
uniform float u_cloud_intensity;
uniform float u_cloud_shadow_strength;
uniform float u_atmospheric_scatter_strength;
uniform float u_height_fog_density;
uniform float u_precipitation_intensity;
uniform float u_storm_intensity;
uniform float u_lightning_intensity;
uniform int u_shadows_enabled;
uniform int u_shadow_cascade_count;
uniform float u_shadow_split_distance;
uniform float u_shadow_transition_width;
uniform float u_player_light_strength;
uniform int u_enclosed_interior;
uniform vec2 u_interior_fog_range;
uniform int u_backrooms_flicker_count;
uniform vec4 u_backrooms_flicker_lights[6];
uniform float u_backrooms_flashlight_intensity;
uniform float u_super_vision_strength;
uniform vec3 u_local_light_radiance;
uniform int u_modern_pipeline;

out vec4 frag_color;

float backrooms_flashlight_irradiance(vec3 world_position) {
    float intensity =
        max(u_backrooms_flashlight_intensity, 0.0);
    if (u_enclosed_interior == 0 || intensity <= 0.0001) {
        return 0.0;
    }

    vec3 camera_to_fragment =
        world_position - u_camera_position;
    float distance_squared =
        dot(camera_to_fragment, camera_to_fragment);
    const float inverse_range_squared =
        1.0 / (34.0 * 34.0);
    float normalized_distance_squared =
        distance_squared * inverse_range_squared;
    if (normalized_distance_squared >= 1.0) {
        return 0.0;
    }

    vec3 ray_direction =
        camera_to_fragment *
        inversesqrt(max(distance_squared, 0.000001));
    float angle_cosine =
        dot(ray_direction, normalize(u_camera_forward));
    const float outer_cone_cosine = 0.913545;
    const float inner_cone_cosine = 0.974370;
    const float hotspot_cosine = 0.994522;
    const float penumbra_cone_cosine = 0.887011;
    if (angle_cosine <= penumbra_cone_cosine) {
        return 0.0;
    }

    float penumbra =
        smoothstep(
            penumbra_cone_cosine,
            outer_cone_cosine,
            angle_cosine);
    float spill =
        smoothstep(
            outer_cone_cosine,
            inner_cone_cosine,
            angle_cosine);
    float hotspot =
        smoothstep(
            inner_cone_cosine,
            hotspot_cosine,
            angle_cosine);
    float angular_profile =
        penumbra *
        mix(
            0.035,
            mix(0.30, 1.0, hotspot),
            spill);
    float range_window =
        clamp(
            1.0 -
                normalized_distance_squared *
                normalized_distance_squared,
            0.0,
            1.0);
    range_window *= range_window;
    float distance_falloff =
        range_window /
        (1.0 + 0.012 * distance_squared);
    return
        intensity *
        angular_profile *
        distance_falloff;
}

float backrooms_flicker_light_scale(
    vec3 world_position) {
    float light_scale = 1.0;
    if (u_enclosed_interior == 0) {
        return light_scale;
    }
    for (int light_index = 0;
         light_index < 6;
         ++light_index) {
        if (light_index >=
            u_backrooms_flicker_count) {
            break;
        }
        vec2 light_delta =
            world_position.xz -
            u_backrooms_flicker_lights[
                light_index].xz;
        float light_distance_squared =
            dot(
                light_delta,
                light_delta);
        float light_influence =
            1.0 -
            smoothstep(
                9.0,
                100.0,
                light_distance_squared);
        float flicker_intensity =
            clamp(
                u_backrooms_flicker_lights[
                    light_index].w,
                0.05,
                1.0);
        light_scale =
            min(
                light_scale,
                mix(
                    1.0,
                    flicker_intensity,
                    light_influence));
    }
    return light_scale;
}

float backrooms_darkness_visibility(
    float local_light,
    float flashlight_energy) {
    if (u_enclosed_interior == 0) {
        return 1.0;
    }

    float safe_local_light =
        (isnan(local_light) ||
         isinf(local_light))
            ? 0.0
            : clamp(
                  local_light,
                  0.0,
                  1.0);
    float safe_flashlight_energy =
        (isnan(flashlight_energy) ||
         isinf(flashlight_energy))
            ? 0.0
            : clamp(
                  flashlight_energy,
                  0.0,
                  1.0);
    float fixture_visibility =
        smoothstep(
            0.000,
            0.620,
            safe_local_light);
    float flashlight_visibility =
        smoothstep(
            0.000,
            0.180,
            safe_flashlight_energy);
    return clamp(
        1.0 -
            (1.0 - fixture_visibility) *
            (1.0 - flashlight_visibility),
        0.0,
        1.0);
}

float shadow_visibility_at(
    vec2 uv,
    float receiver_depth,
    float bias,
    bool far_cascade
) {
    float sampled_depth = far_cascade
        ? texture(u_shadow_map_far, uv).r
        : texture(u_shadow_map, uv).r;
    return (receiver_depth - bias) <= sampled_depth ? 1.0 : 0.0;
}

float sample_shadow_cascade(vec3 normal, bool far_cascade) {
    vec4 light_position = far_cascade
        ? u_light_view_projection_far * vec4(v_world_position, 1.0)
        : v_light_position;
    vec3 projected = light_position.xyz / max(light_position.w, 0.0001);
    projected = projected * 0.5 + 0.5;
    if (projected.z < 0.0 || projected.z > 1.0 || projected.x < 0.0 || projected.x > 1.0 || projected.y < 0.0 || projected.y > 1.0) {
        return 1.0;
    }

    vec2 texel_size = far_cascade
        ? 1.0 / vec2(textureSize(u_shadow_map_far, 0))
        : 1.0 / vec2(textureSize(u_shadow_map, 0));
    float ndotl = max(dot(normalize(normal), normalize(u_sun_direction)), 0.0);
    float bias =
        max(0.00065 * (1.0 - ndotl), 0.00012) *
        (far_cascade ? 1.35 : 1.0);
    // Je garde un PCF en croix pour lisser les ombres sans payer neuf lectures texture par fragment.
    float visibility = shadow_visibility_at(projected.xy, projected.z, bias, far_cascade) * 0.36;
    visibility += shadow_visibility_at(projected.xy + vec2(texel_size.x, 0.0), projected.z, bias, far_cascade) * 0.16;
    visibility += shadow_visibility_at(projected.xy - vec2(texel_size.x, 0.0), projected.z, bias, far_cascade) * 0.16;
    visibility += shadow_visibility_at(projected.xy + vec2(0.0, texel_size.y), projected.z, bias, far_cascade) * 0.16;
    visibility += shadow_visibility_at(projected.xy - vec2(0.0, texel_size.y), projected.z, bias, far_cascade) * 0.16;
    return visibility;
}

float sample_shadow(vec3 normal) {
    if (u_sun_visibility < 0.5 || u_shadows_enabled == 0) {
        return 1.0;
    }
    if (u_shadow_cascade_count <= 1) {
        return sample_shadow_cascade(normal, false);
    }

    float view_depth = max(
        dot(v_world_position - u_camera_position, u_camera_forward),
        0.0);
    float transition_width = max(u_shadow_transition_width, 0.0);
    if (transition_width <= 0.0001) {
        return sample_shadow_cascade(
            normal,
            view_depth > u_shadow_split_distance);
    }

    float half_width = transition_width * 0.5;
    if (view_depth <= u_shadow_split_distance - half_width) {
        return sample_shadow_cascade(normal, false);
    }
    if (view_depth >= u_shadow_split_distance + half_width) {
        return sample_shadow_cascade(normal, true);
    }

    float blend = smoothstep(
        u_shadow_split_distance - half_width,
        u_shadow_split_distance + half_width,
        view_depth);
    return mix(
        sample_shadow_cascade(normal, false),
        sample_shadow_cascade(normal, true),
        blend);
}

float hash12(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float value_noise2(vec2 p) {
    vec2 cell = floor(p);
    vec2 local = fract(p);
    vec2 blend = local * local * (3.0 - 2.0 * local);

    float n00 = hash12(cell);
    float n10 = hash12(cell + vec2(1.0, 0.0));
    float n01 = hash12(cell + vec2(0.0, 1.0));
    float n11 = hash12(cell + vec2(1.0, 1.0));
    float nx0 = mix(n00, n10, blend.x);
    float nx1 = mix(n01, n11, blend.x);
    return mix(nx0, nx1, blend.y);
}

float sample_cloud_shadow(vec3 world_position, vec3 sun_direction) {
    float cloud_factor = clamp(u_cloud_intensity, 0.0, 1.0);
    float daylight = clamp(u_daylight_factor, 0.0, 1.0);
    if (cloud_factor <= 0.01 || u_cloud_shadow_strength <= 0.001 || daylight <= 0.20 || sun_direction.y <= 0.02) {
        return 1.0;
    }

    float projection_scale = (96.0 - world_position.y) / max(sun_direction.y, 0.12);
    vec2 projected = world_position.xz + sun_direction.xz * projection_scale;
    vec2 flow = projected * 0.0032 + vec2(u_time_of_day * 0.085, -u_time_of_day * 0.061);
    float base = value_noise2(flow);
    float detail = value_noise2(flow * 2.17 + vec2(9.3, 4.7));
    float cloud = smoothstep(0.52, 0.84, base * 0.68 + detail * 0.32);
    float coverage = smoothstep(0.10, 0.58, cloud_factor);
    return 1.0 - cloud * coverage * u_cloud_shadow_strength;
}
)";

  static constexpr auto *creature_fragment_shader_part2 = R"(
void main() {
    vec4 sampled = texture(u_atlas, v_uv);
    vec3 albedo = sampled.rgb;
    float emissive_mask = sampled.a;
    vec3 normal = normalize(v_normal);
    vec3 view_direction = normalize(u_camera_position - v_world_position);
    vec3 sun_direction = normalize(u_sun_direction);

    float shadow = sample_shadow(normal);
    float cloud_shadow = sample_cloud_shadow(v_world_position + normal * 0.50, sun_direction);
    float instance_sky_light = clamp(v_sky_light, 0.0, 1.0);
    float instance_block_light =
        clamp(
            v_block_light,
            0.0,
            1.0) *
        backrooms_flicker_light_scale(
            v_world_position);
    float sky_mix = clamp(u_daylight_factor, 0.0, 1.0) * instance_sky_light;
    float super_vision = clamp(u_super_vision_strength, 0.0, 1.0) * (1.0 - sky_mix);
    float cavity = clamp(v_cavity_mask, 0.0, 1.0);
    float hard_material = smoothstep(0.44, 0.90, v_material_class);
    float soft_fiber = 1.0 - smoothstep(0.28, 0.58, v_material_class);
    float thin_surface = 1.0 - smoothstep(0.22, 0.50, v_material_class);
    if (u_modern_pipeline != 0) {
        // Je fixe le grain dans l'espace local de chaque pièce : la matière
        // suit l'animation au lieu de glisser sur l'animal quand il avance.
        float coarse_surface = value_noise2(
            v_local_position.xz * 5.4 +
            vec2(v_local_position.y * 1.7, v_material_class * 13.1));
        float fine_surface = value_noise2(
            v_local_position.xy * 12.0 +
            vec2(v_local_position.z * 2.3, v_material_class * 7.7));
        float surface_variation =
            (coarse_surface - 0.5) * 0.12 +
            (fine_surface - 0.5) * 0.045;
        float organic_surface = 1.0 - hard_material;
        albedo *= 1.0 + surface_variation * organic_surface;

        // Une variation très légère sous le volume sépare mieux le ventre et
        // les membres sans inventer un nouveau matériau ou modifier le rig.
        float underside =
            1.0 - smoothstep(-0.72, 0.10, normal.y);
        albedo = mix(
            albedo,
            albedo * 1.065 + vec3(0.006),
            underside * soft_fiber * 0.32);
    }

    float cavity_occlusion = mix(1.0, 0.54, cavity * (0.62 + 0.14 * v_nightmare_factor));
    float ambient_strength = mix(0.42, 1.02, sky_mix) * mix(1.08, 0.84, hard_material) * cavity_occlusion;
    vec3 ambient = u_ambient_color * ambient_strength;

    float wrap = mix(0.34, 0.10, hard_material);
    float sun_wrap = clamp((dot(normal, sun_direction) + wrap) / (1.0 + wrap), 0.0, 1.0);
    vec3 sunlight = u_sun_color * (sun_wrap * sky_mix * shadow * cloud_shadow * u_sun_visibility);

    float backlight = pow(max(dot(normal, -sun_direction), 0.0), 1.8);
    vec3 translucency = u_sun_color * backlight * thin_surface * sky_mix * u_sun_visibility * cloud_shadow * (0.04 + 0.10 * soft_fiber);

    float player_light_distance = length((u_camera_position + vec3(0.0, -0.18, 0.0)) - v_world_position);
    float player_light_falloff = 1.0 - smoothstep(1.2, 8.8, player_light_distance);
    float player_light_facing = 0.55 + 0.45 * max(dot(normal, view_direction), 0.0);
    float player_light_night_boost = mix(0.18, 1.0, 1.0 - sky_mix);
    vec3 player_light =
        vec3(1.18, 0.78, 0.36) * u_player_light_strength * player_light_falloff * player_light_facing * player_light_night_boost;
    float local_light_facing = 0.62 + 0.38 * max(dot(normal, view_direction), 0.0);
    vec3 local_light = vec3(0.0);
    if (instance_block_light > 0.0001) {
        // Je fais rebondir la lumière chaude sur la couleur propre du
        // vêtement : le bleu, le rouge et le bordeaux restent distincts sous
        // un même fanal. Je n'évalue la racine que pour un sujet éclairé.
        vec3 modern_local_albedo =
            min(
                mix(albedo, sqrt(max(albedo, vec3(0.0))), 0.32) * 1.25,
                vec3(1.0));
        vec3 local_light_albedo =
            u_modern_pipeline != 0
                ? modern_local_albedo
                : 0.34 + 0.42 * albedo;
        local_light =
            u_local_light_radiance *
            instance_block_light *
            local_light_facing *
            local_light_albedo;
    }

    float rim = pow(1.0 - max(dot(view_direction, normal), 0.0), 2.45);
    vec3 rim_light = mix(vec3(0.12, 0.10, 0.08), vec3(0.34, 0.50, 0.60), 1.0 - sky_mix);
    rim_light *= rim * mix(0.08, 0.16, 1.0 - hard_material) * mix(0.78, 1.04, v_nightmare_factor);
    vec3 super_vision_glow = mix(vec3(0.12, 0.72, 0.90), vec3(1.00, 0.24, 0.14), v_nightmare_factor);
    super_vision_glow *= super_vision * (0.24 + 0.32 * rim + 0.18 * emissive_mask + 0.16 * v_nightmare_factor);

    vec3 reflected = reflect(-sun_direction, normal);
    float specular = pow(max(dot(reflected, view_direction), 0.0), mix(42.0, 16.0, hard_material));
    float hard_specular = specular * smoothstep(0.52, 0.90, v_material_class);
    vec3 specular_color = u_sun_color * hard_specular * shadow * cloud_shadow * sky_mix * u_sun_visibility * (0.03 + 0.18 * v_nightmare_factor);

    float pulse = 0.84 + 0.16 * sin(u_time_of_day * 1.7 + v_tension * 7.0 + v_world_position.y * 2.2);
    vec3 nightmare_glow =
        vec3(1.00, 0.18, 0.12) * emissive_mask * v_emissive_strength * v_nightmare_factor * (0.24 + v_tension * 0.30) * pulse;

    vec3 lit_color = albedo * (ambient + sunlight + translucency + player_light) + local_light;
    float flashlight_energy =
        backrooms_flashlight_irradiance(v_world_position);
    if (flashlight_energy > 0.0) {
        float flashlight_incidence =
            max(dot(normal, view_direction), 0.0);
        float contact_visibility =
            mix(0.72, 1.0, 1.0 - cavity);
        vec3 flashlight_radiance =
            vec3(1.00, 0.92, 0.76) *
            (3.60 * flashlight_energy);
        // Je laisse la Maglite reveler Jack dans le cone sans eclairer toute
        // la salle : son corps sort du noir uniquement la ou le faisceau passe.
        lit_color +=
            albedo *
            flashlight_radiance *
            mix(0.20, 1.0, flashlight_incidence) *
            contact_visibility;
    }
    lit_color *= cavity_occlusion;
    lit_color += rim_light + specular_color;
    lit_color = mix(lit_color, max(lit_color, albedo * vec3(0.62, 0.82, 0.88)), super_vision * 0.72);
    lit_color += u_night_tint_color * (0.09 + 0.08 * v_nightmare_factor) * (1.0 - sky_mix);
    float wetness = clamp(u_precipitation_intensity, 0.0, 1.0) * clamp(v_precipitation_exposure, 0.0, 1.0) *
                    (0.40 + 0.60 * smoothstep(-0.10, 1.0, normal.y));
    lit_color = mix(lit_color, lit_color * vec3(0.74, 0.80, 0.88), wetness * (0.12 + 0.12 * clamp(u_storm_intensity, 0.0, 1.0)));
    float lightning_surface = clamp(u_lightning_intensity, 0.0, 1.0) * instance_sky_light *
                              clamp(v_precipitation_exposure, 0.0, 1.0) *
                              (0.35 + 0.65 * smoothstep(-0.20, 1.0, normal.y));
    lit_color += albedo * vec3(0.62, 0.72, 1.00) * lightning_surface * (0.18 + 0.22 * clamp(u_storm_intensity, 0.0, 1.0));
    vec3 view_ray = normalize(v_world_position - u_camera_position);
    float weather_fog = 1.0 + clamp(u_precipitation_intensity, 0.0, 1.0) * 0.38 + clamp(u_storm_intensity, 0.0, 1.0) * 0.34;
    float distance_fog = 1.0 - exp(-v_distance * v_distance * 0.000009 * weather_fog);
    float height_haze = 1.0 - exp(-max(28.0 - v_world_position.y, 0.0) * u_height_fog_density);
    height_haze *= clamp(v_distance / 130.0, 0.0, 1.0) * (0.08 + 0.12 * (1.0 - sky_mix));
    float interior_haze =
        (1.0 - exp(
            -v_distance *
            u_height_fog_density *
            0.55)) *
        float(u_enclosed_interior);
    height_haze = mix(
        height_haze,
        interior_haze,
        float(u_enclosed_interior));
    float fog = clamp(distance_fog + height_haze, 0.0, 1.0);
    fog = mix(fog, fog * 0.38, super_vision);
    if (u_interior_fog_range.x >= 0.0) {
        float interior_distance =
            length(v_world_position.xz - u_camera_position.xz);
        float interior_terminal_fog =
            u_interior_fog_range.y > u_interior_fog_range.x
                ? smoothstep(
                      u_interior_fog_range.x,
                      u_interior_fog_range.y,
                      interior_distance)
                : 1.0;
        fog = max(fog, interior_terminal_fog);
    }
    float sun_scatter = pow(max(dot(view_ray, sun_direction), 0.0), 6.0);
    float horizon = 1.0 - clamp(abs(view_ray.y), 0.0, 1.0);
    vec3 fog_color = mix(u_fog_color, u_distant_fog_color, sqrt(fog));
    fog_color += mix(u_horizon_glow_color, u_sun_color, 0.34 + 0.20 * sky_mix) *
                 sun_scatter * horizon * u_atmospheric_scatter_strength * (0.16 + 0.78 * sky_mix);
    vec3 fogged_color = mix(lit_color, fog_color, fog);
    vec3 fogged_glow = (nightmare_glow + super_vision_glow) * (1.0 - fog * 0.72);
    float darkness_visibility =
        backrooms_darkness_visibility(
            instance_block_light,
            flashlight_energy);
    // Je masque la matiere dans le noir, mais je conserve l'emission apres le
    // brouillard : les yeux de Jack restent un indice lointain, pas une lampe.
    vec3 final_color =
        fogged_color * darkness_visibility +
        fogged_glow;
    frag_color = vec4(final_color, 1.0);
}
)";
  const std::string creature_fragment_shader =
      std::string{creature_fragment_shader_part1} +
      creature_fragment_shader_part2;

  static constexpr auto *creature_shadow_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 4) in mat4 i_transform;

uniform mat4 u_light_view_projection;

void main() {
    gl_Position = u_light_view_projection * i_transform * vec4(a_position, 1.0);
}
)";

  static constexpr auto *creature_shadow_fragment_shader = R"(#version 330 core
void main() {
}
)";

  static constexpr auto *shadow_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 7) in float a_material_class;
layout(location = 8) in float a_wave_weight;

uniform mat4 u_model;
uniform mat4 u_light_view_projection;
uniform float u_time_of_day;
uniform float u_wind_strength;

out vec2 v_uv;
flat out float v_material_class;

float material_mask(float material, float expected) {
    return 1.0 - step(0.25, abs(material - expected));
}

vec2 vegetation_wind_offset(vec3 world_position, float material_class, float time_phase) {
    float foliage_mask = material_mask(material_class, 4.0);
    float flora_mask = material_mask(material_class, 5.0);
    float wind_mask = max(foliage_mask * 0.35, flora_mask);
    if (wind_mask <= 0.0) {
        return vec2(0.0);
    }

    float gust_a = sin(world_position.x * 0.18 + world_position.z * 0.11 + time_phase * 1.35);
    float gust_b = cos(world_position.x * -0.13 + world_position.z * 0.21 + time_phase * 1.65);
    float flutter = sin((world_position.x + world_position.z) * 0.75 + world_position.y * 0.45 + time_phase * 2.40);
    float local_height = clamp(fract(world_position.y), 0.0, 1.0);
    local_height = mix(1.0, smoothstep(0.02, 0.98, local_height), flora_mask);
    float amplitude = u_wind_strength * wind_mask * mix(0.010, 0.032, flora_mask);
    return vec2(gust_a * 0.70 + flutter * 0.30, gust_b * 0.60 - gust_a * 0.22) * amplitude * local_height;
}

vec3 fabric_wind_offset(
    vec3 world_position,
    float material_class,
    float vertex_weight,
    float time_phase
) {
    float fabric_mask = material_mask(material_class, 9.0);
    float flexibility = clamp(vertex_weight, 0.0, 1.0) * fabric_mask;
    if (flexibility <= 0.0) {
        return vec3(0.0);
    }

    float wind = clamp(u_wind_strength, 0.0, 1.0);
    vec2 wind_direction = normalize(vec2(0.82, 0.57));
    vec2 transverse = vec2(-wind_direction.y, wind_direction.x);
    float phase =
        dot(world_position.xz, vec2(0.17, 0.11)) +
        world_position.y * 0.19 +
        time_phase * 1.24;
    float billow = sin(phase) + sin(phase * 2.13 + 0.7) * 0.28;
    float flutter = sin(phase * 3.71 - world_position.y * 0.31);
    float amplitude = flexibility * (0.014 + wind * 0.082);
    vec2 horizontal =
        wind_direction * billow * amplitude +
        transverse * flutter * amplitude * 0.24;
    return vec3(
        horizontal.x,
        flutter * amplitude * 0.055,
        horizontal.y);
}

void main() {
    vec4 world_position = u_model * vec4(a_position, 1.0);
    world_position.xz += vegetation_wind_offset(world_position.xyz, a_material_class, u_time_of_day * 8.0);
    world_position.xyz += fabric_wind_offset(
        world_position.xyz,
        a_material_class,
        a_wave_weight,
        u_time_of_day * 8.0);
    gl_Position = u_light_view_projection * world_position;
    v_uv = a_uv;
    v_material_class = a_material_class;
}
)";

  static constexpr auto *shadow_fragment_shader = R"(#version 330 core
in vec2 v_uv;
flat in float v_material_class;

uniform sampler2D u_atlas;

float material_mask(float material, float expected) {
    return 1.0 - step(0.25, abs(material - expected));
}

float ordered_alpha_threshold(vec2 pixel_position) {
    const float pattern[16] = float[](
         0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
         3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0);
    ivec2 cell = ivec2(mod(floor(pixel_position), 4.0));
    return (pattern[cell.x + cell.y * 4] + 0.5) / 16.0;
}

void main() {
    float alpha = texture(u_atlas, v_uv).a;
    float glass_mask = material_mask(v_material_class, 10.0);
    float threshold = glass_mask > 0.5 ? ordered_alpha_threshold(gl_FragCoord.xy) : 0.1;
    if (alpha < threshold) {
        discard;
    }
}
)";

  static constexpr auto *modern_ship_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_normal;
layout(location = 3) in float a_face_shade;
layout(location = 4) in float a_ao;
layout(location = 5) in float a_sky_light;
layout(location = 6) in float a_block_light;
layout(location = 7) in float a_ship_material;
layout(location = 8) in float a_wave_weight;

uniform mat4 u_model;
uniform mat4 u_view_projection;
uniform mat4 u_light_view_projection;
uniform vec3 u_camera_position;
uniform float u_time_seconds;
uniform float u_wind_strength;
uniform float u_exterior_light_activation;

out vec2 v_uv;
out vec3 v_local_position;
out vec3 v_world_position;
out vec3 v_local_normal;
out vec3 v_world_normal;
out vec4 v_light_position;
out float v_distance;
out float v_face_shade;
out float v_ao;
out float v_sky_light;
out float v_block_light;
flat out int v_ship_material;

vec3 sail_offset(vec3 local_position, int material_id, float weight) {
    if (material_id != 9 || weight <= 0.0) {
        return vec3(0.0);
    }
    float wind = clamp(u_wind_strength, 0.0, 1.0);
    float flexibility = clamp(weight, 0.0, 1.0);
    vec2 wind_direction = normalize(vec2(0.82, 0.57));
    vec2 transverse = vec2(-wind_direction.y, wind_direction.x);
    float phase =
        dot(local_position.xz, vec2(0.17, 0.11)) +
        local_position.y * 0.19 +
        u_time_seconds * 1.24;
    float billow = sin(phase) + sin(phase * 2.13 + 0.7) * 0.28;
    float flutter = sin(phase * 3.71 - local_position.y * 0.31);
    float amplitude = flexibility * (0.014 + wind * 0.082);
    vec2 horizontal =
        wind_direction * billow * amplitude +
        transverse * flutter * amplitude * 0.24;
    return vec3(
        horizontal.x,
        flutter * amplitude * 0.055,
        horizontal.y);
}

void main() {
    int material_id = int(floor(a_ship_material + 0.5));
    vec3 local_position =
        a_position +
        sail_offset(
            a_position,
            material_id,
            a_wave_weight);
    vec4 world_position =
        u_model *
        vec4(local_position, 1.0);
    mat3 normal_matrix =
        transpose(
            inverse(
                mat3(u_model)));

    v_uv = a_uv;
    v_local_position = local_position;
    v_world_position = world_position.xyz;
    v_local_normal = normalize(a_normal);
    v_world_normal =
        normalize(
            normal_matrix *
            a_normal);
    v_light_position =
        u_light_view_projection *
        world_position;
    v_distance =
        distance(
            world_position.xyz,
            u_camera_position);
    v_face_shade = a_face_shade;
    v_ao = a_ao;
    v_sky_light = a_sky_light;
    // Je déplace l'activation uniforme et l'exclusion du fanal au sommet :
    // l'interpolation conserve la même courbe et allège chaque fragment.
    float exterior_orientation =
        0.40 +
        0.60 *
            smoothstep(
                -0.20,
                0.85,
                a_normal.y);
    v_block_light =
        material_id != 7
            ? a_block_light *
                  u_exterior_light_activation *
                  exterior_orientation
            : 0.0;
    v_ship_material = material_id;
    gl_Position =
        u_view_projection *
        world_position;
}
)";

  static constexpr auto *modern_ship_fragment_shader_part1 =
      R"(#version 330 core
in vec2 v_uv;
in vec3 v_local_position;
in vec3 v_world_position;
in vec3 v_local_normal;
in vec3 v_world_normal;
in vec4 v_light_position;
in float v_distance;
in float v_face_shade;
in float v_ao;
in float v_sky_light;
in float v_block_light;
flat in int v_ship_material;

uniform sampler2DArray u_material_albedo;
uniform sampler2DArray u_material_normal_height;
uniform sampler2DArray u_material_orm_emission;
uniform sampler2D u_shadow_map;
uniform sampler2D u_shadow_map_far;
uniform mat4 u_model;
uniform mat4 u_light_view_projection_far;
uniform vec3 u_camera_position;
uniform vec3 u_camera_local_position;
uniform vec3 u_camera_forward;
uniform vec3 u_sun_direction;
uniform vec3 u_sun_color;
uniform vec3 u_ambient_color;
uniform vec3 u_fog_color;
uniform vec3 u_distant_fog_color;
uniform vec3 u_night_tint_color;
uniform float u_daylight_factor;
uniform float u_sun_visibility;
uniform float u_precipitation_intensity;
uniform float u_storm_intensity;
uniform float u_lightning_intensity;
uniform vec3 u_exterior_light_radiance;
uniform float u_material_detail_scale;
uniform int u_shadows_enabled;
uniform int u_shadow_cascade_count;
uniform float u_shadow_split_distance;
uniform float u_shadow_transition_width;
uniform float u_time_seconds;
uniform float u_material_layers[18];
uniform int u_light_count;
uniform vec4 u_light_position_radius[24];
uniform vec4 u_light_color_intensity[24];
uniform vec4 u_light_zone_min_spill[24];
uniform vec4 u_light_zone_max_seed[24];
uniform vec4 u_light_doorways[24];

out vec4 frag_color;

float saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

vec3 safe_normalize(vec3 value, vec3 fallback) {
    float length_squared = dot(value, value);
    if (!(length_squared > 0.00000001) ||
        any(isnan(value)) ||
        any(isinf(value))) {
        return fallback;
    }
    return value * inversesqrt(length_squared);
}

float material_scale(int material_id) {
    if (material_id == 4) return 1.45;
    if (material_id == 5 || material_id == 6 || material_id == 10) return 1.08;
    if (material_id == 3 || material_id == 9 ||
        material_id == 12 || material_id == 13 || material_id == 14) return 0.88;
    if (material_id == 16) return 1.20;
    if (material_id == 17) return 1.05;
    return 0.70;
}

float normal_strength(int material_id) {
    if (material_id == 3 || material_id == 9 ||
        material_id == 12 || material_id == 13 || material_id == 14 ||
        material_id == 16) return 0.34;
    if (material_id == 5 || material_id == 6 || material_id == 10) return 0.58;
    if (material_id == 8 || material_id == 17) return 0.18;
    return 0.72;
}

float amelie_interior_half_width(vec3 local_position) {
    bool bow_side =
        local_position.z >= 0.0;
    float extent =
        bow_side
            ? 36.50
            : 35.50;
    float width_loss =
        bow_side
            ? 7.35
            : 2.00;
    float exponent =
        bow_side
            ? 1.65
            : 1.35;
    float progression =
        clamp(
            abs(local_position.z) /
                extent,
            0.0,
            1.0);
    float outer_half_width =
        max(
            8.75 - width_loss,
            8.75 -
                width_loss *
                    pow(
                        progression,
                        exponent));
    float band_half_width =
        outer_half_width;
    if (local_position.y < -4.05) {
        band_half_width =
            max(
                1.00,
                outer_half_width -
                    2.25);
    } else if (
        local_position.y < -1.05) {
        band_half_width =
            max(
                1.25,
                outer_half_width -
                    1.05);
    }
    float wall_thickness =
        min(
            0.44,
            max(
                0.22,
                band_half_width *
                    0.36));
    return
        max(
            0.48,
            band_half_width -
                wall_thickness);
}

float amelie_interior_mask(vec3 local_position) {
    if (local_position.z < -35.50 ||
        local_position.z > 36.50 ||
        local_position.y < -5.08 ||
        local_position.y > 3.70) {
        return 0.0;
    }
    // Je reproduis ici l'enveloppe utilisée pour poser le mobilier : une
    // lanterne ne peut donc jamais éclairer la face extérieure du bordé.
    return
        1.0 -
        step(
            amelie_interior_half_width(
                local_position) +
                0.06,
            abs(
                local_position.x));
}

float ordered_alpha_threshold(vec2 pixel_position) {
    const float pattern[16] = float[](
         0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
         3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0);
    ivec2 cell =
        ivec2(
            mod(
                floor(pixel_position),
                4.0));
    return
        (pattern[cell.x + cell.y * 4] + 0.5) /
        16.0;
}

mat3 cotangent_frame(vec3 normal, vec3 position, vec2 uv) {
    vec3 dp1 = dFdx(position);
    vec3 dp2 = dFdy(position);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2_perpendicular = cross(dp2, normal);
    vec3 dp1_perpendicular = cross(normal, dp1);
    vec3 tangent =
        dp2_perpendicular * duv1.x +
        dp1_perpendicular * duv2.x;
    vec3 bitangent =
        dp2_perpendicular * duv1.y +
        dp1_perpendicular * duv2.y;
    float inverse_maximum =
        inversesqrt(
            max(
                max(
                    dot(tangent, tangent),
                    dot(bitangent, bitangent)),
                0.00000001));
    return mat3(
        tangent * inverse_maximum,
        bitangent * inverse_maximum,
        normal);
}

float shadow_sample(
    vec3 projected,
    vec2 offset,
    vec2 texel,
    float bias,
    bool far_cascade) {
    float depth =
        far_cascade
            ? texture(
                  u_shadow_map_far,
                  projected.xy +
                      offset *
                          texel).r
            : texture(
                  u_shadow_map,
                  projected.xy +
                      offset *
                          texel).r;
    return
        projected.z - bias <= depth
            ? 1.0
            : 0.0;
}

float shadow_for_cascade(vec3 normal, bool far_cascade) {
    vec4 light_position =
        far_cascade
            ? u_light_view_projection_far *
                  vec4(v_world_position, 1.0)
            : v_light_position;
    vec3 projected =
        light_position.xyz /
        max(light_position.w, 0.0001);
    projected =
        projected * 0.5 +
        0.5;
    if (projected.z < 0.0 || projected.z > 1.0 ||
        any(lessThan(projected.xy, vec2(0.0))) ||
        any(greaterThan(projected.xy, vec2(1.0)))) {
        return 1.0;
    }
    vec2 texel =
        far_cascade
            ? 1.0 /
                  vec2(
                      textureSize(
                          u_shadow_map_far,
                          0))
            : 1.0 /
                  vec2(
                      textureSize(
                          u_shadow_map,
                          0));
    float ndotl =
        max(
            dot(
                normal,
                normalize(
                    u_sun_direction)),
            0.0);
    float bias =
        max(
            0.00062 *
                (1.0 - ndotl),
            0.00010) *
        (far_cascade ? 1.35 : 1.0);
    float visibility =
        shadow_sample(projected, vec2(0.0), texel, bias, far_cascade) *
        0.36;
    visibility +=
        shadow_sample(projected, vec2(1.0, 0.0), texel, bias, far_cascade) *
        0.16;
    visibility +=
        shadow_sample(projected, vec2(-1.0, 0.0), texel, bias, far_cascade) *
        0.16;
    visibility +=
        shadow_sample(projected, vec2(0.0, 1.0), texel, bias, far_cascade) *
        0.16;
    visibility +=
        shadow_sample(projected, vec2(0.0, -1.0), texel, bias, far_cascade) *
        0.16;
    return visibility;
}

float shadow_visibility(vec3 normal) {
    if (u_shadows_enabled == 0 ||
        u_sun_visibility < 0.5) {
        return 1.0;
    }
    if (u_shadow_cascade_count <= 1) {
        return shadow_for_cascade(normal, false);
    }
    float view_depth =
        max(
            dot(
                v_world_position -
                    u_camera_position,
                u_camera_forward),
            0.0);
    float half_width =
        max(
            u_shadow_transition_width,
            0.0) *
        0.5;
    if (half_width <= 0.0001) {
        return shadow_for_cascade(
            normal,
            view_depth >
                u_shadow_split_distance);
    }
    float blend =
        smoothstep(
            u_shadow_split_distance -
                half_width,
            u_shadow_split_distance +
                half_width,
            view_depth);
    return mix(
        shadow_for_cascade(normal, false),
        shadow_for_cascade(normal, true),
        blend);
}
)";

  static constexpr auto *modern_ship_fragment_shader_part2 = R"(
void main() {
    int material_id =
        clamp(
            v_ship_material,
            0,
            17);
    float layer =
        u_material_layers[material_id];
    vec2 uv =
        v_uv *
        material_scale(material_id);
    vec4 albedo_sample =
        texture(
            u_material_albedo,
            vec3(uv, layer));
    if (material_id == 8 &&
        albedo_sample.a <
            ordered_alpha_threshold(
                gl_FragCoord.xy)) {
        discard;
    }
    vec4 normal_height =
        texture(
            u_material_normal_height,
            vec3(uv, layer));
    vec4 orm =
        texture(
            u_material_orm_emission,
            vec3(uv, layer));

    vec3 geometric_normal =
        safe_normalize(
            v_world_normal,
            vec3(0.0, 1.0, 0.0));
    vec3 tangent_normal =
        normal_height.xyz *
            2.0 -
        1.0;
    tangent_normal.xy *=
        normal_strength(material_id) *
        clamp(
            u_material_detail_scale,
            0.0,
            1.0);
    tangent_normal =
        safe_normalize(
            tangent_normal,
            vec3(0.0, 0.0, 1.0));
    vec3 normal =
        safe_normalize(
            cotangent_frame(
                geometric_normal,
                v_world_position,
                uv) *
                tangent_normal,
            geometric_normal);

    vec3 albedo =
        albedo_sample.rgb;
    vec3 soft_bounce_albedo =
        sqrt(
            max(
                albedo,
                vec3(0.0)));
    float occlusion =
        mix(
            0.26,
            1.0,
            saturate(
                orm.r *
                v_ao));
    float roughness =
        clamp(
            orm.g,
            0.08,
            1.0);
    float metallic =
        saturate(
            orm.b);
    float emission =
        saturate(
            orm.a);
    vec3 view_direction =
        safe_normalize(
            u_camera_position -
                v_world_position,
            geometric_normal);
    vec3 sun_direction =
        safe_normalize(
            u_sun_direction,
            vec3(0.0, 1.0, 0.0));
    float daylight =
        saturate(
            u_daylight_factor);
    float sky =
        saturate(
            v_sky_light);
    float exposed_deck =
        material_id == 1
            ? smoothstep(
                  0.78,
                  0.98,
                  sky)
            : 0.0;
    float wetness =
        saturate(
            u_precipitation_intensity) *
        sky *
        smoothstep(
            -0.15,
            0.85,
            normal.y);
    // Je réduis la rugosité du bois réellement mouillé avant de calculer les
    // reflets. Le pont conserve ainsi sa matière sans rester artificiellement
    // mat sous la pluie.
    roughness =
        mix(
            roughness,
            min(
                roughness,
                0.50),
            wetness *
                exposed_deck);
    float enclosure =
        1.0 -
        smoothstep(
            0.12,
            0.58,
            sky);
    float ndotl =
        max(
            dot(normal, sun_direction),
            0.0);
    float shadow =
        shadow_visibility(normal);
    vec3 f0 =
        mix(
            vec3(0.04),
            albedo,
            metallic);
    vec3 half_vector =
        safe_normalize(
            view_direction +
                sun_direction,
            normal);
    float sun_specular =
        pow(
            max(
                dot(normal, half_vector),
                0.0),
            mix(
                10.0,
                96.0,
                1.0 - roughness));
    vec3 ambient =
        u_ambient_color *
        albedo *
        mix(
            0.16,
            0.72,
            daylight) *
        mix(
            0.32,
            1.0,
            sky) *
        occlusion;
    vec3 enclosed_bounce =
        soft_bounce_albedo *
        mix(
            vec3(0.055, 0.035, 0.022),
            vec3(0.240, 0.140, 0.070),
            daylight) *
        enclosure *
        mix(
            0.55,
            1.0,
            occlusion);
    vec3 color =
        ambient +
        enclosed_bounce +
        (
            albedo *
                (1.0 - metallic) *
                ndotl +
            f0 *
                sun_specular *
                ndotl
        ) *
            u_sun_color *
            shadow *
            u_sun_visibility *
            daylight *
            mix(
                0.10,
                1.0,
                smoothstep(
                    0.12,
                    0.80,
                    sky));

    float interior_hull_mask =
        amelie_interior_mask(
            v_local_position);
    float room_enclosure = 0.0;
    if (interior_hull_mask > 0.0) {
        vec3 local_normal =
            safe_normalize(
                transpose(
                    mat3(u_model)) *
                    normal,
                v_local_normal);
        for (int index = 0;
             index < 24;
             ++index) {
        if (index >= u_light_count) {
            break;
        }
        vec4 position_radius =
            u_light_position_radius[index];
        vec4 color_intensity =
            u_light_color_intensity[index];
        vec4 zone_min_spill =
            u_light_zone_min_spill[index];
        vec4 zone_max_seed =
            u_light_zone_max_seed[index];
        vec4 doorways =
            u_light_doorways[index];
        float spill =
            max(
                zone_min_spill.w,
                0.001);
        if (v_local_position.x <
                zone_min_spill.x ||
            v_local_position.x >
                zone_max_seed.x ||
            v_local_position.y <
                zone_min_spill.y ||
            v_local_position.y >
                zone_max_seed.y) {
            continue;
        }
        float outside_distance =
            0.0;
        vec2 doorway =
            vec2(0.0);
        if (v_local_position.z <
                zone_min_spill.z) {
            outside_distance =
                zone_min_spill.z -
                v_local_position.z;
            doorway =
                doorways.xy;
        } else if (
            v_local_position.z >
                zone_max_seed.z) {
            outside_distance =
                v_local_position.z -
                zone_max_seed.z;
            doorway =
                doorways.zw;
        }
        if (outside_distance > spill ||
            (outside_distance > 0.0 &&
             (doorway.y <= 0.0 ||
              abs(
                  v_local_position.x -
                  doorway.x) >
                  doorway.y))) {
            continue;
        }
        float zone_weight =
            outside_distance > 0.0
                ? 1.0 -
                      smoothstep(
                          0.0,
                          spill,
                          outside_distance)
                : 1.0;
        if (zone_weight <= 0.0001) {
            continue;
        }
        // Je considère la pièce comme fermée dès que le fragment appartient à
        // la zone de sa lanterne. Les bordés intérieurs touchent aussi la face
        // extérieure de la coque : leur seule valeur de ciel ne suffit donc pas
        // à les distinguer d'une ouverture.
        room_enclosure =
            max(
                room_enclosure,
                zone_weight);
        vec3 to_light =
            position_radius.xyz -
            v_local_position;
        float distance_to_light =
            length(to_light);
        float radius =
            max(
                position_radius.w,
                0.01);
        if (distance_to_light >= radius) {
            continue;
        }
        float remaining =
            saturate(
                1.0 -
                distance_to_light /
                    radius);
        float radial =
            remaining *
            remaining *
            (3.0 -
             2.0 *
                 remaining);
        float flicker_wave =
            sin(
                u_time_seconds *
                    6.7 +
                zone_max_seed.w *
                    17.0) *
                0.028 +
            sin(
                u_time_seconds *
                    13.1 +
                zone_max_seed.w *
                    31.0) *
                0.012;
        float flicker =
            1.0 +
            flicker_wave;
        vec3 light_direction =
            distance_to_light >
                    0.0001
                ? to_light /
                      distance_to_light
                : local_normal;
        float local_ndotl =
            max(
                dot(
                    local_normal,
                    light_direction),
                0.0);
        vec3 local_half =
            safe_normalize(
                light_direction +
                    safe_normalize(
                        u_camera_local_position -
                            v_local_position,
                        local_normal),
                local_normal);
        float local_specular =
            pow(
                max(
                    dot(
                        local_normal,
                        local_half),
                    0.0),
                mix(
                    10.0,
                    72.0,
                    1.0 - roughness));
        float energy =
            color_intensity.w *
            zone_weight *
            radial *
            flicker *
            1.30;
        vec3 local_diffuse_albedo =
            mix(
                albedo,
                soft_bounce_albedo,
                0.24);
        color +=
            color_intensity.rgb *
            energy *
            (
                local_diffuse_albedo *
                    (0.30 +
                     local_ndotl *
                         0.70) *
                    (1.0 - metallic) +
                f0 *
                    local_specular *
                    local_ndotl
            );
        }
    }

    float interior_enclosure =
        max(
            enclosure,
            room_enclosure);
    float missing_interior_fill =
        max(
            room_enclosure -
                enclosure,
            0.0);
    // Je restitue ici la lumière indirecte du bois et des cloisons. Ce faible
    // rebond garde les bordés brun sombre au lieu de les confondre avec un trou
    // noir, sans éclairer la coque extérieure ni traverser les portes.
    color +=
        soft_bounce_albedo *
        mix(
            vec3(0.038, 0.032, 0.026),
            vec3(0.125, 0.094, 0.066),
            daylight) *
        missing_interior_fill *
        mix(
            0.62,
            1.0,
            occlusion);

    float nocturnal_deck_bounce =
        exposed_deck *
        (1.0 - daylight) *
        mix(
            0.72,
            1.0,
            saturate(
                u_storm_intensity));
    // Je garde un très faible rebond froid sur le seul chêne exposé. Il rend
    // les lames lisibles entre deux fanaux sans éclaircir la coque, les voiles
    // ou les ponts fermés.
    color +=
        soft_bounce_albedo *
        vec3(0.026, 0.036, 0.052) *
        nocturnal_deck_bounce *
        occlusion;

    color +=
        albedo *
        emission *
        vec3(1.34, 0.70, 0.24);
    if (material_id == 7) {
        color +=
            vec3(1.24, 0.58, 0.16) *
            (0.55 +
             emission *
                 0.85);
    }
    color +=
        albedo *
        vec3(0.62, 0.72, 1.0) *
        saturate(
            u_lightning_intensity) *
        sky *
        0.35;
    color +=
        u_night_tint_color *
        (1.0 - daylight) *
        0.025 *
        mix(
            1.0,
            0.12,
            interior_enclosure);

    color =
        mix(
            color,
            color *
                vec3(0.68, 0.75, 0.84),
            wetness *
                (
                    0.12 +
                    saturate(
                        u_storm_intensity) *
                        0.10
                ));
    // Je reçois déjà une valeur finie et bornée depuis la cuisson puis le
    // sommet ; l'interpolation ne peut pas sortir de cet intervalle.
    float exterior_light =
        v_block_light;
    if (exterior_light > 0.0001) {
        vec3 exterior_light_albedo =
            mix(
                albedo,
                soft_bounce_albedo,
                0.30);
        float wet_sheen = 0.0;
        if (exposed_deck > 0.0001 &&
            wetness > 0.0001) {
            float grazing_base =
                1.0 -
                max(
                    dot(
                        normal,
                        view_direction),
                    0.0);
            // Je développe le cube afin de ne pas payer un pow par fragment du
            // pont pendant la tempête, sans changer la courbe du reflet.
            float grazing =
                grazing_base *
                grazing_base *
                grazing_base;
            wet_sheen =
                wetness *
                (1.0 - roughness) *
                (
                    0.045 +
                    grazing *
                        0.095
                );
        }
        // Je module la lumière chaude par l'albédo : le bois, les cordages et
        // le métal gardent leur couleur propre, tandis que le fanal émissif
        // n'est jamais éclairé une seconde fois par sa propre cuisson.
        color +=
            u_exterior_light_radiance *
            exterior_light *
            (
                exterior_light_albedo *
                    (1.0 - metallic) *
                    1.08 +
                f0 *
                    wet_sheen
            );
    }
    float fog =
        1.0 -
        exp(
            -v_distance *
             v_distance *
             0.000008 *
             (
                 1.0 +
                 saturate(
                     u_storm_intensity) *
                     0.38
             ));
    vec3 fog_color =
        mix(
            u_fog_color,
            u_distant_fog_color,
            saturate(fog));
    frag_color =
        vec4(
            mix(
                max(color, vec3(0.0)),
                fog_color,
                saturate(fog)),
            1.0);
}
)";
  const std::string modern_ship_fragment_shader =
      std::string{modern_ship_fragment_shader_part1} +
      modern_ship_fragment_shader_part2;

  static constexpr auto *modern_ship_shadow_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 7) in float a_ship_material;
layout(location = 8) in float a_wave_weight;

uniform mat4 u_model;
uniform mat4 u_light_view_projection;
uniform float u_time_seconds;
uniform float u_wind_strength;

out vec2 v_uv;
flat out int v_ship_material;

vec3 sail_offset(vec3 local_position, int material_id, float weight) {
    if (material_id != 9 || weight <= 0.0) {
        return vec3(0.0);
    }
    float wind = clamp(u_wind_strength, 0.0, 1.0);
    float flexibility = clamp(weight, 0.0, 1.0);
    vec2 wind_direction = normalize(vec2(0.82, 0.57));
    vec2 transverse = vec2(-wind_direction.y, wind_direction.x);
    float phase =
        dot(local_position.xz, vec2(0.17, 0.11)) +
        local_position.y * 0.19 +
        u_time_seconds * 1.24;
    float billow = sin(phase) + sin(phase * 2.13 + 0.7) * 0.28;
    float flutter = sin(phase * 3.71 - local_position.y * 0.31);
    float amplitude = flexibility * (0.014 + wind * 0.082);
    vec2 horizontal =
        wind_direction * billow * amplitude +
        transverse * flutter * amplitude * 0.24;
    return vec3(
        horizontal.x,
        flutter * amplitude * 0.055,
        horizontal.y);
}

void main() {
    int material_id =
        int(
            floor(
                a_ship_material +
                0.5));
    vec3 local_position =
        a_position +
        sail_offset(
            a_position,
            material_id,
            a_wave_weight);
    gl_Position =
        u_light_view_projection *
        u_model *
        vec4(local_position, 1.0);
    v_uv = a_uv;
    v_ship_material = material_id;
}
)";

  static constexpr auto *modern_ship_shadow_fragment_shader =
      R"(#version 330 core
in vec2 v_uv;
flat in int v_ship_material;

uniform sampler2DArray u_material_albedo;
uniform float u_material_layers[18];

float ordered_alpha_threshold(vec2 pixel_position) {
    const float pattern[16] = float[](
         0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
         3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0);
    ivec2 cell =
        ivec2(
            mod(
                floor(pixel_position),
                4.0));
    return
        (pattern[cell.x + cell.y * 4] + 0.5) /
        16.0;
}

void main() {
    if (v_ship_material != 8) {
        return;
    }
    int material_id =
        clamp(
            v_ship_material,
            0,
            17);
    float alpha =
        texture(
            u_material_albedo,
            vec3(
                v_uv,
                u_material_layers[material_id])).a;
    if (alpha <
        ordered_alpha_threshold(
            gl_FragCoord.xy)) {
        discard;
    }
}
)";

  static constexpr auto *hud_vertex_shader = R"(#version 330 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in float a_textured;

out vec2 v_uv;
out vec4 v_color;
flat out float v_textured;

void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_uv = a_uv;
    v_color = a_color;
    v_textured = a_textured;
}
)";

  static constexpr auto *hud_fragment_shader = R"(#version 330 core
in vec2 v_uv;
in vec4 v_color;
flat in float v_textured;

uniform sampler2D u_atlas;
uniform sampler2D u_font_atlas;
uniform sampler2DArray u_model_icon_atlas;
uniform sampler2D u_jack_screamer;

out vec4 frag_color;

float median3(vec3 value) {
    return max(min(value.r, value.g), min(max(value.r, value.g), value.b));
}

void main() {
    vec4 color = v_color;
    if (v_textured > 63.5) {
        color *= texture(u_jack_screamer, v_uv);
    } else if (v_textured > 2.5) {
        float layer = floor(v_textured - 3.0 + 0.5);
        color *= texture(u_model_icon_atlas, vec3(v_uv, layer));
    } else if (v_textured > 1.5) {
        float signed_distance = median3(texture(u_font_atlas, v_uv).rgb);
        float antialias_width = max(fwidth(signed_distance), 1.0 / 255.0);
        float coverage = smoothstep(
            0.5 - antialias_width,
            0.5 + antialias_width,
            signed_distance);
        color.a *= coverage;
    } else if (v_textured > 0.5) {
        color *= texture(u_atlas, v_uv);
    }
    frag_color = color;
}
)";

  static constexpr auto *crosshair_vertex_shader = R"(#version 330 core
layout(location = 0) in vec2 a_position;

void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

  static constexpr auto *crosshair_fragment_shader = R"(#version 330 core
out vec4 frag_color;

void main() {
    frag_color = vec4(0.98, 0.98, 0.98, 1.0);
}
)";

  static constexpr auto *screen_vertex_shader = R"(#version 330 core
out vec2 v_uv;

void main() {
    vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vec2 clip = positions[gl_VertexID];
    gl_Position = vec4(clip, 0.0, 1.0);
    v_uv = clip * 0.5 + 0.5;
}
)";

  const auto *sky_fragment_shader = kSkyFragmentShaderSource.c_str();

  static constexpr auto *glow_extract_fragment_shader = R"(#version 330 core
in vec2 v_uv;

uniform sampler2D u_scene_texture;
uniform float u_threshold;

out vec4 frag_color;

void main() {
    vec3 color = texture(u_scene_texture, v_uv).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    // Je conserve progressivement les hautes lumières autour du seuil afin
    // d'éviter un halo qui apparaît brutalement d'une image à l'autre.
    float knee = max(u_threshold * 0.45, 0.0001);
    float soft = clamp(luminance - u_threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.0001);
    float bloom = max(luminance - u_threshold, soft);
    frag_color = vec4(color * bloom / max(luminance, 0.0001), 1.0);
}
)";

  static constexpr auto *precipitation_vertex_shader = R"(#version 330 core
layout(location = 0) in vec2 a_quad_position;
layout(location = 1) in vec4 i_position_length;
layout(location = 2) in vec4 i_velocity_width;
layout(location = 3) in vec4 i_appearance;

uniform mat4 u_view_projection;
uniform vec3 u_camera_position;
uniform vec3 u_camera_right;
uniform vec3 u_camera_up;

out vec2 v_uv;
out vec3 v_world_position;
out float v_opacity;
out float v_kind;
out float v_age_ratio;

void main() {
    vec3 instance_position = i_position_length.xyz;
    float length_value = max(i_position_length.w, 0.001);
    vec3 velocity = i_velocity_width.xyz;
    float width_value = max(i_velocity_width.w, 0.001);
    float kind = i_appearance.y;
    vec3 world_position;

    if (kind < 0.5) {
        vec3 fall_direction = normalize(
            dot(velocity, velocity) > 0.000001
                ? velocity
                : vec3(0.0, -1.0, 0.0));
        vec3 view_direction = normalize(u_camera_position - instance_position);
        vec3 side = cross(fall_direction, view_direction);
        if (dot(side, side) <= 0.00001) {
            side = u_camera_right;
        } else {
            side = normalize(side);
        }
        world_position =
            instance_position +
            fall_direction * ((a_quad_position.y - 0.5) * length_value) +
            side * (a_quad_position.x * width_value);
    } else {
        float age = clamp(i_appearance.z, 0.0, 1.0);
        float radius = max(i_appearance.w, 0.01) * mix(0.62, 1.18, age);
        world_position =
            instance_position +
            u_camera_right * (a_quad_position.x * radius * 2.0) +
            u_camera_up * (a_quad_position.y * radius);
    }

    gl_Position = u_view_projection * vec4(world_position, 1.0);
    v_uv = vec2(a_quad_position.x + 0.5, a_quad_position.y);
    v_world_position = world_position;
    v_opacity = clamp(i_appearance.x, 0.0, 1.0);
    v_kind = kind;
    v_age_ratio = clamp(i_appearance.z, 0.0, 1.0);
}
)";

  static constexpr auto *precipitation_fragment_shader = R"(#version 330 core
in vec2 v_uv;
in vec3 v_world_position;
in float v_opacity;
in float v_kind;
in float v_age_ratio;

uniform vec3 u_camera_position;
uniform vec3 u_fog_color;
uniform float u_lightning_intensity;
uniform float u_storm_intensity;

out vec4 frag_color;
)" VALCRAFT_SHIP_PROTECTION_GLSL_SOURCE R"(

void main() {
    if (ship_shelters_weather(v_world_position) ||
        (v_kind >= 0.5 &&
         ship_excludes_ocean(v_world_position))) {
        discard;
    }

    float alpha;
    vec3 color;
    if (v_kind < 0.5) {
        float horizontal = 1.0 - smoothstep(0.18, 0.50, abs(v_uv.x - 0.5));
        float head = smoothstep(0.0, 0.10, v_uv.y);
        float tail = 1.0 - smoothstep(0.70, 1.0, v_uv.y);
        alpha = horizontal * head * tail * v_opacity;
        color = mix(vec3(0.48, 0.62, 0.78), vec3(0.82, 0.91, 1.0), v_uv.y);
    } else {
        float centered_x = abs(v_uv.x - 0.5) * 2.0;
        float crown = 1.0 - smoothstep(0.08, 0.82, centered_x);
        float stem = 1.0 - smoothstep(0.12, 0.72, v_uv.y);
        float droplets = smoothstep(0.18, 0.55, v_uv.y) *
                         (1.0 - smoothstep(0.58, 1.0, v_uv.y)) *
                         (0.55 + 0.45 * cos((v_uv.x - 0.5) * 18.0));
        alpha = max(crown * stem * 0.52, droplets) *
                (1.0 - v_age_ratio) *
                v_opacity;
        color = vec3(0.58, 0.78, 0.92);
    }

    float distance_fade =
        1.0 - smoothstep(22.0, 62.0, distance(v_world_position, u_camera_position));
    alpha *= distance_fade;
    if (alpha <= 0.003) {
        discard;
    }

    float lightning = clamp(u_lightning_intensity, 0.0, 1.0);
    float storm = clamp(u_storm_intensity, 0.0, 1.0);
    color = mix(color, u_fog_color + vec3(0.10, 0.16, 0.24), storm * 0.22);
    color += vec3(0.52, 0.62, 0.90) * lightning * 0.65;
    frag_color = vec4(color, alpha);
}
)";

  static constexpr auto *old_guard_effect_vertex_shader = R"(#version 330 core
layout(location = 0) in vec2 a_quad_position;
layout(location = 1) in vec4 i_position_size;
layout(location = 2) in vec4 i_appearance;

uniform mat4 u_view_projection;
uniform vec3 u_camera_right;
uniform vec3 u_camera_up;

out vec2 v_uv;
out float v_opacity;
out float v_kind;
out float v_intensity;

void main() {
    float angle = i_appearance.z;
    float sine = sin(angle);
    float cosine = cos(angle);
    vec2 rotated = vec2(
        a_quad_position.x * cosine - a_quad_position.y * sine,
        a_quad_position.x * sine + a_quad_position.y * cosine);
    float size_value = max(i_position_size.w, 0.001);
    vec3 world_position =
        i_position_size.xyz +
        u_camera_right * rotated.x * size_value +
        u_camera_up * rotated.y * size_value;
    gl_Position = u_view_projection * vec4(world_position, 1.0);
    v_uv = a_quad_position + vec2(0.5);
    v_opacity = clamp(i_appearance.x, 0.0, 1.0);
    v_kind = i_appearance.y;
    v_intensity = max(i_appearance.w, 0.0);
}
)";

  static constexpr auto *old_guard_effect_fragment_shader = R"(#version 330 core
in vec2 v_uv;
in float v_opacity;
in float v_kind;
in float v_intensity;

out vec4 frag_color;

float smoke_noise(vec2 point) {
    return fract(sin(dot(point, vec2(37.13, 91.73))) * 43758.5453);
}

void main() {
    vec2 centered = v_uv - vec2(0.5);
    float radius = length(centered);
    float alpha;
    vec3 color;

    if (v_kind < 0.5) {
        float body = 1.0 - smoothstep(0.18, 0.52, radius);
        float turbulence =
            0.78 +
            0.22 * smoke_noise(floor(v_uv * 9.0) + vec2(v_intensity * 3.1));
        alpha = body * turbulence * v_opacity;
        color = mix(
            vec3(0.46, 0.48, 0.50),
            vec3(0.77, 0.75, 0.69),
            clamp(v_intensity, 0.0, 1.0));
    } else {
        float core = 1.0 - smoothstep(0.02, 0.22, radius);
        float horizontal =
            1.0 - smoothstep(0.015, 0.11, abs(centered.y));
        float vertical =
            1.0 - smoothstep(0.015, 0.10, abs(centered.x));
        float star = max(core, max(horizontal, vertical) * (1.0 - radius * 1.65));
        alpha = clamp(star, 0.0, 1.0) * v_opacity;
        color = mix(vec3(1.0, 0.34, 0.05), vec3(1.0, 0.94, 0.62), core);
        color *= 1.0 + min(v_intensity, 2.0) * 1.4;
    }

    if (alpha <= 0.003) {
        discard;
    }
    frag_color = vec4(color, alpha);
}
)";

  static constexpr auto *glow_blur_fragment_shader = R"(#version 330 core
in vec2 v_uv;

uniform sampler2D u_source_texture;
uniform vec2 u_texel_direction;

out vec4 frag_color;

void main() {
    vec3 color = texture(u_source_texture, v_uv).rgb * 0.227027;
    color += texture(u_source_texture, v_uv + u_texel_direction * 1.384615).rgb * 0.316216;
    color += texture(u_source_texture, v_uv - u_texel_direction * 1.384615).rgb * 0.316216;
    color += texture(u_source_texture, v_uv + u_texel_direction * 3.230769).rgb * 0.070270;
    color += texture(u_source_texture, v_uv - u_texel_direction * 3.230769).rgb * 0.070270;
    frag_color = vec4(color, 1.0);
}
)";

  static constexpr auto *post_process_fragment_shader = R"(#version 330 core
in vec2 v_uv;

uniform sampler2D u_scene_texture;
uniform sampler2D u_glow_texture;
uniform sampler2D u_scene_depth;
uniform float u_exposure;
uniform float u_saturation_boost;
uniform float u_contrast;
uniform float u_vignette_strength;
uniform vec3 u_night_tint_color;
uniform float u_glow_strength;
uniform float u_sharpen_strength;
uniform float u_edge_strength;
uniform int u_fxaa_enabled;
uniform int u_modern_pipeline;
uniform int u_resolve_only;
uniform float u_storm_intensity;
uniform float u_lightning_intensity;
uniform float u_weather_exposure;
uniform float u_projection_far_distance;
uniform int u_maritime_submerged;
uniform float u_maritime_submersion_depth;
uniform float u_maritime_submersion_blend;
uniform float u_water_surface_detail;
uniform float u_time_seconds;
uniform float u_daylight_factor;

out vec4 frag_color;

vec3 apply_saturation(vec3 color, float saturation) {
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(luma), color, saturation);
}

vec3 sample_scene(vec2 uv) {
    return texture(u_scene_texture, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
}

float scene_luma(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

vec3 sample_scene_fxaa(vec2 uv, vec2 texel) {
    vec3 center = sample_scene(uv);
    if (u_fxaa_enabled == 0) {
        return center;
    }

    vec3 north_west = sample_scene(uv + vec2(-texel.x, texel.y));
    vec3 north_east = sample_scene(uv + vec2(texel.x, texel.y));
    vec3 south_west = sample_scene(uv + vec2(-texel.x, -texel.y));
    vec3 south_east = sample_scene(uv + vec2(texel.x, -texel.y));
    float luma_center = scene_luma(center);
    float luma_north_west = scene_luma(north_west);
    float luma_north_east = scene_luma(north_east);
    float luma_south_west = scene_luma(south_west);
    float luma_south_east = scene_luma(south_east);
    float luma_min = min(
        luma_center,
        min(
            min(luma_north_west, luma_north_east),
            min(luma_south_west, luma_south_east)));
    float luma_max = max(
        luma_center,
        max(
            max(luma_north_west, luma_north_east),
            max(luma_south_west, luma_south_east)));
    if (luma_max - luma_min < max(0.0312, luma_max * 0.125)) {
        return center;
    }

    vec2 direction;
    direction.x =
        -((luma_north_west + luma_north_east) -
          (luma_south_west + luma_south_east));
    direction.y =
        (luma_north_west + luma_south_west) -
        (luma_north_east + luma_south_east);
    float direction_reduce = max(
        (luma_north_west + luma_north_east +
         luma_south_west + luma_south_east) *
            0.03125,
        0.0078125);
    float reciprocal_minimum =
        1.0 / (min(abs(direction.x), abs(direction.y)) + direction_reduce);
    direction =
        clamp(direction * reciprocal_minimum, vec2(-8.0), vec2(8.0)) *
        texel;

    vec3 result_a =
        0.5 *
        (sample_scene(uv + direction * (1.0 / 3.0 - 0.5)) +
         sample_scene(uv + direction * (2.0 / 3.0 - 0.5)));
    vec3 result_b =
        result_a * 0.5 +
        0.25 *
        (sample_scene(uv + direction * -0.5) +
         sample_scene(uv + direction * 0.5));
    float result_b_luma = scene_luma(result_b);
    return result_b_luma < luma_min || result_b_luma > luma_max
               ? result_a
               : result_b;
}

vec3 aces_fitted(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp(
        (color * (a * color + b)) /
            (color * (c * color + d) + e),
        0.0,
        1.0);
}

float linearize_depth(float depth_sample) {
    const float near_plane = 0.1;
    float far_plane =
        max(
            u_projection_far_distance,
            near_plane + 0.001);
    float z = depth_sample * 2.0 - 1.0;
    return (2.0 * near_plane * far_plane) / max(far_plane + near_plane - z * (far_plane - near_plane), 0.0001);
}

bool depth_sample_is_usable(float depth_sample) {
    return depth_sample > 0.00001 && depth_sample < 0.9999;
}

vec3 apply_palette_grade(vec3 color, float storm, float lightning) {
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    vec3 shadow_tint = vec3(0.93, 0.97, 1.05);
    vec3 highlight_tint = vec3(1.055, 1.010, 0.940);
    vec3 graded = color;
    graded *= mix(shadow_tint, vec3(1.0), smoothstep(0.18, 0.55, luma));
    graded *= mix(vec3(1.0), highlight_tint, smoothstep(0.48, 0.94, luma));
    float grade_strength = (0.34 + 0.10 * smoothstep(0.10, 0.78, luma)) * (1.0 - storm * 0.28) * (1.0 - lightning * 0.55);
    return mix(color, graded, clamp(grade_strength, 0.0, 0.46));
}

vec2 maritime_distorted_uv(
    vec2 uv,
    vec2 texel
) {
    if (u_maritime_submerged == 0) {
        return uv;
    }
    float detail =
        clamp(
            u_water_surface_detail,
            0.0,
            1.0);
    float blend =
        clamp(
            u_maritime_submersion_blend,
            0.0,
            1.0);
    vec2 shimmer =
        vec2(
            sin(
                uv.y * 37.0 +
                u_time_seconds * 1.17),
            cos(
                uv.x * 31.0 -
                u_time_seconds * 0.93));
    return
        clamp(
            uv +
                shimmer *
                    texel *
                    detail *
                    blend *
                    1.35,
            texel * 0.5,
            vec2(1.0) - texel * 0.5);
}

vec3 apply_maritime_submersion(
    vec3 scene,
    vec2 scene_uv
) {
    if (u_maritime_submerged == 0) {
        return scene;
    }

    float blend =
        clamp(
            u_maritime_submersion_blend,
            0.0,
            1.0);
    float depth_sample =
        texture(
            u_scene_depth,
            scene_uv)
            .r;
    float optical_distance =
        depth_sample_is_usable(depth_sample)
            ? linearize_depth(depth_sample)
            : u_projection_far_distance * 0.12;
    optical_distance =
        clamp(
            optical_distance,
            0.0,
            48.0);
    vec3 absorption =
        vec3(0.120, 0.055, 0.025);
    float daylight =
        clamp(
            u_daylight_factor,
            0.0,
            1.0);
    float storm =
        clamp(
            u_storm_intensity,
            0.0,
            1.0);
    float visibility_loss =
        mix(1.35, 1.0, daylight) *
        mix(1.0, 1.38, storm);
    vec3 transmittance =
        exp(
            -absorption *
            optical_distance *
            mix(0.32, 0.58, blend) *
            visibility_loss);
    float camera_depth =
        clamp(
            u_maritime_submersion_depth / 24.0,
            0.0,
            1.0);
    vec3 scattering_color =
        mix(
            vec3(0.018, 0.125, 0.155),
            vec3(0.010, 0.050, 0.072),
            clamp(
                camera_depth * 0.72 +
                    storm * 0.52,
                0.0,
                1.0)) *
        mix(0.52, 1.0, daylight);
    vec3 submerged =
        scene * transmittance +
        scattering_color *
            (vec3(1.0) - transmittance) *
            (0.72 + 0.28 * blend);

    float shaft_pattern =
        0.5 +
        0.5 *
            sin(
                scene_uv.x * 73.0 +
                scene_uv.y * 19.0 +
                u_time_seconds * 0.31);
    float shaft_quality =
        smoothstep(
            0.84,
            0.90,
            clamp(
                u_water_surface_detail,
                0.0,
                1.0));
    float shaft =
        pow(shaft_pattern, 7.0) *
        daylight *
        (1.0 - storm) *
        shaft_quality *
        exp(-camera_depth * 2.2) *
        0.018;
    submerged +=
        vec3(0.20, 0.38, 0.34) *
        shaft;
    return
        mix(
            scene,
            submerged,
            blend);
}

void main() {
    vec2 texel = 1.0 / vec2(textureSize(u_scene_texture, 0));
    vec2 scene_uv =
        maritime_distorted_uv(
            v_uv,
            texel);
    vec3 scene =
        u_resolve_only != 0
            ? sample_scene(scene_uv)
            : sample_scene_fxaa(scene_uv, texel);
    scene =
        apply_maritime_submersion(
            scene,
            scene_uv);

    if (u_resolve_only != 0) {
        // Même lorsque les effets optionnels sont désactivés, la cible de la
        // version moderne reste HDR et linéaire. Ce chemin minimal effectue
        // uniquement la conversion HDR -> SDR et l'encodage gamma.
        vec3 non_negative_scene = max(scene, vec3(0.0));
        vec3 color =
            u_modern_pipeline != 0
                ? aces_fitted(
                      non_negative_scene *
                      max(u_exposure, 0.001))
                : vec3(1.0) -
                      exp(
                          -non_negative_scene *
                          max(u_exposure, 0.001));
        color = pow(
            clamp(color, 0.0, 1.0),
            vec3(1.0 / 2.2));
        frag_color = vec4(color, 1.0);
        return;
    }

    float depth_edge = 0.0;
    float geometry_mask = 0.0;

    // Je coupe réellement les lectures voisines quand le profil désactive les détails.
    if (max(u_edge_strength, u_sharpen_strength) > 0.001) {
        float center_depth_sample = texture(u_scene_depth, v_uv).r;
        if (depth_sample_is_usable(center_depth_sample)) {
            float center_depth = linearize_depth(center_depth_sample);
            geometry_mask = 1.0 - smoothstep(120.0, 280.0, center_depth);

            if (u_edge_strength > 0.001) {
                float left_depth_sample = texture(u_scene_depth, v_uv + vec2(-texel.x, 0.0)).r;
                float right_depth_sample = texture(u_scene_depth, v_uv + vec2(texel.x, 0.0)).r;
                float down_depth_sample = texture(u_scene_depth, v_uv + vec2(0.0, -texel.y)).r;
                float up_depth_sample = texture(u_scene_depth, v_uv + vec2(0.0, texel.y)).r;

                float left_weight = depth_sample_is_usable(left_depth_sample) ? 1.0 : 0.0;
                float right_weight = depth_sample_is_usable(right_depth_sample) ? 1.0 : 0.0;
                float down_weight = depth_sample_is_usable(down_depth_sample) ? 1.0 : 0.0;
                float up_weight = depth_sample_is_usable(up_depth_sample) ? 1.0 : 0.0;
                float neighbor_count = left_weight + right_weight + down_weight + up_weight;

                if (neighbor_count > 0.0) {
                    float left_depth = linearize_depth(left_depth_sample);
                    float right_depth = linearize_depth(right_depth_sample);
                    float down_depth = linearize_depth(down_depth_sample);
                    float up_depth = linearize_depth(up_depth_sample);

                    depth_edge = abs(left_depth - center_depth) * left_weight +
                                 abs(right_depth - center_depth) * right_weight +
                                 abs(down_depth - center_depth) * down_weight +
                                 abs(up_depth - center_depth) * up_weight;
                    depth_edge = (depth_edge / neighbor_count) / max(center_depth * 0.75, 1.0);
                    depth_edge = smoothstep(0.002, 0.035, depth_edge);
                }
            }
        }
    }
    scene *= 1.0 - depth_edge * u_edge_strength * geometry_mask;

    if (u_sharpen_strength > 0.001) {
        vec3 blur = scene * 0.50;
        blur += sample_scene(v_uv + vec2(texel.x, 0.0)) * 0.125;
        blur += sample_scene(v_uv + vec2(-texel.x, 0.0)) * 0.125;
        blur += sample_scene(v_uv + vec2(0.0, texel.y)) * 0.125;
        blur += sample_scene(v_uv + vec2(0.0, -texel.y)) * 0.125;

        vec3 detail = scene - blur;
        float sharpen_mask = geometry_mask * (1.0 - depth_edge * 0.85);
        scene += detail * u_sharpen_strength * sharpen_mask;
    }

    vec3 glow = texture(u_glow_texture, v_uv).rgb * u_glow_strength;
    vec3 color = scene + glow;
    float weather_exposure = clamp(u_weather_exposure, 0.0, 1.0);
    float flash_exposure = mix(0.12, 1.0, weather_exposure);
    vec3 lightning =
        vec3(0.62, 0.72, 1.00) *
        clamp(u_lightning_intensity, 0.0, 1.0) *
        flash_exposure *
        (0.08 + clamp(u_storm_intensity, 0.0, 1.0) * 0.12);
    if (u_modern_pipeline != 0) {
        color = aces_fitted(
            max(color + lightning, vec3(0.0)) *
            max(u_exposure, 0.001));
    } else {
        // Je conserve strictement la courbe et l'ordre historiques pour que
        // LegacyVoxel reste une référence visuelle fiable de la refonte.
        color = vec3(1.0) -
                exp(-color * max(u_exposure, 0.001));
    }
    color = apply_saturation(color, u_saturation_boost);
    color = (color - 0.5) * u_contrast + 0.5;
    color = apply_palette_grade(color, clamp(u_storm_intensity, 0.0, 1.0), clamp(u_lightning_intensity, 0.0, 1.0));
    if (u_modern_pipeline == 0) {
        color += lightning;
    }

    float vignette = smoothstep(0.92, 0.22, distance(v_uv, vec2(0.5)));
    color *= mix(1.0 - u_vignette_strength, 1.0, vignette);
    color = mix(color, color + u_night_tint_color * 0.28, clamp(length(u_night_tint_color) * 2.0, 0.0, 1.0));
    color = pow(clamp(color, 0.0, 16.0), vec3(1.0 / 2.2));
    frag_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";

  static constexpr auto *menu_background_fragment_shader = R"(#version 330 core
in vec2 v_uv;

uniform sampler2D u_scene_texture;
uniform sampler2D u_blur_texture;
uniform float u_blur_mix;
uniform vec3 u_tint_color;
uniform float u_vignette_strength;
uniform float u_exposure;
uniform int u_modern_pipeline;

out vec4 frag_color;

vec3 aces_fitted(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp(
        (color * (a * color + b)) /
            (color * (c * color + d) + e),
        0.0,
        1.0);
}

void main() {
    vec3 scene = texture(u_scene_texture, v_uv).rgb;
    vec3 blurred = texture(u_blur_texture, v_uv).rgb;
    vec3 color = mix(scene, blurred, clamp(u_blur_mix, 0.0, 1.0));
    color = mix(color, color * u_tint_color, 0.22);
    float vignette = smoothstep(0.94, 0.18, distance(v_uv, vec2(0.5)));
    color *= mix(1.0 - u_vignette_strength, 1.0, vignette);

    // La prévisualisation moderne est elle aussi rendue dans une cible HDR
    // linéaire. Sans tone mapping, les hautes lumières du menu étaient
    // écrêtées avant même l'encodage gamma.
    if (u_modern_pipeline != 0) {
        color = aces_fitted(
            max(color, vec3(0.0)) *
            max(u_exposure, 0.001));
    }
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    frag_color = vec4(color, 1.0);
}
)";

  static constexpr auto *sea_horizon_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in float a_block_id;

uniform mat4 u_view_projection;

out vec3 v_normal;
out vec3 v_albedo;
out vec3 v_world_position;

vec3 surface_albedo(float block_id) {
    if (abs(block_id - 6.0) < 0.5) {
        return vec3(0.055, 0.20, 0.32);
    }
    if (abs(block_id - 4.0) < 0.5) {
        return vec3(0.52, 0.38, 0.19);
    }
    if (abs(block_id - 3.0) < 0.5 ||
        abs(block_id - 8.0) < 0.5 ||
        abs(block_id - 10.0) < 0.5 ||
        abs(block_id - 11.0) < 0.5) {
        return vec3(0.27, 0.29, 0.30);
    }
    if (abs(block_id - 12.0) < 0.5) {
        return vec3(0.74, 0.79, 0.82);
    }
    if (abs(block_id - 5.0) < 0.5 ||
        abs(block_id - 9.0) < 0.5 ||
        abs(block_id - 13.0) < 0.5) {
        return vec3(0.25, 0.14, 0.065);
    }
    return vec3(0.18, 0.34, 0.105);
}

void main() {
    vec4 world_position =
        vec4(
            a_position,
            1.0);
    gl_Position =
        u_view_projection *
        world_position;
    v_normal =
        normalize(
            a_normal);
    v_albedo =
        surface_albedo(
            a_block_id);
    v_world_position =
        world_position.xyz;
}
)";

  static constexpr auto *sea_horizon_fragment_shader = R"(#version 330 core
in vec3 v_normal;
in vec3 v_albedo;
in vec3 v_world_position;

uniform vec3 u_camera_position;
uniform vec3 u_sun_direction;
uniform vec3 u_sun_color;
uniform vec3 u_ambient_color;
uniform vec3 u_fog_color;
uniform vec3 u_distant_fog_color;
uniform float u_daylight_factor;
uniform float u_precipitation_intensity;
uniform float u_storm_intensity;
uniform float u_lightning_intensity;
uniform vec2 u_far_fog_range;
uniform float u_sea_level;
uniform vec2 u_detail_transition_range;
uniform int u_transition_pass;

out vec4 frag_color;

float ordered_transition_threshold(vec2 pixel_position) {
    const float pattern[16] = float[](
         0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
         3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0);
    ivec2 cell =
        ivec2(
            mod(
                floor(pixel_position),
                4.0));
    return
        (pattern[cell.x + cell.y * 4] +
         0.5) /
        16.0;
}

void main() {
    float v_distance =
        distance(
            v_world_position,
            u_camera_position);
    float horizontal_distance =
        length(
            v_world_position.xz -
            u_camera_position.xz);
    // Sous l'eau, le vrai fond proche se fond dans son volume marin. Afficher
    // les silhouettes émergées du proxy à travers ce volume recréerait une
    // bande d'îles irréaliste au milieu de l'eau.
    if (u_camera_position.y <
        u_sea_level) {
        discard;
    }
    bool submerged =
        v_world_position.y <
            u_sea_level - 0.25;
    // Je ne dessine jamais de fond marin proxy. Seule la frange de plage
    // traversant le niveau de la mer subsiste, sans plaque visible sous l'eau.
    if (submerged) {
        discard;
    }
    if (!submerged &&
        u_transition_pass != 0) {
        if (u_detail_transition_range.y <=
            u_detail_transition_range.x) {
            discard;
        }
        float proxy_coverage =
            smoothstep(
                u_detail_transition_range.x,
                u_detail_transition_range.y,
                horizontal_distance);
        float dither_threshold =
            ordered_transition_threshold(
                gl_FragCoord.xy);
        if (proxy_coverage <= dither_threshold) {
            discard;
        }
    }

    vec3 normal =
        normalize(
            v_normal);
    float back_face =
        gl_FrontFacing
            ? 0.0
            : 1.0;
    normal =
        mix(
            normal,
            -normal,
            back_face);
    vec3 sun_direction =
        normalize(
            u_sun_direction);
    float daylight =
        clamp(
            u_daylight_factor,
            0.0,
            1.0);
    float direct =
        max(
            dot(
                normal,
                sun_direction),
            0.0) *
        daylight;
    float upward =
        smoothstep(
            -0.25,
            1.0,
            normal.y);
    vec3 lighting =
        u_ambient_color *
            mix(
                0.58,
                0.94,
                upward) +
        u_sun_color *
            direct *
            0.78;
    vec3 color =
        v_albedo *
        lighting;
    if (submerged) {
        float water_column =
            max(
                u_sea_level -
                    v_world_position.y,
                0.0);
        color *=
            mix(
                vec3(0.68, 0.88, 0.90),
                vec3(0.42, 0.68, 0.74),
                smoothstep(
                    3.0,
                    30.0,
                    water_column));
    }
    color *=
        mix(
            1.0,
            0.46,
            back_face);
    color +=
        v_albedo *
        vec3(0.62, 0.72, 1.0) *
        clamp(
            u_lightning_intensity,
            0.0,
            1.0) *
        0.22;

    float weather_fog =
        1.0 +
        clamp(
            u_precipitation_intensity,
            0.0,
            1.0) *
            0.42 +
        clamp(
            u_storm_intensity,
            0.0,
            1.0) *
            0.38;
    float atmospheric_fog =
        1.0 -
        exp(
            -v_distance *
            v_distance *
            0.000008 *
            weather_fog);
    float terminal_fog =
        smoothstep(
            u_far_fog_range.x,
            max(
                u_far_fog_range.y,
                u_far_fog_range.x +
                    0.001),
            v_distance);
    float fog =
        clamp(
            max(
                atmospheric_fog,
                terminal_fog),
            0.0,
            1.0);
    if (submerged) {
        // Je fonds le relief marin grossier bien avant son bord géométrique :
        // aucune ligne de chunks ne peut apparaître dans l'eau profonde.
        fog =
            max(
                fog,
                smoothstep(
                    96.0,
                    192.0,
                    horizontal_distance));
    }
    vec3 fog_color =
        mix(
            u_fog_color,
            u_distant_fog_color,
            sqrt(
                fog));
    if (submerged) {
        fog_color =
            mix(
                vec3(0.012, 0.060, 0.085),
                u_distant_fog_color,
                sqrt(
                    fog));
    }
    frag_color =
        vec4(
            mix(
                color,
                fog_color,
                fog),
            1.0);
}
)";

  world_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, world_vertex_shader),
      compile_shader(GL_FRAGMENT_SHADER, world_fragment_shader.c_str()));
  modern_water_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, kModernWaterVertexShaderSource.data()),
      compile_shader(GL_FRAGMENT_SHADER,
                     modern_water_fragment_shader_source().c_str()));
  sea_horizon_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, sea_horizon_vertex_shader),
      compile_shader(GL_FRAGMENT_SHADER, sea_horizon_fragment_shader));
  modern_terrain_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, kModernTerrainVertexShaderSource.data()),
      compile_shader(GL_FRAGMENT_SHADER,
                     kModernTerrainFragmentShaderSource.data()));
  modern_architecture_program_ =
      link_program(compile_shader(GL_VERTEX_SHADER,
                                  kModernArchitectureVertexShaderSource.data()),
                   compile_shader(GL_FRAGMENT_SHADER,
                                  kModernTerrainFragmentShaderSource.data()));
  modern_terrain_shadow_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER,
                     kModernTerrainShadowVertexShaderSource.data()),
      compile_shader(GL_FRAGMENT_SHADER,
                     kModernTerrainShadowFragmentShaderSource.data()));
  modern_ship_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, modern_ship_vertex_shader),
      compile_shader(GL_FRAGMENT_SHADER, modern_ship_fragment_shader.c_str()));
  modern_ship_shadow_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, modern_ship_shadow_vertex_shader),
      compile_shader(GL_FRAGMENT_SHADER, modern_ship_shadow_fragment_shader));
  item_drop_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, item_drop_vertex_shader),
      compile_shader(GL_FRAGMENT_SHADER, world_fragment_shader.c_str()));
  precipitation_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, precipitation_vertex_shader),
      compile_shader(GL_FRAGMENT_SHADER, precipitation_fragment_shader));
  old_guard_effect_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, old_guard_effect_vertex_shader),
      compile_shader(GL_FRAGMENT_SHADER, old_guard_effect_fragment_shader));
  creature_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, creature_vertex_shader),
      compile_shader(
          GL_FRAGMENT_SHADER,
          creature_fragment_shader.c_str()));
  creature_shadow_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, creature_shadow_vertex_shader),
      compile_shader(GL_FRAGMENT_SHADER, creature_shadow_fragment_shader));
  shadow_program_ =
      link_program(compile_shader(GL_VERTEX_SHADER, shadow_vertex_shader),
                   compile_shader(GL_FRAGMENT_SHADER, shadow_fragment_shader));
  hud_program_ =
      link_program(compile_shader(GL_VERTEX_SHADER, hud_vertex_shader),
                   compile_shader(GL_FRAGMENT_SHADER, hud_fragment_shader));
  crosshair_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, crosshair_vertex_shader),
      compile_shader(GL_FRAGMENT_SHADER, crosshair_fragment_shader));
  sky_program_ =
      link_program(compile_shader(GL_VERTEX_SHADER, kSkyVertexShaderSource),
                   compile_shader(GL_FRAGMENT_SHADER, sky_fragment_shader));
  glow_extract_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, screen_vertex_shader),
      compile_shader(GL_FRAGMENT_SHADER, glow_extract_fragment_shader));
  glow_blur_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, screen_vertex_shader),
      compile_shader(GL_FRAGMENT_SHADER, glow_blur_fragment_shader));
  post_process_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, screen_vertex_shader),
      compile_shader(GL_FRAGMENT_SHADER, post_process_fragment_shader));
  menu_background_program_ = link_program(
      compile_shader(GL_VERTEX_SHADER, screen_vertex_shader),
      compile_shader(GL_FRAGMENT_SHADER, menu_background_fragment_shader));

  modern_terrain_uniforms_.model =
      glGetUniformLocation(modern_terrain_program_, "u_model");
  modern_terrain_uniforms_.view_projection =
      glGetUniformLocation(modern_terrain_program_, "u_view_projection");
  modern_terrain_uniforms_.light_view_projection =
      glGetUniformLocation(modern_terrain_program_, "u_light_view_projection");
  modern_terrain_uniforms_.light_view_projection_far = glGetUniformLocation(
      modern_terrain_program_, "u_light_view_projection_far");
  modern_terrain_uniforms_.camera_position =
      glGetUniformLocation(modern_terrain_program_, "u_camera_position");
  modern_terrain_uniforms_.camera_forward =
      glGetUniformLocation(modern_terrain_program_, "u_camera_forward");
  modern_terrain_uniforms_.sun_direction =
      glGetUniformLocation(modern_terrain_program_, "u_sun_direction");
  modern_terrain_uniforms_.sun_color =
      glGetUniformLocation(modern_terrain_program_, "u_sun_color");
  modern_terrain_uniforms_.ambient_color =
      glGetUniformLocation(modern_terrain_program_, "u_ambient_color");
  modern_terrain_uniforms_.block_light_color =
      glGetUniformLocation(modern_terrain_program_, "u_block_light_color");
  modern_terrain_uniforms_.enclosed_interior =
      glGetUniformLocation(modern_terrain_program_, "u_enclosed_interior");
  modern_terrain_uniforms_.interior_visibility_floor =
      glGetUniformLocation(
          modern_terrain_program_,
          "u_interior_visibility_floor");
  modern_terrain_uniforms_.backrooms_flicker_count =
      glGetUniformLocation(
          modern_terrain_program_,
          "u_backrooms_flicker_count");
  modern_terrain_uniforms_.backrooms_flicker_lights =
      glGetUniformLocation(
          modern_terrain_program_,
          "u_backrooms_flicker_lights[0]");
  modern_terrain_uniforms_.backrooms_flashlight_intensity =
      glGetUniformLocation(
          modern_terrain_program_,
          "u_backrooms_flashlight_intensity");
  modern_terrain_uniforms_.fog_color =
      glGetUniformLocation(modern_terrain_program_, "u_fog_color");
  modern_terrain_uniforms_.distant_fog_color =
      glGetUniformLocation(modern_terrain_program_, "u_distant_fog_color");
  modern_terrain_uniforms_.interior_fog_range =
      glGetUniformLocation(modern_terrain_program_, "u_interior_fog_range");
  modern_terrain_uniforms_.night_tint_color =
      glGetUniformLocation(modern_terrain_program_, "u_night_tint_color");
  modern_terrain_uniforms_.daylight_factor =
      glGetUniformLocation(modern_terrain_program_, "u_daylight_factor");
  modern_terrain_uniforms_.sun_visibility =
      glGetUniformLocation(modern_terrain_program_, "u_sun_visibility");
  modern_terrain_uniforms_.cloud_intensity =
      glGetUniformLocation(modern_terrain_program_, "u_cloud_intensity");
  modern_terrain_uniforms_.overcast_intensity =
      glGetUniformLocation(modern_terrain_program_, "u_overcast_intensity");
  modern_terrain_uniforms_.precipitation_intensity = glGetUniformLocation(
      modern_terrain_program_, "u_precipitation_intensity");
  modern_terrain_uniforms_.storm_intensity =
      glGetUniformLocation(modern_terrain_program_, "u_storm_intensity");
  modern_terrain_uniforms_.lightning_intensity =
      glGetUniformLocation(modern_terrain_program_, "u_lightning_intensity");
  modern_terrain_uniforms_.triplanar_sharpness =
      glGetUniformLocation(modern_terrain_program_, "u_triplanar_sharpness");
  modern_terrain_uniforms_.material_detail_scale =
      glGetUniformLocation(modern_terrain_program_, "u_material_detail_scale");
  modern_terrain_uniforms_.shadows_enabled =
      glGetUniformLocation(modern_terrain_program_, "u_shadows_enabled");
  modern_terrain_uniforms_.material_albedo =
      glGetUniformLocation(modern_terrain_program_, "u_material_albedo");
  modern_terrain_uniforms_.material_normal_height =
      glGetUniformLocation(modern_terrain_program_, "u_material_normal_height");
  modern_terrain_uniforms_.material_orm_emission =
      glGetUniformLocation(modern_terrain_program_, "u_material_orm_emission");
  modern_terrain_uniforms_.shadow_map =
      glGetUniformLocation(modern_terrain_program_, "u_shadow_map");
  modern_terrain_uniforms_.shadow_map_far =
      glGetUniformLocation(modern_terrain_program_, "u_shadow_map_far");
  modern_terrain_uniforms_.shadow_cascade_count =
      glGetUniformLocation(modern_terrain_program_, "u_shadow_cascade_count");
  modern_terrain_uniforms_.shadow_split_distance =
      glGetUniformLocation(modern_terrain_program_, "u_shadow_split_distance");
  modern_terrain_uniforms_.shadow_transition_width = glGetUniformLocation(
      modern_terrain_program_, "u_shadow_transition_width");
  modern_terrain_uniforms_.maritime_horizon_enabled = glGetUniformLocation(
      modern_terrain_program_, "u_maritime_horizon_enabled");
  modern_terrain_uniforms_.maritime_detail_transition_range =
      glGetUniformLocation(modern_terrain_program_,
                           "u_maritime_detail_transition_range");
  modern_terrain_uniforms_.maritime_sea_level =
      glGetUniformLocation(modern_terrain_program_, "u_maritime_sea_level");
  modern_terrain_uniforms_.maritime_submersion_active = glGetUniformLocation(
      modern_terrain_program_, "u_maritime_submersion_active");
  modern_terrain_uniforms_.time_seconds =
      glGetUniformLocation(modern_terrain_program_, "u_time_seconds");

  const auto load_modern_surface_uniforms =
      [](GLuint program, ModernTerrainUniformLocations &uniforms) {
        uniforms.model = glGetUniformLocation(program, "u_model");
        uniforms.view_projection =
            glGetUniformLocation(program, "u_view_projection");
        uniforms.light_view_projection =
            glGetUniformLocation(program, "u_light_view_projection");
        uniforms.light_view_projection_far =
            glGetUniformLocation(program, "u_light_view_projection_far");
        uniforms.camera_position =
            glGetUniformLocation(program, "u_camera_position");
        uniforms.camera_forward =
            glGetUniformLocation(program, "u_camera_forward");
        uniforms.sun_direction =
            glGetUniformLocation(program, "u_sun_direction");
        uniforms.sun_color = glGetUniformLocation(program, "u_sun_color");
        uniforms.ambient_color =
            glGetUniformLocation(program, "u_ambient_color");
        uniforms.block_light_color =
            glGetUniformLocation(program, "u_block_light_color");
        uniforms.enclosed_interior =
            glGetUniformLocation(program, "u_enclosed_interior");
        uniforms.interior_visibility_floor =
            glGetUniformLocation(
                program,
                "u_interior_visibility_floor");
        uniforms.backrooms_flicker_count =
            glGetUniformLocation(
                program,
                "u_backrooms_flicker_count");
        uniforms.backrooms_flicker_lights =
            glGetUniformLocation(
                program,
                "u_backrooms_flicker_lights[0]");
        uniforms.backrooms_flashlight_intensity =
            glGetUniformLocation(
                program,
                "u_backrooms_flashlight_intensity");
        uniforms.fog_color = glGetUniformLocation(program, "u_fog_color");
        uniforms.distant_fog_color =
            glGetUniformLocation(program, "u_distant_fog_color");
        uniforms.interior_fog_range =
            glGetUniformLocation(program, "u_interior_fog_range");
        uniforms.night_tint_color =
            glGetUniformLocation(program, "u_night_tint_color");
        uniforms.daylight_factor =
            glGetUniformLocation(program, "u_daylight_factor");
        uniforms.sun_visibility =
            glGetUniformLocation(program, "u_sun_visibility");
        uniforms.cloud_intensity =
            glGetUniformLocation(program, "u_cloud_intensity");
        uniforms.overcast_intensity =
            glGetUniformLocation(program, "u_overcast_intensity");
        uniforms.precipitation_intensity =
            glGetUniformLocation(program, "u_precipitation_intensity");
        uniforms.storm_intensity =
            glGetUniformLocation(program, "u_storm_intensity");
        uniforms.lightning_intensity =
            glGetUniformLocation(program, "u_lightning_intensity");
        uniforms.triplanar_sharpness =
            glGetUniformLocation(program, "u_triplanar_sharpness");
        uniforms.material_detail_scale =
            glGetUniformLocation(program, "u_material_detail_scale");
        uniforms.shadows_enabled =
            glGetUniformLocation(program, "u_shadows_enabled");
        uniforms.material_albedo =
            glGetUniformLocation(program, "u_material_albedo");
        uniforms.material_normal_height =
            glGetUniformLocation(program, "u_material_normal_height");
        uniforms.material_orm_emission =
            glGetUniformLocation(program, "u_material_orm_emission");
        uniforms.shadow_map = glGetUniformLocation(program, "u_shadow_map");
        uniforms.shadow_map_far =
            glGetUniformLocation(program, "u_shadow_map_far");
        uniforms.shadow_cascade_count =
            glGetUniformLocation(program, "u_shadow_cascade_count");
        uniforms.shadow_split_distance =
            glGetUniformLocation(program, "u_shadow_split_distance");
        uniforms.shadow_transition_width =
            glGetUniformLocation(program, "u_shadow_transition_width");
        uniforms.maritime_horizon_enabled =
            glGetUniformLocation(program, "u_maritime_horizon_enabled");
        uniforms.maritime_detail_transition_range =
            glGetUniformLocation(program, "u_maritime_detail_transition_range");
        uniforms.maritime_sea_level =
            glGetUniformLocation(program, "u_maritime_sea_level");
        uniforms.maritime_submersion_active =
            glGetUniformLocation(program, "u_maritime_submersion_active");
        uniforms.time_seconds = glGetUniformLocation(program, "u_time_seconds");
      };
  load_modern_surface_uniforms(modern_architecture_program_,
                               modern_architecture_uniforms_);
  modern_terrain_shadow_uniforms_.model =
      glGetUniformLocation(modern_terrain_shadow_program_, "u_model");
  modern_terrain_shadow_uniforms_.light_view_projection = glGetUniformLocation(
      modern_terrain_shadow_program_, "u_light_view_projection");
  modern_terrain_shadow_uniforms_.material_albedo =
      glGetUniformLocation(modern_terrain_shadow_program_, "u_material_albedo");

  const auto load_ship_uniforms = [](GLuint program,
                                     ModernShipUniformLocations &uniforms) {
    uniforms.model = glGetUniformLocation(program, "u_model");
    uniforms.view_projection =
        glGetUniformLocation(program, "u_view_projection");
    uniforms.light_view_projection =
        glGetUniformLocation(program, "u_light_view_projection");
    uniforms.light_view_projection_far =
        glGetUniformLocation(program, "u_light_view_projection_far");
    uniforms.camera_position =
        glGetUniformLocation(program, "u_camera_position");
    uniforms.camera_local_position =
        glGetUniformLocation(program, "u_camera_local_position");
    uniforms.camera_forward = glGetUniformLocation(program, "u_camera_forward");
    uniforms.sun_direction = glGetUniformLocation(program, "u_sun_direction");
    uniforms.sun_color = glGetUniformLocation(program, "u_sun_color");
    uniforms.ambient_color = glGetUniformLocation(program, "u_ambient_color");
    uniforms.fog_color = glGetUniformLocation(program, "u_fog_color");
    uniforms.distant_fog_color =
        glGetUniformLocation(program, "u_distant_fog_color");
    uniforms.night_tint_color =
        glGetUniformLocation(program, "u_night_tint_color");
    uniforms.daylight_factor =
        glGetUniformLocation(program, "u_daylight_factor");
    uniforms.sun_visibility = glGetUniformLocation(program, "u_sun_visibility");
    uniforms.precipitation_intensity =
        glGetUniformLocation(program, "u_precipitation_intensity");
    uniforms.storm_intensity =
        glGetUniformLocation(program, "u_storm_intensity");
    uniforms.exterior_light_activation =
        glGetUniformLocation(program, "u_exterior_light_activation");
    uniforms.exterior_light_radiance =
        glGetUniformLocation(program, "u_exterior_light_radiance");
    uniforms.lightning_intensity =
        glGetUniformLocation(program, "u_lightning_intensity");
    uniforms.material_detail_scale =
        glGetUniformLocation(program, "u_material_detail_scale");
    uniforms.shadows_enabled =
        glGetUniformLocation(program, "u_shadows_enabled");
    uniforms.material_albedo =
        glGetUniformLocation(program, "u_material_albedo");
    uniforms.material_normal_height =
        glGetUniformLocation(program, "u_material_normal_height");
    uniforms.material_orm_emission =
        glGetUniformLocation(program, "u_material_orm_emission");
    uniforms.shadow_map = glGetUniformLocation(program, "u_shadow_map");
    uniforms.shadow_map_far = glGetUniformLocation(program, "u_shadow_map_far");
    uniforms.shadow_cascade_count =
        glGetUniformLocation(program, "u_shadow_cascade_count");
    uniforms.shadow_split_distance =
        glGetUniformLocation(program, "u_shadow_split_distance");
    uniforms.shadow_transition_width =
        glGetUniformLocation(program, "u_shadow_transition_width");
    uniforms.time_seconds = glGetUniformLocation(program, "u_time_seconds");
    uniforms.wind_strength = glGetUniformLocation(program, "u_wind_strength");
    uniforms.material_layers =
        glGetUniformLocation(program, "u_material_layers[0]");
    uniforms.light_count = glGetUniformLocation(program, "u_light_count");
    uniforms.light_position_radius =
        glGetUniformLocation(program, "u_light_position_radius[0]");
    uniforms.light_color_intensity =
        glGetUniformLocation(program, "u_light_color_intensity[0]");
    uniforms.light_zone_min_spill =
        glGetUniformLocation(program, "u_light_zone_min_spill[0]");
    uniforms.light_zone_max_seed =
        glGetUniformLocation(program, "u_light_zone_max_seed[0]");
    uniforms.light_doorways =
        glGetUniformLocation(program, "u_light_doorways[0]");
  };
  load_ship_uniforms(modern_ship_program_, modern_ship_uniforms_);
  modern_ship_shadow_uniforms_.model =
      glGetUniformLocation(modern_ship_shadow_program_, "u_model");
  modern_ship_shadow_uniforms_.light_view_projection = glGetUniformLocation(
      modern_ship_shadow_program_, "u_light_view_projection");
  modern_ship_shadow_uniforms_.material_albedo =
      glGetUniformLocation(modern_ship_shadow_program_, "u_material_albedo");
  modern_ship_shadow_uniforms_.material_layers =
      glGetUniformLocation(modern_ship_shadow_program_, "u_material_layers[0]");
  modern_ship_shadow_uniforms_.time_seconds =
      glGetUniformLocation(modern_ship_shadow_program_, "u_time_seconds");
  modern_ship_shadow_uniforms_.wind_strength =
      glGetUniformLocation(modern_ship_shadow_program_, "u_wind_strength");

  const std::array<GLint, 45> modern_terrain_uniform_locations{{
      modern_terrain_uniforms_.model,
      modern_terrain_uniforms_.view_projection,
      modern_terrain_uniforms_.light_view_projection,
      modern_terrain_uniforms_.light_view_projection_far,
      modern_terrain_uniforms_.camera_position,
      modern_terrain_uniforms_.camera_forward,
      modern_terrain_uniforms_.sun_direction,
      modern_terrain_uniforms_.sun_color,
      modern_terrain_uniforms_.ambient_color,
      modern_terrain_uniforms_.block_light_color,
      modern_terrain_uniforms_.enclosed_interior,
      modern_terrain_uniforms_.interior_visibility_floor,
      modern_terrain_uniforms_.backrooms_flicker_count,
      modern_terrain_uniforms_.backrooms_flicker_lights,
      modern_terrain_uniforms_.backrooms_flashlight_intensity,
      modern_terrain_uniforms_.fog_color,
      modern_terrain_uniforms_.distant_fog_color,
      modern_terrain_uniforms_.interior_fog_range,
      modern_terrain_uniforms_.night_tint_color,
      modern_terrain_uniforms_.daylight_factor,
      modern_terrain_uniforms_.sun_visibility,
      modern_terrain_uniforms_.cloud_intensity,
      modern_terrain_uniforms_.overcast_intensity,
      modern_terrain_uniforms_.precipitation_intensity,
      modern_terrain_uniforms_.storm_intensity,
      modern_terrain_uniforms_.lightning_intensity,
      modern_terrain_uniforms_.triplanar_sharpness,
      modern_terrain_uniforms_.material_detail_scale,
      modern_terrain_uniforms_.shadows_enabled,
      modern_terrain_uniforms_.material_albedo,
      modern_terrain_uniforms_.material_normal_height,
      modern_terrain_uniforms_.material_orm_emission,
      modern_terrain_uniforms_.shadow_map,
      modern_terrain_uniforms_.shadow_map_far,
      modern_terrain_uniforms_.shadow_cascade_count,
      modern_terrain_uniforms_.shadow_split_distance,
      modern_terrain_uniforms_.shadow_transition_width,
      modern_terrain_uniforms_.maritime_horizon_enabled,
      modern_terrain_uniforms_.maritime_detail_transition_range,
      modern_terrain_uniforms_.maritime_sea_level,
      modern_terrain_uniforms_.maritime_submersion_active,
      modern_terrain_uniforms_.time_seconds,
      modern_terrain_shadow_uniforms_.model,
      modern_terrain_shadow_uniforms_.light_view_projection,
      modern_terrain_shadow_uniforms_.material_albedo,
  }};
  if (std::any_of(modern_terrain_uniform_locations.begin(),
                  modern_terrain_uniform_locations.end(),
                  [](GLint location) noexcept { return location < 0; })) {
    throw std::runtime_error(
        "Modern terrain shader is missing one or more required uniforms");
  }

  const std::array<GLint, 42> modern_architecture_uniform_locations{{
      modern_architecture_uniforms_.model,
      modern_architecture_uniforms_.view_projection,
      modern_architecture_uniforms_.light_view_projection,
      modern_architecture_uniforms_.light_view_projection_far,
      modern_architecture_uniforms_.camera_position,
      modern_architecture_uniforms_.camera_forward,
      modern_architecture_uniforms_.sun_direction,
      modern_architecture_uniforms_.sun_color,
      modern_architecture_uniforms_.ambient_color,
      modern_architecture_uniforms_.block_light_color,
      modern_architecture_uniforms_.enclosed_interior,
      modern_architecture_uniforms_.interior_visibility_floor,
      modern_architecture_uniforms_.backrooms_flicker_count,
      modern_architecture_uniforms_.backrooms_flicker_lights,
      modern_architecture_uniforms_.backrooms_flashlight_intensity,
      modern_architecture_uniforms_.fog_color,
      modern_architecture_uniforms_.distant_fog_color,
      modern_architecture_uniforms_.interior_fog_range,
      modern_architecture_uniforms_.night_tint_color,
      modern_architecture_uniforms_.daylight_factor,
      modern_architecture_uniforms_.sun_visibility,
      modern_architecture_uniforms_.cloud_intensity,
      modern_architecture_uniforms_.overcast_intensity,
      modern_architecture_uniforms_.precipitation_intensity,
      modern_architecture_uniforms_.storm_intensity,
      modern_architecture_uniforms_.lightning_intensity,
      modern_architecture_uniforms_.triplanar_sharpness,
      modern_architecture_uniforms_.material_detail_scale,
      modern_architecture_uniforms_.shadows_enabled,
      modern_architecture_uniforms_.material_albedo,
      modern_architecture_uniforms_.material_normal_height,
      modern_architecture_uniforms_.material_orm_emission,
      modern_architecture_uniforms_.shadow_map,
      modern_architecture_uniforms_.shadow_map_far,
      modern_architecture_uniforms_.shadow_cascade_count,
      modern_architecture_uniforms_.shadow_split_distance,
      modern_architecture_uniforms_.shadow_transition_width,
      modern_architecture_uniforms_.maritime_horizon_enabled,
      modern_architecture_uniforms_.maritime_detail_transition_range,
      modern_architecture_uniforms_.maritime_sea_level,
      modern_architecture_uniforms_.maritime_submersion_active,
      modern_architecture_uniforms_.time_seconds,
  }};
  if (std::any_of(modern_architecture_uniform_locations.begin(),
                  modern_architecture_uniform_locations.end(),
                  [](GLint location) noexcept { return location < 0; })) {
    throw std::runtime_error(
        "Modern architecture shader is missing one or more required uniforms");
  }

  const std::array<GLint, 44> modern_ship_uniform_locations{{
      modern_ship_uniforms_.model,
      modern_ship_uniforms_.view_projection,
      modern_ship_uniforms_.light_view_projection,
      modern_ship_uniforms_.light_view_projection_far,
      modern_ship_uniforms_.camera_position,
      modern_ship_uniforms_.camera_local_position,
      modern_ship_uniforms_.camera_forward,
      modern_ship_uniforms_.sun_direction,
      modern_ship_uniforms_.sun_color,
      modern_ship_uniforms_.ambient_color,
      modern_ship_uniforms_.fog_color,
      modern_ship_uniforms_.distant_fog_color,
      modern_ship_uniforms_.night_tint_color,
      modern_ship_uniforms_.daylight_factor,
      modern_ship_uniforms_.sun_visibility,
      modern_ship_uniforms_.precipitation_intensity,
      modern_ship_uniforms_.storm_intensity,
      modern_ship_uniforms_.exterior_light_activation,
      modern_ship_uniforms_.exterior_light_radiance,
      modern_ship_uniforms_.lightning_intensity,
      modern_ship_uniforms_.material_detail_scale,
      modern_ship_uniforms_.shadows_enabled,
      modern_ship_uniforms_.material_albedo,
      modern_ship_uniforms_.material_normal_height,
      modern_ship_uniforms_.material_orm_emission,
      modern_ship_uniforms_.shadow_map,
      modern_ship_uniforms_.shadow_map_far,
      modern_ship_uniforms_.shadow_cascade_count,
      modern_ship_uniforms_.shadow_split_distance,
      modern_ship_uniforms_.shadow_transition_width,
      modern_ship_uniforms_.time_seconds,
      modern_ship_uniforms_.wind_strength,
      modern_ship_uniforms_.material_layers,
      modern_ship_uniforms_.light_count,
      modern_ship_uniforms_.light_position_radius,
      modern_ship_uniforms_.light_color_intensity,
      modern_ship_uniforms_.light_zone_min_spill,
      modern_ship_uniforms_.light_zone_max_seed,
      modern_ship_uniforms_.light_doorways,
      modern_ship_shadow_uniforms_.model,
      modern_ship_shadow_uniforms_.light_view_projection,
      modern_ship_shadow_uniforms_.material_albedo,
      modern_ship_shadow_uniforms_.material_layers,
      modern_ship_shadow_uniforms_.time_seconds,
  }};
  if (std::any_of(modern_ship_uniform_locations.begin(),
                  modern_ship_uniform_locations.end(),
                  [](GLint location) noexcept { return location < 0; }) ||
      modern_ship_shadow_uniforms_.wind_strength < 0) {
    throw std::runtime_error(
        "Modern ship shader is missing one or more required uniforms");
  }

  world_uniforms_.model = glGetUniformLocation(world_program_, "u_model");
  world_uniforms_.view_projection =
      glGetUniformLocation(world_program_, "u_view_projection");
  world_uniforms_.light_view_projection =
      glGetUniformLocation(world_program_, "u_light_view_projection");
  world_uniforms_.light_view_projection_far =
      glGetUniformLocation(world_program_, "u_light_view_projection_far");
  world_uniforms_.camera_position =
      glGetUniformLocation(world_program_, "u_camera_position");
  world_uniforms_.camera_forward =
      glGetUniformLocation(world_program_, "u_camera_forward");
  world_uniforms_.sun_direction =
      glGetUniformLocation(world_program_, "u_sun_direction");
  world_uniforms_.sun_color =
      glGetUniformLocation(world_program_, "u_sun_color");
  world_uniforms_.ambient_color =
      glGetUniformLocation(world_program_, "u_ambient_color");
  world_uniforms_.block_light_color =
      glGetUniformLocation(world_program_, "u_block_light_color");
  world_uniforms_.enclosed_interior =
      glGetUniformLocation(world_program_, "u_enclosed_interior");
  world_uniforms_.interior_visibility_floor =
      glGetUniformLocation(
          world_program_,
          "u_interior_visibility_floor");
  world_uniforms_.backrooms_flicker_count =
      glGetUniformLocation(world_program_, "u_backrooms_flicker_count");
  world_uniforms_.backrooms_flicker_lights =
      glGetUniformLocation(world_program_, "u_backrooms_flicker_lights[0]");
  world_uniforms_.backrooms_flashlight_intensity =
      glGetUniformLocation(
          world_program_,
          "u_backrooms_flashlight_intensity");
  world_uniforms_.fog_color =
      glGetUniformLocation(world_program_, "u_fog_color");
  world_uniforms_.distant_fog_color =
      glGetUniformLocation(world_program_, "u_distant_fog_color");
  world_uniforms_.interior_fog_range =
      glGetUniformLocation(world_program_, "u_interior_fog_range");
  world_uniforms_.horizon_glow_color =
      glGetUniformLocation(world_program_, "u_horizon_glow_color");
  world_uniforms_.night_tint_color =
      glGetUniformLocation(world_program_, "u_night_tint_color");
  world_uniforms_.daylight_factor =
      glGetUniformLocation(world_program_, "u_daylight_factor");
  world_uniforms_.sun_visibility =
      glGetUniformLocation(world_program_, "u_sun_visibility");
  world_uniforms_.time_of_day =
      glGetUniformLocation(world_program_, "u_time_of_day");
  world_uniforms_.cloud_intensity =
      glGetUniformLocation(world_program_, "u_cloud_intensity");
  world_uniforms_.cloud_shadow_strength =
      glGetUniformLocation(world_program_, "u_cloud_shadow_strength");
  world_uniforms_.wind_strength =
      glGetUniformLocation(world_program_, "u_wind_strength");
  world_uniforms_.atmospheric_scatter_strength =
      glGetUniformLocation(world_program_, "u_atmospheric_scatter_strength");
  world_uniforms_.height_fog_density =
      glGetUniformLocation(world_program_, "u_height_fog_density");
  world_uniforms_.precipitation_intensity =
      glGetUniformLocation(world_program_, "u_precipitation_intensity");
  world_uniforms_.storm_intensity =
      glGetUniformLocation(world_program_, "u_storm_intensity");
  world_uniforms_.lightning_intensity =
      glGetUniformLocation(world_program_, "u_lightning_intensity");
  world_uniforms_.ocean_waves =
      glGetUniformLocation(world_program_, "u_ocean_waves[0]");

  world_uniforms_.ocean_wave_phases =
      glGetUniformLocation(world_program_, "u_ocean_wave_phases[0]");

  world_uniforms_.ocean_wave_count =
      glGetUniformLocation(world_program_, "u_ocean_wave_count");

  world_uniforms_.ocean_foam_threshold =
      glGetUniformLocation(world_program_, "u_ocean_foam_threshold");

  world_uniforms_.ocean_detail_strength =
      glGetUniformLocation(world_program_, "u_ocean_detail_strength");

  world_uniforms_.ocean_detail_phase =
      glGetUniformLocation(world_program_, "u_ocean_detail_phase");

  world_uniforms_.ocean_severity =
      glGetUniformLocation(world_program_, "u_ocean_severity");

  world_uniforms_.ocean_tempest_factor =
      glGetUniformLocation(world_program_, "u_ocean_tempest_factor");

  world_uniforms_.ocean_open_sea =
      glGetUniformLocation(world_program_, "u_ocean_open_sea");

  world_uniforms_.maritime_horizon_enabled =
      glGetUniformLocation(world_program_, "u_maritime_horizon_enabled");

  world_uniforms_.maritime_water_blend_range =
      glGetUniformLocation(world_program_, "u_maritime_water_blend_range");

  world_uniforms_.maritime_far_fog_range =
      glGetUniformLocation(world_program_, "u_maritime_far_fog_range");

  world_uniforms_.maritime_sea_level =
      glGetUniformLocation(world_program_, "u_maritime_sea_level");

  const std::array<GLint, 8> world_interior_lighting_uniform_locations{{
      world_uniforms_.ambient_color,
      world_uniforms_.block_light_color,
      world_uniforms_.enclosed_interior,
      world_uniforms_.interior_visibility_floor,
      world_uniforms_.backrooms_flicker_count,
      world_uniforms_.backrooms_flicker_lights,
      world_uniforms_.backrooms_flashlight_intensity,
      world_uniforms_.interior_fog_range,
  }};
  // Je refuse de lancer le rendu avec une chaîne d'éclairage intérieur
  // partielle : une optimisation GLSL indésirable recréerait des noirs francs.
  if (std::any_of(
          world_interior_lighting_uniform_locations.begin(),
          world_interior_lighting_uniform_locations.end(),
          [](GLint location) noexcept { return location < 0; })) {
    throw std::runtime_error(
        "World shader is missing one or more interior lighting uniforms");
  }

  const std::array<GLint, 13> ocean_uniform_locations{{
      world_uniforms_.ocean_waves,
      world_uniforms_.ocean_wave_phases,
      world_uniforms_.ocean_wave_count,
      world_uniforms_.ocean_foam_threshold,
      world_uniforms_.ocean_detail_strength,
      world_uniforms_.ocean_detail_phase,
      world_uniforms_.ocean_severity,
      world_uniforms_.ocean_tempest_factor,
      world_uniforms_.ocean_open_sea,
      world_uniforms_.maritime_horizon_enabled,
      world_uniforms_.maritime_water_blend_range,
      world_uniforms_.maritime_far_fog_range,
      world_uniforms_.maritime_sea_level,
  }};
  // Je refuse une initialisation partielle : un uniform optimise ou mal
  // orthographie rendrait l'ocean visuellement incoherent sans erreur OpenGL.
  if (std::any_of(ocean_uniform_locations.begin(),
                  ocean_uniform_locations.end(),
                  [](GLint location) noexcept { return location < 0; })) {
    throw std::runtime_error(
        "Ocean shader is missing one or more required uniforms");
  }

  sea_horizon_uniforms_.view_projection =
      glGetUniformLocation(sea_horizon_program_, "u_view_projection");
  sea_horizon_uniforms_.camera_position =
      glGetUniformLocation(sea_horizon_program_, "u_camera_position");
  sea_horizon_uniforms_.sun_direction =
      glGetUniformLocation(sea_horizon_program_, "u_sun_direction");
  sea_horizon_uniforms_.sun_color =
      glGetUniformLocation(sea_horizon_program_, "u_sun_color");
  sea_horizon_uniforms_.ambient_color =
      glGetUniformLocation(sea_horizon_program_, "u_ambient_color");
  sea_horizon_uniforms_.fog_color =
      glGetUniformLocation(sea_horizon_program_, "u_fog_color");
  sea_horizon_uniforms_.distant_fog_color =
      glGetUniformLocation(sea_horizon_program_, "u_distant_fog_color");
  sea_horizon_uniforms_.daylight_factor =
      glGetUniformLocation(sea_horizon_program_, "u_daylight_factor");
  sea_horizon_uniforms_.precipitation_intensity =
      glGetUniformLocation(sea_horizon_program_, "u_precipitation_intensity");
  sea_horizon_uniforms_.storm_intensity =
      glGetUniformLocation(sea_horizon_program_, "u_storm_intensity");
  sea_horizon_uniforms_.lightning_intensity =
      glGetUniformLocation(sea_horizon_program_, "u_lightning_intensity");
  sea_horizon_uniforms_.far_fog_range =
      glGetUniformLocation(sea_horizon_program_, "u_far_fog_range");
  sea_horizon_uniforms_.sea_level =
      glGetUniformLocation(sea_horizon_program_, "u_sea_level");
  sea_horizon_uniforms_.detail_transition_range =
      glGetUniformLocation(sea_horizon_program_, "u_detail_transition_range");
  sea_horizon_uniforms_.transition_pass =
      glGetUniformLocation(sea_horizon_program_, "u_transition_pass");
  const std::array<GLint, 15> sea_horizon_uniform_locations{{
      sea_horizon_uniforms_.view_projection,
      sea_horizon_uniforms_.camera_position,
      sea_horizon_uniforms_.sun_direction,
      sea_horizon_uniforms_.sun_color,
      sea_horizon_uniforms_.ambient_color,
      sea_horizon_uniforms_.fog_color,
      sea_horizon_uniforms_.distant_fog_color,
      sea_horizon_uniforms_.daylight_factor,
      sea_horizon_uniforms_.precipitation_intensity,
      sea_horizon_uniforms_.storm_intensity,
      sea_horizon_uniforms_.lightning_intensity,
      sea_horizon_uniforms_.far_fog_range,
      sea_horizon_uniforms_.sea_level,
      sea_horizon_uniforms_.detail_transition_range,
      sea_horizon_uniforms_.transition_pass,
  }};
  if (std::any_of(sea_horizon_uniform_locations.begin(),
                  sea_horizon_uniform_locations.end(),
                  [](GLint location) noexcept { return location < 0; })) {
    throw std::runtime_error(
        "Sea horizon shader is missing one or more required uniforms");
  }

  world_uniforms_.atlas = glGetUniformLocation(world_program_, "u_atlas");
  world_uniforms_.shadow_map =
      glGetUniformLocation(world_program_, "u_shadow_map");
  world_uniforms_.shadow_map_far =
      glGetUniformLocation(world_program_, "u_shadow_map_far");
  world_uniforms_.shadow_cascade_count =
      glGetUniformLocation(world_program_, "u_shadow_cascade_count");
  world_uniforms_.shadow_split_distance =
      glGetUniformLocation(world_program_, "u_shadow_split_distance");
  world_uniforms_.shadow_transition_width =
      glGetUniformLocation(world_program_, "u_shadow_transition_width");
  world_uniforms_.scene_color =
      glGetUniformLocation(world_program_, "u_scene_color");
  world_uniforms_.scene_depth =
      glGetUniformLocation(world_program_, "u_scene_depth");
  world_uniforms_.inverse_view_projection =
      glGetUniformLocation(world_program_, "u_inverse_view_projection");
  world_uniforms_.shadows_enabled =
      glGetUniformLocation(world_program_, "u_shadows_enabled");
  world_uniforms_.super_vision_strength =
      glGetUniformLocation(world_program_, "u_super_vision_strength");
  world_uniforms_.ship_protection_enabled =
      glGetUniformLocation(world_program_, "u_ship_protection_enabled");
  world_uniforms_.ship_inverse_model =
      glGetUniformLocation(world_program_, "u_ship_inverse_model");
  world_uniforms_.ship_bounds_min =
      glGetUniformLocation(world_program_, "u_ship_bounds_min");
  world_uniforms_.ship_bounds_max =
      glGetUniformLocation(world_program_, "u_ship_bounds_max");
  world_uniforms_.ship_profile_longitudinal =
      glGetUniformLocation(world_program_, "u_ship_profile_longitudinal");
  world_uniforms_.ship_profile_taper =
      glGetUniformLocation(world_program_, "u_ship_profile_taper");
  world_uniforms_.ship_profile_heights =
      glGetUniformLocation(world_program_, "u_ship_profile_heights");
  world_uniforms_.ship_profile_widths =
      glGetUniformLocation(world_program_, "u_ship_profile_widths");
  world_uniforms_.ship_sheltered_floor =
      glGetUniformLocation(world_program_, "u_ship_sheltered_floor");

  const std::array<GLint, 9> ship_protection_uniform_locations{{
      world_uniforms_.ship_protection_enabled,
      world_uniforms_.ship_inverse_model,
      world_uniforms_.ship_bounds_min,
      world_uniforms_.ship_bounds_max,
      world_uniforms_.ship_profile_longitudinal,
      world_uniforms_.ship_profile_taper,
      world_uniforms_.ship_profile_heights,
      world_uniforms_.ship_profile_widths,
      world_uniforms_.ship_sheltered_floor,
  }};
  if (std::any_of(ship_protection_uniform_locations.begin(),
                  ship_protection_uniform_locations.end(),
                  [](GLint location) noexcept { return location < 0; })) {
    throw std::runtime_error(
        "World shader is missing one or more ship protection uniforms");
  }

  const auto modern_water_uniform = [this](GLint &destination,
                                           const char *name) {
    destination = glGetUniformLocation(modern_water_program_, name);
  };
  modern_water_uniform(modern_water_uniforms_.model, "u_model");
  modern_water_uniform(modern_water_uniforms_.view_projection,
                       "u_view_projection");
  modern_water_uniform(modern_water_uniforms_.inverse_view_projection,
                       "u_inverse_view_projection");
  modern_water_uniform(modern_water_uniforms_.camera_position,
                       "u_camera_position");
  modern_water_uniform(modern_water_uniforms_.camera_forward,
                       "u_camera_forward");
  modern_water_uniform(modern_water_uniforms_.sun_direction, "u_sun_direction");
  modern_water_uniform(modern_water_uniforms_.sun_color, "u_sun_color");
  modern_water_uniform(modern_water_uniforms_.moon_disk_color,
                       "u_moon_disk_color");
  modern_water_uniform(modern_water_uniforms_.ambient_color, "u_ambient_color");
  modern_water_uniform(modern_water_uniforms_.block_light_color,
                       "u_block_light_color");
  modern_water_uniform(modern_water_uniforms_.fog_color, "u_fog_color");
  modern_water_uniform(modern_water_uniforms_.distant_fog_color,
                       "u_distant_fog_color");
  modern_water_uniform(modern_water_uniforms_.horizon_glow_color,
                       "u_horizon_glow_color");
  modern_water_uniform(modern_water_uniforms_.night_tint_color,
                       "u_night_tint_color");
  modern_water_uniform(modern_water_uniforms_.sky_zenith_color,
                       "u_sky_zenith_color");
  modern_water_uniform(modern_water_uniforms_.sky_horizon_color,
                       "u_sky_horizon_color");
  modern_water_uniform(modern_water_uniforms_.daylight_factor,
                       "u_daylight_factor");
  modern_water_uniform(modern_water_uniforms_.sun_visibility,
                       "u_sun_visibility");
  modern_water_uniform(modern_water_uniforms_.cloud_intensity,
                       "u_cloud_intensity");
  modern_water_uniform(modern_water_uniforms_.overcast_intensity,
                       "u_overcast_intensity");
  modern_water_uniform(modern_water_uniforms_.precipitation_intensity,
                       "u_precipitation_intensity");
  modern_water_uniform(modern_water_uniforms_.storm_intensity,
                       "u_storm_intensity");
  modern_water_uniform(modern_water_uniforms_.lightning_intensity,
                       "u_lightning_intensity");
  modern_water_uniform(modern_water_uniforms_.ocean_waves, "u_ocean_waves[0]");
  modern_water_uniform(modern_water_uniforms_.ocean_wave_phases,
                       "u_ocean_wave_phases[0]");
  modern_water_uniform(modern_water_uniforms_.ocean_wave_count,
                       "u_ocean_wave_count");
  modern_water_uniform(modern_water_uniforms_.ocean_foam_threshold,
                       "u_ocean_foam_threshold");
  modern_water_uniform(modern_water_uniforms_.ocean_detail_strength,
                       "u_ocean_detail_strength");
  modern_water_uniform(modern_water_uniforms_.ocean_detail_phase,
                       "u_ocean_detail_phase");
  modern_water_uniform(modern_water_uniforms_.water_animation_time,
                       "u_water_animation_time");
  modern_water_uniform(modern_water_uniforms_.ocean_severity,
                       "u_ocean_severity");
  modern_water_uniform(modern_water_uniforms_.ocean_tempest_factor,
                       "u_ocean_tempest_factor");
  modern_water_uniform(modern_water_uniforms_.ocean_open_sea,
                       "u_ocean_open_sea");
  modern_water_uniform(modern_water_uniforms_.water_surface_detail,
                       "u_water_surface_detail");
  modern_water_uniform(modern_water_uniforms_.water_detail_samples,
                       "u_water_detail_samples");
  modern_water_uniform(modern_water_uniforms_.has_water_material,
                       "u_has_water_material");
  modern_water_uniform(modern_water_uniforms_.water_normal_layer,
                       "u_water_normal_layer");
  modern_water_uniform(modern_water_uniforms_.scene_color, "u_scene_color");
  modern_water_uniform(modern_water_uniforms_.scene_depth, "u_scene_depth");
  modern_water_uniform(modern_water_uniforms_.material_normal_height,
                       "u_material_normal_height");
  modern_water_uniform(modern_water_uniforms_.maritime_horizon_enabled,
                       "u_maritime_horizon_enabled");
  modern_water_uniform(modern_water_uniforms_.maritime_water_blend_range,
                       "u_maritime_water_blend_range");
  modern_water_uniform(modern_water_uniforms_.maritime_far_fog_range,
                       "u_maritime_far_fog_range");
  modern_water_uniform(modern_water_uniforms_.maritime_sea_level,
                       "u_maritime_sea_level");
  modern_water_uniform(modern_water_uniforms_.enclosed_interior,
                       "u_enclosed_interior");
  modern_water_uniform(
      modern_water_uniforms_.interior_visibility_floor,
      "u_interior_visibility_floor");
  modern_water_uniform(modern_water_uniforms_.poolrooms_interior,
                       "u_poolrooms_interior");
  modern_water_uniform(modern_water_uniforms_.backrooms_flicker_count,
                       "u_backrooms_flicker_count");
  modern_water_uniform(modern_water_uniforms_.backrooms_flicker_lights,
                       "u_backrooms_flicker_lights[0]");
  modern_water_uniform(
      modern_water_uniforms_.backrooms_flashlight_intensity,
      "u_backrooms_flashlight_intensity");
  modern_water_uniform(modern_water_uniforms_.interior_fog_range,
                       "u_interior_fog_range");
  modern_water_uniform(modern_water_uniforms_.ship_speed, "u_ship_speed");
  modern_water_uniform(modern_water_uniforms_.ship_protection_enabled,
                       "u_ship_protection_enabled");
  modern_water_uniform(modern_water_uniforms_.ship_inverse_model,
                       "u_ship_inverse_model");
  modern_water_uniform(modern_water_uniforms_.ship_bounds_min,
                       "u_ship_bounds_min");
  modern_water_uniform(modern_water_uniforms_.ship_bounds_max,
                       "u_ship_bounds_max");
  modern_water_uniform(modern_water_uniforms_.ship_profile_longitudinal,
                       "u_ship_profile_longitudinal");
  modern_water_uniform(modern_water_uniforms_.ship_profile_taper,
                       "u_ship_profile_taper");
  modern_water_uniform(modern_water_uniforms_.ship_profile_heights,
                       "u_ship_profile_heights");
  modern_water_uniform(modern_water_uniforms_.ship_profile_widths,
                       "u_ship_profile_widths");
  modern_water_uniform(modern_water_uniforms_.ship_sheltered_floor,
                       "u_ship_sheltered_floor");

  // Le fragment partage l'enveloppe avec la pluie, mais l'eau n'appelle pas
  // ship_shelters_weather(). Le compilateur retire donc volontairement
  // u_ship_sheltered_floor du programme d'eau actif.
  const std::array modern_water_required_uniforms{
      modern_water_uniforms_.model,
      modern_water_uniforms_.view_projection,
      modern_water_uniforms_.inverse_view_projection,
      modern_water_uniforms_.camera_position,
      modern_water_uniforms_.camera_forward,
      modern_water_uniforms_.sun_direction,
      modern_water_uniforms_.sun_color,
      modern_water_uniforms_.moon_disk_color,
      modern_water_uniforms_.ambient_color,
      modern_water_uniforms_.block_light_color,
      modern_water_uniforms_.fog_color,
      modern_water_uniforms_.distant_fog_color,
      modern_water_uniforms_.horizon_glow_color,
      modern_water_uniforms_.night_tint_color,
      modern_water_uniforms_.sky_zenith_color,
      modern_water_uniforms_.sky_horizon_color,
      modern_water_uniforms_.daylight_factor,
      modern_water_uniforms_.sun_visibility,
      modern_water_uniforms_.cloud_intensity,
      modern_water_uniforms_.overcast_intensity,
      modern_water_uniforms_.precipitation_intensity,
      modern_water_uniforms_.storm_intensity,
      modern_water_uniforms_.lightning_intensity,
      modern_water_uniforms_.ocean_waves,
      modern_water_uniforms_.ocean_wave_phases,
      modern_water_uniforms_.ocean_wave_count,
      modern_water_uniforms_.ocean_foam_threshold,
      modern_water_uniforms_.ocean_detail_strength,
      modern_water_uniforms_.ocean_detail_phase,
      modern_water_uniforms_.water_animation_time,
      modern_water_uniforms_.ocean_severity,
      modern_water_uniforms_.ocean_tempest_factor,
      modern_water_uniforms_.ocean_open_sea,
      modern_water_uniforms_.water_surface_detail,
      modern_water_uniforms_.water_detail_samples,
      modern_water_uniforms_.has_water_material,
      modern_water_uniforms_.water_normal_layer,
      modern_water_uniforms_.scene_color,
      modern_water_uniforms_.scene_depth,
      modern_water_uniforms_.material_normal_height,
      modern_water_uniforms_.maritime_horizon_enabled,
      modern_water_uniforms_.maritime_water_blend_range,
      modern_water_uniforms_.maritime_far_fog_range,
      modern_water_uniforms_.maritime_sea_level,
      modern_water_uniforms_.enclosed_interior,
      modern_water_uniforms_.interior_visibility_floor,
      modern_water_uniforms_.poolrooms_interior,
      modern_water_uniforms_.backrooms_flicker_count,
      modern_water_uniforms_.backrooms_flicker_lights,
      modern_water_uniforms_.backrooms_flashlight_intensity,
      modern_water_uniforms_.interior_fog_range,
      modern_water_uniforms_.ship_speed,
      modern_water_uniforms_.ship_protection_enabled,
      modern_water_uniforms_.ship_inverse_model,
      modern_water_uniforms_.ship_bounds_min,
      modern_water_uniforms_.ship_bounds_max,
      modern_water_uniforms_.ship_profile_longitudinal,
      modern_water_uniforms_.ship_profile_taper,
      modern_water_uniforms_.ship_profile_heights,
      modern_water_uniforms_.ship_profile_widths,
  };
  if (std::any_of(modern_water_required_uniforms.begin(),
                  modern_water_required_uniforms.end(),
                  [](GLint location) noexcept { return location < 0; })) {
    throw std::runtime_error(
        "Modern water shader is missing one or more required uniforms");
  }

  item_drop_uniforms_.view_projection =
      glGetUniformLocation(item_drop_program_, "u_view_projection");
  item_drop_uniforms_.light_view_projection =
      glGetUniformLocation(item_drop_program_, "u_light_view_projection");
  item_drop_uniforms_.light_view_projection_far =
      glGetUniformLocation(item_drop_program_, "u_light_view_projection_far");
  item_drop_uniforms_.camera_position =
      glGetUniformLocation(item_drop_program_, "u_camera_position");
  item_drop_uniforms_.camera_forward =
      glGetUniformLocation(item_drop_program_, "u_camera_forward");
  item_drop_uniforms_.sun_direction =
      glGetUniformLocation(item_drop_program_, "u_sun_direction");
  item_drop_uniforms_.sun_color =
      glGetUniformLocation(item_drop_program_, "u_sun_color");
  item_drop_uniforms_.ambient_color =
      glGetUniformLocation(item_drop_program_, "u_ambient_color");
  item_drop_uniforms_.block_light_color =
      glGetUniformLocation(item_drop_program_, "u_block_light_color");
  item_drop_uniforms_.enclosed_interior =
      glGetUniformLocation(item_drop_program_, "u_enclosed_interior");
  item_drop_uniforms_.interior_visibility_floor =
      glGetUniformLocation(
          item_drop_program_,
          "u_interior_visibility_floor");
  item_drop_uniforms_.backrooms_flicker_count =
      glGetUniformLocation(item_drop_program_, "u_backrooms_flicker_count");
  item_drop_uniforms_.backrooms_flicker_lights =
      glGetUniformLocation(
          item_drop_program_,
          "u_backrooms_flicker_lights[0]");
  item_drop_uniforms_.backrooms_flashlight_intensity =
      glGetUniformLocation(
          item_drop_program_,
          "u_backrooms_flashlight_intensity");
  item_drop_uniforms_.fog_color =
      glGetUniformLocation(item_drop_program_, "u_fog_color");
  item_drop_uniforms_.distant_fog_color =
      glGetUniformLocation(item_drop_program_, "u_distant_fog_color");
  item_drop_uniforms_.interior_fog_range =
      glGetUniformLocation(item_drop_program_, "u_interior_fog_range");
  item_drop_uniforms_.horizon_glow_color =
      glGetUniformLocation(item_drop_program_, "u_horizon_glow_color");
  item_drop_uniforms_.night_tint_color =
      glGetUniformLocation(item_drop_program_, "u_night_tint_color");
  item_drop_uniforms_.daylight_factor =
      glGetUniformLocation(item_drop_program_, "u_daylight_factor");
  item_drop_uniforms_.sun_visibility =
      glGetUniformLocation(item_drop_program_, "u_sun_visibility");
  item_drop_uniforms_.time_of_day =
      glGetUniformLocation(item_drop_program_, "u_time_of_day");
  item_drop_uniforms_.cloud_intensity =
      glGetUniformLocation(item_drop_program_, "u_cloud_intensity");
  item_drop_uniforms_.cloud_shadow_strength =
      glGetUniformLocation(item_drop_program_, "u_cloud_shadow_strength");
  item_drop_uniforms_.wind_strength =
      glGetUniformLocation(item_drop_program_, "u_wind_strength");
  item_drop_uniforms_.atmospheric_scatter_strength = glGetUniformLocation(
      item_drop_program_, "u_atmospheric_scatter_strength");
  item_drop_uniforms_.height_fog_density =
      glGetUniformLocation(item_drop_program_, "u_height_fog_density");
  item_drop_uniforms_.precipitation_intensity =
      glGetUniformLocation(item_drop_program_, "u_precipitation_intensity");
  item_drop_uniforms_.storm_intensity =
      glGetUniformLocation(item_drop_program_, "u_storm_intensity");
  item_drop_uniforms_.lightning_intensity =
      glGetUniformLocation(item_drop_program_, "u_lightning_intensity");
  item_drop_uniforms_.atlas =
      glGetUniformLocation(item_drop_program_, "u_atlas");
  item_drop_uniforms_.shadow_map =
      glGetUniformLocation(item_drop_program_, "u_shadow_map");
  item_drop_uniforms_.shadow_map_far =
      glGetUniformLocation(item_drop_program_, "u_shadow_map_far");
  item_drop_uniforms_.shadow_cascade_count =
      glGetUniformLocation(item_drop_program_, "u_shadow_cascade_count");
  item_drop_uniforms_.shadow_split_distance =
      glGetUniformLocation(item_drop_program_, "u_shadow_split_distance");
  item_drop_uniforms_.shadow_transition_width =
      glGetUniformLocation(item_drop_program_, "u_shadow_transition_width");
  item_drop_uniforms_.scene_color =
      glGetUniformLocation(item_drop_program_, "u_scene_color");
  item_drop_uniforms_.scene_depth =
      glGetUniformLocation(item_drop_program_, "u_scene_depth");
  item_drop_uniforms_.inverse_view_projection =
      glGetUniformLocation(item_drop_program_, "u_inverse_view_projection");
  item_drop_uniforms_.shadows_enabled =
      glGetUniformLocation(item_drop_program_, "u_shadows_enabled");
  const std::array<GLint, 7>
      item_drop_backrooms_uniform_locations {{
          item_drop_uniforms_.block_light_color,
          item_drop_uniforms_.enclosed_interior,
          item_drop_uniforms_.interior_visibility_floor,
          item_drop_uniforms_.backrooms_flicker_count,
          item_drop_uniforms_.backrooms_flicker_lights,
          item_drop_uniforms_.backrooms_flashlight_intensity,
          item_drop_uniforms_.interior_fog_range,
      }};
  if (std::any_of(
          item_drop_backrooms_uniform_locations.begin(),
          item_drop_backrooms_uniform_locations.end(),
          [](GLint location) noexcept {
            return location < 0;
          })) {
    throw std::runtime_error(
        "Item drop shader is missing Backrooms light uniforms");
  }

  precipitation_uniforms_.view_projection =
      glGetUniformLocation(precipitation_program_, "u_view_projection");
  precipitation_uniforms_.camera_position =
      glGetUniformLocation(precipitation_program_, "u_camera_position");
  precipitation_uniforms_.camera_right =
      glGetUniformLocation(precipitation_program_, "u_camera_right");
  precipitation_uniforms_.camera_up =
      glGetUniformLocation(precipitation_program_, "u_camera_up");
  precipitation_uniforms_.fog_color =
      glGetUniformLocation(precipitation_program_, "u_fog_color");
  precipitation_uniforms_.lightning_intensity =
      glGetUniformLocation(precipitation_program_, "u_lightning_intensity");
  precipitation_uniforms_.storm_intensity =
      glGetUniformLocation(precipitation_program_, "u_storm_intensity");
  precipitation_uniforms_.ship_protection_enabled =
      glGetUniformLocation(precipitation_program_, "u_ship_protection_enabled");
  precipitation_uniforms_.ship_inverse_model =
      glGetUniformLocation(precipitation_program_, "u_ship_inverse_model");
  precipitation_uniforms_.ship_bounds_min =
      glGetUniformLocation(precipitation_program_, "u_ship_bounds_min");
  precipitation_uniforms_.ship_bounds_max =
      glGetUniformLocation(precipitation_program_, "u_ship_bounds_max");
  precipitation_uniforms_.ship_profile_longitudinal = glGetUniformLocation(
      precipitation_program_, "u_ship_profile_longitudinal");
  precipitation_uniforms_.ship_profile_taper =
      glGetUniformLocation(precipitation_program_, "u_ship_profile_taper");
  precipitation_uniforms_.ship_profile_heights =
      glGetUniformLocation(precipitation_program_, "u_ship_profile_heights");
  precipitation_uniforms_.ship_profile_widths =
      glGetUniformLocation(precipitation_program_, "u_ship_profile_widths");
  precipitation_uniforms_.ship_sheltered_floor =
      glGetUniformLocation(precipitation_program_, "u_ship_sheltered_floor");

  old_guard_effect_uniforms_.view_projection =
      glGetUniformLocation(old_guard_effect_program_, "u_view_projection");
  old_guard_effect_uniforms_.camera_right =
      glGetUniformLocation(old_guard_effect_program_, "u_camera_right");
  old_guard_effect_uniforms_.camera_up =
      glGetUniformLocation(old_guard_effect_program_, "u_camera_up");

  const std::array<GLint, 16> precipitation_uniform_locations{{
      precipitation_uniforms_.view_projection,
      precipitation_uniforms_.camera_position,
      precipitation_uniforms_.camera_right,
      precipitation_uniforms_.camera_up,
      precipitation_uniforms_.fog_color,
      precipitation_uniforms_.lightning_intensity,
      precipitation_uniforms_.storm_intensity,
      precipitation_uniforms_.ship_protection_enabled,
      precipitation_uniforms_.ship_inverse_model,
      precipitation_uniforms_.ship_bounds_min,
      precipitation_uniforms_.ship_bounds_max,
      precipitation_uniforms_.ship_profile_longitudinal,
      precipitation_uniforms_.ship_profile_taper,
      precipitation_uniforms_.ship_profile_heights,
      precipitation_uniforms_.ship_profile_widths,
      precipitation_uniforms_.ship_sheltered_floor,
  }};
  if (std::any_of(precipitation_uniform_locations.begin(),
                  precipitation_uniform_locations.end(),
                  [](GLint location) noexcept { return location < 0; })) {
    throw std::runtime_error(
        "Precipitation shader is missing one or more required uniforms");
  }

  creature_uniforms_.view_projection =
      glGetUniformLocation(creature_program_, "u_view_projection");
  creature_uniforms_.light_view_projection =
      glGetUniformLocation(creature_program_, "u_light_view_projection");
  creature_uniforms_.light_view_projection_far =
      glGetUniformLocation(creature_program_, "u_light_view_projection_far");
  creature_uniforms_.camera_position =
      glGetUniformLocation(creature_program_, "u_camera_position");
  creature_uniforms_.camera_forward =
      glGetUniformLocation(creature_program_, "u_camera_forward");
  creature_uniforms_.sun_direction =
      glGetUniformLocation(creature_program_, "u_sun_direction");
  creature_uniforms_.sun_color =
      glGetUniformLocation(creature_program_, "u_sun_color");
  creature_uniforms_.ambient_color =
      glGetUniformLocation(creature_program_, "u_ambient_color");
  creature_uniforms_.fog_color =
      glGetUniformLocation(creature_program_, "u_fog_color");
  creature_uniforms_.distant_fog_color =
      glGetUniformLocation(creature_program_, "u_distant_fog_color");
  creature_uniforms_.horizon_glow_color =
      glGetUniformLocation(creature_program_, "u_horizon_glow_color");
  creature_uniforms_.night_tint_color =
      glGetUniformLocation(creature_program_, "u_night_tint_color");
  creature_uniforms_.daylight_factor =
      glGetUniformLocation(creature_program_, "u_daylight_factor");
  creature_uniforms_.sun_visibility =
      glGetUniformLocation(creature_program_, "u_sun_visibility");
  creature_uniforms_.cloud_intensity =
      glGetUniformLocation(creature_program_, "u_cloud_intensity");
  creature_uniforms_.cloud_shadow_strength =
      glGetUniformLocation(creature_program_, "u_cloud_shadow_strength");
  creature_uniforms_.atmospheric_scatter_strength =
      glGetUniformLocation(creature_program_, "u_atmospheric_scatter_strength");
  creature_uniforms_.height_fog_density =
      glGetUniformLocation(creature_program_, "u_height_fog_density");
  creature_uniforms_.precipitation_intensity =
      glGetUniformLocation(creature_program_, "u_precipitation_intensity");
  creature_uniforms_.storm_intensity =
      glGetUniformLocation(creature_program_, "u_storm_intensity");
  creature_uniforms_.lightning_intensity =
      glGetUniformLocation(creature_program_, "u_lightning_intensity");
  creature_uniforms_.atlas = glGetUniformLocation(creature_program_, "u_atlas");
  creature_uniforms_.shadow_map =
      glGetUniformLocation(creature_program_, "u_shadow_map");
  creature_uniforms_.shadow_map_far =
      glGetUniformLocation(creature_program_, "u_shadow_map_far");
  creature_uniforms_.shadow_cascade_count =
      glGetUniformLocation(creature_program_, "u_shadow_cascade_count");
  creature_uniforms_.shadow_split_distance =
      glGetUniformLocation(creature_program_, "u_shadow_split_distance");
  creature_uniforms_.shadow_transition_width =
      glGetUniformLocation(creature_program_, "u_shadow_transition_width");
  creature_uniforms_.shadows_enabled =
      glGetUniformLocation(creature_program_, "u_shadows_enabled");
  creature_uniforms_.time_of_day =
      glGetUniformLocation(creature_program_, "u_time_of_day");
  creature_uniforms_.player_light_strength =
      glGetUniformLocation(creature_program_, "u_player_light_strength");
  creature_uniforms_.enclosed_interior =
      glGetUniformLocation(creature_program_, "u_enclosed_interior");
  creature_uniforms_.interior_fog_range =
      glGetUniformLocation(creature_program_, "u_interior_fog_range");
  creature_uniforms_.backrooms_flicker_count =
      glGetUniformLocation(
          creature_program_,
          "u_backrooms_flicker_count");
  creature_uniforms_.backrooms_flicker_lights =
      glGetUniformLocation(
          creature_program_,
          "u_backrooms_flicker_lights[0]");
  creature_uniforms_.backrooms_flashlight_intensity =
      glGetUniformLocation(
          creature_program_,
          "u_backrooms_flashlight_intensity");
  creature_uniforms_.super_vision_strength =
      glGetUniformLocation(creature_program_, "u_super_vision_strength");
  creature_uniforms_.local_light_radiance =
      glGetUniformLocation(creature_program_, "u_local_light_radiance");
  creature_uniforms_.modern_pipeline =
      glGetUniformLocation(creature_program_, "u_modern_pipeline");
  if (creature_uniforms_.local_light_radiance < 0 ||
      creature_uniforms_.enclosed_interior < 0 ||
      creature_uniforms_.interior_fog_range < 0 ||
      creature_uniforms_.backrooms_flicker_count < 0 ||
      creature_uniforms_.backrooms_flicker_lights < 0 ||
      creature_uniforms_.backrooms_flashlight_intensity < 0) {
    throw std::runtime_error(
        "Creature shader is missing interior or shared light uniforms");
  }
  creature_shadow_light_view_projection_ =
      glGetUniformLocation(creature_shadow_program_, "u_light_view_projection");

  shadow_uniforms_.model = glGetUniformLocation(shadow_program_, "u_model");
  shadow_uniforms_.light_view_projection =
      glGetUniformLocation(shadow_program_, "u_light_view_projection");
  shadow_uniforms_.time_of_day =
      glGetUniformLocation(shadow_program_, "u_time_of_day");
  shadow_uniforms_.wind_strength =
      glGetUniformLocation(shadow_program_, "u_wind_strength");
  shadow_uniforms_.atlas = glGetUniformLocation(shadow_program_, "u_atlas");
  hud_uniforms_.atlas = glGetUniformLocation(hud_program_, "u_atlas");
  hud_uniforms_.font_atlas = glGetUniformLocation(hud_program_, "u_font_atlas");
  hud_uniforms_.model_icon_atlas =
      glGetUniformLocation(hud_program_, "u_model_icon_atlas");
  hud_uniforms_.jack_screamer =
      glGetUniformLocation(hud_program_, "u_jack_screamer");
  if (hud_uniforms_.atlas < 0 || hud_uniforms_.font_atlas < 0 ||
      hud_uniforms_.model_icon_atlas < 0 ||
      hud_uniforms_.jack_screamer < 0) {
    throw std::runtime_error(
        "HUD shader is missing one or more atlas uniforms");
  }
  sky_uniforms_.inverse_view_projection =
      glGetUniformLocation(sky_program_, "u_inverse_view_projection");
  sky_uniforms_.sun_direction =
      glGetUniformLocation(sky_program_, "u_sun_direction");
  sky_uniforms_.daylight_factor =
      glGetUniformLocation(sky_program_, "u_daylight_factor");
  sky_uniforms_.time_of_day =
      glGetUniformLocation(sky_program_, "u_time_of_day");
  sky_uniforms_.sky_zenith_color =
      glGetUniformLocation(sky_program_, "u_sky_zenith_color");
  sky_uniforms_.sky_horizon_color =
      glGetUniformLocation(sky_program_, "u_sky_horizon_color");
  sky_uniforms_.horizon_glow_color =
      glGetUniformLocation(sky_program_, "u_horizon_glow_color");
  sky_uniforms_.sun_disk_color =
      glGetUniformLocation(sky_program_, "u_sun_disk_color");
  sky_uniforms_.moon_disk_color =
      glGetUniformLocation(sky_program_, "u_moon_disk_color");
  sky_uniforms_.star_intensity =
      glGetUniformLocation(sky_program_, "u_star_intensity");
  sky_uniforms_.cloud_intensity =
      glGetUniformLocation(sky_program_, "u_cloud_intensity");
  sky_uniforms_.overcast_intensity =
      glGetUniformLocation(sky_program_, "u_overcast_intensity");
  sky_uniforms_.precipitation_intensity =
      glGetUniformLocation(sky_program_, "u_precipitation_intensity");
  sky_uniforms_.storm_intensity =
      glGetUniformLocation(sky_program_, "u_storm_intensity");
  sky_uniforms_.violent_storm_intensity =
      glGetUniformLocation(sky_program_, "u_violent_storm_intensity");
  sky_uniforms_.lightning_intensity =
      glGetUniformLocation(sky_program_, "u_lightning_intensity");
  sky_uniforms_.lightning_bolt_intensity =
      glGetUniformLocation(sky_program_, "u_lightning_bolt_intensity");
  sky_uniforms_.lightning_direction =
      glGetUniformLocation(sky_program_, "u_lightning_direction");
  sky_uniforms_.lightning_shape_seed =
      glGetUniformLocation(sky_program_, "u_lightning_shape_seed");
  const std::array<GLint, 4> violent_storm_sky_uniform_locations{{
      sky_uniforms_.violent_storm_intensity,
      sky_uniforms_.lightning_bolt_intensity,
      sky_uniforms_.lightning_direction,
      sky_uniforms_.lightning_shape_seed,
  }};
  // Je refuse de masquer une faute d'uniform avec le comportement silencieux
  // de glUniform(-1, ...), car elle désactiverait une partie de la Tempest.
  if (std::any_of(violent_storm_sky_uniform_locations.begin(),
                  violent_storm_sky_uniform_locations.end(),
                  [](GLint location) noexcept { return location < 0; })) {
    throw std::runtime_error(
        "Sky shader is missing one or more violent storm uniforms");
  }
  sky_uniforms_.weather_time =
      glGetUniformLocation(sky_program_, "u_weather_time");
  sky_uniforms_.cloud_steps =
      glGetUniformLocation(sky_program_, "u_cloud_steps");
  sky_uniforms_.cloud_detail =
      glGetUniformLocation(sky_program_, "u_cloud_detail");
  sky_uniforms_.accent_atlas =
      glGetUniformLocation(sky_program_, "u_accent_atlas");
  sky_uniforms_.maritime_horizon_enabled =
      glGetUniformLocation(sky_program_, "u_maritime_horizon_enabled");
  sky_uniforms_.maritime_camera_position =
      glGetUniformLocation(sky_program_, "u_maritime_camera_position");
  sky_uniforms_.maritime_sea_level =
      glGetUniformLocation(sky_program_, "u_maritime_sea_level");
  sky_uniforms_.maritime_submersion_active =
      glGetUniformLocation(sky_program_, "u_maritime_submersion_active");
  sky_uniforms_.ocean_horizon_waves =
      glGetUniformLocation(sky_program_, "u_ocean_horizon_waves[0]");
  sky_uniforms_.ocean_horizon_wave_phases =
      glGetUniformLocation(sky_program_, "u_ocean_horizon_wave_phases[0]");
  sky_uniforms_.ocean_horizon_severity =
      glGetUniformLocation(sky_program_, "u_ocean_horizon_severity");
  sky_uniforms_.ocean_horizon_tempest_factor =
      glGetUniformLocation(sky_program_, "u_ocean_horizon_tempest_factor");
  sky_uniforms_.ocean_horizon_sun_color =
      glGetUniformLocation(sky_program_, "u_ocean_horizon_sun_color");
  sky_uniforms_.maritime_far_fog_range =
      glGetUniformLocation(sky_program_, "u_maritime_far_fog_range");
  sky_uniforms_.fog_color = glGetUniformLocation(sky_program_, "u_fog_color");
  sky_uniforms_.distant_fog_color =
      glGetUniformLocation(sky_program_, "u_distant_fog_color");
  const std::array<GLint, 12> maritime_sky_uniform_locations{{
      sky_uniforms_.maritime_horizon_enabled,
      sky_uniforms_.maritime_camera_position,
      sky_uniforms_.maritime_sea_level,
      sky_uniforms_.maritime_submersion_active,
      sky_uniforms_.ocean_horizon_waves,
      sky_uniforms_.ocean_horizon_wave_phases,
      sky_uniforms_.ocean_horizon_severity,
      sky_uniforms_.ocean_horizon_tempest_factor,
      sky_uniforms_.ocean_horizon_sun_color,
      sky_uniforms_.maritime_far_fog_range,
      sky_uniforms_.fog_color,
      sky_uniforms_.distant_fog_color,
  }};
  if (std::any_of(maritime_sky_uniform_locations.begin(),
                  maritime_sky_uniform_locations.end(),
                  [](GLint location) noexcept { return location < 0; })) {
    throw std::runtime_error(
        "Sky shader is missing one or more maritime horizon uniforms");
  }
  glow_extract_uniforms_.scene_texture =
      glGetUniformLocation(glow_extract_program_, "u_scene_texture");
  glow_extract_uniforms_.threshold =
      glGetUniformLocation(glow_extract_program_, "u_threshold");
  glow_blur_uniforms_.source_texture =
      glGetUniformLocation(glow_blur_program_, "u_source_texture");
  glow_blur_uniforms_.texel_direction =
      glGetUniformLocation(glow_blur_program_, "u_texel_direction");
  post_process_uniforms_.scene_texture =
      glGetUniformLocation(post_process_program_, "u_scene_texture");
  post_process_uniforms_.glow_texture =
      glGetUniformLocation(post_process_program_, "u_glow_texture");
  post_process_uniforms_.scene_depth =
      glGetUniformLocation(post_process_program_, "u_scene_depth");
  post_process_uniforms_.exposure =
      glGetUniformLocation(post_process_program_, "u_exposure");
  post_process_uniforms_.saturation_boost =
      glGetUniformLocation(post_process_program_, "u_saturation_boost");
  post_process_uniforms_.contrast =
      glGetUniformLocation(post_process_program_, "u_contrast");
  post_process_uniforms_.vignette_strength =
      glGetUniformLocation(post_process_program_, "u_vignette_strength");
  post_process_uniforms_.night_tint_color =
      glGetUniformLocation(post_process_program_, "u_night_tint_color");
  post_process_uniforms_.glow_strength =
      glGetUniformLocation(post_process_program_, "u_glow_strength");
  post_process_uniforms_.sharpen_strength =
      glGetUniformLocation(post_process_program_, "u_sharpen_strength");
  post_process_uniforms_.edge_strength =
      glGetUniformLocation(post_process_program_, "u_edge_strength");
  post_process_uniforms_.fxaa_enabled =
      glGetUniformLocation(post_process_program_, "u_fxaa_enabled");
  post_process_uniforms_.modern_pipeline =
      glGetUniformLocation(post_process_program_, "u_modern_pipeline");
  post_process_uniforms_.resolve_only =
      glGetUniformLocation(post_process_program_, "u_resolve_only");
  post_process_uniforms_.storm_intensity =
      glGetUniformLocation(post_process_program_, "u_storm_intensity");
  post_process_uniforms_.lightning_intensity =
      glGetUniformLocation(post_process_program_, "u_lightning_intensity");
  post_process_uniforms_.weather_exposure =
      glGetUniformLocation(post_process_program_, "u_weather_exposure");
  post_process_uniforms_.projection_far_distance =
      glGetUniformLocation(post_process_program_, "u_projection_far_distance");
  post_process_uniforms_.maritime_submerged =
      glGetUniformLocation(post_process_program_, "u_maritime_submerged");
  post_process_uniforms_.maritime_submersion_depth = glGetUniformLocation(
      post_process_program_, "u_maritime_submersion_depth");
  post_process_uniforms_.maritime_submersion_blend = glGetUniformLocation(
      post_process_program_, "u_maritime_submersion_blend");
  post_process_uniforms_.water_surface_detail =
      glGetUniformLocation(post_process_program_, "u_water_surface_detail");
  post_process_uniforms_.time_seconds =
      glGetUniformLocation(post_process_program_, "u_time_seconds");
  post_process_uniforms_.daylight_factor =
      glGetUniformLocation(post_process_program_, "u_daylight_factor");
  if (post_process_uniforms_.weather_exposure < 0 ||
      post_process_uniforms_.projection_far_distance < 0 ||
      post_process_uniforms_.fxaa_enabled < 0 ||
      post_process_uniforms_.modern_pipeline < 0 ||
      post_process_uniforms_.resolve_only < 0 ||
      post_process_uniforms_.maritime_submerged < 0 ||
      post_process_uniforms_.maritime_submersion_depth < 0 ||
      post_process_uniforms_.maritime_submersion_blend < 0 ||
      post_process_uniforms_.water_surface_detail < 0 ||
      post_process_uniforms_.time_seconds < 0 ||
      post_process_uniforms_.daylight_factor < 0) {
    throw std::runtime_error(
        "Post-process shader is missing a required modern uniform");
  }
  menu_background_uniforms_.scene_texture =
      glGetUniformLocation(menu_background_program_, "u_scene_texture");
  menu_background_uniforms_.blur_texture =
      glGetUniformLocation(menu_background_program_, "u_blur_texture");
  menu_background_uniforms_.blur_mix =
      glGetUniformLocation(menu_background_program_, "u_blur_mix");
  menu_background_uniforms_.tint_color =
      glGetUniformLocation(menu_background_program_, "u_tint_color");
  menu_background_uniforms_.vignette_strength =
      glGetUniformLocation(menu_background_program_, "u_vignette_strength");
  menu_background_uniforms_.exposure =
      glGetUniformLocation(menu_background_program_, "u_exposure");
  menu_background_uniforms_.modern_pipeline =
      glGetUniformLocation(menu_background_program_, "u_modern_pipeline");
  if (menu_background_uniforms_.scene_texture < 0 ||
      menu_background_uniforms_.blur_texture < 0 ||
      menu_background_uniforms_.blur_mix < 0 ||
      menu_background_uniforms_.tint_color < 0 ||
      menu_background_uniforms_.vignette_strength < 0 ||
      menu_background_uniforms_.exposure < 0 ||
      menu_background_uniforms_.modern_pipeline < 0) {
    throw std::runtime_error(
        "Menu background shader is missing a required uniform");
  }
}

void Renderer::create_atlas_texture() {
  const auto pixels = build_block_atlas_pixels();

  glGenTextures(1, &atlas_texture_);
  glBindTexture(GL_TEXTURE_2D, atlas_texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kBlockAtlasSize, kBlockAtlasSize, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

auto Renderer::create_msdf_font_texture() -> bool {
  if (msdf_font_texture_ != 0 && g_modern_hud_font_atlas.has_value()) {
    g_modern_hud_font_enabled = true;
    return true;
  }

  g_modern_hud_font_enabled = false;
  std::error_code path_error;
  const auto working_directory = std::filesystem::current_path(path_error);
  if (path_error) {
    last_initialization_error_ =
        "Unable to resolve the working directory for the UI font atlas";
    return false;
  }
  const std::array candidates{
      working_directory / "assets" / "fonts" / "valcraft_ui_font.msdfa",
      working_directory / "bin" / "assets" / "fonts" / "valcraft_ui_font.msdfa",
      working_directory.parent_path() / "assets" / "fonts" /
          "valcraft_ui_font.msdfa",
      working_directory.parent_path().parent_path() / "assets" / "fonts" /
          "valcraft_ui_font.msdfa",
  };

  std::optional<MsdfFontAtlas> loaded_atlas;
  for (const auto &candidate : candidates) {
    std::error_code exists_error;
    if (!std::filesystem::is_regular_file(candidate, exists_error) ||
        exists_error) {
      continue;
    }
    auto loaded = load_msdf_font_atlas_file(candidate);
    if (!loaded) {
      last_initialization_error_ = "Invalid modern UI font atlas '" +
                                   candidate.string() + "': " + loaded.error;
      return false;
    }
    loaded_atlas = std::move(loaded.atlas);
    break;
  }
  if (!loaded_atlas.has_value()) {
    last_initialization_error_ =
        "Unable to find assets/fonts/valcraft_ui_font.msdfa";
    return false;
  }

  const auto &atlas = *loaded_atlas;
  GLint maximum_texture_size = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_size);
  if (atlas.metadata().width >
          static_cast<std::uint32_t>(std::max(maximum_texture_size, 0)) ||
      atlas.metadata().height >
          static_cast<std::uint32_t>(std::max(maximum_texture_size, 0))) {
    last_initialization_error_ =
        "The modern UI font atlas exceeds GL_MAX_TEXTURE_SIZE";
    return false;
  }

  // Je retire une éventuelle erreur antérieure avant de contrôler
  // exclusivement les allocations de cette ressource.
  for (int error_index = 0; error_index < 16 && glGetError() != GL_NO_ERROR;
       ++error_index) {
  }
  glGenTextures(1, &msdf_font_texture_);
  glBindTexture(GL_TEXTURE_2D, msdf_font_texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                  static_cast<GLint>(atlas.metadata().mip_count - 1U));

  const auto all_pixels = atlas.pixels();
  for (std::size_t mip_index = 0U; mip_index < atlas.mip_levels().size();
       ++mip_index) {
    const auto &mip = atlas.mip_levels()[mip_index];
    if (mip.byte_offset > all_pixels.size() ||
        mip.byte_size > all_pixels.size() - mip.byte_offset) {
      destroy_msdf_font_texture();
      last_initialization_error_ =
          "The modern UI font mip chain is out of bounds";
      return false;
    }
    const auto pixels = all_pixels.subspan(mip.byte_offset, mip.byte_size);
    glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(mip_index), GL_RGB8,
                 static_cast<GLsizei>(mip.width),
                 static_cast<GLsizei>(mip.height), 0, GL_RGB, GL_UNSIGNED_BYTE,
                 pixels.data());
  }
  if (glGetError() != GL_NO_ERROR) {
    destroy_msdf_font_texture();
    last_initialization_error_ =
        "OpenGL rejected the modern UI font atlas upload";
    return false;
  }

  msdf_font_width_ = atlas.metadata().width;
  msdf_font_height_ = atlas.metadata().height;
  msdf_font_mips_ = atlas.metadata().mip_count;
  g_modern_hud_font_atlas = std::move(*loaded_atlas);
  g_modern_hud_font_enabled = true;
  glBindTexture(GL_TEXTURE_2D, 0);
  return true;
}

void Renderer::destroy_msdf_font_texture() {
  if (msdf_font_texture_ != 0) {
    glDeleteTextures(1, &msdf_font_texture_);
  }
  msdf_font_texture_ = 0;
  msdf_font_width_ = 0U;
  msdf_font_height_ = 0U;
  msdf_font_mips_ = 0U;
  g_modern_hud_font_enabled = false;
  g_modern_hud_font_atlas.reset();
}

auto Renderer::create_model_icon_texture() -> bool {
  if (model_icon_texture_ != 0 && model_icon_layers_ > 0U) {
    return true;
  }

  std::error_code path_error;
  const auto working_directory = std::filesystem::current_path(path_error);
  if (path_error) {
    last_initialization_error_ =
        "Unable to resolve the working directory for the model icon atlas";
    return false;
  }
  const std::array candidates{
      working_directory / "assets" / "visual" / "valcraft_model_icons.vmia",
      working_directory / "bin" / "assets" / "visual" /
          "valcraft_model_icons.vmia",
      working_directory.parent_path() / "assets" / "visual" /
          "valcraft_model_icons.vmia",
      working_directory.parent_path().parent_path() / "assets" / "visual" /
          "valcraft_model_icons.vmia",
  };

  std::optional<ModelIconAtlas> loaded_atlas;
  for (const auto &candidate : candidates) {
    std::error_code exists_error;
    if (!std::filesystem::is_regular_file(candidate, exists_error) ||
        exists_error) {
      continue;
    }
    auto loaded = load_model_icon_atlas(candidate);
    if (!loaded || !loaded.atlas.has_value()) {
      last_initialization_error_ = "Invalid modern model icon atlas '" +
                                   candidate.string() + "': " + loaded.message;
      return false;
    }
    loaded_atlas = std::move(loaded.atlas);
    break;
  }
  if (!loaded_atlas.has_value()) {
    last_initialization_error_ =
        "Unable to find assets/visual/valcraft_model_icons.vmia";
    return false;
  }

  const auto &atlas = *loaded_atlas;
  GLint maximum_texture_size = 0;
  GLint maximum_array_layers = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_size);
  glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maximum_array_layers);
  if (atlas.metadata.width >
          static_cast<std::uint16_t>(std::max(maximum_texture_size, 0)) ||
      atlas.metadata.height >
          static_cast<std::uint16_t>(std::max(maximum_texture_size, 0)) ||
      atlas.layers.size() >
          static_cast<std::size_t>(std::max(maximum_array_layers, 0))) {
    last_initialization_error_ =
        "The modern model icon atlas exceeds the OpenGL 3.3 limits";
    return false;
  }

  for (int error_index = 0; error_index < 16 && glGetError() != GL_NO_ERROR;
       ++error_index) {
  }
  glGenTextures(1, &model_icon_texture_);
  glBindTexture(GL_TEXTURE_2D_ARRAY, model_icon_texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL,
                  static_cast<GLint>(atlas.metadata.mip_count - 1U));

  for (std::size_t mip_index = 0U; mip_index < atlas.mip_levels.size();
       ++mip_index) {
    const auto &mip = atlas.mip_levels[mip_index];
    glTexImage3D(GL_TEXTURE_2D_ARRAY, static_cast<GLint>(mip_index),
                 GL_SRGB8_ALPHA8, static_cast<GLsizei>(mip.width),
                 static_cast<GLsizei>(mip.height),
                 static_cast<GLsizei>(atlas.layers.size()), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    for (std::size_t layer_index = 0U; layer_index < atlas.layers.size();
         ++layer_index) {
      const auto pixels =
          atlas.texels_for(atlas.layers[layer_index].item_id,
                           static_cast<std::uint16_t>(mip_index));
      if (pixels.size() != mip.byte_count) {
        destroy_model_icon_texture();
        last_initialization_error_ =
            "The modern model icon mip chain is out of bounds";
        return false;
      }
      glTexSubImage3D(GL_TEXTURE_2D_ARRAY, static_cast<GLint>(mip_index), 0, 0,
                      static_cast<GLint>(layer_index),
                      static_cast<GLsizei>(mip.width),
                      static_cast<GLsizei>(mip.height), 1, GL_RGBA,
                      GL_UNSIGNED_BYTE, pixels.data());
    }
  }
  if (glGetError() != GL_NO_ERROR) {
    destroy_model_icon_texture();
    last_initialization_error_ =
        "OpenGL rejected the modern model icon atlas upload";
    return false;
  }

  model_icon_layer_by_block_.fill(0U);
  for (std::size_t block_index = 0U;
       block_index < model_icon_layer_by_block_.size(); ++block_index) {
    const auto layer_index =
        visual_item_layer_index(static_cast<BlockId>(block_index));
    if (layer_index < atlas.layers.size()) {
      model_icon_layer_by_block_[block_index] =
          static_cast<std::uint16_t>(layer_index + 1U);
    }
  }
  model_icon_width_ = atlas.metadata.width;
  model_icon_height_ = atlas.metadata.height;
  model_icon_layers_ = static_cast<std::uint16_t>(atlas.layers.size());
  model_icon_mips_ = atlas.metadata.mip_count;
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  return true;
}

void Renderer::destroy_model_icon_texture() {
  if (model_icon_texture_ != 0) {
    glDeleteTextures(1, &model_icon_texture_);
  }
  model_icon_texture_ = 0;
  model_icon_width_ = 0U;
  model_icon_height_ = 0U;
  model_icon_layers_ = 0U;
  model_icon_mips_ = 0U;
  model_icon_layer_by_block_.fill(0U);
}

auto Renderer::create_backrooms_jack_screamer_texture() -> bool {
  if (backrooms_jack_screamer_texture_ != 0) {
    return true;
  }

  std::error_code path_error;
  const auto working_directory =
      std::filesystem::current_path(path_error);
  if (path_error) {
    last_initialization_error_ =
        "Unable to resolve the working directory for Jack's screamer";
    return false;
  }
  const auto path =
      resolve_backrooms_jack_screamer_path(working_directory);
  if (path.empty()) {
    last_initialization_error_ =
        "Missing assets/backrooms/jack_le_pirate_screamer.bmp";
    return false;
  }
  auto image =
      load_backrooms_jack_screamer_bmp(path);
  if (!image.valid()) {
    last_initialization_error_ =
        image.error.empty()
            ? "Invalid Jack the pirate screamer bitmap"
            : image.error;
    return false;
  }

  GLint maximum_texture_size = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_size);
  if (image.width > maximum_texture_size ||
      image.height > maximum_texture_size) {
    last_initialization_error_ =
        "Jack the pirate screamer exceeds the GPU texture limit";
    return false;
  }

  glGenTextures(
      1,
      &backrooms_jack_screamer_texture_);
  glBindTexture(
      GL_TEXTURE_2D,
      backrooms_jack_screamer_texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(
      GL_TEXTURE_2D,
      0,
      GL_SRGB8_ALPHA8,
      image.width,
      image.height,
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      image.rgba.data());
  glTexParameteri(
      GL_TEXTURE_2D,
      GL_TEXTURE_MIN_FILTER,
      GL_LINEAR);
  glTexParameteri(
      GL_TEXTURE_2D,
      GL_TEXTURE_MAG_FILTER,
      GL_LINEAR);
  glTexParameteri(
      GL_TEXTURE_2D,
      GL_TEXTURE_WRAP_S,
      GL_CLAMP_TO_EDGE);
  glTexParameteri(
      GL_TEXTURE_2D,
      GL_TEXTURE_WRAP_T,
      GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  backrooms_jack_screamer_width_ =
      static_cast<std::uint16_t>(image.width);
  backrooms_jack_screamer_height_ =
      static_cast<std::uint16_t>(image.height);
  return true;
}

void Renderer::destroy_backrooms_jack_screamer_texture() {
  if (backrooms_jack_screamer_texture_ != 0) {
    glDeleteTextures(
        1,
        &backrooms_jack_screamer_texture_);
  }
  backrooms_jack_screamer_texture_ = 0;
  backrooms_jack_screamer_width_ = 0U;
  backrooms_jack_screamer_height_ = 0U;
}

auto Renderer::create_backrooms_marlow_screamer_texture() -> bool {
  if (backrooms_marlow_screamer_texture_ != 0) {
    return true;
  }

  std::error_code path_error;
  const auto working_directory =
      std::filesystem::current_path(path_error);
  if (path_error) {
    last_initialization_error_ =
        "Unable to resolve the working directory for Marlow's screamer";
    return false;
  }
  const auto path =
      resolve_backrooms_marlow_screamer_path(working_directory);
  if (path.empty()) {
    last_initialization_error_ =
        "Missing assets/backrooms/marlow_le_noye_screamer.bmp";
    return false;
  }
  const auto image = load_backrooms_jack_screamer_bmp(path);
  if (!image.valid()) {
    last_initialization_error_ = image.error.empty()
                                     ? "Invalid Marlow screamer bitmap"
                                     : image.error;
    return false;
  }

  GLint maximum_texture_size = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_size);
  if (image.width > maximum_texture_size ||
      image.height > maximum_texture_size) {
    last_initialization_error_ =
        "Marlow screamer exceeds the GPU texture limit";
    return false;
  }

  glGenTextures(1, &backrooms_marlow_screamer_texture_);
  glBindTexture(GL_TEXTURE_2D, backrooms_marlow_screamer_texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(
      GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8,
      image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
      image.rgba.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  backrooms_marlow_screamer_width_ =
      static_cast<std::uint16_t>(image.width);
  backrooms_marlow_screamer_height_ =
      static_cast<std::uint16_t>(image.height);
  return true;
}

void Renderer::destroy_backrooms_marlow_screamer_texture() {
  if (backrooms_marlow_screamer_texture_ != 0) {
    glDeleteTextures(1, &backrooms_marlow_screamer_texture_);
  }
  backrooms_marlow_screamer_texture_ = 0;
  backrooms_marlow_screamer_width_ = 0U;
  backrooms_marlow_screamer_height_ = 0U;
}

auto Renderer::hud_item_texture_mode(BlockId block_id) const noexcept -> float {
  const auto layer =
      model_icon_layer_by_block_[static_cast<std::size_t>(block_id)];
  if (options_.visual_pipeline != VisualPipeline::ModernStylized ||
      model_icon_texture_ == 0 || layer == 0U) {
    return 1.0F;
  }
  return 3.0F + static_cast<float>(layer - 1U);
}

void Renderer::bind_hud_textures() {
  glUniform1i(hud_uniforms_.atlas, 0);
  glUniform1i(hud_uniforms_.font_atlas, 1);
  glUniform1i(hud_uniforms_.model_icon_atlas, 2);
  glUniform1i(hud_uniforms_.jack_screamer, 3);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, atlas_texture_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, msdf_font_texture_);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D_ARRAY, model_icon_texture_);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(
      GL_TEXTURE_2D,
      backrooms_jack_screamer_texture_);
  glActiveTexture(GL_TEXTURE0);
}

auto Renderer::create_modern_material_textures() -> bool {
  if (modern_material_albedo_texture_ != 0 &&
      modern_material_normal_height_texture_ != 0 &&
      modern_material_orm_emission_texture_ != 0) {
    return true;
  }

  std::error_code path_error;
  const auto working_directory = std::filesystem::current_path(path_error);
  if (path_error) {
    return false;
  }
  const std::array candidates{
      working_directory / "assets" / "visual" / "valcraft_visual_materials.vmp",
      working_directory / "bin" / "assets" / "visual" /
          "valcraft_visual_materials.vmp",
      working_directory.parent_path() / "assets" / "visual" /
          "valcraft_visual_materials.vmp",
      working_directory.parent_path().parent_path() / "assets" / "visual" /
          "valcraft_visual_materials.vmp",
  };

  const VisualMaterialPack *selected_pack = nullptr;
  VisualMaterialPackLoadResult load_result{};
  for (const auto &candidate : candidates) {
    std::error_code exists_error;
    if (!std::filesystem::is_regular_file(candidate, exists_error) ||
        exists_error) {
      continue;
    }
    load_result = load_visual_material_pack(candidate);
    if (!load_result) {
      return false;
    }
    selected_pack = &*load_result.pack;
    break;
  }
  if (selected_pack == nullptr || selected_pack->layers.empty()) {
    return false;
  }

  const auto &pack = *selected_pack;
  GLint maximum_layers = 0;
  GLint maximum_texture_size = 0;
  glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maximum_layers);
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_size);
  if (pack.layers.size() >
          static_cast<std::size_t>(std::max(maximum_layers, 0)) ||
      pack.width >
          static_cast<std::uint16_t>(std::max(maximum_texture_size, 0)) ||
      pack.height >
          static_cast<std::uint16_t>(std::max(maximum_texture_size, 0))) {
    return false;
  }

  const auto anisotropy_supported =
      supports_gl_extension("GL_EXT_texture_filter_anisotropic") ||
      supports_gl_extension("GL_ARB_texture_filter_anisotropic");
  GLfloat maximum_anisotropy = 1.0F;
  if (anisotropy_supported) {
    glGetFloatv(kMaxTextureMaxAnisotropyExt, &maximum_anisotropy);
  }

  const auto upload_array = [&pack, anisotropy_supported, maximum_anisotropy](
                                GLuint &texture,
                                VisualMaterialTexture material_texture,
                                GLint internal_format) -> bool {
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL,
                    static_cast<GLint>(pack.mip_count - 1U));
    if (anisotropy_supported) {
      glTexParameterf(GL_TEXTURE_2D_ARRAY, kTextureMaxAnisotropyExt,
                      std::min(maximum_anisotropy, 8.0F));
    }

    auto mip_width = pack.width;
    auto mip_height = pack.height;
    for (std::uint16_t mip = 0U; mip < pack.mip_count; ++mip) {
      glTexImage3D(GL_TEXTURE_2D_ARRAY, static_cast<GLint>(mip),
                   internal_format, static_cast<GLsizei>(mip_width),
                   static_cast<GLsizei>(mip_height),
                   static_cast<GLsizei>(pack.layers.size()), 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, nullptr);
      for (std::size_t layer = 0U; layer < pack.layers.size(); ++layer) {
        const auto texels = pack.texels_for(pack.layers[layer].material_id,
                                            material_texture, mip);
        const auto expected_size = static_cast<std::size_t>(mip_width) *
                                   static_cast<std::size_t>(mip_height) *
                                   kVisualMaterialPackChannelCount;
        if (texels.size() != expected_size) {
          return false;
        }
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, static_cast<GLint>(mip), 0, 0,
                        static_cast<GLint>(layer),
                        static_cast<GLsizei>(mip_width),
                        static_cast<GLsizei>(mip_height), 1, GL_RGBA,
                        GL_UNSIGNED_BYTE, texels.data());
      }
      mip_width = std::max<std::uint16_t>(mip_width / 2U, 1U);
      mip_height = std::max<std::uint16_t>(mip_height / 2U, 1U);
    }
    return glGetError() == GL_NO_ERROR;
  };

  if (!upload_array(modern_material_albedo_texture_,
                    VisualMaterialTexture::Albedo, GL_SRGB8_ALPHA8) ||
      !upload_array(modern_material_normal_height_texture_,
                    VisualMaterialTexture::NormalHeight, GL_RGBA8) ||
      !upload_array(modern_material_orm_emission_texture_,
                    VisualMaterialTexture::OrmEmission, GL_RGBA8)) {
    destroy_modern_material_textures();
    return false;
  }

  material_pack_version_ = pack.format_version;
  material_pack_checksum_ = pack.content_checksum;
  material_pack_width_ = pack.width;
  material_pack_height_ = pack.height;
  material_pack_layers_ = static_cast<std::uint16_t>(pack.layers.size());
  material_pack_mips_ = pack.mip_count;
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  return true;
}

void Renderer::destroy_modern_material_textures() {
  if (modern_material_orm_emission_texture_ != 0) {
    glDeleteTextures(1, &modern_material_orm_emission_texture_);
    modern_material_orm_emission_texture_ = 0;
  }
  if (modern_material_normal_height_texture_ != 0) {
    glDeleteTextures(1, &modern_material_normal_height_texture_);
    modern_material_normal_height_texture_ = 0;
  }
  if (modern_material_albedo_texture_ != 0) {
    glDeleteTextures(1, &modern_material_albedo_texture_);
    modern_material_albedo_texture_ = 0;
  }
  material_pack_checksum_ = 0U;
  material_pack_version_ = 0U;
  material_pack_width_ = 0U;
  material_pack_height_ = 0U;
  material_pack_layers_ = 0U;
  material_pack_mips_ = 0U;
}

void Renderer::create_accent_texture() {
  const auto pixels = build_accent_atlas_pixels();

  glGenTextures(1, &accent_texture_);
  glBindTexture(GL_TEXTURE_2D, accent_texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kAccentAtlasSize, kAccentAtlasSize,
               0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Renderer::create_creature_atlas_texture() {
  const auto pixels = build_creature_atlas_pixels();
  const auto color_format = is_modern_visual_pipeline(options_.visual_pipeline)
                                ? GL_SRGB8_ALPHA8
                                : GL_RGBA8;

  glGenTextures(1, &creature_atlas_texture_);
  glBindTexture(GL_TEXTURE_2D, creature_atlas_texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, color_format, kCreatureAtlasSize,
               kCreatureAtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  const auto filter = is_modern_visual_pipeline(options_.visual_pipeline)
                          ? GL_LINEAR
                          : GL_NEAREST;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Renderer::create_player_atlas_texture() {
  const auto pixels = build_player_atlas_pixels();
  const auto color_format = is_modern_visual_pipeline(options_.visual_pipeline)
                                ? GL_SRGB8_ALPHA8
                                : GL_RGBA8;

  glGenTextures(1, &player_atlas_texture_);
  glBindTexture(GL_TEXTURE_2D, player_atlas_texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, color_format, kPlayerAtlasSize,
               kPlayerAtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  const auto filter = is_modern_visual_pipeline(options_.visual_pipeline)
                          ? GL_LINEAR
                          : GL_NEAREST;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Renderer::create_shadow_map() {
  const std::array<float, 4> border_color{{1.0F, 1.0F, 1.0F, 1.0F}};
  const auto initialize_depth_texture = [&](GLuint &texture) {
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    if (!options_.shadows_enabled) {
      const float depth_value = 1.0F;
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 1, 1, 0,
                   GL_DEPTH_COMPONENT, GL_FLOAT, &depth_value);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      return;
    }

    const auto shadow_map_size = std::max(options_.shadow_map_size, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadow_map_size,
                 shadow_map_size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR,
                     border_color.data());
  };
  initialize_depth_texture(shadow_map_);
  initialize_depth_texture(shadow_map_far_);

  if (!options_.shadows_enabled) {
    return;
  }

  const auto initialize_framebuffer = [](GLuint texture, GLuint &framebuffer,
                                         const char *label) {
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           texture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      throw std::runtime_error(std::string{label} +
                               " shadow framebuffer is incomplete");
    }
  };
  initialize_framebuffer(shadow_map_, shadow_framebuffer_, "Near cascade");
  initialize_framebuffer(shadow_map_far_, shadow_framebuffer_far_,
                         "Far cascade");
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::destroy_shadow_map() {
  if (shadow_framebuffer_far_ != 0) {
    glDeleteFramebuffers(1, &shadow_framebuffer_far_);
    shadow_framebuffer_far_ = 0;
  }
  if (shadow_framebuffer_ != 0) {
    glDeleteFramebuffers(1, &shadow_framebuffer_);
    shadow_framebuffer_ = 0;
  }
  if (shadow_map_far_ != 0) {
    glDeleteTextures(1, &shadow_map_far_);
    shadow_map_far_ = 0;
  }
  if (shadow_map_ != 0) {
    glDeleteTextures(1, &shadow_map_);
    shadow_map_ = 0;
  }
}

void Renderer::create_scene_sampler_fallback_textures() {
  glGenTextures(1, &scene_fallback_color_texture_);
  glBindTexture(GL_TEXTURE_2D, scene_fallback_color_texture_);
  const std::array<std::uint8_t, 4> fallback_color{{0U, 0U, 0U, 255U}};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               fallback_color.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenTextures(1, &scene_fallback_depth_texture_);
  glBindTexture(GL_TEXTURE_2D, scene_fallback_depth_texture_);
  const float fallback_depth = 1.0F;
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 1, 1, 0,
               GL_DEPTH_COMPONENT, GL_FLOAT, &fallback_depth);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Renderer::create_creature_geometry() {
  glGenBuffers(1, &creature_vbo_);
  glGenBuffers(1, &creature_ebo_);
  glBindBuffer(GL_ARRAY_BUFFER, creature_vbo_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, creature_ebo_);
  if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
    std::vector<StylizedPrimitiveVertex> template_vertices{};
    std::vector<std::uint32_t> template_indices{};
    const auto &primitive_cache = visual_entity_primitive_cache();
    for (std::size_t slot = 0U; slot < visual_entity_draw_ranges_.size();
         ++slot) {
      const auto primitive = visual_entity_primitive_for_slot(slot);
      const auto lod = visual_entity_lod_for_slot(slot);
      const auto &template_mesh = primitive_cache.mesh(primitive, lod);
      const auto base_vertex =
          static_cast<std::uint32_t>(template_vertices.size());
      auto &range = visual_entity_draw_ranges_[slot];
      range.first_index = template_indices.size();
      range.index_count = static_cast<GLsizei>(template_mesh.indices.size());
      range.primitive = primitive;
      range.lod = lod;

      template_vertices.insert(template_vertices.end(),
                               template_mesh.vertices.begin(),
                               template_mesh.vertices.end());
      template_indices.reserve(template_indices.size() +
                               template_mesh.indices.size());
      for (const auto index : template_mesh.indices) {
        template_indices.push_back(base_vertex + index);
      }
    }
    creature_template_vertex_buffer_bytes_ = static_cast<GLsizeiptr>(
        template_vertices.size() * sizeof(StylizedPrimitiveVertex));
    creature_template_index_buffer_bytes_ = static_cast<GLsizeiptr>(
        template_indices.size() * sizeof(std::uint32_t));
    creature_template_index_count_ =
        static_cast<GLsizei>(template_indices.size());
    glBufferData(GL_ARRAY_BUFFER, creature_template_vertex_buffer_bytes_,
                 template_vertices.data(), GL_STATIC_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, creature_template_index_buffer_bytes_,
                 template_indices.data(), GL_STATIC_DRAW);
  } else {
    visual_entity_draw_ranges_ = {};
    const auto &template_vertices = box_template_vertices();
    const auto &template_indices = box_template_indices();
    creature_template_vertex_buffer_bytes_ = static_cast<GLsizeiptr>(
        template_vertices.size() * sizeof(BoxTemplateVertex));
    creature_template_index_buffer_bytes_ = static_cast<GLsizeiptr>(
        template_indices.size() * sizeof(std::uint32_t));
    creature_template_index_count_ =
        static_cast<GLsizei>(template_indices.size());
    glBufferData(GL_ARRAY_BUFFER, creature_template_vertex_buffer_bytes_,
                 template_vertices.data(), GL_STATIC_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, creature_template_index_buffer_bytes_,
                 template_indices.data(), GL_STATIC_DRAW);
  }

  glGenVertexArrays(1, &creature_vao_);
  glGenBuffers(1, &creature_instance_vbo_);
  configure_box_template_attributes(creature_vao_, creature_vbo_,
                                    creature_ebo_);
  glBindBuffer(GL_ARRAY_BUFFER, creature_instance_vbo_);
  glBufferData(GL_ARRAY_BUFFER, kInitialCreatureInstanceBufferBytes, nullptr,
               GL_STREAM_DRAW);
  configure_creature_instance_attributes(creature_vao_, creature_instance_vbo_);

  glGenVertexArrays(1, &viewmodel_vao_);
  glGenBuffers(1, &viewmodel_instance_vbo_);
  configure_box_template_attributes(viewmodel_vao_, creature_vbo_,
                                    creature_ebo_);
  glBindBuffer(GL_ARRAY_BUFFER, viewmodel_instance_vbo_);
  glBufferData(GL_ARRAY_BUFFER, kInitialCreatureInstanceBufferBytes, nullptr,
               GL_STREAM_DRAW);
  configure_creature_instance_attributes(viewmodel_vao_,
                                         viewmodel_instance_vbo_);

  creature_instance_buffer_bytes_ = kInitialCreatureInstanceBufferBytes;
  viewmodel_instance_buffer_bytes_ = kInitialCreatureInstanceBufferBytes;
}

void Renderer::create_item_drop_geometry() {
  glGenBuffers(1, &item_drop_vbo_);
  glGenBuffers(1, &item_drop_ebo_);
  glBindBuffer(GL_ARRAY_BUFFER, item_drop_vbo_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, item_drop_ebo_);
  if (item_drop_uses_rounded_template(options_.visual_pipeline)) {
    const auto template_mesh =
        build_stylized_rounded_box(StylizedPrimitiveLod::Low);
    item_drop_template_vertex_buffer_bytes_ = static_cast<GLsizeiptr>(
        template_mesh.vertices.size() * sizeof(StylizedPrimitiveVertex));
    item_drop_template_index_buffer_bytes_ = static_cast<GLsizeiptr>(
        template_mesh.indices.size() * sizeof(std::uint32_t));
    item_drop_template_index_count_ =
        static_cast<GLsizei>(template_mesh.indices.size());
    glBufferData(GL_ARRAY_BUFFER, item_drop_template_vertex_buffer_bytes_,
                 template_mesh.vertices.data(), GL_STATIC_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 item_drop_template_index_buffer_bytes_,
                 template_mesh.indices.data(), GL_STATIC_DRAW);
  } else {
    const auto &template_vertices = box_template_vertices();
    const auto &template_indices = box_template_indices();
    item_drop_template_vertex_buffer_bytes_ = static_cast<GLsizeiptr>(
        template_vertices.size() * sizeof(BoxTemplateVertex));
    item_drop_template_index_buffer_bytes_ = static_cast<GLsizeiptr>(
        template_indices.size() * sizeof(std::uint32_t));
    item_drop_template_index_count_ =
        static_cast<GLsizei>(template_indices.size());
    glBufferData(GL_ARRAY_BUFFER, item_drop_template_vertex_buffer_bytes_,
                 template_vertices.data(), GL_STATIC_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 item_drop_template_index_buffer_bytes_,
                 template_indices.data(), GL_STATIC_DRAW);
  }

  glGenVertexArrays(1, &item_drop_vao_);
  glGenBuffers(1, &item_drop_instance_vbo_);
  configure_box_template_attributes(item_drop_vao_, item_drop_vbo_,
                                    item_drop_ebo_);
  glBindBuffer(GL_ARRAY_BUFFER, item_drop_instance_vbo_);
  glBufferData(GL_ARRAY_BUFFER, kInitialItemDropInstanceBufferBytes, nullptr,
               GL_STREAM_DRAW);
  configure_item_drop_instance_attributes(item_drop_vao_,
                                          item_drop_instance_vbo_);

  item_drop_instance_buffer_bytes_ = kInitialItemDropInstanceBufferBytes;
}

void Renderer::create_precipitation_geometry() {
  constexpr std::array<float, 8> kQuadVertices{{
      -0.5F,
      0.0F,
      0.5F,
      0.0F,
      -0.5F,
      1.0F,
      0.5F,
      1.0F,
  }};

  glGenVertexArrays(1, &precipitation_vao_);
  glGenBuffers(1, &precipitation_vbo_);
  glGenBuffers(1, &precipitation_instance_vbo_);

  glBindVertexArray(precipitation_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, precipitation_vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(kQuadVertices.size() * sizeof(float)),
               kQuadVertices.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                        static_cast<GLsizei>(sizeof(float) * 2U), nullptr);

  glBindBuffer(GL_ARRAY_BUFFER, precipitation_instance_vbo_);
  glBufferData(GL_ARRAY_BUFFER, kInitialPrecipitationInstanceBufferBytes,
               nullptr, GL_STREAM_DRAW);

  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                        static_cast<GLsizei>(sizeof(PrecipitationGpuInstance)),
                        reinterpret_cast<void *>(offsetof(
                            PrecipitationGpuInstance, position_length)));
  glVertexAttribDivisor(1, 1);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE,
                        static_cast<GLsizei>(sizeof(PrecipitationGpuInstance)),
                        reinterpret_cast<void *>(offsetof(
                            PrecipitationGpuInstance, velocity_width)));
  glVertexAttribDivisor(2, 1);
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(
      3, 4, GL_FLOAT, GL_FALSE,
      static_cast<GLsizei>(sizeof(PrecipitationGpuInstance)),
      reinterpret_cast<void *>(offsetof(PrecipitationGpuInstance, appearance)));
  glVertexAttribDivisor(3, 1);

  precipitation_instance_buffer_bytes_ =
      kInitialPrecipitationInstanceBufferBytes;
}

void Renderer::create_old_guard_effect_geometry() {
  constexpr std::array<float, 8> kQuadVertices{{
      -0.5F,
      -0.5F,
      0.5F,
      -0.5F,
      -0.5F,
      0.5F,
      0.5F,
      0.5F,
  }};

  glGenVertexArrays(1, &old_guard_effect_vao_);
  glGenBuffers(1, &old_guard_effect_vbo_);
  glGenBuffers(1, &old_guard_effect_instance_vbo_);

  glBindVertexArray(old_guard_effect_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, old_guard_effect_vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(kQuadVertices.size() * sizeof(float)),
               kQuadVertices.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                        static_cast<GLsizei>(sizeof(float) * 2U), nullptr);

  glBindBuffer(GL_ARRAY_BUFFER, old_guard_effect_instance_vbo_);
  glBufferData(GL_ARRAY_BUFFER, kInitialOldGuardEffectInstanceBufferBytes,
               nullptr, GL_STREAM_DRAW);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                        static_cast<GLsizei>(sizeof(OldGuardEffectGpuInstance)),
                        reinterpret_cast<void *>(offsetof(
                            OldGuardEffectGpuInstance, position_size)));
  glVertexAttribDivisor(1, 1);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE,
                        static_cast<GLsizei>(sizeof(OldGuardEffectGpuInstance)),
                        reinterpret_cast<void *>(
                            offsetof(OldGuardEffectGpuInstance, appearance)));
  glVertexAttribDivisor(2, 1);

  old_guard_effect_instance_buffer_bytes_ =
      kInitialOldGuardEffectInstanceBufferBytes;
}

void Renderer::create_sea_horizon_geometry() {
  glGenVertexArrays(1, &sea_horizon_terrain_vao_);
  glGenBuffers(1, &sea_horizon_terrain_vbo_);
  glGenBuffers(1, &sea_horizon_terrain_ebo_);
  glBindVertexArray(sea_horizon_terrain_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, sea_horizon_terrain_vbo_);
  sea_horizon_terrain_vertex_buffer_bytes_ = static_cast<GLsizeiptr>(
      kSeaHorizonMaxTerrainVertices * sizeof(SeaHorizonTerrainVertex));
  glBufferData(GL_ARRAY_BUFFER, sea_horizon_terrain_vertex_buffer_bytes_,
               nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sea_horizon_terrain_ebo_);
  sea_horizon_terrain_index_buffer_bytes_ = static_cast<GLsizeiptr>(
      kSeaHorizonMaxTerrainTriangles * 3U * sizeof(std::uint32_t));
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sea_horizon_terrain_index_buffer_bytes_,
               nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(
      0, 3, GL_FLOAT, GL_FALSE, sizeof(SeaHorizonTerrainVertex),
      reinterpret_cast<void *>(offsetof(SeaHorizonTerrainVertex, x)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(
      1, 3, GL_BYTE, GL_TRUE, sizeof(SeaHorizonTerrainVertex),
      reinterpret_cast<void *>(offsetof(SeaHorizonTerrainVertex, nx)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(
      2, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(SeaHorizonTerrainVertex),
      reinterpret_cast<void *>(offsetof(SeaHorizonTerrainVertex, block_id)));

  glBindVertexArray(0);
}

void Renderer::destroy_sea_horizon_geometry() {
  if (sea_horizon_terrain_ebo_ != 0U) {
    glDeleteBuffers(1, &sea_horizon_terrain_ebo_);
  }
  if (sea_horizon_terrain_vbo_ != 0U) {
    glDeleteBuffers(1, &sea_horizon_terrain_vbo_);
  }
  if (sea_horizon_terrain_vao_ != 0U) {
    glDeleteVertexArrays(1, &sea_horizon_terrain_vao_);
  }
  sea_horizon_terrain_vao_ = 0U;
  sea_horizon_terrain_vbo_ = 0U;
  sea_horizon_terrain_ebo_ = 0U;
  sea_horizon_terrain_vertex_buffer_bytes_ = 0;
  sea_horizon_terrain_index_buffer_bytes_ = 0;
  sea_horizon_terrain_index_count_ = 0;
  sea_horizon_terrain_transition_index_count_ = 0;
  sea_horizon_terrain_center_ = {};
  sea_horizon_world_seed_ = 0;
  sea_horizon_generation_version_ = WorldGenerationVersion::LegacyV1;
  sea_horizon_terrain_cache_valid_ = false;
  sea_horizon_terrain_mesh_cache_ = {};
  sea_horizon_detail_transition_range_ = {};
  sea_horizon_detailed_chunks_cache_.clear();
  sea_horizon_detailed_chunks_scratch_.clear();
  sea_horizon_filtered_indices_scratch_.clear();
  sea_horizon_transition_indices_scratch_.clear();
}

void Renderer::sync_sea_horizon_terrain(const World &world,
                                        const glm::vec3 &focus,
                                        float detailed_draw_distance) {
  if (world.generation_profile() != WorldGenerationProfile::OceanAdventure ||
      sea_horizon_terrain_vao_ == 0U) {
    sea_horizon_terrain_index_count_ = 0;
    sea_horizon_terrain_transition_index_count_ = 0;
    sea_horizon_terrain_cache_valid_ = false;
    sea_horizon_terrain_mesh_cache_ = {};
    sea_horizon_detail_transition_range_ = {};
    sea_horizon_detailed_chunks_cache_.clear();
    sea_horizon_detailed_chunks_scratch_.clear();
    sea_horizon_filtered_indices_scratch_.clear();
    sea_horizon_transition_indices_scratch_.clear();
    return;
  }

  sea_horizon_detail_transition_range_ =
      sea_horizon_detail_transition_range(detailed_draw_distance);
  const auto same_world =
      sea_horizon_terrain_cache_valid_ &&
      world.seed() == sea_horizon_world_seed_ &&
      world.generation_version() == sea_horizon_generation_version_;
  const auto center =
      same_world ? sea_horizon_stable_center(sea_horizon_terrain_center_, focus)
                 : sea_horizon_snapped_center(focus);
  const auto rebuild_mesh =
      !same_world || center != sea_horizon_terrain_center_;

  // Je sépare les chunks détaillés dans une passe de transition. Le relief
  // grossier se dissout alors progressivement derrière leur vraie géométrie.
  auto &detailed_chunks = sea_horizon_detailed_chunks_scratch_;
  detailed_chunks.clear();
  detailed_chunks.reserve(gpu_meshes_.size());
  const auto safe_draw_distance = std::isfinite(detailed_draw_distance)
                                      ? std::max(detailed_draw_distance, 0.0F)
                                      : 0.0F;
  const auto draw_distance_squared = static_cast<double>(safe_draw_distance) *
                                     static_cast<double>(safe_draw_distance);
  const glm::vec2 safe_focus{
      std::isfinite(focus.x) ? focus.x : 0.0F,
      std::isfinite(focus.z) ? focus.z : 0.0F,
  };
  for (const auto &[coord, gpu_mesh] : gpu_meshes_) {
    // Je ne retire le proxy qu'après publication du vrai relief organique.
    // Une eau ou une architecture déjà prête ne doit jamais faire
    // disparaître prématurément l'île ou le fond qui se trouve derrière.
    if (gpu_mesh.terrain_index_count <= 0) {
      continue;
    }
    const auto delta_x =
        static_cast<double>(gpu_mesh.bounds.center.x - safe_focus.x);
    const auto delta_z =
        static_cast<double>(gpu_mesh.bounds.center.z - safe_focus.y);
    const auto distance_squared = delta_x * delta_x + delta_z * delta_z;
    if (std::isfinite(distance_squared) &&
        distance_squared <= draw_distance_squared) {
      detailed_chunks.push_back(coord);
    }
  }
  std::sort(detailed_chunks.begin(), detailed_chunks.end(),
            [](const ChunkCoord &left, const ChunkCoord &right) noexcept {
              return left.x < right.x ||
                     (left.x == right.x && left.z < right.z);
            });
  const auto detailed_coverage_changed =
      detailed_chunks != sea_horizon_detailed_chunks_cache_;
  if (!rebuild_mesh && !detailed_coverage_changed) {
    return;
  }

  auto vertices_changed = false;
  if (rebuild_mesh) {
    const glm::vec3 mesh_focus{
        static_cast<float>(center.x),
        std::isfinite(focus.y) ? focus.y : 0.0F,
        static_cast<float>(center.z),
    };
    sea_horizon_terrain_mesh_cache_ =
        build_sea_horizon_terrain_mesh(world, mesh_focus);
    vertices_changed = true;
  }
  const auto &mesh = sea_horizon_terrain_mesh_cache_;
  const auto vertices_valid =
      mesh.vertices.size() <= kSeaHorizonMaxTerrainVertices;
  const auto indices_valid =
      mesh.indices.size() <= kSeaHorizonMaxTerrainTriangles * 3U &&
      mesh.indices.size() % 3U == 0U &&
      std::all_of(mesh.indices.begin(), mesh.indices.end(),
                  [&mesh](std::uint32_t index) noexcept {
                    return index < mesh.vertices.size();
                  });

  sea_horizon_terrain_center_ = center;
  sea_horizon_world_seed_ = world.seed();
  sea_horizon_generation_version_ = world.generation_version();
  sea_horizon_terrain_cache_valid_ = true;
  sea_horizon_terrain_index_count_ = 0;
  sea_horizon_terrain_transition_index_count_ = 0;
  sea_horizon_detailed_chunks_cache_ = detailed_chunks;

  if (!vertices_valid || !indices_valid || mesh.empty()) {
    return;
  }

  filter_sea_horizon_terrain_indices(mesh, sea_horizon_detailed_chunks_cache_,
                                     sea_horizon_filtered_indices_scratch_,
                                     sea_horizon_transition_indices_scratch_);
  const auto filtered_index_count =
      sea_horizon_filtered_indices_scratch_.size() +
      sea_horizon_transition_indices_scratch_.size();
  if (filtered_index_count > kSeaHorizonMaxTerrainTriangles * 3U ||
      sea_horizon_filtered_indices_scratch_.size() % 3U != 0U ||
      sea_horizon_transition_indices_scratch_.size() % 3U != 0U) {
    sea_horizon_filtered_indices_scratch_.clear();
    sea_horizon_transition_indices_scratch_.clear();
    return;
  }

  const auto vertex_bytes = static_cast<GLsizeiptr>(
      mesh.vertices.size() * sizeof(SeaHorizonTerrainVertex));
  const auto index_bytes = static_cast<GLsizeiptr>(
      sea_horizon_filtered_indices_scratch_.size() * sizeof(std::uint32_t));
  const auto transition_index_bytes = static_cast<GLsizeiptr>(
      sea_horizon_transition_indices_scratch_.size() * sizeof(std::uint32_t));
  glBindVertexArray(sea_horizon_terrain_vao_);
  if (vertices_changed) {
    glBindBuffer(GL_ARRAY_BUFFER, sea_horizon_terrain_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, mesh.vertices.data());
    frame_uploaded_bytes_ += static_cast<std::uint64_t>(vertex_bytes);
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sea_horizon_terrain_ebo_);
  if (index_bytes > 0) {
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_bytes,
                    sea_horizon_filtered_indices_scratch_.data());
  }
  if (transition_index_bytes > 0) {
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, index_bytes,
                    transition_index_bytes,
                    sea_horizon_transition_indices_scratch_.data());
  }
  sea_horizon_terrain_index_count_ =
      static_cast<GLsizei>(sea_horizon_filtered_indices_scratch_.size());
  sea_horizon_terrain_transition_index_count_ =
      static_cast<GLsizei>(sea_horizon_transition_indices_scratch_.size());
  frame_uploaded_bytes_ +=
      static_cast<std::uint64_t>(index_bytes + transition_index_bytes);
}

void Renderer::upload_sea_horizon_uniforms(
    const glm::mat4 &view_projection, const glm::vec3 &camera_position,
    const EnvironmentState &environment, const SeaHorizonFogRange &fog_range) {
  glUseProgram(sea_horizon_program_);
  glUniformMatrix4fv(sea_horizon_uniforms_.view_projection, 1, GL_FALSE,
                     glm::value_ptr(view_projection));
  glUniform3fv(sea_horizon_uniforms_.camera_position, 1,
               glm::value_ptr(camera_position));
  glUniform3fv(sea_horizon_uniforms_.sun_direction, 1,
               glm::value_ptr(environment.sun_direction));
  glUniform3fv(sea_horizon_uniforms_.sun_color, 1,
               glm::value_ptr(environment.sun_color));
  glUniform3fv(sea_horizon_uniforms_.ambient_color, 1,
               glm::value_ptr(environment.ambient_color));
  glUniform3fv(sea_horizon_uniforms_.fog_color, 1,
               glm::value_ptr(environment.fog_color));
  glUniform3fv(sea_horizon_uniforms_.distant_fog_color, 1,
               glm::value_ptr(environment.distant_fog_color));
  glUniform1f(sea_horizon_uniforms_.daylight_factor,
              environment.daylight_factor);
  glUniform1f(sea_horizon_uniforms_.precipitation_intensity,
              environment.precipitation_intensity);
  glUniform1f(sea_horizon_uniforms_.storm_intensity,
              environment.storm_intensity);
  glUniform1f(sea_horizon_uniforms_.lightning_intensity,
              environment.lightning_intensity);
  glUniform2f(sea_horizon_uniforms_.far_fog_range, fog_range.start_distance,
              fog_range.end_distance);
  glUniform1f(sea_horizon_uniforms_.sea_level,
              static_cast<float>(kSeaLevel + 1));
  glUniform2f(sea_horizon_uniforms_.detail_transition_range,
              sea_horizon_detail_transition_range_.start_distance,
              sea_horizon_detail_transition_range_.end_distance);
}

void Renderer::draw_sea_horizon_terrain(const glm::mat4 &view_projection,
                                        const glm::vec3 &camera_position,
                                        const EnvironmentState &environment,
                                        const SeaHorizonFogRange &fog_range) {
  if (sea_horizon_program_ == 0U || sea_horizon_terrain_vao_ == 0U ||
      (sea_horizon_terrain_index_count_ <= 0 &&
       sea_horizon_terrain_transition_index_count_ <= 0)) {
    return;
  }

  upload_sea_horizon_uniforms(view_projection, camera_position, environment,
                              fog_range);

  // Je garde le proxy legerement derriere le relief reel. Les chunks
  // detailles le remplacent donc par le depth test, sans fondu CPU ni tri.
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(1.0F, 1.0F);
  // Je rends les deux faces du relief très lointain : depuis le niveau de
  // la mer, le bord supérieur d'une île haute peut être observé par dessous.
  // Sans cette garde, son sommet disparaîtrait avant d'entrer dans les chunks.
  glDisable(GL_CULL_FACE);
  glBindVertexArray(sea_horizon_terrain_vao_);
  if (sea_horizon_terrain_index_count_ > 0) {
    glUniform1i(sea_horizon_uniforms_.transition_pass, 0);
    glDrawElements(GL_TRIANGLES, sea_horizon_terrain_index_count_,
                   GL_UNSIGNED_INT, nullptr);
    record_triangle_draw(sea_horizon_terrain_index_count_);
  }
  if (sea_horizon_terrain_transition_index_count_ > 0) {
    glUniform1i(sea_horizon_uniforms_.transition_pass, 1);
    const auto transition_offset =
        static_cast<std::uintptr_t>(sea_horizon_terrain_index_count_) *
        sizeof(std::uint32_t);
    glDrawElements(GL_TRIANGLES, sea_horizon_terrain_transition_index_count_,
                   GL_UNSIGNED_INT,
                   reinterpret_cast<const void *>(transition_offset));
    record_triangle_draw(sea_horizon_terrain_transition_index_count_);
    glUniform1i(sea_horizon_uniforms_.transition_pass, 0);
  }
  glDisable(GL_POLYGON_OFFSET_FILL);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
}

void Renderer::sync_marine_visuals(const World &world,
                                   std::span<const ChunkCoord> visible_chunks,
                                   const glm::vec3 &camera_position,
                                   const ShipRenderState &ship,
                                   RendererQuality quality,
                                   float absolute_time_seconds) {
  const auto marine_enabled =
      options_.visual_pipeline == VisualPipeline::ModernStylized &&
      world.generation_profile() == WorldGenerationProfile::OceanAdventure &&
      modern_material_albedo_texture_ != 0U;
  if (!marine_enabled || !std::isfinite(camera_position.x) ||
      !std::isfinite(camera_position.y) || !std::isfinite(camera_position.z) ||
      !std::isfinite(absolute_time_seconds)) {
    marine_decor_gpu_mesh_.terrain_index_count = 0;
    ocean_life_gpu_mesh_.terrain_index_count = 0;
    ocean_life_field_.clear();
    marine_visual_cache_valid_ = false;
    return;
  }

  const auto same_world =
      marine_cache_world_seed_ == world.seed() &&
      marine_cache_generation_version_ == world.generation_version();
  if (!same_world) {
    marine_decor_cache_.clear();
    marine_visual_cache_valid_ = false;
    marine_cache_world_seed_ = world.seed();
    marine_cache_generation_version_ = world.generation_version();
  }

  marine_visible_chunks_scratch_.assign(visible_chunks.begin(),
                                        visible_chunks.end());
  std::sort(marine_visible_chunks_scratch_.begin(),
            marine_visible_chunks_scratch_.end(),
            [](const ChunkCoord &left, const ChunkCoord &right) noexcept {
              return left.x < right.x ||
                     (left.x == right.x && left.z < right.z);
            });
  marine_visible_chunks_scratch_.erase(
      std::unique(marine_visible_chunks_scratch_.begin(),
                  marine_visible_chunks_scratch_.end()),
      marine_visible_chunks_scratch_.end());

  auto visible_signature = std::uint64_t{14695981039346656037ULL};
  for (const auto &coord : marine_visible_chunks_scratch_) {
    const auto packed =
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.x)) |
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.z))
         << 32U);
    visible_signature ^= packed;
    visible_signature *= 1099511628211ULL;
  }
  visible_signature ^=
      static_cast<std::uint64_t>(marine_visible_chunks_scratch_.size());

  constexpr float kMarineFocusCellSize = 4.0F;
  const auto focus_cell_x =
      static_cast<int>(std::floor(camera_position.x / kMarineFocusCellSize));
  const auto focus_cell_z =
      static_cast<int>(std::floor(camera_position.z / kMarineFocusCellSize));
  const auto rebuild_decor = !marine_visual_cache_valid_ ||
                             visible_signature != marine_visible_signature_ ||
                             focus_cell_x != marine_focus_cell_x_ ||
                             focus_cell_z != marine_focus_cell_z_ ||
                             quality != marine_cache_quality_;

  const auto sample_surface = [&world](int world_x, int world_z) {
    return world.sample_generated_surface(world_x, world_z);
  };

  if (rebuild_decor) {
    marine_decor_instances_scratch_.clear();
    for (const auto &coord : marine_visible_chunks_scratch_) {
      auto iterator = marine_decor_cache_.find(coord);
      if (iterator == marine_decor_cache_.end()) {
        iterator =
            marine_decor_cache_
                .emplace(coord,
                         build_marine_decor(coord, world.generation_version(),
                                            world.seed(), sample_surface))
                .first;
      }
      marine_decor_instances_scratch_.insert(
          marine_decor_instances_scratch_.end(), iterator->second.begin(),
          iterator->second.end());
    }

    marine_decor_mesh_scratch_ = build_marine_decor_visual_mesh(
        marine_decor_instances_scratch_, camera_position,
        marine_visual_budget_for_quality(quality));
    upload_terrain_mesh_data(marine_decor_gpu_mesh_,
                             marine_decor_mesh_scratch_);

    marine_visible_signature_ = visible_signature;
    marine_focus_cell_x_ = focus_cell_x;
    marine_focus_cell_z_ = focus_cell_z;
    marine_cache_quality_ = quality;
    marine_visual_cache_valid_ = true;

    // Je borne le cache d'exploration et je ne conserve au-delà de ce
    // seuil que les chunks encore visibles autour du joueur.
    if (marine_decor_cache_.size() > 384U) {
      for (auto iterator = marine_decor_cache_.begin();
           iterator != marine_decor_cache_.end();) {
        const auto visible =
            std::find(marine_visible_chunks_scratch_.begin(),
                      marine_visible_chunks_scratch_.end(),
                      iterator->first) != marine_visible_chunks_scratch_.end();
        if (!visible) {
          iterator = marine_decor_cache_.erase(iterator);
        } else {
          ++iterator;
        }
      }
    }
  }

  const auto &life_frame = ocean_life_field_.sample(
      world.generation_profile(), static_cast<std::uint32_t>(world.seed()),
      camera_position, absolute_time_seconds,
      ocean_life_budget_for_quality(quality),
      make_ocean_life_surface_sampler(sample_surface));
  if (ship_protection_is_renderable(ship)) {
    ocean_life_mesh_scratch_ = build_ocean_life_visual_mesh(
        life_frame.instances, glm::inverse(ship.model_matrix),
        ship.blueprint->protection_profile);
  } else {
    ocean_life_mesh_scratch_ =
        build_ocean_life_visual_mesh(life_frame.instances);
  }
  upload_terrain_mesh_data(ocean_life_gpu_mesh_, ocean_life_mesh_scratch_);
}

void Renderer::create_screen_quad_geometry() {
  glGenVertexArrays(1, &screen_quad_vao_);
}

void Renderer::create_hud_geometry() {
  glGenVertexArrays(1, &hud_vao_);
  glGenBuffers(1, &hud_vbo_);
  glBindVertexArray(hud_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
  glBufferData(GL_ARRAY_BUFFER, kInitialHudBufferBytes, nullptr,
               GL_STREAM_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(HudVertex),
                        reinterpret_cast<void *>(offsetof(HudVertex, x)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(HudVertex),
                        reinterpret_cast<void *>(offsetof(HudVertex, u)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(HudVertex),
                        reinterpret_cast<void *>(offsetof(HudVertex, r)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(
      3, 1, GL_FLOAT, GL_FALSE, sizeof(HudVertex),
      reinterpret_cast<void *>(offsetof(HudVertex, textured)));
  hud_vertex_buffer_bytes_ = kInitialHudBufferBytes;
}

void Renderer::ensure_hud_buffer_capacity(std::size_t vertex_count) {
  const auto required_bytes =
      static_cast<GLsizeiptr>(vertex_count * sizeof(HudVertex));
  if (hud_vertex_buffer_bytes_ >= required_bytes) {
    return;
  }

  hud_vertex_buffer_bytes_ = grow_buffer_capacity(
      hud_vertex_buffer_bytes_, required_bytes, kInitialHudBufferBytes);
}

void Renderer::upload_hud_vertices(std::span<const HudVertex> vertices) {
  glBindVertexArray(hud_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
  ensure_hud_buffer_capacity(vertices.size());
  orphan_bound_buffer(GL_ARRAY_BUFFER, hud_vertex_buffer_bytes_);
  const auto upload_bytes =
      static_cast<GLsizeiptr>(vertices.size() * sizeof(HudVertex));
  glBufferSubData(GL_ARRAY_BUFFER, 0, upload_bytes, vertices.data());
  frame_uploaded_bytes_ +=
      static_cast<std::uint64_t>(std::max<GLsizeiptr>(upload_bytes, 0));
  record_triangle_draw(static_cast<GLsizei>(vertices.size()));
}

void Renderer::create_crosshair_geometry() {
  static constexpr std::array<float, 8> kCrosshairVertices{{
      -0.015F,
      0.0F,
      0.015F,
      0.0F,
      0.0F,
      -0.02F,
      0.0F,
      0.02F,
  }};

  glGenVertexArrays(1, &crosshair_vao_);
  glGenBuffers(1, &crosshair_vbo_);
  glBindVertexArray(crosshair_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, crosshair_vbo_);
  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(kCrosshairVertices.size() * sizeof(float)),
      kCrosshairVertices.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
}

void Renderer::ensure_water_scene_targets(int width, int height) {
  const auto target_width = std::max(width, 1);
  const auto target_height = std::max(height, 1);
  const auto quality_settings = active_quality_settings_;
  const auto color_format = color_target_format(quality_settings);
  const auto targets_match =
      water_scene_framebuffer_ != 0 && water_scene_color_texture_ != 0 &&
      water_scene_depth_texture_ != 0 &&
      water_scene_target_width_ == target_width &&
      water_scene_target_height_ == target_height &&
      water_scene_color_internal_format_ == color_format.internal_format;
  if (targets_match) {
    return;
  }

  destroy_water_scene_targets();

  glGenFramebuffers(1, &water_scene_framebuffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, water_scene_framebuffer_);

  glGenTextures(1, &water_scene_color_texture_);
  glBindTexture(GL_TEXTURE_2D, water_scene_color_texture_);
  glTexImage2D(GL_TEXTURE_2D, 0, color_format.internal_format, target_width,
               target_height, 0, color_format.pixel_format,
               color_format.pixel_type, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         water_scene_color_texture_, 0);

  glGenTextures(1, &water_scene_depth_texture_);
  glBindTexture(GL_TEXTURE_2D, water_scene_depth_texture_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, target_width,
               target_height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         water_scene_depth_texture_, 0);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    throw std::runtime_error("Water scene framebuffer is incomplete");
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  water_scene_target_width_ = target_width;
  water_scene_target_height_ = target_height;
  water_scene_color_internal_format_ = color_format.internal_format;
}

void Renderer::destroy_water_scene_targets() {
  if (water_scene_depth_texture_ != 0) {
    glDeleteTextures(1, &water_scene_depth_texture_);
    water_scene_depth_texture_ = 0;
  }
  if (water_scene_color_texture_ != 0) {
    glDeleteTextures(1, &water_scene_color_texture_);
    water_scene_color_texture_ = 0;
  }
  if (water_scene_framebuffer_ != 0) {
    glDeleteFramebuffers(1, &water_scene_framebuffer_);
    water_scene_framebuffer_ = 0;
  }
  water_scene_target_width_ = 0;
  water_scene_target_height_ = 0;
  water_scene_color_internal_format_ = 0;
}

void Renderer::ensure_post_process_targets(int width, int height,
                                           bool require_glow_targets) {
  const auto target_width = std::max(width, 1);
  const auto target_height = std::max(height, 1);
  const auto quality_settings = active_quality_settings_;
  const auto color_format = color_target_format(quality_settings);

  const auto scene_matches =
      scene_framebuffer_ != 0 && scene_color_texture_ != 0 &&
      scene_depth_texture_ != 0 && scene_target_width_ == target_width &&
      scene_target_height_ == target_height &&
      scene_color_internal_format_ == color_format.internal_format;
  if (!scene_matches) {
    destroy_scene_targets();

    glGenFramebuffers(1, &scene_framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, scene_framebuffer_);

    glGenTextures(1, &scene_color_texture_);
    glBindTexture(GL_TEXTURE_2D, scene_color_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, color_format.internal_format, target_width,
                 target_height, 0, color_format.pixel_format,
                 color_format.pixel_type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           scene_color_texture_, 0);

    glGenTextures(1, &scene_depth_texture_);
    glBindTexture(GL_TEXTURE_2D, scene_depth_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, target_width,
                 target_height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           scene_depth_texture_, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      throw std::runtime_error("Scene framebuffer is incomplete");
    }
    scene_target_width_ = target_width;
    scene_target_height_ = target_height;
    scene_color_internal_format_ = color_format.internal_format;
  }

  if (require_glow_targets) {
    const auto glow_divisor = std::max(quality_settings.glow_downsample, 1);
    const auto glow_width = std::max(target_width / glow_divisor, 1);
    const auto glow_height = std::max(target_height / glow_divisor, 1);
    const auto glow_matches =
        glow_extract_framebuffer_ != 0 && glow_extract_texture_ != 0 &&
        glow_ping_framebuffer_ != 0 && glow_ping_texture_ != 0 &&
        glow_target_width_ == glow_width &&
        glow_target_height_ == glow_height &&
        glow_color_internal_format_ == color_format.internal_format;
    if (!glow_matches) {
      destroy_glow_targets();

      glGenFramebuffers(1, &glow_extract_framebuffer_);
      glBindFramebuffer(GL_FRAMEBUFFER, glow_extract_framebuffer_);
      glGenTextures(1, &glow_extract_texture_);
      glBindTexture(GL_TEXTURE_2D, glow_extract_texture_);
      glTexImage2D(GL_TEXTURE_2D, 0, color_format.internal_format, glow_width,
                   glow_height, 0, color_format.pixel_format,
                   color_format.pixel_type, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, glow_extract_texture_, 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Glow extract framebuffer is incomplete");
      }

      glGenFramebuffers(1, &glow_ping_framebuffer_);
      glBindFramebuffer(GL_FRAMEBUFFER, glow_ping_framebuffer_);
      glGenTextures(1, &glow_ping_texture_);
      glBindTexture(GL_TEXTURE_2D, glow_ping_texture_);
      glTexImage2D(GL_TEXTURE_2D, 0, color_format.internal_format, glow_width,
                   glow_height, 0, color_format.pixel_format,
                   color_format.pixel_type, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, glow_ping_texture_, 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Glow blur framebuffer is incomplete");
      }

      glow_target_width_ = glow_width;
      glow_target_height_ = glow_height;
      glow_color_internal_format_ = color_format.internal_format;
    }
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::destroy_post_process_targets() {
  destroy_scene_targets();
  destroy_glow_targets();
}

void Renderer::destroy_scene_targets() {
  if (scene_depth_texture_ != 0) {
    glDeleteTextures(1, &scene_depth_texture_);
    scene_depth_texture_ = 0;
  }
  if (scene_color_texture_ != 0) {
    glDeleteTextures(1, &scene_color_texture_);
    scene_color_texture_ = 0;
  }
  if (scene_framebuffer_ != 0) {
    glDeleteFramebuffers(1, &scene_framebuffer_);
    scene_framebuffer_ = 0;
  }
  scene_target_width_ = 0;
  scene_target_height_ = 0;
  scene_color_internal_format_ = 0;
}

void Renderer::destroy_glow_targets() {
  if (glow_extract_texture_ != 0) {
    glDeleteTextures(1, &glow_extract_texture_);
    glow_extract_texture_ = 0;
  }
  if (glow_extract_framebuffer_ != 0) {
    glDeleteFramebuffers(1, &glow_extract_framebuffer_);
    glow_extract_framebuffer_ = 0;
  }
  if (glow_ping_texture_ != 0) {
    glDeleteTextures(1, &glow_ping_texture_);
    glow_ping_texture_ = 0;
  }
  if (glow_ping_framebuffer_ != 0) {
    glDeleteFramebuffers(1, &glow_ping_framebuffer_);
    glow_ping_framebuffer_ = 0;
  }
  glow_target_width_ = 0;
  glow_target_height_ = 0;
  glow_color_internal_format_ = 0;
}

void Renderer::draw_sky(const glm::mat4 &inverse_view_projection,
                        const glm::vec3 &camera_position,
                        const EnvironmentState &environment,
                        const RendererQualitySettings &quality_settings,
                        bool maritime_horizon_enabled,
                        const MaritimeSubmersionState &maritime_submersion,
                        const OceanState &ocean,
                        std::span<const glm::vec4> ocean_wave_uniforms,
                        std::span<const glm::vec2> ocean_phase_uniforms) {
  if (sky_program_ == 0 || screen_quad_vao_ == 0 || accent_texture_ == 0) {
    return;
  }

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glUseProgram(sky_program_);
  glUniformMatrix4fv(sky_uniforms_.inverse_view_projection, 1, GL_FALSE,
                     glm::value_ptr(inverse_view_projection));
  glUniform3fv(sky_uniforms_.sun_direction, 1,
               glm::value_ptr(environment.sun_direction));
  glUniform1f(sky_uniforms_.daylight_factor, environment.daylight_factor);
  glUniform1f(sky_uniforms_.time_of_day, environment.time_of_day);
  glUniform3fv(sky_uniforms_.sky_zenith_color, 1,
               glm::value_ptr(environment.sky_zenith_color));
  glUniform3fv(sky_uniforms_.sky_horizon_color, 1,
               glm::value_ptr(environment.sky_horizon_color));
  glUniform3fv(sky_uniforms_.horizon_glow_color, 1,
               glm::value_ptr(environment.horizon_glow_color));
  glUniform3fv(sky_uniforms_.sun_disk_color, 1,
               glm::value_ptr(environment.sun_disk_color));
  glUniform3fv(sky_uniforms_.moon_disk_color, 1,
               glm::value_ptr(environment.moon_disk_color));
  glUniform1f(sky_uniforms_.star_intensity, environment.star_intensity);
  glUniform1f(sky_uniforms_.cloud_intensity, environment.cloud_intensity);
  glUniform1f(sky_uniforms_.overcast_intensity, environment.overcast_intensity);
  glUniform1f(sky_uniforms_.precipitation_intensity,
              environment.precipitation_intensity);
  glUniform1f(sky_uniforms_.storm_intensity, environment.storm_intensity);
  glUniform1f(sky_uniforms_.violent_storm_intensity,
              environment.violent_storm_intensity);
  glUniform1f(sky_uniforms_.lightning_intensity,
              environment.lightning_intensity);
  glUniform1f(sky_uniforms_.lightning_bolt_intensity,
              environment.lightning_bolt_intensity);
  glUniform3fv(sky_uniforms_.lightning_direction, 1,
               glm::value_ptr(environment.lightning_direction));
  glUniform1f(sky_uniforms_.lightning_shape_seed,
              environment.lightning_shape_seed);
  glUniform1f(sky_uniforms_.weather_time, environment.weather_time_seconds);
  glUniform1i(sky_uniforms_.cloud_steps, quality_settings.cloud_steps);
  glUniform1f(sky_uniforms_.cloud_detail, quality_settings.cloud_detail);
  glUniform1i(sky_uniforms_.accent_atlas, 0);
  glUniform1i(sky_uniforms_.maritime_horizon_enabled,
              maritime_horizon_enabled ? 1 : 0);
  glUniform3fv(sky_uniforms_.maritime_camera_position, 1,
               glm::value_ptr(camera_position));
  glUniform1f(sky_uniforms_.maritime_sea_level,
              static_cast<float>(kSeaLevel + 1));
  glUniform1i(sky_uniforms_.maritime_submersion_active,
              maritime_submersion.active ? 1 : 0);
  glUniform4fv(sky_uniforms_.ocean_horizon_waves, 2,
               glm::value_ptr(ocean_wave_uniforms.front()));
  glUniform2fv(sky_uniforms_.ocean_horizon_wave_phases, 2,
               glm::value_ptr(ocean_phase_uniforms.front()));
  glUniform1f(sky_uniforms_.ocean_horizon_severity, ocean.severity);
  glUniform1f(sky_uniforms_.ocean_horizon_tempest_factor, ocean.tempest_factor);
  const auto horizon_sun_color =
      environment.sun_color *
      (environment.sun_direction.y > 0.0F ? 1.0F : 0.0F);
  glUniform3fv(sky_uniforms_.ocean_horizon_sun_color, 1,
               glm::value_ptr(horizon_sun_color));
  const auto maritime_fog_range =
      sea_horizon_fog_range(environment.storm_intensity);
  glUniform2f(sky_uniforms_.maritime_far_fog_range,
              maritime_fog_range.start_distance,
              maritime_fog_range.end_distance);
  glUniform3fv(sky_uniforms_.fog_color, 1,
               glm::value_ptr(environment.fog_color));
  glUniform3fv(sky_uniforms_.distant_fog_color, 1,
               glm::value_ptr(environment.distant_fog_color));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, accent_texture_);
  glBindVertexArray(screen_quad_vao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  record_triangle_draw(3);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
}

void Renderer::run_post_process(const EnvironmentState &environment,
                                float weather_exposure,
                                const MaritimeSubmersionState &submersion,
                                int width, int height,
                                float projection_far_distance,
                                bool optional_effects_enabled) {
  if (post_process_program_ == 0 || screen_quad_vao_ == 0 ||
      scene_color_texture_ == 0 || scene_depth_texture_ == 0) {
    return;
  }

  const auto glow_resources_ready =
      glow_extract_program_ != 0 && glow_blur_program_ != 0 &&
      glow_extract_framebuffer_ != 0 && glow_extract_texture_ != 0 &&
      glow_ping_framebuffer_ != 0 && glow_ping_texture_ != 0 &&
      glow_target_width_ > 0 && glow_target_height_ > 0;
  const auto run_optional_effects =
      optional_effects_enabled && glow_resources_ready;
  const auto quality_settings = active_quality_settings_;

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  glBindVertexArray(screen_quad_vao_);

  if (run_optional_effects) {
    glViewport(0, 0, glow_target_width_, glow_target_height_);
    glBindFramebuffer(GL_FRAMEBUFFER, glow_extract_framebuffer_);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(glow_extract_program_);
    glUniform1i(glow_extract_uniforms_.scene_texture, 0);
    glUniform1f(glow_extract_uniforms_.threshold,
                visual_pipeline_glow_threshold(options_.visual_pipeline,
                                               environment.glow_threshold));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_color_texture_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    record_triangle_draw(3);

    glBindFramebuffer(GL_FRAMEBUFFER, glow_ping_framebuffer_);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(glow_blur_program_);
    glUniform1i(glow_blur_uniforms_.source_texture, 0);
    glUniform2f(glow_blur_uniforms_.texel_direction,
                1.0F / static_cast<float>(glow_target_width_), 0.0F);
    glBindTexture(GL_TEXTURE_2D, glow_extract_texture_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    record_triangle_draw(3);

    glBindFramebuffer(GL_FRAMEBUFFER, glow_extract_framebuffer_);
    glClear(GL_COLOR_BUFFER_BIT);
    glUniform2f(glow_blur_uniforms_.texel_direction, 0.0F,
                1.0F / static_cast<float>(glow_target_height_));
    glBindTexture(GL_TEXTURE_2D, glow_ping_texture_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    record_triangle_draw(3);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, width, std::max(height, 1));
  glUseProgram(post_process_program_);
  glUniform1i(post_process_uniforms_.scene_texture, 0);
  glUniform1i(post_process_uniforms_.glow_texture, 1);
  glUniform1i(post_process_uniforms_.scene_depth, 2);
  glUniform1f(post_process_uniforms_.exposure,
              std::isfinite(environment.exposure)
                  ? std::max(environment.exposure, 0.001F)
                  : 1.0F);
  glUniform1f(post_process_uniforms_.saturation_boost,
              run_optional_effects ? environment.saturation_boost : 1.0F);
  glUniform1f(post_process_uniforms_.contrast,
              run_optional_effects
                  ? visual_pipeline_post_contrast(options_.visual_pipeline,
                                                  environment.contrast)
                  : 1.0F);
  glUniform1f(post_process_uniforms_.vignette_strength,
              run_optional_effects ? environment.vignette_strength : 0.0F);
  const auto night_tint =
      run_optional_effects ? environment.night_tint_color : glm::vec3{0.0F};
  glUniform3fv(post_process_uniforms_.night_tint_color, 1,
               glm::value_ptr(night_tint));
  glUniform1f(post_process_uniforms_.glow_strength,
              run_optional_effects
                  ? visual_pipeline_glow_strength(options_.visual_pipeline,
                                                  environment.glow_strength)
                  : 0.0F);
  glUniform1f(post_process_uniforms_.sharpen_strength,
              run_optional_effects ? environment.post_sharpen_strength *
                                         quality_settings.post_detail_scale
                                   : 0.0F);
  glUniform1f(post_process_uniforms_.edge_strength,
              run_optional_effects ? environment.post_edge_strength *
                                         quality_settings.post_detail_scale *
                                         (options_.visual_pipeline ==
                                                  VisualPipeline::ModernStylized
                                              ? 0.32F
                                              : 1.0F)
                                   : 0.0F);
  glUniform1i(post_process_uniforms_.fxaa_enabled,
              run_optional_effects &&
                      is_modern_visual_pipeline(options_.visual_pipeline) &&
                      quality_settings.fxaa_enabled
                  ? 1
                  : 0);
  glUniform1i(post_process_uniforms_.modern_pipeline,
              is_modern_visual_pipeline(options_.visual_pipeline) ? 1 : 0);
  glUniform1i(post_process_uniforms_.resolve_only,
              run_optional_effects ? 0 : 1);
  glUniform1f(post_process_uniforms_.storm_intensity,
              run_optional_effects || submersion.active
                  ? environment.storm_intensity
                  : 0.0F);
  glUniform1f(post_process_uniforms_.lightning_intensity,
              run_optional_effects || submersion.active
                  ? environment.lightning_intensity
                  : 0.0F);
  glUniform1f(
      post_process_uniforms_.weather_exposure,
      glm::clamp(std::isfinite(weather_exposure) ? weather_exposure : 1.0F,
                 0.0F, 1.0F));
  glUniform1f(post_process_uniforms_.projection_far_distance,
              std::isfinite(projection_far_distance)
                  ? std::max(projection_far_distance, 0.101F)
                  : 320.0F);
  glUniform1i(post_process_uniforms_.maritime_submerged,
              submersion.active ? 1 : 0);
  glUniform1f(post_process_uniforms_.maritime_submersion_depth,
              std::isfinite(submersion.depth)
                  ? std::clamp(submersion.depth, 0.0F, 64.0F)
                  : 0.0F);
  glUniform1f(post_process_uniforms_.maritime_submersion_blend,
              std::isfinite(submersion.blend)
                  ? std::clamp(submersion.blend, 0.0F, 1.0F)
                  : 0.0F);
  glUniform1f(post_process_uniforms_.water_surface_detail,
              quality_settings.water_surface_detail);
  glUniform1f(post_process_uniforms_.time_seconds,
              std::isfinite(environment.weather_time_seconds)
                  ? environment.weather_time_seconds
                  : 0.0F);
  glUniform1f(post_process_uniforms_.daylight_factor,
              std::clamp(environment.daylight_factor, 0.0F, 1.0F));

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, scene_color_texture_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, run_optional_effects ? glow_extract_texture_
                                                    : scene_color_texture_);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, scene_depth_texture_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  record_triangle_draw(3);
  glActiveTexture(GL_TEXTURE0);
}

void Renderer::run_menu_background_pass(int width, int height, float exposure) {
  if (menu_background_program_ == 0 || glow_blur_program_ == 0 ||
      screen_quad_vao_ == 0 || scene_color_texture_ == 0 ||
      glow_extract_texture_ == 0 || glow_ping_texture_ == 0) {
    return;
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  glBindVertexArray(screen_quad_vao_);

  glViewport(0, 0, glow_target_width_, glow_target_height_);
  glBindFramebuffer(GL_FRAMEBUFFER, glow_ping_framebuffer_);
  glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(glow_blur_program_);
  glUniform1i(glow_blur_uniforms_.source_texture, 0);
  glUniform2f(glow_blur_uniforms_.texel_direction,
              1.0F / static_cast<float>(glow_target_width_), 0.0F);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, scene_color_texture_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  record_triangle_draw(3);

  glBindFramebuffer(GL_FRAMEBUFFER, glow_extract_framebuffer_);
  glClear(GL_COLOR_BUFFER_BIT);
  glUniform2f(glow_blur_uniforms_.texel_direction, 0.0F,
              1.0F / static_cast<float>(glow_target_height_));
  glBindTexture(GL_TEXTURE_2D, glow_ping_texture_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  record_triangle_draw(3);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, width, std::max(height, 1));
  glUseProgram(menu_background_program_);
  glUniform1i(menu_background_uniforms_.scene_texture, 0);
  glUniform1i(menu_background_uniforms_.blur_texture, 1);
  glUniform1f(menu_background_uniforms_.blur_mix, 0.80F);
  const glm::vec3 tint_color{0.66F, 0.72F, 0.78F};
  glUniform3fv(menu_background_uniforms_.tint_color, 1,
               glm::value_ptr(tint_color));
  glUniform1f(menu_background_uniforms_.vignette_strength, 0.30F);
  glUniform1f(menu_background_uniforms_.exposure,
              std::isfinite(exposure) ? std::max(exposure, 0.001F) : 1.0F);
  glUniform1i(menu_background_uniforms_.modern_pipeline,
              is_modern_visual_pipeline(options_.visual_pipeline) ? 1 : 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, scene_color_texture_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, glow_extract_texture_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  record_triangle_draw(3);
  glActiveTexture(GL_TEXTURE0);
}

void Renderer::draw_precipitation(
    const glm::mat4 &view_projection, const glm::mat4 &inverse_view,
    const glm::vec3 &camera_position, const EnvironmentState &environment,
    const OceanState &ocean, const ShipRenderState &ship,
    const RendererQualitySettings &quality_settings,
    RendererFrameStats &frame_stats) {
  if (precipitation_program_ == 0 || precipitation_vao_ == 0 ||
      precipitation_instance_vbo_ == 0 ||
      !std::isfinite(environment.precipitation_intensity) ||
      environment.precipitation_intensity <= 0.0F) {
    precipitation_field_.clear();
    return;
  }

  const PrecipitationBudget budget{
      quality_settings.precipitation_drop_budget,
      quality_settings.precipitation_impact_budget,
      quality_settings.precipitation_radius,
  };
  const auto ocean_displacement =
      std::isfinite(ocean.maximum_displacement)
          ? std::clamp(ocean.maximum_displacement, 0.0F, 16.0F)
          : 0.0F;
  // Je prolonge les trajectoires sous le creux theorique le plus bas ; le
  // depth test les coupe ensuite sur la surface animee exacte de la mer.
  const auto precipitation_floor =
      static_cast<float>(kSeaLevel + 1) - ocean_displacement - 0.50F;
  const auto &precipitation = precipitation_field_.sample(
      environment, camera_position, precipitation_floor, budget);

  auto &instances = precipitation_instances_scratch_;
  instances.clear();
  instances.reserve(precipitation.drops.size() + precipitation.impacts.size());

  const auto protection_enabled = ship_protection_is_renderable(ship);
  const auto inverse_ship_model =
      protection_enabled ? glm::inverse(ship.model_matrix) : glm::mat4{1.0F};
  const auto *protection_profile =
      protection_enabled ? &ship.blueprint->protection_profile : nullptr;
  const auto to_ship_local =
      [&inverse_ship_model](const glm::vec3 &world_point) noexcept {
        return glm::vec3{
            inverse_ship_model * glm::vec4{world_point, 1.0F},
        };
      };
  const auto point_is_sheltered = [&to_ship_local, protection_profile](
                                      const glm::vec3 &world_point) noexcept {
    return protection_profile != nullptr &&
           protection_profile->shelters_from_weather_local(
               to_ship_local(world_point));
  };
  const auto point_excludes_ocean = [&to_ship_local, protection_profile](
                                        const glm::vec3 &world_point) noexcept {
    return protection_profile != nullptr &&
           protection_profile->excludes_ocean_local(to_ship_local(world_point));
  };

  auto visible_drop_count = std::size_t{0U};
  for (const auto &drop : precipitation.drops) {
    const auto speed_squared = glm::dot(drop.velocity, drop.velocity);
    const auto direction =
        std::isfinite(speed_squared) && speed_squared > 1.0e-6F
            ? drop.velocity / std::sqrt(speed_squared)
            : glm::vec3{0.0F, -1.0F, 0.0F};
    const auto half_segment = direction * (std::max(drop.length, 0.0F) * 0.5F);
    if (point_is_sheltered(drop.position) ||
        point_is_sheltered(drop.position - half_segment) ||
        point_is_sheltered(drop.position + half_segment)) {
      continue;
    }

    instances.push_back({
        {
            drop.position.x,
            drop.position.y,
            drop.position.z,
            drop.length,
        },
        {
            drop.velocity.x,
            drop.velocity.y,
            drop.velocity.z,
            drop.width,
        },
        {
            drop.opacity,
            0.0F,
            0.0F,
            0.0F,
        },
    });
    ++visible_drop_count;
  }

  auto wind_direction = glm::vec2{
      environment.wind_direction_xz.x,
      environment.wind_direction_xz.y,
  };
  const auto wind_length_squared = glm::dot(wind_direction, wind_direction);
  if (!std::isfinite(wind_length_squared) || wind_length_squared <= 1.0e-6F) {
    wind_direction = {0.0F, 1.0F};
  } else {
    wind_direction /= std::sqrt(wind_length_squared);
  }
  const auto fall_direction = glm::normalize(glm::vec3{
      wind_direction.x * glm::clamp(environment.wind_strength, 0.0F, 1.0F) *
          0.32F,
      -1.0F,
      wind_direction.y * glm::clamp(environment.wind_strength, 0.0F, 1.0F) *
          0.32F,
  });
  constexpr auto kImpactRayLength = 96.0F;
  auto visible_impact_count = std::size_t{0U};
  for (const auto &impact : precipitation.impacts) {
    auto impact_position = impact.position;
    auto deck_hit = false;

    if (protection_enabled) {
      const auto ray_end = glm::vec3{
          impact.position.x,
          static_cast<float>(kSeaLevel + 1),
          impact.position.z,
      };
      const auto ray_origin_world = ray_end - fall_direction * kImpactRayLength;
      const auto ray_origin_local = to_ship_local(ray_origin_world);
      auto ray_direction_local = glm::vec3{
          inverse_ship_model * glm::vec4{fall_direction, 0.0F},
      };
      const auto local_direction_length_squared =
          glm::dot(ray_direction_local, ray_direction_local);
      if (std::isfinite(local_direction_length_squared) &&
          local_direction_length_squared > 1.0e-6F) {
        ray_direction_local /= std::sqrt(local_direction_length_squared);
        auto nearest_hit = std::numeric_limits<float>::max();
        auto nearest_local_position = glm::vec3{0.0F};

        for (const auto &part : ship.parts) {
          if (!part.supports_player) {
            continue;
          }
          const auto min_corner =
              glm::min(part.local_start, part.local_end) - glm::vec3{0.01F};
          const auto max_corner =
              glm::max(part.local_start, part.local_end) + glm::vec3{0.01F};
          const auto distance =
              ray_aabb_entry_distance(ray_origin_local, ray_direction_local,
                                      min_corner, max_corner, kImpactRayLength);
          if (!distance.has_value() || *distance >= nearest_hit) {
            continue;
          }

          const auto local_hit =
              ray_origin_local + ray_direction_local * *distance;
          const auto outside_probe = local_hit - ray_direction_local * 0.05F;
          if (protection_profile->shelters_from_weather_local(outside_probe)) {
            continue;
          }
          nearest_hit = *distance;
          nearest_local_position = local_hit;
        }

        if (nearest_hit < std::numeric_limits<float>::max()) {
          impact_position = glm::vec3{
              ship.model_matrix *
                  glm::vec4{
                      nearest_local_position - ray_direction_local * 0.025F,
                      1.0F,
                  },
          };
          deck_hit = true;
        }
      }
    }

    if (!deck_hit) {
      const auto ocean_sample =
          OceanSimulation::sample(ocean, {impact_position.x, impact_position.z},
                                  static_cast<std::size_t>(std::clamp(
                                      quality_settings.ocean_wave_count, 1,
                                      static_cast<int>(kOceanMaxWaveCount))));
      impact_position.y =
          static_cast<float>(kSeaLevel + 1) + ocean_sample.height + 0.035F;
      if (point_excludes_ocean(impact_position)) {
        continue;
      }
    }
    if (point_is_sheltered(impact_position) || !finite_vec3(impact_position) ||
        impact.opacity <= 0.003F) {
      continue;
    }

    const auto age_ratio =
        impact.lifetime_seconds > 1.0e-4F
            ? glm::clamp(impact.age_seconds / impact.lifetime_seconds, 0.0F,
                         1.0F)
            : 1.0F;
    instances.push_back({
        {
            impact_position.x,
            impact_position.y,
            impact_position.z,
            impact.radius,
        },
        {
            0.0F,
            1.0F,
            0.0F,
            impact.radius,
        },
        {
            impact.opacity,
            1.0F,
            age_ratio,
            impact.radius,
        },
    });
    ++visible_impact_count;
  }

  if (instances.empty()) {
    return;
  }

  const auto instance_bytes = static_cast<GLsizeiptr>(
      instances.size() * sizeof(PrecipitationGpuInstance));
  const ScopedPrecipitationGlState previous_gl_state{};
  glBindVertexArray(precipitation_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, precipitation_instance_vbo_);
  if (precipitation_instance_buffer_bytes_ < instance_bytes) {
    precipitation_instance_buffer_bytes_ = grow_buffer_capacity(
        precipitation_instance_buffer_bytes_, instance_bytes,
        kInitialPrecipitationInstanceBufferBytes);
  }
  orphan_bound_buffer(GL_ARRAY_BUFFER, precipitation_instance_buffer_bytes_);
  glBufferSubData(GL_ARRAY_BUFFER, 0, instance_bytes, instances.data());
  frame_uploaded_bytes_ +=
      static_cast<std::uint64_t>(std::max<GLsizeiptr>(instance_bytes, 0));

  const auto camera_right = glm::normalize(glm::vec3{
      inverse_view[0],
  });
  const auto camera_up = glm::normalize(glm::vec3{
      inverse_view[1],
  });

  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(precipitation_program_);
  glUniformMatrix4fv(precipitation_uniforms_.view_projection, 1, GL_FALSE,
                     glm::value_ptr(view_projection));
  glUniform3fv(precipitation_uniforms_.camera_position, 1,
               glm::value_ptr(camera_position));
  glUniform3fv(precipitation_uniforms_.camera_right, 1,
               glm::value_ptr(camera_right));
  glUniform3fv(precipitation_uniforms_.camera_up, 1, glm::value_ptr(camera_up));
  glUniform3fv(precipitation_uniforms_.fog_color, 1,
               glm::value_ptr(environment.fog_color));
  glUniform1f(precipitation_uniforms_.lightning_intensity,
              glm::clamp(environment.lightning_intensity, 0.0F, 1.0F));
  glUniform1f(precipitation_uniforms_.storm_intensity,
              glm::clamp(environment.storm_intensity, 0.0F, 1.0F));
  upload_precipitation_ship_protection(ship);

  glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                        static_cast<GLsizei>(instances.size()));
  record_triangle_draw(6, static_cast<GLsizei>(instances.size()));

  frame_stats.precipitation_drops = visible_drop_count;
  frame_stats.precipitation_impacts = visible_impact_count;
}

void Renderer::draw_old_guard_effects(
    std::span<const OldGuardMuzzleFlashInstance> flashes,
    std::span<const OldGuardSmokeInstance> smoke,
    const glm::mat4 &view_projection, const glm::mat4 &inverse_view,
    const glm::vec3 &camera_position, bool viewmodel_overlay,
    const PlayerViewModelPose *viewmodel_pose) {
  if ((flashes.empty() && smoke.empty()) || old_guard_effect_program_ == 0 ||
      old_guard_effect_vao_ == 0 || old_guard_effect_instance_vbo_ == 0) {
    return;
  }

  auto &instances = old_guard_effect_instances_scratch_;
  instances.clear();
  const auto maximum_count = std::min(smoke.size(), kOldGuardSmokeCapacity) +
                             std::min(flashes.size(), kOldGuardFlashCapacity);
  if (instances.capacity() < maximum_count) {
    instances.reserve(maximum_count);
  }

  constexpr auto kEffectDrawDistanceSquared =
      kOldGuardRenderDistance * kOldGuardRenderDistance;
  const auto append_if_visible = [&](const glm::vec3 &position, float size,
                                     float opacity, float kind, float rotation,
                                     float intensity) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z) || !std::isfinite(size) ||
        !std::isfinite(opacity) || !std::isfinite(rotation) ||
        !std::isfinite(intensity) || size <= 0.0F || opacity <= 0.0F) {
      return;
    }
    const auto delta = position - camera_position;
    const auto distance_squared = glm::dot(delta, delta);
    if (!std::isfinite(distance_squared) ||
        distance_squared > kEffectDrawDistanceSquared) {
      return;
    }
    instances.push_back({
        .position_size =
            glm::vec4{
                position,
                std::clamp(size, 0.01F, 3.0F),
            },
        .appearance =
            glm::vec4{
                std::clamp(opacity, 0.0F, 1.0F),
                kind,
                rotation,
                std::max(intensity, 0.0F),
            },
    });
  };

  for (const auto &puff :
       smoke.first(std::min(smoke.size(), kOldGuardSmokeCapacity))) {
    if (!std::isfinite(puff.age) || !std::isfinite(puff.lifetime) ||
        puff.lifetime <= 0.0F || puff.age < 0.0F || puff.age >= puff.lifetime) {
      continue;
    }
    const auto age_ratio = std::clamp(puff.age / puff.lifetime, 0.0F, 1.0F);
    const auto fade = std::pow(1.0F - age_ratio, 1.25F);
    const auto seed_variation = static_cast<float>(puff.seed % 997U) / 997.0F;
    append_if_visible(puff.position, puff.size * (1.0F + age_ratio * 2.35F),
                      puff.opacity * fade, 0.0F, puff.rotation_radians,
                      seed_variation);
  }
  for (const auto &flash :
       flashes.first(std::min(flashes.size(), kOldGuardFlashCapacity))) {
    if (!std::isfinite(flash.age) || !std::isfinite(flash.lifetime) ||
        flash.lifetime <= 0.0F || flash.age < 0.0F ||
        flash.age >= flash.lifetime) {
      continue;
    }
    const auto age_ratio = std::clamp(flash.age / flash.lifetime, 0.0F, 1.0F);
    const auto flash_position = viewmodel_overlay &&
                                        viewmodel_pose != nullptr &&
                                        viewmodel_pose->musket_active
                                    ? viewmodel_pose->muzzle_position
                                    : flash.position;
    const auto flash_direction = viewmodel_overlay &&
                                         viewmodel_pose != nullptr &&
                                         viewmodel_pose->musket_active
                                     ? viewmodel_pose->muzzle_forward
                                     : flash.direction;
    append_if_visible(flash_position, flash.size * (1.0F + age_ratio * 0.55F),
                      (1.0F - age_ratio) * (1.0F - age_ratio), 1.0F,
                      std::atan2(flash_direction.y, flash_direction.x),
                      flash.intensity);
  }
  if (instances.empty()) {
    return;
  }

  // Je trie toutes les transparences du fond vers la camera avant l'upload ;
  // la profondeur reste lue mais aucune bouffee ne masque les suivantes.
  std::stable_sort(instances.begin(), instances.end(),
                   [&](const OldGuardEffectGpuInstance &left,
                       const OldGuardEffectGpuInstance &right) noexcept {
                     const auto left_delta =
                         glm::vec3{left.position_size} - camera_position;
                     const auto right_delta =
                         glm::vec3{right.position_size} - camera_position;
                     return glm::dot(left_delta, left_delta) >
                            glm::dot(right_delta, right_delta);
                   });

  const auto instance_bytes = static_cast<GLsizeiptr>(
      instances.size() * sizeof(OldGuardEffectGpuInstance));
  const ScopedPrecipitationGlState previous_gl_state{};
  glBindVertexArray(old_guard_effect_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, old_guard_effect_instance_vbo_);
  if (old_guard_effect_instance_buffer_bytes_ < instance_bytes) {
    old_guard_effect_instance_buffer_bytes_ = grow_buffer_capacity(
        old_guard_effect_instance_buffer_bytes_, instance_bytes,
        kInitialOldGuardEffectInstanceBufferBytes);
  }
  orphan_bound_buffer(GL_ARRAY_BUFFER, old_guard_effect_instance_buffer_bytes_);
  glBufferSubData(GL_ARRAY_BUFFER, 0, instance_bytes, instances.data());
  frame_uploaded_bytes_ +=
      static_cast<std::uint64_t>(std::max<GLsizeiptr>(instance_bytes, 0));

  const auto camera_right = glm::normalize(glm::vec3{inverse_view[0]});
  const auto camera_up = glm::normalize(glm::vec3{inverse_view[1]});

  if (viewmodel_overlay) {
    // Je dessine le flash avec la projection du fusil et sans profondeur :
    // il reste ainsi solidaire du viewmodel avant que la fumee ne vive
    // independamment dans le monde.
    glDisable(GL_DEPTH_TEST);
  } else {
    glEnable(GL_DEPTH_TEST);
  }
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(old_guard_effect_program_);
  glUniformMatrix4fv(old_guard_effect_uniforms_.view_projection, 1, GL_FALSE,
                     glm::value_ptr(view_projection));
  glUniform3fv(old_guard_effect_uniforms_.camera_right, 1,
               glm::value_ptr(camera_right));
  glUniform3fv(old_guard_effect_uniforms_.camera_up, 1,
               glm::value_ptr(camera_up));
  glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                        static_cast<GLsizei>(instances.size()));
  record_triangle_draw(6, static_cast<GLsizei>(instances.size()));
}

void Renderer::draw_item_drops(
    std::span<const ItemDropRenderInstance> item_drops,
    const glm::mat4 &view_projection, const glm::mat4 &light_view_projection,
    const glm::mat4 &light_view_projection_far, int shadow_cascade_count,
    float shadow_split_distance, float shadow_transition_width,
    const glm::mat4 &inverse_view_projection, const glm::vec3 &camera_position,
    const glm::vec3 &camera_forward, const EnvironmentState &environment,
    bool sun_visible,
    std::span<const glm::vec4> backrooms_flicker_lights,
    const BackroomsTerminalFogRange &backrooms_fog_range,
    float backrooms_flashlight_strength) {
  if (item_drops.empty() || item_drop_program_ == 0 || item_drop_vao_ == 0 ||
      item_drop_instance_vbo_ == 0 || item_drop_ebo_ == 0) {
    return;
  }

  auto &instances = item_drop_instances_scratch_;
  build_item_drop_gpu_instances_into(item_drops, instances);
  if (instances.empty()) {
    return;
  }

  const auto instance_bytes =
      static_cast<GLsizeiptr>(instances.size() * sizeof(ItemDropGpuInstance));
  glBindVertexArray(item_drop_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, item_drop_instance_vbo_);
  if (item_drop_instance_buffer_bytes_ < instance_bytes) {
    item_drop_instance_buffer_bytes_ =
        grow_buffer_capacity(item_drop_instance_buffer_bytes_, instance_bytes,
                             kInitialItemDropInstanceBufferBytes);
  }
  orphan_bound_buffer(GL_ARRAY_BUFFER, item_drop_instance_buffer_bytes_);
  glBufferSubData(GL_ARRAY_BUFFER, 0, instance_bytes, instances.data());
  frame_uploaded_bytes_ +=
      static_cast<std::uint64_t>(std::max<GLsizeiptr>(instance_bytes, 0));

  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glDisable(GL_BLEND);

  glUseProgram(item_drop_program_);
  glUniformMatrix4fv(item_drop_uniforms_.view_projection, 1, GL_FALSE,
                     glm::value_ptr(view_projection));
  glUniformMatrix4fv(item_drop_uniforms_.light_view_projection, 1, GL_FALSE,
                     glm::value_ptr(light_view_projection));
  glUniformMatrix4fv(item_drop_uniforms_.light_view_projection_far, 1, GL_FALSE,
                     glm::value_ptr(light_view_projection_far));
  glUniformMatrix4fv(item_drop_uniforms_.inverse_view_projection, 1, GL_FALSE,
                     glm::value_ptr(inverse_view_projection));
  glUniform3fv(item_drop_uniforms_.camera_position, 1,
               glm::value_ptr(camera_position));
  glUniform3fv(item_drop_uniforms_.camera_forward, 1,
               glm::value_ptr(camera_forward));
  glUniform3fv(item_drop_uniforms_.sun_direction, 1,
               glm::value_ptr(environment.sun_direction));
  glUniform3fv(item_drop_uniforms_.sun_color, 1,
               glm::value_ptr(environment.sun_color));
  glUniform3fv(item_drop_uniforms_.ambient_color, 1,
               glm::value_ptr(environment.ambient_color));
  glUniform3fv(item_drop_uniforms_.block_light_color, 1,
               glm::value_ptr(environment.block_light_color));
  glUniform1i(item_drop_uniforms_.enclosed_interior,
              environment.enclosed_interior ? 1 : 0);
  glUniform1f(item_drop_uniforms_.interior_visibility_floor,
              environment.interior_visibility_floor);
  const auto flicker_count =
      std::min(
          backrooms_flicker_lights.size(),
          kMaximumBackroomsFlickerLights);
  glUniform1i(
      item_drop_uniforms_.backrooms_flicker_count,
      static_cast<GLint>(flicker_count));
  if (flicker_count > 0U) {
    glUniform4fv(
        item_drop_uniforms_.backrooms_flicker_lights,
        static_cast<GLsizei>(flicker_count),
        glm::value_ptr(backrooms_flicker_lights.front()));
  }
  glUniform1f(
      item_drop_uniforms_.backrooms_flashlight_intensity,
      std::isfinite(backrooms_flashlight_strength)
          ? std::clamp(
                backrooms_flashlight_strength,
                0.0F,
                1.0F)
          : 0.0F);
  glUniform3fv(item_drop_uniforms_.fog_color, 1,
               glm::value_ptr(environment.fog_color));
  glUniform3fv(item_drop_uniforms_.distant_fog_color, 1,
               glm::value_ptr(environment.distant_fog_color));
  glUniform2f(
      item_drop_uniforms_.interior_fog_range,
      backrooms_fog_range.start_distance,
      backrooms_fog_range.end_distance);
  glUniform3fv(item_drop_uniforms_.horizon_glow_color, 1,
               glm::value_ptr(environment.horizon_glow_color));
  glUniform3fv(item_drop_uniforms_.night_tint_color, 1,
               glm::value_ptr(environment.night_tint_color));
  glUniform1f(item_drop_uniforms_.daylight_factor, environment.daylight_factor);
  glUniform1f(item_drop_uniforms_.sun_visibility, sun_visible ? 1.0F : 0.0F);
  glUniform1f(item_drop_uniforms_.time_of_day, environment.time_of_day);
  glUniform1f(item_drop_uniforms_.cloud_intensity, environment.cloud_intensity);
  glUniform1f(item_drop_uniforms_.cloud_shadow_strength,
              environment.cloud_shadow_strength);
  glUniform1f(item_drop_uniforms_.wind_strength, environment.wind_strength);
  glUniform1f(item_drop_uniforms_.atmospheric_scatter_strength,
              environment.atmospheric_scatter_strength);
  glUniform1f(item_drop_uniforms_.height_fog_density,
              environment.height_fog_density);
  glUniform1f(item_drop_uniforms_.precipitation_intensity,
              environment.precipitation_intensity);
  glUniform1f(item_drop_uniforms_.storm_intensity, environment.storm_intensity);
  glUniform1f(item_drop_uniforms_.lightning_intensity,
              environment.lightning_intensity);
  glUniform1i(item_drop_uniforms_.atlas, 0);
  glUniform1i(item_drop_uniforms_.shadow_map, 1);
  glUniform1i(item_drop_uniforms_.shadow_map_far, 7);
  glUniform1i(item_drop_uniforms_.shadow_cascade_count, shadow_cascade_count);
  glUniform1f(item_drop_uniforms_.shadow_split_distance, shadow_split_distance);
  glUniform1f(item_drop_uniforms_.shadow_transition_width,
              shadow_transition_width);
  glUniform1i(item_drop_uniforms_.scene_color, 2);
  glUniform1i(item_drop_uniforms_.scene_depth, 3);
  glUniform1i(item_drop_uniforms_.shadows_enabled,
              options_.shadows_enabled ? 1 : 0);

  const auto scene_bindings = select_scene_sampler_bindings(
      false, scene_fallback_color_texture_, scene_fallback_depth_texture_,
      scene_color_texture_, scene_depth_texture_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, atlas_texture_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, shadow_map_);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, scene_bindings.color_texture);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, scene_bindings.depth_texture);
  glActiveTexture(GL_TEXTURE7);
  glBindTexture(GL_TEXTURE_2D, shadow_map_far_);

  glDrawElementsInstanced(GL_TRIANGLES, item_drop_template_index_count_,
                          GL_UNSIGNED_INT, nullptr,
                          static_cast<GLsizei>(instances.size()));
  record_triangle_draw(item_drop_template_index_count_,
                       static_cast<GLsizei>(instances.size()));
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glActiveTexture(GL_TEXTURE0);
}

auto Renderer::collect_visible_creature_parts(
    std::span<const CreatureRenderInstance> creatures,
    std::span<const CrewRenderInstance> crew,
    std::span<const OldGuardRenderInstance> old_guard, const glm::vec3 &focus,
    float creature_draw_distance, float crew_draw_distance,
    float old_guard_draw_distance) -> std::span<const CreaturePartInstance> {
  auto &visible_creatures = visible_creatures_cache_;
  auto &visible_crew = visible_crew_cache_;
  auto &visible_old_guard = visible_old_guard_cache_;
  auto &parts = creature_parts_scratch_;
  auto &part_contexts = creature_part_contexts_scratch_;
  visible_creatures.clear();
  visible_crew.clear();
  visible_old_guard.clear();
  parts.clear();
  part_contexts.clear();
  const auto safe_creature_distance =
      std::isfinite(creature_draw_distance)
          ? std::max(creature_draw_distance, 0.0F)
          : 0.0F;
  const auto safe_crew_distance = std::isfinite(crew_draw_distance)
                                      ? std::max(crew_draw_distance, 0.0F)
                                      : 0.0F;
  const auto safe_old_guard_distance =
      std::isfinite(old_guard_draw_distance)
          ? std::max(old_guard_draw_distance, 0.0F)
          : 0.0F;
  const auto creature_draw_distance_sq =
      safe_creature_distance * safe_creature_distance;
  const auto crew_draw_distance_sq = safe_crew_distance * safe_crew_distance;
  const auto old_guard_draw_distance_sq =
      safe_old_guard_distance * safe_old_guard_distance;

  if (visible_creatures.capacity() < creatures.size()) {
    visible_creatures.reserve(creatures.size());
  }
  for (const auto &creature : creatures) {
    const auto dx = creature.position.x - focus.x;
    const auto dz = creature.position.z - focus.z;
    const auto distance_squared = dx * dx + dz * dz;
    if (!std::isfinite(distance_squared) ||
        distance_squared > creature_draw_distance_sq) {
      continue;
    }

    visible_creatures.push_back({&creature, distance_squared});
  }

  std::sort(visible_creatures.begin(), visible_creatures.end(),
            [](const VisibleCreature &lhs, const VisibleCreature &rhs) {
              return lhs.distance_squared < rhs.distance_squared;
            });
  if (visible_creatures.size() > kCreatureMaxRenderedCount) {
    visible_creatures.resize(kCreatureMaxRenderedCount);
  }

  if (visible_crew.capacity() < crew.size()) {
    visible_crew.reserve(crew.size());
  }
  for (const auto &crew_member : crew) {
    const auto dx = crew_member.position.x - focus.x;
    const auto dz = crew_member.position.z - focus.z;
    const auto distance_squared = dx * dx + dz * dz;
    if (!std::isfinite(distance_squared) ||
        distance_squared > crew_draw_distance_sq) {
      continue;
    }
    visible_crew.push_back({&crew_member, distance_squared});
  }
  std::sort(visible_crew.begin(), visible_crew.end(),
            [](const VisibleCrewMember &lhs, const VisibleCrewMember &rhs) {
              return lhs.distance_squared < rhs.distance_squared;
            });
  if (visible_crew.size() > kCrewVisualRenderCapacity) {
    visible_crew.resize(kCrewVisualRenderCapacity);
  }

  if (visible_old_guard.capacity() < old_guard.size()) {
    visible_old_guard.reserve(old_guard.size());
  }
  for (const auto &guard : old_guard) {
    const auto dx = guard.position.x - focus.x;
    const auto dz = guard.position.z - focus.z;
    const auto distance_squared = dx * dx + dz * dz;
    if (!std::isfinite(distance_squared) ||
        distance_squared > old_guard_draw_distance_sq) {
      continue;
    }
    visible_old_guard.push_back({&guard, distance_squared});
  }
  std::sort(visible_old_guard.begin(), visible_old_guard.end(),
            [](const VisibleOldGuardMember &left,
               const VisibleOldGuardMember &right) noexcept {
              return left.distance_squared < right.distance_squared;
            });
  if (visible_old_guard.size() > kOldGuardMemberCount) {
    visible_old_guard.resize(kOldGuardMemberCount);
  }

  const auto jack_offset =
      backrooms_jack_render_view_.position - focus;
  const auto jack_distance_squared =
      glm::dot(jack_offset, jack_offset);
  const auto jack_visible =
      backrooms_jack_render_view_.visible &&
      !backrooms_jack_parts_.empty() &&
      std::isfinite(jack_distance_squared) &&
      jack_distance_squared <=
          creature_draw_distance_sq;

  const auto marlow_offset =
      backrooms_marlow_visual_anchor_ - focus;
  const auto marlow_distance_squared =
      glm::dot(marlow_offset, marlow_offset);
  const auto marlow_visible =
      !backrooms_marlow_parts_.empty() &&
      std::isfinite(marlow_distance_squared) &&
      marlow_distance_squared <= creature_draw_distance_sq;

  if (visible_creatures.empty() && visible_crew.empty() &&
      visible_old_guard.empty() && legendary_world_parts_.empty() &&
      !jack_visible && !marlow_visible) {
    return {};
  }

  const auto required_part_capacity =
      visible_creatures.size() * kCreatureMaxBoxBudget +
      visible_crew.size() * kCrewVisualPartBudget +
      visible_old_guard.size() * kOldGuardVisualPartBudget +
      legendary_world_parts_.size() +
      (jack_visible ? backrooms_jack_parts_.size() : 0U) +
      (marlow_visible ? backrooms_marlow_parts_.size() : 0U);
  if (parts.capacity() < required_part_capacity) {
    parts.reserve(required_part_capacity);
  }
  if (part_contexts.capacity() < required_part_capacity) {
    part_contexts.reserve(required_part_capacity);
  }

  for (const auto &visible_creature : visible_creatures) {
    const auto creature_parts =
        build_creature_parts(*visible_creature.creature);
    if (creature_parts.empty()) {
      continue;
    }

    parts.insert(parts.end(), creature_parts.begin(), creature_parts.end());
    part_contexts.insert(part_contexts.end(), creature_parts.size(),
                         VisualEntityContext::Creature);
  }
  for (const auto &visible_member : visible_crew) {
    const auto first_part = parts.size();
    append_crew_parts(parts, *visible_member.crew);
    part_contexts.insert(part_contexts.end(), parts.size() - first_part,
                         VisualEntityContext::Crew);
  }
  for (const auto &visible_member : visible_old_guard) {
    const auto first_part = parts.size();
    append_old_guard_parts(parts, *visible_member.guard);
    part_contexts.insert(part_contexts.end(), parts.size() - first_part,
                         VisualEntityContext::Crew);
  }
  if (jack_visible) {
    parts.insert(
        parts.end(),
        backrooms_jack_parts_.begin(),
        backrooms_jack_parts_.end());
    part_contexts.insert(
        part_contexts.end(),
        backrooms_jack_parts_.size(),
        VisualEntityContext::Creature);
  }
  if (marlow_visible) {
    parts.insert(
        parts.end(),
        backrooms_marlow_parts_.begin(),
        backrooms_marlow_parts_.end());
    part_contexts.insert(
        part_contexts.end(),
        backrooms_marlow_parts_.size(),
        VisualEntityContext::Creature);
  }
  const auto legendary_draw_distance_squared =
      kLegendaryWorldDrawDistance * kLegendaryWorldDrawDistance;
  const auto has_legendary_contexts =
      legendary_world_part_contexts_.size() == legendary_world_parts_.size();
  for (std::size_t index = 0U; index < legendary_world_parts_.size(); ++index) {
    const auto &part = legendary_world_parts_[index];
    const auto position = glm::vec3{part.transform[3]};
    const auto offset = position - focus;
    const auto distance_squared = glm::dot(offset, offset);
    if (!std::isfinite(distance_squared) ||
        distance_squared > legendary_draw_distance_squared) {
      continue;
    }
    parts.push_back(part);
    part_contexts.push_back(has_legendary_contexts
                                ? legendary_world_part_contexts_[index]
                                : VisualEntityContext::Generic);
  }
  return parts;
}

void Renderer::prepare_visual_entity_batches(
    std::span<const CreaturePartInstance> parts, VisualEntityContext context,
    std::span<const VisualEntityContext> per_part_contexts,
    const glm::vec3 &focus, bool simplified_shadow, bool viewmodel) {

  for (auto &batch : visual_entity_batches_) {
    batch.clear();
  }

  const auto has_per_part_context = per_part_contexts.size() == parts.size();
  for (std::size_t index = 0U; index < parts.size(); ++index) {
    const auto &part = parts[index];
    const auto part_context =
        has_per_part_context ? per_part_contexts[index] : context;
    const auto classification = classify_visual_entity_part(part, part_context);
    if (!classification.valid_transform) {
      continue;
    }

    const auto maximum_dimension = std::max({
        classification.local_dimensions.x,
        classification.local_dimensions.y,
        classification.local_dimensions.z,
    });
    // Je retire des ombres les boutons, pupilles et petites ferrures : leur
    // silhouette est imperceptible mais chacune coûterait une primitive.
    if (simplified_shadow &&
        !visual_entity_part_casts_simplified_shadow(maximum_dimension)) {
      continue;
    }

    const glm::vec3 position{part.transform[3]};
    const auto offset = position - focus;
    const auto distance_squared = glm::dot(offset, offset);
    const auto lod = select_visual_entity_primitive_lod(
        distance_squared, maximum_dimension,
        active_quality_settings_.terrain_lod_count, simplified_shadow,
        viewmodel);

    const auto slot = visual_entity_batch_slot(classification.primitive, lod);
    auto visual_part = part;
    // Je compose uniquement le gabarit visuel dans le volume de la pièce.
    // La matrice du rig source, ses sockets et ses animations restent
    // strictement inchangés côté gameplay.
    visual_part.transform =
        part.transform * classification.primitive_to_part_local;
    visual_entity_batches_[slot].push_back(std::move(visual_part));
  }
}

void Renderer::draw_visual_entity_batches(GLuint instance_vbo,
                                          GLsizeiptr &instance_buffer_bytes) {

  glBindBuffer(GL_ARRAY_BUFFER, instance_vbo);
  for (std::size_t slot = 0U; slot < visual_entity_batches_.size(); ++slot) {
    const auto &batch = visual_entity_batches_[slot];
    const auto &range = visual_entity_draw_ranges_[slot];
    if (batch.empty() || range.index_count <= 0) {
      continue;
    }

    const auto instance_bytes =
        static_cast<GLsizeiptr>(batch.size() * sizeof(CreaturePartInstance));
    if (instance_buffer_bytes < instance_bytes) {
      instance_buffer_bytes =
          grow_buffer_capacity(instance_buffer_bytes, instance_bytes,
                               kInitialCreatureInstanceBufferBytes);
    }
    orphan_bound_buffer(GL_ARRAY_BUFFER, instance_buffer_bytes);
    glBufferSubData(GL_ARRAY_BUFFER, 0, instance_bytes, batch.data());
    frame_uploaded_bytes_ +=
        static_cast<std::uint64_t>(std::max<GLsizeiptr>(instance_bytes, 0));

    const auto two_sided = range.primitive == StylizedPrimitiveType::Panel ||
                           range.primitive == StylizedPrimitiveType::Ribbon;
    if (two_sided) {
      glDisable(GL_CULL_FACE);
    } else {
      glEnable(GL_CULL_FACE);
    }
    const auto index_byte_offset = range.first_index * sizeof(std::uint32_t);
    glDrawElementsInstanced(GL_TRIANGLES, range.index_count, GL_UNSIGNED_INT,
                            reinterpret_cast<const void *>(
                                static_cast<std::uintptr_t>(index_byte_offset)),
                            static_cast<GLsizei>(batch.size()));
    record_triangle_draw(range.index_count, static_cast<GLsizei>(batch.size()));
  }
  glEnable(GL_CULL_FACE);
}

void Renderer::draw_creature_shadows(
    std::span<const CreatureRenderInstance> creatures,
    std::span<const CrewRenderInstance> crew,
    std::span<const OldGuardRenderInstance> old_guard,
    const glm::mat4 &light_view_projection, const glm::vec3 &shadow_focus) {
  if ((creatures.empty() && crew.empty() && old_guard.empty() &&
       legendary_world_parts_.empty() &&
       backrooms_jack_parts_.empty() &&
       backrooms_marlow_parts_.empty()) ||
      creature_shadow_program_ == 0 || creature_vao_ == 0 ||
      creature_instance_vbo_ == 0 || creature_ebo_ == 0) {
    return;
  }

  const auto parts = collect_visible_creature_parts(
      creatures, crew, old_guard, shadow_focus,
      kShadowDistance + static_cast<float>(kChunkSizeX),
      kCrewVisualDrawDistance, kOldGuardRenderDistance);
  if (parts.empty()) {
    return;
  }

  glBindVertexArray(creature_vao_);
  if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
    prepare_visual_entity_batches(parts, VisualEntityContext::Creature,
                                  creature_part_contexts_scratch_, shadow_focus,
                                  true, false);
  } else {
    glBindBuffer(GL_ARRAY_BUFFER, creature_instance_vbo_);
    const auto instance_bytes = static_cast<GLsizeiptr>(parts.size_bytes());
    if (creature_instance_buffer_bytes_ < instance_bytes) {
      creature_instance_buffer_bytes_ =
          grow_buffer_capacity(creature_instance_buffer_bytes_, instance_bytes,
                               kInitialCreatureInstanceBufferBytes);
    }
    orphan_bound_buffer(GL_ARRAY_BUFFER, creature_instance_buffer_bytes_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, instance_bytes, parts.data());
    frame_uploaded_bytes_ +=
        static_cast<std::uint64_t>(std::max<GLsizeiptr>(instance_bytes, 0));
  }

  glUseProgram(creature_shadow_program_);
  glUniformMatrix4fv(creature_shadow_light_view_projection_, 1, GL_FALSE,
                     glm::value_ptr(light_view_projection));
  if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
    draw_visual_entity_batches(creature_instance_vbo_,
                               creature_instance_buffer_bytes_);
  } else {
    glDrawElementsInstanced(GL_TRIANGLES, creature_template_index_count_,
                            GL_UNSIGNED_INT, nullptr,
                            static_cast<GLsizei>(parts.size()));
    record_triangle_draw(creature_template_index_count_,
                         static_cast<GLsizei>(parts.size()));
  }
}

void Renderer::draw_creatures(
    std::span<const CreatureRenderInstance> creatures,
    std::span<const CrewRenderInstance> crew,
    std::span<const OldGuardRenderInstance> old_guard,
    const glm::mat4 &view_projection, const glm::mat4 &light_view_projection,
    const glm::mat4 &light_view_projection_far, int shadow_cascade_count,
    float shadow_split_distance, float shadow_transition_width,
    const glm::vec3 &camera_position, const glm::vec3 &camera_forward,
    const EnvironmentState &environment, bool player_light_active,
    std::span<const glm::vec4> backrooms_flicker_lights,
    float backrooms_flashlight_strength,
    float super_vision_strength) {
  if ((creatures.empty() && crew.empty() && old_guard.empty() &&
       legendary_world_parts_.empty() &&
       backrooms_jack_parts_.empty() &&
       backrooms_marlow_parts_.empty()) ||
      creature_program_ == 0 || creature_vao_ == 0 ||
      creature_instance_vbo_ == 0 || creature_ebo_ == 0) {
    return;
  }

  const auto parts = collect_visible_creature_parts(
      creatures, crew, old_guard, camera_position, 64.0F,
      kCrewVisualDrawDistance, kOldGuardRenderDistance);

  if (parts.empty()) {
    return;
  }

  glBindVertexArray(creature_vao_);
  if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
    prepare_visual_entity_batches(parts, VisualEntityContext::Creature,
                                  creature_part_contexts_scratch_,
                                  camera_position, false, false);
  } else {
    glBindBuffer(GL_ARRAY_BUFFER, creature_instance_vbo_);
    const auto instance_bytes =
        static_cast<GLsizeiptr>(parts.size() * sizeof(CreaturePartInstance));
    if (creature_instance_buffer_bytes_ < instance_bytes) {
      creature_instance_buffer_bytes_ =
          grow_buffer_capacity(creature_instance_buffer_bytes_, instance_bytes,
                               kInitialCreatureInstanceBufferBytes);
    }
    orphan_bound_buffer(GL_ARRAY_BUFFER, creature_instance_buffer_bytes_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, instance_bytes, parts.data());
    frame_uploaded_bytes_ +=
        static_cast<std::uint64_t>(std::max<GLsizeiptr>(instance_bytes, 0));
  }

  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glUseProgram(creature_program_);
  glUniformMatrix4fv(creature_uniforms_.view_projection, 1, GL_FALSE,
                     glm::value_ptr(view_projection));
  glUniformMatrix4fv(creature_uniforms_.light_view_projection, 1, GL_FALSE,
                     glm::value_ptr(light_view_projection));
  glUniformMatrix4fv(creature_uniforms_.light_view_projection_far, 1, GL_FALSE,
                     glm::value_ptr(light_view_projection_far));
  glUniform3fv(creature_uniforms_.camera_position, 1,
               glm::value_ptr(camera_position));
  glUniform3fv(creature_uniforms_.camera_forward, 1,
               glm::value_ptr(camera_forward));
  glUniform3fv(creature_uniforms_.sun_direction, 1,
               glm::value_ptr(environment.sun_direction));
  glUniform3fv(creature_uniforms_.sun_color, 1,
               glm::value_ptr(environment.sun_color));
  glUniform3fv(creature_uniforms_.ambient_color, 1,
               glm::value_ptr(environment.ambient_color));
  glUniform3fv(creature_uniforms_.fog_color, 1,
               glm::value_ptr(environment.fog_color));
  glUniform3fv(creature_uniforms_.distant_fog_color, 1,
               glm::value_ptr(environment.distant_fog_color));
  glUniform3fv(creature_uniforms_.horizon_glow_color, 1,
               glm::value_ptr(environment.horizon_glow_color));
  glUniform3fv(creature_uniforms_.night_tint_color, 1,
               glm::value_ptr(environment.night_tint_color));
  glUniform1f(creature_uniforms_.daylight_factor, environment.daylight_factor);
  glUniform1f(creature_uniforms_.sun_visibility,
              environment.sun_direction.y > 0.0F ? 1.0F : 0.0F);
  glUniform1f(creature_uniforms_.cloud_intensity, environment.cloud_intensity);
  glUniform1f(creature_uniforms_.cloud_shadow_strength,
              environment.cloud_shadow_strength);
  glUniform1f(creature_uniforms_.atmospheric_scatter_strength,
              environment.atmospheric_scatter_strength);
  glUniform1f(creature_uniforms_.height_fog_density,
              environment.height_fog_density);
  glUniform1f(creature_uniforms_.precipitation_intensity,
              environment.precipitation_intensity);
  glUniform1f(creature_uniforms_.storm_intensity, environment.storm_intensity);
  glUniform1f(creature_uniforms_.lightning_intensity,
              environment.lightning_intensity);
  glUniform1i(creature_uniforms_.atlas, 0);
  glUniform1i(creature_uniforms_.shadow_map, 1);
  glUniform1i(creature_uniforms_.shadow_map_far, 7);
  glUniform1i(creature_uniforms_.shadow_cascade_count, shadow_cascade_count);
  glUniform1f(creature_uniforms_.shadow_split_distance, shadow_split_distance);
  glUniform1f(creature_uniforms_.shadow_transition_width,
              shadow_transition_width);
  glUniform1i(creature_uniforms_.shadows_enabled,
              options_.shadows_enabled ? 1 : 0);
  glUniform1f(creature_uniforms_.time_of_day, environment.time_of_day);
  glUniform1f(creature_uniforms_.player_light_strength,
              player_light_active ? 1.0F : 0.0F);
  glUniform1i(
      creature_uniforms_.enclosed_interior,
      environment.enclosed_interior ? 1 : 0);
  const auto creature_flicker_count =
      std::min(
          backrooms_flicker_lights.size(),
          kMaximumBackroomsFlickerLights);
  glUniform1i(
      creature_uniforms_.backrooms_flicker_count,
      static_cast<GLint>(
          creature_flicker_count));
  if (creature_flicker_count > 0U) {
    glUniform4fv(
        creature_uniforms_.backrooms_flicker_lights,
        static_cast<GLsizei>(
            creature_flicker_count),
        glm::value_ptr(
            backrooms_flicker_lights.front()));
  }
  const auto creature_terminal_fog_range =
      environment.enclosed_interior &&
              backrooms_terminal_fog_snapshot_.valid &&
              backrooms_terminal_fog_snapshot_.range.enabled()
          ? glm::vec2{
                backrooms_terminal_fog_snapshot_.range.start_distance,
                backrooms_terminal_fog_snapshot_.range.end_distance,
            }
          : glm::vec2{-1.0F, -1.0F};
  glUniform2fv(
      creature_uniforms_.interior_fog_range,
      1,
      glm::value_ptr(creature_terminal_fog_range));
  glUniform1f(
      creature_uniforms_.backrooms_flashlight_intensity,
      std::isfinite(backrooms_flashlight_strength)
          ? std::clamp(
                backrooms_flashlight_strength,
                0.0F,
                1.0F)
          : 0.0F);
  glUniform1f(creature_uniforms_.super_vision_strength,
              std::clamp(super_vision_strength, 0.0F, 1.0F));
  const auto local_light_radiance =
      exterior_lantern_radiance(amelie_exterior_lights(), 1.16F);
  glUniform3fv(creature_uniforms_.local_light_radiance, 1,
               glm::value_ptr(local_light_radiance));
  glUniform1i(creature_uniforms_.modern_pipeline,
              is_modern_visual_pipeline(options_.visual_pipeline) ? 1 : 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, creature_atlas_texture_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, shadow_map_);
  glActiveTexture(GL_TEXTURE7);
  glBindTexture(GL_TEXTURE_2D, shadow_map_far_);
  if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
    draw_visual_entity_batches(creature_instance_vbo_,
                               creature_instance_buffer_bytes_);
    glCullFace(GL_BACK);
  } else {
    glDrawElementsInstanced(GL_TRIANGLES, creature_template_index_count_,
                            GL_UNSIGNED_INT, nullptr,
                            static_cast<GLsizei>(parts.size()));
    record_triangle_draw(creature_template_index_count_,
                         static_cast<GLsizei>(parts.size()));
  }
  glActiveTexture(GL_TEXTURE0);
}

auto Renderer::draw_player_viewmodel(
    const PlayerController &player, BlockId held_item,
    const PlayerMusketView &player_musket, const glm::mat4 &view_projection,
    const glm::mat4 &light_view_projection, const glm::vec3 &camera_position,
    const EnvironmentState &environment) -> PlayerViewModelPose {
  if (player.is_dead() || creature_program_ == 0 || viewmodel_vao_ == 0 ||
      viewmodel_instance_vbo_ == 0 || creature_ebo_ == 0 ||
      player_atlas_texture_ == 0) {
    return {};
  }

  const auto resolved_held_item = resolve_renderer_viewmodel_item(
      held_item, !legendary_viewmodel_parts_.empty());
  auto viewmodel =
      build_player_viewmodel_parts(player, resolved_held_item, player_musket);
  if (!legendary_viewmodel_parts_.empty()) {
    viewmodel.parts.insert(viewmodel.parts.end(),
                           legendary_viewmodel_parts_.begin(),
                           legendary_viewmodel_parts_.end());
  }
  if (viewmodel.empty()) {
    return {};
  }

  glBindVertexArray(viewmodel_vao_);
  if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
    prepare_visual_entity_batches(
        viewmodel.parts, VisualEntityContext::PlayerViewModel,
        std::span<const VisualEntityContext>{}, camera_position, false, true);
  } else {
    glBindBuffer(GL_ARRAY_BUFFER, viewmodel_instance_vbo_);
    const auto instance_bytes = static_cast<GLsizeiptr>(
        viewmodel.parts.size() * sizeof(CreaturePartInstance));
    if (viewmodel_instance_buffer_bytes_ < instance_bytes) {
      viewmodel_instance_buffer_bytes_ =
          grow_buffer_capacity(viewmodel_instance_buffer_bytes_, instance_bytes,
                               kInitialCreatureInstanceBufferBytes);
    }
    orphan_bound_buffer(GL_ARRAY_BUFFER, viewmodel_instance_buffer_bytes_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, instance_bytes, viewmodel.parts.data());
    frame_uploaded_bytes_ +=
        static_cast<std::uint64_t>(std::max<GLsizeiptr>(instance_bytes, 0));
  }

  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glUseProgram(creature_program_);
  glUniformMatrix4fv(creature_uniforms_.view_projection, 1, GL_FALSE,
                     glm::value_ptr(view_projection));
  glUniformMatrix4fv(creature_uniforms_.light_view_projection, 1, GL_FALSE,
                     glm::value_ptr(light_view_projection));
  glUniform3fv(creature_uniforms_.camera_position, 1,
               glm::value_ptr(camera_position));
  const auto viewmodel_camera_forward =
      player.look_direction();
  glUniform3fv(
      creature_uniforms_.camera_forward,
      1,
      glm::value_ptr(viewmodel_camera_forward));
  glUniform3fv(creature_uniforms_.sun_direction, 1,
               glm::value_ptr(environment.sun_direction));
  glUniform3fv(creature_uniforms_.sun_color, 1,
               glm::value_ptr(environment.sun_color));
  const auto viewmodel_ambient =
      glm::max(environment.ambient_color, glm::vec3{0.22F, 0.22F, 0.24F});
  const auto viewmodel_fog =
      glm::mix(environment.fog_color, viewmodel_ambient, 0.80F);
  glUniform3fv(creature_uniforms_.ambient_color, 1,
               glm::value_ptr(viewmodel_ambient));
  glUniform3fv(creature_uniforms_.fog_color, 1, glm::value_ptr(viewmodel_fog));
  glUniform3fv(creature_uniforms_.distant_fog_color, 1,
               glm::value_ptr(viewmodel_fog));
  glUniform3fv(creature_uniforms_.horizon_glow_color, 1,
               glm::value_ptr(environment.horizon_glow_color));
  glUniform3fv(creature_uniforms_.night_tint_color, 1,
               glm::value_ptr(environment.night_tint_color));
  glUniform1f(creature_uniforms_.daylight_factor,
              std::max(environment.daylight_factor, 0.20F));
  glUniform1f(creature_uniforms_.sun_visibility,
              environment.sun_direction.y > 0.0F ? 1.0F : 0.0F);
  glUniform1f(creature_uniforms_.cloud_intensity, environment.cloud_intensity);
  glUniform1f(creature_uniforms_.cloud_shadow_strength,
              environment.cloud_shadow_strength);
  glUniform1f(creature_uniforms_.atmospheric_scatter_strength,
              environment.atmospheric_scatter_strength);
  glUniform1f(creature_uniforms_.height_fog_density,
              environment.height_fog_density);
  glUniform1f(creature_uniforms_.precipitation_intensity,
              environment.precipitation_intensity);
  glUniform1f(creature_uniforms_.storm_intensity, environment.storm_intensity);
  glUniform1f(creature_uniforms_.lightning_intensity,
              environment.lightning_intensity);
  glUniform1i(creature_uniforms_.atlas, 0);
  glUniform1i(creature_uniforms_.shadow_map, 1);
  glUniform1i(creature_uniforms_.shadows_enabled, 0);
  glUniform1f(creature_uniforms_.time_of_day, environment.time_of_day);
  glUniform1f(creature_uniforms_.player_light_strength, 0.0F);
  // Je neutralise explicitement les uniforms propres au monde pour que le
  // viewmodel ne depende jamais du dernier draw de Jack ou d'une autre session.
  glUniform1i(
      creature_uniforms_.enclosed_interior,
      0);
  glUniform1i(
      creature_uniforms_.backrooms_flicker_count,
      0);
  glUniform2f(
      creature_uniforms_.interior_fog_range,
      -1.0F,
      -1.0F);
  glUniform1f(
      creature_uniforms_.backrooms_flashlight_intensity,
      0.0F);
  glUniform1f(creature_uniforms_.super_vision_strength, 0.0F);
  const auto local_light_radiance =
      exterior_lantern_radiance(amelie_exterior_lights(), 1.16F);
  glUniform3fv(creature_uniforms_.local_light_radiance, 1,
               glm::value_ptr(local_light_radiance));
  glUniform1i(creature_uniforms_.modern_pipeline,
              is_modern_visual_pipeline(options_.visual_pipeline) ? 1 : 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, player_atlas_texture_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, shadow_map_);
  if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
    draw_visual_entity_batches(viewmodel_instance_vbo_,
                               viewmodel_instance_buffer_bytes_);
  } else {
    glDrawElementsInstanced(GL_TRIANGLES, creature_template_index_count_,
                            GL_UNSIGNED_INT, nullptr,
                            static_cast<GLsizei>(viewmodel.parts.size()));
    record_triangle_draw(creature_template_index_count_,
                         static_cast<GLsizei>(viewmodel.parts.size()));
  }
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glActiveTexture(GL_TEXTURE0);
  return viewmodel.pose;
}

void Renderer::draw_block_break_overlay(const World &world,
                                        const PlayerController &player) {
  const auto &break_progress = player.block_break_progress();
  if (!break_progress.active || break_progress.progress <= 0.0F) {
    block_break_overlay_mesh_.opaque_index_count = 0;
    return;
  }

  upload_block_break_overlay_mesh(world, break_progress);
  if (block_break_overlay_mesh_.vao == 0 ||
      block_break_overlay_mesh_.opaque_index_count == 0) {
    return;
  }

  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glUseProgram(world_program_);
  glBindVertexArray(block_break_overlay_mesh_.vao);
  glDrawElements(GL_TRIANGLES, block_break_overlay_mesh_.opaque_index_count,
                 GL_UNSIGNED_INT, nullptr);
  record_triangle_draw(block_break_overlay_mesh_.opaque_index_count);
  glDepthMask(GL_TRUE);
}

void Renderer::draw_hotbar(const PlayerController &player,
                           const HotbarState &hotbar,
                           const PlayerProgressionState &progression,
                           const EnvironmentState & /*environment*/, int width,
                           int height) {
  if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 ||
      hud_vbo_ == 0) {
    return;
  }

  const auto &player_state = player.state();
  const auto max_health = std::max(player.max_health(), 0.001F);
  const auto max_air = std::max(player.max_air_seconds(), 0.001F);
  const auto damage_flash = std::max(
      glm::clamp(player_state.hurt_timer / 0.35F, 0.0F, 1.0F) * 0.32F,
      glm::clamp((max_health - player_state.health) / max_health, 0.0F, 1.0F) *
          0.18F);
  const auto air_visible = player_state.head_underwater ||
                           player_state.air_seconds < max_air - 0.05F;
  const auto experience_hud = make_progression_experience_hud_snapshot(
      progression, progression_runtime_hud_view_.aggregated_experience_gain);
  const auto level_progress_step = std::clamp(
      quantize_hud_value(experience_hud.progress_ratio, 128.0F), 0, 128);
  const auto visible_level_progress =
      static_cast<float>(level_progress_step) / 128.0F;
  const auto hud_layout = build_gameplay_hud_layout(
      width, height, hotbar, player_state.health, max_health,
      player_state.air_seconds, max_air, air_visible, visible_level_progress);

  HotbarHudCacheKey cache_key{};
  cache_key.hotbar = hotbar;
  cache_key.width = width;
  cache_key.height = height;
  cache_key.health_steps = quantize_hud_value(player_state.health, 16.0F);
  cache_key.air_steps = quantize_hud_value(player_state.air_seconds, 64.0F);
  cache_key.damage_flash_step = quantize_hud_value(damage_flash, 128.0F);
  cache_key.player_level = experience_hud.level;
  cache_key.current_experience = experience_hud.current_experience;
  cache_key.next_level_experience = experience_hud.next_level_experience;
  cache_key.experience_gain = experience_hud.aggregated_experience_gain;
  cache_key.level_progress_step = level_progress_step;
  cache_key.maximum_level = experience_hud.maximum_level;
  cache_key.air_visible = air_visible;
  cache_key.underwater = player_state.head_underwater;

  auto &cache = hotbar_cache_;
  auto &vertices = cache.vertices;
  const auto needs_rebuild = !cache.valid || cache.key != cache_key;
  if (needs_rebuild) {
    cache.valid = true;
    cache.key = cache_key;
    vertices.clear();
    vertices.reserve(24576U);

    const auto viewport_width = static_cast<float>(width);
    const auto viewport_height = static_cast<float>(height);
    const auto modern_hud = is_modern_visual_pipeline(options_.visual_pipeline);
    const auto draw_text_bottom =
        [&](float x, float bottom, float pixel_size, std::string_view text,
            const HudColor &color, bool centered = false) {
          append_pixel_text_bottom_left(vertices, viewport_width,
                                        viewport_height, x + pixel_size,
                                        bottom - pixel_size, pixel_size, text,
                                        {0.0F, 0.0F, 0.0F, 0.56F}, centered);
          append_pixel_text_bottom_left(vertices, viewport_width,
                                        viewport_height, x, bottom, pixel_size,
                                        text, color, centered);
        };

    if (cache_key.underwater &&
        options_.visual_pipeline == VisualPipeline::LegacyVoxel) {
      // Je conserve exactement le voile historique du pipeline Legacy.
      const auto overlay_edge = std::clamp(
          std::min(viewport_width, viewport_height) * 0.17F, 72.0F, 180.0F);
      append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                               0.0F, viewport_width, viewport_height,
                               {0.03F, 0.18F, 0.25F, 0.18F});
      append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                               0.0F, viewport_width, overlay_edge,
                               {0.12F, 0.42F, 0.46F, 0.08F});
      append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                               viewport_height - overlay_edge, viewport_width,
                               overlay_edge, {0.02F, 0.09F, 0.15F, 0.16F});
      append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                               0.0F, overlay_edge, viewport_height,
                               {0.02F, 0.11F, 0.17F, 0.10F});
      append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                               viewport_width - overlay_edge, 0.0F,
                               overlay_edge, viewport_height,
                               {0.02F, 0.11F, 0.17F, 0.10F});
    }

    if (damage_flash > 0.0F) {
      const auto edge_size = std::clamp(
          std::min(viewport_width, viewport_height) * 0.09F, 28.0F, 72.0F);
      append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                               0.0F, viewport_width, viewport_height,
                               {0.44F, 0.03F, 0.05F, damage_flash * 0.12F});
      append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                               0.0F, viewport_width, edge_size,
                               {0.48F, 0.04F, 0.05F, damage_flash});
      append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                               viewport_height - edge_size, viewport_width,
                               edge_size, {0.48F, 0.04F, 0.05F, damage_flash});
      append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                               0.0F, edge_size, viewport_height,
                               {0.48F, 0.04F, 0.05F, damage_flash * 0.9F});
      append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                               viewport_width - edge_size, 0.0F, edge_size,
                               viewport_height,
                               {0.48F, 0.04F, 0.05F, damage_flash * 0.9F});
    }

    const auto dock_palette = modern_hud ? make_modern_neutral_panel_palette()
                                         : make_slate_panel_palette();
    const auto rail_palette =
        modern_hud ? make_modern_glass_panel_palette(
                         {0.83F, 0.67F, 0.34F, 1.0F}, 0.16F)
                   : make_warm_panel_palette({0.70F, 0.56F, 0.30F, 1.0F});
    const auto heart_panel_palette =
        modern_hud ? make_modern_glass_panel_palette(
                         {0.94F, 0.30F, 0.38F, 1.0F}, 0.18F)
                   : make_warm_panel_palette({0.90F, 0.28F, 0.32F, 1.0F});
    const auto bubble_panel_palette =
        modern_hud ? make_modern_glass_panel_palette(
                         {0.42F, 0.80F, 0.98F, 1.0F}, 0.16F)
                   : make_warm_panel_palette({0.42F, 0.80F, 0.98F, 1.0F});
    const auto level_panel_palette =
        modern_hud ? make_modern_glass_panel_palette(
                         {0.88F, 0.72F, 0.35F, 1.0F}, 0.17F)
                   : make_warm_panel_palette({0.78F, 0.66F, 0.36F, 1.0F});

    if (modern_hud) {
      append_modern_panel_top_left(
          vertices, viewport_width, viewport_height, hud_layout.level.x,
          hud_layout.level.y, hud_layout.level.width, hud_layout.level.height,
          3.0F, level_panel_palette, true);
    } else {
      append_stylized_panel_top_left(
          vertices, viewport_width, viewport_height, hud_layout.level.x,
          hud_layout.level.y, hud_layout.level.width, hud_layout.level.height,
          3.0F, level_panel_palette, true);
    }
    append_hud_rect_top_left(
        vertices, viewport_width, viewport_height, hud_layout.level.progress_x,
        hud_layout.level.progress_y, hud_layout.level.progress_width,
        hud_layout.level.progress_height, {0.03F, 0.04F, 0.05F, 0.58F});
    append_hud_rect_top_left(
        vertices, viewport_width, viewport_height, hud_layout.level.progress_x,
        hud_layout.level.progress_y, hud_layout.level.progress_fill_width,
        hud_layout.level.progress_height, {0.97F, 0.78F, 0.35F, 0.92F});
    append_hud_rect_top_left(
        vertices, viewport_width, viewport_height, hud_layout.level.progress_x,
        hud_layout.level.progress_y, hud_layout.level.progress_fill_width,
        std::max(1.0F, hud_layout.level.progress_height * 0.32F),
        {1.0F, 0.96F, 0.74F, 0.24F});
    auto level_label = std::string("LV") + std::to_string(experience_hud.level);
    if (experience_hud.maximum_level) {
      level_label += " NIVEAU MAX";
    } else {
      level_label +=
          " " + std::to_string(experience_hud.current_experience) + "/" +
          std::to_string(experience_hud.next_level_experience) + " XP";
    }
    if (experience_hud.aggregated_experience_gain > 0ULL) {
      level_label += " +" +
                     std::to_string(experience_hud.aggregated_experience_gain) +
                     " XP";
    }
    draw_text_bottom(hud_layout.level.text_center_x,
                     viewport_height - hud_layout.level.text_y -
                         hud_layout.level.text_pixel_size * 7.0F,
                     hud_layout.level.text_pixel_size, level_label,
                     {0.98F, 0.96F, 0.88F, 0.98F}, true);

    append_hud_shadow_bottom_left(
        vertices, viewport_width, viewport_height, hud_layout.hotbar_panel_x,
        hud_layout.hotbar_panel_bottom, hud_layout.hotbar_panel_width,
        hud_layout.hotbar_panel_height, 14.0F, {0.0F, 0.0F, 0.0F, 0.24F});
    if (modern_hud) {
      append_modern_panel_bottom_left(
          vertices, viewport_width, viewport_height, hud_layout.hotbar_panel_x,
          hud_layout.hotbar_panel_bottom, hud_layout.hotbar_panel_width,
          hud_layout.hotbar_panel_height, 4.0F, dock_palette, false);
      append_modern_panel_bottom_left(
          vertices, viewport_width, viewport_height, hud_layout.hotbar_rail_x,
          hud_layout.hotbar_rail_bottom, hud_layout.hotbar_rail_width,
          hud_layout.hotbar_rail_height, 2.0F, rail_palette, false);
    } else {
      append_stylized_panel_bottom_left(
          vertices, viewport_width, viewport_height, hud_layout.hotbar_panel_x,
          hud_layout.hotbar_panel_bottom, hud_layout.hotbar_panel_width,
          hud_layout.hotbar_panel_height, 4.0F, dock_palette, false);
      append_stylized_panel_bottom_left(
          vertices, viewport_width, viewport_height, hud_layout.hotbar_rail_x,
          hud_layout.hotbar_rail_bottom, hud_layout.hotbar_rail_width,
          hud_layout.hotbar_rail_height, 2.0F, rail_palette, false);
    }
    append_hud_rect(vertices, viewport_width, viewport_height,
                    hud_layout.hotbar_panel_x + 12.0F,
                    hud_layout.hotbar_panel_bottom +
                        hud_layout.hotbar_panel_height - 8.0F,
                    std::max(0.0F, hud_layout.hotbar_panel_width - 24.0F), 2.0F,
                    {1.0F, 1.0F, 1.0F, 0.06F});

    if (modern_hud) {
      append_modern_panel_bottom_left(
          vertices, viewport_width, viewport_height, hud_layout.hearts_panel_x,
          hud_layout.hearts_panel_bottom, hud_layout.hearts_panel_width,
          hud_layout.hearts_panel_height, 3.0F, heart_panel_palette, false);
    } else {
      append_stylized_panel_bottom_left(
          vertices, viewport_width, viewport_height, hud_layout.hearts_panel_x,
          hud_layout.hearts_panel_bottom, hud_layout.hearts_panel_width,
          hud_layout.hearts_panel_height, 3.0F, heart_panel_palette, false);
    }
    if (hud_layout.air_visible) {
      if (modern_hud) {
        append_modern_panel_bottom_left(
            vertices, viewport_width, viewport_height,
            hud_layout.bubbles_panel_x, hud_layout.bubbles_panel_bottom,
            hud_layout.bubbles_panel_width, hud_layout.bubbles_panel_height,
            3.0F, bubble_panel_palette, false);
      } else {
        append_stylized_panel_bottom_left(
            vertices, viewport_width, viewport_height,
            hud_layout.bubbles_panel_x, hud_layout.bubbles_panel_bottom,
            hud_layout.bubbles_panel_width, hud_layout.bubbles_panel_height,
            3.0F, bubble_panel_palette, false);
      }
    }

    for (const auto &heart : hud_layout.hearts) {
      append_heart_glyph_bottom_left(vertices, viewport_width, viewport_height,
                                     heart);
    }
    if (hud_layout.air_visible) {
      for (const auto &bubble : hud_layout.bubbles) {
        append_bubble_glyph_bottom_left(vertices, viewport_width,
                                        viewport_height, bubble);
      }
    }

    const auto stack_pixel_size = std::max(
        2.0F,
        static_cast<float>(std::floor(hud_layout.hotbar.slot_size / 18.0F)));
    for (const auto &slot : hud_layout.slots) {
      const auto palette =
          build_slot_palette(slot.slot, slot.is_selected, false, true);
      if (modern_hud) {
        append_modern_slot_bottom_left(vertices, viewport_width,
                                       viewport_height, slot.x, slot.bottom,
                                       slot.size, palette, slot.has_icon);
      } else {
        append_stylized_slot_bottom_left(vertices, viewport_width,
                                         viewport_height, slot.x, slot.bottom,
                                         slot.size, palette, slot.has_icon);
      }

      if (!slot.has_icon) {
        continue;
      }

      const auto icon_texture_mode = hud_item_texture_mode(slot.slot.block_id);
      append_hud_quad(vertices, viewport_width, viewport_height, slot.icon_x,
                      slot.icon_bottom, slot.icon_size, slot.icon_size,
                      {1.0F, 1.0F, 1.0F, slot.is_selected ? 1.0F : 0.98F},
                      icon_texture_mode > 2.5F
                          ? std::array<float, 4>{0.0F, 1.0F, 1.0F, 0.0F}
                          : atlas_uv_rect(slot.icon_tile),
                      icon_texture_mode);
      if (slot.show_stack_count) {
        append_stack_count_bottom_left(
            vertices, viewport_width, viewport_height, slot.count_right_x,
            slot.count_bottom, stack_pixel_size, slot.slot.count);
      }
    }

    const auto selected_label =
        item_stack_display_label(hotbar.selected_slot());
    if (!selected_label.empty()) {
      const auto label_padding_x =
          std::max(10.0F, hud_layout.label.pixel_size * 3.0F);
      const auto label_padding_y =
          std::max(6.0F, hud_layout.label.pixel_size * 2.0F);
      const auto label_width =
          measure_pixel_text(selected_label, hud_layout.label.pixel_size) +
          label_padding_x * 2.0F;
      const auto label_height =
          hud_layout.label.height + label_padding_y * 2.0F;
      const auto label_x = hud_layout.label.center_x - label_width * 0.5F;
      const auto label_y = bottom_to_top_left_y(
          viewport_height, hud_layout.label.bottom, label_height);
      const auto label_accent =
          ui_material_accent(hotbar.selected_slot().block_id);
      const auto label_palette =
          modern_hud ? make_modern_glass_panel_palette(label_accent, 0.18F)
                     : make_warm_panel_palette(label_accent);
      if (modern_hud) {
        append_modern_panel_top_left(vertices, viewport_width, viewport_height,
                                     label_x, label_y, label_width,
                                     label_height, 3.0F, label_palette, true);
      } else {
        append_stylized_panel_top_left(
            vertices, viewport_width, viewport_height, label_x, label_y,
            label_width, label_height, 3.0F, label_palette, true);
      }
      draw_text_bottom(hud_layout.label.center_x,
                       hud_layout.label.bottom + label_padding_y - 1.0F,
                       hud_layout.label.pixel_size, selected_label,
                       {0.98F, 0.98F, 0.96F, 0.98F}, true);
    }
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glUseProgram(hud_program_);
  bind_hud_textures();

  upload_hud_vertices(vertices);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_backrooms_jack_screamer(
    const BackroomsJackRenderView &view,
    float absolute_time_seconds,
    int width,
    int height) {
  if (!view.jumpscare ||
      width <= 0 ||
      height <= 0 ||
      hud_program_ == 0 ||
      hud_vao_ == 0 ||
      hud_vbo_ == 0 ||
      backrooms_jack_screamer_texture_ == 0 ||
      backrooms_jack_screamer_width_ == 0U ||
      backrooms_jack_screamer_height_ == 0U) {
    return;
  }

  const auto viewport_width =
      static_cast<float>(width);
  const auto viewport_height =
      static_cast<float>(height);
  const auto safe_time =
      std::isfinite(absolute_time_seconds)
          ? absolute_time_seconds
          : 0.0F;
  const auto image_aspect =
      static_cast<float>(
          backrooms_jack_screamer_width_) /
      static_cast<float>(
          backrooms_jack_screamer_height_);
  const auto viewport_aspect =
      viewport_width / viewport_height;

  auto u_min = 0.0F;
  auto u_max = 1.0F;
  auto v_min = 0.0F;
  auto v_max = 1.0F;
  if (viewport_aspect > image_aspect) {
    const auto visible_v =
        glm::clamp(
            image_aspect / viewport_aspect,
            0.01F,
            1.0F);
    v_min = (1.0F - visible_v) * 0.5F;
    v_max = 1.0F - v_min;
  } else {
    const auto visible_u =
        glm::clamp(
            viewport_aspect / image_aspect,
            0.01F,
            1.0F);
    u_min = (1.0F - visible_u) * 0.5F;
    u_max = 1.0F - u_min;
  }

  // Je donne au choc une vibration tres courte sans jamais decouvrir les
  // bords de l'image : Jack remplit toujours entierement le cadre.
  const auto jitter_x =
      std::sin(safe_time * 61.0F) *
      viewport_width *
      0.006F;
  const auto jitter_y =
      std::sin(safe_time * 47.0F + 1.8F) *
      viewport_height *
      0.006F;
  const auto pulse =
      0.5F +
      0.5F *
          std::sin(safe_time * 31.0F);
  const auto expansion =
      1.055F + pulse * 0.025F;
  const auto draw_width =
      viewport_width * expansion;
  const auto draw_height =
      viewport_height * expansion;
  const auto draw_x =
      (viewport_width - draw_width) * 0.5F +
      jitter_x;
  const auto draw_y =
      (viewport_height - draw_height) * 0.5F +
      jitter_y;

  auto &vertices =
      backrooms_jack_screamer_vertices_scratch_;
  vertices.clear();
  vertices.reserve(42U);
  append_hud_rect_top_left(
      vertices,
      viewport_width,
      viewport_height,
      0.0F,
      0.0F,
      viewport_width,
      viewport_height,
      {0.0F, 0.0F, 0.0F, 1.0F});
  append_hud_quad_top_left(
      vertices,
      viewport_width,
      viewport_height,
      draw_x,
      draw_y,
      draw_width,
      draw_height,
      {
          1.0F,
          0.94F + pulse * 0.06F,
          0.90F + pulse * 0.08F,
          1.0F,
      },
      {
          u_min,
          v_max,
          u_max,
          v_min,
      },
      64.0F);

  const auto red_flash =
      0.06F +
      std::max(
          std::sin(safe_time * 39.0F),
          0.0F) *
          0.11F;
  append_hud_rect_top_left(
      vertices,
      viewport_width,
      viewport_height,
      0.0F,
      0.0F,
      viewport_width,
      viewport_height,
      {0.34F, 0.0F, 0.0F, red_flash});

  const auto edge_width =
      viewport_width * 0.085F;
  const auto edge_height =
      viewport_height * 0.085F;
  append_hud_rect_top_left(
      vertices,
      viewport_width,
      viewport_height,
      0.0F,
      0.0F,
      edge_width,
      viewport_height,
      {0.0F, 0.0F, 0.0F, 0.58F});
  append_hud_rect_top_left(
      vertices,
      viewport_width,
      viewport_height,
      viewport_width - edge_width,
      0.0F,
      edge_width,
      viewport_height,
      {0.0F, 0.0F, 0.0F, 0.58F});
  append_hud_rect_top_left(
      vertices,
      viewport_width,
      viewport_height,
      0.0F,
      0.0F,
      viewport_width,
      edge_height,
      {0.0F, 0.0F, 0.0F, 0.45F});
  append_hud_rect_top_left(
      vertices,
      viewport_width,
      viewport_height,
      0.0F,
      viewport_height - edge_height,
      viewport_width,
      edge_height,
      {0.0F, 0.0F, 0.0F, 0.45F});

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(
      GL_SRC_ALPHA,
      GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(hud_program_);
  bind_hud_textures();
  upload_hud_vertices(vertices);
  glDrawArrays(
      GL_TRIANGLES,
      0,
      static_cast<GLsizei>(vertices.size()));
  record_triangle_draw(
      static_cast<GLsizei>(vertices.size()));
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_backrooms_marlow_screamer(
    float absolute_time_seconds,
    int width,
    int height) {
  if (width <= 0 || height <= 0 ||
      hud_program_ == 0 || hud_vao_ == 0 || hud_vbo_ == 0 ||
      backrooms_marlow_screamer_texture_ == 0 ||
      backrooms_marlow_screamer_width_ == 0U ||
      backrooms_marlow_screamer_height_ == 0U) {
    return;
  }

  const auto viewport_width = static_cast<float>(width);
  const auto viewport_height = static_cast<float>(height);
  const auto safe_time = std::isfinite(absolute_time_seconds)
                             ? absolute_time_seconds
                             : 0.0F;
  const auto image_aspect =
      static_cast<float>(backrooms_marlow_screamer_width_) /
      static_cast<float>(backrooms_marlow_screamer_height_);
  const auto viewport_aspect = viewport_width / viewport_height;
  auto u_min = 0.0F;
  auto u_max = 1.0F;
  auto v_min = 0.0F;
  auto v_max = 1.0F;
  if (viewport_aspect > image_aspect) {
    const auto visible_v = glm::clamp(
        image_aspect / viewport_aspect, 0.01F, 1.0F);
    v_min = (1.0F - visible_v) * 0.5F;
    v_max = 1.0F - v_min;
  } else {
    const auto visible_u = glm::clamp(
        viewport_aspect / image_aspect, 0.01F, 1.0F);
    u_min = (1.0F - visible_u) * 0.5F;
    u_max = 1.0F - u_min;
  }

  const auto pulse = 0.5F + 0.5F * std::sin(safe_time * 37.0F);
  const auto expansion = 1.07F + pulse * 0.035F;
  const auto draw_width = viewport_width * expansion;
  const auto draw_height = viewport_height * expansion;
  const auto draw_x = (viewport_width - draw_width) * 0.5F +
      std::sin(safe_time * 73.0F) * viewport_width * 0.009F;
  const auto draw_y = (viewport_height - draw_height) * 0.5F +
      std::sin(safe_time * 59.0F + 1.2F) * viewport_height * 0.012F;

  auto &vertices = backrooms_marlow_screamer_vertices_scratch_;
  vertices.clear();
  vertices.reserve(72U);
  append_hud_rect_top_left(
      vertices, viewport_width, viewport_height,
      0.0F, 0.0F, viewport_width, viewport_height,
      {0.0F, 0.015F, 0.025F, 1.0F});
  append_hud_quad_top_left(
      vertices, viewport_width, viewport_height,
      draw_x, draw_y, draw_width, draw_height,
      {0.84F + pulse * 0.12F, 0.96F, 1.0F, 1.0F},
      {u_min, v_max, u_max, v_min}, 64.0F);

  // Je superpose trois lames d'eau irregulieres. Elles cassent brievement le
  // visage sans masquer ses grands yeux blancs ni introduire un nouveau shader.
  for (auto band = 0; band < 3; ++band) {
    const auto band_f = static_cast<float>(band);
    const auto band_height = viewport_height * (0.035F + band_f * 0.007F);
    const auto band_y = viewport_height * (0.24F + band_f * 0.22F) +
        std::sin(safe_time * (43.0F + band_f * 7.0F) + band_f) *
            viewport_height * 0.018F;
    append_hud_rect_top_left(
        vertices, viewport_width, viewport_height,
        0.0F, band_y, viewport_width, band_height,
        {0.02F, 0.22F, 0.30F, 0.13F + pulse * 0.08F});
  }
  append_hud_rect_top_left(
      vertices, viewport_width, viewport_height,
      0.0F, 0.0F, viewport_width, viewport_height,
      {0.0F, 0.08F, 0.12F, 0.08F + pulse * 0.08F});

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(hud_program_);
  bind_hud_textures();
  // Le shader HUD possede deja le sampler du screamer sur l'unite 3. Je ne
  // change que la texture liee pour garder le contrat GPU historique intact.
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, backrooms_marlow_screamer_texture_);
  glActiveTexture(GL_TEXTURE0);
  upload_hud_vertices(vertices);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  record_triangle_draw(static_cast<GLsizei>(vertices.size()));
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, backrooms_jack_screamer_texture_);
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_backrooms_flashlight_hud(
    const BackroomsFlashlightHudView &view,
    int width, int height) {
  if (!view.visible || width <= 0 || height <= 0 ||
      hud_program_ == 0 || hud_vao_ == 0 ||
      hud_vbo_ == 0) {
    return;
  }

  auto &vertices =
      backrooms_flashlight_hud_vertices_scratch_;
  append_backrooms_flashlight_hud_geometry(
      vertices,
      view,
      width,
      height);
  if (vertices.empty()) {
    return;
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(
      GL_SRC_ALPHA,
      GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(hud_program_);
  bind_hud_textures();
  upload_hud_vertices(vertices);
  glDrawArrays(
      GL_TRIANGLES,
      0,
      static_cast<GLsizei>(
          vertices.size()));
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_maritime_hud(const MaritimeHudView &maritime_hud, int width,
                                 int height) {
  if (!maritime_hud.visible || width <= 0 || height <= 0 || hud_program_ == 0 ||
      hud_vao_ == 0 || hud_vbo_ == 0) {
    return;
  }

  const auto viewport_width = static_cast<float>(width);
  const auto viewport_height = static_cast<float>(height);
  const auto layout = build_maritime_hud_layout(
      width, height, maritime_hud.visible, maritime_hud.hunger_ratio,
      maritime_hud.thirst_ratio, maritime_hud.stamina_ratio,
      maritime_hud.fishing_active, maritime_hud.fishing_ratio,
      maritime_hud.crew_focus_visible, maritime_hud.crew_progress_ratio);

  MaritimeHudCacheKey cache_key{};
  cache_key.width = width;
  cache_key.height = height;
  cache_key.visible = maritime_hud.visible;
  cache_key.on_ship = maritime_hud.on_ship;
  cache_key.fishing_active = maritime_hud.fishing_active;
  cache_key.danger = maritime_hud.danger;
  cache_key.moored = maritime_hud.moored;
  cache_key.departing = maritime_hud.departing;
  cache_key.hunger_step = quantize_hud_value(maritime_hud.hunger_ratio, 100.0F);
  cache_key.thirst_step = quantize_hud_value(maritime_hud.thirst_ratio, 100.0F);
  cache_key.stamina_step =
      quantize_hud_value(maritime_hud.stamina_ratio, 100.0F);
  cache_key.fishing_step =
      quantize_hud_value(maritime_hud.fishing_ratio, 100.0F);
  cache_key.ship_distance_step =
      quantize_hud_value(maritime_hud.ship_distance, 0.2F);
  cache_key.ship_speed_step =
      quantize_hud_value(maritime_hud.ship_speed, 10.0F);
  cache_key.departure_seconds_step = static_cast<int>(
      std::ceil(std::max(0.0F, maritime_hud.departure_seconds_remaining)));
  cache_key.food_rations = maritime_hud.food_rations;
  cache_key.water_flasks = maritime_hud.water_flasks;
  cache_key.fish = maritime_hud.fish;
  cache_key.crew_focus_visible = maritime_hud.crew_focus_visible;
  cache_key.crew_moving = maritime_hud.crew_moving;
  cache_key.crew_blocked = maritime_hud.crew_blocked;
  cache_key.crew_knocked_out = maritime_hud.crew_knocked_out;
  cache_key.crew_has_progress = maritime_hud.crew_has_progress;
  cache_key.crew_role = maritime_hud.crew_role;
  cache_key.crew_activity = maritime_hud.crew_activity;
  cache_key.crew_cargo = maritime_hud.crew_cargo;
  cache_key.crew_destination = maritime_hud.crew_destination;
  cache_key.crew_progress_step =
      quantize_hud_value(maritime_hud.crew_progress_ratio, 100.0F);
  cache_key.crew_health_step =
      quantize_hud_value(maritime_hud.crew_health_ratio, 100.0F);
  cache_key.crew_distance_step =
      quantize_hud_value(maritime_hud.crew_distance, 10.0F);

  auto &cache = maritime_cache_;
  auto &vertices = cache.vertices;
  const auto needs_rebuild = !cache.valid || cache.key != cache_key;
  if (needs_rebuild) {
    cache.valid = true;
    cache.key = cache_key;
    vertices.clear();
    vertices.reserve(4096U);

    const auto accent = maritime_hud.danger
                            ? HudColor{0.96F, 0.38F, 0.28F, 1.0F}
                            : HudColor{0.38F, 0.78F, 1.0F, 1.0F};
    auto palette = make_warm_panel_palette(accent);
    palette.fill = {0.04F, 0.08F, 0.10F, 0.84F};
    palette.frame = {0.04F, 0.12F, 0.16F, 0.94F};
    palette.trim = hud_with_alpha(accent, 0.34F);

    const auto draw_text = [&](float x, float y, float pixel_size,
                               std::string_view text, const HudColor &color,
                               bool centered = false) {
      append_pixel_text(vertices, viewport_width, viewport_height,
                        x + pixel_size, y + pixel_size, pixel_size, text,
                        {0.0F, 0.0F, 0.0F, 0.45F}, centered);
      append_pixel_text(vertices, viewport_width, viewport_height, x, y,
                        pixel_size, text, color, centered);
    };
    const auto draw_bar = [&](const MaritimeHudBarLayout &bar,
                              const HudColor &fill_color) {
      append_hud_frame_top_left(vertices, viewport_width, viewport_height,
                                bar.x, bar.y, bar.width, bar.height, 1.5F,
                                {0.0F, 0.0F, 0.0F, 0.72F},
                                {0.02F, 0.03F, 0.04F, 0.72F});
      append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                               bar.x + 2.0F, bar.y + 2.0F,
                               std::max(0.0F, bar.fill_width - 4.0F),
                               std::max(1.0F, bar.height - 4.0F), fill_color);
    };

    append_stylized_panel_top_left(vertices, viewport_width, viewport_height,
                                   layout.panel_x, layout.panel_y,
                                   layout.panel_width, layout.panel_height,
                                   3.0F, palette, true);

    draw_text(layout.title_x, layout.title_y, layout.text_pixel_size,
              "AVENTURE EN MER", {0.94F, 0.98F, 1.0F, 0.98F});
    draw_bar(layout.hunger_bar, {0.86F, 0.48F, 0.22F, 0.92F});
    draw_bar(layout.thirst_bar, {0.30F, 0.70F, 1.0F, 0.92F});
    draw_bar(layout.stamina_bar, {0.40F, 0.92F, 0.52F, 0.92F});
    draw_text(layout.hunger_bar.x, layout.hunger_bar.y - 10.0F,
              layout.body_pixel_size, "FAIM", {0.88F, 0.88F, 0.84F, 0.92F});
    draw_text(layout.thirst_bar.x, layout.thirst_bar.y - 10.0F,
              layout.body_pixel_size, "SOIF", {0.88F, 0.88F, 0.84F, 0.92F});
    draw_text(layout.stamina_bar.x, layout.stamina_bar.y - 10.0F,
              layout.body_pixel_size, "ENDURANCE",
              {0.88F, 0.88F, 0.84F, 0.92F});

    auto status_text = std::string{};
    if (maritime_hud.moored) {
      status_text = layout.compact ? "QUAI " : "A QUAI - DEPART ";
      status_text += std::to_string(cache_key.departure_seconds_step) + "S";
    } else if (maritime_hud.departing) {
      status_text = layout.compact ? "DEPART " : "DEPART EN COURS ";
      status_text += std::to_string(cache_key.departure_seconds_step) + "S";
    } else {
      status_text = maritime_hud.on_ship
                        ? (layout.compact ? std::string("BORD  ")
                                          : std::string("A BORD  "))
                        : (layout.compact ? std::string("MER  ")
                                          : std::string("A LA MER  "));
      status_text += std::to_string(static_cast<int>(
                         std::round(maritime_hud.ship_distance))) +
                     "M";
    }
    draw_text(layout.status_x, layout.status_y, layout.body_pixel_size,
              status_text,
              maritime_hud.danger ? HudColor{1.0F, 0.62F, 0.48F, 0.96F}
                                  : HudColor{0.72F, 0.88F, 0.96F, 0.94F});

    const auto cargo_text =
        layout.compact
            ? std::string("V ") + std::to_string(maritime_hud.food_rations) +
                  "  E " + std::to_string(maritime_hud.water_flasks) + "  P " +
                  std::to_string(maritime_hud.fish)
            : std::string("VIVRES ") +
                  std::to_string(maritime_hud.food_rations) + "  EAU " +
                  std::to_string(maritime_hud.water_flasks) + "  POISSONS " +
                  std::to_string(maritime_hud.fish);
    draw_text(layout.cargo_x, layout.cargo_y, layout.body_pixel_size,
              cargo_text, {0.82F, 0.86F, 0.90F, 0.94F});

    if (maritime_hud.fishing_active) {
      draw_bar(layout.fishing_bar, {0.94F, 0.84F, 0.38F, 0.94F});
      draw_text(layout.fishing_bar.x, layout.fishing_bar.y - 10.0F,
                layout.body_pixel_size, "PECHE", {0.94F, 0.90F, 0.72F, 0.94F});
    }

    if (layout.crew_focus.visible) {
      // Le panneau contextuel rend l'intention du PNJ lisible sans ouvrir de
      // menu.
      const auto focus_accent =
          maritime_hud.crew_knocked_out
              ? HudColor{0.96F, 0.34F, 0.28F, 1.0F}
              : (maritime_hud.crew_blocked
                     ? HudColor{0.98F, 0.74F, 0.28F, 1.0F}
                     : HudColor{0.34F, 0.88F, 0.94F, 1.0F});
      auto focus_palette = make_warm_panel_palette(focus_accent);
      focus_palette.fill = {0.025F, 0.055F, 0.070F, 0.90F};
      focus_palette.frame = {0.035F, 0.105F, 0.130F, 0.98F};
      focus_palette.trim = hud_with_alpha(focus_accent, 0.42F);

      append_stylized_panel_top_left(
          vertices, viewport_width, viewport_height, layout.crew_focus.panel_x,
          layout.crew_focus.panel_y, layout.crew_focus.panel_width,
          layout.crew_focus.panel_height, 3.0F, focus_palette, true);

      auto title = maritime_hud.crew_role.empty()
                       ? std::string("MARIN")
                       : std::string(maritime_hud.crew_role);

      auto detail = std::string{};
      if (maritime_hud.crew_knocked_out) {
        detail = "ASSOMME - RECUPERATION";
      } else if (maritime_hud.crew_blocked) {
        detail = "PASSAGE OCCUPE - PATIENTE";
      } else if (maritime_hud.crew_moving) {
        if (!maritime_hud.crew_cargo.empty()) {
          detail = std::string(maritime_hud.crew_cargo) + " VERS " +
                   std::string(maritime_hud.crew_destination);
        } else {
          detail = "VERS ";
          detail += maritime_hud.crew_destination;
        }
      } else {
        detail = maritime_hud.crew_activity;
      }

      auto status = std::to_string(
          static_cast<int>(std::round(maritime_hud.crew_distance)));
      status += "M  SANTE ";
      status += std::to_string(static_cast<int>(std::round(
          std::clamp(maritime_hud.crew_health_ratio, 0.0F, 1.0F) * 100.0F)));
      status += "%";

      draw_text(layout.crew_focus.title_x, layout.crew_focus.title_y,
                layout.text_pixel_size, title, {0.94F, 0.99F, 1.0F, 0.98F});
      // Les destinations les plus longues doivent rester dans le panneau
      // meme sur une petite fenetre. La reduction reste quantifiee par
      // quarts de pixel afin de conserver l'aspect net de la police bitmap.
      const auto detail_width_limit =
          layout.crew_focus.panel_width -
          2.0F * (layout.crew_focus.detail_x - layout.crew_focus.panel_x);
      auto detail_pixel_size = layout.body_pixel_size;
      while (detail_pixel_size > 1.25F &&
             measure_pixel_text(detail, detail_pixel_size) >
                 detail_width_limit) {
        detail_pixel_size -= 0.25F;
      }

      draw_text(layout.crew_focus.detail_x, layout.crew_focus.detail_y,
                detail_pixel_size, detail,
                maritime_hud.crew_blocked
                    ? HudColor{1.0F, 0.84F, 0.48F, 0.96F}
                    : HudColor{0.76F, 0.92F, 0.98F, 0.96F});
      draw_text(layout.crew_focus.status_x, layout.crew_focus.status_y,
                layout.body_pixel_size, status, {0.76F, 0.80F, 0.84F, 0.94F});

      if (maritime_hud.crew_has_progress) {
        draw_bar(layout.crew_focus.progress_bar, focus_accent);
      }
    }
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glUseProgram(hud_program_);
  bind_hud_textures();

  upload_hud_vertices(vertices);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_issou_legendary_hud(int width, int height) {
  if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 ||
      hud_vbo_ == 0 || !issou_hud_snapshot_.has_visible_content()) {
    return;
  }

  append_renderer_issou_hud_geometry(issou_hud_vertices_scratch_,
                                     issou_hud_snapshot_, width, height);
  if (issou_hud_vertices_scratch_.empty()) {
    return;
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(hud_program_);
  bind_hud_textures();
  upload_hud_vertices(issou_hud_vertices_scratch_);
  glDrawArrays(GL_TRIANGLES, 0,
               static_cast<GLsizei>(issou_hud_vertices_scratch_.size()));
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_gameplay_announcement(
    const GameplayHudAnnouncementView &announcement, int width, int height) {
  if (!announcement.visible || width <= 0 || height <= 0 || hud_program_ == 0 ||
      hud_vao_ == 0 || hud_vbo_ == 0) {
    return;
  }

  const auto viewport_width = static_cast<float>(width);
  const auto viewport_height = static_cast<float>(height);
  const auto progress = std::clamp(announcement.normalized_time, 0.0F, 1.0F);
  const auto fade_in = std::clamp(progress / 0.10F, 0.0F, 1.0F);
  const auto fade_out = std::clamp((1.0F - progress) / 0.22F, 0.0F, 1.0F);
  const auto alpha = std::min(fade_in, fade_out);
  if (alpha <= 0.01F) {
    return;
  }

  auto title_pixel_size = viewport_width < 720.0F ? 3.0F : 4.0F;
  auto detail_pixel_size = viewport_width < 720.0F ? 2.0F : 3.0F;
  const auto panel_max_width = std::max(180.0F, viewport_width - 40.0F);
  const auto padding_x = viewport_width < 720.0F ? 16.0F : 24.0F;
  while (title_pixel_size > 2.0F &&
         measure_pixel_text(announcement.title, title_pixel_size) >
             panel_max_width - padding_x * 2.0F) {
    title_pixel_size -= 1.0F;
  }
  while (detail_pixel_size > 2.0F &&
         measure_pixel_text(announcement.detail, detail_pixel_size) >
             panel_max_width - padding_x * 2.0F) {
    detail_pixel_size -= 1.0F;
  }

  const auto title_width =
      measure_pixel_text(announcement.title, title_pixel_size);
  const auto detail_width =
      measure_pixel_text(announcement.detail, detail_pixel_size);
  const auto panel_width = std::min(
      panel_max_width, std::max(title_width, detail_width) + padding_x * 2.0F);
  const auto panel_height = (detail_width > 0.0F ? 58.0F : 44.0F) +
                            (viewport_width < 720.0F ? -8.0F : 0.0F);
  const auto panel_x = (viewport_width - panel_width) * 0.5F;
  const auto panel_y = std::max(18.0F, viewport_height * 0.055F);
  const auto accent = HudColor{0.34F, 0.92F, 1.0F, 1.0F};
  auto palette = make_warm_panel_palette(accent);
  palette.frame = hud_with_alpha(palette.frame, palette.frame[3] * alpha);
  palette.fill = hud_with_alpha(palette.fill, palette.fill[3] * alpha);
  palette.highlight =
      hud_with_alpha(palette.highlight, palette.highlight[3] * alpha);
  palette.shadow = hud_with_alpha(palette.shadow, palette.shadow[3] * alpha);
  palette.trim = hud_with_alpha(palette.trim, palette.trim[3] * alpha);

  auto &vertices = gameplay_announcement_vertices_scratch_;
  vertices.clear();
  if (vertices.capacity() < 1536U) {
    vertices.reserve(1536U);
  }
  append_stylized_panel_top_left(vertices, viewport_width, viewport_height,
                                 panel_x, panel_y, panel_width, panel_height,
                                 3.0F, palette, true);

  const auto draw_text = [&](float center_x, float y, float pixel_size,
                             std::string_view text, const HudColor &color) {
    append_pixel_text(vertices, viewport_width, viewport_height,
                      center_x + pixel_size, y + pixel_size, pixel_size, text,
                      {0.0F, 0.0F, 0.0F, 0.48F * alpha}, true);
    append_pixel_text(vertices, viewport_width, viewport_height, center_x, y,
                      pixel_size, text, hud_with_alpha(color, color[3] * alpha),
                      true);
  };

  const auto title_y = panel_y + (announcement.detail.empty() ? 15.0F : 12.0F);
  draw_text(panel_x + panel_width * 0.5F, title_y, title_pixel_size,
            announcement.title, {0.96F, 0.99F, 1.0F, 1.0F});
  if (!announcement.detail.empty()) {
    draw_text(panel_x + panel_width * 0.5F,
              title_y + title_pixel_size * 8.0F + 7.0F, detail_pixel_size,
              announcement.detail, {0.72F, 0.94F, 1.0F, 0.92F});
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glUseProgram(hud_program_);
  bind_hud_textures();

  upload_hud_vertices(vertices);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_command_console(const CommandConsoleView &command_console,
                                    int width, int height) {
  if (!command_console.visible || width <= 0 || height <= 0 ||
      hud_program_ == 0 || hud_vao_ == 0 || hud_vbo_ == 0) {
    return;
  }

  const auto viewport_width = static_cast<float>(width);
  const auto viewport_height = static_cast<float>(height);
  const auto layout = build_command_console_layout(width, height);
  const auto pixel_size = layout.text_pixel_size;
  constexpr std::string_view kPrompt = "> ";
  const auto horizontal_padding = std::max(8.0F, pixel_size * 4.0F);
  const auto available_text_width = std::max(
      pixel_size * 6.0F, layout.input_width - horizontal_padding * 2.0F -
                             measure_pixel_text(kPrompt, pixel_size));
  const auto maximum_visible_characters =
      std::max<std::size_t>(static_cast<std::size_t>(std::floor(
                                available_text_width / (pixel_size * 6.0F))),
                            1U);
  const auto text_window = build_command_console_text_window(
      command_console.input, command_console.cursor_byte_offset,
      maximum_visible_characters);
  const auto visible_input =
      command_console.input.substr(text_window.start, text_window.length);

  auto &vertices = command_console_vertices_scratch_;
  vertices.clear();
  if (vertices.capacity() < 16'384U) {
    vertices.reserve(16'384U);
  }

  append_hud_shadow_top_left(vertices, viewport_width, viewport_height,
                             layout.panel_x, layout.panel_y, layout.panel_width,
                             layout.panel_height, 14.0F,
                             {0.0F, 0.0F, 0.0F, 0.26F});
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           layout.panel_x, layout.panel_y, layout.panel_width,
                           layout.panel_height, {0.04F, 0.28F, 0.32F, 0.48F});
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           layout.panel_x + 1.5F, layout.panel_y + 1.5F,
                           std::max(0.0F, layout.panel_width - 3.0F),
                           std::max(0.0F, layout.panel_height - 3.0F),
                           {0.01F, 0.025F, 0.03F, 0.55F});
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           layout.panel_x + 2.0F, layout.panel_y + 2.0F,
                           std::max(0.0F, layout.panel_width - 4.0F), 2.0F,
                           {0.30F, 0.92F, 0.98F, 0.84F});
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           layout.input_x, layout.input_y, layout.input_width,
                           layout.input_height, {0.10F, 0.46F, 0.50F, 0.50F});
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           layout.input_x + 1.5F, layout.input_y + 1.5F,
                           std::max(0.0F, layout.input_width - 3.0F),
                           std::max(0.0F, layout.input_height - 3.0F),
                           {0.005F, 0.015F, 0.02F, 0.58F});

  const auto draw_text = [&](float x, float y, float size,
                             std::string_view text, const HudColor &color) {
    append_pixel_text(vertices, viewport_width, viewport_height, x + size,
                      y + size, size, text, {0.0F, 0.0F, 0.0F, 0.52F});
    append_pixel_text(vertices, viewport_width, viewport_height, x, y, size,
                      text, color);
  };

  const auto title_size = layout.panel_width < 420.0F ? 1.5F : 2.0F;
  draw_text(layout.input_x, layout.panel_y + 11.0F, title_size,
            "CONSOLE DE COMMANDE", {0.62F, 0.94F, 0.98F, 0.96F});

  const auto input_text_y =
      layout.input_y +
      std::max(0.0F, (layout.input_height - pixel_size * 7.0F) * 0.5F);
  const auto prompt_x = layout.input_x + horizontal_padding;
  draw_text(prompt_x, input_text_y, pixel_size, kPrompt,
            {0.38F, 0.94F, 0.72F, 0.98F});
  const auto input_text_x = prompt_x + measure_pixel_text(kPrompt, pixel_size);
  draw_text(input_text_x, input_text_y, pixel_size, visible_input,
            {0.92F, 0.98F, 1.0F, 0.98F});

  const auto cursor_prefix = visible_input.substr(
      0U, std::min(text_window.cursor_offset, visible_input.size()));
  const auto cursor_x =
      input_text_x + measure_pixel_text(cursor_prefix, pixel_size);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height, cursor_x,
                           input_text_y, std::max(1.0F, pixel_size * 0.65F),
                           pixel_size * 7.0F, {0.48F, 1.0F, 0.82F, 0.92F});

  const auto feedback =
      command_console.feedback.empty()
          ? std::string_view("ENTREE POUR VALIDER - ECHAP POUR FERMER")
          : command_console.feedback;
  const auto feedback_size = layout.panel_width < 420.0F ? 1.25F : 2.0F;
  draw_text(layout.input_x,
            layout.panel_y + layout.panel_height - feedback_size * 7.0F - 9.0F,
            feedback_size, feedback,
            command_console.feedback_is_error
                ? HudColor{1.0F, 0.46F, 0.40F, 0.96F}
                : HudColor{0.48F, 0.94F, 0.72F, 0.94F});

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glUseProgram(hud_program_);
  bind_hud_textures();

  upload_hud_vertices(vertices);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_inventory_menu(const InventoryMenuState &inventory_menu,
                                   const HotbarState &hotbar, int width,
                                   int height) {
  if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 ||
      hud_vbo_ == 0) {
    return;
  }

  const auto layout =
      build_inventory_menu_layout(width, height, inventory_menu, hotbar);
  const auto viewport_width = static_cast<float>(width);
  const auto viewport_height = static_cast<float>(height);
  InventoryHudCacheKey cache_key{};
  cache_key.inventory_menu = inventory_menu;
  cache_key.hotbar = hotbar;
  cache_key.width = width;
  cache_key.height = height;

  auto &cache = inventory_cache_;
  auto &vertices = cache.vertices;
  const auto needs_rebuild = !cache.valid || cache.key != cache_key;
  if (needs_rebuild) {
    cache.valid = true;
    cache.key = cache_key;
    vertices.clear();
    vertices.reserve(49152U);

    const auto draw_text = [&](float x, float y, float pixel_size,
                               std::string_view text, const HudColor &color,
                               bool centered = false) {
      append_pixel_text(vertices, viewport_width, viewport_height,
                        x + pixel_size, y + pixel_size, pixel_size, text,
                        {0.0F, 0.0F, 0.0F, 0.58F}, centered);
      append_pixel_text(vertices, viewport_width, viewport_height, x, y,
                        pixel_size, text, color, centered);
    };

    const auto title_pixel_size = std::clamp(
        static_cast<float>(std::floor(layout.slot_size / 12.0F)), 3.0F, 4.0F);
    const auto subtitle_pixel_size = std::clamp(
        static_cast<float>(std::floor(layout.slot_size / 18.0F)), 2.0F, 3.0F);
    const auto label_pixel_size = std::clamp(
        static_cast<float>(std::floor(layout.slot_size / 17.0F)), 2.0F, 3.0F);
    const auto body_pixel_size = std::max(2.0F, subtitle_pixel_size);
    const auto stack_pixel_size = std::max(
        2.0F, static_cast<float>(std::floor(layout.slot_size / 18.0F)));
    const auto focus_item =
        resolve_inventory_focus_item(inventory_menu, hotbar);
    const auto focus_accent = focus_item.has_item
                                  ? ui_material_accent(focus_item.slot.block_id)
                                  : HudColor{0.64F, 0.68F, 0.74F, 1.0F};

    const auto frame_palette = make_stone_panel_palette();
    const auto header_palette = make_header_panel_palette();
    const auto preview_palette = make_slate_panel_palette();
    const auto storage_palette = make_slate_panel_palette();
    const auto hotbar_palette =
        make_warm_panel_palette({0.70F, 0.56F, 0.30F, 1.0F});
    const auto footer_palette = make_slate_panel_palette();
    const auto detail_palette = make_warm_panel_palette(focus_accent);

    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             0.0F, viewport_width, viewport_height,
                             {0.02F, 0.03F, 0.04F, 0.66F});
    const auto vignette_edge = std::clamp(
        std::min(viewport_width, viewport_height) * 0.18F, 72.0F, 180.0F);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             0.0F, viewport_width, vignette_edge,
                             {0.08F, 0.10F, 0.12F, 0.10F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             viewport_height - vignette_edge, viewport_width,
                             vignette_edge, {0.01F, 0.02F, 0.03F, 0.22F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             0.0F, vignette_edge, viewport_height,
                             {0.01F, 0.02F, 0.03F, 0.12F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             viewport_width - vignette_edge, 0.0F,
                             vignette_edge, viewport_height,
                             {0.01F, 0.02F, 0.03F, 0.12F});

    append_hud_shadow_top_left(vertices, viewport_width, viewport_height,
                               layout.panel_x, layout.panel_y,
                               layout.panel_width, layout.panel_height, 18.0F,
                               {0.0F, 0.0F, 0.0F, 0.28F});
    append_stylized_panel_top_left(vertices, viewport_width, viewport_height,
                                   layout.panel_x, layout.panel_y,
                                   layout.panel_width, layout.panel_height,
                                   5.0F, frame_palette, false);

    append_stylized_panel_top_left(
        vertices, viewport_width, viewport_height, layout.header_panel_x,
        layout.header_panel_y, layout.header_panel_width,
        layout.header_panel_height, 4.0F, header_palette, false);
    append_stylized_panel_top_left(
        vertices, viewport_width, viewport_height, layout.preview_panel_x,
        layout.preview_panel_y, layout.preview_panel_width,
        layout.preview_panel_height, 3.0F, preview_palette, true);
    append_stylized_panel_top_left(
        vertices, viewport_width, viewport_height, layout.storage_panel_x,
        layout.storage_panel_y, layout.storage_panel_width,
        layout.storage_panel_height, 3.0F, storage_palette, true);
    append_stylized_panel_top_left(
        vertices, viewport_width, viewport_height, layout.hotbar_panel_x,
        layout.hotbar_panel_y, layout.hotbar_panel_width,
        layout.hotbar_panel_height, 3.0F, hotbar_palette, true);
    append_stylized_panel_top_left(
        vertices, viewport_width, viewport_height, layout.detail_panel_x,
        layout.detail_panel_y, layout.detail_panel_width,
        layout.detail_panel_height, 3.0F, detail_palette, true);
    append_stylized_panel_top_left(
        vertices, viewport_width, viewport_height, layout.footer_panel_x,
        layout.footer_panel_y, layout.footer_panel_width,
        layout.footer_panel_height, 3.0F, footer_palette, false);

    append_hud_scanlines_top_left(
        vertices, viewport_width, viewport_height, layout.panel_x + 10.0F,
        layout.panel_y + 10.0F, std::max(0.0F, layout.panel_width - 20.0F),
        std::max(0.0F, layout.panel_height - 20.0F), 12.0F,
        {1.0F, 1.0F, 1.0F, 0.014F});
    append_hud_scanlines_top_left(
        vertices, viewport_width, viewport_height,
        layout.storage_panel_x + 8.0F, layout.storage_panel_y + 32.0F,
        std::max(0.0F, layout.storage_panel_width - 16.0F),
        std::max(0.0F, layout.storage_panel_height - 40.0F), 10.0F,
        {1.0F, 1.0F, 1.0F, 0.018F});
    append_hud_scanlines_top_left(
        vertices, viewport_width, viewport_height, layout.hotbar_panel_x + 8.0F,
        layout.hotbar_panel_y + 32.0F,
        std::max(0.0F, layout.hotbar_panel_width - 16.0F),
        std::max(0.0F, layout.hotbar_panel_height - 40.0F), 10.0F,
        {1.0F, 0.92F, 0.68F, 0.020F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             layout.header_panel_x + 10.0F,
                             layout.header_panel_y + 10.0F, 4.0F,
                             std::max(0.0F, layout.header_panel_height - 20.0F),
                             {0.98F, 0.76F, 0.34F, 0.38F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             layout.detail_panel_x + 8.0F,
                             layout.detail_panel_y + 8.0F, 3.0F,
                             std::max(0.0F, layout.detail_panel_height - 16.0F),
                             hud_with_alpha(focus_accent, 0.34F));

    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             layout.header_panel_x + 12.0F,
                             layout.header_panel_y +
                                 layout.header_panel_height - 10.0F,
                             std::max(0.0F, layout.header_panel_width - 24.0F),
                             2.0F, {1.0F, 1.0F, 1.0F, 0.06F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             layout.preview_panel_x + 10.0F,
                             layout.preview_panel_y + 26.0F,
                             std::max(0.0F, layout.preview_panel_width - 20.0F),
                             2.0F, {0.86F, 0.90F, 0.96F, 0.08F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             layout.storage_panel_x + 10.0F,
                             layout.storage_panel_y + 26.0F,
                             std::max(0.0F, layout.storage_panel_width - 20.0F),
                             2.0F, {0.86F, 0.90F, 0.96F, 0.08F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             layout.hotbar_panel_x + 10.0F,
                             layout.hotbar_panel_y + 26.0F,
                             std::max(0.0F, layout.hotbar_panel_width - 20.0F),
                             2.0F, {1.0F, 0.90F, 0.66F, 0.12F});
    append_hud_rect_top_left(
        vertices, viewport_width, viewport_height,
        layout.detail_panel_x + 10.0F, layout.detail_panel_y + 26.0F,
        std::max(0.0F, layout.detail_panel_width - 20.0F), 2.0F,
        hud_with_alpha(hud_scale_rgb(focus_accent, 1.14F), 0.18F));
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             layout.footer_panel_x + 10.0F,
                             layout.footer_panel_y + 8.0F,
                             std::max(0.0F, layout.footer_panel_width - 20.0F),
                             1.0F, {1.0F, 1.0F, 1.0F, 0.05F});

    const auto title_y = layout.header_panel_y + 12.0F;
    const auto subtitle_y = title_y + title_pixel_size * 7.0F + 4.0F;
    draw_text(layout.title_center_x, title_y, title_pixel_size, "INVENTAIRE",
              {0.98F, 0.98F, 0.99F, 1.0F}, true);
    draw_text(layout.subtitle_center_x, subtitle_y, subtitle_pixel_size,
              "EQUIPEMENT ET STOCKAGE", {0.82F, 0.84F, 0.88F, 0.96F}, true);
    draw_text(layout.preview_panel_x + layout.preview_panel_width * 0.5F,
              layout.preview_panel_y + 10.0F, label_pixel_size, "AVATAR",
              {0.90F, 0.92F, 0.96F, 0.98F}, true);
    draw_text(layout.equipment_label_x, layout.equipment_label_y,
              label_pixel_size, "EQUIPEMENT", {0.90F, 0.92F, 0.96F, 0.96F},
              true);
    draw_text(layout.storage_label_x, layout.storage_label_y, label_pixel_size,
              "STOCKAGE", {0.90F, 0.92F, 0.96F, 0.98F});
    draw_text(layout.hotbar_label_x, layout.hotbar_label_y, label_pixel_size,
              "BARRE RAPIDE", {0.98F, 0.94F, 0.84F, 0.98F});
    draw_text(layout.detail_label_x, layout.detail_label_y, label_pixel_size,
              "DETAIL", hud_scale_rgb(focus_accent, 1.18F));

    append_avatar_preview_art(vertices, viewport_width, viewport_height,
                              layout);
    const auto preview_caption_y =
        layout.preview_panel_y + layout.preview_panel_height -
        body_pixel_size * 7.0F - std::max(10.0F, layout.slot_size * 0.20F);
    draw_text(layout.preview_panel_x + layout.preview_panel_width * 0.5F,
              preview_caption_y, body_pixel_size, "MODELE JOUEUR",
              {0.76F, 0.79F, 0.84F, 0.88F}, true);

    for (const auto &keycap : layout.hotbar_keycaps) {
      append_keycap_top_left(vertices, viewport_width, viewport_height, keycap,
                             body_pixel_size);
    }

    for (const auto &slot : layout.slots) {
      const auto palette = build_slot_palette(
          slot.slot, slot.is_selected_hotbar, slot.hovered, slot.is_hotbar);
      append_stylized_slot_top_left(vertices, viewport_width, viewport_height,
                                    slot.x, slot.y, slot.size, palette,
                                    slot.has_icon);

      if (!slot.has_icon) {
        continue;
      }

      const auto icon_size =
          std::max(8.0F, slot.size - layout.icon_inset * 2.0F);
      const auto icon_offset = (slot.size - icon_size) * 0.5F;
      const auto icon_texture_mode = hud_item_texture_mode(slot.slot.block_id);
      append_hud_quad_top_left(
          vertices, viewport_width, viewport_height, slot.x + icon_offset,
          slot.y + icon_offset, icon_size, icon_size, {1.0F, 1.0F, 1.0F, 1.0F},
          icon_texture_mode > 2.5F
              ? std::array<float, 4>{0.0F, 1.0F, 1.0F, 0.0F}
              : atlas_uv_rect(slot.icon_tile),
          icon_texture_mode);
      append_stack_count(vertices, viewport_width, viewport_height,
                         slot.x + slot.size - 4.0F, slot.y + slot.size - 4.0F,
                         stack_pixel_size, slot.slot.count);
    }

    const auto detail_padding = std::max(10.0F, layout.slot_size * 0.26F);
    const auto detail_content_y = layout.detail_label_y +
                                  label_pixel_size * 7.0F +
                                  std::max(10.0F, layout.slot_size * 0.22F);
    const auto detail_slot_size = std::clamp(
        std::min(layout.detail_panel_width - detail_padding * 2.0F,
                 layout.slot_size * (layout.compact_detail ? 1.20F : 1.52F)),
        layout.slot_size, layout.compact_detail ? 68.0F : 84.0F);
    const auto detail_slot_x =
        layout.detail_panel_x +
        (layout.detail_panel_width - detail_slot_size) * 0.5F;
    const auto detail_slot_y = detail_content_y;
    const auto detail_slot_palette =
        build_slot_palette(focus_item.slot, focus_item.has_item, false, false);
    append_stylized_slot_top_left(
        vertices, viewport_width, viewport_height, detail_slot_x, detail_slot_y,
        detail_slot_size, detail_slot_palette, focus_item.has_item);

    if (focus_item.has_item) {
      const auto icon_size =
          std::max(12.0F, detail_slot_size - detail_padding * 1.40F);
      const auto icon_offset = (detail_slot_size - icon_size) * 0.5F;
      const auto icon_texture_mode =
          hud_item_texture_mode(focus_item.slot.block_id);
      append_hud_quad_top_left(
          vertices, viewport_width, viewport_height,
          detail_slot_x + icon_offset, detail_slot_y + icon_offset, icon_size,
          icon_size, {1.0F, 1.0F, 1.0F, 1.0F},
          icon_texture_mode > 2.5F
              ? std::array<float, 4>{0.0F, 1.0F, 1.0F, 0.0F}
              : atlas_uv_rect(
                    inventory_slot_icon_tile(focus_item.slot.block_id)),
          icon_texture_mode);
      append_stack_count(vertices, viewport_width, viewport_height,
                         detail_slot_x + detail_slot_size - 4.0F,
                         detail_slot_y + detail_slot_size - 4.0F,
                         stack_pixel_size, focus_item.slot.count);
    }

    auto detail_name =
        focus_item.has_item
            ? std::string(inventory_item_label(focus_item.slot.block_id))
            : std::string("SURVOLE UN OBJET");
    auto detail_name_pixel_size = std::clamp(
        label_pixel_size + (layout.compact_detail ? 0.0F : 1.0F), 2.0F, 4.0F);
    while (detail_name_pixel_size > 2.0F &&
           measure_pixel_text(detail_name, detail_name_pixel_size) >
               layout.detail_panel_width - detail_padding * 2.0F) {
      detail_name_pixel_size -= 1.0F;
    }
    const auto detail_name_y = detail_slot_y + detail_slot_size +
                               std::max(10.0F, layout.slot_size * 0.18F);
    draw_text(layout.detail_panel_x + layout.detail_panel_width * 0.5F,
              detail_name_y, detail_name_pixel_size, detail_name,
              focus_item.has_item ? hud_scale_rgb(focus_accent, 1.18F)
                                  : HudColor{0.90F, 0.92F, 0.96F, 0.98F},
              true);

    const auto detail_rule_y =
        detail_name_y + detail_name_pixel_size * 7.0F + 8.0F;
    append_hud_rect_top_left(
        vertices, viewport_width, viewport_height,
        layout.detail_panel_x + detail_padding, detail_rule_y,
        std::max(0.0F, layout.detail_panel_width - detail_padding * 2.0F), 1.0F,
        hud_with_alpha(hud_scale_rgb(focus_accent, 1.10F), 0.16F));

    if (focus_item.has_item) {
      std::string pile_line = "PILE ";
      pile_line += std::to_string(static_cast<int>(focus_item.slot.count));
      pile_line += " SUR ";
      pile_line += std::to_string(
          static_cast<int>(max_item_stack_count(focus_item.slot.block_id)));

      const auto tool_material_count =
          inventory_available_tool_crafting_material(inventory_menu, hotbar);
      std::string material_line = "MATIERE ";
      material_line += item_material_label(focus_item.slot.block_id);
      if (const auto stats = weapon_stats(focus_item.slot.block_id);
          stats.has_value()) {
        material_line = "DEGATS ";
        material_line +=
            std::to_string(static_cast<int>(std::round(stats->damage)));
        material_line += "  PORTEE ";
        material_line +=
            std::to_string(static_cast<int>(std::round(stats->range)));
      } else if (const auto resistance =
                     armor_resistance_percent(focus_item.slot.block_id);
                 resistance > 0.0F) {
        material_line = "RESISTANCE +";
        material_line +=
            std::to_string(static_cast<int>(std::round(resistance)));
        material_line += "%";
      } else if (is_tool_item(focus_item.slot.block_id)) {
        material_line = "OUTIL MINAGE";
      } else if (inventory_is_tool_crafting_material(
                     focus_item.slot.block_id)) {
        material_line = "MATERIAU OUTILS";
      }

      std::string source_line = "SOURCE ";
      if (focus_item.from_carried_slot) {
        source_line += "MAIN";
      } else if (focus_item.group == InventorySlotGroup::Hotbar) {
        source_line += "BARRE";
      } else if (focus_item.group == InventorySlotGroup::Equipment) {
        source_line += "EQUIP";
      } else {
        source_line += inventory_slot_group_label(focus_item.group);
      }
      if (is_tool_item(focus_item.slot.block_id) ||
          inventory_is_tool_crafting_material(focus_item.slot.block_id)) {
        source_line = "STOCK BOIS ";
        source_line += std::to_string(static_cast<int>(tool_material_count));
      }

      const auto info_y = detail_rule_y + 8.0F;
      const auto line_step = body_pixel_size * 7.0F + 6.0F;
      draw_text(layout.detail_panel_x + layout.detail_panel_width * 0.5F,
                info_y, body_pixel_size, pile_line,
                {0.96F, 0.97F, 0.98F, 0.96F}, true);
      draw_text(layout.detail_panel_x + layout.detail_panel_width * 0.5F,
                info_y + line_step, body_pixel_size, material_line,
                {0.84F, 0.86F, 0.90F, 0.94F}, true);
      draw_text(layout.detail_panel_x + layout.detail_panel_width * 0.5F,
                info_y + line_step * 2.0F, body_pixel_size, source_line,
                {0.84F, 0.86F, 0.90F, 0.94F}, true);
    } else {
      draw_text(layout.detail_panel_x + layout.detail_panel_width * 0.5F,
                detail_rule_y + 10.0F, body_pixel_size, "OU PRENDS EN UN",
                {0.76F, 0.79F, 0.84F, 0.90F}, true);
    }

    for (const auto &hint : layout.footer_hints) {
      const auto hint_accent = hint.emphasized
                                   ? HudColor{0.96F, 0.78F, 0.36F, 1.0F}
                                   : HudColor{0.52F, 0.74F, 0.92F, 1.0F};
      append_stylized_panel_top_left(
          vertices, viewport_width, viewport_height, hint.x, hint.y, hint.width,
          hint.height, 2.0F,
          hint.emphasized ? make_warm_panel_palette(hint_accent)
                          : make_slate_panel_palette(),
          false);
      append_hud_rect_top_left(
          vertices, viewport_width, viewport_height, hint.x + 4.0F,
          hint.y + 4.0F, 3.0F, std::max(0.0F, hint.height - 8.0F),
          hud_with_alpha(hint_accent, hint.emphasized ? 0.42F : 0.24F));

      auto hint_pixel_size = subtitle_pixel_size;
      while (hint_pixel_size > 2.0F &&
             measure_pixel_text(hint.label, hint_pixel_size) >
                 hint.width - 18.0F) {
        hint_pixel_size -= 1.0F;
      }
      const auto hint_text_y =
          hint.y +
          std::max(0.0F, (hint.height - hint_pixel_size * 7.0F) * 0.5F);
      draw_text(hint.x + hint.width * 0.5F, hint_text_y, hint_pixel_size,
                hint.label,
                hint.emphasized ? HudColor{0.99F, 0.96F, 0.86F, 0.98F}
                                : HudColor{0.82F, 0.86F, 0.92F, 0.96F},
                true);
    }

    std::string tooltip_label;
    auto tooltip_accent = focus_accent;
    if (inventory_menu.carrying_item &&
        inventory_slot_has_item(inventory_menu.carried_slot)) {
      tooltip_label = item_stack_display_label(inventory_menu.carried_slot);
      tooltip_accent = ui_material_accent(inventory_menu.carried_slot.block_id);
    } else if (inventory_menu.hovered_slot.has_value()) {
      if (const auto *slot = inventory_slot_ptr(inventory_menu, hotbar,
                                                *inventory_menu.hovered_slot);
          slot != nullptr && inventory_slot_has_item(*slot)) {
        tooltip_label = item_stack_display_label(*slot);
        tooltip_accent = ui_material_accent(slot->block_id);
      }
    }

    if (!tooltip_label.empty()) {
      const auto tooltip_pixel_size = subtitle_pixel_size;
      const auto tooltip_padding_x = std::max(8.0F, tooltip_pixel_size * 2.8F);
      const auto tooltip_padding_y = std::max(6.0F, tooltip_pixel_size * 2.0F);
      const auto tooltip_width =
          measure_pixel_text(tooltip_label, tooltip_pixel_size) +
          tooltip_padding_x * 2.0F;
      const auto tooltip_height =
          tooltip_pixel_size * 7.0F + tooltip_padding_y * 2.0F;
      const auto tooltip_x = std::clamp(
          inventory_menu.cursor_x + 18.0F, layout.panel_x + 12.0F,
          layout.panel_x + layout.panel_width - tooltip_width - 12.0F);
      const auto tooltip_y = std::clamp(
          inventory_menu.cursor_y + 18.0F, layout.panel_y + 12.0F,
          layout.panel_y + layout.panel_height - tooltip_height - 12.0F);
      append_stylized_panel_top_left(
          vertices, viewport_width, viewport_height, tooltip_x, tooltip_y,
          tooltip_width, tooltip_height, 3.0F,
          make_warm_panel_palette(tooltip_accent), true);
      draw_text(tooltip_x + tooltip_width * 0.5F, tooltip_y + tooltip_padding_y,
                tooltip_pixel_size, tooltip_label, {0.98F, 0.98F, 0.96F, 0.98F},
                true);
    }

    if (inventory_menu.carrying_item &&
        inventory_slot_has_item(inventory_menu.carried_slot)) {
      const auto carried_size = layout.slot_size;
      const auto carried_x = inventory_menu.cursor_x - carried_size * 0.5F;
      const auto carried_y = inventory_menu.cursor_y - carried_size * 0.5F;
      const auto carried_palette =
          build_slot_palette(inventory_menu.carried_slot, true, false, false);
      append_stylized_slot_top_left(vertices, viewport_width, viewport_height,
                                    carried_x, carried_y, carried_size,
                                    carried_palette, true);

      const auto icon_size =
          std::max(8.0F, carried_size - layout.icon_inset * 2.0F);
      const auto icon_offset = (carried_size - icon_size) * 0.5F;
      const auto icon_texture_mode =
          hud_item_texture_mode(inventory_menu.carried_slot.block_id);
      append_hud_quad_top_left(
          vertices, viewport_width, viewport_height, carried_x + icon_offset,
          carried_y + icon_offset, icon_size, icon_size,
          {1.0F, 1.0F, 1.0F, 1.0F},
          icon_texture_mode > 2.5F
              ? std::array<float, 4>{0.0F, 1.0F, 1.0F, 0.0F}
              : atlas_uv_rect(inventory_slot_icon_tile(
                    inventory_menu.carried_slot.block_id)),
          icon_texture_mode);
      append_stack_count(vertices, viewport_width, viewport_height,
                         carried_x + carried_size - 4.0F,
                         carried_y + carried_size - 4.0F, stack_pixel_size,
                         inventory_menu.carried_slot.count);
    }
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glUseProgram(hud_program_);
  bind_hud_textures();

  upload_hud_vertices(vertices);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_death_screen(const DeathScreenState &death_screen,
                                 int width, int height) {
  if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 ||
      hud_vbo_ == 0) {
    return;
  }

  const auto layout = build_death_screen_layout(width, height, death_screen);
  const auto viewport_width = static_cast<float>(width);
  const auto viewport_height = static_cast<float>(height);
  DeathHudCacheKey cache_key{};
  cache_key.death_screen = death_screen;
  cache_key.width = width;
  cache_key.height = height;

  auto &cache = death_cache_;
  auto &vertices = cache.vertices;
  const auto needs_rebuild = !cache.valid || cache.key != cache_key;
  if (needs_rebuild) {
    cache.valid = true;
    cache.key = cache_key;
    vertices.clear();
    vertices.reserve(12288U);

    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             0.0F, viewport_width, viewport_height,
                             {0.10F, 0.02F, 0.03F, 0.72F});

    append_hud_beveled_panel_top_left(
        vertices, viewport_width, viewport_height, layout.panel_x,
        layout.panel_y, layout.panel_width, layout.panel_height, 8.0F,
        {0.11F, 0.02F, 0.03F, 0.98F}, {0.24F, 0.07F, 0.09F, 0.94F},
        {0.72F, 0.18F, 0.22F, 0.18F}, {0.05F, 0.01F, 0.02F, 0.82F});

    const auto title_pixel_size = static_cast<float>(
        std::floor(std::clamp(viewport_width * 0.0039F, 4.0F, 7.0F)));
    const auto subtitle_pixel_size = static_cast<float>(
        std::floor(std::clamp(viewport_width * 0.0020F, 2.0F, 3.0F)));

    append_pixel_text(vertices, viewport_width, viewport_height,
                      layout.title_center_x, layout.panel_y + 12.0F,
                      subtitle_pixel_size, kGameDisplayNamePixel,
                      {0.92F, 0.78F, 0.80F, 0.90F}, true);
    append_pixel_text(vertices, viewport_width, viewport_height,
                      layout.title_center_x + title_pixel_size,
                      layout.title_y + title_pixel_size, title_pixel_size,
                      "VOUS ETES MORT", {0.0F, 0.0F, 0.0F, 0.45F}, true);
    append_pixel_text(vertices, viewport_width, viewport_height,
                      layout.title_center_x, layout.title_y, title_pixel_size,
                      "VOUS ETES MORT", {1.0F, 0.95F, 0.96F, 1.0F}, true);
    append_pixel_text(vertices, viewport_width, viewport_height,
                      layout.subtitle_center_x, layout.subtitle_y,
                      subtitle_pixel_size, "LA SURVIE RECOMMENCE ICI",
                      {0.98F, 0.82F, 0.84F, 0.96F}, true);
    append_pixel_text(vertices, viewport_width, viewport_height,
                      layout.cause_center_x, layout.cause_y,
                      subtitle_pixel_size,
                      death_screen_cause_label(death_screen.cause),
                      {0.98F, 0.90F, 0.92F, 0.92F}, true);

    for (const auto &button : layout.buttons) {
      const auto selected = button.selected;
      const auto border_color =
          selected ? std::array<float, 4>{0.98F, 0.96F, 0.98F, 1.0F}
                   : std::array<float, 4>{0.15F, 0.03F, 0.04F, 0.98F};
      const auto fill_color =
          selected ? std::array<float, 4>{0.58F, 0.18F, 0.22F, 0.96F}
                   : std::array<float, 4>{0.36F, 0.11F, 0.13F, 0.94F};
      append_hud_beveled_panel_top_left(
          vertices, viewport_width, viewport_height, button.x, button.y,
          button.width, button.height, 5.0F, border_color, fill_color,
          {1.0F, 1.0F, 1.0F, selected ? 0.18F : 0.08F},
          {0.0F, 0.0F, 0.0F, 0.42F});

      const auto button_pixel_size = static_cast<float>(
          std::floor(std::clamp(button.height / 11.0F, 3.0F, 4.0F)));
      const auto text_y =
          button.y +
          std::floor((button.height - button_pixel_size * 7.0F) * 0.5F);
      append_pixel_text(vertices, viewport_width, viewport_height,
                        button.x + button.width * 0.5F, text_y,
                        button_pixel_size, button.label,
                        {0.0F, 0.0F, 0.0F, 0.38F}, true);
      append_pixel_text(vertices, viewport_width, viewport_height,
                        button.x + button.width * 0.5F, text_y - 1.0F,
                        button_pixel_size, button.label,
                        {1.0F, 0.96F, 0.97F, 1.0F}, true);
    }
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glUseProgram(hud_program_);
  bind_hud_textures();

  upload_hud_vertices(vertices);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_progression_menu(const ProgressionMenuViewModel &menu,
                                     const PlayerBuildState &build, int width,
                                     int height) {
  if (!menu.visible || width <= 0 || height <= 0 || hud_program_ == 0 ||
      hud_vao_ == 0 || hud_vbo_ == 0) {
    return;
  }

  const auto viewport_width = static_cast<float>(width);
  const auto viewport_height = static_cast<float>(height);
  const auto menu_page = construction_plan_view_.active
                             ? ProgressionMenuPage::ConstructionPlan
                             : progression_runtime_hud_view_.menu_page;
  const auto layout = make_progression_menu_layout(width, height, menu_page);
  if (!layout.valid()) {
    return;
  }
  auto vertices = std::vector<HudVertex>{};
  vertices.reserve(32'768U);
  const auto draw_text = [&](float x, float y, float pixel_size,
                             std::string_view text, const HudColor &color,
                             bool centered = false) {
    append_pixel_text(vertices, viewport_width, viewport_height, x + pixel_size,
                      y + pixel_size, pixel_size, text,
                      {0.0F, 0.0F, 0.0F, 0.62F}, centered);
    append_pixel_text(vertices, viewport_width, viewport_height, x, y,
                      pixel_size, text, color, centered);
  };

  const auto panel_width = layout.panel.width;
  const auto panel_height = layout.panel.height;
  const auto panel_x = layout.panel.x;
  const auto panel_y = layout.panel.y;
  const auto accent = HudColor{
      0.96F,
      0.70F,
      0.26F,
      1.0F,
  };
  append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                           0.0F, viewport_width, viewport_height,
                           {0.01F, 0.02F, 0.03F, 0.76F});
  append_hud_shadow_top_left(vertices, viewport_width, viewport_height, panel_x,
                             panel_y, panel_width, panel_height, 24.0F,
                             {0.0F, 0.0F, 0.0F, 0.42F});
  append_stylized_panel_top_left(vertices, viewport_width, viewport_height,
                                 panel_x, panel_y, panel_width, panel_height,
                                 5.0F, make_stone_panel_palette(), false);
  append_hud_rect_top_left(
      vertices, viewport_width, viewport_height, layout.navigation.x,
      layout.navigation.bottom() - 3.0F, layout.navigation.width, 3.0F,
      hud_with_alpha(accent, 0.64F));

  draw_text(layout.title.x + layout.title.width * 0.5F, layout.title.y,
            layout.mode == ProgressionMenuLayoutMode::CompactPages ? 3.0F
                                                                   : 5.0F,
            "PROGRESSION DU PERSONNAGE", {0.99F, 0.96F, 0.86F, 1.0F}, true);
  const auto level_line =
      std::string("NIVEAU ") + std::to_string(menu.level) + "   UTILISABLES " +
      std::to_string(menu.budget.spendable_skill_points) + "   RESERVE " +
      std::to_string(menu.budget.reserved_skill_points) + "   ATTRIBUTS " +
      std::to_string(menu.budget.available_attribute_points) + "   MAITRISES " +
      std::to_string(menu.budget.available_mastery_points);
  draw_text(layout.summary.x + layout.summary.width * 0.5F, layout.summary.y,
            layout.mode == ProgressionMenuLayoutMode::CompactPages ? 1.7F
                                                                   : 2.5F,
            level_line, {0.74F, 0.82F, 0.90F, 0.96F}, true);

  if (layout.mode == ProgressionMenuLayoutMode::CompactPages) {
    constexpr std::array<std::string_view, 4U> kPageLabels{{
        "SORT",
        "ATTRIBUTS",
        "SLOTS",
        "PLAN 3D",
    }};
    const auto selected_page = std::min<std::size_t>(
        static_cast<std::size_t>(menu_page), kPageLabels.size() - 1U);
    const auto navigation_cell_width =
        layout.navigation.width / static_cast<float>(kPageLabels.size());
    for (std::size_t index = 0U; index < kPageLabels.size(); ++index) {
      draw_text(
                layout.navigation.x +
                    navigation_cell_width *
                        (static_cast<float>(
                             index) +
                         0.5F),
                layout.navigation.y +
                    4.0F,
                1.7F,
                kPageLabels[index],
                index == selected_page
                    ? accent
                    : HudColor {
                          0.66F,
                          0.72F,
                          0.80F,
                          0.92F,
                      },
                true);
    }

    const auto content_x = layout.primary_content.x + 4.0F;
    auto content_y = layout.primary_content.y + 4.0F;
    const auto content_step =
        std::clamp(layout.primary_content.height / 9.5F, 17.0F, 23.0F);
    const auto regular = HudColor{
        0.84F,
        0.88F,
        0.94F,
        0.98F,
    };
    if (menu_page == ProgressionMenuPage::Attributes) {
      draw_text(content_x, content_y, 2.3F, "ATTRIBUTS", accent);
      content_y += content_step;
      for (const auto &attribute : menu.attributes) {
        draw_text(content_x, content_y, 2.0F,
                  std::string(attribute.name) + "  " +
                      std::to_string(attribute.allocated_value) + "/" +
                      std::to_string(attribute.allocation_cap),
                  regular);
        content_y += content_step;
      }
    } else if (menu_page == ProgressionMenuPage::Slots) {
      draw_text(content_x, content_y, 2.3F, "RACCOURCIS DE POUVOIR", accent);
      content_y += content_step;
      for (const auto &slot : menu.slots) {
        const auto line = std::string("F") + std::to_string(slot.index + 1U) +
                          " " + std::string(slot.name) + " : " +
                          (slot.ability == AbilityId::None
                               ? std::string("VIDE")
                               : std::string(slot.ability_display_name));
        draw_text(content_x, content_y, 1.9F, line,
                  slot.index == menu.selected_slot ? accent : regular);
        content_y += content_step;
      }
    } else if (menu_page == ProgressionMenuPage::ConstructionPlan) {
      const auto plan_index =
          construction_plan_view_.active
              ? std::min(construction_plan_view_.selected_plan,
                         construction_plan_view_.plan_count - 1U)
              : std::min<std::size_t>(build.selected_construction_plan,
                                      build.construction_plans.size() - 1U);
      const auto &saved_plan = build.construction_plans[plan_index];
      const auto cell_count = construction_plan_view_.active
                                  ? construction_plan_view_.cell_count
                                  : saved_plan.cell_count;
      const auto mirrored = construction_plan_view_.active
                                ? construction_plan_view_.mirrored
                                : saved_plan.mirrored;
      draw_text(content_x, content_y, 2.3F,
                std::string("PLAN ") + std::to_string(plan_index + 1U) +
                    "/3   CELLULES " + std::to_string(cell_count) +
                    "/10   MIROIR " + (mirrored ? "OUI" : "NON"),
                accent);
      content_y += content_step;
      if (construction_plan_view_.active) {
        const auto &cursor = construction_plan_view_.cursor;
        draw_text(
            content_x, content_y, 1.9F,
            std::string("COUCHE Y=") + std::to_string(cursor.y) +
                "   CURSEUR " + std::to_string(cursor.x) + "," +
                std::to_string(cursor.z) + "   MATERIAU " +
                std::to_string(construction_plan_view_.selected_material_id),
            regular);
        content_y += content_step;
        auto displayed_cells = std::size_t{0U};
        for (std::size_t index = 0U;
             index < construction_plan_view_.cell_count &&
             index < construction_plan_view_.cells.size();
             ++index) {
          const auto &cell = construction_plan_view_.cells[index];
          if (!cell.on_selected_layer || displayed_cells >= 6U) {
            continue;
          }
          draw_text(content_x, content_y, 1.8F,
                    std::string(cell.selected ? "> " : "  ") + "X " +
                        std::to_string(cell.position.x) + "  Z " +
                        std::to_string(cell.position.z) + "  MAT " +
                        std::to_string(cell.material_id),
                    cell.selected ? accent : regular);
          content_y += content_step;
          ++displayed_cells;
        }
        if (displayed_cells == 0U) {
          draw_text(content_x, content_y, 1.9F,
                    "AUCUNE CELLULE SUR CETTE COUCHE", regular);
        }
      } else {
        draw_text(content_x, content_y, 2.0F, "C MODIFIER PLAN", regular);
      }
    } else {
      draw_text(content_x, content_y, 2.3F,
                std::string("VOIE : ") + std::string(menu.selected_path_name),
                accent);
      content_y += content_step;
      draw_text(content_x, content_y, 2.1F,
                std::string("PALIER ") + std::to_string(menu.selected_tier) +
                    " - " + std::string(menu.ability.display_name),
                regular);
      content_y += content_step;
      draw_text(
                content_x,
                content_y,
                1.9F,
                std::string("RANG ") +
                    std::to_string(
                        menu.ability
                            .current_rank) +
                    "/3   " +
                    (menu.ability.mastered
                         ? "MAITRISE"
                         : "NON MAITRISE") +
                    "   " +
                    (menu.ability
                             .implemented
                         ? "JOUABLE"
                         : "EN PREPARATION"),
                menu.ability.implemented
                    ? HudColor {
                          0.56F,
                          0.92F,
                          0.62F,
                          1.0F,
                      }
                    : HudColor {
                          0.86F,
                          0.70F,
                          0.44F,
                          1.0F,
                      });
      content_y += content_step;
      draw_text(content_x, content_y, 1.9F,
                std::string("COUT ") +
                    std::to_string(static_cast<int>(
                        std::lround(menu.ability.energy_cost))) +
                    " EV   RECHARGE " +
                    std::to_string(static_cast<int>(
                        std::lround(menu.ability.cooldown_seconds))) +
                    " S   PORTEE " +
                    std::to_string(static_cast<int>(
                        std::lround(menu.ability.range_meters))) +
                    " M",
                {0.50F, 0.78F, 0.96F, 1.0F});
      content_y += content_step;
      draw_text(content_x, content_y, 1.8F,
                std::string("ACHAT : ") +
                    std::string(menu.ability.rank_purchase_status),
                regular);
      content_y += content_step;
      draw_text(content_x, content_y, 1.8F,
                std::string("MAITRISE : ") +
                    std::string(menu.ability.mastery_purchase_status),
                regular);
    }

    if (construction_plan_view_.active) {
      draw_text(layout.footer.x + layout.footer.width * 0.5F,
                layout.footer.y + 1.0F, 1.35F,
                "1-3 PLAN  FLECHES X/Z  PAGEUP/PAGEDOWN Y  ESPACE CELLULE  Q/E "
                "MATERIAU",
                {0.82F, 0.85F, 0.90F, 0.96F}, true);
      draw_text(layout.footer.x + layout.footer.width * 0.5F,
                layout.footer.y + 13.0F, 1.35F,
                "M MIROIR  ENTREE VALIDER  ECHAP ANNULER",
                {0.68F, 0.76F, 0.86F, 0.96F}, true);
    } else {
      draw_text(layout.footer.x + layout.footer.width * 0.5F,
                layout.footer.y + 7.0F, 1.45F,
                "FLECHES CHOISIR  ENTREE ACHETER  M MAITRISER  TAB SLOT  "
                "ESPACE EQUIPER  C MODIFIER PLAN",
                {0.78F, 0.83F, 0.90F, 0.96F}, true);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(hud_program_);
    bind_hud_textures();
    upload_hud_vertices(vertices);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    return;
  }

  if (construction_plan_view_.active) {
    const auto plan_count =
        std::max<std::size_t>(construction_plan_view_.plan_count, 1U);
    const auto plan_index =
        std::min(construction_plan_view_.selected_plan, plan_count - 1U);
    const auto &cursor = construction_plan_view_.cursor;
    auto primary_y = layout.primary_content.y;
    draw_text(layout.primary_content.x, primary_y, 3.2F,
              std::string("PLAN ") + std::to_string(plan_index + 1U) + "/" +
                  std::to_string(plan_count),
              accent);
    primary_y += 38.0F;
    draw_text(layout.primary_content.x, primary_y, 2.4F,
              std::string("CELLULES ") +
                  std::to_string(construction_plan_view_.cell_count) + "/" +
                  std::to_string(construction_plan_view_.maximum_cell_count),
              {0.88F, 0.92F, 0.97F, 1.0F});
    primary_y += 30.0F;
    draw_text(layout.primary_content.x, primary_y, 2.4F,
              std::string("COUCHE Y = ") + std::to_string(cursor.y),
              {0.70F, 0.84F, 0.96F, 1.0F});
    primary_y += 30.0F;
    draw_text(layout.primary_content.x, primary_y, 2.4F,
              std::string("CURSEUR X/Z = ") + std::to_string(cursor.x) + " / " +
                  std::to_string(cursor.z),
              {0.70F, 0.84F, 0.96F, 1.0F});
    primary_y += 30.0F;
    draw_text(layout.primary_content.x, primary_y, 2.4F,
              std::string("MATERIAU = ") +
                  std::to_string(construction_plan_view_.selected_material_id),
              {0.86F, 0.80F, 0.58F, 1.0F});
    primary_y += 30.0F;
    draw_text(
            layout.primary_content.x,
            primary_y,
            2.4F,
            std::string("MIROIR = ") +
                (construction_plan_view_
                         .mirrored
                     ? "OUI"
                     : "NON"),
            construction_plan_view_
                    .mirror_unlocked
                ? HudColor {
                      0.62F,
                      0.92F,
                      0.70F,
                      1.0F,
                  }
                : HudColor {
                      0.72F,
                      0.62F,
                      0.50F,
                      1.0F,
                  });

    auto secondary_y = layout.secondary_content.y;
    draw_text(layout.secondary_content.x, secondary_y, 3.0F,
              "CELLULES DE LA COUCHE", accent);
    secondary_y += 34.0F;
    auto displayed_cells = std::size_t{0U};
    for (std::size_t index = 0U; index < construction_plan_view_.cell_count &&
                                 index < construction_plan_view_.cells.size();
         ++index) {
      const auto &cell = construction_plan_view_.cells[index];
      if (!cell.on_selected_layer) {
        continue;
      }
      draw_text(layout.secondary_content.x, secondary_y, 2.1F,
                std::string(cell.selected ? "> " : "  ") + "X " +
                    std::to_string(cell.position.x) + "  Z " +
                    std::to_string(cell.position.z) + "  MAT " +
                    std::to_string(cell.material_id),
                cell.selected ? accent
                              : HudColor{
                                    0.82F,
                                    0.86F,
                                    0.92F,
                                    0.98F,
                                });
      secondary_y += 26.0F;
      ++displayed_cells;
    }
    if (displayed_cells == 0U) {
      draw_text(layout.secondary_content.x, secondary_y, 2.1F, "AUCUNE CELLULE",
                {0.72F, 0.76F, 0.82F, 0.94F});
    }

    draw_text(layout.footer.x + layout.footer.width * 0.5F, layout.footer.y,
              1.8F,
              "1-3 PLAN  FLECHES X/Z  PAGEUP/PAGEDOWN Y  ESPACE CELLULE  Q/E "
              "MATERIAU",
              {0.82F, 0.85F, 0.90F, 0.96F}, true);
    draw_text(layout.footer.x + layout.footer.width * 0.5F,
              layout.footer.y + 18.0F, 1.8F,
              "M MIROIR  ENTREE VALIDER  ECHAP ANNULER",
              {0.68F, 0.76F, 0.86F, 0.96F}, true);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(hud_program_);
    bind_hud_textures();
    upload_hud_vertices(vertices);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    return;
  }

  const auto left_x = layout.primary_content.x;
  const auto right_x = layout.secondary_content.x;
  auto left_y = layout.primary_content.y;
  draw_text(left_x, left_y, 3.5F,
            std::string("VOIE : ") + std::string(menu.selected_path_name),
            accent);
  left_y += 34.0F;
  draw_text(left_x, left_y, 3.0F,
            std::string("PALIER ") + std::to_string(menu.selected_tier) +
                " - " + std::string(menu.ability.display_name),
            {0.92F, 0.94F, 0.98F, 1.0F});
  left_y += 30.0F;
  const auto state_line =
      std::string("RANG ") + std::to_string(menu.ability.current_rank) +
      "/3   " + (menu.ability.mastered ? "MAITRISE" : "NON MAITRISE") + "   " +
      (menu.ability.implemented ? "JOUABLE" : "EN PREPARATION");
  draw_text(
        left_x,
        left_y,
        2.5F,
        state_line,
        menu.ability.implemented
            ? HudColor {
                  0.56F,
                  0.92F,
                  0.62F,
                  1.0F,
              }
            : HudColor {
                  0.86F,
                  0.70F,
                  0.44F,
                  1.0F,
              });
  left_y += 34.0F;

  const auto ability_data =
      std::string("EV ") +
      std::to_string(static_cast<int>(std::lround(menu.ability.energy_cost))) +
      "   RECHARGE " +
      std::to_string(
          static_cast<int>(std::lround(menu.ability.cooldown_seconds))) +
      " S   PORTEE " +
      std::to_string(static_cast<int>(std::lround(menu.ability.range_meters))) +
      " M";
  draw_text(left_x, left_y, 2.5F, ability_data, {0.50F, 0.78F, 0.96F, 1.0F});
  left_y += 34.0F;
  draw_text(left_x, left_y, 2.4F,
            std::string("ACHAT : ") +
                std::string(menu.ability.rank_purchase_status),
            {0.82F, 0.84F, 0.88F, 0.94F});
  left_y += 26.0F;
  draw_text(left_x, left_y, 2.4F,
            std::string("MAITRISE : ") +
                std::string(menu.ability.mastery_purchase_status),
            {0.82F, 0.84F, 0.88F, 0.94F});

  auto right_y = layout.secondary_content.y;
  draw_text(right_x, right_y, 3.0F, "ATTRIBUTS", accent);
  right_y += 30.0F;
  for (const auto &attribute : menu.attributes) {
    const auto line = std::string(attribute.name) + "  " +
                      std::to_string(attribute.allocated_value) + "/" +
                      std::to_string(attribute.allocation_cap);
    draw_text(right_x, right_y, 2.5F, line, {0.88F, 0.90F, 0.94F, 1.0F});
    right_y += 25.0F;
  }

  right_y += 18.0F;
  draw_text(right_x, right_y, 3.0F, "RACCOURCIS DE POUVOIR", accent);
  right_y += 30.0F;
  for (const auto &slot : menu.slots) {
    const auto line = std::string("F") + std::to_string(slot.index + 1U) +
                      "  " + std::string(slot.name) + " : " +
                      (slot.ability == AbilityId::None
                           ? std::string("VIDE")
                           : std::string(slot.ability_display_name));
    draw_text(
            right_x,
            right_y,
            2.3F,
            line,
            slot.index ==
                    menu.selected_slot
                ? HudColor {
                      0.99F,
                      0.86F,
                      0.48F,
                      1.0F,
                  }
                : HudColor {
                      0.76F,
                      0.80F,
                      0.86F,
                      0.96F,
                  });
    right_y += 24.0F;
  }

  draw_text(layout.footer.x + layout.footer.width * 0.5F, layout.footer.y, 1.9F,
            "FLECHES CHOISIR   ENTREE ACHETER   M MAITRISER   TAB SLOT   "
            "ESPACE EQUIPER   C MODIFIER PLAN",
            {0.82F, 0.85F, 0.90F, 0.96F}, true);
  draw_text(
      layout.footer.x + layout.footer.width * 0.5F, layout.footer.y + 18.0F,
      1.9F,
      "1 FORCE   2 SAGESSE   3 AGILITE   4 ROBUSTESSE   P OU ECHAP FERMER",
      {0.68F, 0.76F, 0.86F, 0.96F}, true);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(hud_program_);
  bind_hud_textures();
  upload_hud_vertices(vertices);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  (void)build;
}

void Renderer::draw_progression_ability_hud(const PlayerBuildState &build,
                                            int width, int height) {
  if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 ||
      hud_vbo_ == 0) {
    return;
  }

  const auto viewport_width = static_cast<float>(width);
  const auto viewport_height = static_cast<float>(height);
  auto vertices = std::vector<HudVertex>{};
  vertices.reserve(8'192U);
  const auto snapshot = make_progression_ability_hud_snapshot(
      build, progression_runtime_hud_view_);
  if (!snapshot.visible) {
    return;
  }
  const auto layout = make_progression_ability_hud_layout(width, height);
  if (!layout.valid()) {
    return;
  }
  auto feedback_hash = std::uint32_t{2'166'136'261U};
  for (const auto character : snapshot.feedback_assets.visual_id) {
    feedback_hash ^=
        static_cast<std::uint32_t>(static_cast<unsigned char>(character));
    feedback_hash *= 16'777'619U;
  }
  const auto feedback_mix = static_cast<float>(feedback_hash & 0xFFU) / 255.0F;
  const auto feedback_accent = HudColor{
      0.20F + feedback_mix * 0.18F,
      0.62F + feedback_mix * 0.18F,
      0.94F - feedback_mix * 0.12F,
      0.98F,
  };
  append_stylized_panel_top_left(vertices, viewport_width, viewport_height,
                                 layout.panel.x, layout.panel.y,
                                 layout.panel.width, layout.panel.height, 3.0F,
                                 make_slate_panel_palette(), false);
  const auto ratio = std::clamp(snapshot.current_energy /
                                    std::max(snapshot.maximum_energy, 1.0F),
                                0.0F, 1.0F);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           layout.energy.x, layout.energy.y,
                           layout.energy.width, layout.energy.height,
                           {0.03F, 0.08F, 0.12F, 0.95F});
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           layout.energy.x, layout.energy.y,
                           layout.energy.width * ratio, layout.energy.height,
                           feedback_accent);
  const auto energy_label =
      std::string("EV ") +
      std::to_string(static_cast<int>(std::lround(snapshot.current_energy))) +
      "/" +
      std::to_string(static_cast<int>(std::lround(snapshot.maximum_energy)));
  append_pixel_text(vertices, viewport_width, viewport_height,
                    layout.energy.x + layout.energy.width * 0.5F,
                    layout.energy.y + 1.0F, 1.35F, energy_label,
                    {0.90F, 0.96F, 1.0F, 1.0F}, true);

  const auto format_seconds = [](float seconds) {
    const auto tenths =
        std::max(0, static_cast<int>(std::lround(seconds * 10.0F)));
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) +
           "S";
  };
  const auto text_pixel_size = layout.panel.width < 320.0F ? 1.3F : 1.55F;
  const auto ability_label =
      std::string(snapshot.display_name) + "  COUT " +
      std::to_string(static_cast<int>(std::lround(snapshot.energy_cost))) +
      " EV";
  append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.ability.x,
        layout.ability.y +
            3.0F,
        text_pixel_size,
        ability_label,
        snapshot.energy_insufficient
            ? HudColor {
                  1.0F,
                  0.46F,
                  0.36F,
                  1.0F,
              }
            : HudColor {
                  0.94F,
                  0.92F,
                  0.80F,
                  0.98F,
              });
  const auto timer_label =
      std::string("GCD ") + format_seconds(snapshot.global_cooldown_remaining) +
      "  CD " + format_seconds(snapshot.cooldown_remaining) + "  CH " +
      std::to_string(snapshot.charges) + "/" +
      std::to_string(snapshot.maximum_charges) + "  ACT " +
      format_seconds(snapshot.active_duration_remaining);
  append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.timers.x,
        layout.timers.y +
            3.0F,
        text_pixel_size,
        timer_label,
        snapshot.energy_insufficient
            ? HudColor {
                  1.0F,
                  0.58F,
                  0.42F,
                  1.0F,
              }
            : HudColor {
                  0.70F,
                  0.86F,
                  0.98F,
                  0.98F,
              });
  const auto effects_label =
      std::string(snapshot.wind_blade_armed ? "LAME OUI" : "LAME NON") +
      (snapshot.wind_dodge_ready ? "  ESQ OUI" : "  ESQ NON") +
      (snapshot.iron_guard_active ? "  GARDE OUI" : "  GARDE NON") + "  FANT " +
      std::to_string(snapshot.active_footmen);
  auto effects_y = layout.effects.y;
  const auto effects_pixel_size =
      snapshot.energy_insufficient ? 1.15F : text_pixel_size;
  if (snapshot.energy_insufficient) {
    append_pixel_text(vertices, viewport_width, viewport_height,
                      layout.effects.x, effects_y, effects_pixel_size,
                      "ENERGIE INSUFFISANTE", {1.0F, 0.48F, 0.34F, 1.0F});
    effects_y += 11.0F;
  }
  append_pixel_text(vertices, viewport_width, viewport_height, layout.effects.x,
                    effects_y, effects_pixel_size, effects_label,
                    {0.78F, 0.90F, 0.82F, 0.98F});
  const auto points_label =
      std::string("POINTS UTIL ") +
      std::to_string(progression_menu_view_.budget.spendable_skill_points) +
      "  RESERVE " +
      std::to_string(progression_menu_view_.budget.reserved_skill_points);
  append_pixel_text(vertices, viewport_width, viewport_height, layout.effects.x,
                    effects_y + (snapshot.energy_insufficient ? 11.0F : 15.0F),
                    effects_pixel_size, points_label,
                    {0.92F, 0.82F, 0.52F, 0.98F});

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(hud_program_);
  bind_hud_textures();
  upload_hud_vertices(vertices);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_pause_menu(const PauseMenuState &pause_menu, int width,
                               int height) {
  if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 ||
      hud_vbo_ == 0) {
    return;
  }

  const auto layout = build_pause_menu_layout(width, height, pause_menu);
  const auto viewport_width = static_cast<float>(width);
  const auto viewport_height = static_cast<float>(height);
  PauseHudCacheKey cache_key{};
  cache_key.pause_menu = pause_menu;
  cache_key.width = width;
  cache_key.height = height;

  auto &cache = pause_cache_;
  auto &vertices = cache.vertices;
  const auto needs_rebuild = !cache.valid || cache.key != cache_key;
  if (needs_rebuild) {
    cache.valid = true;
    cache.key = cache_key;
    vertices.clear();
    vertices.reserve(16384U);

    const auto draw_text = [&](float x, float y, float pixel_size,
                               std::string_view text, const HudColor &color,
                               bool centered = false) {
      append_pixel_text(vertices, viewport_width, viewport_height,
                        x + pixel_size, y + pixel_size, pixel_size, text,
                        {0.0F, 0.0F, 0.0F, 0.58F}, centered);
      append_pixel_text(vertices, viewport_width, viewport_height, x, y,
                        pixel_size, text, color, centered);
    };

    const auto primary_accent = HudColor{0.96F, 0.74F, 0.32F, 1.0F};
    const auto secondary_accent = HudColor{0.34F, 0.72F, 0.92F, 1.0F};
    const auto title_pixel_size = static_cast<float>(
        std::floor(std::clamp(layout.panel_width / 100.0F, 4.0F, 5.0F)));
    const auto subtitle_pixel_size = static_cast<float>(
        std::floor(std::clamp(layout.panel_width / 160.0F, 2.0F, 3.0F)));

    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             0.0F, viewport_width, viewport_height,
                             {0.02F, 0.02F, 0.03F, 0.66F});
    const auto vignette_edge = std::clamp(
        std::min(viewport_width, viewport_height) * 0.22F, 72.0F, 210.0F);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             0.0F, viewport_width, vignette_edge,
                             {0.10F, 0.10F, 0.12F, 0.10F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             viewport_height - vignette_edge, viewport_width,
                             vignette_edge, {0.0F, 0.0F, 0.02F, 0.24F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             0.0F, vignette_edge, viewport_height,
                             {0.0F, 0.0F, 0.02F, 0.12F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             viewport_width - vignette_edge, 0.0F,
                             vignette_edge, viewport_height,
                             {0.0F, 0.0F, 0.02F, 0.12F});

    append_hud_shadow_top_left(vertices, viewport_width, viewport_height,
                               layout.panel_x, layout.panel_y,
                               layout.panel_width, layout.panel_height, 20.0F,
                               {0.0F, 0.0F, 0.0F, 0.32F});
    append_stylized_panel_top_left(vertices, viewport_width, viewport_height,
                                   layout.panel_x, layout.panel_y,
                                   layout.panel_width, layout.panel_height,
                                   5.0F, make_stone_panel_palette(), false);
    append_hud_scanlines_top_left(vertices, viewport_width, viewport_height,
                                  layout.panel_x + 8.0F, layout.panel_y + 8.0F,
                                  std::max(0.0F, layout.panel_width - 16.0F),
                                  std::max(0.0F, layout.panel_height - 16.0F),
                                  12.0F, {1.0F, 1.0F, 1.0F, 0.018F});
    append_hud_rect_top_left(
        vertices, viewport_width, viewport_height, layout.accent_rail_x,
        layout.accent_rail_y, layout.accent_rail_width,
        layout.accent_rail_height, hud_with_alpha(primary_accent, 0.52F));
    append_hud_rect_top_left(
        vertices, viewport_width, viewport_height,
        layout.accent_rail_x + layout.accent_rail_width + 3.0F,
        layout.accent_rail_y, std::max(2.0F, layout.accent_rail_width * 0.70F),
        layout.accent_rail_height, hud_with_alpha(secondary_accent, 0.18F));

    append_stylized_panel_top_left(
        vertices, viewport_width, viewport_height, layout.header_panel_x,
        layout.header_panel_y, layout.header_panel_width,
        layout.header_panel_height, 4.0F,
        make_warm_panel_palette(primary_accent), false);
    append_stylized_panel_top_left(
        vertices, viewport_width, viewport_height, layout.footer_panel_x,
        layout.footer_panel_y, layout.footer_panel_width,
        layout.footer_panel_height, 3.0F, make_slate_panel_palette(), false);

    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             layout.header_panel_x + 14.0F,
                             layout.header_panel_y +
                                 layout.header_panel_height - 10.0F,
                             std::max(0.0F, layout.header_panel_width - 28.0F),
                             2.0F, hud_with_alpha(primary_accent, 0.20F));
    append_corner_brackets_top_left(
        vertices, viewport_width, viewport_height, layout.panel_x + 7.0F,
        layout.panel_y + 7.0F, std::max(0.0F, layout.panel_width - 14.0F),
        std::max(0.0F, layout.panel_height - 14.0F), 4.0F,
        {1.0F, 1.0F, 1.0F, 0.08F});

    draw_text(layout.brand_center_x, layout.brand_y, subtitle_pixel_size,
              kGameDisplayNamePixel, {0.84F, 0.86F, 0.90F, 0.86F}, true);
    draw_text(layout.title_center_x, layout.title_y, title_pixel_size,
              "JEU EN PAUSE", {0.99F, 0.98F, 0.94F, 1.0F}, true);
    draw_text(layout.subtitle_center_x, layout.subtitle_y, subtitle_pixel_size,
              "SESSION SUSPENDUE", {0.86F, 0.88F, 0.92F, 0.94F}, true);

    for (std::size_t index = 0; index < layout.buttons.size(); ++index) {
      const auto &button = layout.buttons[index];
      const auto selected_accent =
          button.selected ? primary_accent : secondary_accent;
      const auto button_palette =
          !button.enabled
              ? make_slate_panel_palette()
              : (button.selected ? make_warm_panel_palette(primary_accent)
                                 : make_slate_panel_palette());
      if (button.selected) {
        append_hud_shadow_top_left(vertices, viewport_width, viewport_height,
                                   button.x - 3.0F, button.y - 3.0F,
                                   button.width + 6.0F, button.height + 6.0F,
                                   7.0F, hud_with_alpha(primary_accent, 0.16F));
        append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                                 button.x - 3.0F, button.y - 3.0F,
                                 button.width + 6.0F, button.height + 6.0F,
                                 hud_with_alpha(primary_accent, 0.06F));
      }

      append_stylized_panel_top_left(vertices, viewport_width, viewport_height,
                                     button.x, button.y, button.width,
                                     button.height, 4.0F, button_palette, true);
      append_hud_rect_top_left(
          vertices, viewport_width, viewport_height, button.x + 7.0F,
          button.y + 7.0F, 4.0F, std::max(0.0F, button.height - 14.0F),
          hud_with_alpha(selected_accent, button.selected ? 0.68F : 0.22F));

      const auto chip_size = std::clamp(button.height - 18.0F, 18.0F, 28.0F);
      const auto chip_x = button.x + 18.0F;
      const auto chip_y = button.y + (button.height - chip_size) * 0.5F;
      append_stylized_panel_top_left(
          vertices, viewport_width, viewport_height, chip_x, chip_y, chip_size,
          chip_size, 2.0F,
          button.selected ? make_warm_panel_palette(primary_accent)
                          : make_slate_panel_palette(),
          false);

      const auto number_label = std::to_string(index + 1U);
      const auto chip_pixel_size = std::max(2.0F, subtitle_pixel_size);
      draw_text(chip_x + chip_size * 0.5F,
                chip_y +
                    std::max(0.0F, (chip_size - chip_pixel_size * 7.0F) * 0.5F),
                chip_pixel_size, number_label,
                button.selected ? HudColor{0.99F, 0.96F, 0.84F, 1.0F}
                                : HudColor{0.70F, 0.74F, 0.82F, 0.92F},
                true);

      auto button_pixel_size = static_cast<float>(
          std::floor(std::clamp(button.height / 12.0F, 3.0F, 4.0F)));
      const auto label_x = chip_x + chip_size + 16.0F;
      const auto label_max_width =
          std::max(24.0F, button.x + button.width - label_x - 34.0F);
      while (button_pixel_size > 2.0F &&
             measure_pixel_text(button.label, button_pixel_size) >
                 label_max_width) {
        button_pixel_size -= 1.0F;
      }
      const auto text_y =
          button.y + static_cast<float>(std::floor(
                         (button.height - button_pixel_size * 7.0F) * 0.5F));
      draw_text(label_x, text_y, button_pixel_size, button.label,
                !button.enabled
                    ? HudColor{0.58F, 0.60F, 0.66F, 0.72F}
                    : (button.selected ? HudColor{1.0F, 0.98F, 0.90F, 1.0F}
                                       : HudColor{0.90F, 0.92F, 0.96F, 0.96F}));

      if (button.selected) {
        draw_text(button.x + button.width - 22.0F, text_y, button_pixel_size,
                  ">", {0.99F, 0.86F, 0.48F, 0.96F});
      }
    }

    auto footer_text =
        std::string("ENTREE / ESPACE VALIDER    ECHAP REPRENDRE");
    auto footer_pixel_size = subtitle_pixel_size;
    while (footer_pixel_size > 2.0F &&
           measure_pixel_text(footer_text, footer_pixel_size) >
               layout.footer_panel_width - 20.0F) {
      footer_pixel_size -= 1.0F;
    }
    if (measure_pixel_text(footer_text, footer_pixel_size) >
        layout.footer_panel_width - 20.0F) {
      footer_text = "ENTREE VALIDER  ECHAP REPRENDRE";
    }
    draw_text(layout.footer_center_x, layout.footer_y, footer_pixel_size,
              footer_text, {0.80F, 0.83F, 0.88F, 0.94F}, true);
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glUseProgram(hud_program_);
  bind_hud_textures();

  upload_hud_vertices(vertices);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_main_menu(const MainMenuState &main_menu, int width,
                              int height) {
  if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 ||
      hud_vbo_ == 0) {
    return;
  }

  const auto layout = build_main_menu_layout(width, height, main_menu);
  const auto viewport_width = static_cast<float>(width);
  const auto viewport_height = static_cast<float>(height);
  MainMenuHudCacheKey cache_key{};
  cache_key.main_menu = main_menu;
  cache_key.width = width;
  cache_key.height = height;

  auto &cache = main_menu_cache_;
  auto &vertices = cache.vertices;
  const auto needs_rebuild = !cache.valid || cache.key != cache_key;
  if (needs_rebuild) {
    cache.valid = true;
    cache.key = cache_key;
    vertices.clear();
    vertices.reserve(16384U);

    const auto draw_text = [&](float x, float y, float pixel_size,
                               std::string_view text, const HudColor &color,
                               bool centered = false) {
      append_pixel_text(vertices, viewport_width, viewport_height,
                        x + pixel_size, y + pixel_size, pixel_size, text,
                        {0.0F, 0.0F, 0.0F, 0.45F}, centered);
      append_pixel_text(vertices, viewport_width, viewport_height, x, y,
                        pixel_size, text, color, centered);
    };

    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             0.0F, viewport_width, viewport_height,
                             {0.03F, 0.04F, 0.05F, 0.42F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             0.0F, viewport_width, viewport_height * 0.32F,
                             {0.04F, 0.05F, 0.06F, 0.18F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             viewport_height * 0.68F, viewport_width,
                             viewport_height * 0.32F,
                             {0.01F, 0.02F, 0.03F, 0.28F});
    append_hud_rect_top_left(
        vertices, viewport_width, viewport_height, viewport_width * 0.18F, 0.0F,
        viewport_width * 0.64F, viewport_height, {0.32F, 0.28F, 0.18F, 0.06F});
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             viewport_width * 0.22F, layout.hero_y + 92.0F,
                             viewport_width * 0.56F, 2.0F,
                             {1.0F, 0.84F, 0.48F, 0.10F});

    const auto title_pixel_size =
        std::floor(std::clamp(viewport_width * 0.0064F, 6.0F, 11.0F));
    const auto logo_glow = HudColor{0.96F, 0.82F, 0.46F, 0.16F};
    append_hud_shadow_top_left(
        vertices, viewport_width, viewport_height,
        layout.hero_center_x -
            measure_pixel_text("VALCRAFT", title_pixel_size) * 0.5F - 20.0F,
        layout.hero_y - 16.0F,
        measure_pixel_text("VALCRAFT", title_pixel_size) + 40.0F,
        title_pixel_size * 8.0F + 24.0F, 26.0F, logo_glow);
    draw_text(layout.hero_center_x, layout.hero_y, title_pixel_size, "VALCRAFT",
              {0.16F, 0.12F, 0.05F, 0.88F}, true);
    draw_text(layout.hero_center_x - 2.0F, layout.hero_y - 2.0F,
              title_pixel_size, "VALCRAFT", {1.00F, 0.86F, 0.54F, 0.96F}, true);
    draw_text(layout.hero_center_x, layout.hero_y - 4.0F, title_pixel_size,
              "VALCRAFT", {0.98F, 0.95F, 0.88F, 1.0F}, true);
    draw_text(layout.tagline_center_x, layout.tagline_y,
              std::floor(std::clamp(viewport_width * 0.0019F, 2.0F, 4.0F)),
              "CONSTRUIRE  EXPLORER  SURVIVRE", {0.92F, 0.93F, 0.96F, 0.92F},
              true);

    for (const auto &button : layout.buttons) {
      const auto palette =
          button.selected ? make_warm_panel_palette({0.96F, 0.78F, 0.34F, 1.0F})
                          : make_slate_panel_palette();
      append_stylized_panel_top_left(vertices, viewport_width, viewport_height,
                                     button.x, button.y, button.width,
                                     button.height, 4.0F, palette, true);

      const auto button_pixel_size =
          std::floor(std::clamp(button.height / 11.0F, 3.0F, 4.0F));
      draw_text(
          button.x + button.width * 0.5F,
          button.y +
              std::floor((button.height - button_pixel_size * 7.0F) * 0.5F),
          button_pixel_size, button.label,
          button.selected ? HudColor{1.0F, 0.98F, 0.92F, 1.0F}
                          : HudColor{0.92F, 0.94F, 0.98F, 0.96F},
          true);
    }

    draw_text(viewport_width * 0.5F,
              layout.button_stack_y + layout.button_stack_height + 26.0F, 2.0F,
              "ENTREE POUR VALIDER", {0.80F, 0.82F, 0.86F, 0.84F}, true);
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(hud_program_);
  bind_hud_textures();
  upload_hud_vertices(vertices);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_save_slot_menu(const SaveSlotMenuState &save_slot_menu,
                                   int width, int height) {
  if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 ||
      hud_vbo_ == 0) {
    return;
  }

  const auto layout =
      build_save_slot_menu_layout(width, height, save_slot_menu);
  const auto viewport_width = static_cast<float>(width);
  const auto viewport_height = static_cast<float>(height);
  SaveSlotHudCacheKey cache_key{};
  cache_key.save_slot_menu = save_slot_menu;
  cache_key.width = width;
  cache_key.height = height;

  auto &cache = save_slot_cache_;
  auto &vertices = cache.vertices;
  const auto needs_rebuild = !cache.valid || cache.key != cache_key;
  if (needs_rebuild) {
    cache.valid = true;
    cache.key = cache_key;
    vertices.clear();
    vertices.reserve(32768U);

    const auto draw_text = [&](float x, float y, float pixel_size,
                               std::string_view text, const HudColor &color,
                               bool centered = false) {
      append_pixel_text(vertices, viewport_width, viewport_height,
                        x + pixel_size, y + pixel_size, pixel_size, text,
                        {0.0F, 0.0F, 0.0F, 0.44F}, centered);
      append_pixel_text(vertices, viewport_width, viewport_height, x, y,
                        pixel_size, text, color, centered);
    };

    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             0.0F, viewport_width, viewport_height,
                             {0.02F, 0.03F, 0.04F, 0.52F});
    append_stylized_panel_top_left(vertices, viewport_width, viewport_height,
                                   layout.panel_x, layout.panel_y,
                                   layout.panel_width, layout.panel_height,
                                   5.0F, make_stone_panel_palette(), false);

    const auto title_pixel_size =
        std::floor(std::clamp(viewport_width * 0.0038F, 4.0F, 6.0F));
    const auto subtitle_pixel_size =
        std::floor(std::clamp(viewport_width * 0.0018F, 2.0F, 3.0F));
    draw_text(layout.title_center_x, layout.title_y, title_pixel_size,
              save_slot_menu_title(save_slot_menu), {0.98F, 0.97F, 0.94F, 1.0F},
              true);
    draw_text(layout.subtitle_center_x, layout.subtitle_y, subtitle_pixel_size,
              save_slot_menu_subtitle(save_slot_menu),
              {0.82F, 0.84F, 0.88F, 0.94F}, true);

    for (const auto &card : layout.cards) {
      auto palette =
          card.selected
              ? make_warm_panel_palette(
                    card.occupied ? HudColor{0.92F, 0.74F, 0.34F, 1.0F}
                                  : HudColor{0.70F, 0.86F, 0.98F, 1.0F})
              : make_slate_panel_palette();
      if (!card.enabled) {
        palette.fill = {0.13F, 0.14F, 0.16F, 0.84F};
        palette.highlight = {0.20F, 0.20F, 0.22F, 0.12F};
      }
      append_stylized_panel_top_left(vertices, viewport_width, viewport_height,
                                     card.x, card.y, card.width, card.height,
                                     3.0F, palette, true);

      const auto heading_size = 3.0F;
      const auto body_size = 2.0F;
      const auto padding = 12.0F;
      const auto slot_label =
          std::string("SLOT ") +
          std::to_string(static_cast<int>(card.slot_index + 1U));
      draw_text(card.x + padding, card.y + 10.0F, heading_size, slot_label,
                {0.98F, 0.99F, 1.0F, 0.98F});

      if (card.delete_visible) {
        const auto delete_palette =
            card.delete_hovered
                ? make_warm_panel_palette({0.88F, 0.33F, 0.27F, 1.0F})
                : make_slate_panel_palette();
        append_stylized_panel_top_left(
            vertices, viewport_width, viewport_height, card.delete_x,
            card.delete_y, card.delete_size, card.delete_size, 2.0F,
            delete_palette, true);
        draw_text(card.delete_x + card.delete_size * 0.5F,
                  card.delete_y + std::floor((card.delete_size - 21.0F) * 0.5F),
                  3.0F, "X", {0.98F, 0.97F, 0.95F, 0.98F}, true);
      }

      if (!card.occupied) {
        draw_text(card.x + padding, card.y + 34.0F, body_size, "VIDE",
                  {0.74F, 0.78F, 0.84F, 0.90F});
        if (save_slot_menu.mode == SaveSlotMenuMode::NewGame) {
          const auto mode_text =
              format_save_slot_mode(save_slot_menu.new_game_mode);
          draw_text(card.x + padding, card.y + 52.0F, body_size, mode_text,
                    {0.62F, 0.78F, 0.90F, 0.90F});
        }
        if (card.active_slot) {
          draw_text(card.x + card.width - 50.0F, card.y + 10.0F, body_size,
                    "ACTIF", {1.0F, 0.88F, 0.56F, 0.94F});
        }
        continue;
      }

      const auto timestamp =
          format_save_slot_timestamp(card.metadata.saved_at_unix_seconds);
      const auto seed_text = format_save_slot_seed(card.metadata.seed);
      const auto time_text = format_save_slot_time(card.metadata.time_of_day);
      const auto mode_text = format_save_slot_mode(card.metadata.game_mode);
      draw_text(card.x + padding, card.y + 34.0F, body_size, timestamp,
                {0.86F, 0.88F, 0.92F, 0.96F});
      draw_text(card.x + padding, card.y + 52.0F, body_size, seed_text,
                {0.80F, 0.83F, 0.88F, 0.94F});
      draw_text(card.x + padding, card.y + 70.0F, body_size, time_text,
                {0.80F, 0.83F, 0.88F, 0.94F});
      const auto mode_x = std::max(
          card.x + padding, card.x + card.width - padding -
                                measure_pixel_text(mode_text, body_size));
      draw_text(mode_x, card.y + 70.0F, body_size, mode_text,
                {0.64F, 0.82F, 0.94F, 0.90F});
      if (card.active_slot) {
        const auto active_x = card.delete_visible ? card.delete_x - 52.0F
                                                  : card.x + card.width - 50.0F;
        draw_text(active_x, card.y + 10.0F, body_size, "ACTIF",
                  {1.0F, 0.88F, 0.56F, 0.94F});
      }
    }

    const auto back_palette =
        layout.back_button.selected
            ? make_warm_panel_palette({0.88F, 0.72F, 0.34F, 1.0F})
            : make_slate_panel_palette();
    append_stylized_panel_top_left(
        vertices, viewport_width, viewport_height, layout.back_button.x,
        layout.back_button.y, layout.back_button.width,
        layout.back_button.height, 3.0F, back_palette, true);
    draw_text(layout.back_button.x + layout.back_button.width * 0.5F,
              layout.back_button.y +
                  std::floor((layout.back_button.height - 21.0F) * 0.5F),
              3.0F, "RETOUR", {0.96F, 0.97F, 0.99F, 0.98F}, true);
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(hud_program_);
  bind_hud_textures();
  upload_hud_vertices(vertices);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_options_menu(const OptionsMenuState &options_menu,
                                 int width, int height) {
  if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 ||
      hud_vbo_ == 0) {
    return;
  }

  const auto layout = build_options_menu_layout(width, height, options_menu);
  const auto viewport_width = static_cast<float>(width);
  const auto viewport_height = static_cast<float>(height);
  OptionsHudCacheKey cache_key{};
  cache_key.options_menu = options_menu;
  cache_key.width = width;
  cache_key.height = height;

  auto &cache = options_cache_;
  auto &vertices = cache.vertices;
  const auto needs_rebuild = !cache.valid || cache.key != cache_key;
  if (needs_rebuild) {
    cache.valid = true;
    cache.key = cache_key;
    vertices.clear();
    vertices.reserve(12288U);

    const auto draw_text = [&](float x, float y, float pixel_size,
                               std::string_view text, const HudColor &color,
                               bool centered = false) {
      append_pixel_text(vertices, viewport_width, viewport_height,
                        x + pixel_size, y + pixel_size, pixel_size, text,
                        {0.0F, 0.0F, 0.0F, 0.44F}, centered);
      append_pixel_text(vertices, viewport_width, viewport_height, x, y,
                        pixel_size, text, color, centered);
    };

    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             0.0F, viewport_width, viewport_height,
                             {0.03F, 0.04F, 0.05F, 0.56F});
    append_stylized_panel_top_left(vertices, viewport_width, viewport_height,
                                   layout.panel_x, layout.panel_y,
                                   layout.panel_width, layout.panel_height,
                                   5.0F, make_stone_panel_palette(), false);

    const auto title_pixel_size =
        std::floor(std::clamp(viewport_width * 0.0038F, 4.0F, 6.0F));
    const auto subtitle_pixel_size =
        std::floor(std::clamp(viewport_width * 0.0018F, 2.0F, 3.0F));
    draw_text(layout.title_center_x, layout.title_y, title_pixel_size,
              "OPTIONS", {0.98F, 0.97F, 0.94F, 1.0F}, true);
    draw_text(layout.subtitle_center_x, layout.subtitle_y, subtitle_pixel_size,
              options_menu_subtitle(options_menu.parent),
              {0.82F, 0.84F, 0.88F, 0.94F}, true);

    for (const auto &button : layout.buttons) {
      const auto palette =
          button.selected ? make_warm_panel_palette({0.90F, 0.74F, 0.34F, 1.0F})
                          : make_slate_panel_palette();
      append_stylized_panel_top_left(vertices, viewport_width, viewport_height,
                                     button.x, button.y, button.width,
                                     button.height, 4.0F, palette, true);
      const auto button_pixel_size =
          std::floor(std::clamp(button.height / 11.0F, 3.0F, 4.0F));
      draw_text(
          button.x + button.width * 0.5F,
          button.y +
              std::floor((button.height - button_pixel_size * 7.0F) * 0.5F),
          button_pixel_size, button.label, {0.96F, 0.97F, 0.99F, 0.98F}, true);
    }
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(hud_program_);
  bind_hud_textures();
  upload_hud_vertices(vertices);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_confirm_dialog(const ConfirmDialogState &confirm_dialog,
                                   int width, int height) {
  if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 ||
      hud_vbo_ == 0) {
    return;
  }

  const auto layout =
      build_confirm_dialog_layout(width, height, confirm_dialog);
  const auto viewport_width = static_cast<float>(width);
  const auto viewport_height = static_cast<float>(height);
  ConfirmHudCacheKey cache_key{};
  cache_key.confirm_dialog = confirm_dialog;
  cache_key.width = width;
  cache_key.height = height;

  auto &cache = confirm_cache_;
  auto &vertices = cache.vertices;
  const auto needs_rebuild = !cache.valid || cache.key != cache_key;
  if (needs_rebuild) {
    cache.valid = true;
    cache.key = cache_key;
    vertices.clear();
    vertices.reserve(8192U);

    const auto draw_text = [&](float x, float y, float pixel_size,
                               std::string_view text, const HudColor &color,
                               bool centered = false) {
      append_pixel_text(vertices, viewport_width, viewport_height,
                        x + pixel_size, y + pixel_size, pixel_size, text,
                        {0.0F, 0.0F, 0.0F, 0.44F}, centered);
      append_pixel_text(vertices, viewport_width, viewport_height, x, y,
                        pixel_size, text, color, centered);
    };

    append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F,
                             0.0F, viewport_width, viewport_height,
                             {0.02F, 0.03F, 0.04F, 0.62F});
    append_stylized_panel_top_left(vertices, viewport_width, viewport_height,
                                   layout.panel_x, layout.panel_y,
                                   layout.panel_width, layout.panel_height,
                                   4.0F, make_stone_panel_palette(), false);

    draw_text(layout.title_center_x, layout.title_y, 4.0F,
              confirm_dialog_title(confirm_dialog.intent),
              {0.98F, 0.97F, 0.94F, 1.0F}, true);
    draw_text(layout.subtitle_center_x, layout.subtitle_y, 2.0F,
              confirm_dialog_subtitle(confirm_dialog.intent),
              {0.84F, 0.86F, 0.90F, 0.94F}, true);

    for (const auto &button : layout.buttons) {
      const auto palette =
          button.selected ? make_warm_panel_palette(
                                button.choice == ConfirmDialogChoice::Confirm
                                    ? HudColor{0.90F, 0.74F, 0.34F, 1.0F}
                                    : HudColor{0.72F, 0.78F, 0.88F, 1.0F})
                          : make_slate_panel_palette();
      append_stylized_panel_top_left(vertices, viewport_width, viewport_height,
                                     button.x, button.y, button.width,
                                     button.height, 3.0F, palette, true);
      draw_text(button.x + button.width * 0.5F,
                button.y + std::floor((button.height - 21.0F) * 0.5F), 3.0F,
                button.label, {0.96F, 0.97F, 0.99F, 0.98F}, true);
    }
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(hud_program_);
  bind_hud_textures();
  upload_hud_vertices(vertices);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_musket_hud(const PlayerMusketView &musket, int width,
                               int height) {
  if (!musket.active || width <= 0 || height <= 0 || hud_program_ == 0 ||
      hud_vao_ == 0 || hud_vbo_ == 0) {
    return;
  }

  const auto layout =
      resolve_musket_hud_layout(width, height, musket.aim_ratio);
  if (!layout.valid) {
    return;
  }
  const auto viewport_width = layout.viewport_width;
  const auto viewport_height = layout.viewport_height;
  const auto hud_scale = layout.scale;
  const auto center_x = layout.center_x;
  const auto center_y = layout.center_y;
  const auto outline = layout.outline;
  const auto dark = std::array<float, 4>{0.015F, 0.018F, 0.022F, 0.92F};
  const auto white = std::array<float, 4>{0.98F, 0.985F, 1.0F, 0.98F};
  const auto red = std::array<float, 4>{0.96F, 0.035F, 0.025F, 1.0F};

  std::vector<HudVertex> vertices;
  vertices.reserve(1'024U);

  const auto append_outlined_rect = [&](float x, float y, float rectangle_width,
                                        float rectangle_height) {
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             x - outline, y - outline,
                             rectangle_width + outline * 2.0F,
                             rectangle_height + outline * 2.0F, dark);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, x, y,
                             rectangle_width, rectangle_height, white);
  };

  for (const auto &branch : layout.branches) {
    append_outlined_rect(branch.x, branch.y, branch.width, branch.height);
  }

  const auto dot_outline_size = layout.dot_outline_size;
  const auto dot_size = layout.dot_size;
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           center_x - dot_outline_size * 0.5F,
                           center_y - dot_outline_size * 0.5F, dot_outline_size,
                           dot_outline_size, dark);
  append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                           center_x - dot_size * 0.5F,
                           center_y - dot_size * 0.5F, dot_size, dot_size, red);

  const auto text_pixel_size = std::max(1.25F, 1.55F * hud_scale);
  const auto text_y = layout.text_y;
  std::string status_text;
  std::string ammo_prefix;
  std::string ammo_suffix;
  auto show_infinite_reserve = false;
  if (musket.reloading()) {
    const auto percentage = std::clamp(
        static_cast<int>(std::lround(
            std::clamp(musket.reload_progress, 0.0F, 1.0F) * 100.0F)),
        0, 100);
    status_text = "RECHARGEMENT " + std::to_string(percentage) + "%";
  } else if (musket.loaded()) {
    ammo_prefix = "1 / ";
    show_infinite_reserve = true;
  } else {
    ammo_prefix = "0 / ";
    ammo_suffix = " - R RECHARGER";
    show_infinite_reserve = true;
  }

  const auto infinity_width = text_pixel_size * 10.0F;
  const auto status_width =
      show_infinite_reserve
          ? measure_pixel_text(ammo_prefix, text_pixel_size) + infinity_width +
                measure_pixel_text(ammo_suffix, text_pixel_size)
          : measure_pixel_text(status_text, text_pixel_size);
  const auto panel_padding_x = 5.0F * hud_scale;
  const auto panel_padding_y = 3.0F * hud_scale;
  const auto text_height = text_pixel_size * 7.0F;
  append_hud_rect_top_left(
      vertices, viewport_width, viewport_height,
      center_x - status_width * 0.5F - panel_padding_x,
      text_y - panel_padding_y, status_width + panel_padding_x * 2.0F,
      text_height + panel_padding_y * 2.0F, {0.01F, 0.012F, 0.016F, 0.58F});
  if (!show_infinite_reserve) {
    append_pixel_text(vertices, viewport_width, viewport_height, center_x,
                      text_y, text_pixel_size, status_text, white, true);
  } else {
    auto cursor_x = center_x - status_width * 0.5F;
    append_pixel_text(vertices, viewport_width, viewport_height, cursor_x,
                      text_y, text_pixel_size, ammo_prefix, white);
    cursor_x += measure_pixel_text(ammo_prefix, text_pixel_size);

    // Je trace moi-meme l'infini pour qu'il existe aussi dans la fonte
    // pixel Legacy et ne depende jamais d'un glyphe optionnel de l'atlas.
    constexpr std::array<std::uint16_t, 5U> kInfinityRows{{
        0b011000110U,
        0b100101001U,
        0b100010001U,
        0b100101001U,
        0b011000110U,
    }};
    for (std::size_t row = 0U; row < kInfinityRows.size(); ++row) {
      for (int column = 0; column < 9; ++column) {
        const auto bit = static_cast<std::uint16_t>(1U << (8 - column));
        if ((kInfinityRows[row] & bit) == 0U) {
          continue;
        }
        append_hud_rect_top_left(
            vertices, viewport_width, viewport_height,
            cursor_x + static_cast<float>(column) * text_pixel_size,
            text_y + (static_cast<float>(row) + 1.0F) * text_pixel_size,
            text_pixel_size, text_pixel_size, white);
      }
    }
    cursor_x += infinity_width;
    append_pixel_text(vertices, viewport_width, viewport_height, cursor_x,
                      text_y, text_pixel_size, ammo_suffix, white);
  }

  if (musket.reloading()) {
    const auto bar_width = std::max(94.0F * hud_scale, status_width);
    const auto bar_height = std::max(2.0F, 2.5F * hud_scale);
    const auto bar_y = text_y + text_height + 6.0F * hud_scale;
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             center_x - bar_width * 0.5F - outline,
                             bar_y - outline, bar_width + outline * 2.0F,
                             bar_height + outline * 2.0F, dark);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height,
                             center_x - bar_width * 0.5F, bar_y, bar_width,
                             bar_height, {0.18F, 0.18F, 0.20F, 0.86F});
    append_hud_rect_top_left(
        vertices, viewport_width, viewport_height, center_x - bar_width * 0.5F,
        bar_y, bar_width * std::clamp(musket.reload_progress, 0.0F, 1.0F),
        bar_height, {0.84F, 0.63F, 0.25F, 0.98F});
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(hud_program_);
  bind_hud_textures();
  upload_hud_vertices(vertices);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_crosshair() {
  glDisable(GL_DEPTH_TEST);
  glUseProgram(crosshair_program_);
  glBindVertexArray(crosshair_vao_);
  glDrawArrays(GL_LINES, 0, 4);
  record_draw_call();
  glEnable(GL_DEPTH_TEST);
}

} // namespace valcraft

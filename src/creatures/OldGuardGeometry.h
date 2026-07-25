#pragma once

#include "creatures/CreatureGeometry.h"
#include "gameplay/OldGuard.h"

#include <vector>

namespace valcraft {

void append_old_guard_parts(std::vector<CreaturePartInstance>& parts,
                            const OldGuardRenderInstance& guard);
[[nodiscard]] auto build_old_guard_parts(const OldGuardRenderInstance& guard)
    -> std::vector<CreaturePartInstance>;
[[nodiscard]] auto build_old_guard_mesh(const OldGuardRenderInstance& guard)
    -> CreatureMeshData;

} // namespace valcraft

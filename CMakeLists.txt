cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

project(ValCraft VERSION 0.1.0 LANGUAGES C CXX)

# Je fournis explicitement à Ninja le préfixe /showIncludes français avec ses
# espaces insécables. Sa détection automatique les réencodait sur certaines
# consoles, supprimait les dépendances de headers et pouvait ainsi conserver
# des objets ABI obsolètes.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "Remarque : inclusion du fichier :")
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "Remarque : inclusion du fichier :")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "Remarque : inclusion du fichier :")
endif()

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)
include(CheckIPOSupported)
enable_testing()
option(VALCRAFT_STRICT_WARNINGS "Enable strict warnings on project targets" ON)
option(VALCRAFT_ENABLE_COVERAGE "Enable gcov coverage for critical modules" OFF)
option(VALCRAFT_ENABLE_ADDRESS_SANITIZER "Instrument ValCraft targets with AddressSanitizer" OFF)
option(VALCRAFT_ENABLE_GPU_INTEGRATION_TESTS "Register SDL/OpenGL smoke tests in the default CTest suite" ON)

check_ipo_supported(RESULT VALCRAFT_IPO_SUPPORTED OUTPUT VALCRAFT_IPO_ERROR)
if(NOT VALCRAFT_IPO_SUPPORTED AND VALCRAFT_IPO_ERROR)
    message(STATUS "IPO/LTO disabled: ${VALCRAFT_IPO_ERROR}")
endif()

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
set(SDL_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    sdl2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.9
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    fastnoise
    GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
    GIT_TAG v1.1.1
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.12
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.8
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(sdl2 glm fastnoise doctest)
FetchContent_GetProperties(glad)
if(NOT glad_POPULATED)
    FetchContent_Populate(glad)
endif()
add_subdirectory(${glad_SOURCE_DIR}/cmake ${glad_BINARY_DIR})

function(valcraft_apply_strict_warnings target)
    if(NOT VALCRAFT_STRICT_WARNINGS)
        return()
    endif()

    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,GNU>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror>
        $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror>
    )
endfunction()

function(valcraft_enable_coverage_for target)
    if(NOT VALCRAFT_ENABLE_COVERAGE)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "VALCRAFT_ENABLE_COVERAGE currently supports GNU/MinGW only")
    endif()

    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-O0 -g --coverage>
        $<$<COMPILE_LANGUAGE:CXX>:-O0 -g --coverage>
    )
    target_link_options(${target} PRIVATE --coverage)
endfunction()

function(valcraft_enable_address_sanitizer_for target)
    if(NOT VALCRAFT_ENABLE_ADDRESS_SANITIZER)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
        target_link_options(${target} PRIVATE /fsanitize=address /INCREMENTAL:NO)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address)
    else()
        message(FATAL_ERROR "AddressSanitizer is unsupported by this compiler")
    endif()
endfunction()

function(valcraft_enable_ipo_for target)
    if(NOT VALCRAFT_IPO_SUPPORTED)
        return()
    endif()

    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
endfunction()

function(valcraft_copy_runtime_dlls target)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}>
                $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS
        )
    endif()
endfunction()

function(valcraft_copy_mingw_runtime_dlls target)
    if(NOT MINGW)
        return()
    endif()

    get_filename_component(VALCRAFT_COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(runtime_dll IN ITEMS libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${VALCRAFT_COMPILER_BIN_DIR}/${runtime_dll}")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${VALCRAFT_COMPILER_BIN_DIR}/${runtime_dll}"
                    $<TARGET_FILE_DIR:${target}>
            )
        endif()
    endforeach()
endfunction()

if(WIN32)
    set(VALCRAFT_POWERSHELL_EXECUTABLE powershell)
else()
    set(VALCRAFT_POWERSHELL_EXECUTABLE pwsh)
endif()

if(TARGET glm::glm)
    set(VALCRAFT_GLM_TARGET glm::glm)
elseif(TARGET glm)
    set(VALCRAFT_GLM_TARGET glm)
else()
    message(FATAL_ERROR "GLM target not found")
endif()

if(NOT TARGET FastNoiseLite)
    add_library(FastNoiseLite INTERFACE)
    target_include_directories(FastNoiseLite SYSTEM INTERFACE ${fastnoise_SOURCE_DIR}/Cpp)
endif()

glad_add_library(valcraft_glad REPRODUCIBLE API gl:core=3.3)

add_library(valcraft_core
    src/app/Audit.cpp
    src/app/CommandConsole.cpp
    src/app/GameBranding.cpp
    src/app/GameOptions.cpp
    src/app/ProcessMemory.cpp
    src/app/SaveGame.cpp
    src/app/SmokeCamera.cpp
    src/app/TerrainEditStress.cpp
    src/app/ProgressionMenu.cpp
    src/audio/BackroomsAmbience.cpp
    src/audio/LeviathanProceduralAudio.cpp
    src/audio/ProceduralMusic.cpp
    src/audio/ProceduralSfx.cpp
    src/creatures/CrewAnimation.cpp
    src/creatures/CreatureGeometry.cpp
    src/creatures/CreatureSystem.cpp
    src/creatures/OldGuardAnimation.cpp
    src/creatures/OldGuardGeometry.cpp
    src/creatures/bosses/ChainedColossus.cpp
    src/creatures/legendary/LegendaryEnemySystem.cpp
    src/world/Chunk.cpp
    src/world/BackroomsGenerator.cpp
    src/world/BackroomsSpatialStack.cpp
    src/world/BlockVisuals.cpp
    src/world/World.cpp
    src/world/WorldGenerator.cpp
    src/world/ChunkMesher.cpp
    src/world/OrganicTerrainMesher.cpp
    src/world/Environment.cpp
    src/world/OceanSimulation.cpp
    src/world/PrecipitationField.cpp
    src/gameplay/PlayerController.cpp
    src/gameplay/BackroomsFlashlight.cpp
    src/gameplay/BackroomsJack.cpp
    src/gameplay/BackroomsMarlow.cpp
    src/gameplay/BackroomsMarlowWorld.cpp
    src/gameplay/BackroomsThreatArbiter.cpp
    src/gameplay/PlayerMusket.cpp
    src/gameplay/PlayerMusketEffects.cpp
    src/gameplay/PlayerProgression.cpp
    src/gameplay/combat/DamageZones.cpp
    src/gameplay/combat/DismembermentSystem.cpp
    src/gameplay/combat/ColossalSweep.cpp
    src/gameplay/combat/StaggerSystem.cpp
    src/gameplay/combat/WorldProtectionRegistry.cpp
    src/gameplay/encounters/SeaLeviathanEncounter.cpp
    src/gameplay/progression/AbilityCatalog.cpp
    src/gameplay/progression/AbilityEvents.cpp
    src/gameplay/progression/AbilitySystem.cpp
    src/gameplay/progression/BuilderAbilityGeometry.cpp
    src/gameplay/progression/BuilderAbilityRuntime.cpp
    src/gameplay/progression/CommanderAbilityEffects.cpp
    src/gameplay/progression/KnightAdvancedAbilitySystem.cpp
    src/gameplay/progression/NinjaAbilityEffects.cpp
    src/gameplay/progression/PlayerBuildState.cpp
    src/gameplay/progression/PlayerAbilityEffects.cpp
    src/gameplay/progression/ProgressionCurve.cpp
    src/gameplay/progression/StatusEffectSystem.cpp
    src/gameplay/progression/SummonedUnitSystem.cpp
    src/gameplay/progression/VanguardTargeting.cpp
    src/gameplay/progression/WorldEditTransaction.cpp
    src/gameplay/SeaAdventure.cpp
    src/gameplay/SeaAdventureLighting.cpp
    src/gameplay/ShipCrew.cpp
    src/gameplay/StartingPort.cpp
    src/gameplay/StartingVillage.cpp
    src/gameplay/quests/LegendaryQuestWorldContent.cpp
    src/gameplay/quests/LegendaryWeaponQuest.cpp
    src/gameplay/scenarios/IssouArenaLayout.cpp
    src/gameplay/scenarios/IssouArenaScenario.cpp
    src/gameplay/weapons/ColossalWeaponCombat.cpp
    src/gameplay/weapons/ColossalWeaponDefinition.cpp
    src/gameplay/weapons/ColossalWeaponSystem.cpp
    src/gameplay/weapons/LeviathanKnightSynergy.cpp
    src/gameplay/weapons/LegendaryWeaponProgression.cpp
    src/player/PlayerGeometry.cpp
    src/gameplay/ItemDropSystem.cpp
    src/gameplay/OldGuard.cpp
    src/render/ArchitecturalFixtureMesh.cpp
    src/render/ArchitecturalMesher.cpp
    src/render/BackroomsPropMesh.cpp
    src/render/BackroomsFlicker.cpp
    src/render/BackroomsJackScreamer.cpp
    src/render/BackroomsJackVisual.cpp
    src/render/BackroomsMarlowVisual.cpp
    src/render/BackroomsVisibility.cpp
    src/render/ItemDropGeometry.cpp
    src/render/MarineDecor.cpp
    src/render/MarineVisualMesh.cpp
    src/render/ModelIconAtlas.cpp
    src/render/MsdfFontAtlas.cpp
    src/render/OceanLifeField.cpp
    src/render/SeaHorizon.cpp
    src/render/ShadowCascades.cpp
    src/render/StylizedPrimitives.cpp
    src/render/StylizedShipMesh.cpp
    src/render/TerrainVisualSample.cpp
    src/render/VisualEntityPrimitives.cpp
    src/render/VisualItemModel.cpp
    src/render/VisualMaterials.cpp
    src/render/VisualVegetation.cpp
    src/render/VisualVegetationMesh.cpp
    src/render/creatures/ChainedColossusPresentation.cpp
    src/render/scenarios/IssouArenaPresentation.cpp
    src/render/weapons/LeviathanWeaponPresentation.cpp
)

target_include_directories(valcraft_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(valcraft_core
    PUBLIC
        ${VALCRAFT_GLM_TARGET}
        FastNoiseLite
)

if(WIN32)
    target_link_libraries(valcraft_core PUBLIC psapi)
endif()

target_compile_definitions(valcraft_core
    PUBLIC
        NOMINMAX
        VALCRAFT_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
)
valcraft_apply_strict_warnings(valcraft_core)
valcraft_enable_coverage_for(valcraft_core)
valcraft_enable_address_sanitizer_for(valcraft_core)
valcraft_enable_ipo_for(valcraft_core)

add_executable(ValCraft
    src/main.cpp
    src/app/Game.cpp
    src/app/GameMusic.cpp
    src/render/Renderer.cpp
)

target_link_libraries(ValCraft
    PRIVATE
        valcraft_core
        SDL2::SDL2
        valcraft_glad
)
target_compile_definitions(ValCraft
    PRIVATE
        VALCRAFT_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
        VALCRAFT_COVERAGE_BUILD=$<BOOL:${VALCRAFT_ENABLE_COVERAGE}>
)

if(WIN32)
    target_link_libraries(ValCraft PRIVATE opengl32)
endif()
valcraft_apply_strict_warnings(ValCraft)
valcraft_enable_coverage_for(ValCraft)
valcraft_enable_address_sanitizer_for(ValCraft)
valcraft_enable_ipo_for(ValCraft)

valcraft_copy_runtime_dlls(ValCraft)
valcraft_copy_mingw_runtime_dlls(ValCraft)
add_custom_command(TARGET ValCraft POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:ValCraft>/Images
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/Images/valcraft_icon.bmp
        $<TARGET_FILE_DIR:ValCraft>/Images/valcraft_icon.bmp
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/Images/valcraft_icon.png
        $<TARGET_FILE_DIR:ValCraft>/Images/valcraft_icon.png
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:ValCraft>/assets/visual
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/visual/valcraft_visual_materials.vmp
        $<TARGET_FILE_DIR:ValCraft>/assets/visual/valcraft_visual_materials.vmp
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/visual/valcraft_model_icons.vmia
        $<TARGET_FILE_DIR:ValCraft>/assets/visual/valcraft_model_icons.vmia
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:ValCraft>/assets/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/valcraft_ui_font.msdfa
        $<TARGET_FILE_DIR:ValCraft>/assets/fonts/valcraft_ui_font.msdfa
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL.txt
        $<TARGET_FILE_DIR:ValCraft>/assets/fonts/OFL.txt
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:ValCraft>/assets/backrooms
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/backrooms/jack_le_pirate_screamer.bmp
        $<TARGET_FILE_DIR:ValCraft>/assets/backrooms/jack_le_pirate_screamer.bmp
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/backrooms/marlow_le_noye_screamer.bmp
        $<TARGET_FILE_DIR:ValCraft>/assets/backrooms/marlow_le_noye_screamer.bmp
)

add_executable(valcraft_tests
    src/app/GameMusic.cpp
    src/render/Renderer.cpp
    tests/main.cpp
    tests/AppTests.cpp
    tests/AudioTests.cpp
    tests/BackroomsAmbienceTests.cpp
    tests/BackroomsFlickerTests.cpp
    tests/BackroomsFlashlightTests.cpp
    tests/BackroomsJackAudioTests.cpp
    tests/BackroomsJackFlickerTests.cpp
    tests/BackroomsJackIntegrationTests.cpp
    tests/BackroomsJackSaveTests.cpp
    tests/BackroomsJackScreamerTests.cpp
    tests/BackroomsJackPressureTests.cpp
    tests/BackroomsJackDirectorAuditTests.cpp
    tests/BackroomsJackTests.cpp
    tests/BackroomsJackVisualTests.cpp
    tests/BackroomsMarlowAudioTests.cpp
    tests/BackroomsMarlowVisualTests.cpp
    tests/BackroomsMarlowTests.cpp
    tests/BackroomsMarlowWorldTests.cpp
    tests/BackroomsMarlowSaveTests.cpp
    tests/BackroomsThreatArbiterTests.cpp
    tests/BackroomsConnectorBlockTests.cpp
    tests/BackroomsPropMeshTests.cpp
    tests/BackroomsSpatialStackTests.cpp
    tests/BackroomsLevelTests.cpp
    tests/BackroomsGeneratorTests.cpp
    tests/BackroomsVisibilityTests.cpp
    tests/CommandConsoleTests.cpp
    tests/ChainedColossusTests.cpp
    tests/ChainedColossusPresentationTests.cpp
    tests/ColossalCombatCoreTests.cpp
    tests/ColossalCombatIntegrationTests.cpp
    tests/ColossalWeaponTests.cpp
    tests/CrewAnimationTests.cpp
    tests/CreatureTests.cpp
    tests/OldGuardCombatTests.cpp
    tests/OldGuardSaveTests.cpp
    tests/OldGuardTests.cpp
    tests/ArchitecturalFixtureMeshTests.cpp
    tests/ArchitecturalMesherTests.cpp
    tests/PerformanceReportV3Tests.cpp
    tests/TerrainEditStressTests.cpp
    tests/OceanLifeFieldTests.cpp
    tests/RenderTests.cpp
    tests/OceanVisualShaderTests.cpp
    tests/SeaHorizonTests.cpp
    tests/MarineDecorTests.cpp
    tests/MarineVisualMeshTests.cpp
    tests/ModelIconAtlasTests.cpp
    tests/MsdfFontAtlasTests.cpp
    tests/ShadowCascadeTests.cpp
    tests/SmokeCameraTests.cpp
    tests/StylizedPrimitiveTests.cpp
    tests/StylizedShipMeshTests.cpp
    tests/TerrainVisualSampleTests.cpp
    tests/VisualEntityPrimitiveTests.cpp
    tests/VisualGameplayDifferentialTests.cpp
    tests/VisualMaterialTests.cpp
    tests/VisualRemeshStateTests.cpp
    tests/VisualVegetationTests.cpp
    tests/VisualVegetationMeshTests.cpp
    tests/WaterVertexTests.cpp
    tests/AmelieInteriorRefitTests.cpp
    tests/ShipInteriorLightingTests.cpp
    tests/ShipCrewTests.cpp
    tests/StartingPortTests.cpp
    tests/WorldSchedulingTests.cpp
    tests/WorldTests.cpp
    tests/OrganicTerrainMesherTests.cpp
    tests/VisualWorldTests.cpp
    tests/PlayerTests.cpp
    tests/PlayerMusketTests.cpp
    tests/PlayerMusketEffectsTests.cpp
    tests/AbilityEventTests.cpp
    tests/BuilderAbilityGeometryTests.cpp
    tests/BuilderAbilityRuntimeTests.cpp
    tests/CommanderAbilityEffectsTests.cpp
    tests/KnightAdvancedAbilitySystemTests.cpp
    tests/NinjaAbilityEffectsTests.cpp
    tests/PlayerAbilityEffectsTests.cpp
    tests/ProgressionAbilityTests.cpp
    tests/ProgressionCoreTests.cpp
    tests/ProgressionMenuTests.cpp
    tests/StatusEffectSystemTests.cpp
    tests/SummonedUnitSystemTests.cpp
    tests/VanguardTargetingTests.cpp
    tests/WorldEditTransactionTests.cpp
    tests/MusketCombatTests.cpp
    tests/MusketHudTests.cpp
    tests/DamageZonesTests.cpp
    tests/DismembermentSystemTests.cpp
    tests/IssouArenaScenarioTests.cpp
    tests/IssouArenaPresentationTests.cpp
    tests/LegendaryEnemySystemTests.cpp
    tests/LegendaryQuestWorldContentTests.cpp
    tests/LegendaryWeaponQuestTests.cpp
    tests/LeviathanKnightSynergyTests.cpp
    tests/LeviathanProceduralAudioTests.cpp
    tests/LeviathanWeaponPresentationTests.cpp
    tests/PrecipitationTests.cpp
    tests/ProceduralSfxTests.cpp
    tests/SeaLeviathanEncounterTests.cpp
    tests/StaggerSystemTests.cpp
)

target_link_libraries(valcraft_tests
    PRIVATE
        valcraft_core
        SDL2::SDL2
        valcraft_glad
        doctest::doctest
)
if(WIN32)
    target_link_libraries(valcraft_tests PRIVATE opengl32)
endif()
target_compile_definitions(valcraft_tests PRIVATE VALCRAFT_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
valcraft_apply_strict_warnings(valcraft_tests)
valcraft_enable_coverage_for(valcraft_tests)
valcraft_enable_address_sanitizer_for(valcraft_tests)
valcraft_copy_runtime_dlls(valcraft_tests)
valcraft_copy_mingw_runtime_dlls(valcraft_tests)

add_executable(valcraft_model_icon_atlas_tool EXCLUDE_FROM_ALL
    tools/generate_model_icon_atlas.cpp
)
target_link_libraries(valcraft_model_icon_atlas_tool PRIVATE valcraft_core)
valcraft_apply_strict_warnings(valcraft_model_icon_atlas_tool)
valcraft_enable_coverage_for(valcraft_model_icon_atlas_tool)
valcraft_enable_address_sanitizer_for(valcraft_model_icon_atlas_tool)
# Je rends l'outil disponible avant CTest, même lorsque la CI ne construit
# explicitement que le binaire de tests.
add_dependencies(valcraft_tests valcraft_model_icon_atlas_tool)

add_custom_target(valcraft_regenerate_model_icon_atlas
    COMMAND $<TARGET_FILE:valcraft_model_icon_atlas_tool>
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS valcraft_model_icon_atlas_tool
    COMMENT "Regenerating deterministic model-based item icon atlas"
    VERBATIM
)

add_custom_target(valcraft_check_model_icon_atlas
    COMMAND $<TARGET_FILE:valcraft_model_icon_atlas_tool> --check
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS valcraft_model_icon_atlas_tool
    COMMENT "Checking deterministic model-based item icon atlas"
    VERBATIM
)

add_test(
    NAME valcraft_model_icon_atlas_determinism
    COMMAND $<TARGET_FILE:valcraft_model_icon_atlas_tool> --check
)
set_tests_properties(
    valcraft_model_icon_atlas_determinism
    PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        LABELS "assets;determinism;visual"
)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(valcraft_tests)

find_program(
    VALCRAFT_POWERSHELL_TEST_EXECUTABLE
    NAMES pwsh powershell
)
if(VALCRAFT_POWERSHELL_TEST_EXECUTABLE)
    add_test(
        NAME valcraft_perf_script_self_test
        COMMAND
            ${VALCRAFT_POWERSHELL_TEST_EXECUTABLE}
            -NoProfile
            -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/perf.ps1
            -SelfTest
    )
    set_tests_properties(
        valcraft_perf_script_self_test
        PROPERTIES LABELS "performance;script;unit"
    )
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(valcraft_regenerate_visual_material_pack
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_visual_material_pack.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Regenerating deterministic stylized material pack"
        VERBATIM
    )

    add_custom_target(valcraft_check_visual_material_pack
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_visual_material_pack.py --check
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking deterministic stylized material pack"
        VERBATIM
    )

    add_custom_target(valcraft_regenerate_msdf_font_atlas
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_msdf_font_atlas.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Regenerating deterministic Montserrat UI font atlas"
        VERBATIM
    )

    add_custom_target(valcraft_check_msdf_font_atlas
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_msdf_font_atlas.py --check
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking deterministic Montserrat UI font atlas"
        VERBATIM
    )

    add_test(
        NAME valcraft_visual_capture_comparator_self_test
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/compare_visual_capture.py --self-test
    )

    add_test(
        NAME valcraft_visual_material_pack_determinism
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_visual_material_pack.py --check
    )
    set_tests_properties(
        valcraft_visual_material_pack_determinism
        PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_test(
        NAME valcraft_msdf_font_atlas_determinism
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_msdf_font_atlas.py --check
    )
    set_tests_properties(
        valcraft_msdf_font_atlas_determinism
        PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_custom_target(valcraft_regenerate_block_texture_tiles
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_block_texture_tiles.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Regenerating baked block texture tiles from Textures/*.png"
        VERBATIM
    )

    add_custom_target(valcraft_check_block_texture_tiles
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_block_texture_tiles.py --check
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking deterministic baked block texture tiles"
        VERBATIM
    )

    add_test(
        NAME valcraft_block_texture_tiles_determinism
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_block_texture_tiles.py --check
    )
    set_tests_properties(
        valcraft_block_texture_tiles_determinism
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            LABELS "assets;determinism;visual"
    )

    add_custom_target(valcraft_regenerate_window_icon
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_window_icon.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Regenerating ValCraft window icon assets"
        VERBATIM
    )
endif()

add_custom_target(valcraft_check_build
    DEPENDS
        ValCraft
        valcraft_tests
)

add_custom_target(valcraft_check_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure
    DEPENDS valcraft_tests
    USES_TERMINAL
)

# Je rejoue le perimetre Marlow dans plusieurs ordres deterministes afin de
# detecter une dependance cachee entre tests sans ralentir toute la suite.
add_custom_target(valcraft_check_marlow
    COMMAND $<TARGET_FILE:valcraft_tests>
        "--source-file=*BackroomsMarlow*,*BackroomsThreatArbiter*,*BackroomsJackIntegration*"
        --order-by=rand
        --rand-seed=15485863
    COMMAND $<TARGET_FILE:valcraft_tests>
        "--source-file=*BackroomsMarlow*,*BackroomsThreatArbiter*,*BackroomsJackIntegration*"
        --order-by=rand
        --rand-seed=32452843
    COMMAND $<TARGET_FILE:valcraft_tests>
        "--source-file=*BackroomsMarlow*,*BackroomsThreatArbiter*,*BackroomsJackIntegration*"
        --order-by=rand
        --rand-seed=49979687
    DEPENDS valcraft_tests
    USES_TERMINAL
    VERBATIM
)

add_custom_target(valcraft_check_smoke
    COMMAND $<TARGET_FILE:ValCraft> --smoke-test --smoke-frames=360 --hidden-window
    DEPENDS ValCraft
    USES_TERMINAL
)

add_custom_target(valcraft_check_marlow_smoke
    COMMAND $<TARGET_FILE:ValCraft>
        --smoke-test --smoke-session=backrooms --smoke-backrooms-level=-2
        --smoke-backrooms-marlow=peek --smoke-frames=2 --hidden-window
    COMMAND $<TARGET_FILE:ValCraft>
        --smoke-test --smoke-session=backrooms --smoke-backrooms-level=-2
        --smoke-backrooms-marlow=chase --smoke-frames=2 --hidden-window
    COMMAND $<TARGET_FILE:ValCraft>
        --smoke-test --smoke-session=backrooms --smoke-backrooms-level=-2
        --smoke-backrooms-marlow=capture-blocked --smoke-frames=2 --hidden-window
    COMMAND $<TARGET_FILE:ValCraft>
        --smoke-test --smoke-session=backrooms --smoke-backrooms-level=-2
        --smoke-backrooms-marlow=screamer --smoke-frames=2 --hidden-window
    DEPENDS ValCraft
    USES_TERMINAL
    VERBATIM
)

add_custom_target(valcraft_check_modern_visual_smoke
    COMMAND $<TARGET_FILE:ValCraft>
        --smoke-test
        --smoke-frames=240
        --hidden-window
        --visual-pipeline=modern
        --fixed-render-quality
        --window-width=1920
        --window-height=1080
        --capture-frame=${CMAKE_BINARY_DIR}/artifacts/modern/continental-day.bmp
    DEPENDS ValCraft
    USES_TERMINAL
)

add_custom_target(valcraft_check_sea_loading_smoke
    COMMAND $<TARGET_FILE:ValCraft>
        --smoke-test
        --smoke-session=sea-new
        --smoke-frames=240
        --hidden-window
        --visual-pipeline=modern
    COMMAND $<TARGET_FILE:ValCraft>
        --smoke-test
        --smoke-session=sea-legacy
        --smoke-frames=240
        --hidden-window
        --visual-pipeline=modern
    DEPENDS ValCraft
    USES_TERMINAL
)

# Je permets aux runners sans display ni pilote OpenGL de desactiver ces tests;
# la cible valcraft_check_sea_loading_smoke reste toujours disponible sur un
# poste equipe pour valider le vrai chemin SDL/GPU.
if(VALCRAFT_ENABLE_GPU_INTEGRATION_TESTS)
    function(valcraft_add_marlow_gpu_smoke test_name smoke_mode)
        add_test(
            NAME ${test_name}
            COMMAND $<TARGET_FILE:ValCraft>
                --smoke-test
                --smoke-session=backrooms
                --smoke-backrooms-level=-2
                --smoke-backrooms-marlow=${smoke_mode}
                --smoke-frames=2
                --hidden-window
        )
        set_tests_properties(
            ${test_name}
            PROPERTIES
                LABELS "gpu;integration;backrooms;marlow"
                TIMEOUT 90
                RESOURCE_LOCK valcraft_gpu
        )
    endfunction()

    valcraft_add_marlow_gpu_smoke(valcraft_marlow_peek_smoke peek)
    valcraft_add_marlow_gpu_smoke(valcraft_marlow_chase_smoke chase)
    valcraft_add_marlow_gpu_smoke(
        valcraft_marlow_capture_blocked_smoke
        capture-blocked)
    valcraft_add_marlow_gpu_smoke(valcraft_marlow_screamer_smoke screamer)

    add_test(
        NAME valcraft_modern_visual_smoke
        COMMAND $<TARGET_FILE:ValCraft>
            --smoke-test
            --smoke-frames=240
            --hidden-window
            --visual-pipeline=modern
            --fixed-render-quality
            --window-width=1920
            --window-height=1080
            --capture-frame=${CMAKE_BINARY_DIR}/artifacts/modern/continental-day.bmp
    )
    set_tests_properties(
        valcraft_modern_visual_smoke
        PROPERTIES
            LABELS "gpu;integration;modern;capture"
            TIMEOUT 180
            RESOURCE_LOCK valcraft_gpu
    )

    add_test(
        NAME valcraft_sea_new_loading_smoke
        COMMAND $<TARGET_FILE:ValCraft>
            --smoke-test
            --smoke-session=sea-new
            --smoke-frames=240
            --hidden-window
            --visual-pipeline=modern
    )
    add_test(
        NAME valcraft_sea_legacy_loading_smoke
        COMMAND $<TARGET_FILE:ValCraft>
            --smoke-test
            --smoke-session=sea-legacy
            --smoke-frames=240
            --hidden-window
            --visual-pipeline=modern
    )
    set_tests_properties(
        valcraft_sea_new_loading_smoke
        valcraft_sea_legacy_loading_smoke
        PROPERTIES
            LABELS "gpu;integration;loading"
            TIMEOUT 60
            RESOURCE_LOCK valcraft_gpu
    )

    function(valcraft_add_ocean_gpu_smoke
             test_name
             ship_view
             time_of_day
             capture_name)
        add_test(
            NAME ${test_name}
            COMMAND $<TARGET_FILE:ValCraft>
                --smoke-test
                --smoke-session=sea-open
                --smoke-ship-view=${ship_view}
                --visual-pipeline=modern
                --initial-time=${time_of_day}
                --initial-weather-time=0
                --smoke-frames=240
                --window-width=1280
                --window-height=720
                --hidden-window
                --fixed-render-quality
                --capture-frame=${CMAKE_BINARY_DIR}/artifacts/ocean/${capture_name}.bmp
        )
        set_tests_properties(
            ${test_name}
            PROPERTIES
                LABELS "gpu;integration;ocean;capture"
                TIMEOUT 90
                RESOURCE_LOCK valcraft_gpu
        )
    endfunction()

    # Je verrouille les trois vues qui couvrent la continuité de l'horizon,
    # le sillage au soleil rasant et le volume sous-marin moderne.
    valcraft_add_ocean_gpu_smoke(
        valcraft_ocean_clear_horizon_smoke
        stern
        10.5
        clear-horizon)
    valcraft_add_ocean_gpu_smoke(
        valcraft_ocean_sunset_wake_smoke
        wake
        18.5
        sunset-wake)
    valcraft_add_ocean_gpu_smoke(
        valcraft_ocean_underwater_smoke
        underwater
        10.5
        underwater)

    function(valcraft_add_tempest_gpu_smoke test_name ship_view weather_time capture_name)
        add_test(
            NAME ${test_name}
            COMMAND $<TARGET_FILE:ValCraft>
                --smoke-test
                --smoke-session=sea-new
                --smoke-ship-view=${ship_view}
                --visual-pipeline=modern
                --initial-weather-time=${weather_time}
                --smoke-frames=240
                --window-width=1280
                --window-height=720
                --hidden-window
                --fixed-render-quality
                --capture-frame=${CMAKE_BINARY_DIR}/artifacts/tempest/${capture_name}.bmp
                ${ARGN}
        )
        set_tests_properties(
            ${test_name}
            PROPERTIES
                LABELS "gpu;integration;weather;tempest;capture"
                TIMEOUT 90
                RESOURCE_LOCK valcraft_gpu
        )
    endfunction()

    # Je couvre les espaces critiques au pic d'éclair, puis les deux flancs
    # entre deux éclairs afin de distinguer la pluie du flash atmosphérique.
    valcraft_add_tempest_gpu_smoke(
        valcraft_tempest_deck_smoke
        deck
        2685.0979
        deck)
    valcraft_add_tempest_gpu_smoke(
        valcraft_tempest_bow_smoke
        bow
        2685.0979
        bow)
    valcraft_add_tempest_gpu_smoke(
        valcraft_tempest_stern_smoke
        stern
        2685.0979
        stern)
    valcraft_add_tempest_gpu_smoke(
        valcraft_tempest_interior_smoke
        interior
        2685.0979
        interior)
    valcraft_add_tempest_gpu_smoke(
        valcraft_tempest_cabin_smoke
        cabin
        2685.0979
        cabin)
    valcraft_add_tempest_gpu_smoke(
        valcraft_tempest_cargo_smoke
        cargo
        2685.0979
        cargo)
    valcraft_add_tempest_gpu_smoke(
        valcraft_tempest_crew_smoke
        crew
        2685.0979
        crew)
    valcraft_add_tempest_gpu_smoke(
        valcraft_tempest_port_smoke
        port
        2760
        port-between-strikes)
    valcraft_add_tempest_gpu_smoke(
        valcraft_tempest_starboard_smoke
        starboard
        2760
        starboard-between-strikes)
    valcraft_add_tempest_gpu_smoke(
        valcraft_tempest_deck_no_post_process_smoke
        deck
        2685.0979
        deck-no-post-process
        --disable-post-process)
endif()

add_custom_target(valcraft_check_perf_smoke
    COMMAND ${VALCRAFT_POWERSHELL_EXECUTABLE} -ExecutionPolicy Bypass -File
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/perf.ps1
        -BuildDir ${CMAKE_BINARY_DIR}
        -Configuration $<CONFIG>
        -NoConfigure
        -NoBuild
        -SmokeFrames 180
        -WarmupFrames 60
        -Repetitions 1
        -Scenarios baseline
        -EnforceThresholds
    DEPENDS ValCraft
    USES_TERMINAL
)

# Je garde la Tempest dans une cible dédiée afin de mesurer durablement son
# budget à 60 FPS sans rendre incompatibles les baselines de la suite générale.
add_custom_target(valcraft_check_tempest_perf_smoke
    COMMAND ${VALCRAFT_POWERSHELL_EXECUTABLE} -ExecutionPolicy Bypass -File
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/perf.ps1
        -BuildDir ${CMAKE_BINARY_DIR}
        -Configuration $<CONFIG>
        -NoConfigure
        -NoBuild
        -SmokeFrames 180
        -WarmupFrames 60
        -Repetitions 1
        -Scenarios sea_tempest
        -EnforceThresholds
    DEPENDS ValCraft
    USES_TERMINAL
)

add_custom_target(valcraft_check_audit_smoke
    COMMAND ${VALCRAFT_POWERSHELL_EXECUTABLE} -ExecutionPolicy Bypass -File
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit.ps1
        -BuildDir ${CMAKE_BINARY_DIR}
        -NoConfigure
        -NoBuild
        -Mode measure
        -Label smoke
        -SmokeFrames 180
    DEPENDS ValCraft
    USES_TERMINAL
)

add_custom_target(valcraft_perf_suite
    COMMAND ${VALCRAFT_POWERSHELL_EXECUTABLE} -ExecutionPolicy Bypass -File
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/perf.ps1
        -BuildDir ${CMAKE_BINARY_DIR}
        -Configuration $<CONFIG>
        -NoConfigure
        -NoBuild
        -EnforceThresholds
    DEPENDS ValCraft
    USES_TERMINAL
)

if(VALCRAFT_ENABLE_COVERAGE)
    add_custom_target(valcraft_check_coverage
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure
        DEPENDS valcraft_tests
        USES_TERMINAL
    )
else()
    add_custom_target(valcraft_check_coverage
        COMMAND ${CMAKE_COMMAND} -E echo "Coverage disabled. Reconfigure with -DVALCRAFT_ENABLE_COVERAGE=ON to run coverage checks."
        USES_TERMINAL
    )
endif()

add_custom_target(valcraft_check_all
    DEPENDS
        valcraft_check_build
        valcraft_check_tests
        valcraft_check_smoke
)

if(VALCRAFT_ENABLE_COVERAGE)
    add_dependencies(valcraft_check_all valcraft_check_coverage)
endif()

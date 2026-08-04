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
include(CheckCXXSourceCompiles)
enable_testing()
option(VALCRAFT_STRICT_WARNINGS "Enable strict warnings on project targets" ON)
option(VALCRAFT_ENABLE_COVERAGE "Enable gcov coverage for critical modules" OFF)
option(VALCRAFT_ENABLE_ADDRESS_SANITIZER "Instrument ValCraft targets with AddressSanitizer" OFF)
option(VALCRAFT_ENABLE_UNDEFINED_SANITIZER "Instrument ValCraft targets with UndefinedBehaviorSanitizer" OFF)
option(VALCRAFT_ENABLE_GPU_INTEGRATION_TESTS "Register SDL/OpenGL smoke tests in the default CTest suite" ON)
option(VALCRAFT_ALLOW_DEPENDENCY_DOWNLOADS "Allow CMake to download dependencies which are not available locally" ON)
option(VALCRAFT_HEADLESS_BACKROOMS_TESTS_ONLY "Configure only the Backrooms logic tests without SDL or OpenGL" OFF)
set(
    VALCRAFT_DEPENDENCY_SOURCE_ROOT
    ""
    CACHE PATH
    "Optional directory containing pre-populated sdl2, glm, fastnoise, doctest and glad source trees"
)
set(
    VALCRAFT_GLAD_GENERATED_SOURCE_DIR
    ""
    CACHE PATH
    "Optional directory containing verified generated glad2 include/glad/gl.h and src/gl.c files"
)

set(VALCRAFT_GLM_GIT_REVISION "8d1fd52e5ab5590e2c81768ace50c72bae28f2ed")
set(VALCRAFT_FASTNOISE_GIT_REVISION "7ccfbc16eb1c932568f177d63a9ba51d89bbe516")
set(VALCRAFT_DOCTEST_GIT_REVISION "1da23a3e8119ec5cce4f9388e91b065e20bf06f5")
set(VALCRAFT_SDL2_GIT_REVISION "c98c4fbff6d8f3016a3ce6685bf8f43433c3efcc")
set(VALCRAFT_GLAD_GIT_REVISION "73db193f853e2ee079bf3ca8a64aa2eaf6459043")
set(VALCRAFT_GLAD_JINJA2_VERSION "3.1.6")
set(VALCRAFT_GLAD_MARKUPSAFE_VERSION "3.0.3")
set(VALCRAFT_GLAD_HEADER_SHA256
    "38555534130ab0f6bdf7815fbbae52241f4f7aeb6165949fccd284348a1daa1a"
)
set(VALCRAFT_GLAD_SOURCE_SHA256
    "60ba6709b27cde10e2b617a4a4ca6793ffb733fab18623a0e7b7513167a44fc9"
)
set(VALCRAFT_GLAD_KHRPLATFORM_SHA256
    "5ee5d5c2c6b2abd028cec8dc1a24e995bb47b075654a6fc55f3a1293de36943f"
)

if(VALCRAFT_ALLOW_DEPENDENCY_DOWNLOADS)
    # Je remets aussi explicitement le cache en ligne lors d'une reconfiguration
    # afin qu'un ancien build hors ligne ne reste pas bloque silencieusement.
    set(FETCHCONTENT_FULLY_DISCONNECTED OFF CACHE BOOL "Disable FetchContent network access" FORCE)
else()
    # Je rends le contrat hors ligne explicite : FetchContent reutilise un
    # cache deja peuple ou un repertoire fourni, mais ne contacte jamais Git.
    set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "Disable FetchContent network access" FORCE)
endif()

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

function(valcraft_clear_legacy_managed_dependency_source dependency_name)
    string(TOUPPER "${dependency_name}" dependency_name_upper)
    set(source_override_variable "FETCHCONTENT_SOURCE_DIR_${dependency_name_upper}")
    set(managed_override_variable
        "VALCRAFT_MANAGED_FETCHCONTENT_SOURCE_DIR_${dependency_name_upper}"
    )

    get_property(
        managed_override_is_cached
        CACHE "${managed_override_variable}"
        PROPERTY TYPE
        SET
    )
    if(managed_override_is_cached)
        get_property(
            managed_override
            CACHE "${managed_override_variable}"
            PROPERTY VALUE
        )
        if(VALCRAFT_DEPENDENCY_SOURCE_ROOT)
            set(current_managed_override
                "${VALCRAFT_DEPENDENCY_SOURCE_ROOT}/${dependency_name}"
            )
            cmake_path(
                ABSOLUTE_PATH current_managed_override
                BASE_DIRECTORY "${PROJECT_SOURCE_DIR}"
                NORMALIZE
            )
        else()
            set(current_managed_override "")
        endif()

        if(NOT managed_override STREQUAL current_managed_override)
            get_property(
                source_override_is_cached
                CACHE "${source_override_variable}"
                PROPERTY TYPE
                SET
            )
            if(source_override_is_cached)
                get_property(
                    source_override
                    CACHE "${source_override_variable}"
                    PROPERTY VALUE
                )
                if(source_override STREQUAL managed_override)
                    # Je retire uniquement la valeur que ValCraft avait placee.
                    # Un override remplace depuis par l'utilisateur est preserve.
                    unset(${source_override_variable} CACHE)
                endif()
            endif()
            unset(${managed_override_variable} CACHE)
            message(STATUS
                "Cleared stale ValCraft source override for ${dependency_name}"
            )
        endif()
    endif()

    get_property(
        source_override_is_cached
        CACHE "${source_override_variable}"
        PROPERTY TYPE
        SET
    )
    if(NOT source_override_is_cached)
        return()
    endif()

    get_property(
        source_override_help
        CACHE "${source_override_variable}"
        PROPERTY HELPSTRING
    )
    if(source_override_help STREQUAL "Local source override for ${dependency_name}")
        # Je ne supprime que les anciennes entrees creees par ValCraft. Une
        # valeur -DFETCHCONTENT_SOURCE_DIR_* fournie directement reste intacte.
        unset(${source_override_variable} CACHE)
        message(STATUS "Cleared legacy ValCraft source override for ${dependency_name}")
    endif()
endfunction()

foreach(dependency_name IN ITEMS glm fastnoise doctest sdl2 glad)
    valcraft_clear_legacy_managed_dependency_source(${dependency_name})
endforeach()

function(valcraft_use_cached_dependency_source dependency_name)
    if(NOT VALCRAFT_DEPENDENCY_SOURCE_ROOT)
        return()
    endif()

    string(TOUPPER "${dependency_name}" dependency_name_upper)
    set(dependency_source "${VALCRAFT_DEPENDENCY_SOURCE_ROOT}/${dependency_name}")
    cmake_path(
        ABSOLUTE_PATH dependency_source
        BASE_DIRECTORY "${PROJECT_SOURCE_DIR}"
        NORMALIZE
    )
    if(NOT IS_DIRECTORY "${dependency_source}")
        message(FATAL_ERROR
            "Missing cached dependency '${dependency_name}' in "
            "VALCRAFT_DEPENDENCY_SOURCE_ROOT=${VALCRAFT_DEPENDENCY_SOURCE_ROOT}"
        )
    endif()
    # Je garde l'override dans la portee de cette configuration seulement. Il
    # ne peut donc pas survivre au changement ou a la suppression de la racine.
    set(
        FETCHCONTENT_SOURCE_DIR_${dependency_name_upper}
        "${dependency_source}"
        PARENT_SCOPE
    )
    set(
        VALCRAFT_MANAGED_FETCHCONTENT_SOURCE_DIR_${dependency_name_upper}
        "${dependency_source}"
        CACHE INTERNAL
        "FetchContent source override managed by ValCraft"
        FORCE
    )
endfunction()

function(valcraft_require_offline_source dependency_name)
    string(TOUPPER "${dependency_name}" dependency_name_upper)
    set(source_override_variable "FETCHCONTENT_SOURCE_DIR_${dependency_name_upper}")
    if(VALCRAFT_ALLOW_DEPENDENCY_DOWNLOADS OR
       VALCRAFT_DEPENDENCY_SOURCE_ROOT OR
       IS_DIRECTORY "${${source_override_variable}}" OR
       IS_DIRECTORY "${FETCHCONTENT_BASE_DIR}/${dependency_name}-src")
        return()
    endif()
    message(FATAL_ERROR
        "Dependency '${dependency_name}' was not found locally. Install its "
        "CMake package, provide VALCRAFT_DEPENDENCY_SOURCE_ROOT, or enable "
        "VALCRAFT_ALLOW_DEPENDENCY_DOWNLOADS."
    )
endfunction()

function(valcraft_verify_dependency_revision dependency_name source_dir expected_revision)
    if(NOT EXISTS "${source_dir}/.git")
        message(FATAL_ERROR
            "Fallback dependency '${dependency_name}' must be a Git checkout so "
            "ValCraft can verify revision ${expected_revision}. Received: ${source_dir}"
        )
    endif()

    find_package(Git QUIET)
    if(NOT Git_FOUND OR NOT GIT_EXECUTABLE)
        message(FATAL_ERROR
            "Git is required to verify fallback dependency '${dependency_name}' "
            "at revision ${expected_revision}."
        )
    endif()

    file(REAL_PATH "${source_dir}" source_dir_real)
    file(TO_CMAKE_PATH "${source_dir}" source_dir_for_git)
    file(TO_CMAKE_PATH "${source_dir_real}" source_dir_real_for_git)
    execute_process(
        COMMAND
            "${GIT_EXECUTABLE}"
            -c "safe.directory=${source_dir_for_git}"
            -c "safe.directory=${source_dir_real_for_git}"
            -C "${source_dir_real_for_git}"
            rev-parse --verify HEAD
        RESULT_VARIABLE revision_result
        OUTPUT_VARIABLE actual_revision
        ERROR_VARIABLE revision_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    string(TOLOWER "${actual_revision}" actual_revision)
    string(TOLOWER "${expected_revision}" expected_revision_lower)
    if(NOT revision_result EQUAL 0 OR
       NOT actual_revision STREQUAL expected_revision_lower)
        message(FATAL_ERROR
            "Fallback dependency '${dependency_name}' has revision "
            "'${actual_revision}', expected '${expected_revision_lower}'. "
            "Refresh the dependency cache before configuring. Git said: ${revision_error}"
        )
    endif()

    execute_process(
        COMMAND
            "${GIT_EXECUTABLE}"
            -c "safe.directory=${source_dir_for_git}"
            -c "safe.directory=${source_dir_real_for_git}"
            -C "${source_dir_real_for_git}"
            status --porcelain=v1 --untracked-files=all
        RESULT_VARIABLE status_result
        OUTPUT_VARIABLE tracked_changes
        ERROR_VARIABLE status_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT status_result EQUAL 0)
        message(FATAL_ERROR
            "Unable to inspect fallback dependency '${dependency_name}': ${status_error}"
        )
    endif()
    if(tracked_changes)
        message(FATAL_ERROR
            "Fallback dependency '${dependency_name}' contains local changes "
            "at ${source_dir}. Restore a clean checkout of revision "
            "${expected_revision_lower} before configuring. Changes: "
            "${tracked_changes}"
        )
    endif()

    message(STATUS
        "Verified fallback dependency ${dependency_name} at ${expected_revision_lower}"
    )
endfunction()

macro(valcraft_populate_verified_dependency dependency_name expected_revision)
    FetchContent_GetProperties(${dependency_name})
    if(NOT ${dependency_name}_POPULATED)
        FetchContent_Populate(${dependency_name})
    endif()

    set(_valcraft_dependency_source_variable "${dependency_name}_SOURCE_DIR")
    set(_valcraft_dependency_source
        "${${_valcraft_dependency_source_variable}}"
    )

    # Je valide chaque source avant de l'exposer au reste de la configuration,
    # y compris les dependances header-only sans projet CMake comme FastNoiseLite.
    valcraft_verify_dependency_revision(
        "${dependency_name}"
        "${_valcraft_dependency_source}"
        "${expected_revision}"
    )

    unset(_valcraft_dependency_source_variable)
    unset(_valcraft_dependency_source)
endmacro()

macro(valcraft_make_verified_dependency_available dependency_name expected_revision)
    valcraft_populate_verified_dependency(
        "${dependency_name}"
        "${expected_revision}"
    )

    set(_valcraft_dependency_source_variable "${dependency_name}_SOURCE_DIR")
    set(_valcraft_dependency_binary_variable "${dependency_name}_BINARY_DIR")
    set(_valcraft_dependency_source
        "${${_valcraft_dependency_source_variable}}"
    )
    set(_valcraft_dependency_binary
        "${${_valcraft_dependency_binary_variable}}"
    )

    # Je n'execute le CMakeLists.txt d'une dependance qu'apres le controle Git.
    add_subdirectory(
        "${_valcraft_dependency_source}"
        "${_valcraft_dependency_binary}"
    )

    unset(_valcraft_dependency_source_variable)
    unset(_valcraft_dependency_binary_variable)
    unset(_valcraft_dependency_source)
    unset(_valcraft_dependency_binary)
endmacro()

function(valcraft_check_system_glad_target candidate_target result_variable)
    set(CMAKE_REQUIRED_QUIET TRUE)
    set(CMAKE_REQUIRED_LIBRARIES "${candidate_target}")
    unset(VALCRAFT_SYSTEM_GLAD_TARGET_COMPILES CACHE)
    check_cxx_source_compiles(
        [=[
            #include <glad/gl.h>

            int main() {
                GLADloadfunc loader = nullptr;
                return gladLoadGL(loader) + GLAD_GL_VERSION_3_3;
            }
        ]=]
        VALCRAFT_SYSTEM_GLAD_TARGET_COMPILES
    )
    set(system_glad_target_compiles "${VALCRAFT_SYSTEM_GLAD_TARGET_COMPILES}")
    unset(VALCRAFT_SYSTEM_GLAD_TARGET_COMPILES CACHE)
    set(${result_variable} "${system_glad_target_compiles}" PARENT_SCOPE)
endfunction()

function(valcraft_generated_glad_sources_are_verified generated_root result_variable)
    set(generated_header "${generated_root}/include/glad/gl.h")
    set(generated_khrplatform "${generated_root}/include/KHR/khrplatform.h")
    set(generated_source "${generated_root}/src/gl.c")
    if(NOT EXISTS "${generated_header}" OR
       NOT EXISTS "${generated_khrplatform}" OR
       NOT EXISTS "${generated_source}")
        set(${result_variable} FALSE PARENT_SCOPE)
        return()
    endif()

    file(SHA256 "${generated_header}" generated_header_sha256)
    file(SHA256 "${generated_khrplatform}" generated_khrplatform_sha256)
    file(SHA256 "${generated_source}" generated_source_sha256)
    if("${generated_header_sha256}" STREQUAL "${VALCRAFT_GLAD_HEADER_SHA256}" AND
       "${generated_khrplatform_sha256}" STREQUAL "${VALCRAFT_GLAD_KHRPLATFORM_SHA256}" AND
       "${generated_source_sha256}" STREQUAL "${VALCRAFT_GLAD_SOURCE_SHA256}")
        set(${result_variable} TRUE PARENT_SCOPE)
    else()
        message(STATUS
            "Existing generated glad2 sources do not match the pinned hashes; "
            "they will be regenerated with the locked Python runtime."
        )
        set(${result_variable} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(valcraft_require_glad_generator_runtime)
    find_package(Python 3.8 COMPONENTS Interpreter QUIET)
    if(NOT Python_Interpreter_FOUND)
        message(FATAL_ERROR
            "The generated glad fallback requires Python 3.8 or newer. Install "
            "tools/requirements-build.txt or provide a compatible glad2 package."
        )
    endif()

    foreach(python_distribution IN ITEMS Jinja2 MarkupSafe)
        if(python_distribution STREQUAL "Jinja2")
            set(expected_version "${VALCRAFT_GLAD_JINJA2_VERSION}")
        else()
            set(expected_version "${VALCRAFT_GLAD_MARKUPSAFE_VERSION}")
        endif()
        execute_process(
            COMMAND
                "${Python_EXECUTABLE}"
                -c
                "print(__import__('importlib.metadata', fromlist=['version']).version('${python_distribution}'))"
            RESULT_VARIABLE package_probe_result
            OUTPUT_VARIABLE installed_version
            ERROR_VARIABLE package_probe_error
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_STRIP_TRAILING_WHITESPACE
        )
        if(NOT package_probe_result EQUAL 0 OR
           NOT installed_version VERSION_EQUAL expected_version)
            message(FATAL_ERROR
                "The generated glad fallback requires ${python_distribution}==${expected_version} "
                "for deterministic output, but Python '${Python_EXECUTABLE}' reports "
                "'${installed_version}'. Run '${Python_EXECUTABLE} -m pip install "
                "--requirement ${PROJECT_SOURCE_DIR}/tools/requirements-build.txt'. "
                "Probe details: ${package_probe_error}"
            )
        endif()
    endforeach()
endfunction()

# Je privilegie toujours les paquets fournis par le runner. Les commits de
# repli sont immuables afin qu'un tag amont ne puisse jamais modifier le build.
find_package(glm 1.0 CONFIG QUIET)
if(NOT TARGET glm::glm AND NOT TARGET glm)
    valcraft_use_cached_dependency_source(glm)
    valcraft_require_offline_source(glm)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG ${VALCRAFT_GLM_GIT_REVISION}
    )
    valcraft_make_verified_dependency_available(
        glm
        "${VALCRAFT_GLM_GIT_REVISION}"
    )
endif()

if(NOT VALCRAFT_HEADLESS_BACKROOMS_TESTS_ONLY)
    find_path(VALCRAFT_FASTNOISE_INCLUDE_DIR NAMES FastNoiseLite.h PATH_SUFFIXES Cpp)
    if(NOT VALCRAFT_FASTNOISE_INCLUDE_DIR)
        valcraft_use_cached_dependency_source(fastnoise)
        valcraft_require_offline_source(fastnoise)
        FetchContent_Declare(
            fastnoise
            GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
            GIT_TAG ${VALCRAFT_FASTNOISE_GIT_REVISION}
        )
        valcraft_populate_verified_dependency(
            fastnoise
            "${VALCRAFT_FASTNOISE_GIT_REVISION}"
        )
    endif()
endif()

find_package(doctest 2.4 CONFIG QUIET)
if(NOT TARGET doctest::doctest)
    valcraft_use_cached_dependency_source(doctest)
    valcraft_require_offline_source(doctest)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG ${VALCRAFT_DOCTEST_GIT_REVISION}
    )
    valcraft_make_verified_dependency_available(
        doctest
        "${VALCRAFT_DOCTEST_GIT_REVISION}"
    )
endif()

if(NOT COMMAND doctest_discover_tests)
    include(doctest OPTIONAL)
endif()
if(NOT COMMAND doctest_discover_tests AND doctest_SOURCE_DIR)
    include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
endif()

if(NOT VALCRAFT_HEADLESS_BACKROOMS_TESTS_ONLY)
    find_package(SDL2 2.30 CONFIG QUIET)
    if(NOT TARGET SDL2::SDL2)
        valcraft_use_cached_dependency_source(sdl2)
        valcraft_require_offline_source(sdl2)
        FetchContent_Declare(
            sdl2
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG ${VALCRAFT_SDL2_GIT_REVISION}
        )
        valcraft_make_verified_dependency_available(
            sdl2
            "${VALCRAFT_SDL2_GIT_REVISION}"
        )
    endif()

    set(VALCRAFT_SYSTEM_GLAD_TARGET "")
    set(VALCRAFT_VERIFIED_GLAD_SOURCE_DIR "")
    if(VALCRAFT_GLAD_GENERATED_SOURCE_DIR)
        set(verified_glad_source_dir "${VALCRAFT_GLAD_GENERATED_SOURCE_DIR}")
        cmake_path(
            ABSOLUTE_PATH verified_glad_source_dir
            BASE_DIRECTORY "${PROJECT_SOURCE_DIR}"
            NORMALIZE
        )
        valcraft_generated_glad_sources_are_verified(
            "${verified_glad_source_dir}"
            provided_glad_is_verified
        )
        if(NOT provided_glad_is_verified)
            message(FATAL_ERROR
                "VALCRAFT_GLAD_GENERATED_SOURCE_DIR must contain the pinned "
                "glad2 OpenGL 3.3 files include/glad/gl.h and src/gl.c. "
                "Expected SHA-256: gl.h=${VALCRAFT_GLAD_HEADER_SHA256}, "
                "khrplatform.h=${VALCRAFT_GLAD_KHRPLATFORM_SHA256}, "
                "gl.c=${VALCRAFT_GLAD_SOURCE_SHA256}. Received: "
                "${verified_glad_source_dir}"
            )
        endif()
        set(VALCRAFT_VERIFIED_GLAD_SOURCE_DIR "${verified_glad_source_dir}")
        message(STATUS
            "Using verified pre-generated glad2 sources from "
            "${VALCRAFT_VERIFIED_GLAD_SOURCE_DIR}"
        )
    else()
        find_package(glad CONFIG QUIET)
        if(TARGET glad::glad)
            set(system_glad_candidate glad::glad)
        elseif(TARGET glad)
            set(system_glad_candidate glad)
        else()
            set(system_glad_candidate "")
        endif()
        if(system_glad_candidate)
            valcraft_check_system_glad_target(
                "${system_glad_candidate}"
                system_glad_is_compatible
            )
            if(system_glad_is_compatible)
                set(VALCRAFT_SYSTEM_GLAD_TARGET "${system_glad_candidate}")
                message(STATUS
                    "Using compatible system glad2 target '${VALCRAFT_SYSTEM_GLAD_TARGET}'"
                )
            else()
                message(WARNING
                    "System target '${system_glad_candidate}' does not provide the "
                    "required glad2 <glad/gl.h>, gladLoadGL and OpenGL 3.3 symbols. "
                    "ValCraft will use its pinned generated fallback instead; see "
                    "CMakeFiles/CMakeError.log for the compatibility probe."
                )
            endif()
        endif()

        if(NOT VALCRAFT_SYSTEM_GLAD_TARGET)
            valcraft_use_cached_dependency_source(glad)
            valcraft_require_offline_source(glad)
            FetchContent_Declare(
                glad
                GIT_REPOSITORY https://github.com/Dav1dde/glad.git
                GIT_TAG ${VALCRAFT_GLAD_GIT_REVISION}
            )
            FetchContent_GetProperties(glad)
            if(NOT glad_POPULATED)
                FetchContent_Populate(glad)
            endif()
            valcraft_verify_dependency_revision(
                glad
                "${glad_SOURCE_DIR}"
                "${VALCRAFT_GLAD_GIT_REVISION}"
            )
            add_subdirectory(${glad_SOURCE_DIR}/cmake ${glad_BINARY_DIR})
        endif()
    endif()
endif()

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

function(valcraft_enable_undefined_sanitizer_for target)
    if(NOT VALCRAFT_ENABLE_UNDEFINED_SANITIZER)
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Je garde float-cast-overflow explicite : ce controle couvre les
        # conversions de coordonnees qui avaient echappe a l'UBSan generique.
        target_compile_options(${target} PRIVATE
            -fsanitize=undefined,float-cast-overflow
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE
            -fsanitize=undefined,float-cast-overflow
        )
    else()
        message(FATAL_ERROR
            "VALCRAFT_ENABLE_UNDEFINED_SANITIZER requires GCC or Clang"
        )
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

if(VALCRAFT_HEADLESS_BACKROOMS_TESTS_ONLY)
    add_library(valcraft_backrooms_core
        src/audio/BackroomsAmbience.cpp
        src/gameplay/BackroomsFlashlight.cpp
        src/gameplay/BackroomsThreatArbiter.cpp
        src/world/BackroomsGenerator.cpp
        src/world/BackroomsSpatialStack.cpp
        src/world/Environment.cpp
    )
    target_include_directories(valcraft_backrooms_core
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(valcraft_backrooms_core PUBLIC ${VALCRAFT_GLM_TARGET})
    valcraft_apply_strict_warnings(valcraft_backrooms_core)
    valcraft_enable_coverage_for(valcraft_backrooms_core)
    valcraft_enable_address_sanitizer_for(valcraft_backrooms_core)
    valcraft_enable_undefined_sanitizer_for(valcraft_backrooms_core)

    add_executable(backrooms_core_tests
        tests/main.cpp
        tests/BackroomsCoreTests.cpp
        tests/BackroomsThreatArbiterTests.cpp
    )
    target_link_libraries(backrooms_core_tests
        PRIVATE valcraft_backrooms_core doctest::doctest
    )
    valcraft_apply_strict_warnings(backrooms_core_tests)
    valcraft_enable_coverage_for(backrooms_core_tests)
    valcraft_enable_address_sanitizer_for(backrooms_core_tests)
    valcraft_enable_undefined_sanitizer_for(backrooms_core_tests)

    if(COMMAND doctest_discover_tests)
        doctest_discover_tests(backrooms_core_tests TEST_PREFIX "backrooms_core::")
    else()
        add_test(NAME backrooms_core_tests COMMAND backrooms_core_tests)
    endif()

    add_custom_target(valcraft_check_backrooms_core
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure
        DEPENDS backrooms_core_tests
        USES_TERMINAL
    )
    return()
endif()

if(NOT TARGET FastNoiseLite)
    add_library(FastNoiseLite INTERFACE)
    if(VALCRAFT_FASTNOISE_INCLUDE_DIR)
        target_include_directories(FastNoiseLite SYSTEM INTERFACE ${VALCRAFT_FASTNOISE_INCLUDE_DIR})
    else()
        target_include_directories(FastNoiseLite SYSTEM INTERFACE ${fastnoise_SOURCE_DIR}/Cpp)
    endif()
endif()

if(VALCRAFT_SYSTEM_GLAD_TARGET)
    add_library(valcraft_glad INTERFACE)
    target_link_libraries(valcraft_glad INTERFACE ${VALCRAFT_SYSTEM_GLAD_TARGET})
elseif(VALCRAFT_VERIFIED_GLAD_SOURCE_DIR)
    set(VALCRAFT_GENERATED_GLAD_ROOT "${VALCRAFT_VERIFIED_GLAD_SOURCE_DIR}")
    add_library(valcraft_glad STATIC
        "${VALCRAFT_GENERATED_GLAD_ROOT}/src/gl.c"
    )
    target_include_directories(valcraft_glad PUBLIC
        "${VALCRAFT_GENERATED_GLAD_ROOT}/include"
    )
    if(CMAKE_DL_LIBS)
        target_link_libraries(valcraft_glad PUBLIC ${CMAKE_DL_LIBS})
    endif()
else()
    set(VALCRAFT_GENERATED_GLAD_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/gladsources/valcraft_glad"
    )
    valcraft_generated_glad_sources_are_verified(
        "${VALCRAFT_GENERATED_GLAD_ROOT}"
        generated_glad_is_verified
    )
    if(generated_glad_is_verified)
        message(STATUS "Reusing verified generated glad2 sources")
    else()
        valcraft_require_glad_generator_runtime()
    endif()
    glad_add_library(valcraft_glad REPRODUCIBLE API gl:core=3.3)
endif()

if(NOT VALCRAFT_SYSTEM_GLAD_TARGET)
    add_custom_command(TARGET valcraft_glad POST_BUILD
        COMMAND
            ${CMAKE_COMMAND}
            -DVALCRAFT_FILE=${VALCRAFT_GENERATED_GLAD_ROOT}/include/glad/gl.h
            -DVALCRAFT_EXPECTED_SHA256=${VALCRAFT_GLAD_HEADER_SHA256}
            -P ${PROJECT_SOURCE_DIR}/tools/VerifyFileHash.cmake
        COMMAND
            ${CMAKE_COMMAND}
            -DVALCRAFT_FILE=${VALCRAFT_GENERATED_GLAD_ROOT}/include/KHR/khrplatform.h
            -DVALCRAFT_EXPECTED_SHA256=${VALCRAFT_GLAD_KHRPLATFORM_SHA256}
            -P ${PROJECT_SOURCE_DIR}/tools/VerifyFileHash.cmake
        COMMAND
            ${CMAKE_COMMAND}
            -DVALCRAFT_FILE=${VALCRAFT_GENERATED_GLAD_ROOT}/src/gl.c
            -DVALCRAFT_EXPECTED_SHA256=${VALCRAFT_GLAD_SOURCE_SHA256}
            -P ${PROJECT_SOURCE_DIR}/tools/VerifyFileHash.cmake
        COMMENT "Verifying reproducible glad2 generated sources"
        VERBATIM
    )
endif()

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
valcraft_enable_undefined_sanitizer_for(valcraft_core)
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
valcraft_enable_undefined_sanitizer_for(ValCraft)
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
valcraft_enable_undefined_sanitizer_for(valcraft_tests)
valcraft_copy_runtime_dlls(valcraft_tests)
valcraft_copy_mingw_runtime_dlls(valcraft_tests)

add_executable(valcraft_model_icon_atlas_tool EXCLUDE_FROM_ALL
    tools/generate_model_icon_atlas.cpp
)
target_link_libraries(valcraft_model_icon_atlas_tool PRIVATE valcraft_core)
valcraft_apply_strict_warnings(valcraft_model_icon_atlas_tool)
valcraft_enable_coverage_for(valcraft_model_icon_atlas_tool)
valcraft_enable_address_sanitizer_for(valcraft_model_icon_atlas_tool)
valcraft_enable_undefined_sanitizer_for(valcraft_model_icon_atlas_tool)
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

if(COMMAND doctest_discover_tests)
    doctest_discover_tests(valcraft_tests)
else()
    add_test(NAME valcraft_tests COMMAND valcraft_tests)
endif()

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

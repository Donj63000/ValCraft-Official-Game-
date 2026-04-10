#include "app/GameBranding.h"

#include <system_error>

namespace valcraft {

namespace {

auto window_icon_file_name() -> std::filesystem::path {
    return std::filesystem::path(kGameWindowIconRelativePath).filename();
}

} // namespace

auto window_icon_candidate_paths(const std::filesystem::path& working_directory,
                                 const std::filesystem::path& executable_directory)
    -> std::array<std::filesystem::path, 3> {
    const auto icon_file_name = window_icon_file_name();
    return {
        working_directory / "Images" / icon_file_name,
        executable_directory / "Images" / icon_file_name,
        executable_directory.parent_path() / "Images" / icon_file_name,
    };
}

auto resolve_window_icon_path(const std::filesystem::path& working_directory,
                              const std::filesystem::path& executable_directory)
    -> std::optional<std::filesystem::path> {
    for (const auto& candidate : window_icon_candidate_paths(working_directory, executable_directory)) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate.lexically_normal();
        }
    }

    return std::nullopt;
}

} // namespace valcraft

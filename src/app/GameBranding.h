#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string_view>

namespace valcraft {

inline constexpr std::string_view kGameDisplayName = "ValCraft (C++ version)";
inline constexpr std::string_view kGameDisplayNamePixel = "VALCRAFT (C++ VERSION)";
inline constexpr std::string_view kGameWindowTitle = "ValCraft (C++ version)";
inline constexpr std::string_view kGameWindowIconRelativePath = "Images/valcraft_icon.bmp";
inline constexpr std::string_view kGameWindowIconPreviewRelativePath = "Images/valcraft_icon.png";

[[nodiscard]] auto window_icon_candidate_paths(const std::filesystem::path& working_directory,
                                               const std::filesystem::path& executable_directory)
    -> std::array<std::filesystem::path, 3>;

[[nodiscard]] auto resolve_window_icon_path(const std::filesystem::path& working_directory,
                                            const std::filesystem::path& executable_directory)
    -> std::optional<std::filesystem::path>;

} // namespace valcraft

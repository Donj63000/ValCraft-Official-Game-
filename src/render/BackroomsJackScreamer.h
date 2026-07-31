#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace valcraft {

struct BackroomsJackScreamerImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba {};
    std::string error {};

    [[nodiscard]] auto valid() const noexcept -> bool {
        return width > 0 &&
               height > 0 &&
               rgba.size() ==
                   static_cast<std::size_t>(width) *
                       static_cast<std::size_t>(height) *
                       4U;
    }
};

// Je charge moi-meme le BMP afin de garder cette ressource testable sans
// initialiser SDL ni OpenGL. Le resultat est toujours RGBA, rangee du haut
// vers le bas, ce qui evite une inversion au moment du screamer.
[[nodiscard]] auto load_backrooms_jack_screamer_bmp(
    const std::filesystem::path& path) -> BackroomsJackScreamerImage;

// Je cherche les memes emplacements en developpement, dans bin/ et dans un
// paquet installe. Aucun chemin absolu n'est fige dans l'executable.
[[nodiscard]] auto resolve_backrooms_jack_screamer_path(
    const std::filesystem::path& working_directory)
    -> std::filesystem::path;

} // namespace valcraft

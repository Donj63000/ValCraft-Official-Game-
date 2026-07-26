#include "render/ModelIconAtlas.h"
#include "render/VisualMaterials.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::filesystem::path materials {
        "assets/visual/valcraft_visual_materials.vmp",
    };
    std::filesystem::path output {
        "assets/visual/valcraft_model_icons.vmia",
    };
    bool check = false;
};

[[nodiscard]] auto parse_options(
    int argument_count,
    char** arguments,
    Options& options) -> bool {
    for (int index = 1; index < argument_count; ++index) {
        const auto argument = std::string_view {arguments[index]};
        if (argument == "--check") {
            options.check = true;
            continue;
        }
        if ((argument == "--materials" ||
             argument == "--output") &&
            index + 1 < argument_count) {
            const auto value =
                std::filesystem::path {arguments[++index]};
            if (argument == "--materials") {
                options.materials = value;
            } else {
                options.output = value;
            }
            continue;
        }
        std::cerr
            << "Usage: generate_model_icon_atlas "
               "[--check] [--materials PATH] [--output PATH]\n";
        return false;
    }
    return true;
}

[[nodiscard]] auto read_bytes(
    const std::filesystem::path& path)
    -> std::vector<std::uint8_t> {
    std::error_code error;
    const auto file_size =
        std::filesystem::file_size(path, error);
    if (error ||
        file_size >
            static_cast<std::uintmax_t>(
                (std::numeric_limits<std::size_t>::max)())) {
        return {};
    }
    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(file_size));
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    if (!bytes.empty()) {
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!input ||
            input.gcount() !=
                static_cast<std::streamsize>(bytes.size())) {
            return {};
        }
    }
    return bytes;
}

} // namespace

int main(int argument_count, char** arguments) {
    Options options {};
    if (!parse_options(
            argument_count,
            arguments,
            options)) {
        return 2;
    }

    const auto materials =
        valcraft::load_visual_material_pack(
            options.materials);
    if (!materials || !materials.pack.has_value()) {
        std::cerr
            << "Erreur materiaux: "
            << materials.message << '\n';
        return 1;
    }
    const auto generated =
        valcraft::generate_model_icon_atlas(
            *materials.pack);
    if (!generated || !generated.atlas.has_value()) {
        std::cerr
            << "Erreur generation: "
            << generated.message << '\n';
        return 1;
    }
    const auto bytes =
        valcraft::serialize_model_icon_atlas(
            *generated.atlas);
    if (bytes.empty()) {
        std::cerr
            << "Erreur: l'atlas genere n'est pas serialisable.\n";
        return 1;
    }

    if (options.check) {
        const auto existing = read_bytes(options.output);
        if (existing != bytes) {
            std::cerr
                << "L'atlas d'icones differe de sa recette deterministe.\n";
            return 1;
        }
        std::cout
            << "Atlas d'icones conforme: "
            << options.output.string()
            << " (" << bytes.size()
            << " octets, checksum 0x"
            << std::hex
            << generated.atlas->metadata.content_checksum
            << std::dec << ")\n";
        return 0;
    }

    std::error_code directory_error;
    const auto parent = options.output.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(
            parent,
            directory_error);
        if (directory_error) {
            std::cerr
                << "Impossible de creer le dossier de sortie: "
                << directory_error.message() << '\n';
            return 1;
        }
    }
    std::string write_error;
    if (!valcraft::write_model_icon_atlas(
            options.output,
            *generated.atlas,
            &write_error)) {
        std::cerr
            << "Erreur d'ecriture: "
            << write_error << '\n';
        return 1;
    }
    std::cout
        << "Atlas d'icones genere: "
        << options.output.string()
        << " (" << bytes.size()
        << " octets, checksum 0x"
        << std::hex
        << generated.atlas->metadata.content_checksum
        << std::dec << ")\n";
    return 0;
}

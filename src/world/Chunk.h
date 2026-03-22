#pragma once

#include "world/Block.h"

#include <array>
#include <cstdint>

namespace valcraft {

inline constexpr auto kChunkVolume = static_cast<std::size_t>(kChunkSizeX * kChunkHeight * kChunkSizeZ);

class Chunk {
public:
    explicit Chunk(ChunkCoord coord = {});

    [[nodiscard]] auto coord() const noexcept -> ChunkCoord;
    [[nodiscard]] auto in_bounds_local(int x, int y, int z) const noexcept -> bool;
    [[nodiscard]] auto get_local(int x, int y, int z) const -> BlockId;
    [[nodiscard]] auto get_sky_light_local(int x, int y, int z) const -> std::uint8_t;
    [[nodiscard]] auto get_block_light_local(int x, int y, int z) const -> std::uint8_t;
    void set_local(int x, int y, int z, BlockId block_id);
    void set_sky_light_local(int x, int y, int z, std::uint8_t light_level);
    void set_block_light_local(int x, int y, int z, std::uint8_t light_level);
    void fill(BlockId block_id);
    void clear_lighting() noexcept;
    [[nodiscard]] auto rebuild_sky_light_column(int x, int z) -> bool;
    void copy_block_light_from(const std::uint8_t* data, std::size_t count);
    [[nodiscard]] auto blocks() const noexcept -> const std::array<BlockId, kChunkVolume>&;
    [[nodiscard]] auto sky_light() const noexcept -> const std::array<std::uint8_t, kChunkVolume>&;
    [[nodiscard]] auto block_light() const noexcept -> const std::array<std::uint8_t, kChunkVolume>&;
    [[nodiscard]] auto has_meshable_blocks() const noexcept -> bool;
    [[nodiscard]] auto min_mesh_y() const noexcept -> int;
    [[nodiscard]] auto max_mesh_y() const noexcept -> int;

    [[nodiscard]] auto is_dirty() const noexcept -> bool;
    [[nodiscard]] auto is_lighting_dirty() const noexcept -> bool;
    void mark_dirty() noexcept;
    void mark_lighting_dirty() noexcept;
    void clear_dirty() noexcept;
    void clear_lighting_dirty() noexcept;

private:
    [[nodiscard]] static auto index_of(int x, int y, int z) noexcept -> std::size_t;
    void rebuild_meshable_bounds() noexcept;

    ChunkCoord coord_ {};
    std::array<BlockId, kChunkVolume> blocks_ {};
    std::array<std::uint8_t, kChunkVolume> sky_light_ {};
    std::array<std::uint8_t, kChunkVolume> block_light_ {};
    int min_mesh_y_ = kChunkHeight;
    int max_mesh_y_ = kWorldMinY - 1;
    bool dirty_ = true;
    bool lighting_dirty_ = true;
};

} // namespace valcraft

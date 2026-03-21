#pragma once

#include "world/Block.h"

#include <array>

namespace valcraft {

class Chunk {
public:
    explicit Chunk(ChunkCoord coord = {});

    [[nodiscard]] auto coord() const noexcept -> ChunkCoord;
    [[nodiscard]] auto in_bounds_local(int x, int y, int z) const noexcept -> bool;
    [[nodiscard]] auto get_local(int x, int y, int z) const -> BlockId;
    void set_local(int x, int y, int z, BlockId block_id);
    void fill(BlockId block_id);

    [[nodiscard]] auto is_dirty() const noexcept -> bool;
    void mark_dirty() noexcept;
    void clear_dirty() noexcept;

private:
    [[nodiscard]] static auto index_of(int x, int y, int z) noexcept -> std::size_t;

    ChunkCoord coord_ {};
    std::array<BlockId, static_cast<std::size_t>(kChunkSizeX * kChunkHeight * kChunkSizeZ)> blocks_ {};
    bool dirty_ = true;
};

} // namespace valcraft

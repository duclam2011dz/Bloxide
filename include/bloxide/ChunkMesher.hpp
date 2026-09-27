#pragma once

#include "bloxide/Chunk.hpp"

#include <memory>

namespace bloxide {

struct ChunkNeighbors {
    std::shared_ptr<const ChunkData> west;
    std::shared_ptr<const ChunkData> east;
    std::shared_ptr<const ChunkData> north;
    std::shared_ptr<const ChunkData> south;
};

class ChunkMesher {
public:
    [[nodiscard]] std::shared_ptr<ChunkMesh> build(ChunkCoord coord,
                                                    const std::shared_ptr<const ChunkData>& center,
                                                    const ChunkNeighbors& neighbors,
                                                    std::uint8_t lod) const;
};

} // namespace bloxide

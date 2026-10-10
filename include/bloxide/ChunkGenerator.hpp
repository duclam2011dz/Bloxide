#pragma once
#include "bloxide/Chunk.hpp"
#include <memory>
namespace bloxide {
class ChunkGenerator {
public:
    static int surfaceHeight(std::int64_t worldX, std::int64_t worldZ);
    [[nodiscard]] std::shared_ptr<ChunkData> generate(ChunkCoord coord, std::uint64_t version = 1) const;
};
} // namespace bloxide

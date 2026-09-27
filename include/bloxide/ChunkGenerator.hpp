#pragma once
#include "bloxide/Chunk.hpp"
#include <memory>
namespace bloxide {
class ChunkGenerator {
public:
    [[nodiscard]] std::shared_ptr<ChunkData> generate(ChunkCoord coord, std::uint64_t version = 1) const;
};
} // namespace bloxide

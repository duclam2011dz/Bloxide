#include "bloxide/ChunkGenerator.hpp"
namespace bloxide {
std::shared_ptr<ChunkData> ChunkGenerator::generate(ChunkCoord, std::uint64_t version) const {
    auto result = std::make_shared<ChunkData>();
    result->blocks.fill(BlockType::Air);
    for (int x = 0; x < ChunkData::Width; ++x) for (int z = 0; z < ChunkData::Depth; ++z) {
        result->blocks[result->index(x, 0, z)] = BlockType::Bedrock;
        for (int y = 1; y <= 61; ++y) result->blocks[result->index(x, y, z)] = BlockType::Stone;
        result->blocks[result->index(x, 62, z)] = BlockType::Dirt;
        result->blocks[result->index(x, 63, z)] = BlockType::Grass;
    }
    result->version = version;
    return result;
}
} // namespace bloxide

#include "bloxide/ChunkGenerator.hpp"
#include "bloxide/ChunkMesher.hpp"
#include <cassert>
#include <memory>

int main() {
    using namespace bloxide;
    assert(ChunkData::Width == 16 && ChunkData::Height == 256 && ChunkData::Depth == 16);
    const ChunkCoord negative = worldToChunk(-0.1f, -16.1f); assert(negative.x == -1 && negative.z == -2);
    assert(worldToLocal(-0.1f) == 15);
    ChunkGenerator generator;
    const auto chunk = generator.generate({-1, 2});
    assert(chunk->get(0, 0, 0) == BlockType::Bedrock);
    assert(chunk->get(0, 1, 0) == BlockType::Stone);
    assert(chunk->get(0, 62, 0) == BlockType::Dirt);
    assert(chunk->get(0, 63, 0) == BlockType::Grass);
    assert(chunk->get(0, 64, 0) == BlockType::Air);
    assert(!chunk->set(0, 0, 0, BlockType::Air));
    assert(chunk->set(0, 64, 0, BlockType::Stone));
    ChunkMesher mesher;
    const auto mesh = mesher.build({0, 0}, chunk, {}, 0);
    assert(mesh->sourceVersion == chunk->version);
    assert(!mesh->vertices.empty() && !mesh->indices.empty());
    const auto lod = mesher.build({0, 0}, chunk, {}, 1);
    assert(lod->vertices.size() == 4 && lod->indices.size() == 6);
    return 0;
}

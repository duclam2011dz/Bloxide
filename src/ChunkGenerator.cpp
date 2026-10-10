#include <bloxide/ChunkGenerator.hpp>
#include <cmath>
namespace bloxide {
int ChunkGenerator::surfaceHeight(std::int64_t x, std::int64_t z) {
    const auto mod = [](std::int64_t v) { return (v % 128 + 128) % 128; };
    constexpr double tau = 6.28318530717958647692;
    return static_cast<int>(std::floor(64.0 + 10.0 * std::sin(tau * mod(x) / 128.0)
                                         + 6.0 * std::cos(tau * mod(z) / 128.0)));
}
std::shared_ptr<ChunkData> ChunkGenerator::generate(ChunkCoord coord, std::uint64_t version) const {
    auto data = std::make_shared<ChunkData>();
    data->blocks.fill(BlockType::Air);
    for (int x=0;x<16;++x) for (int z=0;z<16;++z) {
        // Modulo before multiplication also keeps huge signed chunk coordinates safe.
        const int height = surfaceHeight((coord.x % 8) * 16 + x, (coord.z % 8) * 16 + z);
        data->blocks[data->index(x,0,z)] = BlockType::Bedrock;
        for (int y=1;y<=height;++y)
            data->blocks[data->index(x,y,z)] = y==height ? BlockType::Grass : (y>=height-3 ? BlockType::Dirt : BlockType::Stone);
    }
    data->version=version;
    return data;
}
}

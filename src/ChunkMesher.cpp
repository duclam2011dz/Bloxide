#include "bloxide/ChunkMesher.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace bloxide {
namespace {

struct Cell { BlockType block = BlockType::Air; std::uint8_t ao = 0; };

std::uint32_t packVertex(int x, int y, int z, BlockType block, int face, int ao) {
    // Vertices include the far boundaries: X/Z=16 and Y=256.
    return (static_cast<std::uint32_t>(x) & 31U) |
           ((static_cast<std::uint32_t>(y) & 511U) << 5U) |
           ((static_cast<std::uint32_t>(z) & 31U) << 14U) |
           ((static_cast<std::uint32_t>(block) & 7U) << 19U) |
           ((static_cast<std::uint32_t>(face) & 7U) << 22U) |
           ((static_cast<std::uint32_t>(ao) & 3U) << 25U);
}

} // namespace

std::shared_ptr<ChunkMesh> ChunkMesher::build(ChunkCoord coord,
                                               const std::shared_ptr<const ChunkData>& center,
                                               const ChunkNeighbors& neighbors,
                                               std::uint8_t lod) const {
    auto mesh = std::make_shared<ChunkMesh>();
    mesh->coord = coord;
    mesh->lod = center && center->edited ? 0 : lod;
    mesh->sourceVersion = center ? center->version : 0;
    if (!center) return mesh;

    std::shared_ptr<const ChunkData> surface = center;
    if (lod == 1 && !center->edited) {
        auto coarse = std::make_shared<ChunkData>(*center);
        // Exact one-cell perimeter; interior patches up to 4x4 use maximum height.
        for (int x0=1;x0<15;x0+=4) for (int z0=1;z0<15;z0+=4) {
            const int x1=std::min(15,x0+4), z1=std::min(15,z0+4);
            int height=0;
            for (int x=x0;x<x1;++x) for (int z=z0;z<z1;++z)
                for (int y=255;y>height;--y) if (isSolid(center->get(x,y,z))) { height=y; break; }
            for (int x=x0;x<x1;++x) for (int z=z0;z<z1;++z)
                for (int y=1;y<=height;++y)
                    coarse->blocks[coarse->index(x,y,z)] = y==height ? BlockType::Grass : (y>=height-3 ? BlockType::Dirt : BlockType::Stone);
        }
        surface=coarse;
    }
    const auto get = [&](int x, int y, int z) -> BlockType {
        if (surface->inBounds(x, y, z)) return surface->get(x, y, z);
        if (y < 0 || y >= ChunkData::Height) return BlockType::Air;
        if (x < 0 && neighbors.west) return neighbors.west->get(ChunkData::Width - 1, y, z);
        if (x >= ChunkData::Width && neighbors.east) return neighbors.east->get(0, y, z);
        if (z < 0 && neighbors.north) return neighbors.north->get(x, y, ChunkData::Depth - 1);
        if (z >= ChunkData::Depth && neighbors.south) return neighbors.south->get(x, y, 0);
        return BlockType::Air;
    };

    const std::array<int, 6> uSize{16, 16, 16, 16, 16, 16};
    const std::array<int, 6> vSize{256, 256, 16, 16, 256, 256};
    for (int face = 0; face < 6; ++face) {
        const int slices = (face < 2) ? 16 : (face < 4 ? 256 : 16);
        for (int slice = 0; slice < slices; ++slice) {
            std::vector<Cell> mask(static_cast<std::size_t>(uSize[face] * vSize[face]));
            for (int v = 0; v < vSize[face]; ++v) for (int u = 0; u < uSize[face]; ++u) {
                int x = 0, y = 0, z = 0, nx = 0, ny = 0, nz = 0;
                if (face < 2) { x = slice; y = v; z = u; nx = x + (face == 0 ? -1 : 1); ny = y; nz = z; }
                else if (face < 4) { x = u; y = slice; z = v; nx = x; ny = y + (face == 2 ? 1 : -1); nz = z; }
                else { x = u; y = v; z = slice; nx = x; ny = y; nz = z + (face == 4 ? -1 : 1); }
                const BlockType block = get(x, y, z);
                if (isSolid(block) && !isSolid(get(nx, ny, nz))) {
                    const int sideA = (face < 2) ? (x + (face == 0 ? -1 : 1)) : x;
                    const int sideB = (face < 4) ? (y + (face == 3 ? -1 : 1)) : y;
                    const int sideC = (face >= 4) ? (z + (face == 4 ? -1 : 1)) : z;
                    const int adjacent = isSolid(get(sideA, sideB, sideC)) ? 1 : 0;
                    mask[static_cast<std::size_t>(v * uSize[face] + u)] = {block, static_cast<std::uint8_t>(adjacent)};
                }
            }
            for (int v = 0; v < vSize[face]; ++v) for (int u = 0; u < uSize[face]; ++u) {
                const Cell cell = mask[static_cast<std::size_t>(v * uSize[face] + u)];
                if (!isSolid(cell.block)) continue;
                int width = 1;
                while (u + width < uSize[face]) {
                    const Cell next = mask[static_cast<std::size_t>(v * uSize[face] + u + width)];
                    if (next.block != cell.block || next.ao != cell.ao) break;
                    ++width;
                }
                int height = 1;
                while (v + height < vSize[face]) {
                    bool same = true;
                    for (int i = 0; i < width; ++i) {
                        const Cell next = mask[static_cast<std::size_t>((v + height) * uSize[face] + u + i)];
                        if (next.block != cell.block || next.ao != cell.ao) { same = false; break; }
                    }
                    if (!same) break;
                    ++height;
                }
                const int u1 = u + width;
                const int v1 = v + height;
                std::array<std::array<int, 3>, 4> positions{};
                if (face == 0) positions = {{{slice, v, u}, {slice, v, u1}, {slice, v1, u1}, {slice, v1, u}}};
                if (face == 1) positions = {{{slice + 1, v, u1}, {slice + 1, v, u}, {slice + 1, v1, u}, {slice + 1, v1, u1}}};
                if (face == 2) positions = {{{u, slice + 1, v}, {u1, slice + 1, v}, {u1, slice + 1, v1}, {u, slice + 1, v1}}};
                if (face == 3) positions = {{{u, slice, v1}, {u1, slice, v1}, {u1, slice, v}, {u, slice, v}}};
                if (face == 4) positions = {{{u1, v, slice}, {u, v, slice}, {u, v1, slice}, {u1, v1, slice}}};
                if (face == 5) positions = {{{u, v, slice + 1}, {u1, v, slice + 1}, {u1, v1, slice + 1}, {u, v1, slice + 1}}};
                const std::uint32_t base = static_cast<std::uint32_t>(mesh->vertices.size());
                for (const auto& p : positions) mesh->vertices.push_back({packVertex(p[0], p[1], p[2], cell.block, face, cell.ao)});
                if (face == 2 || face == 3)
                    mesh->indices.insert(mesh->indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
                else
                    mesh->indices.insert(mesh->indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
                for (int clearV = 0; clearV < height; ++clearV) for (int clearU = 0; clearU < width; ++clearU) mask[static_cast<std::size_t>((v + clearV) * uSize[face] + u + clearU)].block = BlockType::Air;
            }
        }
    }
    return mesh;
}

} // namespace bloxide

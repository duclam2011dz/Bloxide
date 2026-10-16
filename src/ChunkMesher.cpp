#include "bloxide/ChunkMesher.hpp"

#include <algorithm>
#include <array>
#include <vector>
#include <bloxide/Textures.hpp>

namespace bloxide {
namespace {

struct Cell {
    BlockType block = BlockType::Air;
    std::uint8_t ao = 0;
    float high = 1, low = 0;
};
bool sameSurface(const Cell& a, const Cell& b, int face) {
    return a.block != BlockType::Air && b.block != BlockType::Air &&
           renderClass(a.block) == renderClass(b.block) &&
           textureBaseLayer(a.block, face) == textureBaseLayer(b.block, face) && a.ao == b.ao &&
           a.high == b.high && a.low == b.low;
}

std::uint32_t packVertex(int x, int y, int z, BlockType block, int face, int ao) {
    // Vertices include the far boundaries: X/Z=16 and Y=256.
    return (static_cast<std::uint32_t>(x) & 31U) | ((static_cast<std::uint32_t>(y) & 511U) << 5U) |
           ((static_cast<std::uint32_t>(z) & 31U) << 14U) |
           ((textureBaseLayer(block, face) & 127U) << 19U) |
           ((static_cast<std::uint32_t>(face) & 7U) << 26U) |
           ((static_cast<std::uint32_t>(ao) & 3U) << 29U);
}

} // namespace

std::shared_ptr<ChunkMesh> ChunkMesher::build(ChunkCoord coord,
                                              const std::shared_ptr<const ChunkData>& center,
                                              const ChunkNeighbors& neighbors,
                                              std::uint8_t lod) const {
    auto mesh = std::make_shared<ChunkMesh>();
    mesh->coord = coord;
    mesh->lod = 0;
    (void)lod; // Full occupancy meshes preserve natural cave mouths at every distance.
    mesh->sourceVersion = center ? center->version : 0;
    if (!center)
        return mesh;

    std::shared_ptr<const ChunkData> surface = center;
    const auto get = [&](int x, int y, int z) -> BlockType {
        if (surface->inBounds(x, y, z))
            return surface->get(x, y, z);
        if (y < 0 || y >= ChunkData::Height)
            return BlockType::Air;
        if (x < 0 && neighbors.west)
            return neighbors.west->get(ChunkData::Width - 1, y, z);
        if (x >= ChunkData::Width && neighbors.east)
            return neighbors.east->get(0, y, z);
        if (z < 0 && neighbors.north)
            return neighbors.north->get(x, y, ChunkData::Depth - 1);
        if (z >= ChunkData::Depth && neighbors.south)
            return neighbors.south->get(x, y, 0);
        return BlockType::Air;
    };
    auto state = [&](int x, int y, int z) {
        if (surface->inBounds(x, y, z))
            return surface->getState(x, y, z);
        if (y < 0 || y >= 256)
            return BlockState{};
        if (x < 0 && neighbors.west)
            return neighbors.west->getState(15, y, z);
        if (x >= 16 && neighbors.east)
            return neighbors.east->getState(0, y, z);
        if (z < 0 && neighbors.north)
            return neighbors.north->getState(x, y, 15);
        if (z >= 16 && neighbors.south)
            return neighbors.south->getState(x, y, 0);
        return BlockState{};
    };
    auto floatQuad = [&](const std::array<std::array<float, 3>, 4>& p, unsigned layer,
                         unsigned face, bool transparent) {
        auto& vertices = transparent ? mesh->translucentVertices : mesh->plantVertices;
        auto& indices = transparent ? mesh->translucentIndices : mesh->plantIndices;
        auto base = std::uint32_t(vertices.size()), first = std::uint32_t(indices.size());
        for (auto& v : p)
            vertices.push_back({v[0], v[1], v[2], v[0], v[1], v[2], layer, face, 0});
        if (face == 2 || face == 3)
            indices.insert(indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
        else
            indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        if (transparent) {
            float x = 0, y = 0, z = 0;
            for (auto& v : p) {
                x += v[0];
                y += v[1];
                z += v[2];
            }
            mesh->translucentFaces.push_back({first, 6, x / 4, y / 4, z / 4});
        }
    };

    const std::array<int, 6> uSize{16, 16, 16, 16, 16, 16};
    const std::array<int, 6> vSize{256, 256, 16, 16, 256, 256};
    for (int face = 0; face < 6; ++face) {
        const int slices = (face < 2) ? 16 : (face < 4 ? 256 : 16);
        for (int slice = 0; slice < slices; ++slice) {
            thread_local std::vector<Cell> mask;
            mask.assign(static_cast<std::size_t>(uSize[face] * vSize[face]), {});
            for (int v = 0; v < vSize[face]; ++v)
                for (int u = 0; u < uSize[face]; ++u) {
                    int x = 0, y = 0, z = 0, nx = 0, ny = 0, nz = 0;
                    if (face < 2) {
                        x = slice;
                        y = v;
                        z = u;
                        nx = x + (face == 0 ? -1 : 1);
                        ny = y;
                        nz = z;
                    } else if (face < 4) {
                        x = u;
                        y = slice;
                        z = v;
                        nx = x;
                        ny = y + (face == 2 ? 1 : -1);
                        nz = z;
                    } else {
                        x = u;
                        y = v;
                        z = slice;
                        nx = x;
                        ny = y;
                        nz = z + (face == 4 ? -1 : 1);
                    }
                    const BlockType block = get(x, y, z);
                    auto neighbor = get(nx, ny, nz);
                    if (renderClass(block) == RenderClass::Translucent && !isOpaque(neighbor)) {
                        auto bs = state(x, y, z), ns = state(nx, ny, nz);
                        float high =
                                  block == BlockType::Water
                                      ? (state(x, y + 1, z) == BlockType::Water ? 1
                                                                                : bs.waterHeight())
                                      : 1,
                              low = 0;
                        bool visible = true;
                        if (block == neighbor) {
                            if (block == BlockType::Glass || face == 2 || face == 3)
                                visible = false;
                            else {
                                float nh = state(nx, ny + 1, nz) == BlockType::Water
                                               ? 1
                                               : ns.waterHeight();
                                if (nh >= high)
                                    visible = false;
                                else
                                    low = nh;
                            }
                        }
                        if (visible)
                            mask[std::size_t(v * uSize[face] + u)] = {block, 0, high, low};
                    }
                    if (isSolid(block) && (!isOpaque(neighbor)) &&
                        !(renderClass(block) != RenderClass::Opaque && block == neighbor) &&
                        renderClass(block) != RenderClass::Translucent) {
                        const int sideA = (face < 2) ? (x + (face == 0 ? -1 : 1)) : x;
                        const int sideB = (face < 4) ? (y + (face == 3 ? -1 : 1)) : y;
                        const int sideC = (face >= 4) ? (z + (face == 4 ? -1 : 1)) : z;
                        const int adjacent = isOpaque(get(sideA, sideB, sideC)) ? 1 : 0;
                        mask[static_cast<std::size_t>(v * uSize[face] + u)] = {
                            block, static_cast<std::uint8_t>(adjacent)};
                    }
                }
            for (int v = 0; v < vSize[face]; ++v)
                for (int u = 0; u < uSize[face]; ++u) {
                    const Cell cell = mask[static_cast<std::size_t>(v * uSize[face] + u)];
                    if (cell.block == BlockType::Air)
                        continue;
                    int width = 1;
                    while (u + width < uSize[face]) {
                        const Cell next =
                            mask[static_cast<std::size_t>(v * uSize[face] + u + width)];
                        if (!sameSurface(next, cell, face))
                            break;
                        ++width;
                    }
                    int height = 1;
                    while (v + height < vSize[face]) {
                        if (renderClass(cell.block) == RenderClass::Translucent && face != 2 &&
                            face != 3 && (cell.high != 1 || cell.low != 0))
                            break;
                        bool same = true;
                        for (int i = 0; i < width; ++i) {
                            const Cell next =
                                mask[static_cast<std::size_t>((v + height) * uSize[face] + u + i)];
                            if (!sameSurface(next, cell, face)) {
                                same = false;
                                break;
                            }
                        }
                        if (!same)
                            break;
                        ++height;
                    }
                    const int u1 = u + width;
                    const int v1 = v + height;
                    std::array<std::array<int, 3>, 4> positions{};
                    if (face == 0)
                        positions = {
                            {{slice, v, u}, {slice, v, u1}, {slice, v1, u1}, {slice, v1, u}}};
                    if (face == 1)
                        positions = {{{slice + 1, v, u1},
                                      {slice + 1, v, u},
                                      {slice + 1, v1, u},
                                      {slice + 1, v1, u1}}};
                    if (face == 2)
                        positions = {{{u, slice + 1, v},
                                      {u1, slice + 1, v},
                                      {u1, slice + 1, v1},
                                      {u, slice + 1, v1}}};
                    if (face == 3)
                        positions = {
                            {{u, slice, v1}, {u1, slice, v1}, {u1, slice, v}, {u, slice, v}}};
                    if (face == 4)
                        positions = {
                            {{u1, v, slice}, {u, v, slice}, {u, v1, slice}, {u1, v1, slice}}};
                    if (face == 5)
                        positions = {{{u, v, slice + 1},
                                      {u1, v, slice + 1},
                                      {u1, v1, slice + 1},
                                      {u, v1, slice + 1}}};
                    if (renderClass(cell.block) == RenderClass::Translucent) {
                        std::array<std::array<float, 3>, 4> p{};
                        for (int i = 0; i < 4; ++i) {
                            for (int a = 0; a < 3; ++a)
                                p[i][a] = float(positions[i][a]);
                            if (face == 2)
                                p[i][1] = slice + cell.high;
                            else if (face != 3) {
                                if (positions[i][1] == v)
                                    p[i][1] = v + cell.low;
                                else
                                    p[i][1] = v1 - 1 + cell.high;
                            }
                        }
                        floatQuad(p, textureBaseLayer(cell.block, face), unsigned(face), true);
                        for (int vv = 0; vv < height; ++vv)
                            for (int uu = 0; uu < width; ++uu)
                                mask[std::size_t((v + vv) * uSize[face] + u + uu)].block =
                                    BlockType::Air;
                        continue;
                    }
                    auto& vertices = renderClass(cell.block) == RenderClass::Cutout
                                         ? mesh->cutoutVertices
                                         : mesh->vertices;
                    auto& indices = renderClass(cell.block) == RenderClass::Cutout
                                        ? mesh->cutoutIndices
                                        : mesh->indices;
                    const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
                    for (const auto& p : positions)
                        vertices.push_back(
                            {packVertex(p[0], p[1], p[2], cell.block, face, cell.ao)});
                    if (face == 2 || face == 3)
                        indices.insert(indices.end(),
                                       {base, base + 2, base + 1, base, base + 3, base + 2});
                    else
                        indices.insert(indices.end(),
                                       {base, base + 1, base + 2, base, base + 2, base + 3});
                    for (int clearV = 0; clearV < height; ++clearV)
                        for (int clearU = 0; clearU < width; ++clearU)
                            mask[static_cast<std::size_t>((v + clearV) * uSize[face] + u + clearU)]
                                .block = BlockType::Air;
                }
        }
    }
    auto quad = [&](const std::array<std::array<float, 3>, 4>& p, unsigned layer, unsigned face,
                    bool transparent) {
        auto& vertices = transparent ? mesh->translucentVertices : mesh->plantVertices;
        auto& indices = transparent ? mesh->translucentIndices : mesh->plantIndices;
        auto base = std::uint32_t(vertices.size()), first = std::uint32_t(indices.size());
        for (auto& v : p)
            vertices.push_back({v[0], v[1], v[2], v[0], v[1], v[2], layer, face, 0});
        if (face == 2 || face == 3)
            indices.insert(indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
        else
            indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        if (transparent) {
            float x = 0, y = 0, z = 0;
            for (auto& v : p) {
                x += v[0];
                y += v[1];
                z += v[2];
            }
            mesh->translucentFaces.push_back({first, 6, x / 4, y / 4, z / 4});
        }
    };
    for (int y = 0; y < 256; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                auto block = state(x, y, z);
                auto type = block.type();
                if (isFlower(type)) {
                    unsigned layer = textureBaseLayer(type, 5);
                    quad({{{x + 0.15f, y + 0.0f, z + 0.15f},
                           {x + 0.85f, y + 0.0f, z + 0.85f},
                           {x + 0.85f, y + 0.9f, z + 0.85f},
                           {x + 0.15f, y + 0.9f, z + 0.15f}}},
                         layer, 5, false);
                    quad({{{x + 0.85f, y + 0.0f, z + 0.15f},
                           {x + 0.15f, y + 0.0f, z + 0.85f},
                           {x + 0.15f, y + 0.9f, z + 0.85f},
                           {x + 0.85f, y + 0.9f, z + 0.15f}}},
                         layer, 5, false);
                }
            }
    return mesh;
}

} // namespace bloxide

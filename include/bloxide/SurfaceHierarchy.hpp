#pragma once
#include <bloxide/Chunk.hpp>
namespace bloxide {
class SurfaceHierarchy {
  public:
    // Fine input is immutable. Integer-shell/material/ambiguous vertices stay fine.
    static std::shared_ptr<ChunkMesh> build(const ChunkMesh& fine, unsigned cellStep,
                                            const std::shared_ptr<const ChunkMesh>& previous = {},
                                            float previousMorph = 1,
                                            const std::atomic<bool>* cancel = nullptr);
    static std::uint32_t anchor(float x, float y, float z) noexcept;
    static bool validMorph(const ChunkMesh&);
};
} // namespace bloxide

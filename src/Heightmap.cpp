#include <bloxide/Heightmap.hpp>
#include <bloxide/Coordinates.hpp>
#include <bloxide/SurfaceHierarchy.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <bloxide/Trace.hpp>
namespace bloxide {
namespace {
int radius(ChunkCoord a, ChunkCoord b) {
    return int(std::max(std::llabs(a.x - b.x), std::llabs(a.z - b.z)));
}
constexpr std::size_t FarReservation = 128 * MiB;
std::shared_ptr<const ChunkData> sourceData(const ChunkGenerator& gen, ChunkCoord coord,
                                            std::shared_ptr<const ChunkData> existing,
                                            const std::shared_ptr<const ChunkEdits>& edits,
                                            const std::atomic<bool>* cancel) {
    if (!existing)
        existing = gen.generate(coord, 1, cancel);
    if (!existing)
        return {};
    std::shared_ptr<ChunkData> copy;
    if (edits)
        for (auto& [key, value] : *edits) {
            int x = key & 15, y = (key >> 4) & 255, z = (key >> 12) & 15;
            if (existing->getState(x, y, z) == value)
                continue;
            if (!copy)
                copy = std::make_shared<ChunkData>(*existing);
            copy->setState(x, y, z, value);
        }
    return copy ? std::shared_ptr<const ChunkData>(copy) : existing;
}
std::uint64_t signature(const std::shared_ptr<const ChunkData>& center,
                        const ChunkNeighbors& neighbors) {
    std::uint64_t h = 1469598103934665603ULL;
    for (auto p : {center, neighbors.west, neighbors.east, neighbors.north, neighbors.south})
        if (p)
            for (auto b : p->blocks) {
                h ^= b.value & 255;
                h *= 1099511628211ULL;
                h ^= b.value >> 8;
                h *= 1099511628211ULL;
            }
    return h;
}
} // namespace
int HeightmapMesher::selectStep(int r, int previous) {
    if (previous == 2 && r <= 13)
        return 2;
    if (previous == 4 && r >= 12 && r <= 19)
        return 4;
    if (previous == 8 && r >= 18)
        return 8;
    return r <= 12 ? 2 : r <= 18 ? 4 : 8;
}
float HeightmapMesher::heightOnMesh(const ChunkMesh& m, float x, float z, float morph) {
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        auto a = m.heightVertices[m.indices[i]], b = m.heightVertices[m.indices[i + 1]],
             c = m.heightVertices[m.indices[i + 2]];
        if (a.face != 2 || b.face != 2 || c.face != 2)
            continue;
        double d = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (std::abs(d) < 1e-9)
            continue;
        double u = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / d;
        double v = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / d, w = 1 - u - v;
        if (u >= -1e-5 && v >= -1e-5 && w >= -1e-5)
            return float(u * (a.fromY + (a.y - a.fromY) * morph) +
                         v * (b.fromY + (b.y - b.fromY) * morph) +
                         w * (c.fromY + (c.y - c.fromY) * morph));
    }
    return std::numeric_limits<float>::quiet_NaN();
}
std::shared_ptr<ChunkMesh> HeightmapMesher::build(const ChunkGenerator& gen,
                                                  const HeightTileInput& in,
                                                  const std::atomic<bool>* cancel) {
    if (in.step != 2 && in.step != 4 && in.step != 8)
        throw std::invalid_argument("heightmap step must be 2,4,8");
    if (in.coord.x < -WorldLimit / 16 || in.coord.x >= WorldLimit / 16 ||
        in.coord.z < -WorldLimit / 16 || in.coord.z >= WorldLimit / 16)
        throw std::out_of_range("heightmap coordinate outside gameplay bounds");
    for (int edge = 0; edge < 4; ++edge)
        if (in.voxelEdge[edge])
            for (float h : in.edges[edge])
                if (!std::isfinite(h) || h < 0 || h > 256)
                    throw std::invalid_argument("invalid voxel boundary snapshot");
    auto mesh = std::make_shared<ChunkMesh>();
    mesh->coord = in.coord;
    mesh->heightmap = true;
    mesh->lod = std::uint8_t(in.step);
    const auto bx = in.coord.x * 16, bz = in.coord.z * 16;
    auto height = [&](int x, int z) {
        float h = float(gen.sampleTerrain(bx + x, bz + z,
                                          (x == 0 || z == 0 || x == 16 || z == 16) ? 1 : in.step)
                            .height +
                        1);
        if ((x == 0 || x == 16) && (z == 0 || z == 16))
            return h; // canonical shared corner
        if (x == 0 && in.voxelEdge[0])
            h = in.edges[0][z];
        if (x == 16 && in.voxelEdge[1])
            h = in.edges[1][z];
        if (z == 0 && in.voxelEdge[2])
            h = in.edges[2][x];
        if (z == 16 && in.voxelEdge[3])
            h = in.edges[3][x];
        return h;
    };
    auto vertex = [&](int x, int z) {
        float h = height(x, z), old = h;
        // Fixed, full-resolution shared edges: morph only the filtered interior.
        if (in.previous && x > 0 && x < 16 && z > 0 && z < 16) {
            float p = heightOnMesh(*in.previous, float(x), float(z), in.previousMorph);
            if (std::isfinite(p))
                old = p;
        }
        auto index = std::uint32_t(mesh->heightVertices.size());
        mesh->heightVertices.push_back({float(x), h, float(z), old, 0, 2});
        return index;
    };
    for (int z = 0; z < 16; z += in.step) {
        if (cancel && *cancel)
            return {};
        for (int x = 0; x < 16; x += in.step) {
            // Clockwise viewed from above produces +Y-facing triangles.
            std::vector<std::pair<int, int>> ring;
            int s = in.step;
            for (int t = 0; t < s; t += (x == 0 ? 1 : s))
                ring.push_back({x, z + t});
            for (int t = 0; t < s; t += (z + s == 16 ? 1 : s))
                ring.push_back({x + t, z + s});
            for (int t = 0; t < s; t += (x + s == 16 ? 1 : s))
                ring.push_back({x + s, z + s - t});
            for (int t = 0; t < s; t += (z == 0 ? 1 : s))
                ring.push_back({x + s - t, z});
            auto center = vertex(x + s / 2, z + s / 2);
            std::vector<std::uint32_t> ids;
            for (auto p : ring)
                ids.push_back(vertex(p.first, p.second));
            for (std::size_t i = 0; i < ids.size(); ++i)
                mesh->indices.insert(mesh->indices.end(),
                                     {center, ids[i], ids[(i + 1) % ids.size()]});
        }
    }
    // Deep skirts are a safety fallback, not a substitute for shared-edge topology.
    for (int edge = 0; edge < 4; ++edge)
        if (in.skirts[edge])
            for (int t = 0; t < 16; ++t) {
                int x = edge == 0 ? 0 : edge == 1 ? 16 : t, z = edge == 2 ? 0 : edge == 3 ? 16 : t;
                int nx = edge < 2 ? x : x + 1, nz = edge < 2 ? z + 1 : z;
                float a = height(x, z), b = height(nx, nz);
                auto start = std::uint32_t(mesh->heightVertices.size());
                std::uint32_t face = edge == 0 ? 0 : edge == 1 ? 1 : edge == 2 ? 4 : 5;
                for (auto v :
                     std::array<HeightVertex, 4>{{{float(x), a, float(z), a, 8, face},
                                                  {float(nx), b, float(nz), b, 8, face},
                                                  {float(nx), b - 8, float(nz), b - 8, 12, face},
                                                  {float(x), a - 8, float(z), a - 8, 12, face}}})
                    mesh->heightVertices.push_back(v);
                if (edge == 0 || edge == 3)
                    mesh->indices.insert(mesh->indices.end(), {start, start + 2, start + 1, start,
                                                               start + 3, start + 2});
                else
                    mesh->indices.insert(mesh->indices.end(), {start, start + 1, start + 2, start,
                                                               start + 2, start + 3});
            }
    // Close the wedge between interpolated far edges and each integer voxel column.
    for (int edge = 0; edge < 4; ++edge)
        if (in.voxelEdge[edge])
            for (int t = 0; t < 16; ++t) {
                int x = edge == 0 ? 0 : edge == 1 ? 16 : t, z = edge == 2 ? 0 : edge == 3 ? 16 : t;
                int nx = edge < 2 ? x : x + 1, nz = edge < 2 ? z + 1 : z;
                float a = height(x, z), b = height(nx, nz), voxel = in.edges[edge][t];
                if (a == voxel && b == voxel)
                    continue;
                auto start = std::uint32_t(mesh->heightVertices.size());
                std::uint32_t face = edge == 0 ? 0 : edge == 1 ? 1 : edge == 2 ? 4 : 5;
                for (auto v :
                     std::array<HeightVertex, 4>{{{float(x), a, float(z), a, 8, face},
                                                  {float(nx), b, float(nz), b, 8, face},
                                                  {float(nx), voxel, float(nz), voxel, 8, face},
                                                  {float(x), voxel, float(z), voxel, 8, face}}})
                    mesh->heightVertices.push_back(v);
                auto triangle = [&](std::uint32_t ia, std::uint32_t ib, std::uint32_t ic) {
                    auto a = mesh->heightVertices[ia], b = mesh->heightVertices[ib],
                         c = mesh->heightVertices[ic];
                    double nx = (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y);
                    double nz = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                    double outward = edge == 0 ? -nx : edge == 1 ? nx : edge == 2 ? -nz : nz;
                    if (outward < 0)
                        std::swap(ib, ic);
                    mesh->indices.insert(mesh->indices.end(), {ia, ib, ic});
                };
                triangle(start, start + 1, start + 2);
                triangle(start, start + 2, start + 3);
            }
    return mesh;
}
float HeightmapStream::morph(const Tile& t) const {
    return std::clamp(
        float(
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t.published).count() /
            0.25),
        0.0f, 1.0f);
}
void HeightmapStream::setLimits(int radius, std::size_t generationSlots, std::size_t meshSlots,
                                std::size_t memoryHeadroom) {
    std::lock_guard lock(mutex_);
    if (radius_ != radius) {
        radius_ = std::clamp(radius, 8, 24);
        initialized_ = false;
    }
    generationSlots_ = std::min<std::size_t>(32, generationSlots);
    meshSlots_ = std::min<std::size_t>(64, meshSlots);
    memoryHeadroom_ = memoryHeadroom;
}
void HeightmapStream::refill(ChunkCoord center, const std::array<float, 3>& direction,
                             const SnapshotFactory& snapshots) {
    if (!adaptive_)
        return;
    std::vector<std::shared_ptr<Tile>> requested;
    for (auto& [c, t] : tiles_)
        if (radius(c, center) > 8 && t->state == GenerationState::Requested && !t->enqueued)
            requested.push_back(t);
    std::stable_sort(requested.begin(), requested.end(), [&](auto& a, auto& b) {
        return radius(a->input.coord, center) < radius(b->input.coord, center);
    });
    std::lock_guard lock(mutex_);
    for (auto& t : requested) {
        if (jobs_.size() >= generationSlots_ ||
            (generationCredits_ && !generationCredits_->acquire()))
            break;
        auto c = t->input.coord;
        double dx = double(c.x - center.x), dz = double(c.z - center.z),
               length = std::hypot(dx, dz);
        int cameraPriority =
            length > 0 ? int(25 * (direction[0] * dx + direction[2] * dz) / length) : 0;
        t->enqueued = true;
        FarVoxelSnapshot source = snapshots ? snapshots(c) : FarVoxelSnapshot{};
        source.edits = t->input.edits;
        jobs_.push_back({t, t->input, t->revision, t->group,
                         radius(c, center) * 100 - cameraPriority, std::chrono::steady_clock::now(),
                         t->cancel, std::move(source)});
    }
}
void HeightmapStream::update(ChunkCoord center, const BorderQuery& query,
                             const std::array<float, 3>& direction,
                             const std::function<bool(ChunkCoord)>& voxelVisible,
                             std::uint64_t borderRevision, const SnapshotFactory& snapshots) {
    std::vector<Result> results;
    {
        std::lock_guard lock(mutex_);
        results.swap(results_);
    }
    bool retry = false;
    for (auto& r : results) {
        buildMs += r.ms;
        generationMs += r.generationMs;
        auto it = tiles_.find(r.job.input.coord);
        if (it == tiles_.end() || it->second != r.job.tile || *r.job.cancel ||
            r.job.tile->revision != r.job.revision) {
            ++stale;
            if (*r.job.cancel)
                ++cancelled;
            continue;
        }
        r.job.tile->enqueued = false;
        if (r.mesh) {
            r.mesh->instanceId = (1ULL << 63) | r.job.tile->instance;
            r.mesh->revision = r.job.revision;
            r.mesh->groupId = r.job.tile->group;
            r.job.tile->pending = r.mesh;
            r.job.tile->state = GenerationState::Ready;
            r.job.tile->retries = 0;
            ++completed;
        } else if (*r.job.tile->cancel) {
            r.job.tile->state = GenerationState::Cancelled;
            ++cancelled;
        } else {
            r.job.tile->state = GenerationState::Failed;
            ++r.job.tile->retries;
            retry |= r.job.tile->retries <= 3;
        }
    }
    if (initialized_ && center == lastCenter_ && borderRevision == lastBorderRevision_ && !retry) {
        refill(center, direction, snapshots);
        return;
    }
    initialized_ = true;
    lastCenter_ = center;
    lastBorderRevision_ = borderRevision;
    for (auto it = tiles_.begin(); it != tiles_.end();)
        if (radius(it->first, center) > radius_ ||
            (radius(it->first, center) <= 8 &&
             (!it->second->active ||
              (voxelVisible ? voxelVisible(it->first)
                            : bool(query(it->first.x * 16, it->first.z * 16)))))) {
            *it->second->cancel = true;
            it = tiles_.erase(it);
        } else
            ++it;
    for (auto& [c, t] : tiles_)
        if (radius(c, center) <= 8) {
            // A previous far mesh is a handoff fallback, never a new far publication.
            *t->cancel = true;
            t->group = 0;
            t->pending.reset();
            t->uploaded = false;
        }
    std::vector<std::shared_ptr<Tile>> changed;
    std::vector<std::shared_ptr<Tile>> voxelChanges;
    for (int z = -radius_; z <= radius_; ++z)
        for (int x = -radius_; x <= radius_; ++x) {
            int r = std::max(std::abs(x), std::abs(z));
            if (r <= 8)
                continue;
            ChunkCoord c{center.x + x, center.z + z};
            // Render only supported gameplay coordinates; no unchecked multiplication.
            if (c.x < -WorldLimit / 16 || c.x >= WorldLimit / 16 || c.z < -WorldLimit / 16 ||
                c.z >= WorldLimit / 16)
                continue;
            auto& tile = tiles_[c];
            bool created = !tile;
            if (created) {
                tile = std::make_shared<Tile>();
                tile->instance = nextInstance_++;
                tile->input.coord = c;
            }
            HeightTileInput next = tile->input;
            next.step = HeightmapMesher::selectStep(r, tile->active ? tile->active->lod : 0);
            if (adaptive_ && snapshots) {
                auto source = snapshots(c);
                next.edits = source.edits;
                bool changedData = next.edits != tile->input.edits;
                if (changedData)
                    voxelChanges.push_back(tile);
                if (created || changedData || next.step != tile->input.step ||
                    (tile->state == GenerationState::Failed && tile->retries <= 3))
                    changed.push_back(tile);
                else
                    continue;
                tile->input = std::move(next);
                continue;
            }
            next.voxelEdge.fill(false);
            for (auto& edge : next.edges)
                edge.fill(0);
            std::array<ChunkCoord, 4> neighbors{
                {{c.x - 1, c.z}, {c.x + 1, c.z}, {c.x, c.z - 1}, {c.x, c.z + 1}}};
            for (int e = 0; e < 4; ++e)
                if (radius(neighbors[e], center) <= 8)
                    for (int t = 0; t <= 16; ++t) {
                        auto wx = c.x * 16 + (e == 0    ? -1
                                              : e == 1  ? 16
                                              : t == 16 ? 15
                                                        : t),
                             wz = c.z * 16 + (e == 2    ? -1
                                              : e == 3  ? 16
                                              : t == 16 ? 15
                                                        : t);
                        if (auto h = query(wx, wz)) {
                            next.voxelEdge[e] = true;
                            next.edges[e][t] = *h;
                        }
                    }
            for (int e = 0; e < 4; ++e)
                next.skirts[e] = radius(neighbors[e], center) > radius_ || next.voxelEdge[e];
            if (created)
                changed.push_back(tile);
            else if (next.step != tile->input.step || next.voxelEdge != tile->input.voxelEdge ||
                     next.edges != tile->input.edges || next.skirts != tile->input.skirts ||
                     (tile->state == GenerationState::Failed && tile->retries <= 3))
                changed.push_back(tile);
            else
                continue;
            tile->input = next;
        }
    if (adaptive_) {
        for (auto& t : changed) {
            *t->cancel = true;
            t->cancel = std::make_shared<std::atomic<bool>>(false);
            ++t->revision;
            t->pending.reset();
            t->uploaded = false;
            t->state = GenerationState::Requested;
            t->enqueued = false;
            t->input.previous = t->active;
            t->input.previousMorph = morph(*t);
            t->group = (1ULL << 63) | nextGroup_++;
        }
        // Journal changes couple neighboring active tiles; LOD-only changes keep fixed borders.
        for (auto& t : voxelChanges)
            if (t->active) {
                auto id = t->group;
                for (auto& [c, n] : tiles_)
                    if (n->group && radius(c, t->input.coord) <= 1)
                        n->group = id;
            }
        {
            std::lock_guard lock(mutex_);
            jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
                                       [&](const Job& j) {
                                           bool stale = *j.cancel || j.revision != j.tile->revision;
                                           if (stale && generationCredits_)
                                               generationCredits_->release();
                                           return stale;
                                       }),
                        jobs_.end());
        }
        refill(center, direction, snapshots);
        return;
    }
    if (!changed.empty()) {
        // Merge overlapping publication batches. Every pending member shares a group.
        group_ = (1ULL << 63) | nextGroup_++;
        for (auto& [c, t] : tiles_)
            if (t->group && !t->uploaded &&
                std::find(changed.begin(), changed.end(), t) == changed.end())
                changed.push_back(t);
        for (auto& [c, t] : tiles_)
            if (t->group && t->uploaded &&
                std::find(changed.begin(), changed.end(), t) == changed.end())
                changed.push_back(t);
        std::lock_guard lock(mutex_);
        for (auto& t : changed) {
            *t->cancel = true;
            t->cancel = std::make_shared<std::atomic<bool>>(false);
            ++t->revision;
            t->group = group_;
            t->pending.reset();
            t->uploaded = false;
            t->state = GenerationState::Requested;
            t->input.previous = t->active;
            t->input.previousMorph = morph(*t);
            double dx = double(t->input.coord.x - center.x),
                   dz = double(t->input.coord.z - center.z), length = std::hypot(dx, dz);
            int cameraPriority =
                length > 0 ? int(25 * (direction[0] * dx + direction[2] * dz) / length) : 0;
            jobs_.push_back({t,
                             t->input,
                             t->revision,
                             t->group,
                             radius(t->input.coord, center) * 100 - cameraPriority,
                             std::chrono::steady_clock::now(),
                             t->cancel,
                             {}});
        }
        jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
                                   [](const Job& j) { return j.revision != j.tile->revision; }),
                    jobs_.end());
    }
}
bool HeightmapStream::hasJobs() const {
    std::lock_guard lock(mutex_);
    return !jobs_.empty() &&
           (!adaptive_ ||
            (meshTasks_.size() < meshSlots_ &&
             reservedBytes_.load() + FarReservation <= memoryHeadroom_.load() &&
             (!meshCredits_ || meshCredits_->used.load() < meshCredits_->capacity.load())));
}
bool HeightmapStream::work(const ChunkGenerator& gen) {
    Job job;
    {
        std::lock_guard lock(mutex_);
        if (jobs_.empty())
            return false;
        if (adaptive_) {
            if (meshTasks_.size() >= meshSlots_ ||
                reservedBytes_.load() + FarReservation > memoryHeadroom_.load() ||
                (meshCredits_ && !meshCredits_->acquire()))
                return false;
            reservedBytes_ += FarReservation;
        }
        auto now = std::chrono::steady_clock::now();
        auto best = std::min_element(jobs_.begin(), jobs_.end(), [&](const Job& a, const Job& b) {
            auto score = [&](const Job& j) {
                return j.priority - std::chrono::duration<double>(now - j.queued).count() * 100;
            };
            return score(a) < score(b);
        });
        job = *best;
        jobs_.erase(best);
        if (adaptive_ && generationCredits_)
            generationCredits_->release();
        if (!*job.cancel)
            job.tile->state = GenerationState::Generating;
    }
    Result result;
    result.job = job;
    auto start = std::chrono::steady_clock::now();
    if (adaptive_) {
        TraceScope trace(TracePhase::FarGeneration, job.input.coord);
        MeshTask task;
        task.job = job;
        bool failed = false;
        try {
            auto c = job.input.coord;
            task.center =
                sourceData(gen, c, job.source.center, job.input.edits[0], job.cancel.get());
            task.neighbors.west = sourceData(gen, {c.x - 1, c.z}, job.source.neighbors.west,
                                             job.input.edits[1], job.cancel.get());
            task.neighbors.east = sourceData(gen, {c.x + 1, c.z}, job.source.neighbors.east,
                                             job.input.edits[2], job.cancel.get());
            task.neighbors.north = sourceData(gen, {c.x, c.z - 1}, job.source.neighbors.north,
                                              job.input.edits[3], job.cancel.get());
            task.neighbors.south = sourceData(gen, {c.x, c.z + 1}, job.source.neighbors.south,
                                              job.input.edits[4], job.cancel.get());
            if (!task.center || *job.cancel)
                failed = true;
            else
                task.signature = signature(task.center, task.neighbors);
        } catch (...) {
            failed = true;
        }
        task.generationMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        std::lock_guard lock(mutex_);
        if (failed) {
            result.failed = true;
            result.generationMs = task.generationMs;
            results_.push_back(std::move(result));
            reservedBytes_ -= FarReservation;
            if (meshCredits_)
                meshCredits_->release();
        } else
            meshTasks_.push_back(std::move(task));
        return true;
    }
    try {
        result.mesh = HeightmapMesher::build(gen, job.input, job.cancel.get());
    } catch (...) {
        result.failed = true;
    }
    result.ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    {
        std::lock_guard lock(mutex_);
        results_.push_back(std::move(result));
    }
    return true;
}
bool HeightmapStream::hasMeshJobs() const {
    std::lock_guard lock(mutex_);
    return !meshTasks_.empty() &&
           (!uploadCredits_ || uploadCredits_->used.load() < uploadCredits_->capacity.load());
}
bool HeightmapStream::memoryPressure() const {
    std::lock_guard lock(mutex_);
    return adaptive_ && !jobs_.empty() && reservedBytes_.load() == 0 &&
           memoryHeadroom_.load() < FarReservation;
}
bool HeightmapStream::workMeshing(const ChunkMesher& mesher) {
    MeshTask task;
    std::shared_ptr<void> lease;
    {
        std::lock_guard lock(mutex_);
        if (meshTasks_.empty())
            return false;
        if (uploadCredits_) {
            if (!uploadCredits_->acquire())
                return false;
            lease = std::shared_ptr<void>(
                uploadCredits_.get(), [credits = uploadCredits_](void*) { credits->release(); });
        }
        task = std::move(meshTasks_.back());
        meshTasks_.pop_back();
        if (meshCredits_)
            meshCredits_->release();
    }
    Result result;
    result.job = task.job;
    result.generationMs = task.generationMs;
    auto start = std::chrono::steady_clock::now();
    TraceScope trace(TracePhase::FarMeshing, task.job.input.coord);
    try {
        if (!*task.job.cancel) {
            auto fine = mesher.build(task.job.input.coord, task.center, task.neighbors, 0);
            fine->sourceVersion = task.signature;
            result.mesh = SurfaceHierarchy::build(
                *fine, unsigned(task.job.input.step), task.job.input.previous,
                task.job.input.previousMorph, task.job.cancel.get());
            if (result.mesh)
                result.mesh->uploadLease = std::move(lease);
        }
    } catch (...) {
        result.failed = true;
    }
    result.ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    {
        std::lock_guard lock(mutex_);
        results_.push_back(std::move(result));
    }
    reservedBytes_ -= FarReservation;
    return true;
}
void HeightmapStream::stop() {
    for (auto& [c, t] : tiles_)
        *t->cancel = true;
    std::lock_guard lock(mutex_);
    if (adaptive_ && generationCredits_)
        for (std::size_t i = 0; i < jobs_.size(); ++i)
            generationCredits_->release();
    jobs_.clear();
    for (std::size_t i = 0; i < meshTasks_.size(); ++i) {
        if (meshCredits_)
            meshCredits_->release();
        reservedBytes_ -= FarReservation;
    }
    meshTasks_.clear();
}
void HeightmapStream::voxelPublished(ChunkCoord coord) {
    auto it = tiles_.find(coord);
    if (it == tiles_.end())
        return;
    *it->second->cancel = true;
    tiles_.erase(it);
}
std::vector<std::shared_ptr<HeightmapStream::Tile>> HeightmapStream::pending() const {
    std::vector<std::shared_ptr<Tile>> out;
    for (auto& [c, t] : tiles_)
        if (t->pending && !t->uploaded)
            out.push_back(t);
    return out;
}
std::vector<std::shared_ptr<HeightmapStream::Tile>> HeightmapStream::ready() const {
    std::vector<std::shared_ptr<Tile>> out;
    for (auto& [c, t] : tiles_)
        if (t->group) {
            if (!t->pending || !t->uploaded)
                return {};
            out.push_back(t);
        }
    return out;
}
std::vector<std::vector<std::shared_ptr<HeightmapStream::Tile>>>
HeightmapStream::readyBatches() const {
    if (!adaptive_) {
        auto group = ready();
        return group.empty() ? std::vector<std::vector<std::shared_ptr<Tile>>>{}
                             : std::vector<std::vector<std::shared_ptr<Tile>>>{group};
    }
    std::unordered_map<std::uint64_t, std::vector<std::shared_ptr<Tile>>> groups;
    std::unordered_set<std::uint64_t> blocked;
    for (auto& [c, t] : tiles_)
        if (t->group) {
            groups[t->group].push_back(t);
            if (!t->pending || !t->uploaded)
                blocked.insert(t->group);
        }
    std::vector<std::vector<std::shared_ptr<Tile>>> out;
    for (auto& [id, members] : groups)
        if (!blocked.count(id))
            out.push_back(std::move(members));
    return out;
}
bool HeightmapStream::commit(const std::vector<std::shared_ptr<Tile>>& group) {
    auto members = group;
    if (members.empty())
        return false;
    if (adaptive_) {
        auto id = members.front()->group;
        if (!id)
            return false;
        std::size_t count = 0;
        for (auto& [c, t] : tiles_)
            if (t->group == id)
                ++count;
        if (count != members.size())
            return false;
        for (auto& t : members)
            if (t->group != id || !t->pending || !t->uploaded)
                return false;
    } else {
        members = ready();
        if (members.empty() || members.size() != group.size())
            return false;
    }
    auto now = std::chrono::steady_clock::now();
    for (auto& t : members) {
        t->active = t->pending;
        t->pending.reset();
        t->input.previous.reset();
        t->group = 0;
        t->published = now;
    }
    return true;
}
std::vector<std::shared_ptr<HeightmapStream::Tile>>
HeightmapStream::visible(ChunkCoord center) const {
    std::vector<std::shared_ptr<Tile>> out;
    for (auto& [c, t] : tiles_)
        if (t->active && radius(c, center) <= radius_)
            out.push_back(t);
    return out;
}
std::vector<ChunkCoord> HeightmapStream::coords() const {
    std::vector<ChunkCoord> out;
    for (auto& [c, t] : tiles_)
        out.push_back(c);
    return out;
}
bool HeightmapStream::settled() const {
    for (auto& [c, t] : tiles_)
        if (t->group || !t->active)
            return false;
    return !hasJobs() && !hasMeshJobs();
}
std::size_t
HeightmapStream::residentBytes(const std::vector<const ChunkData*>& alreadyCounted) const {
    thread_local std::vector<const ChunkMesh*> seen;
    seen.clear();
    std::size_t bytes = 0;
    thread_local std::vector<const ChunkData*> data;
    data.clear();
    auto source = [&](const FarVoxelSnapshot& s) {
        for (auto& p :
             {s.center, s.neighbors.west, s.neighbors.east, s.neighbors.north, s.neighbors.south})
            if (p)
                data.push_back(p.get());
    };
    auto count = [&](const auto& mesh) {
        if (mesh)
            seen.push_back(mesh.get());
    };
    for (auto& [c, t] : tiles_) {
        count(t->active);
        count(t->pending);
        count(t->input.previous);
    }
    {
        std::lock_guard lock(mutex_);
        for (auto& r : results_) {
            count(r.mesh);
            source(r.job.source);
        }
        for (auto& j : jobs_)
            source(j.source);
    }
    std::sort(seen.begin(), seen.end(), std::less<const ChunkMesh*>{});
    auto end = std::unique(seen.begin(), seen.end());
    for (auto it = seen.begin(); it != end; ++it)
        bytes += (*it)->residentByteSize();
    std::sort(data.begin(), data.end(), std::less<const ChunkData*>{});
    auto dataEnd = std::unique(data.begin(), data.end());
    for (auto it = data.begin(); it != dataEnd; ++it)
        if (!std::binary_search(alreadyCounted.begin(), alreadyCounted.end(), *it,
                                std::less<const ChunkData*>{}))
            bytes += sizeof(ChunkData);
    return bytes;
}
} // namespace bloxide

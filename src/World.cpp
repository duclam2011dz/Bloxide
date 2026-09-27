#include "bloxide/World.hpp"

#include <algorithm>
#include <cmath>

namespace bloxide {
namespace {
std::uint32_t editKey(int x, int y, int z) { return static_cast<std::uint32_t>(x) | (static_cast<std::uint32_t>(y) << 4U) | (static_cast<std::uint32_t>(z) << 12U); }
float dotXZ(const std::array<float, 3>& a, float x, float z) { return a[0] * x + a[2] * z; }
}

World::World() {
    for (int i = 0; i < 2; ++i) generationThreads_.emplace_back(&World::generationLoop, this);
    for (int i = 0; i < 2; ++i) meshingThreads_.emplace_back(&World::meshingLoop, this);
}

World::~World() {
    {
        std::scoped_lock lock(generationMutex_, meshingMutex_);
        stopping_ = true;
    }
    generationCv_.notify_all();
    meshingCv_.notify_all();
    for (auto& thread : generationThreads_) if (thread.joinable()) thread.join();
    for (auto& thread : meshingThreads_) if (thread.joinable()) thread.join();
}

std::shared_ptr<ChunkRecord> World::find(const ChunkCoord& coord) const {
    std::scoped_lock lock(mapMutex_);
    const auto it = chunks_.find(coord);
    return it == chunks_.end() ? nullptr : it->second;
}

int World::priorityFor(const ChunkCoord& coord, const ChunkCoord& player, const std::array<float, 3>& direction) const noexcept {
    const float dx = static_cast<float>(coord.x - player.x);
    const float dz = static_cast<float>(coord.z - player.z);
    const float distance = std::max(std::abs(dx), std::abs(dz));
    const float length = std::sqrt(dx * dx + dz * dz);
    const float alignment = length > 0.0f ? dotXZ(direction, dx / length, dz / length) : 1.0f;
    return static_cast<int>(distance * 100.0f - alignment * 25.0f);
}

void World::scheduleGeneration(const ChunkCoord& coord, int priority) {
    std::shared_ptr<ChunkRecord> record;
    {
        std::scoped_lock lock(mapMutex_);
        auto& slot = chunks_[coord];
        if (!slot) slot = std::make_shared<ChunkRecord>(coord);
        record = slot;
        ChunkState expected = ChunkState::Unloaded;
        if (!record->state.compare_exchange_strong(expected, ChunkState::Queued)) return;
        record->cancelRequested = false;
        record->version.fetch_add(1);
    }
    {
        std::scoped_lock lock(generationMutex_);
        generationQueue_.push({priority, coord, record, record->version.load()});
    }
    generationCv_.notify_one();
}

void World::scheduleMeshing(const std::shared_ptr<ChunkRecord>& record, int priority, std::uint8_t lod) {
    if (!record || !record->data) return;
    ChunkState expected = ChunkState::Generated;
    if (!record->state.compare_exchange_strong(expected, ChunkState::Meshing)) return;
    ChunkNeighbors neighbors;
    {
        std::scoped_lock lock(mapMutex_);
        const auto get = [&](ChunkCoord c) -> std::shared_ptr<const ChunkData> { const auto it = chunks_.find(c); return it == chunks_.end() || !it->second ? nullptr : it->second->data; };
        neighbors.west = get({record->coord.x - 1, record->coord.z});
        neighbors.east = get({record->coord.x + 1, record->coord.z});
        neighbors.north = get({record->coord.x, record->coord.z - 1});
        neighbors.south = get({record->coord.x, record->coord.z + 1});
    }
    {
        std::scoped_lock lock(meshingMutex_);
        meshingQueue_.push({priority, record->coord, record, record->data, neighbors, lod, record->version.load()});
    }
    meshingCv_.notify_one();
}

void World::applyJournal(const ChunkCoord& coord, ChunkData& data) {
    std::scoped_lock lock(mapMutex_);
    const auto it = edits_.find(coord);
    if (it == edits_.end()) return;
    for (const auto& [key, block] : it->second) data.blocks[key] = block;
}

void World::pumpResults() {
    std::vector<GenerateResult> generated;
    std::vector<MeshResult> meshed;
    {
        std::scoped_lock lock(resultMutex_);
        generated.swap(generationResults_);
        meshed.swap(meshingResults_);
    }
    for (auto& result : generated) {
        if (!result.record || result.record->cancelRequested || result.record->version.load() != result.version) continue;
        applyJournal(result.coord, *result.data);
        result.record->data = std::move(result.data);
        result.record->state = ChunkState::Generated;
        result.record->dirty = static_cast<unsigned char>(ChunkDirty::Mesh);
        ++stats_.generated;
    }
    for (auto& result : meshed) {
        if (!result.record || result.record->cancelRequested || result.record->version.load() != result.version) continue;
        result.record->mesh = std::move(result.mesh);
        result.record->state = ChunkState::Uploading;
        ++stats_.meshed;
    }
}

void World::update(const std::array<float, 3>& playerPosition, const std::array<float, 3>& viewDirection) {
    pumpResults();
    playerChunk_ = worldToChunk(playerPosition[0], playerPosition[2]);
    viewDirection_ = viewDirection;
    for (int z = -GenerationDistance; z <= GenerationDistance; ++z) for (int x = -GenerationDistance; x <= GenerationDistance; ++x) {
        const int distance = std::max(std::abs(x), std::abs(z));
        if (distance <= GenerationDistance) scheduleGeneration({playerChunk_.x + x, playerChunk_.z + z}, priorityFor({playerChunk_.x + x, playerChunk_.z + z}, playerChunk_, viewDirection_));
    }
    std::vector<std::shared_ptr<ChunkRecord>> records;
    {
        std::scoped_lock lock(mapMutex_);
        for (const auto& [coord, record] : chunks_) {
            const int distance = static_cast<int>(std::max(std::llabs(coord.x - playerChunk_.x), std::llabs(coord.z - playerChunk_.z)));
            if (distance > GenerationDistance + 2) { record->cancelRequested = true; record->state = ChunkState::Unloading; }
            else if (distance <= RenderDistance && record->state == ChunkState::Generated) records.push_back(record);
        }
    }
    for (const auto& record : records) scheduleMeshing(record, priorityFor(record->coord, playerChunk_, viewDirection_), static_cast<std::uint8_t>(std::max(std::abs(record->coord.x - playerChunk_.x), std::abs(record->coord.z - playerChunk_.z)) > 4));
    {
        std::scoped_lock lock(mapMutex_);
        for (auto it = chunks_.begin(); it != chunks_.end();) {
            if (it->second->state == ChunkState::Unloading) it = chunks_.erase(it); else ++it;
        }
        stats_.loaded = chunks_.size();
    }
    { std::scoped_lock lock(generationMutex_); stats_.generationQueue = generationQueue_.size(); }
    { std::scoped_lock lock(meshingMutex_); stats_.meshingQueue = meshingQueue_.size(); }
}

void World::generationLoop() {
    for (;;) {
        GenerateJob job;
        {
            std::unique_lock lock(generationMutex_);
            generationCv_.wait(lock, [&] { return stopping_ || !generationQueue_.empty(); });
            if (stopping_ && generationQueue_.empty()) return;
            job = generationQueue_.top(); generationQueue_.pop();
        }
        if (job.record->cancelRequested) { std::scoped_lock statsLock(statsMutex_); ++stats_.cancelledJobs; continue; }
        job.record->state = ChunkState::Loading;
        auto data = generator_.generate(job.coord, job.version);
        {
            std::scoped_lock lock(resultMutex_);
            generationResults_.push_back({job.coord, job.record, std::move(data), job.version});
        }
        { std::scoped_lock statsLock(statsMutex_); ++stats_.generationJobs; }
    }
}

void World::meshingLoop() {
    for (;;) {
        MeshJob job;
        {
            std::unique_lock lock(meshingMutex_);
            meshingCv_.wait(lock, [&] { return stopping_ || !meshingQueue_.empty(); });
            if (stopping_ && meshingQueue_.empty()) return;
            job = meshingQueue_.top(); meshingQueue_.pop();
        }
        if (job.record->cancelRequested) { std::scoped_lock statsLock(statsMutex_); ++stats_.cancelledJobs; continue; }
        auto mesh = mesher_.build(job.coord, job.center, job.neighbors, job.lod);
        {
            std::scoped_lock lock(resultMutex_);
            meshingResults_.push_back({job.coord, job.record, std::move(mesh), job.version});
        }
        { std::scoped_lock statsLock(statsMutex_); ++stats_.meshingJobs; }
    }
}

std::vector<ChunkUpload> World::takePendingUploads() {
    pumpResults();
    std::vector<ChunkUpload> uploads;
    std::scoped_lock lock(mapMutex_);
    for (const auto& [coord, record] : chunks_) if (record->state == ChunkState::Uploading && record->mesh) uploads.push_back({coord, record->mesh});
    return uploads;
}

void World::markUploaded(const ChunkCoord& coord, std::uint64_t version) {
    const auto record = find(coord);
    if (record && record->mesh && record->mesh->sourceVersion == version) record->state = ChunkState::Ready;
}

std::vector<RenderChunk> World::renderChunks(const std::array<float, 3>& playerPosition, const std::array<float, 3>& viewDirection) const {
    std::vector<RenderChunk> result;
    const ChunkCoord center = worldToChunk(playerPosition[0], playerPosition[2]);
    std::scoped_lock lock(mapMutex_);
    for (const auto& [coord, record] : chunks_) {
        if (record->state != ChunkState::Ready || !record->mesh) continue;
        const float dx = static_cast<float>(coord.x - center.x), dz = static_cast<float>(coord.z - center.z);
        if (std::max(std::abs(dx), std::abs(dz)) > RenderDistance) continue;
        const float len = std::sqrt(dx * dx + dz * dz);
        if (len > 1.0f && dotXZ(viewDirection, dx / len, dz / len) < -0.92f) continue;
        result.push_back({coord, record->mesh});
    }
    { std::scoped_lock statsLock(statsMutex_); stats_.visible = result.size(); }
    return result;
}

BlockType World::get(int worldX, int y, int worldZ) const {
    if (y < 0 || y >= ChunkData::Height) return BlockType::Air;
    const ChunkCoord coord{static_cast<std::int64_t>(std::floor(static_cast<double>(worldX) / 16.0)), static_cast<std::int64_t>(std::floor(static_cast<double>(worldZ) / 16.0))};
    const auto record = find(coord);
    return record && record->data ? record->data->get(floorMod(worldX, 16), y, floorMod(worldZ, 16)) : BlockType::Air;
}

bool World::set(int worldX, int y, int worldZ, BlockType block) {
    if (y < 0 || y >= ChunkData::Height || (y == 0 && block != BlockType::Bedrock)) return false;
    const ChunkCoord coord{static_cast<std::int64_t>(std::floor(static_cast<double>(worldX) / 16.0)), static_cast<std::int64_t>(std::floor(static_cast<double>(worldZ) / 16.0))};
    const auto record = find(coord);
    if (!record || !record->data || (get(worldX, y, worldZ) == BlockType::Bedrock && block == BlockType::Air)) return false;
    const int x = floorMod(worldX, 16), z = floorMod(worldZ, 16);
    if (!isBreakable(get(worldX, y, worldZ)) && block == BlockType::Air) return false;
    if (!record->data->set(x, y, z, block)) return false;
    { std::scoped_lock lock(mapMutex_); edits_[coord][editKey(x, y, z)] = block; }
    record->state = ChunkState::Generated;
    record->dirty = static_cast<unsigned char>(ChunkDirty::Terrain) | static_cast<unsigned char>(ChunkDirty::Mesh);
    return true;
}

RaycastHit World::raycast(const std::array<float, 3>& origin, const std::array<float, 3>& direction, float maxDistance) const noexcept {
    const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (length <= 0.0001f) return {};
    const std::array<float, 3> dir{direction[0] / length, direction[1] / length, direction[2] / length};
    Int3 previous{static_cast<int>(std::floor(origin[0])), static_cast<int>(std::floor(origin[1])), static_cast<int>(std::floor(origin[2]))};
    for (float distance = 0.0f; distance <= maxDistance; distance += 0.04f) {
        const Int3 current{static_cast<int>(std::floor(origin[0] + dir[0] * distance)), static_cast<int>(std::floor(origin[1] + dir[1] * distance)), static_cast<int>(std::floor(origin[2] + dir[2] * distance))};
        if (current.x == previous.x && current.y == previous.y && current.z == previous.z) continue;
        if (isSolid(get(current.x, current.y, current.z))) return {true, current, previous, distance};
        previous = current;
    }
    return {};
}

WorldStats World::stats() const { std::scoped_lock lock(statsMutex_); return stats_; }

} // namespace bloxide

#include <bloxide/BuildInfo.hpp>
#include <bloxide/DevTools.hpp>
#include <bloxide/DensityWorld.hpp>
#include <bloxide/Textures.hpp>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <cmath>
namespace bloxide {
namespace {
constexpr double MB = 1024.0 * 1024.0;
const char* glString(GLenum name) {
    auto p = glGetString(name);
    return p ? reinterpret_cast<const char*>(p) : "N/A";
}
void number(const char* name, double value, bool available, const char* suffix) {
    if (available)
        ImGui::Text("%s: %.2f %s", name, value, suffix);
    else
        ImGui::Text("%s: N/A", name);
}
std::pair<ImVec2, ImVec2> iconUv(BlockType b) {
    auto layer = textureBaseLayer(b, 2);
    unsigned x = layer % 16, y = layer / 16;
    return {{float(x) / 16, float(y) / 7}, {float(x + 1) / 16, float(y + 1) / 7}};
}
} // namespace
bool DevTools::initialize(SDL_Window* window, void* context, const std::filesystem::path& assets,
                          const std::filesystem::path& captures) {
    window_ = window;
    captureRoot_ = captures;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL2_InitForOpenGL(window, context)) {
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    initialized_ = true;
    writerStopping_ = false;
    writer_ = std::thread(&DevTools::writerLoop, this);
    try {
        auto catalog = AssetLoader::load(assets);
        std::vector<unsigned char> rgba(512 * 224 * 4, 0);
        for (unsigned layer = 0; layer < catalog.layerCount; ++layer)
            for (unsigned y = 0; y < 32; ++y)
                std::copy_n(catalog.layers[layer].rgba.begin() + y * 128, 128,
                            rgba.begin() + ((layer / 16 * 32 + y) * 512 + layer % 16 * 32) * 4);
        glGenTextures(1, &icons_);
        glBindTexture(GL_TEXTURE_2D, icons_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 512, 224, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glGenTextures(1, &previewTexture_);
        glBindTexture(GL_TEXTURE_2D, previewTexture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     previewPixels_.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        hardware_.start(glString(GL_VENDOR), glString(GL_RENDERER), glString(GL_VERSION));
    } catch (...) {
        shutdown();
        throw;
    }
    return true;
}
void DevTools::shutdown() {
    if (!initialized_)
        return;
    TraceBuffer::instance().setEnabled(false);
    if (locate_.valid())
        locate_.wait();
    {
        std::lock_guard lock(exportMutex_);
        writerStopping_ = true;
    }
    exportCv_.notify_all();
    if (writer_.joinable())
        writer_.join();
    hardware_.stop();
    if (icons_)
        glDeleteTextures(1, &icons_);
    if (previewTexture_)
        glDeleteTextures(1, &previewTexture_);
    icons_ = previewTexture_ = 0;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}
void DevTools::processEvent(const SDL_Event& event) {
    if (initialized_)
        ImGui_ImplSDL2_ProcessEvent(&event);
}
void DevTools::selectSlot(int index) {
    selected_ = std::clamp(index, 0, 8);
}
void DevTools::record(DevFrame frame) {
    frames_.push_back(frame);
    ++framesSeen_;
    if (frames_.size() > 4096)
        frames_.pop_front();
}
void DevTools::updatePreview(const World& world) {
    if (!previewRequested_)
        return;
    auto start = std::chrono::steady_clock::now();
    auto context = world.generator().densityContext();
    while (previewCursor_ < 128 * 128) {
        unsigned x = previewCursor_ % 128, z = previewCursor_ / 128;
        auto wx = previewX_ + std::int64_t(x), wz = previewZ_ + std::int64_t(z);
        double value = 0;
        try {
            if (previewActiveField_ == 0)
                value = world.surfaceHeight(wx, wz) / 248.0;
            else if (context && previewActiveField_ == 3)
                value = 0.5 + 0.25 * context->density(wx, previewActiveY_, wz);
            else if (context && previewActiveField_ == 4) {
                auto f = context->fields(wx, previewActiveY_, wz);
                value = f.terrain >= 0 && f.cave > world.settings().caveThreshold ? 1 : 0;
            } else if (context && previewActiveField_ == 5) {
                auto b = context->baseState(wx, previewActiveY_, wz).type();
                value = b == BlockType::IronOre      ? 0.4
                        : b == BlockType::GoldOre    ? 0.7
                        : b == BlockType::DiamondOre ? 1
                                                     : 0;
            } else if (context && previewActiveField_ == 6)
                value = context->baseState(wx, previewActiveY_, wz) == BlockType::Water ? 1 : 0;
            else if (context) {
                auto climate = context->climate(wx, wz);
                value = previewActiveField_ == 1 ? climate.temperature : climate.humidity;
            }
        } catch (...) {
            value = 0;
        }
        value = std::clamp(value, 0.0, 1.0);
        auto p = previewCursor_ * 4;
        previewPixels_[p] = static_cast<unsigned char>(255 * value);
        previewPixels_[p + 1] = static_cast<unsigned char>(255 * (1 - std::abs(value - 0.5) * 2));
        previewPixels_[p + 2] = static_cast<unsigned char>(255 * (1 - value));
        previewPixels_[p + 3] = 255;
        ++previewCursor_;
        if (previewCursor_ % 8 == 0 &&
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count() >= 0.5)
            break;
    }
    if (previewCursor_ == 128 * 128) {
        glBindTexture(GL_TEXTURE_2D, previewTexture_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE,
                        previewPixels_.data());
        previewRequested_ = false;
    }
}
void DevTools::exportHistory(const World& world, const Renderer& renderer) {
    auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    ExportJob job;
    job.base = captureRoot_ / ("devtools-" + suffix);
    job.frames.assign(frames_.begin(), frames_.end());
    job.events = TraceBuffer::instance().snapshot();
    job.overwritten = TraceBuffer::instance().overwritten();
    job.seed = world.settings().seed;
    job.version = world.settings().generatorVersion;
    job.framesSeen = framesSeen_;
    job.mainThread = std::uint32_t(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    job.gpuMs = renderer.gpuTerrainMs();
    job.hardware = hardware_.snapshot();
    std::size_t bytes =
        job.frames.size() * sizeof(DevFrame) + job.events.size() * sizeof(TraceEvent);
    {
        std::lock_guard lock(exportMutex_);
        if (exports_.size() >= 2 || bytes > 16 * MiB) {
            ++droppedCaptures_;
            notice_ = "Capture queue/memory limit reached";
            return;
        }
        exports_.push(std::move(job));
    }
    notice_ = "Capture queued on writer thread";
    exportCv_.notify_one();
}
void DevTools::writerLoop() {
    for (;;) {
        ExportJob job;
        {
            std::unique_lock lock(exportMutex_);
            exportCv_.wait(lock, [&] { return writerStopping_ || !exports_.empty(); });
            if (exports_.empty() && writerStopping_)
                return;
            job = std::move(exports_.front());
            exports_.pop();
        }
        bool ok = true;
        std::string result;
        try {
            std::filesystem::create_directories(job.base.parent_path());
            auto base = job.base;
            std::ofstream csv(base.string() + ".csv");
            csv << "sample,frame_ms,world_ms,upload_ms,render_ms,fluid_ms,ui_ms,gpu_frame,gpu_"
                   "opaque_ms,gpu_cutout_ms,gpu_translucent_ms\n";
            unsigned i = 0;
            for (auto& f : job.frames)
                csv << i++ << ',' << f.frameMs << ',' << f.worldMs << ',' << f.uploadMs << ','
                    << f.renderMs << ',' << f.fluidMs << ',' << f.uiMs << ',' << f.gpuFrame << ','
                    << f.gpuPassMs[0] << ',' << f.gpuPassMs[1] << ',' << f.gpuPassMs[2] << '\n';
            auto hw = job.hardware;
            std::vector<double> times;
            for (auto& f : job.frames)
                times.push_back(f.frameMs);
            std::sort(times.begin(), times.end());
            auto percentile = [&](unsigned p) {
                return times.empty() ? 0 : times[(times.size() - 1) * p / 100];
            };
            std::ofstream summary(base.string() + ".json");
            summary << std::setprecision(17) << "{\"build\":" << buildInfoJson() << ",\"seed\":\""
                    << job.seed << "\",\"generator\":\"" << job.version
                    << "\",\"frames_available\":" << job.frames.size()
                    << ",\"frames_seen\":" << job.framesSeen
                    << ",\"history_is_rolling\":true,\"trace_overwritten_events\":"
                    << job.overwritten << ",\"capture_complete\":"
                    << ((job.overwritten || job.framesSeen > job.frames.size()) ? "false" : "true")
                    << ",\"p50_ms\":" << percentile(50) << ",\"p95_ms\":" << percentile(95)
                    << ",\"p99_ms\":" << percentile(99)
                    << ",\"gpu_terrain_latest_ms\":" << job.gpuMs
                    << ",\"working_set_bytes\":" << hw.workingSet
                    << ",\"private_commit_bytes\":" << hw.privateCommit
                    << ",\"gpu_memory_available\":" << (hw.gpuMemoryAvailable ? "true" : "false")
                    << ",\"gpu_engine_available\":" << (hw.gpuEngineAvailable ? "true" : "false")
                    << "}\n";
            std::ofstream trace(base.string() + "-trace.json");
            trace << std::setprecision(17) << "{\"traceEvents\":[";
            bool first = true;
            for (auto& f : job.frames) {
                const char* names[]{"World", "Upload", "Render submission", "Fluid", "UI"};
                double values[]{f.worldMs, f.uploadMs, f.renderMs, f.fluidMs, f.uiMs};
                for (int p = 0; p < 5; ++p) {
                    if (!first)
                        trace << ',';
                    first = false;
                    trace << "{\"name\":\"" << names[p]
                          << "\",\"ph\":\"X\",\"pid\":1,\"tid\":" << job.mainThread
                          << ",\"ts\":" << f.phaseStartUs[p] << ",\"dur\":" << values[p] * 1000
                          << '}';
                }
            }
            for (auto& e : job.events) {
                if (!first)
                    trace << ',';
                first = false;
                trace << "{\"name\":\"" << tracePhaseName(e.phase)
                      << "\",\"ph\":\"X\",\"pid\":1,\"tid\":" << e.thread << ",\"ts\":" << e.startUs
                      << ",\"dur\":" << e.durationUs << ",\"args\":{\"chunk_x\":\"" << e.chunk.x
                      << "\",\"chunk_z\":\"" << e.chunk.z << "\"}}";
            }
            trace << "]}\n";
            ok = bool(csv) && bool(summary) && bool(trace);
            result = ok ? "Exported " + base.filename().string() : "Capture write failed";
        } catch (const std::exception& e) {
            ok = false;
            result = e.what();
        }
        {
            std::lock_guard lock(exportMutex_);
            writerNotice_ = result;
        }
        (void)ok;
    }
}
void DevTools::draw(World& world, Renderer& renderer, const Camera& camera,
                    const PlayerPhysics& player, const BlockTarget& target) {
    if (!initialized_)
        return;
    if (locate_.valid() && locate_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto found = locate_.get();
            if (found) {
                teleportX_ = found->first.blockX();
                teleportZ_ = found->first.blockZ();
                teleportY_ = 128;
                notice_ = "Village ID " + std::to_string(found->second) + " selected for teleport";
            } else
                notice_ = "No suitable village in search region";
        } catch (const std::exception& e) {
            notice_ = e.what();
        }
    }
    {
        std::lock_guard lock(exportMutex_);
        if (!writerNotice_.empty()) {
            notice_ = std::move(writerNotice_);
            writerNotice_.clear();
        }
    }
    TraceScope uiTrace(TracePhase::UI);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    if (interactive_)
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    else
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    int width = 0, height = 0;
    SDL_GetWindowSize(window_, &width, &height);
    auto dpi = ImGui_ImplSDL2_GetContentScaleForWindow(window_);
    ImGui::GetStyle().FontScaleDpi = std::clamp(dpi, 1.0f, 3.0f);
    ImGui::SetNextWindowPos({width * 0.5f - 260, height - 76.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({520, 66}, ImGuiCond_Always);
    ImGui::Begin("Creative hotbar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     (!interactive_ ? ImGuiWindowFlags_NoInputs : 0));
    for (int slot = 0; slot < 9; ++slot) {
        if (slot)
            ImGui::SameLine();
        auto uv = iconUv(slots_[slot]);
        ImGui::PushID(slot);
        bool highlighted = slot == selected_;
        if (highlighted)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.33f, 0.48f, 0.25f, 1));
        if (ImGui::ImageButton("slot", ImTextureRef(ImTextureID(icons_)), {38, 38}, uv.first,
                               uv.second))
            selected_ = slot;
        if (highlighted)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%d: %s", slot + 1, std::string(blockName(slots_[slot])).c_str());
        ImGui::PopID();
    }
    ImGui::End();
    if (visible_) {
        auto stats = world.stats();
        auto fluid = world.fluidStats();
        auto hw = hardware_.snapshot();
        ImGui::SetNextWindowPos({12, 12}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            {std::min(950.0f, width - 24.0f), std::min(560.0f, height - 100.0f)},
            ImGuiCond_FirstUseEver);
        ImGui::Begin("Bloxide DevTools | F3 HUD, F3+Tab interact", nullptr,
                     ImGuiWindowFlags_NoSavedSettings |
                         (!interactive_ ? ImGuiWindowFlags_NoInputs : 0));
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Overview")) {
                auto frame = frames_.empty() ? DevFrame{} : frames_.back();
                ImGui::Text("FPS %.1f | frame %.3f ms | GPU terrain (delayed) %.3f ms",
                            frame.frameMs > 0 ? 1000 / frame.frameMs : 0, frame.frameMs,
                            renderer.gpuTerrainMs());
                ImGui::Text("GPU frame %llu: opaque %.3f / cutout %.3f / translucent %.3f ms",
                            (unsigned long long)renderer.gpuTimingFrame(), renderer.gpuOpaqueMs(),
                            renderer.gpuCutoutMs(), renderer.gpuTranslucentMs());
                ImGui::Text(
                    "GPU command frame %llu interval %.3f ms (includes GPU idle/scheduling gaps)",
                    (unsigned long long)renderer.gpuFrameTimingId(), renderer.gpuFrameMs());
                std::vector<float> plot;
                std::vector<double> sorted;
                for (auto& f : frames_)
                    sorted.push_back(f.frameMs);
                for (std::size_t i = frames_.size() > 256 ? frames_.size() - 256 : 0;
                     i < frames_.size(); ++i)
                    plot.push_back(float(frames_[i].frameMs));
                std::sort(sorted.begin(), sorted.end());
                if (!sorted.empty())
                    ImGui::Text("Rolling %zu frames: p50 %.3f / p95 %.3f / p99 %.3f ms",
                                sorted.size(), sorted[(sorted.size() - 1) / 2],
                                sorted[(sorted.size() - 1) * 95 / 100],
                                sorted[(sorted.size() - 1) * 99 / 100]);
                if (!plot.empty())
                    ImGui::PlotLines("Frame spikes", plot.data(), int(plot.size()), 0, nullptr, 0,
                                     std::max(33.0f, *std::max_element(plot.begin(), plot.end())),
                                     {0, 120});
                ImGui::Text("Terrain draws %zu | vertices %zu | triangles %zu | swaps %llu",
                            renderer.drawCalls(), stats.vertices, stats.indices / 3,
                            (unsigned long long)stats.swaps);
                ImGui::Text("Seed %llu | %s | %s", (unsigned long long)world.settings().seed,
                            world.settings().generatorVersion.c_str(),
                            world.settings().preset.c_str());
                auto p = player.worldFeet();
                ImGui::Text("Chunk %lld %lld | local %.5f %.5f | Y %.3f", (long long)p.chunk.x,
                            (long long)p.chunk.z, p.x, p.z, p.y);
                ImGui::Text("Velocity %.3f %.3f %.3f | grounded %d | submerged %.3f",
                            player.velocity[0], player.velocity[1], player.velocity[2],
                            player.grounded, player.submerged);
                ImGui::TextWrapped("Hardware counters are capability-dependent. Worker values are "
                                   "elapsed sums, not CPU utilization. GPU timer values are "
                                   "delayed pass durations, not hardware busy percentages.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("CPU / GPU")) {
                ImGui::TextWrapped("%s", hw.cpuName.c_str());
                ImGui::Text("Logical CPUs: %u", hw.logicalCpus);
                number("Process CPU (normalized)", hw.processCpuPercent, hw.processCpuAvailable,
                       "%");
                number("System CPU", hw.systemCpuPercent, hw.systemCpuAvailable, "%");
                ImGui::Text(
                    "Worker generation %.2f / meshing %.2f / far %.2f ms cumulative elapsed",
                    stats.generationCpuMs, stats.meshingCpuMs, stats.heightmapCpuMs);
                if (!frames_.empty()) {
                    auto f = frames_.back();
                    ImGui::Text(
                        "Main: world %.3f / upload %.3f / render %.3f / fluid %.3f / UI %.3f ms",
                        f.worldMs, f.uploadMs, f.renderMs, f.fluidMs, f.uiMs);
                }
                ImGui::Separator();
                ImGui::TextWrapped("%s",
                                   hw.gpuName.empty() ? hw.glRenderer.c_str() : hw.gpuName.c_str());
                ImGui::TextWrapped("OpenGL: %s", hw.glVersion.c_str());
                number("Process busiest 3D engine", hw.gpuBusiest3DPercent, hw.gpuEngineAvailable,
                       "%");
                ImGui::TextWrapped("Source: %s", hw.gpuEngineSource.c_str());
                ImGui::Text("GPU terrain timer: %.3f ms (latest available)",
                            renderer.gpuTerrainMs());
                ImGui::TextUnformatted(
                    "Fragment / geometry / texture-unit utilization: N/A (no vendor profiler)");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Memory")) {
                number("Working set", hw.workingSet / MB, hw.ramAvailable, "MiB");
                number("Private commit", hw.privateCommit / MB, hw.ramAvailable, "MiB");
                number("System RAM available", hw.physicalAvailable / MB, hw.ramAvailable, "MiB");
                number("System RAM total", hw.physicalTotal / MB, hw.ramAvailable, "MiB");
                ImGui::Text("Tracked cache payload %.2f MiB | GPU mesh payload %.2f MiB",
                            stats.cpuCacheBytes / MB, renderer.allocatedMeshBytes() / MB);
                ImGui::Text("Journal cell payload %.2f MiB (allocator/map overhead excluded)",
                            stats.journalPayloadBytes / MB);
                if (stats.journalPayloadBytes >= 128 * MiB)
                    ImGui::TextColored({1, 0.5f, 0.1f, 1}, "RAM-only journal warning: >=128 MiB");
                number("DXGI process local usage", hw.gpuLocalUsage / MB, hw.gpuMemoryAvailable,
                       "MiB");
                number("DXGI adapter local budget", hw.gpuLocalBudget / MB, hw.gpuMemoryAvailable,
                       "MiB");
                number("DXGI process shared usage", hw.gpuSharedUsage / MB, hw.gpuMemoryAvailable,
                       "MiB");
                ImGui::TextWrapped("%s", hw.gpuMemorySource.c_str());
                ImGui::TextUnformatted(
                    "Tracked payload is not total process RAM or driver VRAM residency.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Streaming / LOD")) {
                auto limits = world.resourceLimits();
                int cpu = int(limits.cpuBytes / MiB), gpu = int(limits.gpuBytes / MiB),
                    far = limits.requestedFarRadius;
                ImGui::Text("Near data records %zu | near ready %zu | far tiles %zu", stats.loaded,
                            stats.ready, world.heightmapTiles());
                ImGui::Text("Queues gen %zu/%zu | mesh %zu/%zu | upload %zu/%zu",
                            stats.generationQueue, limits.generationQueue, stats.meshingQueue,
                            limits.meshingQueue, stats.pendingUploads, limits.uploadQueue);
                ImGui::Text("Upload staging reservations: %zu/%zu", stats.uploadStaging,
                            limits.uploadQueue);
                ImGui::Text("Deferred gen %llu / mesh %llu | stale near %llu / far %llu",
                            (unsigned long long)stats.deferredGeneration,
                            (unsigned long long)stats.deferredMeshing,
                            (unsigned long long)stats.staleResults,
                            (unsigned long long)stats.heightmapStale);
                ImGui::Text(
                    "Skipped/cancelled near jobs %llu | cancelled far results %llu | failures %llu "
                    "| retries %llu",
                    (unsigned long long)stats.cancelledJobs, (unsigned long long)stats.farCancelled,
                    (unsigned long long)stats.failedJobs, (unsigned long long)stats.retriedJobs);
                ImGui::Text("Groups %zu | far upload pending %zu | retired GPU records %zu",
                            stats.publicationGroups, stats.farPendingUploads,
                            renderer.retiredCount());
                bool changed = ImGui::SliderInt("Requested far radius", &far, 8, 24);
                changed |= ImGui::SliderInt("CPU cache budget MiB", &cpu, 64, 2048);
                changed |= ImGui::SliderInt("GPU mesh budget MiB", &gpu, 16, 512);
                if (changed) {
                    limits.cpuBytes = std::size_t(cpu) * MiB;
                    limits.gpuBytes = std::size_t(gpu) * MiB;
                    limits.requestedFarRadius = far;
                    world.setResourceLimits(limits);
                    renderer.setMeshBudget(limits.gpuBytes);
                }
                ImGui::Text("Effective far radius: %d | upload soft budget 2 ms",
                            world.effectiveFarRadius());
                ImGui::Text("Distance reason: %s", world.distanceReason());
                ImGui::Text("LOD2/4/8 tiles: %zu/%zu/%zu | morphing %zu", stats.lodTiles[0],
                            stats.lodTiles[1], stats.lodTiles[2], stats.morphingTiles);
                ImGui::Text("QEM accepted %llu / rejected %llu | pinned/refined vertices %llu",
                            (unsigned long long)stats.collapses,
                            (unsigned long long)stats.rejectedCollapses,
                            (unsigned long long)stats.refinedVertices);
                ImGui::Text("Edit-to-publish latest: %.3f ms", stats.lastPublishMs);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Noise / Worldgen")) {
                auto p = player.worldFeet();
                ImGui::TextUnformatted(
                    "Worldgen configuration is read-only; use a new world to change seed/config.");
                auto densityContext = world.generator().densityContext();
                ImGui::Text("Noise backend: %s; explicit ISA paths are experimental",
                            noiseBackendName(densityContext ? densityContext->backend()
                                                            : NoiseBackend::Scalar));
                if (auto context = world.generator().densityContext()) {
                    auto climate = context->climate(p.blockX(), p.blockZ());
                    ImGui::Text("Temperature %.4f | humidity %.4f | biome %s", climate.temperature,
                                climate.humidity,
                                climate.biome == Biome::Forest      ? "forest"
                                : climate.biome == Biome::Mountains ? "mountains"
                                                                    : "plains");
                    ImGui::Text("Density %.5f at feet | sea Y=%d",
                                context->density(p.blockX(), int(p.y), p.blockZ()),
                                world.settings().density.seaLevel);
                }
                if (ImGui::TreeNode("Versioned configuration (read-only)")) {
                    auto& s = world.settings();
                    std::array<std::pair<const char*, const FractalSettings*>, 16> fields{
                        {{"Continental", &s.continental},
                         {"Ridges", &s.ridges},
                         {"Hills", &s.hills},
                         {"Turbulence", &s.turbulence},
                         {"Terrain warp", &s.warp2D},
                         {"Cave warp", &s.caveWarp},
                         {"Cave density", &s.caveDensity},
                         {"Roughness", &s.roughness},
                         {"Mountain mask", &s.mountainMask},
                         {"Valley", &s.valley},
                         {"Plateau", &s.plateau},
                         {"3D density", &s.density.terrain},
                         {"3D warp", &s.density.warp},
                         {"Rock mask", &s.density.rockMask},
                         {"Climate", &s.density.climate},
                         {"Aquifer", &s.density.aquifer}}};
                    for (auto& f : fields)
                        ImGui::Text("%s: octaves %d / frequency %.8g / lacunarity %.4g / gain %.4g",
                                    f.first, f.second->octaves, f.second->frequency,
                                    f.second->lacunarity, f.second->gain);
                    ImGui::Text("Height clamp %.3f..%.3f | ridge %.3f / valley %.3f / plateau %.3f",
                                s.minHeight, s.maxHeight, s.ridgeAmplitude, s.valleyAmplitude,
                                s.plateauBlend);
                    ImGui::Text(
                        "Density amplitude %.3f / band %.3f / warp %.3f | cave threshold %.3f",
                        s.density.amplitude, s.density.band, s.density.warpStrength,
                        s.caveThreshold);
                    ImGui::Text("Water table %d +/- %d | aquifer threshold %.3f",
                                s.density.waterTableBase, s.density.waterTableAmplitude,
                                s.density.aquiferThreshold);
                    for (unsigned i = 0; i < 3; ++i)
                        ImGui::Text("Ore %u: Y %d..%d / probability %.3f / radius %d..%d", i,
                                    s.density.ore[i].minY, s.density.ore[i].maxY,
                                    s.density.ore[i].probability, s.density.ore[i].minRadius,
                                    s.density.ore[i].maxRadius);
                    ImGui::Text("Tree cells %d | village regions %d / chance %.3f / jitter %d",
                                s.density.treeCell, s.density.villageCell,
                                s.density.villageProbability, s.density.villageJitter);
                    ImGui::TreePop();
                }
                ImGui::Combo("Preview field", &previewField_,
                             "Height envelope\0Temperature\0Humidity\0Density slice\0Cave carving "
                             "mask\0Ore slice\0Base water slice\0");
                ImGui::SliderInt("Slice Y", &previewY_, 4, 247);
                if (ImGui::Button("Sample 128x128 preview")) {
                    previewX_ = p.blockX() - 64;
                    previewZ_ = p.blockZ() - 64;
                    previewCursor_ = 0;
                    previewRequested_ = true;
                    previewActiveField_ = previewField_;
                    previewActiveY_ = previewY_;
                }
                updatePreview(world);
                ImGui::Text("Preview %u/16384 samples; main-thread allowance 0.5 ms/frame",
                            previewCursor_);
                ImGui::Image(ImTextureRef(ImTextureID(previewTexture_)), {256, 256});
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Fluids")) {
                ImGui::Text("10 Hz | radius 6 | ticks %llu | updates %llu | conversions %llu",
                            (unsigned long long)fluid.ticks, (unsigned long long)fluid.updates,
                            (unsigned long long)fluid.sourceConversions);
                ImGui::Text(
                    "Frontier %zu | frozen %zu | debt %.3f s | last %.3f ms | total %.3f ms",
                    fluid.frontier, fluid.frozen, fluid.debtSeconds, fluid.lastMs, fluid.totalMs);
                ImGui::Text("Water blocks %llu | source states %llu",
                            (unsigned long long)stats.blockCounts[unsigned(BlockType::Water)],
                            (unsigned long long)stats.fluidCounts[0]);
                for (unsigned i = 1; i < 16; ++i)
                    ImGui::Text("State %u: %llu", i, (unsigned long long)stats.fluidCounts[i]);
                ImGui::TextUnformatted(
                    "Space swim up / Ctrl swim down. Edits and fluid states persist only in RAM.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Features")) {
                ImGui::Text("Tree chunk incidences %llu | village chunk incidences %llu",
                            (unsigned long long)stats.treeIncidences,
                            (unsigned long long)stats.villageIncidences);
                for (unsigned b = 1; b < unsigned(BlockType::Count); ++b)
                    ImGui::Text("%s: %llu loaded blocks",
                                std::string(blockName(BlockType(b))).c_str(),
                                (unsigned long long)stats.blockCounts[b]);
                if (ImGui::Button("Locate village within 2048 blocks")) {
                    if (locate_.valid())
                        notice_ = "Village lookup already running";
                    else if (auto context = world.generator().densityContext()) {
                        auto p = player.worldFeet();
                        notice_ = "Looking up villages on a background thread";
                        locate_ = std::async(
                            std::launch::async,
                            [context,
                             p]() -> std::optional<std::pair<WorldPosition, std::uint64_t>> {
                                auto sites = context->villagesIn(
                                    {std::max(-WorldLimit + 256, p.blockX() - 2048),
                                     std::min(WorldLimit - 256, p.blockX() + 2048),
                                     std::max(-WorldLimit + 256, p.blockZ() - 2048),
                                     std::min(WorldLimit - 256, p.blockZ() + 2048), 4, 247});
                                if (sites.empty())
                                    return {};
                                auto& v = sites.front();
                                return std::make_pair(
                                    WorldPosition::fromAbsolute(double(v.x), 128, double(v.z)),
                                    v.id);
                            });
                    } else
                        notice_ = "Legacy world: structure generation disabled";
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("World Inspector")) {
                if (target.hit) {
                    auto state = world.getState(target.block.x, target.block.y, target.block.z);
                    ImGui::Text("Target %lld %d %lld | %s | reach %.4f", (long long)target.block.x,
                                target.block.y, (long long)target.block.z,
                                std::string(blockName(state.type())).c_str(), target.distance);
                    ImGui::Text("State 0x%04x | instance %llu | revision %llu",
                                unsigned(state.value), (unsigned long long)target.instanceId,
                                (unsigned long long)target.revision);
                } else
                    ImGui::TextUnformatted("No target in reach / chunk locked");
                ImGui::InputScalar("Teleport X", ImGuiDataType_S64, &teleportX_);
                ImGui::InputDouble("Teleport Y", &teleportY_);
                ImGui::InputScalar("Teleport Z", ImGuiDataType_S64, &teleportZ_);
                auto inspection = world.inspectChunk(blockToChunk(teleportX_, teleportZ_));
                if (inspection) {
                    const char* states[]{"Unloaded",  "Queued", "Loading",   "Generated", "Meshing",
                                         "Uploading", "Ready",  "Unloading", "Failed"};
                    const char* generation[]{"Requested", "Generating", "Ready", "Cancelled",
                                             "Failed"};
                    auto& i = *inspection;
                    ImGui::Text("Voxel record: %s | generation %s | instance %llu",
                                states[unsigned(i.state)], generation[unsigned(i.generation)],
                                (unsigned long long)i.instance);
                    ImGui::Text("Data %d | active %d | pending %d | upload ack %d | group %llu",
                                i.data, i.active, i.pending, i.uploaded,
                                (unsigned long long)i.group);
                    ImGui::Text(
                        "Revisions desired/active/pending: %llu / %llu / %llu | generation %llu",
                        (unsigned long long)i.revision, (unsigned long long)i.activeRevision,
                        (unsigned long long)i.pendingRevision,
                        (unsigned long long)i.generationRevision);
                } else
                    ImGui::TextUnformatted(
                        "No voxel record here (far surfaces are not collision data).");
                if (std::isfinite(teleportY_) && teleportY_ >= 0 && teleportY_ < 256 &&
                    world.columnLoaded(teleportX_, teleportZ_)) {
                    auto cell = world.getState(teleportX_, int(std::floor(teleportY_)), teleportZ_);
                    ImGui::Text(
                        "Inspector cell: %s | state 0x%04x | source %d / level %u / falling %d",
                        std::string(blockName(cell.type())).c_str(), unsigned(cell.value),
                        cell.source(), unsigned(cell.waterLevel()), cell.falling());
                }
                if (ImGui::Button("Teleport + safe respawn"))
                    try {
                        if (!std::isfinite(teleportY_) || std::abs(teleportY_) > 4096)
                            throw std::out_of_range("Teleport Y must be finite within -4096..4096");
                        teleport_ = WorldPosition::fromAbsolute(double(teleportX_), teleportY_,
                                                                double(teleportZ_));
                    } catch (const std::exception& e) {
                        notice_ = e.what();
                    }
                ImGui::Separator();
                ImGui::Text("Creative slot %d: %s", selected_ + 1,
                            std::string(blockName(selectedBlock())).c_str());
                for (unsigned b = 2; b < unsigned(BlockType::Count); ++b)
                    if (BlockType(b) != BlockType::Water) {
                        if (ImGui::Selectable(std::string(blockName(BlockType(b))).c_str(),
                                              selectedBlock() == BlockType(b)))
                            slots_[selected_] = BlockType(b);
                    }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Captures")) {
                ImGui::Text("Rolling history %zu/4096 frames; total observed %llu", frames_.size(),
                            (unsigned long long)framesSeen_);
                bool enabled = TraceBuffer::instance().enabled();
                if (ImGui::Checkbox("Record worker timeline", &enabled))
                    TraceBuffer::instance().setEnabled(enabled);
                if (ImGui::Button("Clear trace ring"))
                    TraceBuffer::instance().clear();
                ImGui::Text("Trace overwritten %llu | dropped capture requests %llu",
                            (unsigned long long)TraceBuffer::instance().overwritten(),
                            (unsigned long long)droppedCaptures_);
                if (ImGui::Button("Export CSV / JSON / trace"))
                    exportHistory(world, renderer);
                if (ImGui::Button("Screenshot after UI"))
                    screenshot_ = true;
                ImGui::TextWrapped("Export directory: %s", captureRoot_.string().c_str());
                ImGui::TextWrapped("%s", notice_.c_str());
                ImGui::TextUnformatted(
                    "Captures/readbacks may cause frame spikes; benchmark them separately.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    (void)camera;
}
} // namespace bloxide

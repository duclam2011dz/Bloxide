#pragma once
#include <bloxide/World.hpp>
#include <bloxide/Renderer.hpp>
#include <bloxide/Physics.hpp>
#include <bloxide/Telemetry.hpp>
#include <SDL.h>
#include <deque>
#include <filesystem>
#include <optional>
#include <bloxide/Trace.hpp>
#include <queue>
#include <future>
namespace bloxide {
struct DevFrame {
    double frameMs = 0, worldMs = 0, uploadMs = 0, renderMs = 0, fluidMs = 0, uiMs = 0, startUs = 0;
    std::array<double, 5> phaseStartUs{};
    std::uint64_t gpuFrame = 0;
    std::array<double, 3> gpuPassMs{};
};
class DevTools {
  public:
    ~DevTools() {
        shutdown();
    }
    bool initialize(SDL_Window*, void* context, const std::filesystem::path& assets,
                    const std::filesystem::path& captures);
    void shutdown();
    void processEvent(const SDL_Event&);
    void setVisible(bool value) {
        visible_ = value;
        if (!value)
            interactive_ = false;
    }
    bool visible() const {
        return visible_;
    }
    void setInteractive(bool value) {
        interactive_ = value;
        visible_ |= value;
    }
    bool interactive() const {
        return interactive_;
    }
    void selectSlot(int index);
    void scroll(int amount) {
        selectSlot((selected_ - amount + 900) % 9);
    }
    BlockType selectedBlock() const {
        return slots_[selected_];
    }
    void record(DevFrame);
    void exportCapture(const World& world, const Renderer& renderer) {
        exportHistory(world, renderer);
    }
    void draw(World&, Renderer&, const Camera&, const PlayerPhysics&, const BlockTarget&);
    std::optional<WorldPosition> takeTeleport() {
        auto p = teleport_;
        teleport_.reset();
        return p;
    }
    bool takeScreenshot() {
        bool result = screenshot_;
        screenshot_ = false;
        return result;
    }
    HardwareSnapshot hardware() const {
        return hardware_.snapshot();
    }
    std::size_t historySize() const {
        return frames_.size();
    }

  private:
    void exportHistory(const World&, const Renderer&);
    void updatePreview(const World&);
    bool initialized_ = false, visible_ = false, interactive_ = false, screenshot_ = false;
    int selected_ = 0;
    std::array<BlockType, 9> slots_{BlockType::Grass,  BlockType::Dirt,      BlockType::Stone,
                                    BlockType::OakLog, BlockType::OakPlanks, BlockType::Cobblestone,
                                    BlockType::Glass,  BlockType::Bricks,    BlockType::SlateRoof};
    SDL_Window* window_ = nullptr;
    unsigned icons_ = 0, previewTexture_ = 0;
    std::filesystem::path captureRoot_;
    HardwareTelemetry hardware_;
    std::deque<DevFrame> frames_;
    std::uint64_t framesSeen_ = 0;
    std::optional<WorldPosition> teleport_;
    std::int64_t teleportX_ = 0, teleportZ_ = 0;
    double teleportY_ = 128;
    int previewField_ = 0, previewY_ = 62;
    bool previewRequested_ = false;
    unsigned previewCursor_ = 0;
    int previewActiveField_ = 0, previewActiveY_ = 62;
    std::int64_t previewX_ = 0, previewZ_ = 0;
    std::array<unsigned char, 128 * 128 * 4> previewPixels_{};
    std::string notice_;
    std::future<std::optional<std::pair<WorldPosition, std::uint64_t>>> locate_;
    struct ExportJob {
        std::vector<DevFrame> frames;
        std::vector<TraceEvent> events;
        HardwareSnapshot hardware;
        std::uint64_t seed = 0, framesSeen = 0, overwritten = 0;
        std::string version;
        double gpuMs = 0;
        std::filesystem::path base;
        std::uint32_t mainThread = 0;
    };
    void writerLoop();
    std::thread writer_;
    std::mutex exportMutex_;
    std::condition_variable exportCv_;
    std::queue<ExportJob> exports_;
    bool writerStopping_ = false;
    std::string writerNotice_;
    std::uint64_t droppedCaptures_ = 0;
};
} // namespace bloxide

#include "bloxide/Renderer.hpp"
#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <sstream>
#include <bloxide/Physics.hpp>
#include <iostream>
#include <vector>
#include <chrono>
#include <filesystem>
#include <bloxide/Textures.hpp>

#ifndef BLOXIDE_SOURCE_DIR
#define BLOXIDE_SOURCE_DIR "."
#endif

namespace bloxide {
class Game {
public:
    explicit Game(const WorldSettings& settings,std::int64_t smokeOffset=0):world_(settings){
        auto p=WorldPosition::fromAbsolute(double(smokeOffset)+8.5,128,double(smokeOffset)+8.5);
        player_.origin=p.chunk;player_.feet=p.local();
    }
    int run(bool smokeTest = false);
private:
    void event(const SDL_Event& e);
    void update(float dt);
    void interact(bool place);
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    World world_;
    Camera camera_;
    Renderer renderer_;
    bool running_ = true;
    bool captured_ = true;
    bool hudEnabled_ = false;
    PlayerPhysics player_;
    bool spawned_ = false;
    bool jumpRequested_ = false;
    struct InteractionCommand { bool place; BlockTarget presented; };
    std::vector<InteractionCommand> commands_;
    BlockTarget presentedTarget_{};
};

int Game::run(bool smokeTest) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::cerr << SDL_GetError() << std::endl; return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window_ = SDL_CreateWindow("Bloxide v1.3.0", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (smokeTest ? SDL_WINDOW_HIDDEN : 0));
    if (!window_) { std::cerr << SDL_GetError() << std::endl; SDL_Quit(); return 1; }
    context_ = SDL_GL_CreateContext(window_);
    if (!context_ || !gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        std::cerr << "OpenGL context/loader failed: " << SDL_GetError() << std::endl;
        if (context_) SDL_GL_DeleteContext(context_);
        SDL_DestroyWindow(window_); SDL_Quit(); return 1;
    }
    SDL_GL_SetSwapInterval(1);
    char* executableBase=SDL_GetBasePath();
    if(!executableBase){std::cerr<<SDL_GetError()<<std::endl;SDL_GL_DeleteContext(context_);SDL_DestroyWindow(window_);SDL_Quit();return 1;}
    std::filesystem::path root(executableBase);SDL_free(executableBase);
    if (!renderer_.initialize((root/"shaders/voxel.vert").string(),(root/"shaders/voxel.frag").string(),(root/"assets/textures").string())) {
        std::cerr << "Renderer/assets initialization failed" << std::endl;
        renderer_.shutdown(); SDL_GL_DeleteContext(context_); SDL_DestroyWindow(window_); SDL_Quit(); return 1;
    }
    if(smokeTest&&!renderer_.validateTexturesForTests()){std::cerr<<"Texture Array/mipmap validation failed"<<std::endl;renderer_.shutdown();SDL_GL_DeleteContext(context_);SDL_DestroyWindow(window_);SDL_Quit();return 3;}
    SDL_SetRelativeMouseMode(smokeTest ? SDL_FALSE : SDL_TRUE);
    camera_.setPosition(player_.eye());
    int smokeFrames = 0;
    bool smokePassed = false;
    bool smokeEditing = false;
    const ChunkCoord smokeChunk=player_.origin;
    const std::int64_t smokeX=smokeChunk.x*16+15,smokeZ=smokeChunk.z*16+8;
    int smokeY=0;BlockType smokeMaterial=BlockType::Grass;
    std::uint64_t smokeRevision=0;
    int remeshFrames=0;
    int smokeEditCount=0,phaseVisibleFrames=0;
    double breakPublishMs=0;
    bool smokeAimed=false,outlineChecked=false;
    std::size_t outlinePixels=0;
    double maxFrameMs=0;
    std::uint64_t previous = SDL_GetPerformanceCounter();
    while (running_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) event(e);
        const std::uint64_t now = SDL_GetPerformanceCounter();
        const float dt = std::min(0.1f, static_cast<float>(now - previous) / static_cast<float>(SDL_GetPerformanceFrequency()));
        previous = now;
        world_.updateAt(player_.worldEye(),camera_.forward());
        renderer_.synchronize(world_.loadedCoords());
        const auto uploadStart=std::chrono::steady_clock::now();
        for (const auto& upload : world_.takePendingUploads()) {
            if(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-uploadStart).count()>=2.0)break;
            if (renderer_.upload(upload)) world_.markUploaded(upload);
        }
        for(const auto& group:world_.readyGroups())
            if(renderer_.publish(group)) world_.commitGroup(group);
        update(dt);
        if(smokeTest&&spawned_&&!smokeAimed){camera_.rotate(0,-35);smokeAimed=true;}
        for(auto& command:commands_) {
            if(!captured_||!world_.validateTargetAt(command.presented,player_.worldEye(),camera_.forward()))continue;
            presentedTarget_=command.presented;
            interact(command.place);
        }
        commands_.clear();
        const auto target=spawned_&&captured_?world_.targetAt(player_.worldEye(),camera_.forward()):BlockTarget{};
        int width = 0, height = 0;
        SDL_GetWindowSize(window_, &width, &height);
        const auto renderedChunks=world_.renderChunksAt(player_.worldEye(),camera_.forward());
        renderer_.render(camera_,renderedChunks,width,height);
        if(smokeTest&&target.hit&&!outlineChecked&&world_.stats().ready>=289){
            const auto blackPixels=[&](){
                std::vector<unsigned char> pixels(static_cast<std::size_t>(width*height*3));
                glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
                std::size_t count=0;
                for(std::size_t i=0;i<pixels.size();i+=3)if(pixels[i]<5&&pixels[i+1]<5&&pixels[i+2]<5)++count;
                return count;
            };
            auto baseline=blackPixels();
            renderer_.renderTarget(camera_,target,width,height,false);
            auto outlined=blackPixels();
            if(outlined<=baseline){std::cerr<<"Visible block outline missing"<<std::endl;break;}
            outlinePixels=outlined-baseline;
            renderer_.render(camera_,renderedChunks,width,height);
            auto occluded=target;occluded.block.y=0;
            renderer_.renderTarget(camera_,occluded,width,height,false);
            if(blackPixels()>baseline){std::cerr<<"Outline drew through terrain"<<std::endl;break;}
            renderer_.render(camera_,renderedChunks,width,height);
            renderer_.renderTarget(camera_,{},width,height,true);
            unsigned char center[3]{};
            glReadPixels(width/2,height/2,1,1,GL_RGB,GL_UNSIGNED_BYTE,center);
            if(center[0]<220||center[1]<220||center[2]<220){std::cerr<<"Crosshair missing"<<std::endl;break;}
            renderer_.render(camera_,renderedChunks,width,height);
            outlineChecked=true;
            const auto saveImage=[&](const char* filename){
                TextureImage image;image.width=width;image.height=height;image.rgba=std::vector<unsigned char>(std::size_t(width)*height*4);
                std::vector<unsigned char> raw(image.rgba.size());
                glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());
                for(int y=0;y<height;++y)std::copy_n(raw.begin()+std::size_t(height-1-y)*width*4,width*4,image.rgba.begin()+std::size_t(y)*width*4);
                auto diagnostics=root/"diagnostics"/("seed-"+std::to_string(world_.settings().seed)+"-"+std::to_string(smokeChunk.x)+"-"+std::to_string(smokeChunk.z));
                std::filesystem::create_directories(diagnostics);
                TextureGenerator::writePng(diagnostics/filename,image);
            };
            renderer_.renderTarget(camera_,target,width,height);
            saveImage("textures_near.png");
            Camera far=camera_;far.rotate(0,45);
            renderer_.render(far,renderedChunks,width,height);
            renderer_.renderTarget(far,{},width,height);
            saveImage("textures_far.png");
            renderer_.render(camera_,renderedChunks,width,height);
        }
        if(spawned_&&captured_)renderer_.renderTarget(camera_,target,width,height);
        presentedTarget_=target;
        if (smokeTest) {
            if (spawned_ && renderer_.drawCalls() > 0) {
                std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 3));
                glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
                std::size_t terrainPixels = 0;
                for (std::size_t p = 0; p < pixels.size(); p += 3)
                    if(std::abs(int(pixels[p])-122)>3||std::abs(int(pixels[p+1])-186)>3||std::abs(int(pixels[p+2])-235)>3)++terrainPixels;
                if(smokeEditing&&terrainPixels<=1000){std::cerr<<"Terrain vanished during render regression"<<std::endl;break;}
                if(!smokeEditing&&outlineChecked&&terrainPixels>1000&&world_.targetable(smokeX,smokeZ)&&world_.targetable(smokeX+1,smokeZ)) {
                    for(auto& c:world_.renderChunksAt(player_.worldEye(),camera_.forward()))
                        if(c.coord==smokeChunk)smokeRevision=c.mesh->revision;
                    world_.setMeshDelayForTests(80);
                    for(smokeY=255;smokeY>0&&!isSolid(world_.get(smokeX,smokeY,smokeZ));--smokeY){}
                    smokeMaterial=world_.get(smokeX,smokeY,smokeZ);
                    renderer_.failNextUploadForTests(smokeChunk);
                    smokeEditing=world_.set(smokeX,smokeY,smokeZ,BlockType::Air);
                    smokeEditCount=1;
                } else if(smokeEditing) {
                    bool visible=false;
                    for(auto& c:world_.renderChunksAt(player_.worldEye(),camera_.forward()))if(c.coord==smokeChunk) {
                        visible=true;
                        if(c.mesh->revision==smokeRevision){++remeshFrames;++phaseVisibleFrames;}
                    }
                    if(!visible){std::cerr<<"Active chunk disappeared during remesh"<<std::endl;break;}
                }
                if (smokeEditing&&outlineChecked&&phaseVisibleFrames>0&&world_.targetable(smokeX,smokeZ)&&renderer_.retiredCount()==0&&terrainPixels > 1000) {
                    if(smokeEditCount==1){
                        breakPublishMs=world_.stats().lastPublishMs;
                        for(auto& c:renderedChunks)if(c.coord==smokeChunk)smokeRevision=c.mesh->revision;
                        phaseVisibleFrames=0;smokeEditCount=2;
                        renderer_.failNextUploadForTests(smokeChunk);
                        if(!world_.set(smokeX,smokeY,smokeZ,smokeMaterial)){
                            std::cerr<<"Place regression edit failed"<<std::endl;break;
                        }
                    }else{
                    smokePassed = true;
                    std::cout << "terrain_pixels=" << terrainPixels << " draws=" << renderer_.drawCalls()
                              << " feet_y=" << player_.feet[1] << " grounded=" << player_.grounded
                              << " remesh_visible_frames=" << remeshFrames
                              << " edit_publish_ms=" << world_.stats().lastPublishMs
                              << " swaps=" << renderer_.swaps()
                              << " stale_results=" << world_.stats().staleResults << std::endl;
                    std::cout<<"outline_pixels="<<outlinePixels<<" max_frame_ms="<<maxFrameMs
                             <<" break_publish_ms="<<breakPublishMs<<" edits="<<smokeEditCount
                             <<" texture_load_ms="<<renderer_.textureLoadMs()<<" texture_upload_submit_ms="<<renderer_.textureUploadMs()<<std::endl;
                    running_ = false;
                    }
                }
            }
            if (++smokeFrames > 3000) running_ = false;
            SDL_Delay(1);
        }
        SDL_GL_SwapWindow(window_);
        if(smokeTest)maxFrameMs=std::max(maxFrameMs,static_cast<double>(SDL_GetPerformanceCounter()-now)*1000.0/SDL_GetPerformanceFrequency());
    }
    SDL_SetRelativeMouseMode(SDL_FALSE);
    renderer_.shutdown();
    SDL_GL_DeleteContext(context_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
    return smokeTest && !smokePassed ? 2 : 0;
}

void Game::event(const SDL_Event& e) {
    if (e.type == SDL_QUIT) running_ = false;
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_SPACE && e.key.repeat == 0) jumpRequested_ = true;
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F3 && e.key.repeat == 0) hudEnabled_ = !hudEnabled_;
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && e.key.repeat == 0) { captured_ = !captured_; SDL_SetRelativeMouseMode(captured_ ? SDL_TRUE : SDL_FALSE); }
    if (e.type == SDL_MOUSEMOTION && captured_) camera_.rotate(static_cast<float>(e.motion.xrel) * 0.1f, -static_cast<float>(e.motion.yrel) * 0.1f);
    if (e.type == SDL_MOUSEBUTTONDOWN && captured_) {
        if(e.button.button==SDL_BUTTON_LEFT)commands_.push_back({false,presentedTarget_});
        if(e.button.button==SDL_BUTTON_RIGHT)commands_.push_back({true,presentedTarget_});
    }
}

void Game::update(float dt) {
    if (!spawned_) {
        const auto x=player_.worldFeet().blockX(),z=player_.worldFeet().blockZ();
        if (!world_.columnLoaded(x, z)) return;
        for (int y = ChunkData::Height - 1; y >= 0; --y) {
            if (isSolid(world_.get(x, y, z)) && !isSolid(world_.get(x, y + 1, z)) && !isSolid(world_.get(x, y + 2, z))) {
                player_.feet[1]=y+1;
                if(!player_.clear([this](std::int64_t x,int y,std::int64_t z){return !world_.columnLoaded(x,z)||isSolid(world_.get(x,y,z));}))continue;
                player_.grounded = true;
                spawned_ = true;
                break;
            }
        }
        if (!spawned_) return;
    }
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    const auto f = camera_.forward();
    const auto r = camera_.right();
    std::array<float, 3> move{0.0f, 0.0f, 0.0f};
    if (keys[SDL_SCANCODE_W]) { move[0] += f[0]; move[2] += f[2]; }
    if (keys[SDL_SCANCODE_S]) { move[0] -= f[0]; move[2] -= f[2]; }
    if (keys[SDL_SCANCODE_D]) { move[0] += r[0]; move[2] += r[2]; }
    if (keys[SDL_SCANCODE_A]) { move[0] -= r[0]; move[2] -= r[2]; }
    const float length = std::sqrt(move[0] * move[0] + move[2] * move[2]);
    if (!captured_) move = {0.0f, 0.0f, 0.0f};
    if (length > 0.0f) { move[0] /= length; move[2] /= length; }
    player_.step(dt, move[0], move[2], jumpRequested_ && captured_, [this](std::int64_t x,int y,std::int64_t z) {
        // Unknown chunks are barriers until their collision data arrives.
        return !world_.columnLoaded(x, z) || isSolid(world_.get(x, y, z));
    },[this](std::int64_t x,int y,std::int64_t z){return world_.get(x,y,z);});
    jumpRequested_ = false;
    camera_.setPosition(player_.eye());
    camera_.setOrigin(player_.origin);
    if (hudEnabled_) {
        const auto stats = world_.stats();
        std::ostringstream title;
        title << "Bloxide v1.3.0 | loaded " << stats.loaded << " draws " << renderer_.drawCalls()
              << " feet Y " << player_.feet[1] << " grounded " << player_.grounded
              << " swaps " << stats.swaps << " edit ms " << stats.lastPublishMs;
        SDL_SetWindowTitle(window_, title.str().c_str());
    }
}

void Game::interact(bool place) {
    if (!spawned_) return;
    const auto& hit = presentedTarget_;
    if (!world_.validateTargetAt(hit,player_.worldEye(),camera_.forward())) return;
    if(!place&&!isBreakable(hit.type))return;
    const Int3 target = place ? Int3{hit.block.x+hit.normal.x,hit.block.y+hit.normal.y,hit.block.z+hit.normal.z} : hit.block;
    if (place && player_.overlapsBlock(target.x, target.y, target.z)) return;
    if(place&&(!world_.targetable(target.x,target.z)||world_.get(target.x,target.y,target.z)!=BlockType::Air||target.y<=0||target.y>=256))return;
    world_.set(target.x, target.y, target.z, place ? BlockType::Grass : BlockType::Air);
}
} // namespace bloxide

int main(int argc,char** argv){
    try{
        bool smoke=false;std::optional<std::uint64_t> seed;std::filesystem::path worldDir;
        std::int64_t offset=0;
        for(int i=1;i<argc;++i){
            std::string arg=argv[i];
            if(arg=="--help"){std::cout<<"Bloxide [--world folder] [--seed uint64-decimal]\nDefault world: SDL user data directory/worlds/default\nOnly seed/config metadata is persisted; block edits remain in RAM.\n";return 0;}
            if(arg=="--smoke-test")smoke=true;
            else if(arg=="--seed"&&i+1<argc)seed=bloxide::WorldSettings::parseSeed(argv[++i]);
            else if(arg=="--world"&&i+1<argc)worldDir=argv[++i];
            else if(arg=="--smoke-origin"&&i+1<argc){std::string v=argv[++i];std::size_t used=0;offset=std::stoll(v,&used);if(used!=v.size())throw std::invalid_argument("invalid smoke origin");}
            else throw std::invalid_argument("usage: Bloxide [--world folder] [--seed uint64] [--smoke-test]");
        }
        if(offset&&!smoke)throw std::invalid_argument("--smoke-origin is test-only");
        if(worldDir.empty()){
            char* path=smoke?SDL_GetBasePath():SDL_GetPrefPath("Bloxide","Bloxide");
            if(!path)throw std::runtime_error(SDL_GetError());
            worldDir=std::filesystem::path(path)/(smoke?"testing/smoke-world-"+std::to_string(offset)+"-"+std::to_string(seed.value_or(130)):"worlds/default");SDL_free(path);
            if(smoke&&!seed)seed=130;
        }
        auto settings=bloxide::WorldSettings::open(worldDir,seed);
        std::cout<<"world_seed="<<settings.seed<<" generator="<<settings.generatorVersion<<" metadata="<<(worldDir/"world.meta").string()<<std::endl;
        bloxide::Game game(settings,offset);return game.run(smoke);
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}

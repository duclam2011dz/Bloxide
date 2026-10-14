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
#include <fstream>
#include <bloxide/Textures.hpp>
#include <bloxide/DevTools.hpp>
#include <bloxide/DensityWorld.hpp>
#include <deque>
#include <bloxide/Trace.hpp>
#include <numeric>
#include <iomanip>

#ifndef BLOXIDE_SOURCE_DIR
#define BLOXIDE_SOURCE_DIR "."
#endif

namespace bloxide {
struct BenchmarkRun {
    bool enabled=false,overlay=false;
    double warmup=5,duration=30;
    std::string scenario="steady";
    std::filesystem::path output;
    NoiseBackend backend=NoiseBackend::Auto;
    int farRadius=24;
};
class Game {
public:
    explicit Game(const WorldSettings& settings,std::int64_t smokeOffset=0,bool lod=true,bool requireLod=false,const BenchmarkRun& benchmark={}):world_(settings,lod,benchmark.backend),requireLod_(requireLod),benchmark_(benchmark){
        auto p=WorldPosition::fromAbsolute(double(smokeOffset)+8.5,128,double(smokeOffset)+8.5);
        player_.origin=p.chunk;player_.feet=p.local();
        auto limits=world_.resourceLimits();limits.requestedFarRadius=benchmark.farRadius;world_.setResourceLimits(limits);
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
    DevTools devtools_;
    bool running_ = true;
    bool captured_ = true;
    bool hudEnabled_ = false;
    bool f3Held_=false;
    bool captureBeforeTools_=true;
    PlayerPhysics player_;
    bool spawned_ = false;
    bool requireLod_=false;
    BenchmarkRun benchmark_;
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
    window_ = SDL_CreateWindow("Bloxide v1.4.0", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (smokeTest||benchmark_.enabled ? SDL_WINDOW_HIDDEN : 0));
    if (!window_) { std::cerr << SDL_GetError() << std::endl; SDL_Quit(); return 1; }
    context_ = SDL_GL_CreateContext(window_);
    if (!context_ || !gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        std::cerr << "OpenGL context/loader failed: " << SDL_GetError() << std::endl;
        if (context_) SDL_GL_DeleteContext(context_);
        SDL_DestroyWindow(window_); SDL_Quit(); return 1;
    }
    SDL_GL_SetSwapInterval(benchmark_.enabled?0:1);
    char* executableBase=SDL_GetBasePath();
    if(!executableBase){std::cerr<<SDL_GetError()<<std::endl;SDL_GL_DeleteContext(context_);SDL_DestroyWindow(window_);SDL_Quit();return 1;}
    std::filesystem::path root(executableBase);SDL_free(executableBase);
    if (!renderer_.initialize((root/"shaders/voxel.vert").string(),(root/"shaders/voxel.frag").string(),(root/"assets/textures").string())) {
        std::cerr << "Renderer/assets initialization failed" << std::endl;
        renderer_.shutdown(); SDL_GL_DeleteContext(context_); SDL_DestroyWindow(window_); SDL_Quit(); return 1;
    }
    if(smokeTest&&!renderer_.validateTexturesForTests()){std::cerr<<"Texture Array/mipmap validation failed"<<std::endl;renderer_.shutdown();SDL_GL_DeleteContext(context_);SDL_DestroyWindow(window_);SDL_Quit();return 3;}
    if(smokeTest&&!renderer_.validateHeightmapForTests()){std::cerr<<"Heightmap integer attributes/morph validation failed"<<std::endl;renderer_.shutdown();SDL_GL_DeleteContext(context_);SDL_DestroyWindow(window_);SDL_Quit();return 3;}
    if(smokeTest&&!renderer_.validateSurfaceForTests()){std::cerr<<"XYZ surface morph/depth validation failed"<<std::endl;renderer_.shutdown();SDL_GL_DeleteContext(context_);SDL_DestroyWindow(window_);SDL_Quit();return 3;}
    if(!devtools_.initialize(window_,context_,root/"assets/textures",benchmark_.enabled?benchmark_.output/"captures":root/"benchmark/devtools")){
        std::cerr<<"DevTools backend initialization failed"<<std::endl;renderer_.shutdown();SDL_GL_DeleteContext(context_);SDL_DestroyWindow(window_);SDL_Quit();return 1;
    }
    SDL_SetRelativeMouseMode(smokeTest ? SDL_FALSE : SDL_TRUE);
    if(smokeTest){
        SDL_Event key{};key.type=SDL_KEYDOWN;key.key.keysym.sym=SDLK_F3;event(key);
        key.key.keysym.sym=SDLK_TAB;event(key);
        if(!devtools_.interactive()||captured_)throw std::runtime_error("DevTools input capture regression");
        SDL_Event click{};click.type=SDL_MOUSEBUTTONDOWN;click.button.button=SDL_BUTTON_LEFT;event(click);
        if(!commands_.empty())throw std::runtime_error("DevTools leaked gameplay command");
        key.type=SDL_KEYUP;key.key.keysym.sym=SDLK_F3;event(key);
        key.type=SDL_KEYDOWN;key.key.keysym.sym=SDLK_ESCAPE;event(key);
        if(devtools_.interactive()||!captured_)throw std::runtime_error("DevTools did not restore capture");
        SDL_SetWindowSize(window_,800,600);devtools_.draw(world_,renderer_,camera_,player_,{});
        SDL_SetWindowSize(window_,1280,720);devtools_.draw(world_,renderer_,camera_,player_,{});
        key.key.keysym.sym=SDLK_F3;event(key);key.type=SDL_KEYUP;event(key);
        if(devtools_.visible())throw std::runtime_error("DevTools HUD toggle regression");
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("DevTools resize/OpenGL state regression");
    }
    if(benchmark_.enabled){devtools_.setVisible(benchmark_.overlay);SDL_SetRelativeMouseMode(SDL_FALSE);TraceBuffer::instance().clear();TraceBuffer::instance().setEnabled(true);camera_.rotate(0,-7);}
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
    int smokeTraversal=0;
    double breakPublishMs=0;
    bool smokeAimed=false,outlineChecked=false;
    std::size_t outlinePixels=0;
    double maxFrameMs=0;
    std::uint64_t previous = SDL_GetPerformanceCounter();
    auto profileDir=benchmark_.enabled?benchmark_.output:root/"benchmark";std::filesystem::create_directories(profileDir);
    auto profileName=world_.settings().generatorVersion+"-"+world_.settings().preset+"-"+std::to_string(world_.settings().seed)+"-"+std::to_string(smokeChunk.x)+"-"+std::to_string(smokeChunk.z)+(requireLod_?"-lod-regression":"");
    std::ofstream frameCsv(profileDir/(profileName+"-runtime.csv"));
    frameCsv<<"frame,frame_ms,world_driver_ms,upload_submit_ms,render_submit_ms,gpu_terrain_ms,draws,near_ready,far_visible,generation_cpu_total_ms,meshing_cpu_total_ms,far_cpu_total_ms,gpu_pass_frame,gpu_opaque_ms,gpu_cutout_ms,gpu_translucent_ms,gpu_frame_id,gpu_frame_interval_ms,fluid_ms,ui_ms,generation_queue,meshing_queue,upload_staging,cpu_cache_bytes,cpu_reserved_bytes,gpu_mesh_bytes,journal_cell_bytes,effective_far_radius,swaps,stale_results,fluid_ticks,fluid_frontier,iron_blocks,gold_blocks,diamond_blocks,water_blocks,tree_chunk_incidences,village_chunk_incidences,working_set_bytes,private_commit_bytes,process_cpu_percent\n";
    std::deque<double> measuredFrames;std::uint64_t totalFrames=0;
    auto runStart=std::chrono::steady_clock::now();std::optional<std::chrono::steady_clock::time_point> benchmarkReady,benchmarkSample;
    std::optional<std::chrono::steady_clock::time_point> previousFrameStart;
    bool benchmarkFixture=false;std::vector<double> benchmarkFrames;
    std::uint64_t benchmarkSamples=0;double benchmarkTimeMs=0;
    double benchmarkX=8.5,benchmarkZ=8.5;
    if(benchmark_.enabled&&(benchmark_.scenario=="forest"||benchmark_.scenario=="village")){
        if(auto context=world_.generator().densityContext()){
            if(benchmark_.scenario=="village"){
                auto sites=context->villagesIn({-2048,2048,-2048,2048,4,247});
                if(sites.empty())throw std::runtime_error("no village fixture for selected seed/preset");
                benchmarkX=double(sites.front().x);benchmarkZ=double(sites.front().z);
            }else{
                auto trees=context->treesIn({-256,256,-256,256,4,247});
                if(trees.empty())throw std::runtime_error("no tree fixture for selected seed/preset");
                benchmarkX=double(trees.front().x);benchmarkZ=double(trees.front().z);
            }
        }
    }
    while (running_) {
        auto frameStart=std::chrono::steady_clock::now();
        double completeIntervalMs=previousFrameStart?std::chrono::duration<double,std::milli>(frameStart-*previousFrameStart).count():0;
        previousFrameStart=frameStart;
        renderer_.setFrameId(totalFrames);
        DevFrame devFrame;devFrame.startUs=std::chrono::duration<double,std::micro>(frameStart.time_since_epoch()).count();
        SDL_Event e;
        while (SDL_PollEvent(&e)) event(e);
        if(benchmark_.enabled){
            double time=benchmarkSample?std::chrono::duration<double>(std::chrono::steady_clock::now()-*benchmarkSample).count():0;
            double x=benchmarkX,z=benchmarkZ;
            if(benchmark_.scenario=="traversal"){x+=time*16;z+=std::sin(time*0.4)*32;}
            if(benchmark_.scenario=="reversal")x+=std::sin(time*0.7)*96;
            auto p=WorldPosition::fromAbsolute(x,benchmark_.scenario=="underwater"?213.4:128,z);player_.origin=p.chunk;player_.feet=p.local();player_.velocity={};spawned_=true;
            camera_.setOrigin(p.chunk);camera_.setPosition({float(p.x),160,float(p.z+64)});
            if(benchmark_.scenario=="underwater")camera_.setPosition({float(p.x),215,float(p.z)});
        }
        if(auto next=devtools_.takeTeleport()){
            player_.origin=next->chunk;player_.feet=next->local();player_.velocity={0,0,0};spawned_=false;commands_.clear();presentedTarget_={};
            camera_.setOrigin(player_.origin);camera_.setPosition(player_.eye());
        }
        const std::uint64_t now = SDL_GetPerformanceCounter();
        const double simulationDt = static_cast<double>(now - previous) / static_cast<double>(SDL_GetPerformanceFrequency());
        const float dt = static_cast<float>(std::min(0.1, simulationDt));
        previous = now;
        auto worldStart=std::chrono::steady_clock::now();
        devFrame.phaseStartUs[0]=std::chrono::duration<double,std::micro>(worldStart.time_since_epoch()).count();
        auto priorityDirection=camera_.forward();double horizontalSpeed=std::hypot(player_.velocity[0],player_.velocity[2]);
        if(horizontalSpeed>0.1){
            priorityDirection[0]=float(0.65*player_.velocity[0]/horizontalSpeed+0.35*priorityDirection[0]);
            priorityDirection[2]=float(0.65*player_.velocity[2]/horizontalSpeed+0.35*priorityDirection[2]);
            double length=std::hypot(priorityDirection[0],priorityDirection[2]);
            if(length>0){priorityDirection[0]=float(priorityDirection[0]/length);priorityDirection[2]=float(priorityDirection[2]/length);}
        }
        world_.updateAt(player_.worldEye(),priorityDirection);
        if(benchmark_.enabled&&benchmark_.scenario=="underwater"&&!benchmarkFixture&&world_.stats().ready>=289){
            for(int y=208;y<=220;++y)for(int z=3;z<=13;++z)for(int x=3;x<=13;++x){
                auto state=(y==208||y==220||x==3||x==13||z==3||z==13)?BlockState(BlockType::Glass):BlockState::water();
                world_.setState(x,y,z,state);
            }
            benchmarkFixture=true;
        }
        double worldMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-worldStart).count();
        renderer_.synchronize(world_.gpuCoords());
        const auto pendingUploads=world_.takePendingUploads();
        const auto uploadStart=std::chrono::steady_clock::now();
        devFrame.phaseStartUs[1]=std::chrono::duration<double,std::micro>(uploadStart.time_since_epoch()).count();
        for (const auto& upload : pendingUploads) {
            if(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-uploadStart).count()>=2.0)break;
            if (renderer_.upload(upload)) world_.markUploaded(upload);
        }
        for(const auto& group:world_.readyGroups())
            if(renderer_.publish(group)) world_.commitGroup(group);
        double uploadMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-uploadStart).count();
        world_.reportGpuPressure(renderer_.budgetPressure()?world_.resourceLimits().gpuBytes:renderer_.allocatedMeshBytes(),dt);
        if(!benchmark_.enabled)update(dt);
        auto fluidStart=std::chrono::steady_clock::now();
        devFrame.phaseStartUs[3]=std::chrono::duration<double,std::micro>(fluidStart.time_since_epoch()).count();
        world_.advanceSimulation(simulationDt);
        devFrame.fluidMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-fluidStart).count();
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
        if(smokeTest&&!renderer_.validatePublishedForTests(renderedChunks)){std::cerr<<"CPU/GPU mesh ownership mismatch during OpenGL regression"<<std::endl;break;}
        const auto farCount=std::count_if(renderedChunks.begin(),renderedChunks.end(),[](const RenderChunk& c){return c.mesh->lod!=0;});
        auto renderStart=std::chrono::steady_clock::now();
        devFrame.phaseStartUs[2]=std::chrono::duration<double,std::micro>(renderStart.time_since_epoch()).count();
        auto eyePosition=player_.worldEye();int eyeY=int(std::floor(eyePosition.y));auto eyeState=world_.getState(eyePosition.blockX(),eyeY,eyePosition.blockZ());
        bool underwater=eyeState==BlockType::Water&&eyePosition.y-eyeY<(world_.get(eyePosition.blockX(),eyeY+1,eyePosition.blockZ())==BlockType::Water?1:eyeState.waterHeight());
        renderer_.render(camera_,renderedChunks,width,height,underwater);
        double renderMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-renderStart).count();
        bool farReady=world_.expectedFarTiles()>0&&std::size_t(farCount)>=world_.expectedFarTiles()&&world_.heightmapSettled();
        if(smokeTest&&target.hit&&!outlineChecked&&world_.stats().ready>=289&&(!requireLod_||farReady)){
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
                auto diagnostics=root/"diagnostics"/(world_.settings().generatorVersion+"-"+world_.settings().preset+"-seed-"+std::to_string(world_.settings().seed)+"-"+std::to_string(smokeChunk.x)+"-"+std::to_string(smokeChunk.z));
                std::filesystem::create_directories(diagnostics);
                TextureGenerator::writePng(diagnostics/filename,image);
            };
            renderer_.renderTarget(camera_,target,width,height);
            saveImage("textures_near.png");
            Camera far=camera_;far.rotate(0,28);
            auto overview=camera_.position();overview[1]+=90;overview[2]+=100;far.setPosition(overview);
            renderer_.render(far,renderedChunks,width,height);
            renderer_.renderTarget(far,{},width,height);
            saveImage("textures_far.png");
            if(requireLod_)saveImage("terrain_lod_overview.png");
            renderer_.render(camera_,renderedChunks,width,height);
        }
        if(spawned_&&captured_)renderer_.renderTarget(camera_,target,width,height);
        presentedTarget_=target;
        if (smokeTest) {
            if(smokeFrames%300==0){auto s=world_.stats();std::cout<<"smoke_frame="<<smokeFrames<<" ready="<<s.ready<<" near_upload_pending="<<s.pendingUploads
                <<" far_upload_pending="<<s.farPendingUploads<<" far_group_ready="<<s.farGroupReady<<" groups="<<s.publicationGroups<<" far_jobs="<<s.heightmapJobs<<" far_stale="<<s.heightmapStale<<std::endl;}
            if (spawned_ && renderer_.drawCalls() > 0) {
                std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 3));
                glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
                std::size_t terrainPixels = 0;
                for (std::size_t p = 0; p < pixels.size(); p += 3)
                    if(std::abs(int(pixels[p])-122)>3||std::abs(int(pixels[p+1])-186)>3||std::abs(int(pixels[p+2])-235)>3)++terrainPixels;
                if(smokeEditing&&terrainPixels<=1000){std::cerr<<"Terrain vanished during render regression"<<std::endl;break;}
                if(requireLod_&&!smokeEditing&&outlineChecked&&smokeTraversal<6&&farReady&&world_.stats().ready==289){
                    const int path[]{16,32,16,0,-16,0};
                    auto next=WorldPosition::fromAbsolute(double(smokeChunk.x*16+path[smokeTraversal])+8.5,128,double(smokeChunk.z*16)+8.5);
                    player_.origin=next.chunk;player_.feet=next.local();player_.velocity={0,0,0};spawned_=false;++smokeTraversal;
                }
                if(!smokeEditing&&spawned_&&(!requireLod_||(smokeTraversal==6&&world_.heightmapSettled()))&&outlineChecked&&terrainPixels>1000&&world_.targetable(smokeX,smokeZ)&&world_.targetable(smokeX+1,smokeZ)) {
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
                    std::cout<<"heightmap_tiles="<<farCount<<" gpu_terrain_ms="<<renderer_.gpuTerrainMs()<<" lod_reversals="<<smokeTraversal<<std::endl;
                    running_ = false;
                    }
                }
            }
#ifdef NDEBUG
            if (++smokeFrames > 20000) running_ = false;
#else
            if (++smokeFrames > 30000) running_ = false;
#endif
            SDL_Delay(1);
        }
        auto uiStart=std::chrono::steady_clock::now();devFrame.phaseStartUs[4]=std::chrono::duration<double,std::micro>(uiStart.time_since_epoch()).count();
        devtools_.draw(world_,renderer_,camera_,player_,target);
        renderer_.finishFrame();
        devFrame.uiMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-uiStart).count();
        if(devtools_.takeScreenshot()&&width>0&&height>0&&width<=8192&&height<=8192&&std::size_t(width)*std::size_t(height)*4<=32*MiB){
            std::size_t pixelBytes=std::size_t(width)*std::size_t(height)*4;
            TextureImage shot;shot.width=unsigned(width);shot.height=unsigned(height);shot.rgba=std::vector<unsigned char>(pixelBytes);
            std::vector<unsigned char> raw(shot.rgba.size());glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());
            for(int y=0;y<height;++y)std::copy_n(raw.begin()+std::size_t(height-1-y)*width*4,width*4,shot.rgba.begin()+std::size_t(y)*width*4);
            auto output=root/"benchmark/devtools";std::filesystem::create_directories(output);
            TextureGenerator::writePng(output/("shot-"+std::to_string(totalFrames)+".png"),shot);
        }
        SDL_GL_SwapWindow(window_);
        double measuredMs=completeIntervalMs>0?completeIntervalMs:std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frameStart).count();
        devFrame.frameMs=measuredMs;devFrame.worldMs=worldMs;devFrame.uploadMs=uploadMs;devFrame.renderMs=renderMs;
        devFrame.gpuFrame=renderer_.gpuTimingFrame();devFrame.gpuPassMs={renderer_.gpuOpaqueMs(),renderer_.gpuCutoutMs(),renderer_.gpuTranslucentMs()};devtools_.record(devFrame);
        if(benchmark_.enabled){
            auto tick=std::chrono::steady_clock::now();
            if(!benchmarkReady&&(benchmark_.scenario=="cold"||(world_.stats().ready>=289&&world_.heightmapSettled())))benchmarkReady=tick;
            if(benchmarkReady&&!benchmarkSample&&std::chrono::duration<double>(tick-*benchmarkReady).count()>=benchmark_.warmup)benchmarkSample=tick;
            if(benchmarkSample){
                ++benchmarkSamples;benchmarkTimeMs+=measuredMs;
                if(benchmarkFrames.size()<1000000)benchmarkFrames.push_back(measuredMs);
                double time=std::chrono::duration<double>(tick-*benchmarkSample).count();
                if(benchmark_.scenario=="fluid"&&!benchmarkFixture){
                    for(int z=3;z<=12;++z)for(int x=3;x<=12;++x)world_.set(x,96,z,BlockType::Stone);
                    world_.setState(5,102,6,BlockState::water());world_.setState(7,102,6,BlockState::water());benchmarkFixture=true;
                }
                if(benchmark_.scenario=="edit"&&totalFrames%20==0){
                    int y=world_.surfaceHeight(8,8);auto block=world_.get(8,y,8);world_.set(8,y,8,block==BlockType::Air?BlockType::Grass:BlockType::Air);
                }
                if(benchmark_.scenario=="budget"){
                    auto limits=world_.resourceLimits();bool pressure=time<benchmark_.duration/2;
                    limits.cpuBytes=(pressure?192:512)*MiB;limits.gpuBytes=(pressure?32:128)*MiB;limits.requestedFarRadius=24;
                    world_.setResourceLimits(limits);renderer_.setMeshBudget(limits.gpuBytes);
                }
                if(time>=benchmark_.duration){
                    devtools_.exportCapture(world_,renderer_);
                    if(width>0&&height>0&&width<=8192&&height<=8192&&std::size_t(width)*std::size_t(height)<=8*MiB){
                        const std::size_t bytes=std::size_t(width)*std::size_t(height)*4;
                        TextureImage shot;shot.width=width;shot.height=height;shot.rgba=std::vector<unsigned char>(bytes);
                        std::vector<unsigned char> raw(shot.rgba.size());glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());
                        for(int y=0;y<height;++y)std::copy_n(raw.begin()+std::size_t(height-1-y)*width*4,width*4,shot.rgba.begin()+std::size_t(y)*width*4);
                        TextureGenerator::writePng(profileDir/"screenshot.png",shot);
                    }
                    running_=false;
                }
            }
            if(std::chrono::duration<double>(tick-runStart).count()>180+benchmark_.duration){std::cerr<<"benchmark warmup timeout"<<std::endl;running_=false;}
        }
        measuredFrames.push_back(measuredMs);
        if(measuredFrames.size()>32768)measuredFrames.pop_front();
        ++totalFrames;
        auto metrics=world_.stats();
        auto fluidMetrics=world_.fluidStats();auto hardware=devtools_.hardware();
        frameCsv<<totalFrames<<','<<measuredMs<<','<<worldMs<<','<<uploadMs<<','<<renderMs<<','<<renderer_.gpuTerrainMs()<<','<<renderer_.drawCalls()<<','<<metrics.ready<<','<<farCount<<','<<metrics.generationCpuMs<<','<<metrics.meshingCpuMs<<','<<metrics.heightmapCpuMs
            <<','<<renderer_.gpuTimingFrame()<<','<<renderer_.gpuOpaqueMs()<<','<<renderer_.gpuCutoutMs()<<','<<renderer_.gpuTranslucentMs()<<','<<renderer_.gpuFrameTimingId()<<','<<renderer_.gpuFrameMs()
            <<','<<devFrame.fluidMs<<','<<devFrame.uiMs<<','<<metrics.generationQueue<<','<<metrics.meshingQueue<<','<<metrics.uploadStaging<<','<<metrics.cpuCacheBytes<<','<<metrics.cpuReservations<<','<<renderer_.allocatedMeshBytes()
            <<','<<metrics.journalPayloadBytes<<','<<world_.effectiveFarRadius()<<','<<metrics.swaps<<','<<metrics.staleResults<<','<<fluidMetrics.ticks<<','<<fluidMetrics.frontier
            <<','<<metrics.blockCounts[unsigned(BlockType::IronOre)]<<','<<metrics.blockCounts[unsigned(BlockType::GoldOre)]<<','<<metrics.blockCounts[unsigned(BlockType::DiamondOre)]<<','<<metrics.blockCounts[unsigned(BlockType::Water)]
            <<','<<metrics.treeIncidences<<','<<metrics.villageIncidences<<','<<hardware.workingSet<<','<<hardware.privateCommit<<','<<hardware.processCpuPercent<<'\n';
        if(smokeTest)maxFrameMs=std::max(maxFrameMs,static_cast<double>(SDL_GetPerformanceCounter()-now)*1000.0/SDL_GetPerformanceFrequency());
    }
    if(benchmark_.enabled){
        world_.settings().save(profileDir/"world-config.meta");
        std::sort(benchmarkFrames.begin(),benchmarkFrames.end());auto hw=devtools_.hardware();auto stats=world_.stats();
        auto percentile=[&](unsigned p){return benchmarkFrames.empty()?0:benchmarkFrames[(benchmarkFrames.size()-1)*p/100];};
        std::ofstream report(profileDir/"runtime-summary.json");report<<std::setprecision(17)
            <<"{\"scenario\":\""<<benchmark_.scenario<<"\",\"seed\":\""<<world_.settings().seed<<"\",\"generator\":\""<<world_.settings().generatorVersion
            <<"\",\"noise_backend\":\""<<noiseBackendName(benchmark_.backend)<<"\",\"frames\":"<<benchmarkSamples<<",\"retained_frames\":"<<benchmarkFrames.size()<<",\"frame_samples_dropped\":"<<(benchmarkSamples-benchmarkFrames.size())<<",\"fps\":"<<(benchmarkTimeMs>0?benchmarkSamples*1000/benchmarkTimeMs:0)<<",\"p50_ms\":"<<percentile(50)<<",\"p95_ms\":"<<percentile(95)
            <<",\"p99_ms\":"<<percentile(99)<<",\"gpu_terrain_latest_ms\":"<<renderer_.gpuTerrainMs()<<",\"mesh_bytes\":"<<renderer_.allocatedMeshBytes()
            <<",\"gpu_timing_frame\":"<<renderer_.gpuTimingFrame()<<",\"gpu_opaque_ms\":"<<renderer_.gpuOpaqueMs()<<",\"gpu_cutout_ms\":"<<renderer_.gpuCutoutMs()<<",\"gpu_translucent_ms\":"<<renderer_.gpuTranslucentMs()
            <<",\"gpu_frame_command_interval_ms\":"<<renderer_.gpuFrameMs()<<",\"gpu_frame_timing_id\":"<<renderer_.gpuFrameTimingId()
            <<",\"cpu_payload_bytes\":"<<stats.cpuCacheBytes<<",\"working_set_bytes\":"<<hw.workingSet<<",\"private_commit_bytes\":"<<hw.privateCommit<<",\"effective_far_radius\":"<<world_.effectiveFarRadius()
            <<",\"gpu_engine_available\":"<<(hw.gpuEngineAvailable?"true":"false")<<",\"gpu_memory_available\":"<<(hw.gpuMemoryAvailable?"true":"false")
            <<",\"cpu\":"<<std::quoted(hw.cpuName)<<",\"logical_cpus\":"<<hw.logicalCpus<<",\"physical_ram_bytes\":"<<hw.physicalTotal
            <<",\"gl_vendor\":"<<std::quoted(hw.glVendor)<<",\"gl_renderer\":"<<std::quoted(hw.glRenderer)<<",\"gl_version\":"<<std::quoted(hw.glVersion)
            <<",\"process_cpu_percent_latest\":"<<hw.processCpuPercent<<",\"system_cpu_percent_latest\":"<<hw.systemCpuPercent
            <<",\"process_cpu_available\":"<<(hw.processCpuAvailable?"true":"false")<<",\"system_cpu_available\":"<<(hw.systemCpuAvailable?"true":"false")<<",\"ram_available\":"<<(hw.ramAvailable?"true":"false")
            <<",\"driver_gpu_local_usage_bytes\":"<<hw.gpuLocalUsage<<",\"gpu_engine_source\":"<<std::quoted(hw.gpuEngineSource)<<"}\n";
    }
    if(!measuredFrames.empty()){
        std::sort(measuredFrames.begin(),measuredFrames.end());
        std::ofstream summary(profileDir/(profileName+"-summary.json"));
        summary<<"{\"diagnostic_run\": "<<(smokeTest?"true":"false")<<", \"frames\": "<<measuredFrames.size()
            <<", \"p50_ms\": "<<measuredFrames[(measuredFrames.size()-1)*50/100]<<", \"p95_ms\": "<<measuredFrames[(measuredFrames.size()-1)*95/100]
            <<", \"p99_ms\": "<<measuredFrames[(measuredFrames.size()-1)*99/100]<<", \"gpu_terrain_last_ms\": "<<renderer_.gpuTerrainMs()<<"}\n";
    }
    SDL_SetRelativeMouseMode(SDL_FALSE);
    devtools_.shutdown();
    renderer_.shutdown();
    SDL_GL_DeleteContext(context_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
    return (smokeTest&&!smokePassed)||(benchmark_.enabled&&benchmarkFrames.empty())?2:0;
}

void Game::event(const SDL_Event& e) {
    devtools_.processEvent(e);
    if (e.type == SDL_QUIT) running_ = false;
    if(e.type==SDL_KEYUP&&e.key.keysym.sym==SDLK_F3){f3Held_=false;return;}
    if(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_F3&&!e.key.repeat){
        f3Held_=true;
        bool wasInteractive=devtools_.interactive();
        hudEnabled_=!hudEnabled_;devtools_.setVisible(hudEnabled_);
        if(!hudEnabled_&&wasInteractive){captured_=captureBeforeTools_;SDL_SetRelativeMouseMode(captured_?SDL_TRUE:SDL_FALSE);}
        return;
    }
    if(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_TAB&&!e.key.repeat&&f3Held_){
        bool next=!devtools_.interactive();if(next)captureBeforeTools_=captured_;
        devtools_.setInteractive(next);hudEnabled_=devtools_.visible();captured_=next?false:captureBeforeTools_;SDL_SetRelativeMouseMode(captured_?SDL_TRUE:SDL_FALSE);commands_.clear();return;
    }
    if(devtools_.interactive()){
        if(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_ESCAPE&&!e.key.repeat){devtools_.setInteractive(false);captured_=captureBeforeTools_;SDL_SetRelativeMouseMode(captured_?SDL_TRUE:SDL_FALSE);}
        return;
    }
    if(e.type==SDL_KEYDOWN&&e.key.keysym.sym>=SDLK_1&&e.key.keysym.sym<=SDLK_9&&!e.key.repeat)devtools_.selectSlot(e.key.keysym.sym-SDLK_1);
    if(e.type==SDL_MOUSEWHEEL&&captured_)devtools_.scroll(e.wheel.y);
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_SPACE && e.key.repeat == 0) jumpRequested_ = true;
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
        auto original=player_;
        for(int radius=0;radius<=16&&!spawned_;++radius)
          for(int dz=-radius;dz<=radius&&!spawned_;++dz)
            for(int dx=-radius;dx<=radius&&!spawned_;++dx){
                if(std::max(std::abs(dx),std::abs(dz))!=radius)continue;
                auto wx=x+dx,wz=z+dz;
                if(wx<-WorldLimit||wx>=WorldLimit||wz<-WorldLimit||wz>=WorldLimit||!world_.columnLoaded(wx,wz))continue;
                for (int y = ChunkData::Height - 3; y >= 0; --y) {
                    if(!isSolid(world_.get(wx,y,wz))||world_.get(wx,y+1,wz)==BlockType::Water||world_.get(wx,y+2,wz)==BlockType::Water)continue;
                    auto p=WorldPosition::fromAbsolute(double(wx)+0.5,y+1,double(wz)+0.5);
                    player_.origin=p.chunk;player_.feet=p.local();
                    if(!player_.clear([this](std::int64_t x,int y,std::int64_t z){return !world_.columnLoaded(x,z)||isSolid(world_.get(x,y,z));}))continue;
                    if(player_.submergedFraction([this](std::int64_t x,int y,std::int64_t z){return world_.getState(x,y,z);})>0)continue;
                    player_.grounded=true;player_.velocity={};spawned_=true;break;
                }
            }
        if(!spawned_){
            player_=original;
            for(int y=ChunkData::Height-3;y>0;--y)if(world_.get(x,y,z)==BlockType::Water&&world_.get(x,y+1,z)==BlockType::Air){
                player_.feet[1]=y+world_.getState(x,y,z).waterHeight()-0.4;
                if(player_.clear([this](std::int64_t x,int y,std::int64_t z){return !world_.columnLoaded(x,z)||isSolid(world_.get(x,y,z));})){
                    player_.grounded=false;player_.velocity={};spawned_=true;break;
                }
            }
            if(!spawned_){player_=original;return;}
        }
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
    bool jumping=captured_&&(jumpRequested_||(player_.submerged>0.05&&keys[SDL_SCANCODE_SPACE]));
    player_.step(dt, move[0], move[2], jumping, [this](std::int64_t x,int y,std::int64_t z) {
        // Unknown chunks are barriers until their collision data arrives.
        return !world_.columnLoaded(x, z) || isSolid(world_.get(x, y, z));
    },[this](std::int64_t x,int y,std::int64_t z){return world_.get(x,y,z);},
      [this](std::int64_t x,int y,std::int64_t z){return world_.getState(x,y,z);},captured_&&keys[SDL_SCANCODE_LCTRL]);
    jumpRequested_ = false;
    camera_.setPosition(player_.eye());
    camera_.setOrigin(player_.origin);
    if (hudEnabled_) {
        const auto stats = world_.stats();
        std::ostringstream title;
        title << "Bloxide DevTools | loaded " << stats.loaded << " draws " << renderer_.drawCalls()
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
    const Int3 target=place&&!isReplaceable(hit.type)?Int3{hit.block.x+hit.normal.x,hit.block.y+hit.normal.y,hit.block.z+hit.normal.z}:hit.block;
    if(place&&isSolid(devtools_.selectedBlock())&&player_.overlapsBlock(target.x,target.y,target.z))return;
    if(place&&(!world_.targetable(target.x,target.z)||!isReplaceable(world_.get(target.x,target.y,target.z))||target.y<=0||target.y>=256))return;
    world_.set(target.x,target.y,target.z,place?devtools_.selectedBlock():BlockType::Air);
}
} // namespace bloxide

int main(int argc,char** argv){
    try{
        bool smoke=false,lod=true,requireLod=false;std::optional<std::uint64_t> seed;std::filesystem::path worldDir;
        bloxide::BenchmarkRun benchmark;
        std::optional<std::string> preset;std::optional<bool> caves;
        std::int64_t offset=0;
        for(int i=1;i<argc;++i){
            std::string arg=argv[i];
            if(arg=="--help"){std::cout<<"Bloxide [--world folder] [--seed uint64-decimal] [--preset flat|rolling-hills|mountainous] [--caves on|off] [--no-lod]\nDefault world: SDL user data directory/worlds/default\nOnly seed/config metadata is persisted; block edits remain in RAM.\n";return 0;}
            if(arg=="--smoke-test")smoke=true;
            else if(arg=="--seed"&&i+1<argc)seed=bloxide::WorldSettings::parseSeed(argv[++i]);
            else if(arg=="--world"&&i+1<argc)worldDir=argv[++i];
            else if(arg=="--preset"&&i+1<argc)preset=argv[++i];
            else if(arg=="--caves"&&i+1<argc){std::string v=argv[++i];if(v!="on"&&v!="off")throw std::invalid_argument("--caves requires on|off");caves=v=="on";}
            else if(arg=="--no-lod")lod=false;
            else if(arg=="--far-radius"&&i+1<argc){auto value=std::string(argv[++i]);std::size_t used=0;benchmark.farRadius=std::stoi(value,&used);
                if(used!=value.size()||benchmark.farRadius<8||benchmark.farRadius>24)throw std::invalid_argument("far-radius 8..24");}
            else if(arg=="--require-lod")requireLod=true;
            else if(arg=="--benchmark-run")benchmark.enabled=true;
            else if((arg=="--warmup"||arg=="--duration")&&i+1<argc){
                std::string value=argv[++i];std::size_t used=0;double n=std::stod(value,&used);
                if(used!=value.size()||!std::isfinite(n)||n<0||n>600||(arg=="--duration"&&n<=0))throw std::invalid_argument("invalid benchmark time");
                if(arg=="--warmup")benchmark.warmup=n;else benchmark.duration=n;
            }
            else if(arg=="--scenario"&&i+1<argc){benchmark.scenario=argv[++i];
                if(benchmark.scenario!="steady"&&benchmark.scenario!="traversal"&&benchmark.scenario!="reversal"&&benchmark.scenario!="fluid"&&benchmark.scenario!="edit"&&benchmark.scenario!="budget"&&benchmark.scenario!="cold"&&benchmark.scenario!="forest"&&benchmark.scenario!="village"&&benchmark.scenario!="underwater")throw std::invalid_argument("invalid benchmark scenario");}
            else if(arg=="--benchmark-output"&&i+1<argc)benchmark.output=argv[++i];
            else if(arg=="--benchmark-devtools"&&i+1<argc){std::string v=argv[++i];if(v!="on"&&v!="off")throw std::invalid_argument("benchmark-devtools on|off");benchmark.overlay=v=="on";}
            else if(arg=="--noise-backend"&&i+1<argc){std::string v=argv[++i];
                if(v=="scalar")benchmark.backend=bloxide::NoiseBackend::Scalar;else if(v=="sse2")benchmark.backend=bloxide::NoiseBackend::SSE2;
                else if(v=="avx2")benchmark.backend=bloxide::NoiseBackend::AVX2;else if(v=="auto")benchmark.backend=bloxide::NoiseBackend::Auto;else throw std::invalid_argument("invalid noise backend");}
            else if(arg=="--smoke-origin"&&i+1<argc){std::string v=argv[++i];std::size_t used=0;offset=std::stoll(v,&used);if(used!=v.size())throw std::invalid_argument("invalid smoke origin");}
            else throw std::invalid_argument("usage: Bloxide [--world folder] [--seed uint64] [--smoke-test]");
        }
        if(offset&&!smoke)throw std::invalid_argument("--smoke-origin is test-only");
        if(benchmark.enabled&&smoke)throw std::invalid_argument("benchmark and smoke modes are separate");
        if(!benchmark.enabled&&benchmark.backend!=bloxide::NoiseBackend::Auto)throw std::invalid_argument("explicit experimental noise backend is benchmark-only");
        if(benchmark.enabled){
            char* base=SDL_GetBasePath();if(!base)throw std::runtime_error(SDL_GetError());
            auto root=std::filesystem::path(base);SDL_free(base);auto id=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            if(benchmark.output.empty())benchmark.output=root/"benchmark"/("runtime-"+id);
            if(worldDir.empty())worldDir=root/"testing"/("benchmark-world-"+id);
            if(!seed)seed=130;
        }
        if(requireLod&&(!smoke||!lod))throw std::invalid_argument("--require-lod requires --smoke-test and enabled LOD");
        if(worldDir.empty()){
            char* path=smoke?SDL_GetBasePath():SDL_GetPrefPath("Bloxide","Bloxide");
            if(!path)throw std::runtime_error(SDL_GetError());
            worldDir=std::filesystem::path(path)/(smoke?"testing/smoke-world-"+preset.value_or("legacy")+"-"+std::to_string(offset)+"-"+std::to_string(seed.value_or(130)):"worlds/default");SDL_free(path);
            if(smoke&&!seed)seed=130;
        }
        auto settings=bloxide::WorldSettings::open(worldDir,seed,preset,caves);
        std::cout<<"world_seed="<<settings.seed<<" generator="<<settings.generatorVersion<<" metadata="<<(worldDir/"world.meta").string()<<std::endl;
        bloxide::Game game(settings,offset,lod,requireLod,benchmark);return game.run(smoke);
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}

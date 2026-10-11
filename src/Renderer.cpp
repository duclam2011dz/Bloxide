#include <bloxide/Renderer.hpp>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <bloxide/Textures.hpp>
#include <chrono>
namespace bloxide {
namespace {
std::string readFile(const std::string& path){std::ifstream f(path);std::stringstream s;s<<f.rdbuf();return s.str();}
GLuint shader(GLenum type,const std::string& source) {
    GLuint id=glCreateShader(type);const char* s=source.c_str();glShaderSource(id,1,&s,nullptr);glCompileShader(id);
    GLint ok=0;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);if(!ok){glDeleteShader(id);return 0;}return id;
}
GLuint program(const std::string& vs,const std::string& fs) {
    GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs);
    if(!v||!f){if(v)glDeleteShader(v);if(f)glDeleteShader(f);return 0;}
    GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){glDeleteProgram(p);return 0;}return p;
}
using Clip=std::array<float,4>;
Clip transform(const std::array<float,16>& m,const Clip& v) {
    Clip out{};for(int r=0;r<4;++r)for(int c=0;c<4;++c)out[r]+=m[c*4+r]*v[c];return out;
}
void ribbon(std::vector<Clip>& verts,Clip a,Clip b,int width,int height,float pixels) {
    // Clip segment against the near plane before perspective division.
    float da=a[2]+a[3],db=b[2]+b[3];
    if(da<0&&db<0)return;
    if(da<0||db<0) {
        float t=da/(da-db);Clip cut{};
        for(int i=0;i<4;++i)cut[i]=a[i]+(b[i]-a[i])*t;
        if(da<0)a=cut;else b=cut;
    }
    if(a[3]<=0.000001f||b[3]<=0.000001f)return;
    float dx=(b[0]/b[3]-a[0]/a[3])*width,dy=(b[1]/b[3]-a[1]/a[3])*height;
    float len=std::sqrt(dx*dx+dy*dy);if(len<0.00001f)return;
    float ox=-dy/len*pixels/width,oy=dx/len*pixels/height;
    Clip al=a,ar=a,bl=b,br=b;
    al[0]+=ox*a[3];al[1]+=oy*a[3];ar[0]-=ox*a[3];ar[1]-=oy*a[3];
    bl[0]+=ox*b[3];bl[1]+=oy*b[3];br[0]-=ox*b[3];br[1]-=oy*b[3];
    for(Clip* p:{&al,&ar,&bl,&br})(*p)[2]-=0.00002f*(*p)[3];
    verts.insert(verts.end(),{al,ar,bl,ar,br,bl});
}
}
Renderer::~Renderer(){shutdown();}
void Renderer::assertThread()const {if(owner_!=std::this_thread::get_id())throw std::logic_error("OpenGL must run on render thread");}
void Renderer::destroy(GpuMesh& m){if(m.ebo)glDeleteBuffers(1,&m.ebo);if(m.vbo)glDeleteBuffers(1,&m.vbo);if(m.vao)glDeleteVertexArrays(1,&m.vao);m={};}
void Renderer::retire(GpuMesh m){if(m.vao)retired_.push_back({m,glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0)});}
void Renderer::collectRetired() {
    for(auto it=retired_.begin();it!=retired_.end();) {
        GLenum status=it->fence?glClientWaitSync(it->fence,0,0):GL_ALREADY_SIGNALED;
        if(status==GL_ALREADY_SIGNALED||status==GL_CONDITION_SATISFIED){
            if(it->fence)glDeleteSync(it->fence);
            destroy(it->mesh);it=retired_.erase(it);
        }else ++it;
    }
}
void Renderer::shutdown() {
    if(!program_&&!overlayProgram_&&!textures_&&meshes_.empty()&&pending_.empty())return;
    assertThread();
    for(auto&[c,m]:meshes_)destroy(m);
    meshes_.clear();
    for(auto&[c,m]:pending_)destroy(m);
    pending_.clear();
    for(auto&r:retired_){if(r.fence)glDeleteSync(r.fence);destroy(r.mesh);}retired_.clear();
    if(overlayVbo_)glDeleteBuffers(1,&overlayVbo_);
    if(overlayVao_)glDeleteVertexArrays(1,&overlayVao_);
    if(program_)glDeleteProgram(program_);
    if(overlayProgram_)glDeleteProgram(overlayProgram_);
    if(textures_)glDeleteTextures(1,&textures_);
    textures_=0;
    overlayVbo_=overlayVao_=program_=overlayProgram_=0;
}
bool Renderer::initialize(const std::string& vp,const std::string& fp,const std::string& assetDir) {
    owner_=std::this_thread::get_id();
    TextureAssets assets;
    try{assets=AssetLoader::load(assetDir);}
    catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return false;}
    assetsDirectory_=assetDir;textureSeed_=assets.seed;textureLoadMs_=assets.loadMs;
    GLint maxLayers=0;glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS,&maxLayers);
    if(maxLayers<20){std::cerr<<"OpenGL requires at least 20 array layers"<<std::endl;return false;}
    auto start=std::chrono::steady_clock::now();
    glGenTextures(1,&textures_);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D_ARRAY,textures_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_RGBA8,32,32,20,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    for(int layer=0;layer<20;++layer)
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY,0,0,0,layer,32,32,1,GL_RGBA,GL_UNSIGNED_BYTE,assets.layers[layer].rgba.data());
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    textureUploadMs_=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    if(glGetError()!=GL_NO_ERROR){std::cerr<<"Texture Array upload failed"<<std::endl;return false;}
    program_=program(readFile(vp),readFile(fp));if(!program_)return false;
    overlayProgram_=program("#version 330 core\nlayout(location=0) in vec4 clip;void main(){gl_Position=clip;}\n",
                            "#version 330 core\nuniform vec3 color;out vec4 frag;void main(){frag=vec4(color,1);}\n");
    if(!overlayProgram_)return false;
    glGenVertexArrays(1,&overlayVao_);glGenBuffers(1,&overlayVbo_);
    glBindVertexArray(overlayVao_);glBindBuffer(GL_ARRAY_BUFFER,overlayVbo_);
    glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,sizeof(Clip),nullptr);glEnableVertexAttribArray(0);glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);glEnable(GL_CULL_FACE);glCullFace(GL_BACK);return true;
}
bool Renderer::validateTexturesForTests() const {
    assertThread();glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D_ARRAY,textures_);
    for(int level=0;level<6;++level){
        GLint w=0,h=0,d=0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY,level,GL_TEXTURE_WIDTH,&w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY,level,GL_TEXTURE_HEIGHT,&h);
        glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY,level,GL_TEXTURE_DEPTH,&d);
        if(w!=(32>>level)||h!=w||d!=20)return false;
    }
    auto assets=AssetLoader::load(assetsDirectory_);
    std::vector<unsigned char> pixels(20*4096);
    glGetTexImage(GL_TEXTURE_2D_ARRAY,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    for(int layer=0;layer<20;++layer)
        if(!std::equal(assets.layers[layer].rgba.begin(),assets.layers[layer].rgba.end(),pixels.begin()+layer*4096))return false;
    // Rasterize large quads at one texel per pixel. Compare shader repetition,
    // face orientation and signed-coordinate variant hashing to source PNGs.
    GpuMesh fixture;
    glGenVertexArrays(1,&fixture.vao);glGenBuffers(1,&fixture.vbo);glGenBuffers(1,&fixture.ebo);
    glBindVertexArray(fixture.vao);glBindBuffer(GL_ARRAY_BUFFER,fixture.vbo);
    glVertexAttribIPointer(0,1,GL_UNSIGNED_INT,sizeof(ChunkMeshVertex),nullptr);glEnableVertexAttribArray(0);
    std::uint32_t indices[]{0,1,2,0,2,3};
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,fixture.ebo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    glViewport(0,0,256,128);glDisable(GL_CULL_FACE);glUseProgram(program_);
    const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    glUniformMatrix4fv(glGetUniformLocation(program_,"uProjection"),1,GL_FALSE,identity.data());
    glUniform1i(glGetUniformLocation(program_,"uBlockTextures"),0);glUniform1ui(glGetUniformLocation(program_,"uTextureSeed"),textureSeed_);
    bool correct=true;
    bool reported=false;
    std::vector<unsigned char> rendered(256*128*4);
    for(std::int64_t offset:std::array<std::int64_t,4>{0,-16,WorldLimit-32,-WorldLimit+32})for(int face=0;face<6;++face)for(unsigned base:{0U,4U,8U,12U,16U})for(unsigned ao:{0U,3U}){
        std::array<std::array<int,3>,4> positions{};
        std::array<float,16> view{};
        if(face<2){
            positions={{{0,0,0},{0,0,8},{0,4,8},{0,4,0}}};
            view={0,0,0,0,0,0.5f,0,0,0.25f,0,0,0,-1,-1,0,1};
        }else if(face<4){
            positions={{{0,0,0},{8,0,0},{8,0,4},{0,0,4}}};
            view={0.25f,0,0,0,0,0,0,0,0,-0.5f,0,0,-1,1,0,1};
        }else{
            positions={{{0,0,0},{8,0,0},{8,4,0},{0,4,0}}};
            view={0.25f,0,0,0,0,0.5f,0,0,0,0,0,0,-1,-1,0,1};
        }
        std::array<ChunkMeshVertex,4> vertices{};
        for(int i=0;i<4;++i)vertices[i].packed=positions[i][0]|(positions[i][1]<<5)|(positions[i][2]<<14)|(base<<19)|(unsigned(face)<<24)|(ao<<27);
        glBindBuffer(GL_ARRAY_BUFFER,fixture.vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices.data(),GL_STATIC_DRAW);
        glUniformMatrix4fv(glGetUniformLocation(program_,"uView"),1,GL_FALSE,view.data());
        glUniform3f(glGetUniformLocation(program_,"uChunkOrigin"),0,0,0);
        auto bits=static_cast<std::uint64_t>(offset);
        glUniform2ui(glGetUniformLocation(program_,"uChunkBlockX"),static_cast<GLuint>(bits),static_cast<GLuint>(bits>>32));
        glUniform2ui(glGetUniformLocation(program_,"uChunkBlockZ"),static_cast<GLuint>(bits),static_cast<GLuint>(bits>>32));
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,nullptr);
        glReadPixels(0,0,256,128,GL_RGBA,GL_UNSIGNED_BYTE,rendered.data());
        for(int py=7;py<128;py+=13)for(int px=7;px<256;px+=13){
            std::int64_t x=0,z=0;int y=0,tx=px%32,ty=31-py%32;
            if(face<2){x=offset-(face==1);y=py/32;z=offset+px/32;if(face==1)tx=31-tx;}
            else if(face<4){x=offset+px/32;y=face==2?-1:0;z=offset+3-py/32;if(face==3)ty=py%32;}
            else {x=offset+px/32;y=py/32;z=offset-(face==5);if(face==4)tx=31-tx;}
            auto layer=base+textureVariant(x,y,z,base,textureSeed_);
            auto source=(ty*32+tx)*4,target=(py*256+px)*4;
            for(int c=0;c<3;++c){
                int expected=int(std::round(assets.layers[layer].rgba[source+c]*(1.0f-ao*0.18f)));
                if(std::abs(int(rendered[target+c])-expected)>2){
                    correct=false;
                    if(!reported){std::cerr<<"sampling face="<<face<<" base="<<base<<" ao="<<ao<<" offset="<<offset<<" pixel="<<px<<','<<py<<" texel="<<tx<<','<<ty<<" layer="<<layer<<" actual="<<int(rendered[target+c])<<" expected="<<expected<<std::endl;reported=true;}
                }
            }
        }
    }
    glEnable(GL_CULL_FACE);glBindVertexArray(0);
    if(fixture.ebo)glDeleteBuffers(1,&fixture.ebo);
    if(fixture.vbo)glDeleteBuffers(1,&fixture.vbo);
    if(fixture.vao)glDeleteVertexArrays(1,&fixture.vao);
    if(!correct){std::cerr<<"Texture UV/layer/AO sampling regression failed"<<std::endl;return false;}
    return glGetError()==GL_NO_ERROR;
}
bool Renderer::upload(const ChunkUpload& u) {
    assertThread();if(!u.mesh)return false;
    if(failNextUpload_&&u.coord==failCoord_){failNextUpload_=false;return false;}
    auto existing=pending_.find(u.coord);
    if(existing!=pending_.end()&&existing->second.instanceId==u.mesh->instanceId&&existing->second.revision==u.mesh->revision)return true;
    while(glGetError()!=GL_NO_ERROR){}
    GpuMesh m;
    glGenVertexArrays(1,&m.vao);glGenBuffers(1,&m.vbo);glGenBuffers(1,&m.ebo);
    glBindVertexArray(m.vao);glBindBuffer(GL_ARRAY_BUFFER,m.vbo);
    glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(u.mesh->vertices.size()*sizeof(ChunkMeshVertex)),u.mesh->vertices.data(),GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,static_cast<GLsizeiptr>(u.mesh->indices.size()*sizeof(std::uint32_t)),u.mesh->indices.data(),GL_STATIC_DRAW);
    glVertexAttribIPointer(0,1,GL_UNSIGNED_INT,sizeof(ChunkMeshVertex),nullptr);glEnableVertexAttribArray(0);glBindVertexArray(0);
    if(glGetError()!=GL_NO_ERROR||!m.vao||!m.vbo||!m.ebo){destroy(m);return false;}
    m.indexCount=static_cast<std::uint32_t>(u.mesh->indices.size());m.instanceId=u.mesh->instanceId;m.revision=u.mesh->revision;
    if(existing!=pending_.end())destroy(existing->second);
    pending_[u.coord]=m;uploadedBytes_+=u.mesh->byteSize();return true;
}
bool Renderer::publish(const UploadGroup& group) {
    assertThread();
    for(auto&u:group.members){auto it=pending_.find(u.coord);if(it==pending_.end()||it->second.instanceId!=u.mesh->instanceId||it->second.revision!=u.mesh->revision)return false;}
    for(auto&u:group.members){auto& active=meshes_[u.coord];retire(active);active=pending_.at(u.coord);pending_.erase(u.coord);}
    ++swaps_;return true;
}
void Renderer::synchronize(const std::vector<ChunkCoord>& loaded) {
    assertThread();std::unordered_set<ChunkCoord,ChunkCoordHash> keep(loaded.begin(),loaded.end());
    for(auto it=meshes_.begin();it!=meshes_.end();)if(!keep.count(it->first)){retire(it->second);it=meshes_.erase(it);}else ++it;
    for(auto it=pending_.begin();it!=pending_.end();)if(!keep.count(it->first)){destroy(it->second);it=pending_.erase(it);}else ++it;
    collectRetired();
}
void Renderer::render(const Camera& camera,const std::vector<RenderChunk>& chunks,int w,int h) {
    assertThread();drawCalls_=0;if(w<=0||h<=0)return;
    glViewport(0,0,w,h);glClearColor(0.48f,0.73f,0.92f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D_ARRAY,textures_);
    glUniform1i(glGetUniformLocation(program_,"uBlockTextures"),0);
    glUniform1ui(glGetUniformLocation(program_,"uTextureSeed"),textureSeed_);
    auto view=camera.viewMatrix(),projection=camera.projectionMatrix(static_cast<float>(w)/h);
    glUniformMatrix4fv(glGetUniformLocation(program_,"uView"),1,GL_FALSE,view.data());
    glUniformMatrix4fv(glGetUniformLocation(program_,"uProjection"),1,GL_FALSE,projection.data());
    for(auto& c:chunks){
        auto it=meshes_.find(c.coord);
        if(it==meshes_.end()||it->second.indexCount==0||it->second.instanceId!=c.mesh->instanceId||it->second.revision!=c.mesh->revision)continue;
        glUniform3f(glGetUniformLocation(program_,"uChunkOrigin"),static_cast<float>((c.coord.x-camera.origin().x)*16),0,static_cast<float>((c.coord.z-camera.origin().z)*16));
        auto bx=static_cast<std::uint64_t>(c.coord.x*16),bz=static_cast<std::uint64_t>(c.coord.z*16);
        glUniform2ui(glGetUniformLocation(program_,"uChunkBlockX"),static_cast<GLuint>(bx),static_cast<GLuint>(bx>>32));
        glUniform2ui(glGetUniformLocation(program_,"uChunkBlockZ"),static_cast<GLuint>(bz),static_cast<GLuint>(bz>>32));
        glBindVertexArray(it->second.vao);glDrawElements(GL_TRIANGLES,it->second.indexCount,GL_UNSIGNED_INT,nullptr);++drawCalls_;
    }
}
void Renderer::renderTarget(const Camera& camera,const BlockTarget& target,int w,int h,bool crosshair) {
    assertThread();if(w<=0||h<=0)return;
    glUseProgram(overlayProgram_);glBindVertexArray(overlayVao_);glBindBuffer(GL_ARRAY_BUFFER,overlayVbo_);
    glDisable(GL_CULL_FACE);glDepthMask(GL_FALSE);glDepthFunc(GL_LEQUAL);
    std::vector<Clip> vertices;
    if(target.hit){
        auto view=camera.viewMatrix(),proj=camera.projectionMatrix(static_cast<float>(w)/h);
        std::array<Clip,8> corners{};
        for(int i=0;i<8;++i){
            Clip pos{static_cast<float>(target.block.x-camera.origin().x*16)+((i&1)?1.002f:-0.002f),target.block.y+((i&2)?1.002f:-0.002f),static_cast<float>(target.block.z-camera.origin().z*16)+((i&4)?1.002f:-0.002f),1};
            corners[i]=transform(proj,transform(view,pos));
        }
        for(int i=0;i<8;++i)for(int bit:{1,2,4})if(!(i&bit))ribbon(vertices,corners[i],corners[i|bit],w,h,1.5f);
        glUniform3f(glGetUniformLocation(overlayProgram_,"color"),0,0,0);
        glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Clip),vertices.data(),GL_STATIC_DRAW);
        glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices.size()));
    }
    if(crosshair){
        vertices.clear();glDisable(GL_DEPTH_TEST);
        ribbon(vertices,{-8.0f/w,0,0,1},{8.0f/w,0,0,1},w,h,1.5f);
        ribbon(vertices,{0,-8.0f/h,0,1},{0,8.0f/h,0,1},w,h,1.5f);
        glUniform3f(glGetUniformLocation(overlayProgram_,"color"),0.95f,0.95f,0.95f);
        glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Clip),vertices.data(),GL_STATIC_DRAW);
        glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices.size()));glEnable(GL_DEPTH_TEST);
    }
    glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);glEnable(GL_CULL_FACE);glBindVertexArray(0);
}
}

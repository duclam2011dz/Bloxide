#include <bloxide/Renderer.hpp>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
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
    if(!program_&&!overlayProgram_&&meshes_.empty()&&pending_.empty())return;
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
    overlayVbo_=overlayVao_=program_=overlayProgram_=0;
}
bool Renderer::initialize(const std::string& vp,const std::string& fp) {
    owner_=std::this_thread::get_id();
    program_=program(readFile(vp),readFile(fp));if(!program_)return false;
    overlayProgram_=program("#version 330 core\nlayout(location=0) in vec4 clip;void main(){gl_Position=clip;}\n",
                            "#version 330 core\nuniform vec3 color;out vec4 frag;void main(){frag=vec4(color,1);}\n");
    if(!overlayProgram_)return false;
    glGenVertexArrays(1,&overlayVao_);glGenBuffers(1,&overlayVbo_);
    glBindVertexArray(overlayVao_);glBindBuffer(GL_ARRAY_BUFFER,overlayVbo_);
    glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,sizeof(Clip),nullptr);glEnableVertexAttribArray(0);glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);glEnable(GL_CULL_FACE);glCullFace(GL_BACK);return true;
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
    auto view=camera.viewMatrix(),projection=camera.projectionMatrix(static_cast<float>(w)/h);
    glUniformMatrix4fv(glGetUniformLocation(program_,"uView"),1,GL_FALSE,view.data());
    glUniformMatrix4fv(glGetUniformLocation(program_,"uProjection"),1,GL_FALSE,projection.data());
    for(auto& c:chunks){
        auto it=meshes_.find(c.coord);
        if(it==meshes_.end()||it->second.indexCount==0||it->second.instanceId!=c.mesh->instanceId||it->second.revision!=c.mesh->revision)continue;
        glUniform3f(glGetUniformLocation(program_,"uChunkOrigin"),static_cast<float>(c.coord.x*16),0,static_cast<float>(c.coord.z*16));
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
            Clip pos{target.block.x+((i&1)?1.002f:-0.002f),target.block.y+((i&2)?1.002f:-0.002f),target.block.z+((i&4)?1.002f:-0.002f),1};
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

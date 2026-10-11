#ifndef BLOXIDE_GLAD_H
#define BLOXIDE_GLAD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef struct __GLsync* GLsync;
typedef unsigned long long GLuint64;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_BACK 0x0405
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_INT 0x1405
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RGB 0x1907
#define GL_TRIANGLES 0x0004
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define GL_ALREADY_SIGNALED 0x911A
#define GL_CONDITION_SATISFIED 0x911C
#define GL_LEQUAL 0x0203
#define GL_LESS 0x0201
#define GL_NO_ERROR 0
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#define GL_TEXTURE0 0x84C0
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_REPEAT 0x2901
#define GL_NEAREST 0x2600
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_MAX_ARRAY_TEXTURE_LAYERS 0x88FF
#define GL_TEXTURE_WIDTH 0x1000
#define GL_TEXTURE_HEIGHT 0x1001
#define GL_TEXTURE_DEPTH 0x8071

typedef void* (*GLADloadproc)(const char* name);
int gladLoadGLLoader(GLADloadproc load);

void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat);
void glClear(GLbitfield);
void glEnable(GLenum);
void glViewport(GLint, GLint, GLsizei, GLsizei);
void glCullFace(GLenum);
void glReadPixels(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
GLenum glGetError(void);
void glDisable(GLenum);
void glDepthMask(GLboolean);
void glDepthFunc(GLenum);
void glGenTextures(GLsizei,GLuint*);
void glDeleteTextures(GLsizei,const GLuint*);
void glBindTexture(GLenum,GLuint);
void glTexParameteri(GLenum,GLenum,GLint);
void glGetIntegerv(GLenum,GLint*);
void glGetTexLevelParameteriv(GLenum,GLint,GLenum,GLint*);
void glGetTexImage(GLenum,GLint,GLenum,GLenum,void*);
extern void (*glTexImage3D)(GLenum,GLint,GLint,GLsizei,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*);
extern void (*glTexSubImage3D)(GLenum,GLint,GLint,GLint,GLint,GLsizei,GLsizei,GLsizei,GLenum,GLenum,const void*);
extern void (*glGenerateMipmap)(GLenum);
extern void (*glActiveTexture)(GLenum);
extern void (*glUniform1i)(GLint,GLint);
extern void (*glUniform1ui)(GLint,GLuint);
extern void (*glUniform2ui)(GLint,GLuint,GLuint);
extern GLsync (*glFenceSync)(GLenum, GLbitfield);
extern GLenum (*glClientWaitSync)(GLsync, GLbitfield, GLuint64);
extern void (*glDeleteSync)(GLsync);
void glDrawElements(GLenum, GLsizei, GLenum, const void*);
extern GLuint (*glCreateShader)(GLenum);
extern void (*glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
extern void (*glCompileShader)(GLuint);
extern void (*glGetShaderiv)(GLuint, GLenum, GLint*);
extern void (*glDeleteShader)(GLuint);
extern GLuint (*glCreateProgram)(void);
extern void (*glAttachShader)(GLuint, GLuint);
extern void (*glLinkProgram)(GLuint);
extern void (*glGetProgramiv)(GLuint, GLenum, GLint*);
extern void (*glDeleteProgram)(GLuint);
extern void (*glUseProgram)(GLuint);
extern GLint (*glGetUniformLocation)(GLuint, const GLchar*);
extern void (*glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
extern void (*glUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
extern void (*glGenVertexArrays)(GLsizei, GLuint*);
extern void (*glDeleteVertexArrays)(GLsizei, const GLuint*);
extern void (*glBindVertexArray)(GLuint);
extern void (*glGenBuffers)(GLsizei, GLuint*);
extern void (*glDeleteBuffers)(GLsizei, const GLuint*);
extern void (*glBindBuffer)(GLenum, GLuint);
extern void (*glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
extern void (*glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
extern void (*glVertexAttribIPointer)(GLuint, GLint, GLenum, GLsizei, const void*);
extern void (*glEnableVertexAttribArray)(GLuint);
void glDrawArrays(GLenum, GLint, GLsizei);

#ifdef __cplusplus
}
#endif

#endif

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

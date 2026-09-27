#include <stddef.h>
#include <glad/glad.h>

GLuint (*glCreateShader)(GLenum) = NULL;
void (*glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = NULL;
void (*glCompileShader)(GLuint) = NULL;
void (*glGetShaderiv)(GLuint, GLenum, GLint*) = NULL;
void (*glDeleteShader)(GLuint) = NULL;
GLuint (*glCreateProgram)(void) = NULL;
void (*glAttachShader)(GLuint, GLuint) = NULL;
void (*glLinkProgram)(GLuint) = NULL;
void (*glGetProgramiv)(GLuint, GLenum, GLint*) = NULL;
void (*glDeleteProgram)(GLuint) = NULL;
void (*glUseProgram)(GLuint) = NULL;
GLint (*glGetUniformLocation)(GLuint, const GLchar*) = NULL;
void (*glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*) = NULL;
void (*glGenVertexArrays)(GLsizei, GLuint*) = NULL;
void (*glDeleteVertexArrays)(GLsizei, const GLuint*) = NULL;
void (*glBindVertexArray)(GLuint) = NULL;
void (*glGenBuffers)(GLsizei, GLuint*) = NULL;
void (*glDeleteBuffers)(GLsizei, const GLuint*) = NULL;
void (*glBindBuffer)(GLenum, GLuint) = NULL;
void (*glBufferData)(GLenum, GLsizeiptr, const void*, GLenum) = NULL;
void (*glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = NULL;
void (*glEnableVertexAttribArray)(GLuint) = NULL;

#define LOAD(name) do { *(void **)(&name) = load(#name); if (!name) return 0; } while (0)
int gladLoadGLLoader(GLADloadproc load) {
    if (!load) return 0;
    LOAD(glCreateShader); LOAD(glShaderSource); LOAD(glCompileShader); LOAD(glGetShaderiv); LOAD(glDeleteShader);
    LOAD(glCreateProgram); LOAD(glAttachShader); LOAD(glLinkProgram); LOAD(glGetProgramiv); LOAD(glDeleteProgram);
    LOAD(glUseProgram); LOAD(glGetUniformLocation); LOAD(glUniformMatrix4fv);
    LOAD(glGenVertexArrays); LOAD(glDeleteVertexArrays); LOAD(glBindVertexArray);
    LOAD(glGenBuffers); LOAD(glDeleteBuffers); LOAD(glBindBuffer); LOAD(glBufferData);
    LOAD(glVertexAttribPointer); LOAD(glEnableVertexAttribArray);
    return 1;
}

#include "Shader.h"
#include "PlatformGL.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

std::string Shader::readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << path << "\n";
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

unsigned int Shader::compile(unsigned int type, const std::string& src) {
    unsigned int shader = glCreateShader(type);
    const char* csrc = src.c_str();
    glShaderSource(shader, 1, &csrc, nullptr);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, 1024, nullptr, log);
        std::cerr << "Shader compile error: " << log << "\n";
    }
    return shader;
}

Shader::Shader(const std::string& vertPath, const std::string& fragPath) {
    std::string vertSrc = readFile(vertPath);
    std::string fragSrc = readFile(fragPath);

    unsigned int vert = compile(GL_VERTEX_SHADER, vertSrc);
    unsigned int frag = compile(GL_FRAGMENT_SHADER, fragSrc);

    id = glCreateProgram();
    glAttachShader(id, vert);
    glAttachShader(id, frag);
    glLinkProgram(id);

    int success;
    glGetProgramiv(id, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(id, 1024, nullptr, log);
        std::cerr << "Shader link error: " << log << "\n";
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
}

Shader::~Shader() {
    if (id) glDeleteProgram(id);
}

void Shader::use() const { glUseProgram(id); }

void Shader::setMat4(const std::string& name, const glm::mat4& m) const {
    glUniformMatrix4fv(glGetUniformLocation(id, name.c_str()), 1, GL_FALSE, glm::value_ptr(m));
}

void Shader::setVec3(const std::string& name, const glm::vec3& v) const {
    glUniform3fv(glGetUniformLocation(id, name.c_str()), 1, glm::value_ptr(v));
}

void Shader::setVec2(const std::string& name, const glm::vec2& v) const {
    glUniform2fv(glGetUniformLocation(id, name.c_str()), 1, glm::value_ptr(v));
}

void Shader::setFloat(const std::string& name, float f) const {
    glUniform1f(glGetUniformLocation(id, name.c_str()), f);
}

void Shader::setInt(const std::string& name, int i) const {
    glUniform1i(glGetUniformLocation(id, name.c_str()), i);
}

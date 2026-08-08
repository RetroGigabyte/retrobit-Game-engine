#pragma once
#include <string>
#include <glm/glm.hpp>

class Shader {
public:
    unsigned int id = 0;

    Shader(const std::string& vertPath, const std::string& fragPath);
    ~Shader();

    void use() const;
    void setMat4(const std::string& name, const glm::mat4& m) const;
    void setVec3(const std::string& name, const glm::vec3& v) const;
    void setFloat(const std::string& name, float f) const;
    void setInt(const std::string& name, int i) const;

private:
    static std::string readFile(const std::string& path);
    static unsigned int compile(unsigned int type, const std::string& src);
};

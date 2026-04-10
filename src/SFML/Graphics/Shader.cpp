////////////////////////////////////////////////////////////
//
// SFML - Simple and Fast Multimedia Library
// Copyright (C) 2007-2026 Laurent Gomila (laurent@sfml-dev.org)
//
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it freely,
// subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented;
//    you must not claim that you wrote the original software.
//    If you use this software in a product, an acknowledgment
//    in the product documentation would be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such,
//    and must not be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source distribution.
//
////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////
#include <SFML/Graphics/Backend/BackendContext.hpp>
#include <SFML/Graphics/Backend/BackendFactory.hpp>
#include <SFML/Graphics/Shader.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <SFML/System/Err.hpp>
#include <SFML/System/Exception.hpp>
#include <SFML/System/InputStream.hpp>
#include <SFML/System/Utils.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/System/Vector3.hpp>

#include <array>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <utility>
#include <vector>

#include <cstdint>

namespace
{

// Read the contents of a file into an array of char
bool getFileContents(const std::filesystem::path& filename, std::vector<char>& buffer)
{
    if (auto file = std::ifstream(filename, std::ios_base::binary))
    {
        file.seekg(0, std::ios_base::end);
        const std::ifstream::pos_type size = file.tellg();
        if (size > 0)
        {
            file.seekg(0, std::ios_base::beg);
            buffer.resize(static_cast<std::size_t>(size));
            file.read(buffer.data(), static_cast<std::streamsize>(size));
        }
        buffer.push_back('\0');
        return true;
    }

    return false;
}

// Read the contents of a stream into an array of char
bool getStreamContents(sf::InputStream& stream, std::vector<char>& buffer)
{
    bool                success = false;
    const std::optional size    = stream.getSize();
    if (size > std::size_t{0})
    {
        buffer.resize(*size);

        if (!stream.seek(0).has_value())
        {
            sf::err() << "Failed to seek shader stream" << std::endl;
            return false;
        }

        const std::optional read = stream.read(buffer.data(), *size);
        success                  = (read == size);
    }
    buffer.push_back('\0');
    return success;
}

// Transforms an array of 2D vectors into a contiguous array of scalars
std::vector<float> flatten(const sf::Vector2f* vectorArray, std::size_t length)
{
    const std::size_t vectorSize = 2;

    std::vector<float> contiguous(vectorSize * length);
    for (std::size_t i = 0; i < length; ++i)
    {
        contiguous[vectorSize * i]     = vectorArray[i].x;
        contiguous[vectorSize * i + 1] = vectorArray[i].y;
    }

    return contiguous;
}

// Transforms an array of 3D vectors into a contiguous array of scalars
std::vector<float> flatten(const sf::Vector3f* vectorArray, std::size_t length)
{
    const std::size_t vectorSize = 3;

    std::vector<float> contiguous(vectorSize * length);
    for (std::size_t i = 0; i < length; ++i)
    {
        contiguous[vectorSize * i]     = vectorArray[i].x;
        contiguous[vectorSize * i + 1] = vectorArray[i].y;
        contiguous[vectorSize * i + 2] = vectorArray[i].z;
    }

    return contiguous;
}

// Transforms an array of 4D vectors into a contiguous array of scalars
std::vector<float> flatten(const sf::Glsl::Vec4* vectorArray, std::size_t length)
{
    const std::size_t vectorSize = 4;

    std::vector<float> contiguous(vectorSize * length);
    for (std::size_t i = 0; i < length; ++i)
    {
        contiguous[vectorSize * i]     = vectorArray[i].x;
        contiguous[vectorSize * i + 1] = vectorArray[i].y;
        contiguous[vectorSize * i + 2] = vectorArray[i].z;
        contiguous[vectorSize * i + 3] = vectorArray[i].w;
    }

    return contiguous;
}
} // namespace


namespace sf
{

////////////////////////////////////////////////////////////
struct Shader::UniformBinder
{
    UniformBinder(Shader& shader, const std::string& name)
    {
        priv::getGraphicsBackend().prepareUniformUpdate(
            shader.m_shaderProgram);

        location = shader.getUniformLocation(name);
    }

    ~UniformBinder()
    {
        priv::getGraphicsBackend().finalizeUniformUpdate();
    }

    UniformBinder(const UniformBinder&) = delete;
    UniformBinder& operator=(const UniformBinder&) = delete;

    priv::BackendContextLock lock;
    int                  location{-1};
};


////////////////////////////////////////////////////////////
Shader::Shader(const std::filesystem::path& filename, Type type)
{
    if (!loadFromFile(filename, type))
        throw Exception("Failed to load shader from file");
}


////////////////////////////////////////////////////////////
Shader::Shader(const std::filesystem::path& vertexShaderFilename, const std::filesystem::path& fragmentShaderFilename)
{
    if (!loadFromFile(vertexShaderFilename, fragmentShaderFilename))
        throw Exception("Failed to load shader from files");
}


////////////////////////////////////////////////////////////
Shader::Shader(const std::filesystem::path& vertexShaderFilename,
               const std::filesystem::path& geometryShaderFilename,
               const std::filesystem::path& fragmentShaderFilename)
{
    if (!loadFromFile(vertexShaderFilename, geometryShaderFilename, fragmentShaderFilename))
        throw Exception("Failed to load shader from files");
}


////////////////////////////////////////////////////////////
Shader::Shader(std::string_view shader, Type type)
{
    if (!loadFromMemory(shader, type))
        throw Exception("Failed to load shader from memory");
}


////////////////////////////////////////////////////////////
Shader::Shader(std::string_view vertexShader, std::string_view fragmentShader)
{
    if (!loadFromMemory(vertexShader, fragmentShader))
        throw Exception("Failed to load shader from memory");
}


////////////////////////////////////////////////////////////
Shader::Shader(std::string_view vertexShader, std::string_view geometryShader, std::string_view fragmentShader)
{
    if (!loadFromMemory(vertexShader, geometryShader, fragmentShader))
        throw Exception("Failed to load shader from memory");
}


////////////////////////////////////////////////////////////
Shader::Shader(InputStream& stream, Type type)
{
    if (!loadFromStream(stream, type))
        throw Exception("Failed to load shader from stream");
}


////////////////////////////////////////////////////////////
Shader::Shader(InputStream& vertexShaderStream, InputStream& fragmentShaderStream)
{
    if (!loadFromStream(vertexShaderStream, fragmentShaderStream))
        throw Exception("Failed to load shader from streams");
}


////////////////////////////////////////////////////////////
Shader::Shader(InputStream& vertexShaderStream, InputStream& geometryShaderStream, InputStream& fragmentShaderStream)
{
    if (!loadFromStream(vertexShaderStream, geometryShaderStream, fragmentShaderStream))
        throw Exception("Failed to load shader from streams");
}


////////////////////////////////////////////////////////////
Shader::~Shader()
{
    const priv::BackendContextLock lock;

    if (m_shaderProgram)
        priv::getGraphicsBackend().destroyShader(m_shaderProgram);
}

////////////////////////////////////////////////////////////
Shader::Shader(Shader&& source) noexcept :
    m_shaderProgram(std::exchange(source.m_shaderProgram, 0u)),
    m_currentTexture(std::exchange(source.m_currentTexture, -1)),
    m_textures(std::move(source.m_textures)),
    m_uniforms(std::move(source.m_uniforms))
{
}

////////////////////////////////////////////////////////////
Shader& Shader::operator=(Shader&& right) noexcept
{
    // Make sure we aren't moving ourselves.
    if (&right == this)
    {
        return *this;
    }

    if (m_shaderProgram)
    {
        const priv::BackendContextLock lock;
        priv::getGraphicsBackend().destroyShader(m_shaderProgram);
    }

    // Move the contents of right.
    m_shaderProgram  = std::exchange(right.m_shaderProgram, 0u);
    m_currentTexture = std::exchange(right.m_currentTexture, -1);
    m_textures       = std::move(right.m_textures);
    m_uniforms       = std::move(right.m_uniforms);
    return *this;
}

////////////////////////////////////////////////////////////
bool Shader::loadFromFile(const std::filesystem::path& filename, Type type)
{
    // Read the file
    std::vector<char> shader;
    if (!getFileContents(filename, shader))
    {
        err() << "Failed to open shader file\n" << formatDebugPathInfo(filename) << std::endl;
        return false;
    }

    // Compile the shader program
    if (type == Type::Vertex)
        return compile(shader.data(), {}, {});

    if (type == Type::Geometry)
        return compile({}, shader.data(), {});

    return compile({}, {}, shader.data());
}


////////////////////////////////////////////////////////////
bool Shader::loadFromFile(const std::filesystem::path& vertexShaderFilename,
                          const std::filesystem::path& fragmentShaderFilename)
{
    // Read the vertex shader file
    std::vector<char> vertexShader;
    if (!getFileContents(vertexShaderFilename, vertexShader))
    {
        err() << "Failed to open vertex shader file\n" << formatDebugPathInfo(vertexShaderFilename) << std::endl;
        return false;
    }

    // Read the fragment shader file
    std::vector<char> fragmentShader;
    if (!getFileContents(fragmentShaderFilename, fragmentShader))
    {
        err() << "Failed to open fragment shader file\n" << formatDebugPathInfo(fragmentShaderFilename) << std::endl;
        return false;
    }

    // Compile the shader program
    return compile(vertexShader.data(), {}, fragmentShader.data());
}


////////////////////////////////////////////////////////////
bool Shader::loadFromFile(const std::filesystem::path& vertexShaderFilename,
                          const std::filesystem::path& geometryShaderFilename,
                          const std::filesystem::path& fragmentShaderFilename)
{
    // Read the vertex shader file
    std::vector<char> vertexShader;
    if (!getFileContents(vertexShaderFilename, vertexShader))
    {
        err() << "Failed to open vertex shader file\n" << formatDebugPathInfo(vertexShaderFilename) << std::endl;
        return false;
    }

    // Read the geometry shader file
    std::vector<char> geometryShader;
    if (!getFileContents(geometryShaderFilename, geometryShader))
    {
        err() << "Failed to open geometry shader file\n" << formatDebugPathInfo(geometryShaderFilename) << std::endl;
        return false;
    }

    // Read the fragment shader file
    std::vector<char> fragmentShader;
    if (!getFileContents(fragmentShaderFilename, fragmentShader))
    {
        err() << "Failed to open fragment shader file\n" << formatDebugPathInfo(fragmentShaderFilename) << std::endl;
        return false;
    }

    // Compile the shader program
    return compile(vertexShader.data(), geometryShader.data(), fragmentShader.data());
}


////////////////////////////////////////////////////////////
bool Shader::loadFromMemory(std::string_view shader, Type type)
{
    // Compile the shader program
    if (type == Type::Vertex)
        return compile(shader, {}, {});

    if (type == Type::Geometry)
        return compile({}, shader, {});

    return compile({}, {}, shader);
}


////////////////////////////////////////////////////////////
bool Shader::loadFromMemory(std::string_view vertexShader, std::string_view fragmentShader)
{
    // Compile the shader program
    return compile(vertexShader, {}, fragmentShader);
}


////////////////////////////////////////////////////////////
bool Shader::loadFromMemory(std::string_view vertexShader, std::string_view geometryShader, std::string_view fragmentShader)
{
    // Compile the shader program
    return compile(vertexShader, geometryShader, fragmentShader);
}


////////////////////////////////////////////////////////////
bool Shader::loadFromStream(InputStream& stream, Type type)
{
    // Read the shader code from the stream
    std::vector<char> shader;
    if (!getStreamContents(stream, shader))
    {
        err() << "Failed to read shader from stream" << std::endl;
        return false;
    }

    // Compile the shader program
    if (type == Type::Vertex)
        return compile(shader.data(), {}, {});

    if (type == Type::Geometry)
        return compile({}, shader.data(), {});

    return compile({}, {}, shader.data());
}


////////////////////////////////////////////////////////////
bool Shader::loadFromStream(InputStream& vertexShaderStream, InputStream& fragmentShaderStream)
{
    // Read the vertex shader code from the stream
    std::vector<char> vertexShader;
    if (!getStreamContents(vertexShaderStream, vertexShader))
    {
        err() << "Failed to read vertex shader from stream" << std::endl;
        return false;
    }

    // Read the fragment shader code from the stream
    std::vector<char> fragmentShader;
    if (!getStreamContents(fragmentShaderStream, fragmentShader))
    {
        err() << "Failed to read fragment shader from stream" << std::endl;
        return false;
    }

    // Compile the shader program
    return compile(vertexShader.data(), {}, fragmentShader.data());
}


////////////////////////////////////////////////////////////
bool Shader::loadFromStream(InputStream& vertexShaderStream, InputStream& geometryShaderStream, InputStream& fragmentShaderStream)
{
    // Read the vertex shader code from the stream
    std::vector<char> vertexShader;
    if (!getStreamContents(vertexShaderStream, vertexShader))
    {
        err() << "Failed to read vertex shader from stream" << std::endl;
        return false;
    }

    // Read the geometry shader code from the stream
    std::vector<char> geometryShader;
    if (!getStreamContents(geometryShaderStream, geometryShader))
    {
        err() << "Failed to read geometry shader from stream" << std::endl;
        return false;
    }

    // Read the fragment shader code from the stream
    std::vector<char> fragmentShader;
    if (!getStreamContents(fragmentShaderStream, fragmentShader))
    {
        err() << "Failed to read fragment shader from stream" << std::endl;
        return false;
    }

    // Compile the shader program
    return compile(vertexShader.data(), geometryShader.data(), fragmentShader.data());
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, float x)
{
    const UniformBinder binder(*this, name);
    if (binder.location != -1)
        priv::getGraphicsBackend().setUniform(m_shaderProgram, binder.location, x);
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, Glsl::Vec2 v)
{
    const UniformBinder binder(*this, name);
    if (binder.location != -1)
        priv::getGraphicsBackend().setUniform(m_shaderProgram, binder.location, v);
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, const Glsl::Vec3& v)
{
    const UniformBinder binder(*this, name);
    if (binder.location != -1)
        priv::getGraphicsBackend().setUniform(m_shaderProgram, binder.location, v);
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, const Glsl::Vec4& v)
{
    const UniformBinder binder(*this, name);
    if (binder.location != -1)
        priv::getGraphicsBackend().setUniform(m_shaderProgram, binder.location, v);
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, int x)
{
    const UniformBinder binder(*this, name);
    if (binder.location != -1)
        priv::getGraphicsBackend().setUniform(m_shaderProgram, binder.location, x);
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, Glsl::Ivec2 v)
{
    const UniformBinder binder(*this, name);
    if (binder.location != -1)
        priv::getGraphicsBackend().setUniform(m_shaderProgram, binder.location, v);
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, const Glsl::Ivec3& v)
{
    const UniformBinder binder(*this, name);
    if (binder.location != -1)
        priv::getGraphicsBackend().setUniform(m_shaderProgram, binder.location, v);
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, const Glsl::Ivec4& v)
{
    const UniformBinder binder(*this, name);
    if (binder.location != -1)
        priv::getGraphicsBackend().setUniform(m_shaderProgram, binder.location, v);
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, bool x)
{
    setUniform(name, static_cast<int>(x));
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, Glsl::Bvec2 v)
{
    setUniform(name, Glsl::Ivec2(v));
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, const Glsl::Bvec3& v)
{
    setUniform(name, Glsl::Ivec3(v));
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, const Glsl::Bvec4& v)
{
    setUniform(name, Glsl::Ivec4(v));
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, const Glsl::Mat3& matrix)
{
    const UniformBinder binder(*this, name);
    if (binder.location != -1)
        priv::getGraphicsBackend().setUniform(m_shaderProgram, binder.location, matrix);
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, const Glsl::Mat4& matrix)
{
    const UniformBinder binder(*this, name);
    if (binder.location != -1)
        priv::getGraphicsBackend().setUniform(m_shaderProgram, binder.location, matrix);
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, const Texture& texture)
{
    if (!m_shaderProgram)
        return;

    const priv::BackendContextLock lock;

    // Find the location of the variable in the shader
    const int location = getUniformLocation(name);
    if (location != -1)
    {
        // Store the location -> texture mapping
        const auto it = m_textures.find(location);
        if (it == m_textures.end())
        {
            // New entry, make sure there are enough texture units
            if (m_textures.size() + 1 >= priv::getGraphicsBackend().getMaxTextureUnits())
            {
                err() << "Impossible to use texture " << std::quoted(name)
                      << " for shader: all available texture units are used" << std::endl;
                return;
            }

            m_textures[location] = &texture;
        }
        else
        {
            // Location already used, just replace the texture
            it->second = &texture;
        }
    }
}


////////////////////////////////////////////////////////////
void Shader::setUniform(const std::string& name, CurrentTextureType)
{
    if (!m_shaderProgram)
        return;

    const priv::BackendContextLock lock;

    // Find the location of the variable in the shader
    m_currentTexture = getUniformLocation(name);
}


////////////////////////////////////////////////////////////
void Shader::setUniformArray(const std::string& name, const float* scalarArray, std::size_t length)
{
    const UniformBinder binder(*this, name);
    if (binder.location != -1)
        priv::getGraphicsBackend().setUniformArray(m_shaderProgram,
                                                   binder.location,
                                                   scalarArray,
                                                   length);
}


////////////////////////////////////////////////////////////
void Shader::setUniformArray(const std::string& name, const Glsl::Vec2* vectorArray, std::size_t length)
{
    std::vector<float> contiguous = flatten(vectorArray, length);

    const UniformBinder binder(*this, name);
    if (binder.location != -1)
        priv::getGraphicsBackend().setUniformArray(m_shaderProgram,
                                                   binder.location,
                                                   reinterpret_cast<const Glsl::Vec2*>(contiguous.data()),
                                                   length);
}


////////////////////////////////////////////////////////////
void Shader::setUniformArray(const std::string& name, const Glsl::Vec3* vectorArray, std::size_t length)
{
    std::vector<float> contiguous = flatten(vectorArray, length);

    const UniformBinder binder(*this, name);
    if (binder.location != -1)
        priv::getGraphicsBackend().setUniformArray(m_shaderProgram,
                                                   binder.location,
                                                   reinterpret_cast<const Glsl::Vec3*>(contiguous.data()),
                                                   length);
}


////////////////////////////////////////////////////////////
void Shader::setUniformArray(const std::string& name, const Glsl::Vec4* vectorArray, std::size_t length)
{
    std::vector<float> contiguous = flatten(vectorArray, length);

    const UniformBinder binder(*this, name);
    if (binder.location != -1)
        priv::getGraphicsBackend().setUniformArray(m_shaderProgram,
                                                   binder.location,
                                                   reinterpret_cast<const Glsl::Vec4*>(contiguous.data()),
                                                   length);
}


////////////////////////////////////////////////////////////
void Shader::setUniformArray(const std::string& name, const Glsl::Mat3* matrixArray, std::size_t length)
{
    static const std::size_t matrixSize = matrixArray[0].array.size();

    std::vector<float> contiguous(matrixSize * length);
    for (std::size_t i = 0; i < length; ++i)
        priv::copyMatrix(matrixArray[i].array.data(), matrixSize, &contiguous[matrixSize * i]);

    const UniformBinder binder(*this, name);
    if (binder.location != -1)
        priv::getGraphicsBackend().setUniformArray(m_shaderProgram,
                                                   binder.location,
                                                   reinterpret_cast<const Glsl::Mat3*>(contiguous.data()),
                                                   length);
}


////////////////////////////////////////////////////////////
void Shader::setUniformArray(const std::string& name, const Glsl::Mat4* matrixArray, std::size_t length)
{
    static const std::size_t matrixSize = matrixArray[0].array.size();

    std::vector<float> contiguous(matrixSize * length);
    for (std::size_t i = 0; i < length; ++i)
        priv::copyMatrix(matrixArray[i].array.data(), matrixSize, &contiguous[matrixSize * i]);

    const UniformBinder binder(*this, name);
    if (binder.location != -1)
        priv::getGraphicsBackend().setUniformArray(m_shaderProgram,
                                                   binder.location,
                                                   reinterpret_cast<const Glsl::Mat4*>(contiguous.data()),
                                                   length);
}


////////////////////////////////////////////////////////////
unsigned int Shader::getNativeHandle() const
{
    return static_cast<unsigned int>(m_shaderProgram);
}


////////////////////////////////////////////////////////////
void Shader::bind(const Shader* shader)
{
    const priv::BackendContextLock lock;

    // Make sure that we can use shaders
    if (!isAvailable())
    {
        err() << "Failed to bind or unbind shader: your system doesn't support shaders "
              << "(you should test Shader::isAvailable() before trying to use the Shader class)" << std::endl;
        return;
    }

    auto& backend = priv::getGraphicsBackend();

    if (shader && shader->m_shaderProgram)
    {
        // Enable the program
        backend.bindShader(shader->m_shaderProgram);

        // Bind the textures
        shader->bindTextures();

        // Bind the current texture
        if (shader->m_currentTexture != -1)
            backend.setUniform(shader->m_shaderProgram,
                               shader->m_currentTexture,
                               0);
    }
    else
    {
        // Bind no shader
        backend.bindShader(0);
    }
}


////////////////////////////////////////////////////////////
bool Shader::isAvailable()
{
    static const bool available = []
    {
        const priv::BackendContextLock contextLock;
        return priv::getGraphicsBackend().isShaderAvailable();
    }();

    return available;
}


////////////////////////////////////////////////////////////
bool Shader::isGeometryAvailable()
{
    static const bool available = []
    {
        const priv::BackendContextLock contextLock;
        return priv::getGraphicsBackend().isGeometryShaderAvailable();
    }();

    return available;
}


////////////////////////////////////////////////////////////
bool Shader::compile(std::string_view vertexShaderCode, std::string_view geometryShaderCode, std::string_view fragmentShaderCode)
{
    const priv::BackendContextLock lock;

    auto& backend = priv::getGraphicsBackend();

    // First make sure that we can use shaders
    if (!isAvailable())
    {
        err() << "Failed to create a shader: your system doesn't support shaders "
              << "(you should test Shader::isAvailable() before trying to use the Shader class)" << std::endl;
        return false;
    }

    // Make sure we can use geometry shaders
    if (!geometryShaderCode.empty() && !isGeometryAvailable())
    {
        err() << "Failed to create a shader: your system doesn't support geometry shaders "
              << "(you should test Shader::isGeometryAvailable() before trying to use geometry shaders)" << std::endl;
        return false;
    }

    // Compile the shader via the backend
    const auto handle = backend.compileShader(vertexShaderCode, geometryShaderCode, fragmentShaderCode);
    if (!handle)
        return false;

    // Destroy the old shader if it was already created
    if (m_shaderProgram)
        backend.destroyShader(m_shaderProgram);

    // Reset the internal state
    m_currentTexture = -1;
    m_textures.clear();
    m_uniforms.clear();

    m_shaderProgram = handle;

    // Force a pipeline flush, so that the shader will appear updated
    // in all contexts immediately (solves problems in multi-threaded apps)
    priv::getGraphicsBackend().flushPipeline();

    return true;
}


////////////////////////////////////////////////////////////
void Shader::bindTextures() const
{
    auto& backend = priv::getGraphicsBackend();

    auto it = m_textures.begin();
    for (std::size_t i = 0; i < m_textures.size(); ++i)
    {
        const auto index = static_cast<int>(i + 1);
        backend.setUniformTexture(m_shaderProgram,
                                  it->first,
                                  static_cast<priv::BackendTextureHandle>(it->second->getNativeHandle()),
                                  index);
        ++it;
    }
}


////////////////////////////////////////////////////////////
int Shader::getUniformLocation(const std::string& name)
{
    // Check the cache
    if (const auto it = m_uniforms.find(name); it != m_uniforms.end())
        return it->second;

    // Not in cache, request the location from the backend
    const int location = priv::getGraphicsBackend().getUniformLocation(
        m_shaderProgram, name);
    m_uniforms.try_emplace(name, location);

    if (location == -1)
        err() << "Uniform " << std::quoted(name) << " not found in shader" << std::endl;

    return location;
}

} // namespace sf

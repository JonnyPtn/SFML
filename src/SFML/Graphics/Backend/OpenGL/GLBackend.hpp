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

#pragma once

////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////
#include <SFML/Graphics/Backend/GraphicsBackend.hpp>


namespace sf::priv
{

////////////////////////////////////////////////////////////
/// \brief OpenGL implementation of the GraphicsBackend interface
///
////////////////////////////////////////////////////////////
class GLBackend : public GraphicsBackend
{
public:
    ////////////////////////////////////////////////////////////
    /// \brief Default constructor
    ///
    ////////////////////////////////////////////////////////////
    GLBackend() = default;

    ////////////////////////////////////////////////////////////
    // Render target operations
    ////////////////////////////////////////////////////////////

    void clear(Color color) override;
    void clearStencil(StencilValue stencilValue) override;
    void clear(Color color, StencilValue stencilValue) override;
    void setViewport(const IntRect& viewport) override;
    void setScissor(const IntRect& scissor, bool enable) override;
    void setSrgb(bool enable) override;

    ////////////////////////////////////////////////////////////
    // State management
    ////////////////////////////////////////////////////////////

    void applyBlendMode(const BlendMode& mode) override;
    void applyStencilMode(const StencilMode& mode) override;
    void setColorMask(bool enable) override;

    ////////////////////////////////////////////////////////////
    // Drawing
    ////////////////////////////////////////////////////////////

    void setupVertexData(const Vertex* vertices, std::size_t count) override;
    void setupVertexBuffer(BackendBufferHandle buffer) override;
    void applyTransform(const Transform& projection, const Transform& model) override;
    void drawPrimitives(PrimitiveType type, std::size_t firstVertex, std::size_t vertexCount) override;

    ////////////////////////////////////////////////////////////
    // Texture operations
    ////////////////////////////////////////////////////////////

    BackendTextureHandle createTexture(Vector2u size, bool sRgb) override;
    void                 destroyTexture(BackendTextureHandle handle) override;
    void                 updateTexture(BackendTextureHandle handle,
                                       const std::uint8_t*  pixels,
                                       Vector2u             size,
                                       Vector2u             dest) override;
    void                 updateTextureFromTexture(BackendTextureHandle handle,
                                                  BackendTextureHandle srcHandle,
                                                  Vector2u             srcSize,
                                                  Vector2u             dest) override;
    void                 updateTextureFromFramebuffer(BackendTextureHandle handle, Vector2u size, Vector2u dest) override;
    void                 bindTexture(BackendTextureHandle handle,
                                     CoordinateType       coordinateType,
                                     Vector2u             textureSize,
                                     Vector2u             actualSize,
                                     bool                 pixelsFlipped) override;
    Image                readbackTexture(BackendTextureHandle handle, Vector2u size) override;
    void                 setTextureSmooth(BackendTextureHandle handle, bool smooth, bool hasMipmap) override;
    void                 setTextureRepeated(BackendTextureHandle handle, bool repeated) override;
    bool                 generateMipmap(BackendTextureHandle handle, Vector2u size, bool smooth) override;
    unsigned int         getMaxTextureSize() const override;
    unsigned int         getValidTextureSize(unsigned int size) const override;

    ////////////////////////////////////////////////////////////
    // Shader operations
    ////////////////////////////////////////////////////////////

    BackendShaderHandle compileShader(std::string_view vertexShaderCode,
                                      std::string_view geometryShaderCode,
                                      std::string_view fragmentShaderCode) override;
    void                destroyShader(BackendShaderHandle handle) override;
    void                bindShader(BackendShaderHandle handle) override;
    int                 getUniformLocation(BackendShaderHandle handle, const std::string& name) override;

    void setUniform(BackendShaderHandle handle, int location, float x) override;
    void setUniform(BackendShaderHandle handle, int location, const Glsl::Vec2& v) override;
    void setUniform(BackendShaderHandle handle, int location, const Glsl::Vec3& v) override;
    void setUniform(BackendShaderHandle handle, int location, const Glsl::Vec4& v) override;
    void setUniform(BackendShaderHandle handle, int location, int x) override;
    void setUniform(BackendShaderHandle handle, int location, const Glsl::Ivec2& v) override;
    void setUniform(BackendShaderHandle handle, int location, const Glsl::Ivec3& v) override;
    void setUniform(BackendShaderHandle handle, int location, const Glsl::Ivec4& v) override;
    void setUniform(BackendShaderHandle handle, int location, bool x) override;
    void setUniform(BackendShaderHandle handle, int location, const Glsl::Bvec2& v) override;
    void setUniform(BackendShaderHandle handle, int location, const Glsl::Bvec3& v) override;
    void setUniform(BackendShaderHandle handle, int location, const Glsl::Bvec4& v) override;
    void setUniform(BackendShaderHandle handle, int location, const Glsl::Mat3& m) override;
    void setUniform(BackendShaderHandle handle, int location, const Glsl::Mat4& m) override;

    void setUniformTexture(BackendShaderHandle  handle,
                           int                  location,
                           BackendTextureHandle textureHandle,
                           int                  textureUnit) override;

    void setUniformArray(BackendShaderHandle handle, int location, const float* data, std::size_t length) override;
    void setUniformArray(BackendShaderHandle handle, int location, const Glsl::Vec2* data, std::size_t length) override;
    void setUniformArray(BackendShaderHandle handle, int location, const Glsl::Vec3* data, std::size_t length) override;
    void setUniformArray(BackendShaderHandle handle, int location, const Glsl::Vec4* data, std::size_t length) override;
    void setUniformArray(BackendShaderHandle handle, int location, const Glsl::Mat3* data, std::size_t length) override;
    void setUniformArray(BackendShaderHandle handle, int location, const Glsl::Mat4* data, std::size_t length) override;

    ////////////////////////////////////////////////////////////
    // Vertex buffer operations
    ////////////////////////////////////////////////////////////

    BackendBufferHandle createBuffer(std::size_t vertexCount, VertexBuffer::Usage usage) override;
    void                destroyBuffer(BackendBufferHandle handle) override;
    bool                updateBuffer(BackendBufferHandle handle,
                                     const Vertex*       vertices,
                                     std::size_t         count,
                                     unsigned int        offset) override;
    bool                copyBuffer(BackendBufferHandle destHandle,
                                   BackendBufferHandle srcHandle,
                                   std::size_t         srcSize) override;

    ////////////////////////////////////////////////////////////
    // Framebuffer operations
    ////////////////////////////////////////////////////////////

    BackendFramebufferHandle createFramebuffer(Vector2u               size,
                                               BackendTextureHandle   texture,
                                               const ContextSettings& settings) override;
    void                     destroyFramebuffer(BackendFramebufferHandle handle) override;
    bool                     bindFramebuffer(BackendFramebufferHandle handle) override;
    bool                     isFramebufferSrgb(BackendFramebufferHandle handle) const override;
    void                     updateFramebufferTexture(BackendFramebufferHandle handle,
                                                      BackendTextureHandle     texture) override;

    ////////////////////////////////////////////////////////////
    // Capability queries
    ////////////////////////////////////////////////////////////

    bool         isShaderAvailable() const override;
    bool         isGeometryShaderAvailable() const override;
    bool         isVertexBufferAvailable() const override;
    bool         isNonPowerOfTwoTextureSupported() const override;

    ////////////////////////////////////////////////////////////
    // OpenGL-specific: Reset internal GL states for first draw
    ////////////////////////////////////////////////////////////

    void resetGLStates();
};

} // namespace sf::priv

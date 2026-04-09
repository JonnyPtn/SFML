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
#include <SFML/Graphics/BlendMode.hpp>
#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/CoordinateType.hpp>
#include <SFML/Graphics/Glsl.hpp>
#include <SFML/Graphics/PrimitiveType.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/StencilMode.hpp>
#include <SFML/Graphics/Vertex.hpp>
#include <SFML/Graphics/VertexBuffer.hpp>

#include <SFML/System/Vector2.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>


namespace sf
{
class Image;
class Transform;
class Texture;

struct ContextSettings;

namespace priv
{

////////////////////////////////////////////////////////////
/// \brief Opaque handle types for backend-managed resources
///
/// Each backend interprets the std::uint64_t value as it sees
/// fit (e.g. an OpenGL name, a Metal id<MTLTexture>, etc.).
/// A value of 0 represents no resource / null handle.
///
////////////////////////////////////////////////////////////
using BackendTextureHandle     = std::uint64_t;
using BackendShaderHandle      = std::uint64_t;
using BackendBufferHandle      = std::uint64_t;
using BackendFramebufferHandle = std::uint64_t;


////////////////////////////////////////////////////////////
/// \brief Abstract interface for graphics backend implementations
///
/// This class defines all the operations that a graphics backend
/// (OpenGL, Metal, Vulkan, ...) must implement in order to be
/// used by SFML's public graphics classes.
///
////////////////////////////////////////////////////////////
class GraphicsBackend
{
public:
    ////////////////////////////////////////////////////////////
    /// \brief Virtual destructor
    ///
    ////////////////////////////////////////////////////////////
    virtual ~GraphicsBackend() = default;

    ////////////////////////////////////////////////////////////
    /// \brief Deleted copy operations
    ///
    ////////////////////////////////////////////////////////////
    GraphicsBackend(const GraphicsBackend&) = delete;
    GraphicsBackend& operator=(const GraphicsBackend&) = delete;

    ////////////////////////////////////////////////////////////
    /// \brief Deleted move operations
    ///
    ////////////////////////////////////////////////////////////
    GraphicsBackend(GraphicsBackend&&) = delete;
    GraphicsBackend& operator=(GraphicsBackend&&) = delete;

    ////////////////////////////////////////////////////////////
    // Render target operations
    ////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////
    /// \brief Clear the current render target with a color
    ///
    /// \param color Fill color
    ///
    ////////////////////////////////////////////////////////////
    virtual void clear(Color color) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Clear the stencil buffer
    ///
    /// \param stencilValue Value to clear to
    ///
    ////////////////////////////////////////////////////////////
    virtual void clearStencil(StencilValue stencilValue) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Clear both color and stencil buffers
    ///
    /// \param color        Fill color
    /// \param stencilValue Stencil value to clear to
    ///
    ////////////////////////////////////////////////////////////
    virtual void clear(Color color, StencilValue stencilValue) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Set the viewport rectangle
    ///
    /// \param viewport Viewport in pixels
    ///
    ////////////////////////////////////////////////////////////
    virtual void setViewport(const IntRect& viewport) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Set or disable the scissor rectangle
    ///
    /// \param scissor Scissor rectangle in pixels
    /// \param enable  Whether scissor testing is enabled
    ///
    ////////////////////////////////////////////////////////////
    virtual void setScissor(const IntRect& scissor, bool enable) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Enable or disable sRGB framebuffer conversion
    ///
    /// \param enable `true` to enable sRGB, `false` to disable
    ///
    ////////////////////////////////////////////////////////////
    virtual void setSrgb(bool enable) = 0;

    ////////////////////////////////////////////////////////////
    // State management
    ////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////
    /// \brief Apply a blending mode
    ///
    /// \param mode Blending mode to apply
    ///
    ////////////////////////////////////////////////////////////
    virtual void applyBlendMode(const BlendMode& mode) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Apply a stencil mode
    ///
    /// \param mode Stencil mode to apply
    ///
    ////////////////////////////////////////////////////////////
    virtual void applyStencilMode(const StencilMode& mode) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Set the color write mask
    ///
    /// \param enable `true` to enable color writes, `false` to disable
    ///
    ////////////////////////////////////////////////////////////
    virtual void setColorMask(bool enable) = 0;

    ////////////////////////////////////////////////////////////
    // Drawing
    ////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////
    /// \brief Enable or disable texture coordinate arrays
    ///
    /// \param enable `true` to enable, `false` to disable
    ///
    ////////////////////////////////////////////////////////////
    virtual void setTexCoordsEnabled(bool enable) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Upload vertex data from CPU memory for subsequent drawing
    ///
    /// \param vertices Pointer to vertex array
    /// \param count    Number of vertices
    ///
    ////////////////////////////////////////////////////////////
    virtual void setupVertexData(const Vertex* vertices, std::size_t count) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Bind a vertex buffer for subsequent drawing
    ///
    /// \param buffer Backend handle of the vertex buffer
    ///
    ////////////////////////////////////////////////////////////
    virtual void setupVertexBuffer(BackendBufferHandle buffer) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Set the projection and model-view transforms
    ///
    /// \param projection Projection transform (from the View)
    /// \param model      Model transform (from RenderStates)
    ///
    ////////////////////////////////////////////////////////////
    virtual void applyTransform(const Transform& projection, const Transform& model) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Draw primitives
    ///
    /// \param type        Primitive type
    /// \param firstVertex Index of the first vertex
    /// \param vertexCount Number of vertices to draw
    ///
    ////////////////////////////////////////////////////////////
    virtual void drawPrimitives(PrimitiveType type, std::size_t firstVertex, std::size_t vertexCount) = 0;

    ////////////////////////////////////////////////////////////
    // Texture operations
    ////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////
    /// \brief Create a texture
    ///
    /// \param size Size in pixels
    /// \param sRgb Whether the texture uses sRGB encoding
    ///
    /// \return Backend texture handle, or 0 on failure
    ///
    ////////////////////////////////////////////////////////////
    virtual BackendTextureHandle createTexture(Vector2u size, bool sRgb) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Destroy a texture
    ///
    /// \param handle Texture handle to destroy
    ///
    ////////////////////////////////////////////////////////////
    virtual void destroyTexture(BackendTextureHandle handle) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Update texture pixel data
    ///
    /// \param handle Texture handle
    /// \param pixels RGBA pixel data
    /// \param size   Size of the pixel region
    /// \param dest   Destination offset in the texture
    ///
    ////////////////////////////////////////////////////////////
    virtual void updateTexture(BackendTextureHandle handle,
                               const std::uint8_t*  pixels,
                               Vector2u             size,
                               Vector2u             dest) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Update texture from another texture (GPU-to-GPU copy)
    ///
    /// \param handle     Destination texture handle
    /// \param srcHandle  Source texture handle
    /// \param srcSize    Size of the source texture
    /// \param dest       Destination offset in the target texture
    ///
    ////////////////////////////////////////////////////////////
    virtual void updateTextureFromTexture(BackendTextureHandle handle,
                                          BackendTextureHandle srcHandle,
                                          Vector2u             srcSize,
                                          Vector2u             dest) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Update texture from the current framebuffer
    ///
    /// \param handle Texture handle
    /// \param size   Size of the region to copy
    /// \param dest   Destination offset in the texture
    ///
    ////////////////////////////////////////////////////////////
    virtual void updateTextureFromFramebuffer(BackendTextureHandle handle, Vector2u size, Vector2u dest) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Bind a texture for rendering
    ///
    /// \param handle         Texture handle (0 to unbind)
    /// \param coordinateType Texture coordinate interpretation
    /// \param textureSize    Public size of the texture
    /// \param actualSize     Actual (padded) size of the texture
    /// \param pixelsFlipped  Whether the texture pixels are Y-flipped
    ///
    ////////////////////////////////////////////////////////////
    virtual void bindTexture(BackendTextureHandle handle,
                             CoordinateType       coordinateType,
                             Vector2u             textureSize,
                             Vector2u             actualSize,
                             bool                 pixelsFlipped) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Read back texture pixels into an Image
    ///
    /// \param handle Texture handle
    /// \param size   Size of the texture
    ///
    /// \return Image containing the texture pixels
    ///
    ////////////////////////////////////////////////////////////
    virtual Image readbackTexture(BackendTextureHandle handle, Vector2u size) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Set texture smooth filtering
    ///
    /// \param handle Texture handle
    /// \param smooth `true` for linear, `false` for nearest
    /// \param hasMipmap Whether the texture has a mipmap
    ///
    ////////////////////////////////////////////////////////////
    virtual void setTextureSmooth(BackendTextureHandle handle, bool smooth, bool hasMipmap) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Set texture repeat mode
    ///
    /// \param handle   Texture handle
    /// \param repeated `true` for repeat, `false` for clamp
    ///
    ////////////////////////////////////////////////////////////
    virtual void setTextureRepeated(BackendTextureHandle handle, bool repeated) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Generate a mipmap for a texture
    ///
    /// \param handle Texture handle
    /// \param size   Size of the texture
    /// \param smooth Current smooth setting
    ///
    /// \return `true` if mipmap generation succeeded
    ///
    ////////////////////////////////////////////////////////////
    virtual bool generateMipmap(BackendTextureHandle handle, Vector2u size, bool smooth) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Get the maximum texture size supported by the backend
    ///
    /// \return Maximum texture size in pixels
    ///
    ////////////////////////////////////////////////////////////
    virtual unsigned int getMaxTextureSize() const = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Get a valid texture size (e.g. power-of-two padding)
    ///
    /// \param size Desired size
    ///
    /// \return Valid size (>= input)
    ///
    ////////////////////////////////////////////////////////////
    virtual unsigned int getValidTextureSize(unsigned int size) const = 0;

    ////////////////////////////////////////////////////////////
    // Shader operations
    ////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////
    /// \brief Compile a shader program from GLSL source
    ///
    /// Any of the source parameters may be empty to skip that stage.
    ///
    /// \param vertexShaderCode   Vertex shader GLSL source
    /// \param geometryShaderCode Geometry shader GLSL source
    /// \param fragmentShaderCode Fragment shader GLSL source
    ///
    /// \return Backend shader handle, or 0 on failure
    ///
    ////////////////////////////////////////////////////////////
    virtual BackendShaderHandle compileShader(std::string_view vertexShaderCode,
                                              std::string_view geometryShaderCode,
                                              std::string_view fragmentShaderCode) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Destroy a shader program
    ///
    /// \param handle Shader handle to destroy
    ///
    ////////////////////////////////////////////////////////////
    virtual void destroyShader(BackendShaderHandle handle) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Bind a shader for rendering
    ///
    /// \param handle Shader handle (0 to unbind)
    ///
    ////////////////////////////////////////////////////////////
    virtual void bindShader(BackendShaderHandle handle) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Get the location of a uniform variable
    ///
    /// \param handle Shader handle
    /// \param name   Uniform name
    ///
    /// \return Location ID, or -1 if not found
    ///
    ////////////////////////////////////////////////////////////
    virtual int getUniformLocation(BackendShaderHandle handle, const std::string& name) = 0;

    ////////////////////////////////////////////////////////////
    // Uniform setters
    ////////////////////////////////////////////////////////////

    virtual void setUniform(BackendShaderHandle handle, int location, float x) = 0;
    virtual void setUniform(BackendShaderHandle handle, int location, const Glsl::Vec2& v) = 0;
    virtual void setUniform(BackendShaderHandle handle, int location, const Glsl::Vec3& v) = 0;
    virtual void setUniform(BackendShaderHandle handle, int location, const Glsl::Vec4& v) = 0;
    virtual void setUniform(BackendShaderHandle handle, int location, int x) = 0;
    virtual void setUniform(BackendShaderHandle handle, int location, const Glsl::Ivec2& v) = 0;
    virtual void setUniform(BackendShaderHandle handle, int location, const Glsl::Ivec3& v) = 0;
    virtual void setUniform(BackendShaderHandle handle, int location, const Glsl::Ivec4& v) = 0;
    virtual void setUniform(BackendShaderHandle handle, int location, bool x) = 0;
    virtual void setUniform(BackendShaderHandle handle, int location, const Glsl::Bvec2& v) = 0;
    virtual void setUniform(BackendShaderHandle handle, int location, const Glsl::Bvec3& v) = 0;
    virtual void setUniform(BackendShaderHandle handle, int location, const Glsl::Bvec4& v) = 0;
    virtual void setUniform(BackendShaderHandle handle, int location, const Glsl::Mat3& m) = 0;
    virtual void setUniform(BackendShaderHandle handle, int location, const Glsl::Mat4& m) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Set a texture sampler uniform
    ///
    /// \param handle       Shader handle
    /// \param location     Uniform location
    /// \param textureHandle Backend handle of the texture to bind
    /// \param textureUnit  Texture unit index to bind to
    ///
    ////////////////////////////////////////////////////////////
    virtual void setUniformTexture(BackendShaderHandle  handle,
                                   int                  location,
                                   BackendTextureHandle textureHandle,
                                   int                  textureUnit) = 0;

    ////////////////////////////////////////////////////////////
    // Uniform array setters
    ////////////////////////////////////////////////////////////

    virtual void setUniformArray(BackendShaderHandle handle, int location, const float* data, std::size_t length) = 0;
    virtual void setUniformArray(BackendShaderHandle handle, int location, const Glsl::Vec2* data, std::size_t length) = 0;
    virtual void setUniformArray(BackendShaderHandle handle, int location, const Glsl::Vec3* data, std::size_t length) = 0;
    virtual void setUniformArray(BackendShaderHandle handle, int location, const Glsl::Vec4* data, std::size_t length) = 0;
    virtual void setUniformArray(BackendShaderHandle handle, int location, const Glsl::Mat3* data, std::size_t length) = 0;
    virtual void setUniformArray(BackendShaderHandle handle, int location, const Glsl::Mat4* data, std::size_t length) = 0;

    ////////////////////////////////////////////////////////////
    // Vertex buffer operations
    ////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////
    /// \brief Create a vertex buffer
    ///
    /// \param vertexCount Number of vertices to allocate
    /// \param usage       Usage hint
    ///
    /// \return Backend buffer handle, or 0 on failure
    ///
    ////////////////////////////////////////////////////////////
    virtual BackendBufferHandle createBuffer(std::size_t vertexCount, VertexBuffer::Usage usage) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Destroy a vertex buffer
    ///
    /// \param handle Buffer handle to destroy
    ///
    ////////////////////////////////////////////////////////////
    virtual void destroyBuffer(BackendBufferHandle handle) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Update vertex buffer data
    ///
    /// \param handle   Buffer handle
    /// \param vertices Vertex data to upload
    /// \param count    Number of vertices
    /// \param offset   Offset in vertices from the start of the buffer
    ///
    /// \return `true` if update was successful
    ///
    ////////////////////////////////////////////////////////////
    virtual bool updateBuffer(BackendBufferHandle handle,
                              const Vertex*       vertices,
                              std::size_t         count,
                              unsigned int        offset) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Copy vertex buffer data (GPU-to-GPU)
    ///
    /// \param destHandle   Destination buffer handle
    /// \param srcHandle    Source buffer handle
    /// \param srcSize      Number of vertices in the source buffer
    ///
    /// \return `true` if copy was successful
    ///
    ////////////////////////////////////////////////////////////
    virtual bool copyBuffer(BackendBufferHandle destHandle,
                            BackendBufferHandle srcHandle,
                            std::size_t         srcSize) = 0;

    ////////////////////////////////////////////////////////////
    // Framebuffer operations (for RenderTexture)
    ////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////
    /// \brief Create a framebuffer for render-to-texture
    ///
    /// \param size      Framebuffer size
    /// \param texture   Color attachment texture handle
    /// \param settings  Context settings (depth/stencil bits, etc.)
    ///
    /// \return Backend framebuffer handle, or 0 on failure
    ///
    ////////////////////////////////////////////////////////////
    virtual BackendFramebufferHandle createFramebuffer(Vector2u               size,
                                                       BackendTextureHandle   texture,
                                                       const ContextSettings& settings) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Destroy a framebuffer
    ///
    /// \param handle Framebuffer handle to destroy
    ///
    ////////////////////////////////////////////////////////////
    virtual void destroyFramebuffer(BackendFramebufferHandle handle) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Bind a framebuffer as the current render target
    ///
    /// \param handle Framebuffer handle (0 to bind the default framebuffer)
    ///
    /// \return `true` if binding was successful
    ///
    ////////////////////////////////////////////////////////////
    virtual bool bindFramebuffer(BackendFramebufferHandle handle) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Check if a framebuffer uses sRGB encoding
    ///
    /// \param handle Framebuffer handle
    ///
    /// \return `true` if sRGB, `false` otherwise
    ///
    ////////////////////////////////////////////////////////////
    virtual bool isFramebufferSrgb(BackendFramebufferHandle handle) const = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Update the framebuffer's texture attachment
    ///
    /// Called after the texture is resized, etc.
    ///
    /// \param handle    Framebuffer handle
    /// \param texture   New texture handle
    ///
    ////////////////////////////////////////////////////////////
    virtual void updateFramebufferTexture(BackendFramebufferHandle handle, BackendTextureHandle texture) = 0;

    ////////////////////////////////////////////////////////////
    // Capability queries
    ////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////
    /// \brief Check if shaders are supported
    ///
    /// \return `true` if shaders are available
    ///
    ////////////////////////////////////////////////////////////
    virtual bool isShaderAvailable() const = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Check if geometry shaders are supported
    ///
    /// \return `true` if geometry shaders are available
    ///
    ////////////////////////////////////////////////////////////
    virtual bool isGeometryShaderAvailable() const = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Check if vertex buffers are supported
    ///
    /// \return `true` if vertex buffers are available
    ///
    ////////////////////////////////////////////////////////////
    virtual bool isVertexBufferAvailable() const = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Check if non-power-of-two textures are supported
    ///
    /// \return `true` if NPOT textures are natively supported
    ///
    ////////////////////////////////////////////////////////////
    virtual bool isNonPowerOfTwoTextureSupported() const = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Get the maximum number of texture units available
    ///
    /// \return Maximum number of combined texture image units
    ///
    ////////////////////////////////////////////////////////////
    virtual std::size_t getMaxTextureUnits() const = 0;

    ////////////////////////////////////////////////////////////
    // Pipeline operations
    ////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////
    /// \brief Flush the graphics pipeline
    ///
    /// Ensures all previously issued commands are submitted to the GPU.
    ///
    ////////////////////////////////////////////////////////////
    virtual void flushPipeline() = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Save the entire graphics state
    ///
    /// Used when mixing SFML rendering with direct backend API usage.
    ///
    ////////////////////////////////////////////////////////////
    virtual void pushGLStates() = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Restore a previously saved graphics state
    ///
    ////////////////////////////////////////////////////////////
    virtual void popGLStates() = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Bind a vertex buffer object (without setting vertex pointers)
    ///
    /// \param buffer Backend buffer handle (0 to unbind)
    ///
    ////////////////////////////////////////////////////////////
    virtual void bindBuffer(BackendBufferHandle buffer) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Get the default framebuffer binding for the current context
    ///
    /// On most platforms this is 0, but on iOS it can be non-zero.
    ///
    /// \return Default framebuffer identifier
    ///
    ////////////////////////////////////////////////////////////
    virtual unsigned int getDefaultFramebufferBinding() const = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Check if framebuffer objects are supported
    ///
    /// \return `true` if FBOs are available
    ///
    ////////////////////////////////////////////////////////////
    virtual bool isFramebufferAvailable() const = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Get the maximum anti-aliasing level for render textures
    ///
    /// \return Maximum anti-aliasing sample count
    ///
    ////////////////////////////////////////////////////////////
    virtual unsigned int getMaxAntiAliasingLevel() const = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Check if sRGB texture encoding is supported
    ///
    /// \return `true` if sRGB textures are available
    ///
    ////////////////////////////////////////////////////////////
    virtual bool isSrgbTextureAvailable() const = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Copy vertex buffer data via map/unmap fallback
    ///
    /// \param destHandle   Destination buffer handle
    /// \param srcHandle    Source buffer handle
    /// \param srcSize      Number of vertices in the source buffer
    ///
    /// \return `true` if copy was successful
    ///
    ////////////////////////////////////////////////////////////
    virtual bool copyBufferFallback(BackendBufferHandle destHandle,
                                    BackendBufferHandle srcHandle,
                                    std::size_t         srcSize) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Prepare for setting uniforms on a shader
    ///
    /// Binds the shader program for uniform updates. Must be
    /// paired with finalizeUniformUpdate().
    ///
    /// \param handle Shader handle
    ///
    ////////////////////////////////////////////////////////////
    virtual void prepareUniformUpdate(BackendShaderHandle handle) = 0;

    ////////////////////////////////////////////////////////////
    /// \brief Finalize uniform updates on a shader
    ///
    /// Restores the previously active shader program.
    ///
    ////////////////////////////////////////////////////////////
    virtual void finalizeUniformUpdate() = 0;

protected:
    ////////////////////////////////////////////////////////////
    /// \brief Default constructor
    ///
    ////////////////////////////////////////////////////////////
    GraphicsBackend() = default;
};

} // namespace priv

} // namespace sf

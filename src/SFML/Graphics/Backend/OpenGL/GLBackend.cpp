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
#include <SFML/Graphics/Backend/OpenGL/GLBackend.hpp>
#include <SFML/Graphics/GLCheck.hpp>
#include <SFML/Graphics/GLExtensions.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Transform.hpp>

#include <SFML/Window/Context.hpp>
#include <SFML/Window/ContextSettings.hpp>

#include <SFML/System/EnumArray.hpp>
#include <SFML/System/Err.hpp>

#include <algorithm>
#include <ostream>

#include <cassert>
#include <cstring>

#ifndef SFML_OPENGL_ES

#if defined(SFML_SYSTEM_MACOS) || defined(SFML_SYSTEM_IOS)

#define castToGlHandle(x)   reinterpret_cast<GLEXT_GLhandle>(std::ptrdiff_t{x})
#define castFromGlHandle(x) static_cast<unsigned int>(reinterpret_cast<std::ptrdiff_t>(x))

#else

#define castToGlHandle(x)   (x)
#define castFromGlHandle(x) (x)

#endif

#endif // SFML_OPENGL_ES


namespace
{
////////////////////////////////////////////////////////////
// GL enum conversion helpers
////////////////////////////////////////////////////////////

std::uint32_t factorToGlConstant(sf::BlendMode::Factor blendFactor)
{
    // clang-format off
    switch (blendFactor)
    {
        case sf::BlendMode::Factor::Zero:             return GL_ZERO;
        case sf::BlendMode::Factor::One:              return GL_ONE;
        case sf::BlendMode::Factor::SrcColor:         return GL_SRC_COLOR;
        case sf::BlendMode::Factor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
        case sf::BlendMode::Factor::DstColor:         return GL_DST_COLOR;
        case sf::BlendMode::Factor::OneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
        case sf::BlendMode::Factor::SrcAlpha:         return GL_SRC_ALPHA;
        case sf::BlendMode::Factor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        case sf::BlendMode::Factor::DstAlpha:         return GL_DST_ALPHA;
        case sf::BlendMode::Factor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
    }
    // clang-format on

    sf::err() << "Invalid value for sf::BlendMode::Factor! Fallback to sf::BlendMode::Factor::Zero." << std::endl;
    assert(false);
    return GL_ZERO;
}


std::uint32_t equationToGlConstant(sf::BlendMode::Equation blendEquation)
{
    switch (blendEquation)
    {
        case sf::BlendMode::Equation::Add:
            return GLEXT_GL_FUNC_ADD;
        case sf::BlendMode::Equation::Subtract:
            if (GLEXT_blend_subtract)
                return GLEXT_GL_FUNC_SUBTRACT;
            break;
        case sf::BlendMode::Equation::ReverseSubtract:
            if (GLEXT_blend_subtract)
                return GLEXT_GL_FUNC_REVERSE_SUBTRACT;
            break;
        case sf::BlendMode::Equation::Min:
            if (GLEXT_blend_minmax)
                return GLEXT_GL_MIN;
            break;
        case sf::BlendMode::Equation::Max:
            if (GLEXT_blend_minmax)
                return GLEXT_GL_MAX;
            break;
    }

    static bool warned = false;
    if (!warned)
    {
        sf::err() << "OpenGL extension EXT_blend_minmax or EXT_blend_subtract unavailable" << '\n'
                  << "Some blending equations will fallback to sf::BlendMode::Equation::Add" << '\n'
                  << "Ensure that hardware acceleration is enabled if available" << std::endl;
        warned = true;
    }

    return GLEXT_GL_FUNC_ADD;
}


std::uint32_t stencilOperationToGlConstant(sf::StencilUpdateOperation operation)
{
    // clang-format off
    switch (operation)
    {
        case sf::StencilUpdateOperation::Keep:      return GL_KEEP;
        case sf::StencilUpdateOperation::Zero:      return GL_ZERO;
        case sf::StencilUpdateOperation::Replace:   return GL_REPLACE;
        case sf::StencilUpdateOperation::Increment: return GL_INCR;
        case sf::StencilUpdateOperation::Decrement: return GL_DECR;
        case sf::StencilUpdateOperation::Invert:    return GL_INVERT;
    }
    // clang-format on

    sf::err() << "Invalid value for sf::StencilUpdateOperation! Fallback to sf::StencilMode::Keep." << std::endl;
    assert(false);
    return GL_KEEP;
}


std::uint32_t stencilFunctionToGlConstant(sf::StencilComparison comparison)
{
    // clang-format off
    switch (comparison)
    {
        case sf::StencilComparison::Never:        return GL_NEVER;
        case sf::StencilComparison::Less:         return GL_LESS;
        case sf::StencilComparison::LessEqual:    return GL_LEQUAL;
        case sf::StencilComparison::Greater:      return GL_GREATER;
        case sf::StencilComparison::GreaterEqual: return GL_GEQUAL;
        case sf::StencilComparison::Equal:        return GL_EQUAL;
        case sf::StencilComparison::NotEqual:     return GL_NOTEQUAL;
        case sf::StencilComparison::Always:       return GL_ALWAYS;
    }
    // clang-format on

    sf::err() << "Invalid value for sf::StencilComparison! Fallback to sf::StencilMode::Always." << std::endl;
    assert(false);
    return GL_ALWAYS;
}


std::uint32_t usageToGlConstant(sf::VertexBuffer::Usage usage)
{
    // clang-format off
    switch (usage)
    {
        case sf::VertexBuffer::Usage::Stream:  return GLEXT_GL_STREAM_DRAW;
        case sf::VertexBuffer::Usage::Dynamic: return GLEXT_GL_DYNAMIC_DRAW;
        case sf::VertexBuffer::Usage::Static:  return GLEXT_GL_STATIC_DRAW;
    }
    // clang-format on

    sf::err() << "Invalid value for sf::VertexBuffer::Usage! Fallback to sf::VertexBuffer::Usage::Stream." << std::endl;
    assert(false);
    return GLEXT_GL_STREAM_DRAW;
}

// RAII helper to save/restore the current GL_TEXTURE_BINDING_2D
struct TextureBindingSaver
{
    TextureBindingSaver()
    {
        glCheck(glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding));
    }

    ~TextureBindingSaver()
    {
        glCheck(glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(binding)));
    }

    TextureBindingSaver(const TextureBindingSaver&) = delete;
    TextureBindingSaver& operator=(const TextureBindingSaver&) = delete;

    GLint binding{};
};

} // anonymous namespace


namespace sf::priv
{

////////////////////////////////////////////////////////////
// Internal framebuffer data
////////////////////////////////////////////////////////////

struct GLBackend::GLFramebufferData
{
    struct FBO
    {
        ~FBO()
        {
            if (object)
                glCheck(GLEXT_glDeleteFramebuffers(1, &object));
        }

        GLuint object{};
    };

    using FBOMap = std::unordered_map<std::uint64_t, std::weak_ptr<FBO>>;

    FBOMap       frameBuffers;
    FBOMap       multisampleFrameBuffers;
    GLuint       depthStencilBuffer{};
    GLuint       colorBuffer{};
    GLuint       textureId{};
    Vector2u     size;
    bool         multisample{};
    bool         hasDepth{};
    bool         hasStencil{};
    bool         sRgb{};
};


////////////////////////////////////////////////////////////
GLBackend::GLBackend() = default;


////////////////////////////////////////////////////////////
GLBackend::~GLBackend() = default;


////////////////////////////////////////////////////////////
// Render target operations
////////////////////////////////////////////////////////////

void GLBackend::clear(Color color)
{
    glCheck(glClearColor(color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f));
    glCheck(glClear(GL_COLOR_BUFFER_BIT));
}


////////////////////////////////////////////////////////////
void GLBackend::clearStencil(StencilValue stencilValue)
{
    glCheck(glClearStencil(static_cast<int>(stencilValue.value)));
    glCheck(glClear(GL_STENCIL_BUFFER_BIT));
}


////////////////////////////////////////////////////////////
void GLBackend::clear(Color color, StencilValue stencilValue)
{
    glCheck(glClearColor(color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f));
    glCheck(glClearStencil(static_cast<int>(stencilValue.value)));
    glCheck(glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));
}


////////////////////////////////////////////////////////////
void GLBackend::setViewport(const IntRect& viewport)
{
    glCheck(glViewport(viewport.position.x, viewport.position.y, viewport.size.x, viewport.size.y));
}


////////////////////////////////////////////////////////////
void GLBackend::setScissor(const IntRect& scissor, bool enable)
{
    if (enable)
    {
        glCheck(glScissor(scissor.position.x, scissor.position.y, scissor.size.x, scissor.size.y));
        glCheck(glEnable(GL_SCISSOR_TEST));
    }
    else
    {
        glCheck(glDisable(GL_SCISSOR_TEST));
    }
}


////////////////////////////////////////////////////////////
void GLBackend::setSrgb(bool enable)
{
#ifndef SFML_OPENGL_ES
    if (enable)
        glCheck(glEnable(GL_FRAMEBUFFER_SRGB));
    else if (GLEXT_framebuffer_sRGB)
        glCheck(glDisable(GL_FRAMEBUFFER_SRGB));
#else
    (void)enable;
#endif
}


////////////////////////////////////////////////////////////
// State management
////////////////////////////////////////////////////////////

void GLBackend::applyBlendMode(const BlendMode& mode)
{
    if (GLEXT_blend_func_separate)
    {
        glCheck(GLEXT_glBlendFuncSeparate(factorToGlConstant(mode.colorSrcFactor),
                                          factorToGlConstant(mode.colorDstFactor),
                                          factorToGlConstant(mode.alphaSrcFactor),
                                          factorToGlConstant(mode.alphaDstFactor)));
    }
    else
    {
        glCheck(glBlendFunc(factorToGlConstant(mode.colorSrcFactor), factorToGlConstant(mode.colorDstFactor)));
    }

    if (GLEXT_blend_minmax || GLEXT_blend_subtract)
    {
        if (GLEXT_blend_equation_separate)
        {
            glCheck(GLEXT_glBlendEquationSeparate(equationToGlConstant(mode.colorEquation),
                                                  equationToGlConstant(mode.alphaEquation)));
        }
        else
        {
            glCheck(GLEXT_glBlendEquation(equationToGlConstant(mode.colorEquation)));
        }
    }
    else if ((mode.colorEquation != BlendMode::Equation::Add) || (mode.alphaEquation != BlendMode::Equation::Add))
    {
        static bool warned = false;

        if (!warned)
        {
#ifdef SFML_OPENGL_ES
            err() << "OpenGL ES extension OES_blend_subtract unavailable" << std::endl;
#else
            err() << "OpenGL extension EXT_blend_minmax and EXT_blend_subtract unavailable" << std::endl;
#endif
            err() << "Selecting a blend equation not possible" << '\n'
                  << "Ensure that hardware acceleration is enabled if available" << std::endl;

            warned = true;
        }
    }
}


////////////////////////////////////////////////////////////
void GLBackend::applyStencilMode(const StencilMode& mode)
{
    if (mode == StencilMode())
    {
        glCheck(glDisable(GL_STENCIL_TEST));
        glCheck(glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));
    }
    else
    {
        glCheck(glEnable(GL_STENCIL_TEST));
        glCheck(glStencilOp(GL_KEEP,
                            stencilOperationToGlConstant(mode.stencilUpdateOperation),
                            stencilOperationToGlConstant(mode.stencilUpdateOperation)));
        glCheck(glStencilFunc(stencilFunctionToGlConstant(mode.stencilComparison),
                              static_cast<int>(mode.stencilReference.value),
                              mode.stencilMask.value));
    }
}


////////////////////////////////////////////////////////////
void GLBackend::setColorMask(bool enable)
{
    const auto mask = enable ? GL_TRUE : GL_FALSE;
    glCheck(glColorMask(mask, mask, mask, mask));
}


////////////////////////////////////////////////////////////
// Drawing
////////////////////////////////////////////////////////////

void GLBackend::setTexCoordsEnabled(bool enable)
{
    if (enable)
        glCheck(glEnableClientState(GL_TEXTURE_COORD_ARRAY));
    else
        glCheck(glDisableClientState(GL_TEXTURE_COORD_ARRAY));
}


////////////////////////////////////////////////////////////
void GLBackend::setupVertexData(const Vertex* vertices, std::size_t count)
{
    (void)count;
    const auto* data = reinterpret_cast<const std::byte*>(vertices);

    glCheck(glVertexPointer(2, GL_FLOAT, sizeof(Vertex), data + 0));
    glCheck(glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), data + 8));
    glCheck(glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), data + 12));
}


////////////////////////////////////////////////////////////
void GLBackend::setupVertexBuffer(BackendBufferHandle buffer)
{
    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, static_cast<unsigned int>(buffer)));

    glCheck(glVertexPointer(2, GL_FLOAT, sizeof(Vertex), reinterpret_cast<const void*>(0)));
    glCheck(glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), reinterpret_cast<const void*>(8)));
    glCheck(glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), reinterpret_cast<const void*>(12)));
}


////////////////////////////////////////////////////////////
void GLBackend::applyTransform(const Transform& projection, const Transform& model)
{
    glCheck(glMatrixMode(GL_PROJECTION));
    glCheck(glLoadMatrixf(projection.getMatrix()));
    glCheck(glMatrixMode(GL_MODELVIEW));

    if (model == Transform::Identity)
        glCheck(glLoadIdentity());
    else
        glCheck(glLoadMatrixf(model.getMatrix()));
}


////////////////////////////////////////////////////////////
void GLBackend::drawPrimitives(PrimitiveType type, std::size_t firstVertex, std::size_t vertexCount)
{
    static constexpr EnumArray<PrimitiveType, GLenum, 6> modes =
        {GL_POINTS, GL_LINES, GL_LINE_STRIP, GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN};

    glCheck(glDrawArrays(modes[type], static_cast<GLint>(firstVertex), static_cast<GLsizei>(vertexCount)));
}


////////////////////////////////////////////////////////////
// Texture operations
////////////////////////////////////////////////////////////

BackendTextureHandle GLBackend::createTexture(Vector2u size, bool sRgb)
{
    const TextureBindingSaver textureSave;

    GLuint texture = 0;
    glCheck(glGenTextures(1, &texture));

    if (!texture)
    {
        err() << "Failed to create texture" << std::endl;
        return 0;
    }

    glCheck(glBindTexture(GL_TEXTURE_2D, texture));

    const GLenum internalFormat = sRgb ? GLEXT_GL_SRGB8_ALPHA8 : GL_RGBA;

    glCheck(glTexImage2D(GL_TEXTURE_2D,
                         0,
                         static_cast<GLint>(internalFormat),
                         static_cast<GLsizei>(size.x),
                         static_cast<GLsizei>(size.y),
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         nullptr));

    glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));

    return static_cast<BackendTextureHandle>(texture);
}


////////////////////////////////////////////////////////////
void GLBackend::destroyTexture(BackendTextureHandle handle)
{
    const GLuint texture = static_cast<GLuint>(handle);
    glCheck(glDeleteTextures(1, &texture));
}


////////////////////////////////////////////////////////////
void GLBackend::updateTexture(BackendTextureHandle handle,
                              const std::uint8_t*  pixels,
                              Vector2u             size,
                              Vector2u             dest)
{
    assert(handle);
    const TextureBindingSaver textureSave;
    const GLuint texture = static_cast<GLuint>(handle);

    glCheck(glBindTexture(GL_TEXTURE_2D, texture));
    glCheck(glTexSubImage2D(GL_TEXTURE_2D,
                            0,
                            static_cast<GLint>(dest.x),
                            static_cast<GLint>(dest.y),
                            static_cast<GLsizei>(size.x),
                            static_cast<GLsizei>(size.y),
                            GL_RGBA,
                            GL_UNSIGNED_BYTE,
                            pixels));
    glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));

    // Force an OpenGL flush to ensure texture data is available on all contexts
    glCheck(glFlush());
}


////////////////////////////////////////////////////////////
void GLBackend::updateTextureFromTexture(BackendTextureHandle handle,
                                         BackendTextureHandle srcHandle,
                                         Vector2u             srcSize,
                                         Vector2u             dest)
{
    const TextureBindingSaver textureSave;

#ifndef SFML_OPENGL_ES

    if (!GLEXT_copy_buffer)
    {
        // Fall back: readback + upload
        const Image image = readbackTexture(srcHandle, srcSize);
        updateTexture(handle, image.getPixelsPtr(), srcSize, dest);
        return;
    }

    // Use FBO-based copy (efficient GPU path)
    GLint readFramebuffer = 0;
    GLint drawFramebuffer = 0;
    glCheck(glGetIntegerv(GLEXT_GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer));
    glCheck(glGetIntegerv(GLEXT_GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer));

    // Create a temporary FBO for reading from source texture
    GLuint sourceFBO = 0;
    glCheck(GLEXT_glGenFramebuffers(1, &sourceFBO));
    glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_READ_FRAMEBUFFER, sourceFBO));
    glCheck(GLEXT_glFramebufferTexture2D(GLEXT_GL_READ_FRAMEBUFFER,
                                         GLEXT_GL_COLOR_ATTACHMENT0,
                                         GL_TEXTURE_2D,
                                         static_cast<GLuint>(srcHandle),
                                         0));

    // Bind dest texture and copy
    glCheck(glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(handle)));
    glCheck(glCopyTexSubImage2D(GL_TEXTURE_2D,
                                0,
                                static_cast<GLint>(dest.x),
                                static_cast<GLint>(dest.y),
                                0,
                                0,
                                static_cast<GLsizei>(srcSize.x),
                                static_cast<GLsizei>(srcSize.y)));

    // Restore
    glCheck(GLEXT_glDeleteFramebuffers(1, &sourceFBO));
    glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFramebuffer)));
    glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFramebuffer)));

#else

    // On OpenGL ES, fall back to readback + upload
    const Image image = readbackTexture(srcHandle, srcSize);
    updateTexture(handle, image.getPixelsPtr(), srcSize, dest);

#endif
}


////////////////////////////////////////////////////////////
void GLBackend::updateTextureFromFramebuffer(BackendTextureHandle handle, Vector2u size, Vector2u dest)
{
    assert(handle);
    const TextureBindingSaver textureSave;
    glCheck(glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(handle)));
    glCheck(glCopyTexSubImage2D(GL_TEXTURE_2D,
                                0,
                                static_cast<GLint>(dest.x),
                                static_cast<GLint>(dest.y),
                                0,
                                0,
                                static_cast<GLsizei>(size.x),
                                static_cast<GLsizei>(size.y)));
}


////////////////////////////////////////////////////////////
void GLBackend::bindTexture(BackendTextureHandle handle,
                            CoordinateType       coordinateType,
                            Vector2u             textureSize,
                            Vector2u             actualSize,
                            bool                 pixelsFlipped)
{
    if (handle)
    {
        glCheck(glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(handle)));

        // Check if we need a special texture matrix
        if ((coordinateType == CoordinateType::Pixels) || pixelsFlipped ||
            ((coordinateType == CoordinateType::Normalized) && (textureSize != actualSize)))
        {
            // clang-format off
            float matrix[16] = {1.f, 0.f, 0.f, 0.f,
                                0.f, 1.f, 0.f, 0.f,
                                0.f, 0.f, 1.f, 0.f,
                                0.f, 0.f, 0.f, 1.f};
            // clang-format on

            // Pixel coordinates: scale [0..size] to [0..1]
            if (coordinateType == CoordinateType::Pixels)
            {
                matrix[0] = 1.f / static_cast<float>(actualSize.x);
                matrix[5] = 1.f / static_cast<float>(actualSize.y);
            }

            // Normalized coords with NPOT padding: scale to actual/padded ratio
            if ((coordinateType == CoordinateType::Normalized) && (textureSize != actualSize))
            {
                matrix[0] = static_cast<float>(textureSize.x) / static_cast<float>(actualSize.x);
                matrix[5] = static_cast<float>(textureSize.y) / static_cast<float>(actualSize.y);
            }

            // Flip Y axis if pixels are flipped
            if (pixelsFlipped)
            {
                matrix[5]  = -matrix[5];
                matrix[13] = static_cast<float>(textureSize.y) / static_cast<float>(actualSize.y);
            }

            glCheck(glMatrixMode(GL_TEXTURE));
            glCheck(glLoadMatrixf(matrix));
        }
        else
        {
            glCheck(glMatrixMode(GL_TEXTURE));
            glCheck(glLoadIdentity());
        }

        glCheck(glMatrixMode(GL_MODELVIEW));
    }
    else
    {
        glCheck(glBindTexture(GL_TEXTURE_2D, 0));
        glCheck(glMatrixMode(GL_TEXTURE));
        glCheck(glLoadIdentity());
        glCheck(glMatrixMode(GL_MODELVIEW));
    }
}


////////////////////////////////////////////////////////////
Image GLBackend::readbackTexture(BackendTextureHandle handle, Vector2u size)
{
    Image image;
    if (!handle)
        return image;

    const TextureBindingSaver textureSave;

#ifndef SFML_OPENGL_ES

    image.resize(size);

    // Make sure we're not reading from an FBO - save & restore binding
    GLint textureBinding = 0;
    glCheck(glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureBinding));

    glCheck(glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(handle)));
    glCheck(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, const_cast<std::uint8_t*>(image.getPixelsPtr())));

    glCheck(glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textureBinding)));

#else

    // OpenGL ES doesn't have glGetTexImage; use FBO readback instead
    GLint previousFrameBuffer = 0;
    glCheck(glGetIntegerv(GLEXT_GL_FRAMEBUFFER_BINDING, &previousFrameBuffer));

    GLuint frameBuffer = 0;
    glCheck(GLEXT_glGenFramebuffers(1, &frameBuffer));
    glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_FRAMEBUFFER, frameBuffer));
    glCheck(GLEXT_glFramebufferTexture2D(GLEXT_GL_FRAMEBUFFER,
                                         GLEXT_GL_COLOR_ATTACHMENT0,
                                         GL_TEXTURE_2D,
                                         static_cast<GLuint>(handle),
                                         0));

    image.resize(size);
    glCheck(glReadPixels(0,
                         0,
                         static_cast<GLsizei>(size.x),
                         static_cast<GLsizei>(size.y),
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         const_cast<std::uint8_t*>(image.getPixelsPtr())));

    glCheck(GLEXT_glDeleteFramebuffers(1, &frameBuffer));
    glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_FRAMEBUFFER, static_cast<GLuint>(previousFrameBuffer)));

#endif

    return image;
}


////////////////////////////////////////////////////////////
void GLBackend::setTextureSmooth(BackendTextureHandle handle, bool smooth, bool hasMipmap)
{
    const TextureBindingSaver textureSave;
    glCheck(glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(handle)));
    glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, smooth ? GL_LINEAR : GL_NEAREST));

    if (hasMipmap)
    {
        glCheck(glTexParameteri(GL_TEXTURE_2D,
                                GL_TEXTURE_MIN_FILTER,
                                smooth ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR));
    }
    else
    {
        glCheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, smooth ? GL_LINEAR : GL_NEAREST));
    }
}


////////////////////////////////////////////////////////////
void GLBackend::setTextureRepeated(BackendTextureHandle handle, bool repeated)
{
    const TextureBindingSaver textureSave;
    glCheck(glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(handle)));
    glCheck(
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeated ? GL_REPEAT : GL_CLAMP_TO_EDGE));
    glCheck(
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeated ? GL_REPEAT : GL_CLAMP_TO_EDGE));
}


////////////////////////////////////////////////////////////
bool GLBackend::generateMipmap(BackendTextureHandle handle, Vector2u size, bool smooth)
{
    (void)size;
    const TextureBindingSaver textureSave;

    if (!GLEXT_framebuffer_object)
        return false;

    glCheck(glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(handle)));
    glCheck(GLEXT_glGenerateMipmap(GL_TEXTURE_2D));
    glCheck(glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_MIN_FILTER,
                            smooth ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR));

    return true;
}


////////////////////////////////////////////////////////////
unsigned int GLBackend::getMaxTextureSize() const
{
    ensureExtensionsInit();
    static unsigned int maxSize = 0;

    if (maxSize == 0)
    {
        GLint size = 0;
        glCheck(glGetIntegerv(GL_MAX_TEXTURE_SIZE, &size));
        maxSize = static_cast<unsigned int>(size);
    }

    return maxSize;
}


////////////////////////////////////////////////////////////
unsigned int GLBackend::getValidTextureSize(unsigned int size) const
{
    if (isNonPowerOfTwoTextureSupported())
    {
        // If hardware supports NPOT, any size is valid (but clamp to max)
        return std::min(size, getMaxTextureSize());
    }

    // Otherwise round up to the next power of two
    unsigned int powerOfTwo = 1;
    while (powerOfTwo < size)
        powerOfTwo *= 2;

    return powerOfTwo;
}


////////////////////////////////////////////////////////////
// Shader operations
////////////////////////////////////////////////////////////

BackendShaderHandle GLBackend::compileShader(std::string_view vertexShaderCode,
                                             std::string_view geometryShaderCode,
                                             std::string_view fragmentShaderCode)
{
#ifndef SFML_OPENGL_ES

    ensureExtensionsInit();

    // Create the program
    const GLEXT_GLhandle shaderProgram = glCheck(GLEXT_glCreateProgramObject());

    auto compileStage = [&](std::string_view source, GLenum shaderType) -> bool
    {
        if (source.empty())
            return true;

        const GLEXT_GLhandle shader  = glCheck(GLEXT_glCreateShaderObject(shaderType));
        const auto*          srcPtr  = source.data();
        const auto           srcLen  = static_cast<GLint>(source.size());
        glCheck(GLEXT_glShaderSource(shader, 1, &srcPtr, &srcLen));
        glCheck(GLEXT_glCompileShader(shader));

        GLint success = 0;
        glCheck(GLEXT_glGetObjectParameteriv(shader, GLEXT_GL_OBJECT_COMPILE_STATUS, &success));

        if (success == GL_FALSE)
        {
            char log[1024];
            glCheck(GLEXT_glGetInfoLog(shader, sizeof(log), nullptr, log));
            err() << "Failed to compile shader:" << '\n' << log << std::endl;
            glCheck(GLEXT_glDeleteObject(shader));
            glCheck(GLEXT_glDeleteObject(shaderProgram));
            return false;
        }

        glCheck(GLEXT_glAttachObject(shaderProgram, shader));
        glCheck(GLEXT_glDeleteObject(shader));
        return true;
    };

    if (!compileStage(vertexShaderCode, GLEXT_GL_VERTEX_SHADER))
        return 0;
    if (!compileStage(geometryShaderCode, GLEXT_GL_GEOMETRY_SHADER))
        return 0;
    if (!compileStage(fragmentShaderCode, GLEXT_GL_FRAGMENT_SHADER))
        return 0;

    // Link the program
    glCheck(GLEXT_glLinkProgram(shaderProgram));

    GLint success = 0;
    glCheck(GLEXT_glGetObjectParameteriv(shaderProgram, GLEXT_GL_OBJECT_LINK_STATUS, &success));

    if (success == GL_FALSE)
    {
        char log[1024];
        glCheck(GLEXT_glGetInfoLog(shaderProgram, sizeof(log), nullptr, log));
        err() << "Failed to link shader:" << '\n' << log << std::endl;
        glCheck(GLEXT_glDeleteObject(shaderProgram));
        return 0;
    }

    return reinterpret_cast<BackendShaderHandle>(shaderProgram);

#else

    (void)vertexShaderCode;
    (void)geometryShaderCode;
    (void)fragmentShaderCode;
    return 0;

#endif
}


////////////////////////////////////////////////////////////
void GLBackend::destroyShader(BackendShaderHandle handle)
{
#ifndef SFML_OPENGL_ES
    if (handle)
        glCheck(GLEXT_glDeleteObject(reinterpret_cast<GLEXT_GLhandle>(handle)));
#else
    (void)handle;
#endif
}


////////////////////////////////////////////////////////////
void GLBackend::bindShader(BackendShaderHandle handle)
{
#ifndef SFML_OPENGL_ES
    glCheck(GLEXT_glUseProgramObject(reinterpret_cast<GLEXT_GLhandle>(handle)));
#else
    (void)handle;
#endif
}


////////////////////////////////////////////////////////////
int GLBackend::getUniformLocation(BackendShaderHandle handle, const std::string& name)
{
#ifndef SFML_OPENGL_ES
    return glCheck(GLEXT_glGetUniformLocation(reinterpret_cast<GLEXT_GLhandle>(handle), name.c_str()));
#else
    (void)handle;
    (void)name;
    return -1;
#endif
}


////////////////////////////////////////////////////////////
// Uniform setters
////////////////////////////////////////////////////////////

void GLBackend::setUniform(BackendShaderHandle handle, int location, float x)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniform1f(location, x));
#else
    (void)handle;
    (void)location;
    (void)x;
#endif
}

void GLBackend::setUniform(BackendShaderHandle handle, int location, const Glsl::Vec2& v)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniform2f(location, v.x, v.y));
#else
    (void)handle;
    (void)location;
    (void)v;
#endif
}

void GLBackend::setUniform(BackendShaderHandle handle, int location, const Glsl::Vec3& v)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniform3f(location, v.x, v.y, v.z));
#else
    (void)handle;
    (void)location;
    (void)v;
#endif
}

void GLBackend::setUniform(BackendShaderHandle handle, int location, const Glsl::Vec4& v)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniform4f(location, v.x, v.y, v.z, v.w));
#else
    (void)handle;
    (void)location;
    (void)v;
#endif
}

void GLBackend::setUniform(BackendShaderHandle handle, int location, int x)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniform1i(location, x));
#else
    (void)handle;
    (void)location;
    (void)x;
#endif
}

void GLBackend::setUniform(BackendShaderHandle handle, int location, const Glsl::Ivec2& v)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniform2i(location, static_cast<int>(v.x), static_cast<int>(v.y)));
#else
    (void)handle;
    (void)location;
    (void)v;
#endif
}

void GLBackend::setUniform(BackendShaderHandle handle, int location, const Glsl::Ivec3& v)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniform3i(location, static_cast<int>(v.x), static_cast<int>(v.y), static_cast<int>(v.z)));
#else
    (void)handle;
    (void)location;
    (void)v;
#endif
}

void GLBackend::setUniform(BackendShaderHandle handle, int location, const Glsl::Ivec4& v)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniform4i(location,
                              static_cast<int>(v.x),
                              static_cast<int>(v.y),
                              static_cast<int>(v.z),
                              static_cast<int>(v.w)));
#else
    (void)handle;
    (void)location;
    (void)v;
#endif
}

void GLBackend::setUniform(BackendShaderHandle handle, int location, bool x)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniform1i(location, static_cast<int>(x)));
#else
    (void)handle;
    (void)location;
    (void)x;
#endif
}

void GLBackend::setUniform(BackendShaderHandle handle, int location, const Glsl::Bvec2& v)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniform2i(location, static_cast<int>(v.x), static_cast<int>(v.y)));
#else
    (void)handle;
    (void)location;
    (void)v;
#endif
}

void GLBackend::setUniform(BackendShaderHandle handle, int location, const Glsl::Bvec3& v)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniform3i(location, static_cast<int>(v.x), static_cast<int>(v.y), static_cast<int>(v.z)));
#else
    (void)handle;
    (void)location;
    (void)v;
#endif
}

void GLBackend::setUniform(BackendShaderHandle handle, int location, const Glsl::Bvec4& v)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniform4i(location,
                              static_cast<int>(v.x),
                              static_cast<int>(v.y),
                              static_cast<int>(v.z),
                              static_cast<int>(v.w)));
#else
    (void)handle;
    (void)location;
    (void)v;
#endif
}

void GLBackend::setUniform(BackendShaderHandle handle, int location, const Glsl::Mat3& m)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniformMatrix3fv(location, 1, GL_FALSE, m.array.data()));
#else
    (void)handle;
    (void)location;
    (void)m;
#endif
}

void GLBackend::setUniform(BackendShaderHandle handle, int location, const Glsl::Mat4& m)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniformMatrix4fv(location, 1, GL_FALSE, m.array.data()));
#else
    (void)handle;
    (void)location;
    (void)m;
#endif
}

void GLBackend::setUniformTexture(BackendShaderHandle  handle,
                                  int                  location,
                                  BackendTextureHandle textureHandle,
                                  int                  textureUnit)
{
#ifndef SFML_OPENGL_ES
    (void)handle;

    // Activate the texture unit and bind the texture
    glCheck(GLEXT_glActiveTexture(GLEXT_GL_TEXTURE0 + static_cast<GLenum>(textureUnit)));
    glCheck(glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textureHandle)));

    // Set the sampler uniform to the texture unit index
    glCheck(GLEXT_glUniform1i(location, textureUnit));

    // Reset active texture unit to 0
    glCheck(GLEXT_glActiveTexture(GLEXT_GL_TEXTURE0));
#else
    (void)handle;
    (void)location;
    (void)textureHandle;
    (void)textureUnit;
#endif
}


////////////////////////////////////////////////////////////
// Uniform array setters
////////////////////////////////////////////////////////////

void GLBackend::setUniformArray(BackendShaderHandle handle, int location, const float* data, std::size_t length)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniform1fv(location, static_cast<GLsizei>(length), data));
#else
    (void)handle;
    (void)location;
    (void)data;
    (void)length;
#endif
}

void GLBackend::setUniformArray(BackendShaderHandle handle, int location, const Glsl::Vec2* data, std::size_t length)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniform2fv(location, static_cast<GLsizei>(length), &data[0].x));
#else
    (void)handle;
    (void)location;
    (void)data;
    (void)length;
#endif
}

void GLBackend::setUniformArray(BackendShaderHandle handle, int location, const Glsl::Vec3* data, std::size_t length)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniform3fv(location, static_cast<GLsizei>(length), &data[0].x));
#else
    (void)handle;
    (void)location;
    (void)data;
    (void)length;
#endif
}

void GLBackend::setUniformArray(BackendShaderHandle handle, int location, const Glsl::Vec4* data, std::size_t length)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniform4fv(location, static_cast<GLsizei>(length), &data[0].x));
#else
    (void)handle;
    (void)location;
    (void)data;
    (void)length;
#endif
}

void GLBackend::setUniformArray(BackendShaderHandle handle, int location, const Glsl::Mat3* data, std::size_t length)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniformMatrix3fv(location, static_cast<GLsizei>(length), GL_FALSE, data[0].array.data()));
#else
    (void)handle;
    (void)location;
    (void)data;
    (void)length;
#endif
}

void GLBackend::setUniformArray(BackendShaderHandle handle, int location, const Glsl::Mat4* data, std::size_t length)
{
#ifndef SFML_OPENGL_ES
    (void)handle;
    glCheck(GLEXT_glUniformMatrix4fv(location, static_cast<GLsizei>(length), GL_FALSE, data[0].array.data()));
#else
    (void)handle;
    (void)location;
    (void)data;
    (void)length;
#endif
}


////////////////////////////////////////////////////////////
// Vertex buffer operations
////////////////////////////////////////////////////////////

BackendBufferHandle GLBackend::createBuffer(std::size_t vertexCount, VertexBuffer::Usage usage)
{
    GLuint buffer = 0;
    glCheck(GLEXT_glGenBuffers(1, &buffer));

    if (!buffer)
        return 0;

    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, buffer));
    glCheck(GLEXT_glBufferData(GLEXT_GL_ARRAY_BUFFER,
                               static_cast<GLsizeiptr>(sizeof(Vertex) * vertexCount),
                               nullptr,
                               usageToGlConstant(usage)));
    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, 0));

    return static_cast<BackendBufferHandle>(buffer);
}


////////////////////////////////////////////////////////////
void GLBackend::destroyBuffer(BackendBufferHandle handle)
{
    const GLuint buffer = static_cast<GLuint>(handle);
    glCheck(GLEXT_glDeleteBuffers(1, &buffer));
}


////////////////////////////////////////////////////////////
bool GLBackend::updateBuffer(BackendBufferHandle handle,
                             const Vertex*       vertices,
                             std::size_t         count,
                             unsigned int        offset)
{
    if (!handle || !vertices)
        return false;

    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, static_cast<GLuint>(handle)));

    // If offset is 0 and we're updating the full buffer, orphan and reallocate
    if (offset == 0)
    {
        glCheck(GLEXT_glBufferData(GLEXT_GL_ARRAY_BUFFER,
                                   static_cast<GLsizeiptr>(sizeof(Vertex) * count),
                                   nullptr,
                                   GLEXT_GL_STREAM_DRAW));
    }

    glCheck(GLEXT_glBufferSubData(GLEXT_GL_ARRAY_BUFFER,
                                  static_cast<GLintptr>(sizeof(Vertex) * offset),
                                  static_cast<GLsizeiptr>(sizeof(Vertex) * count),
                                  vertices));

    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, 0));

    return true;
}


////////////////////////////////////////////////////////////
bool GLBackend::copyBuffer(BackendBufferHandle destHandle,
                           BackendBufferHandle srcHandle,
                           std::size_t         srcSize)
{
#ifndef SFML_OPENGL_ES

    if (GLEXT_copy_buffer)
    {
        glCheck(GLEXT_glBindBuffer(GLEXT_GL_COPY_READ_BUFFER, static_cast<GLuint>(srcHandle)));
        glCheck(GLEXT_glBindBuffer(GLEXT_GL_COPY_WRITE_BUFFER, static_cast<GLuint>(destHandle)));
        glCheck(GLEXT_glCopyBufferSubData(GLEXT_GL_COPY_READ_BUFFER,
                                          GLEXT_GL_COPY_WRITE_BUFFER,
                                          0,
                                          0,
                                          static_cast<GLsizeiptr>(sizeof(Vertex) * srcSize)));
        glCheck(GLEXT_glBindBuffer(GLEXT_GL_COPY_READ_BUFFER, 0));
        glCheck(GLEXT_glBindBuffer(GLEXT_GL_COPY_WRITE_BUFFER, 0));
        return true;
    }

#endif

    (void)destHandle;
    (void)srcHandle;
    (void)srcSize;
    return false;
}


////////////////////////////////////////////////////////////
// Framebuffer operations
////////////////////////////////////////////////////////////

BackendFramebufferHandle GLBackend::createFramebuffer(Vector2u               size,
                                                      BackendTextureHandle   texture,
                                                      const ContextSettings& settings)
{
    ensureExtensionsInit();

    if (!GLEXT_framebuffer_object)
        return 0;

    if (settings.antiAliasingLevel && !(GLEXT_framebuffer_multisample && GLEXT_framebuffer_blit))
        return 0;

    auto data  = std::make_unique<GLFramebufferData>();
    data->size      = size;
    data->textureId = static_cast<GLuint>(texture);
    data->sRgb      = settings.sRgbCapable && GL_EXT_texture_sRGB;

#ifndef SFML_OPENGL_ES

    // Check if the requested anti-aliasing level is supported
    if (settings.antiAliasingLevel)
    {
        GLint samples = 0;
        glCheck(glGetIntegerv(GLEXT_GL_MAX_SAMPLES, &samples));

        if (settings.antiAliasingLevel > static_cast<unsigned int>(samples))
        {
            err() << "Impossible to create render texture (unsupported anti-aliasing level)"
                  << " Requested: " << settings.antiAliasingLevel << " Maximum supported: " << samples << std::endl;
            return 0;
        }
    }

#endif

    if (!settings.antiAliasingLevel)
    {
        // Create the depth/stencil buffer if requested
        if (settings.stencilBits && settings.depthBits)
        {
            if (!GLEXT_packed_depth_stencil)
            {
                err() << "Impossible to create render texture (combined depth/stencil buffer not supported)" << std::endl;
                return 0;
            }

            GLuint depthStencil = 0;
            glCheck(GLEXT_glGenRenderbuffers(1, &depthStencil));
            data->depthStencilBuffer = depthStencil;
            if (!data->depthStencilBuffer)
            {
                err() << "Impossible to create render texture (failed to create the attached depth/stencil buffer)"
                      << std::endl;
                return 0;
            }
            glCheck(GLEXT_glBindRenderbuffer(GLEXT_GL_RENDERBUFFER, data->depthStencilBuffer));
            glCheck(GLEXT_glRenderbufferStorage(GLEXT_GL_RENDERBUFFER,
                                                GLEXT_GL_DEPTH24_STENCIL8,
                                                static_cast<GLsizei>(size.x),
                                                static_cast<GLsizei>(size.y)));

            data->hasDepth   = true;
            data->hasStencil = true;
        }
        else if (settings.depthBits)
        {
            GLuint depthStencil = 0;
            glCheck(GLEXT_glGenRenderbuffers(1, &depthStencil));
            data->depthStencilBuffer = depthStencil;
            if (!data->depthStencilBuffer)
            {
                err() << "Impossible to create render texture (failed to create the attached depth buffer)" << std::endl;
                return 0;
            }
            glCheck(GLEXT_glBindRenderbuffer(GLEXT_GL_RENDERBUFFER, data->depthStencilBuffer));
            glCheck(GLEXT_glRenderbufferStorage(GLEXT_GL_RENDERBUFFER,
                                                GLEXT_GL_DEPTH_COMPONENT,
                                                static_cast<GLsizei>(size.x),
                                                static_cast<GLsizei>(size.y)));

            data->hasDepth   = true;
            data->hasStencil = false;
        }
        else if (settings.stencilBits)
        {
            GLuint depthStencil = 0;
            glCheck(GLEXT_glGenRenderbuffers(1, &depthStencil));
            data->depthStencilBuffer = depthStencil;
            if (!data->depthStencilBuffer)
            {
                err() << "Impossible to create render texture (failed to create the attached stencil buffer)" << std::endl;
                return 0;
            }
            glCheck(GLEXT_glBindRenderbuffer(GLEXT_GL_RENDERBUFFER, data->depthStencilBuffer));
            glCheck(GLEXT_glRenderbufferStorage(GLEXT_GL_RENDERBUFFER,
                                                GLEXT_GL_STENCIL_INDEX8,
                                                static_cast<GLsizei>(size.x),
                                                static_cast<GLsizei>(size.y)));

            data->hasDepth   = false;
            data->hasStencil = true;
        }
    }
    else
    {

#ifndef SFML_OPENGL_ES

        // Create the multisample color buffer
        GLuint color = 0;
        glCheck(GLEXT_glGenRenderbuffers(1, &color));
        data->colorBuffer = color;
        if (!data->colorBuffer)
        {
            err() << "Impossible to create render texture (failed to create the attached multisample color buffer)"
                  << std::endl;
            return 0;
        }
        glCheck(GLEXT_glBindRenderbuffer(GLEXT_GL_RENDERBUFFER, data->colorBuffer));
        glCheck(GLEXT_glRenderbufferStorageMultisample(GLEXT_GL_RENDERBUFFER,
                                                       static_cast<GLsizei>(settings.antiAliasingLevel),
                                                       data->sRgb ? GL_SRGB8_ALPHA8_EXT : GL_RGBA,
                                                       static_cast<GLsizei>(size.x),
                                                       static_cast<GLsizei>(size.y)));

        // Create the multisample depth/stencil buffer if requested
        if (settings.stencilBits && settings.depthBits)
        {
            GLuint depthStencil = 0;
            glCheck(GLEXT_glGenRenderbuffers(1, &depthStencil));
            data->depthStencilBuffer = depthStencil;
            if (!data->depthStencilBuffer)
            {
                err() << "Impossible to create render texture (failed to create the attached multisample "
                         "depth/stencil buffer)"
                      << std::endl;
                return 0;
            }
            glCheck(GLEXT_glBindRenderbuffer(GLEXT_GL_RENDERBUFFER, data->depthStencilBuffer));
            glCheck(GLEXT_glRenderbufferStorageMultisample(GLEXT_GL_RENDERBUFFER,
                                                           static_cast<GLsizei>(settings.antiAliasingLevel),
                                                           GLEXT_GL_DEPTH24_STENCIL8,
                                                           static_cast<GLsizei>(size.x),
                                                           static_cast<GLsizei>(size.y)));

            data->hasDepth   = true;
            data->hasStencil = true;
        }
        else if (settings.depthBits)
        {
            GLuint depthStencil = 0;
            glCheck(GLEXT_glGenRenderbuffers(1, &depthStencil));
            data->depthStencilBuffer = depthStencil;
            if (!data->depthStencilBuffer)
            {
                err() << "Impossible to create render texture (failed to create the attached multisample depth "
                         "buffer)"
                      << std::endl;
                return 0;
            }
            glCheck(GLEXT_glBindRenderbuffer(GLEXT_GL_RENDERBUFFER, data->depthStencilBuffer));
            glCheck(GLEXT_glRenderbufferStorageMultisample(GLEXT_GL_RENDERBUFFER,
                                                           static_cast<GLsizei>(settings.antiAliasingLevel),
                                                           GLEXT_GL_DEPTH_COMPONENT,
                                                           static_cast<GLsizei>(size.x),
                                                           static_cast<GLsizei>(size.y)));

            data->hasDepth   = true;
            data->hasStencil = false;
        }
        else if (settings.stencilBits)
        {
            GLuint depthStencil = 0;
            glCheck(GLEXT_glGenRenderbuffers(1, &depthStencil));
            data->depthStencilBuffer = depthStencil;
            if (!data->depthStencilBuffer)
            {
                err() << "Impossible to create render texture (failed to create the attached multisample "
                         "stencil buffer)"
                      << std::endl;
                return 0;
            }
            glCheck(GLEXT_glBindRenderbuffer(GLEXT_GL_RENDERBUFFER, data->depthStencilBuffer));
            glCheck(GLEXT_glRenderbufferStorageMultisample(GLEXT_GL_RENDERBUFFER,
                                                           static_cast<GLsizei>(settings.antiAliasingLevel),
                                                           GLEXT_GL_STENCIL_INDEX8,
                                                           static_cast<GLsizei>(size.x),
                                                           static_cast<GLsizei>(size.y)));

            data->hasDepth   = false;
            data->hasStencil = true;
        }

        data->multisample = true;

#else

        err() << "Impossible to create render texture (failed to create the multisample render buffers)" << std::endl;
        return 0;

#endif // SFML_OPENGL_ES
    }

    const BackendFramebufferHandle handle = m_nextFramebufferHandle++;
    m_framebuffers[handle] = std::move(data);
    return handle;
}


////////////////////////////////////////////////////////////
bool GLBackend::createFBOForContext(GLFramebufferData& data)
{
    // Create the framebuffer object
    auto frameBuffer = std::make_shared<GLFramebufferData::FBO>();
    glCheck(GLEXT_glGenFramebuffers(1, &frameBuffer->object));

    if (!frameBuffer->object)
    {
        err() << "Impossible to create render texture (failed to create the frame buffer object)" << std::endl;
        return false;
    }
    glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_FRAMEBUFFER, frameBuffer->object));

    // Link the depth/stencil renderbuffer to the frame buffer
    if (!data.multisample && data.depthStencilBuffer)
    {
        if (data.hasDepth)
        {
            glCheck(GLEXT_glFramebufferRenderbuffer(GLEXT_GL_FRAMEBUFFER,
                                                    GLEXT_GL_DEPTH_ATTACHMENT,
                                                    GLEXT_GL_RENDERBUFFER,
                                                    data.depthStencilBuffer));
        }

        if (data.hasStencil)
        {
            glCheck(GLEXT_glFramebufferRenderbuffer(GLEXT_GL_FRAMEBUFFER,
                                                    GLEXT_GL_STENCIL_ATTACHMENT,
                                                    GLEXT_GL_RENDERBUFFER,
                                                    data.depthStencilBuffer));
        }
    }

    // Link the texture to the frame buffer
    glCheck(GLEXT_glFramebufferTexture2D(GLEXT_GL_FRAMEBUFFER, GLEXT_GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, data.textureId, 0));

    // A final check, just to be sure...
    if (glCheck(GLEXT_glCheckFramebufferStatus(GLEXT_GL_FRAMEBUFFER)) != GLEXT_GL_FRAMEBUFFER_COMPLETE)
    {
        glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_FRAMEBUFFER, 0));
        err() << "Impossible to create render texture (failed to link the target texture to the frame buffer)" << std::endl;
        return false;
    }

    // Insert the FBO into our map
    data.frameBuffers.try_emplace(Context::getActiveContextId(), frameBuffer);

    // Register the object with the current context so it is automatically destroyed
    registerUnsharedGlObject(std::move(frameBuffer));

#ifndef SFML_OPENGL_ES

    if (data.multisample)
    {
        // Create the multisample framebuffer object
        auto multisampleFrameBuffer = std::make_shared<GLFramebufferData::FBO>();
        glCheck(GLEXT_glGenFramebuffers(1, &multisampleFrameBuffer->object));

        if (!multisampleFrameBuffer->object)
        {
            err() << "Impossible to create render texture (failed to create the multisample frame buffer object)"
                  << std::endl;
            return false;
        }
        glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_FRAMEBUFFER, multisampleFrameBuffer->object));

        // Link the multisample color buffer to the frame buffer
        glCheck(GLEXT_glBindRenderbuffer(GLEXT_GL_RENDERBUFFER, data.colorBuffer));
        glCheck(
            GLEXT_glFramebufferRenderbuffer(GLEXT_GL_FRAMEBUFFER, GLEXT_GL_COLOR_ATTACHMENT0, GLEXT_GL_RENDERBUFFER, data.colorBuffer));

        // Link the depth/stencil renderbuffer to the frame buffer
        if (data.depthStencilBuffer)
        {
            if (data.hasDepth)
            {
                glCheck(GLEXT_glFramebufferRenderbuffer(GLEXT_GL_FRAMEBUFFER,
                                                        GLEXT_GL_DEPTH_ATTACHMENT,
                                                        GLEXT_GL_RENDERBUFFER,
                                                        data.depthStencilBuffer));
            }

            if (data.hasStencil)
            {
                glCheck(GLEXT_glFramebufferRenderbuffer(GLEXT_GL_FRAMEBUFFER,
                                                        GLEXT_GL_STENCIL_ATTACHMENT,
                                                        GLEXT_GL_RENDERBUFFER,
                                                        data.depthStencilBuffer));
            }
        }

        // A final check, just to be sure...
        if (glCheck(GLEXT_glCheckFramebufferStatus(GLEXT_GL_FRAMEBUFFER)) != GLEXT_GL_FRAMEBUFFER_COMPLETE)
        {
            glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_FRAMEBUFFER, 0));
            err() << "Impossible to create render texture (failed to link the render buffers to the multisample frame "
                     "buffer)"
                  << std::endl;
            return false;
        }

        // Insert the FBO into our map
        data.multisampleFrameBuffers.try_emplace(Context::getActiveContextId(), multisampleFrameBuffer);

        // Register the object with the current context so it is automatically destroyed
        registerUnsharedGlObject(std::move(multisampleFrameBuffer));
    }

#endif

    return true;
}


////////////////////////////////////////////////////////////
void GLBackend::destroyFramebuffer(BackendFramebufferHandle handle)
{
    const auto it = m_framebuffers.find(handle);
    if (it == m_framebuffers.end())
        return;

    auto& data = *it->second;

    const TransientContextLock contextLock;

    // Destroy the color buffer
    if (data.colorBuffer)
    {
        const GLuint colorBuffer = data.colorBuffer;
        glCheck(GLEXT_glDeleteRenderbuffers(1, &colorBuffer));
    }

    // Destroy the depth/stencil buffer
    if (data.depthStencilBuffer)
    {
        const GLuint depthStencilBuffer = data.depthStencilBuffer;
        glCheck(GLEXT_glDeleteRenderbuffers(1, &depthStencilBuffer));
    }

    // Unregister FBOs with the contexts if they haven't already been destroyed
    for (auto& entry : data.frameBuffers)
    {
        auto frameBuffer = entry.second.lock();

        if (frameBuffer)
            unregisterUnsharedGlObject(std::move(frameBuffer));
    }

    for (auto& entry : data.multisampleFrameBuffers)
    {
        auto frameBuffer = entry.second.lock();

        if (frameBuffer)
            unregisterUnsharedGlObject(std::move(frameBuffer));
    }

    m_framebuffers.erase(it);
}


////////////////////////////////////////////////////////////
bool GLBackend::bindFramebuffer(BackendFramebufferHandle handle)
{
    // Unbind (bind default framebuffer)
    if (handle == 0)
    {
        glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_FRAMEBUFFER, 0));
        return true;
    }

    const auto it = m_framebuffers.find(handle);
    if (it == m_framebuffers.end())
    {
        // Treat as raw framebuffer ID (e.g., default framebuffer on iOS)
        glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_FRAMEBUFFER, static_cast<GLuint>(handle)));
        return true;
    }

    auto& data = *it->second;

    const std::uint64_t contextId = Context::getActiveContextId();
    if (!contextId)
        return false;

    // Lookup the FBO corresponding to the currently active context
    if (data.multisample)
    {
        const auto fboIt = data.multisampleFrameBuffers.find(contextId);

        if (fboIt != data.multisampleFrameBuffers.end())
        {
            const auto frameBuffer = fboIt->second.lock();

            if (frameBuffer)
            {
                glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_FRAMEBUFFER, frameBuffer->object));
                return true;
            }
        }
    }
    else
    {
        const auto fboIt = data.frameBuffers.find(contextId);

        if (fboIt != data.frameBuffers.end())
        {
            const auto frameBuffer = fboIt->second.lock();

            if (frameBuffer)
            {
                glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_FRAMEBUFFER, frameBuffer->object));
                return true;
            }
        }
    }

    // No FBO for this context yet, create one
    return createFBOForContext(data);
}


////////////////////////////////////////////////////////////
bool GLBackend::isFramebufferSrgb(BackendFramebufferHandle handle) const
{
    const auto it = m_framebuffers.find(handle);
    if (it == m_framebuffers.end())
        return false;

    return it->second->sRgb;
}


////////////////////////////////////////////////////////////
void GLBackend::updateFramebufferTexture(BackendFramebufferHandle handle, BackendTextureHandle /* texture */)
{
    // If multisampling is enabled, we need to resolve by blitting
    // from our FBO with multisample renderbuffer attachments
    // to our FBO to which our target texture is attached

#ifndef SFML_OPENGL_ES

    const auto it = m_framebuffers.find(handle);
    if (it == m_framebuffers.end())
        return;

    auto& data = *it->second;

    // In case of multisampling, make sure both FBOs
    // are already available within the current context
    if (data.multisample && data.size.x && data.size.y && bindFramebuffer(handle))
    {
        const std::uint64_t contextId = Context::getActiveContextId();

        const auto frameBufferIt = data.frameBuffers.find(contextId);
        const auto multisampleIt = data.multisampleFrameBuffers.find(contextId);

        if ((frameBufferIt != data.frameBuffers.end()) && (multisampleIt != data.multisampleFrameBuffers.end()))
        {
            const auto frameBuffer            = frameBufferIt->second.lock();
            const auto multiSampleFrameBuffer = multisampleIt->second.lock();

            if (frameBuffer && multiSampleFrameBuffer)
            {
                // Scissor testing affects framebuffer blits as well
                // Since we don't want scissor testing to interfere with our copying, we temporarily disable it for the blit if it is enabled
                GLboolean scissorEnabled = GL_FALSE;
                glCheck(glGetBooleanv(GL_SCISSOR_TEST, &scissorEnabled));

                if (scissorEnabled == GL_TRUE)
                    glCheck(glDisable(GL_SCISSOR_TEST));

                // Set up the blit target (draw framebuffer) and blit (from the read framebuffer, our multisample FBO)
                glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_DRAW_FRAMEBUFFER, frameBuffer->object));
                glCheck(GLEXT_glBlitFramebuffer(0,
                                                0,
                                                static_cast<GLint>(data.size.x),
                                                static_cast<GLint>(data.size.y),
                                                0,
                                                0,
                                                static_cast<GLint>(data.size.x),
                                                static_cast<GLint>(data.size.y),
                                                GL_COLOR_BUFFER_BIT,
                                                GL_NEAREST));
                glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_DRAW_FRAMEBUFFER, multiSampleFrameBuffer->object));

                // Re-enable scissor testing if it was previously enabled
                if (scissorEnabled == GL_TRUE)
                    glCheck(glEnable(GL_SCISSOR_TEST));
            }
        }
    }

#else

    (void)handle;

#endif // SFML_OPENGL_ES
}


////////////////////////////////////////////////////////////
// Capability queries
////////////////////////////////////////////////////////////

bool GLBackend::isShaderAvailable() const
{
    ensureExtensionsInit();
#ifndef SFML_OPENGL_ES
    return GLEXT_multitexture && GLEXT_shading_language_100 && GLEXT_shader_objects && GLEXT_vertex_shader &&
           GLEXT_fragment_shader;
#else
    return false;
#endif
}


////////////////////////////////////////////////////////////
bool GLBackend::isGeometryShaderAvailable() const
{
    ensureExtensionsInit();
#ifndef SFML_OPENGL_ES
    return isShaderAvailable() && (GLEXT_geometry_shader4 || GLEXT_GL_VERSION_3_2);
#else
    return false;
#endif
}


////////////////////////////////////////////////////////////
bool GLBackend::isVertexBufferAvailable() const
{
    ensureExtensionsInit();
    return GLEXT_vertex_buffer_object;
}


////////////////////////////////////////////////////////////
bool GLBackend::isNonPowerOfTwoTextureSupported() const
{
    ensureExtensionsInit();
    return GLEXT_texture_non_power_of_two;
}


////////////////////////////////////////////////////////////
std::size_t GLBackend::getMaxTextureUnits() const
{
#ifndef SFML_OPENGL_ES
    static const GLint maxUnits = []
    {
        GLint value = 0;
        glCheck(glGetIntegerv(GLEXT_GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &value));
        return value;
    }();
    return static_cast<std::size_t>(maxUnits);
#else
    return 0;
#endif
}


////////////////////////////////////////////////////////////
// Pipeline operations
////////////////////////////////////////////////////////////

void GLBackend::flushPipeline()
{
    glCheck(glFlush());
}


////////////////////////////////////////////////////////////
void GLBackend::pushRenderStates()
{
#ifdef SFML_DEBUG
    // Make sure that the user didn't leave an unchecked OpenGL error
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR)
    {
        err() << "OpenGL error (" << error << ") detected in user code, "
              << "you should check for errors with glGetError()" << std::endl;
    }
#endif

#ifndef SFML_OPENGL_ES
    glCheck(glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS));
    glCheck(glPushAttrib(GL_ALL_ATTRIB_BITS));
#endif
    glCheck(glMatrixMode(GL_MODELVIEW));
    glCheck(glPushMatrix());
    glCheck(glMatrixMode(GL_PROJECTION));
    glCheck(glPushMatrix());
    glCheck(glMatrixMode(GL_TEXTURE));
    glCheck(glPushMatrix());
}


////////////////////////////////////////////////////////////
void GLBackend::popRenderStates()
{
    glCheck(glMatrixMode(GL_PROJECTION));
    glCheck(glPopMatrix());
    glCheck(glMatrixMode(GL_MODELVIEW));
    glCheck(glPopMatrix());
    glCheck(glMatrixMode(GL_TEXTURE));
    glCheck(glPopMatrix());
#ifndef SFML_OPENGL_ES
    glCheck(glPopClientAttrib());
    glCheck(glPopAttrib());
#endif
}


////////////////////////////////////////////////////////////
void GLBackend::bindBuffer(BackendBufferHandle buffer)
{
    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, static_cast<unsigned int>(buffer)));
}


////////////////////////////////////////////////////////////
unsigned int GLBackend::getDefaultFramebufferBinding() const
{
    ensureExtensionsInit();
    GLint binding = 0;
    glCheck(glGetIntegerv(GLEXT_GL_FRAMEBUFFER_BINDING, &binding));
    return static_cast<unsigned int>(binding);
}


////////////////////////////////////////////////////////////
bool GLBackend::isFramebufferAvailable() const
{
    ensureExtensionsInit();
    return GLEXT_framebuffer_object != 0;
}


////////////////////////////////////////////////////////////
unsigned int GLBackend::getMaxAntiAliasingLevel() const
{
#ifdef SFML_OPENGL_ES
    return 0;
#else
    GLint samples = 0;
    glCheck(glGetIntegerv(GLEXT_GL_MAX_SAMPLES, &samples));
    return static_cast<unsigned int>(samples);
#endif
}


////////////////////////////////////////////////////////////
bool GLBackend::isSrgbTextureAvailable() const
{
    ensureExtensionsInit();
    return GLEXT_texture_sRGB != 0;
}


////////////////////////////////////////////////////////////
bool GLBackend::copyBufferFallback(BackendBufferHandle destHandle,
                                   BackendBufferHandle srcHandle,
                                   std::size_t         srcSize)
{
#ifndef SFML_OPENGL_ES
    const auto dest = static_cast<unsigned int>(destHandle);
    const auto src  = static_cast<unsigned int>(srcHandle);

    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, dest));
    glCheck(GLEXT_glBufferData(GLEXT_GL_ARRAY_BUFFER,
                               static_cast<GLsizeiptrARB>(sizeof(Vertex) * srcSize),
                               nullptr,
                               GLEXT_GL_STREAM_DRAW));

    void* const destination = glCheck(GLEXT_glMapBuffer(GLEXT_GL_ARRAY_BUFFER, GLEXT_GL_WRITE_ONLY));

    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, src));

    const void* const source = glCheck(GLEXT_glMapBuffer(GLEXT_GL_ARRAY_BUFFER, GLEXT_GL_READ_ONLY));

    std::memcpy(destination, source, sizeof(Vertex) * srcSize);

    const GLboolean sourceResult = glCheck(GLEXT_glUnmapBuffer(GLEXT_GL_ARRAY_BUFFER));

    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, dest));

    const GLboolean destinationResult = glCheck(GLEXT_glUnmapBuffer(GLEXT_GL_ARRAY_BUFFER));

    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, 0));

    return (sourceResult == GL_TRUE) && (destinationResult == GL_TRUE);
#else
    (void)destHandle;
    (void)srcHandle;
    (void)srcSize;
    return false;
#endif
}


////////////////////////////////////////////////////////////
void GLBackend::prepareUniformUpdate(BackendShaderHandle handle)
{
#ifndef SFML_OPENGL_ES
    const auto currentProgram = castToGlHandle(static_cast<unsigned int>(handle));
    if (currentProgram)
    {
        m_savedProgram = castFromGlHandle(glCheck(GLEXT_glGetHandle(GLEXT_GL_PROGRAM_OBJECT)));
        if (currentProgram != castToGlHandle(m_savedProgram))
            glCheck(GLEXT_glUseProgramObject(currentProgram));
    }
#else
    (void)handle;
#endif
}


////////////////////////////////////////////////////////////
void GLBackend::finalizeUniformUpdate()
{
#ifndef SFML_OPENGL_ES
    glCheck(GLEXT_glUseProgramObject(castToGlHandle(m_savedProgram)));
    m_savedProgram = 0;
#endif
}


////////////////////////////////////////////////////////////
void GLBackend::resetGLStates()
{
    ensureExtensionsInit();

    // Make sure texture unit 0 is active
    if (GLEXT_multitexture)
    {
        glCheck(GLEXT_glClientActiveTexture(GLEXT_GL_TEXTURE0));
        glCheck(GLEXT_glActiveTexture(GLEXT_GL_TEXTURE0));
    }

    // Define the default OpenGL states
    glCheck(glDisable(GL_CULL_FACE));
    glCheck(glDisable(GL_LIGHTING));
    glCheck(glDisable(GL_STENCIL_TEST));
    glCheck(glDisable(GL_DEPTH_TEST));
    glCheck(glDisable(GL_ALPHA_TEST));
    glCheck(glDisable(GL_SCISSOR_TEST));
    glCheck(glEnable(GL_TEXTURE_2D));
    glCheck(glEnable(GL_BLEND));
    glCheck(glMatrixMode(GL_MODELVIEW));
    glCheck(glLoadIdentity());
    glCheck(glEnableClientState(GL_VERTEX_ARRAY));
    glCheck(glEnableClientState(GL_COLOR_ARRAY));
    glCheck(glEnableClientState(GL_TEXTURE_COORD_ARRAY));
    glCheck(glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));
}

} // namespace sf::priv

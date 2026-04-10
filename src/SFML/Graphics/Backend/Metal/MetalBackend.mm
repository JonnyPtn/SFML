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
#include <SFML/Graphics/Backend/Metal/MetalBackend.hpp>
#include <SFML/Graphics/Image.hpp>

#include <SFML/System/Err.hpp>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <ostream>
#include <vector>


namespace sf::priv
{

////////////////////////////////////////////////////////////
struct MetalBackend::Impl
{
    id<MTLDevice>       device;
    id<MTLCommandQueue> commandQueue;

    std::uint64_t nextTextureHandle{1};
    std::uint64_t nextBufferHandle{1};
    std::uint64_t nextShaderHandle{1};
    std::uint64_t nextFramebufferHandle{1};

    std::unordered_map<std::uint64_t, id<MTLTexture>> textures;
    std::unordered_map<std::uint64_t, id<MTLBuffer>>  buffers;
    std::unordered_map<std::uint64_t, Vector2u>       textureSizes;
    std::unordered_map<std::uint64_t, bool>           textureFlipped;
};


////////////////////////////////////////////////////////////
MetalBackend::MetalBackend() : m_impl(new Impl)
{
    m_impl->device = MTLCreateSystemDefaultDevice();
    if (!m_impl->device)
    {
        err() << "Failed to create Metal device" << std::endl;
        return;
    }

    m_impl->commandQueue = [m_impl->device newCommandQueue];
}


////////////////////////////////////////////////////////////
MetalBackend::~MetalBackend()
{
    delete m_impl;
}


////////////////////////////////////////////////////////////
// Render target operations
////////////////////////////////////////////////////////////

void MetalBackend::clear(Color /* color */)
{
    // TODO: implement Metal clear
}


////////////////////////////////////////////////////////////
void MetalBackend::clearStencil(StencilValue /* stencilValue */)
{
    // TODO: implement Metal stencil clear
}


////////////////////////////////////////////////////////////
void MetalBackend::clear(Color /* color */, StencilValue /* stencilValue */)
{
    // TODO: implement Metal clear with stencil
}


////////////////////////////////////////////////////////////
void MetalBackend::setViewport(const IntRect& /* viewport */, unsigned int /* targetHeight */)
{
    // TODO: implement Metal viewport
}


////////////////////////////////////////////////////////////
void MetalBackend::setScissor(const IntRect& /* scissor */, bool /* enable */, unsigned int /* targetHeight */)
{
    // TODO: implement Metal scissor
}


////////////////////////////////////////////////////////////
void MetalBackend::setSrgb(bool /* enable */)
{
    // Metal handles sRGB through pixel formats, not a global toggle
}


////////////////////////////////////////////////////////////
// State management
////////////////////////////////////////////////////////////

void MetalBackend::applyBlendMode(const BlendMode& /* mode */)
{
    // TODO: configure MTLRenderPipelineDescriptor color attachment blending
}


////////////////////////////////////////////////////////////
void MetalBackend::applyStencilMode(const StencilMode& /* mode */)
{
    // TODO: configure MTLDepthStencilDescriptor
}


////////////////////////////////////////////////////////////
void MetalBackend::setColorMask(bool /* enable */)
{
    // TODO: configure MTLRenderPipelineColorAttachmentDescriptor writeMask
}


////////////////////////////////////////////////////////////
// Drawing
////////////////////////////////////////////////////////////

void MetalBackend::setupVertexData(const Vertex* /* vertices */, std::size_t /* count */, bool /* textured */)
{
    // TODO: upload vertex data to MTLBuffer
}


////////////////////////////////////////////////////////////
void MetalBackend::setupVertexBuffer(BackendBufferHandle /* buffer */, bool /* textured */)
{
    // TODO: bind MTLBuffer for drawing
}


////////////////////////////////////////////////////////////
void MetalBackend::applyTransform(const Transform& /* projection */, const Transform& /* model */)
{
    // TODO: set transform uniforms
}


////////////////////////////////////////////////////////////
void MetalBackend::drawPrimitives(PrimitiveType /* type */, std::size_t /* firstVertex */, std::size_t /* vertexCount */)
{
    // TODO: encode draw command via MTLRenderCommandEncoder
}


////////////////////////////////////////////////////////////
// Texture operations
////////////////////////////////////////////////////////////

BackendTextureHandle MetalBackend::createTexture(Vector2u size, bool sRgb)
{
    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:sRgb ? MTLPixelFormatRGBA8Unorm_sRGB
                                                                                               : MTLPixelFormatRGBA8Unorm
                                                                                   width:size.x
                                                                                  height:size.y
                                                                               mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;

    id<MTLTexture> texture = [m_impl->device newTextureWithDescriptor:desc];
    if (!texture)
        return 0;

    const auto handle = m_impl->nextTextureHandle++;
    m_impl->textures[handle]      = texture;
    m_impl->textureSizes[handle]  = size;
    m_impl->textureFlipped[handle] = false;

    return handle;
}


////////////////////////////////////////////////////////////
void MetalBackend::destroyTexture(BackendTextureHandle handle)
{
    m_impl->textures.erase(handle);
    m_impl->textureSizes.erase(handle);
    m_impl->textureFlipped.erase(handle);
}


////////////////////////////////////////////////////////////
void MetalBackend::updateTexture(BackendTextureHandle handle,
                                 const std::uint8_t*  pixels,
                                 Vector2u             size,
                                 Vector2u             dest)
{
    auto it = m_impl->textures.find(handle);
    if (it == m_impl->textures.end() || !pixels)
        return;

    MTLRegion region = MTLRegionMake2D(dest.x, dest.y, size.x, size.y);
    [it->second replaceRegion:region mipmapLevel:0 withBytes:pixels bytesPerRow:size.x * 4];
}


////////////////////////////////////////////////////////////
void MetalBackend::updateTextureFromTexture(BackendTextureHandle handle,
                                            BackendTextureHandle srcHandle,
                                            Vector2u             srcSize,
                                            Vector2u             dest)
{
    auto dstIt = m_impl->textures.find(handle);
    auto srcIt = m_impl->textures.find(srcHandle);
    if (dstIt == m_impl->textures.end() || srcIt == m_impl->textures.end())
        return;

    id<MTLCommandBuffer> cmdBuf  = [m_impl->commandQueue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cmdBuf blitCommandEncoder];

    [blit copyFromTexture:srcIt->second
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(srcSize.x, srcSize.y, 1)
                toTexture:dstIt->second
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(dest.x, dest.y, 0)];

    [blit endEncoding];
    [cmdBuf commit];
    [cmdBuf waitUntilCompleted];
}


////////////////////////////////////////////////////////////
void MetalBackend::updateTextureFromFramebuffer(BackendTextureHandle /* handle */, Vector2u /* size */, Vector2u /* dest */)
{
    // TODO: blit from current render target to texture
}


////////////////////////////////////////////////////////////
void MetalBackend::bindTexture(BackendTextureHandle /* handle */, CoordinateType /* coordinateType */)
{
    // TODO: track active texture for next draw call
}


////////////////////////////////////////////////////////////
Image MetalBackend::readbackTexture(BackendTextureHandle handle, Vector2u size)
{
    auto it = m_impl->textures.find(handle);
    if (it == m_impl->textures.end())
        return {};

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size.x) * size.y * 4);
    MTLRegion region = MTLRegionMake2D(0, 0, size.x, size.y);
    [it->second getBytes:pixels.data() bytesPerRow:size.x * 4 fromRegion:region mipmapLevel:0];

    Image image(size, pixels.data());
    return image;
}


////////////////////////////////////////////////////////////
void MetalBackend::setTextureSmooth(BackendTextureHandle /* handle */, bool /* smooth */, bool /* hasMipmap */)
{
    // Metal sets sampler state at draw time via MTLSamplerDescriptor
    // TODO: track per-texture sampler settings
}


////////////////////////////////////////////////////////////
void MetalBackend::setTextureRepeated(BackendTextureHandle /* handle */, bool /* repeated */)
{
    // Metal sets address mode via MTLSamplerDescriptor
    // TODO: track per-texture sampler settings
}


////////////////////////////////////////////////////////////
bool MetalBackend::generateMipmap(BackendTextureHandle handle, Vector2u /* size */, bool /* smooth */)
{
    auto it = m_impl->textures.find(handle);
    if (it == m_impl->textures.end())
        return false;

    id<MTLCommandBuffer> cmdBuf = [m_impl->commandQueue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cmdBuf blitCommandEncoder];
    [blit generateMipmapsForTexture:it->second];
    [blit endEncoding];
    [cmdBuf commit];
    [cmdBuf waitUntilCompleted];

    return true;
}


////////////////////////////////////////////////////////////
unsigned int MetalBackend::getMaxTextureSize() const
{
    if (!m_impl->device)
        return 0;

    // Apple GPUs support at least 8192, most modern ones 16384
    if ([m_impl->device supportsFamily:MTLGPUFamilyApple3])
        return 16384;

    return 8192;
}


////////////////////////////////////////////////////////////
void MetalBackend::setTextureFlipped(BackendTextureHandle handle, bool flipped)
{
    m_impl->textureFlipped[handle] = flipped;
}


////////////////////////////////////////////////////////////
Vector2u MetalBackend::getTextureActualSize(BackendTextureHandle handle) const
{
    auto it = m_impl->textureSizes.find(handle);
    if (it == m_impl->textureSizes.end())
        return {};

    // Metal always uses exact sizes (no power-of-two padding)
    return it->second;
}


////////////////////////////////////////////////////////////
// Shader operations
////////////////////////////////////////////////////////////

BackendShaderHandle MetalBackend::compileShader(std::string_view /* vertexShaderCode */,
                                                std::string_view /* geometryShaderCode */,
                                                std::string_view /* fragmentShaderCode */)
{
    // TODO: compile MSL shaders (GLSL->MSL cross-compilation or native MSL)
    err() << "Metal shader compilation not yet implemented" << std::endl;
    return 0;
}


////////////////////////////////////////////////////////////
void MetalBackend::destroyShader(BackendShaderHandle /* handle */)
{
    // TODO: release shader resources
}


////////////////////////////////////////////////////////////
void MetalBackend::bindShader(BackendShaderHandle /* handle */)
{
    // TODO: track active shader for next draw call
}


////////////////////////////////////////////////////////////
int MetalBackend::getUniformLocation(BackendShaderHandle /* handle */, const std::string& /* name */)
{
    // TODO: look up uniform offset in Metal argument buffer
    return -1;
}


////////////////////////////////////////////////////////////
void MetalBackend::setUniform(BackendShaderHandle, int, float) {}
void MetalBackend::setUniform(BackendShaderHandle, int, const Glsl::Vec2&) {}
void MetalBackend::setUniform(BackendShaderHandle, int, const Glsl::Vec3&) {}
void MetalBackend::setUniform(BackendShaderHandle, int, const Glsl::Vec4&) {}
void MetalBackend::setUniform(BackendShaderHandle, int, int) {}
void MetalBackend::setUniform(BackendShaderHandle, int, const Glsl::Ivec2&) {}
void MetalBackend::setUniform(BackendShaderHandle, int, const Glsl::Ivec3&) {}
void MetalBackend::setUniform(BackendShaderHandle, int, const Glsl::Ivec4&) {}
void MetalBackend::setUniform(BackendShaderHandle, int, bool) {}
void MetalBackend::setUniform(BackendShaderHandle, int, const Glsl::Bvec2&) {}
void MetalBackend::setUniform(BackendShaderHandle, int, const Glsl::Bvec3&) {}
void MetalBackend::setUniform(BackendShaderHandle, int, const Glsl::Bvec4&) {}
void MetalBackend::setUniform(BackendShaderHandle, int, const Glsl::Mat3&) {}
void MetalBackend::setUniform(BackendShaderHandle, int, const Glsl::Mat4&) {}

void MetalBackend::setUniformTexture(BackendShaderHandle, int, BackendTextureHandle, int) {}

void MetalBackend::setUniformArray(BackendShaderHandle, int, const float*, std::size_t) {}
void MetalBackend::setUniformArray(BackendShaderHandle, int, const Glsl::Vec2*, std::size_t) {}
void MetalBackend::setUniformArray(BackendShaderHandle, int, const Glsl::Vec3*, std::size_t) {}
void MetalBackend::setUniformArray(BackendShaderHandle, int, const Glsl::Vec4*, std::size_t) {}
void MetalBackend::setUniformArray(BackendShaderHandle, int, const Glsl::Mat3*, std::size_t) {}
void MetalBackend::setUniformArray(BackendShaderHandle, int, const Glsl::Mat4*, std::size_t) {}


////////////////////////////////////////////////////////////
// Vertex buffer operations
////////////////////////////////////////////////////////////

BackendBufferHandle MetalBackend::createBuffer(std::size_t vertexCount, VertexBuffer::Usage /* usage */)
{
    const auto       byteSize = vertexCount * sizeof(Vertex);
    id<MTLBuffer>    buffer   = [m_impl->device newBufferWithLength:byteSize options:MTLResourceStorageModeShared];

    if (!buffer)
        return 0;

    const auto handle = m_impl->nextBufferHandle++;
    m_impl->buffers[handle] = buffer;
    return handle;
}


////////////////////////////////////////////////////////////
void MetalBackend::destroyBuffer(BackendBufferHandle handle)
{
    m_impl->buffers.erase(handle);
}


////////////////////////////////////////////////////////////
bool MetalBackend::updateBuffer(BackendBufferHandle handle,
                                const Vertex*       vertices,
                                std::size_t         count,
                                unsigned int        offset)
{
    auto it = m_impl->buffers.find(handle);
    if (it == m_impl->buffers.end() || !vertices)
        return false;

    const auto byteOffset = static_cast<std::size_t>(offset) * sizeof(Vertex);
    const auto byteSize   = count * sizeof(Vertex);

    std::memcpy(static_cast<char*>([it->second contents]) + byteOffset, vertices, byteSize);
    return true;
}


////////////////////////////////////////////////////////////
bool MetalBackend::copyBuffer(BackendBufferHandle destHandle,
                              BackendBufferHandle srcHandle,
                              std::size_t         srcSize)
{
    auto dstIt = m_impl->buffers.find(destHandle);
    auto srcIt = m_impl->buffers.find(srcHandle);
    if (dstIt == m_impl->buffers.end() || srcIt == m_impl->buffers.end())
        return false;

    id<MTLCommandBuffer> cmdBuf = [m_impl->commandQueue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cmdBuf blitCommandEncoder];

    const auto byteSize = srcSize * sizeof(Vertex);
    [blit copyFromBuffer:srcIt->second sourceOffset:0 toBuffer:dstIt->second destinationOffset:0 size:byteSize];

    [blit endEncoding];
    [cmdBuf commit];
    [cmdBuf waitUntilCompleted];

    return true;
}


////////////////////////////////////////////////////////////
// Framebuffer operations
////////////////////////////////////////////////////////////

BackendFramebufferHandle MetalBackend::createFramebuffer(Vector2u /* size */,
                                                          BackendTextureHandle /* texture */,
                                                          const ContextSettings& /* settings */)
{
    // TODO: set up render pass descriptor with the texture as color attachment
    return m_impl->nextFramebufferHandle++;
}


////////////////////////////////////////////////////////////
void MetalBackend::destroyFramebuffer(BackendFramebufferHandle /* handle */)
{
    // TODO: clean up framebuffer resources
}


////////////////////////////////////////////////////////////
bool MetalBackend::bindFramebuffer(BackendFramebufferHandle /* handle */)
{
    // TODO: set active render pass
    return true;
}


////////////////////////////////////////////////////////////
bool MetalBackend::isFramebufferSrgb(BackendFramebufferHandle /* handle */) const
{
    // TODO: check texture pixel format
    return false;
}


////////////////////////////////////////////////////////////
void MetalBackend::updateFramebufferTexture(BackendFramebufferHandle /* handle */, BackendTextureHandle /* texture */)
{
    // TODO: update render pass color attachment
}


////////////////////////////////////////////////////////////
// Capability queries
////////////////////////////////////////////////////////////

bool MetalBackend::isShaderAvailable() const
{
    return true; // Metal always supports shaders
}


////////////////////////////////////////////////////////////
bool MetalBackend::isGeometryShaderAvailable() const
{
    return false; // Metal does not support geometry shaders
}


////////////////////////////////////////////////////////////
bool MetalBackend::isVertexBufferAvailable() const
{
    return true; // Metal always supports vertex buffers
}


////////////////////////////////////////////////////////////
bool MetalBackend::isNonPowerOfTwoTextureSupported() const
{
    return true; // Metal always supports NPOT textures
}


////////////////////////////////////////////////////////////
std::size_t MetalBackend::getMaxTextureUnits() const
{
    return 31; // Metal guarantees at least 31 texture slots
}


////////////////////////////////////////////////////////////
// Pipeline operations
////////////////////////////////////////////////////////////

void MetalBackend::flushPipeline()
{
    // TODO: commit current command buffer and wait
}


////////////////////////////////////////////////////////////
void MetalBackend::pushRenderStates()
{
    // No-op for Metal (state is not global like OpenGL)
}


////////////////////////////////////////////////////////////
void MetalBackend::popRenderStates()
{
    // No-op for Metal (state is not global like OpenGL)
}


////////////////////////////////////////////////////////////
void MetalBackend::bindBuffer(BackendBufferHandle /* buffer */)
{
    // TODO: track active buffer
}


////////////////////////////////////////////////////////////
unsigned int MetalBackend::getDefaultFramebufferBinding() const
{
    return 0; // Metal doesn't have a default framebuffer concept like GL
}


////////////////////////////////////////////////////////////
bool MetalBackend::isFramebufferAvailable() const
{
    return true; // Metal always supports render-to-texture
}


////////////////////////////////////////////////////////////
unsigned int MetalBackend::getMaxAntiAliasingLevel() const
{
    if (!m_impl->device)
        return 0;

    // Check supported sample counts (Metal supports 1, 2, 4, 8)
    for (unsigned int level = 8; level >= 2; level /= 2)
    {
        if ([m_impl->device supportsTextureSampleCount:level])
            return level;
    }

    return 0;
}


////////////////////////////////////////////////////////////
bool MetalBackend::isSrgbTextureAvailable() const
{
    return true; // Metal always supports sRGB textures
}


////////////////////////////////////////////////////////////
void MetalBackend::resetStates()
{
    // No-op for Metal (state is per-encoder, not global)
}


////////////////////////////////////////////////////////////
bool MetalBackend::copyBufferFallback(BackendBufferHandle destHandle,
                                      BackendBufferHandle srcHandle,
                                      std::size_t         srcSize)
{
    // Metal always supports blit copies, so just delegate
    return copyBuffer(destHandle, srcHandle, srcSize);
}


////////////////////////////////////////////////////////////
void MetalBackend::prepareUniformUpdate(BackendShaderHandle /* handle */)
{
    // TODO: prepare argument buffer for updates
}


////////////////////////////////////////////////////////////
void MetalBackend::finalizeUniformUpdate()
{
    // TODO: finalize argument buffer
}

} // namespace sf::priv

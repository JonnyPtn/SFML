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
#include <SFML/Graphics/Transform.hpp>

#include <SFML/System/Err.hpp>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

////////////////////////////////////////////////////////////
/// \brief ObjC wrapper to guarantee ARC manages the render
///        command encoder with proper strong-reference semantics.
///        id<> in C++ structs can have unreliable ARC behavior.
////////////////////////////////////////////////////////////
@interface SFMLMetalState : NSObject
@property (nonatomic, strong) id<MTLRenderCommandEncoder> encoder;
@property (nonatomic, strong) id<MTLCommandBuffer> commandBuffer;
@property (nonatomic, strong) id<CAMetalDrawable> currentDrawable;
- (void)endEncoderIfActive;
@end

@implementation SFMLMetalState
- (void)endEncoderIfActive
{
    if (_encoder)
    {
        [_encoder endEncoding];
        _encoder = nil;
    }
}
@end

#include <ostream>
#include <vector>

#include <cstring>


namespace
{

////////////////////////////////////////////////////////////
// Default Metal Shading Language source
// Replicates the OpenGL fixed-function pipeline behavior:
//   vertex: position * projection * model, pass through color + texcoords
//   fragment: vertex color * texture sample (or just vertex color if untextured)
////////////////////////////////////////////////////////////
constexpr const char* defaultShaderSource = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VertexIn
{
    float2 position  [[attribute(0)]];
    uchar4 color     [[attribute(1)]];
    float2 texCoords [[attribute(2)]];
};

struct VertexOut
{
    float4 position  [[position]];
    float4 color;
    float2 texCoords;
};

struct Uniforms
{
    float4x4 projection;
    float4x4 model;
    float4x4 textureMatrix;
};

vertex VertexOut sfml_default_vertex(VertexIn in [[stage_in]],
                                     constant Uniforms& uniforms [[buffer(1)]])
{
    VertexOut out;
    out.position  = uniforms.projection * uniforms.model * float4(in.position, 0.0, 1.0);
    out.color     = float4(in.color) / 255.0;
    float4 tc     = uniforms.textureMatrix * float4(in.texCoords, 0.0, 1.0);
    out.texCoords = tc.xy;
    return out;
}

fragment float4 sfml_fragment_textured(VertexOut in [[stage_in]],
                                       texture2d<float> tex [[texture(0)]],
                                       sampler smp [[sampler(0)]])
{
    return in.color * tex.sample(smp, in.texCoords);
}

fragment float4 sfml_fragment_color(VertexOut in [[stage_in]])
{
    return in.color;
}
)msl";


////////////////////////////////////////////////////////////
/// \brief Convert SFML blend factor to Metal
////////////////////////////////////////////////////////////
MTLBlendFactor toMetalBlendFactor(sf::BlendMode::Factor factor)
{
    // clang-format off
    switch (factor)
    {
        case sf::BlendMode::Factor::Zero:             return MTLBlendFactorZero;
        case sf::BlendMode::Factor::One:              return MTLBlendFactorOne;
        case sf::BlendMode::Factor::SrcColor:         return MTLBlendFactorSourceColor;
        case sf::BlendMode::Factor::OneMinusSrcColor: return MTLBlendFactorOneMinusSourceColor;
        case sf::BlendMode::Factor::DstColor:         return MTLBlendFactorDestinationColor;
        case sf::BlendMode::Factor::OneMinusDstColor: return MTLBlendFactorOneMinusDestinationColor;
        case sf::BlendMode::Factor::SrcAlpha:         return MTLBlendFactorSourceAlpha;
        case sf::BlendMode::Factor::OneMinusSrcAlpha: return MTLBlendFactorOneMinusSourceAlpha;
        case sf::BlendMode::Factor::DstAlpha:         return MTLBlendFactorDestinationAlpha;
        case sf::BlendMode::Factor::OneMinusDstAlpha: return MTLBlendFactorOneMinusDestinationAlpha;
    }
    // clang-format on
    return MTLBlendFactorZero;
}


////////////////////////////////////////////////////////////
/// \brief Convert SFML blend equation to Metal
////////////////////////////////////////////////////////////
MTLBlendOperation toMetalBlendOp(sf::BlendMode::Equation eq)
{
    // clang-format off
    switch (eq)
    {
        case sf::BlendMode::Equation::Add:             return MTLBlendOperationAdd;
        case sf::BlendMode::Equation::Subtract:        return MTLBlendOperationSubtract;
        case sf::BlendMode::Equation::ReverseSubtract: return MTLBlendOperationReverseSubtract;
        case sf::BlendMode::Equation::Min:             return MTLBlendOperationMin;
        case sf::BlendMode::Equation::Max:             return MTLBlendOperationMax;
    }
    // clang-format on
    return MTLBlendOperationAdd;
}


////////////////////////////////////////////////////////////
/// \brief Convert SFML primitive type to Metal
////////////////////////////////////////////////////////////
MTLPrimitiveType toMetalPrimitive(sf::PrimitiveType type)
{
    // clang-format off
    switch (type)
    {
        case sf::PrimitiveType::Points:        return MTLPrimitiveTypePoint;
        case sf::PrimitiveType::Lines:         return MTLPrimitiveTypeLine;
        case sf::PrimitiveType::LineStrip:     return MTLPrimitiveTypeLineStrip;
        case sf::PrimitiveType::Triangles:     return MTLPrimitiveTypeTriangle;
        case sf::PrimitiveType::TriangleStrip: return MTLPrimitiveTypeTriangleStrip;
    }
    // clang-format on
    return MTLPrimitiveTypeTriangle;
}

} // anonymous namespace


namespace sf::priv
{

////////////////////////////////////////////////////////////
/// \brief Pipeline cache key combining blend mode + textured flag
////////////////////////////////////////////////////////////
struct PipelineKey
{
    BlendMode blendMode;
    bool      textured{};
    bool      colorMask{true};

    bool operator==(const PipelineKey& rhs) const
    {
        return blendMode == rhs.blendMode && textured == rhs.textured && colorMask == rhs.colorMask;
    }
};

struct PipelineKeyHash
{
    std::size_t operator()(const PipelineKey& k) const
    {
        // Simple hash combining blend mode fields
        auto h = static_cast<std::size_t>(static_cast<int>(k.blendMode.colorSrcFactor));
        h = h * 31 + static_cast<std::size_t>(static_cast<int>(k.blendMode.colorDstFactor));
        h = h * 31 + static_cast<std::size_t>(static_cast<int>(k.blendMode.colorEquation));
        h = h * 31 + static_cast<std::size_t>(static_cast<int>(k.blendMode.alphaSrcFactor));
        h = h * 31 + static_cast<std::size_t>(static_cast<int>(k.blendMode.alphaDstFactor));
        h = h * 31 + static_cast<std::size_t>(static_cast<int>(k.blendMode.alphaEquation));
        h = h * 31 + static_cast<std::size_t>(k.textured);
        h = h * 31 + static_cast<std::size_t>(k.colorMask);
        return h;
    }
};


////////////////////////////////////////////////////////////
/// \brief Uniform buffer layout matching the MSL Uniforms struct
////////////////////////////////////////////////////////////
struct Uniforms
{
    float projection[16];
    float model[16];
    float textureMatrix[16];
};


////////////////////////////////////////////////////////////
/// \brief Framebuffer data for render-to-texture
////////////////////////////////////////////////////////////
struct FramebufferData
{
    BackendTextureHandle textureHandle{};
    bool                 sRgb{};
};


////////////////////////////////////////////////////////////
struct MetalBackend::Impl
{
    // Device & queue
    id<MTLDevice>       device{};
    id<MTLCommandQueue> commandQueue{};

    // Default shader library and functions
    id<MTLLibrary>  defaultLibrary{};
    id<MTLFunction> vertexFunction{};
    id<MTLFunction> fragmentTextured{};
    id<MTLFunction> fragmentColor{};

    // Default sampler
    id<MTLSamplerState> linearSampler{};
    id<MTLSamplerState> nearestSampler{};

    // Pipeline state cache
    std::unordered_map<PipelineKey, id<MTLRenderPipelineState>, PipelineKeyHash> pipelineCache;

    // Render state — wrapped in ObjC object for reliable ARC lifetime
    SFMLMetalState* metalState{};

    // Current render target
    BackendFramebufferHandle currentFramebuffer{};

    // Per-texture sampler settings
    struct TextureInfo
    {
        Vector2u size;
        bool     flipped{};
        bool     smooth{};
        bool     repeated{};
        bool     sRgb{};
    };

    // Resource maps
    std::unordered_map<std::uint64_t, id<MTLTexture>> textures;
    std::unordered_map<std::uint64_t, id<MTLBuffer>>  buffers;
    std::unordered_map<std::uint64_t, TextureInfo>    textureInfos;
    std::unordered_map<std::uint64_t, FramebufferData> framebuffers;

    std::uint64_t nextTextureHandle{1};
    std::uint64_t nextBufferHandle{1};
    std::uint64_t nextShaderHandle{1};
    std::uint64_t nextFramebufferHandle{1};

    // Current draw state
    Uniforms                uniforms{};
    BlendMode               currentBlendMode;
    bool                    currentTextured{};
    bool                    currentColorMask{true};
    id<MTLTexture>          boundTexture{};
    id<MTLBuffer>           currentVertexBuffer{};
    MTLViewport             viewport{};
    MTLScissorRect          scissorRect{};
    bool                    scissorEnabled{};
    Color                   clearColor;
    bool                    needsClear{};

    // Per-window rendering state
    struct WindowTarget
    {
        CAMetalLayer* layer{};
    };
    std::unordered_map<void*, WindowTarget> windowTargets;
    void* activeWindowHandle{};  //!< Non-null when rendering to a window (framebuffer 0)

    ~Impl()
    {
        flushEncoder();
    }

    ////////////////////////////////////////////////////////////
    /// \brief Get or create a pipeline state for the current draw configuration
    ////////////////////////////////////////////////////////////
    id<MTLRenderPipelineState> getOrCreatePipeline(MTLPixelFormat colorFormat)
    {
        PipelineKey key{currentBlendMode, currentTextured, currentColorMask};

        auto it = pipelineCache.find(key);
        if (it != pipelineCache.end())
            return it->second;

        // Create vertex descriptor matching sf::Vertex layout (20 bytes):
        //   offset 0:  float2 position
        //   offset 8:  uchar4 color
        //   offset 12: float2 texCoords
        MTLVertexDescriptor* vtxDesc = [[MTLVertexDescriptor alloc] init];

        vtxDesc.attributes[0].format      = MTLVertexFormatFloat2;
        vtxDesc.attributes[0].offset      = 0;
        vtxDesc.attributes[0].bufferIndex = 0;

        vtxDesc.attributes[1].format      = MTLVertexFormatUChar4;
        vtxDesc.attributes[1].offset      = 8;
        vtxDesc.attributes[1].bufferIndex = 0;

        vtxDesc.attributes[2].format      = MTLVertexFormatFloat2;
        vtxDesc.attributes[2].offset      = 12;
        vtxDesc.attributes[2].bufferIndex = 0;

        vtxDesc.layouts[0].stride       = sizeof(Vertex);
        vtxDesc.layouts[0].stepRate     = 1;
        vtxDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        // Create new pipeline state
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction   = vertexFunction;
        desc.fragmentFunction = currentTextured ? fragmentTextured : fragmentColor;
        desc.vertexDescriptor = vtxDesc;

        auto* colorAttachment         = desc.colorAttachments[0];
        colorAttachment.pixelFormat   = colorFormat;
        colorAttachment.blendingEnabled = YES;

        colorAttachment.rgbBlendOperation   = toMetalBlendOp(currentBlendMode.colorEquation);
        colorAttachment.alphaBlendOperation = toMetalBlendOp(currentBlendMode.alphaEquation);
        colorAttachment.sourceRGBBlendFactor        = toMetalBlendFactor(currentBlendMode.colorSrcFactor);
        colorAttachment.destinationRGBBlendFactor    = toMetalBlendFactor(currentBlendMode.colorDstFactor);
        colorAttachment.sourceAlphaBlendFactor       = toMetalBlendFactor(currentBlendMode.alphaSrcFactor);
        colorAttachment.destinationAlphaBlendFactor  = toMetalBlendFactor(currentBlendMode.alphaDstFactor);

        if (currentColorMask)
            colorAttachment.writeMask = MTLColorWriteMaskAll;
        else
            colorAttachment.writeMask = MTLColorWriteMaskNone;

        NSError* error = nil;
        id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];

        if (!pipeline)
        {
            err() << "Failed to create Metal pipeline state: " << [[error localizedDescription] UTF8String] << std::endl;
            return nil;
        }

        pipelineCache[key] = pipeline;
        return pipeline;
    }


    ////////////////////////////////////////////////////////////
    /// \brief Ensure a render command encoder is active
    ////////////////////////////////////////////////////////////
    void ensureEncoder()
    {
        if (metalState.encoder)
            return;

        // Determine target texture
        id<MTLTexture> targetTexture = nil;

        if (currentFramebuffer == 0 && activeWindowHandle)
        {
            // Rendering to a window — get drawable from CAMetalLayer
            auto winIt = windowTargets.find(activeWindowHandle);
            if (winIt != windowTargets.end())
            {
                if (!metalState.currentDrawable)
                    metalState.currentDrawable = [winIt->second.layer nextDrawable];
                if (metalState.currentDrawable)
                    targetTexture = [metalState.currentDrawable texture];
            }
        }
        else
        {
            // Rendering to a framebuffer (RenderTexture)
            auto fbIt = framebuffers.find(currentFramebuffer);
            if (fbIt != framebuffers.end())
            {
                auto texIt = textures.find(fbIt->second.textureHandle);
                if (texIt != textures.end())
                    targetTexture = texIt->second;
            }
        }

        if (!targetTexture)
            return; // No valid render target

        // Create command buffer if needed
        if (!metalState.commandBuffer)
            metalState.commandBuffer = [commandQueue commandBuffer];

        // Create render pass descriptor
        MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
        rpd.colorAttachments[0].texture = targetTexture;

        if (needsClear)
        {
            rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
            rpd.colorAttachments[0].clearColor = MTLClearColorMake(
                clearColor.r / 255.0, clearColor.g / 255.0, clearColor.b / 255.0, clearColor.a / 255.0);
            needsClear = false;
        }
        else
        {
            rpd.colorAttachments[0].loadAction = MTLLoadActionLoad;
        }

        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;

        metalState.encoder = [metalState.commandBuffer renderCommandEncoderWithDescriptor:rpd];

        // Set viewport and scissor
        [metalState.encoder setViewport:viewport];
        if (scissorEnabled)
            [metalState.encoder setScissorRect:scissorRect];
    }


    ////////////////////////////////////////////////////////////
    /// \brief End the current render encoder and commit
    ////////////////////////////////////////////////////////////
    void flushEncoder()
    {
        [metalState endEncoderIfActive];

        if (metalState.commandBuffer)
        {
            [metalState.commandBuffer commit];
            [metalState.commandBuffer waitUntilCompleted];
            metalState.commandBuffer = nil;
        }
    }
};


////////////////////////////////////////////////////////////
MetalBackend::MetalBackend() : m_impl(new Impl)
{
    @autoreleasepool
    {
        m_impl->device = MTLCreateSystemDefaultDevice();
        if (!m_impl->device)
        {
            err() << "Failed to create Metal device" << std::endl;
            return;
        }

        m_impl->commandQueue = [m_impl->device newCommandQueue];

        // Create encoder holder (ObjC object for reliable ARC management)
        m_impl->metalState = [[SFMLMetalState alloc] init];

        // Compile default shader library
        NSError* error = nil;
        NSString* source = [NSString stringWithUTF8String:defaultShaderSource];
        m_impl->defaultLibrary = [m_impl->device newLibraryWithSource:source options:nil error:&error];

        if (!m_impl->defaultLibrary)
        {
            err() << "Failed to compile Metal shaders: " << [[error localizedDescription] UTF8String] << std::endl;
            return;
        }

        m_impl->vertexFunction    = [m_impl->defaultLibrary newFunctionWithName:@"sfml_default_vertex"];
        m_impl->fragmentTextured  = [m_impl->defaultLibrary newFunctionWithName:@"sfml_fragment_textured"];
        m_impl->fragmentColor     = [m_impl->defaultLibrary newFunctionWithName:@"sfml_fragment_color"];

        // Create default samplers
        MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];

        samplerDesc.minFilter    = MTLSamplerMinMagFilterLinear;
        samplerDesc.magFilter    = MTLSamplerMinMagFilterLinear;
        samplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        samplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        m_impl->linearSampler = [m_impl->device newSamplerStateWithDescriptor:samplerDesc];

        samplerDesc.minFilter = MTLSamplerMinMagFilterNearest;
        samplerDesc.magFilter = MTLSamplerMinMagFilterNearest;
        m_impl->nearestSampler = [m_impl->device newSamplerStateWithDescriptor:samplerDesc];

        // Initialize identity matrices in uniforms
        static constexpr float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        std::memcpy(m_impl->uniforms.projection, identity, sizeof(identity));
        std::memcpy(m_impl->uniforms.model, identity, sizeof(identity));
        std::memcpy(m_impl->uniforms.textureMatrix, identity, sizeof(identity));
    }
}


////////////////////////////////////////////////////////////
MetalBackend::~MetalBackend()
{
    m_impl->flushEncoder();
    delete m_impl;
}


////////////////////////////////////////////////////////////
// Render target operations
////////////////////////////////////////////////////////////

void MetalBackend::clear(Color color)
{
    @autoreleasepool
    {
        // End current encoder so the next one picks up the clear via load action
        [m_impl->metalState endEncoderIfActive];

        m_impl->clearColor = color;
        m_impl->needsClear = true;

        // Don't force encoder creation — the clear load action will be
        // applied when ensureEncoder() is called by the next draw or present.
    }
}


////////////////////////////////////////////////////////////
void MetalBackend::clearStencil(StencilValue /* stencilValue */)
{
    // TODO: implement Metal stencil clear
}


////////////////////////////////////////////////////////////
void MetalBackend::clear(Color color, StencilValue /* stencilValue */)
{
    clear(color);
    // TODO: also clear stencil
}


////////////////////////////////////////////////////////////
void MetalBackend::setViewport(const IntRect& viewport, unsigned int /* targetHeight */)
{
    // Metal uses top-left origin like SFML, no Y-flip needed
    m_impl->viewport.originX = static_cast<double>(viewport.position.x);
    m_impl->viewport.originY = static_cast<double>(viewport.position.y);
    m_impl->viewport.width   = static_cast<double>(viewport.size.x);
    m_impl->viewport.height  = static_cast<double>(viewport.size.y);
    m_impl->viewport.znear   = 0.0;
    m_impl->viewport.zfar    = 1.0;

    if (m_impl->metalState.encoder)
        [m_impl->metalState.encoder setViewport:m_impl->viewport];
}


////////////////////////////////////////////////////////////
void MetalBackend::setScissor(const IntRect& scissor, bool enable, unsigned int /* targetHeight */)
{
    m_impl->scissorEnabled = enable;

    if (enable)
    {
        m_impl->scissorRect.x      = static_cast<NSUInteger>(scissor.position.x);
        m_impl->scissorRect.y      = static_cast<NSUInteger>(scissor.position.y);
        m_impl->scissorRect.width  = static_cast<NSUInteger>(scissor.size.x);
        m_impl->scissorRect.height = static_cast<NSUInteger>(scissor.size.y);
    }

    if (m_impl->metalState.encoder)
    {
        if (enable)
            [m_impl->metalState.encoder setScissorRect:m_impl->scissorRect];
        // When disabled, Metal doesn't have a "disable scissor" — we set it to the full viewport
    }
}


////////////////////////////////////////////////////////////
void MetalBackend::setSrgb(bool /* enable */)
{
    // Metal handles sRGB through pixel formats, not a global toggle
}


////////////////////////////////////////////////////////////
// State management
////////////////////////////////////////////////////////////

void MetalBackend::applyBlendMode(const BlendMode& mode)
{
    m_impl->currentBlendMode = mode;
    // Pipeline state will be updated at draw time
}


////////////////////////////////////////////////////////////
void MetalBackend::applyStencilMode(const StencilMode& /* mode */)
{
    // TODO: configure MTLDepthStencilDescriptor
}


////////////////////////////////////////////////////////////
void MetalBackend::setColorMask(bool enable)
{
    m_impl->currentColorMask = enable;
    // Pipeline state will be updated at draw time
}


////////////////////////////////////////////////////////////
// Drawing
////////////////////////////////////////////////////////////

void MetalBackend::setupVertexData(const Vertex* vertices, std::size_t count, bool textured)
{
    m_impl->currentTextured = textured;

    const auto byteSize = count * sizeof(Vertex);
    m_impl->currentVertexBuffer = [m_impl->device newBufferWithBytes:vertices
                                                              length:byteSize
                                                             options:MTLResourceStorageModeShared];
}


////////////////////////////////////////////////////////////
void MetalBackend::setupVertexBuffer(BackendBufferHandle buffer, bool textured)
{
    m_impl->currentTextured = textured;

    auto it = m_impl->buffers.find(buffer);
    if (it != m_impl->buffers.end())
        m_impl->currentVertexBuffer = it->second;
}


////////////////////////////////////////////////////////////
void MetalBackend::applyTransform(const Transform& projection, const Transform& model)
{
    std::memcpy(m_impl->uniforms.projection, projection.getMatrix(), 16 * sizeof(float));
    std::memcpy(m_impl->uniforms.model, model.getMatrix(), 16 * sizeof(float));
}


////////////////////////////////////////////////////////////
void MetalBackend::drawPrimitives(PrimitiveType type, std::size_t firstVertex, std::size_t vertexCount)
{
    @autoreleasepool
    {
        m_impl->ensureEncoder();
        if (!m_impl->metalState.encoder)
            return;

        // Determine target pixel format for pipeline creation
        MTLPixelFormat colorFormat = MTLPixelFormatBGRA8Unorm; // Default for window drawables

        if (m_impl->currentFramebuffer == 0 && m_impl->activeWindowHandle)
        {
            auto winIt = m_impl->windowTargets.find(m_impl->activeWindowHandle);
            if (winIt != m_impl->windowTargets.end())
                colorFormat = winIt->second.layer.pixelFormat;
        }
        else
        {
            auto fbIt = m_impl->framebuffers.find(m_impl->currentFramebuffer);
            if (fbIt != m_impl->framebuffers.end())
            {
                auto texIt = m_impl->textures.find(fbIt->second.textureHandle);
                if (texIt != m_impl->textures.end())
                    colorFormat = [texIt->second pixelFormat];
            }
        }

        // Get or create pipeline state
        id<MTLRenderPipelineState> pipeline = m_impl->getOrCreatePipeline(colorFormat);
        if (!pipeline)
            return;

        [m_impl->metalState.encoder setRenderPipelineState:pipeline];

        // Bind vertex buffer at index 0
        [m_impl->metalState.encoder setVertexBuffer:m_impl->currentVertexBuffer offset:0 atIndex:0];

        // Bind uniforms at index 1
        [m_impl->metalState.encoder setVertexBytes:&m_impl->uniforms length:sizeof(Uniforms) atIndex:1];

        // Bind texture and sampler if textured
        if (m_impl->currentTextured && m_impl->boundTexture)
        {
            [m_impl->metalState.encoder setFragmentTexture:m_impl->boundTexture atIndex:0];

            // Choose sampler based on texture settings
            [m_impl->metalState.encoder setFragmentSamplerState:m_impl->linearSampler atIndex:0];
        }

        [m_impl->metalState.encoder drawPrimitives:toMetalPrimitive(type)
                            vertexStart:firstVertex
                            vertexCount:vertexCount];
    }
}


////////////////////////////////////////////////////////////
// Texture operations
////////////////////////////////////////////////////////////

BackendTextureHandle MetalBackend::createTexture(Vector2u size, bool sRgb)
{
    @autoreleasepool
    {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:sRgb ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm
                                        width:size.x
                                       height:size.y
                                    mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        desc.storageMode = MTLStorageModePrivate;

        // For CPU upload we need shared storage; use private + blit for perf later
        // For now use shared to allow replaceRegion
        desc.storageMode = MTLStorageModeShared;

        id<MTLTexture> texture = [m_impl->device newTextureWithDescriptor:desc];
        if (!texture)
            return 0;

        const auto handle = m_impl->nextTextureHandle++;
        m_impl->textures[handle] = texture;
        m_impl->textureInfos[handle] = {size, false, false, false, sRgb};

        return handle;
    }
}


////////////////////////////////////////////////////////////
void MetalBackend::destroyTexture(BackendTextureHandle handle)
{
    m_impl->textures.erase(handle);
    m_impl->textureInfos.erase(handle);
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
    @autoreleasepool
    {
        auto dstIt = m_impl->textures.find(handle);
        auto srcIt = m_impl->textures.find(srcHandle);
        if (dstIt == m_impl->textures.end() || srcIt == m_impl->textures.end())
            return;

        // Must flush any active encoder before blit
        m_impl->flushEncoder();

        id<MTLCommandBuffer> cmdBuf = [m_impl->commandQueue commandBuffer];
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
}


////////////////////////////////////////////////////////////
void MetalBackend::updateTextureFromFramebuffer(BackendTextureHandle handle, Vector2u size, Vector2u dest)
{
    // The current framebuffer's texture IS the render target,
    // so we just need to copy from it to the destination texture
    auto fbIt = m_impl->framebuffers.find(m_impl->currentFramebuffer);
    if (fbIt == m_impl->framebuffers.end())
        return;

    updateTextureFromTexture(handle, fbIt->second.textureHandle, size, dest);
}


////////////////////////////////////////////////////////////
void MetalBackend::bindTexture(BackendTextureHandle handle, CoordinateType coordinateType)
{
    if (handle)
    {
        auto texIt  = m_impl->textures.find(handle);
        auto infoIt = m_impl->textureInfos.find(handle);

        if (texIt != m_impl->textures.end())
            m_impl->boundTexture = texIt->second;

        // Build texture matrix (same logic as GL backend)
        // Identity by default
        // clang-format off
        float matrix[16] = {1.f, 0.f, 0.f, 0.f,
                            0.f, 1.f, 0.f, 0.f,
                            0.f, 0.f, 1.f, 0.f,
                            0.f, 0.f, 0.f, 1.f};
        // clang-format on

        if (infoIt != m_impl->textureInfos.end())
        {
            const auto& info = infoIt->second;

            // Metal always uses exact texture sizes (no NPOT padding),
            // so actualSize == userSize. Only pixel coords and flipping matter.

            if (coordinateType == CoordinateType::Pixels)
            {
                // Scale [0..size] to [0..1]
                matrix[0] = 1.f / static_cast<float>(info.size.x);
                matrix[5] = 1.f / static_cast<float>(info.size.y);
            }

            if (info.flipped)
            {
                matrix[5]  = -matrix[5];
                matrix[13] = 1.f;
            }
        }

        std::memcpy(m_impl->uniforms.textureMatrix, matrix, sizeof(matrix));
    }
    else
    {
        m_impl->boundTexture = nil;

        // Reset texture matrix to identity
        static constexpr float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        std::memcpy(m_impl->uniforms.textureMatrix, identity, sizeof(identity));
    }
}


////////////////////////////////////////////////////////////
Image MetalBackend::readbackTexture(BackendTextureHandle handle, Vector2u size)
{
    auto it = m_impl->textures.find(handle);
    if (it == m_impl->textures.end())
        return {};

    // Flush any pending rendering to this texture
    m_impl->flushEncoder();

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size.x) * size.y * 4);
    MTLRegion region = MTLRegionMake2D(0, 0, size.x, size.y);
    [it->second getBytes:pixels.data() bytesPerRow:size.x * 4 fromRegion:region mipmapLevel:0];

    Image image(size, pixels.data());
    return image;
}


////////////////////////////////////////////////////////////
void MetalBackend::setTextureSmooth(BackendTextureHandle handle, bool smooth, bool /* hasMipmap */)
{
    auto it = m_impl->textureInfos.find(handle);
    if (it != m_impl->textureInfos.end())
        it->second.smooth = smooth;
}


////////////////////////////////////////////////////////////
void MetalBackend::setTextureRepeated(BackendTextureHandle handle, bool repeated)
{
    auto it = m_impl->textureInfos.find(handle);
    if (it != m_impl->textureInfos.end())
        it->second.repeated = repeated;
}


////////////////////////////////////////////////////////////
bool MetalBackend::generateMipmap(BackendTextureHandle handle, Vector2u /* size */, bool /* smooth */)
{
    @autoreleasepool
    {
        auto it = m_impl->textures.find(handle);
        if (it == m_impl->textures.end())
            return false;

        m_impl->flushEncoder();

        id<MTLCommandBuffer> cmdBuf = [m_impl->commandQueue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cmdBuf blitCommandEncoder];
        [blit generateMipmapsForTexture:it->second];
        [blit endEncoding];
        [cmdBuf commit];
        [cmdBuf waitUntilCompleted];

        return true;
    }
}


////////////////////////////////////////////////////////////
unsigned int MetalBackend::getMaxTextureSize() const
{
    if (!m_impl->device)
        return 0;

    if ([m_impl->device supportsFamily:MTLGPUFamilyApple3])
        return 16384;

    return 8192;
}


////////////////////////////////////////////////////////////
void MetalBackend::setTextureFlipped(BackendTextureHandle handle, bool flipped)
{
    auto it = m_impl->textureInfos.find(handle);
    if (it != m_impl->textureInfos.end())
        it->second.flipped = flipped;
}


////////////////////////////////////////////////////////////
Vector2u MetalBackend::getTextureActualSize(BackendTextureHandle handle) const
{
    auto it = m_impl->textureInfos.find(handle);
    if (it == m_impl->textureInfos.end())
        return {};

    // Metal always uses exact sizes (no power-of-two padding)
    return it->second.size;
}


////////////////////////////////////////////////////////////
// Shader operations (user shaders — not yet implemented)
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
}


////////////////////////////////////////////////////////////
void MetalBackend::bindShader(BackendShaderHandle /* handle */)
{
}


////////////////////////////////////////////////////////////
int MetalBackend::getUniformLocation(BackendShaderHandle /* handle */, const std::string& /* name */)
{
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
    @autoreleasepool
    {
        const auto byteSize = vertexCount * sizeof(Vertex);
        id<MTLBuffer> buffer = [m_impl->device newBufferWithLength:byteSize options:MTLResourceStorageModeShared];

        if (!buffer)
            return 0;

        const auto handle = m_impl->nextBufferHandle++;
        m_impl->buffers[handle] = buffer;
        return handle;
    }
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
    @autoreleasepool
    {
        auto dstIt = m_impl->buffers.find(destHandle);
        auto srcIt = m_impl->buffers.find(srcHandle);
        if (dstIt == m_impl->buffers.end() || srcIt == m_impl->buffers.end())
            return false;

        m_impl->flushEncoder();

        id<MTLCommandBuffer> cmdBuf = [m_impl->commandQueue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cmdBuf blitCommandEncoder];

        const auto byteSize = srcSize * sizeof(Vertex);
        [blit copyFromBuffer:srcIt->second sourceOffset:0 toBuffer:dstIt->second destinationOffset:0 size:byteSize];

        [blit endEncoding];
        [cmdBuf commit];
        [cmdBuf waitUntilCompleted];

        return true;
    }
}


////////////////////////////////////////////////////////////
// Framebuffer operations
////////////////////////////////////////////////////////////

BackendFramebufferHandle MetalBackend::createFramebuffer(Vector2u /* size */,
                                                          BackendTextureHandle texture,
                                                          const ContextSettings& /* settings */)
{
    const auto handle = m_impl->nextFramebufferHandle++;

    auto texIt = m_impl->textureInfos.find(texture);
    const bool sRgb = (texIt != m_impl->textureInfos.end()) && texIt->second.sRgb;

    m_impl->framebuffers[handle] = {texture, sRgb};
    return handle;
}


////////////////////////////////////////////////////////////
void MetalBackend::destroyFramebuffer(BackendFramebufferHandle handle)
{
    m_impl->framebuffers.erase(handle);
}


////////////////////////////////////////////////////////////
bool MetalBackend::bindFramebuffer(BackendFramebufferHandle handle)
{
    if (handle != m_impl->currentFramebuffer)
    {
        // Flush current encoder before switching targets
        m_impl->flushEncoder();
        m_impl->currentFramebuffer = handle;
    }
    return true;
}


////////////////////////////////////////////////////////////
bool MetalBackend::isFramebufferSrgb(BackendFramebufferHandle handle) const
{
    auto it = m_impl->framebuffers.find(handle);
    if (it == m_impl->framebuffers.end())
        return false;

    return it->second.sRgb;
}


////////////////////////////////////////////////////////////
void MetalBackend::updateFramebufferTexture(BackendFramebufferHandle handle, BackendTextureHandle texture)
{
    auto it = m_impl->framebuffers.find(handle);
    if (it == m_impl->framebuffers.end())
        return;

    // Flush if this is the active framebuffer (texture is changing)
    if (handle == m_impl->currentFramebuffer)
        m_impl->flushEncoder();

    it->second.textureHandle = texture;

    auto texIt = m_impl->textureInfos.find(texture);
    it->second.sRgb = (texIt != m_impl->textureInfos.end()) && texIt->second.sRgb;
}


////////////////////////////////////////////////////////////
// Capability queries
////////////////////////////////////////////////////////////

bool MetalBackend::isShaderAvailable() const
{
    return true;
}


////////////////////////////////////////////////////////////
bool MetalBackend::isGeometryShaderAvailable() const
{
    return false;
}


////////////////////////////////////////////////////////////
bool MetalBackend::isVertexBufferAvailable() const
{
    return true;
}


////////////////////////////////////////////////////////////
bool MetalBackend::isNonPowerOfTwoTextureSupported() const
{
    return true;
}


////////////////////////////////////////////////////////////
std::size_t MetalBackend::getMaxTextureUnits() const
{
    return 31;
}


////////////////////////////////////////////////////////////
// Pipeline operations
////////////////////////////////////////////////////////////

void MetalBackend::flushPipeline()
{
    m_impl->flushEncoder();
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
void MetalBackend::bindBuffer(BackendBufferHandle buffer)
{
    auto it = m_impl->buffers.find(buffer);
    if (it != m_impl->buffers.end())
        m_impl->currentVertexBuffer = it->second;
    else
        m_impl->currentVertexBuffer = nil;
}


////////////////////////////////////////////////////////////
unsigned int MetalBackend::getDefaultFramebufferBinding() const
{
    return 0;
}


////////////////////////////////////////////////////////////
bool MetalBackend::isFramebufferAvailable() const
{
    return true;
}


////////////////////////////////////////////////////////////
unsigned int MetalBackend::getMaxAntiAliasingLevel() const
{
    if (!m_impl->device)
        return 0;

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
    return true;
}


////////////////////////////////////////////////////////////
void MetalBackend::resetStates()
{
    // Reset tracked state to defaults
    m_impl->currentBlendMode = BlendMode{};
    m_impl->currentTextured  = false;
    m_impl->currentColorMask = true;
    m_impl->boundTexture     = nil;

    static constexpr float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(m_impl->uniforms.projection, identity, sizeof(identity));
    std::memcpy(m_impl->uniforms.model, identity, sizeof(identity));
    std::memcpy(m_impl->uniforms.textureMatrix, identity, sizeof(identity));
}


////////////////////////////////////////////////////////////
bool MetalBackend::copyBufferFallback(BackendBufferHandle destHandle,
                                      BackendBufferHandle srcHandle,
                                      std::size_t         srcSize)
{
    return copyBuffer(destHandle, srcHandle, srcSize);
}


////////////////////////////////////////////////////////////
void MetalBackend::prepareUniformUpdate(BackendShaderHandle /* handle */)
{
}


////////////////////////////////////////////////////////////
void MetalBackend::finalizeUniformUpdate()
{
}


////////////////////////////////////////////////////////////
// Window rendering lifecycle
////////////////////////////////////////////////////////////

void MetalBackend::initializeWindowRendering(void* nativeHandle, Vector2u size, const ContextSettings& /* settings */)
{
    @autoreleasepool
    {
        CAMetalLayer* layer = [CAMetalLayer layer];
        layer.device = m_impl->device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.drawableSize = CGSizeMake(static_cast<CGFloat>(size.x), static_cast<CGFloat>(size.y));

#if TARGET_OS_IPHONE
        // On iOS, the native handle is a UIWindow
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
        auto* uiWindow = (__bridge UIWindow*)nativeHandle;
#pragma clang diagnostic pop
        UIView* rootView = [uiWindow rootViewController].view;
        [rootView.layer addSublayer:layer];
        layer.frame = rootView.bounds;
#else
        // On macOS, the native handle is an NSWindow
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
        auto* window = (__bridge NSWindow*)nativeHandle;
#pragma clang diagnostic pop
        NSView* contentView = [window contentView];
        [contentView setWantsLayer:YES];
        [contentView setLayer:layer];
#endif

        m_impl->windowTargets[nativeHandle] = {layer};
        m_impl->activeWindowHandle = nativeHandle;
    }
}


////////////////////////////////////////////////////////////
void MetalBackend::destroyWindowRendering(void* nativeHandle)
{
    m_impl->flushEncoder();
    m_impl->windowTargets.erase(nativeHandle);

    if (m_impl->activeWindowHandle == nativeHandle)
        m_impl->activeWindowHandle = nullptr;
}


////////////////////////////////////////////////////////////
void MetalBackend::presentWindow(void* nativeHandle)
{
    @autoreleasepool
    {
        if (m_impl->windowTargets.find(nativeHandle) == m_impl->windowTargets.end())
            return;

        // If a clear is pending but no draws happened, we still need to
        // create an encoder so the clear load action produces a frame
        if (!m_impl->metalState.encoder && m_impl->needsClear)
            m_impl->ensureEncoder();

        // End the current render encoder
        [m_impl->metalState endEncoderIfActive];

        // Present the drawable and commit
        if (m_impl->metalState.commandBuffer && m_impl->metalState.currentDrawable)
        {
            [m_impl->metalState.commandBuffer presentDrawable:m_impl->metalState.currentDrawable];
            [m_impl->metalState.commandBuffer commit];
            [m_impl->metalState.commandBuffer waitUntilCompleted];
            m_impl->metalState.commandBuffer = nil;
        }

        // Release the drawable for next frame
        m_impl->metalState.currentDrawable = nil;
    }
}


////////////////////////////////////////////////////////////
void MetalBackend::setWindowVerticalSyncEnabled(void* nativeHandle, bool enabled)
{
    auto winIt = m_impl->windowTargets.find(nativeHandle);
    if (winIt == m_impl->windowTargets.end())
        return;

#if !TARGET_OS_IPHONE
    winIt->second.layer.displaySyncEnabled = enabled ? YES : NO;
#else
    (void)enabled; // iOS always syncs to display
#endif
}


////////////////////////////////////////////////////////////
bool MetalBackend::setWindowActive(void* nativeHandle, bool active)
{
    if (active)
        m_impl->activeWindowHandle = nativeHandle;
    else if (m_impl->activeWindowHandle == nativeHandle)
        m_impl->activeWindowHandle = nullptr;

    return true;
}

} // namespace sf::priv

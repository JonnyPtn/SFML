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
#include <SFML/Graphics/Backend/DX12/DX12Backend.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Transform.hpp>

#include <SFML/System/Err.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <ostream>
#include <vector>

#include <cstring>

using Microsoft::WRL::ComPtr;


namespace
{

////////////////////////////////////////////////////////////
// Default HLSL shader source
////////////////////////////////////////////////////////////
constexpr const char* defaultShaderSource = R"hlsl(
cbuffer Uniforms : register(b0)
{
    float4x4 projection;
    float4x4 model;
    float4x4 textureMatrix;
};

Texture2D    sfmlTexture : register(t0);
SamplerState sfmlSampler : register(s0);

struct VSInput
{
    float2 position  : POSITION;
    float4 color     : COLOR;
    float2 texCoords : TEXCOORD0;
};

struct PSInput
{
    float4 position  : SV_POSITION;
    float4 color     : COLOR;
    float2 texCoords : TEXCOORD0;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position  = mul(projection, mul(model, float4(input.position, 0.0, 1.0)));
    float4 tc        = mul(textureMatrix, float4(input.texCoords, 0.0, 1.0));
    output.texCoords = tc.xy;
    output.color     = input.color;
    return output;
}

float4 PSTextured(PSInput input) : SV_TARGET
{
    return input.color * sfmlTexture.Sample(sfmlSampler, input.texCoords);
}

float4 PSColor(PSInput input) : SV_TARGET
{
    return input.color;
}
)hlsl";


////////////////////////////////////////////////////////////
/// \brief Convert SFML blend factor to D3D12
////////////////////////////////////////////////////////////
D3D12_BLEND toDX12BlendFactor(sf::BlendMode::Factor factor)
{
    // clang-format off
    switch (factor)
    {
        case sf::BlendMode::Factor::Zero:             return D3D12_BLEND_ZERO;
        case sf::BlendMode::Factor::One:              return D3D12_BLEND_ONE;
        case sf::BlendMode::Factor::SrcColor:         return D3D12_BLEND_SRC_COLOR;
        case sf::BlendMode::Factor::OneMinusSrcColor: return D3D12_BLEND_INV_SRC_COLOR;
        case sf::BlendMode::Factor::DstColor:         return D3D12_BLEND_DEST_COLOR;
        case sf::BlendMode::Factor::OneMinusDstColor: return D3D12_BLEND_INV_DEST_COLOR;
        case sf::BlendMode::Factor::SrcAlpha:         return D3D12_BLEND_SRC_ALPHA;
        case sf::BlendMode::Factor::OneMinusSrcAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
        case sf::BlendMode::Factor::DstAlpha:         return D3D12_BLEND_DEST_ALPHA;
        case sf::BlendMode::Factor::OneMinusDstAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
    }
    // clang-format on
    return D3D12_BLEND_ZERO;
}


////////////////////////////////////////////////////////////
/// \brief Convert SFML blend equation to D3D12
////////////////////////////////////////////////////////////
D3D12_BLEND_OP toDX12BlendOp(sf::BlendMode::Equation eq)
{
    // clang-format off
    switch (eq)
    {
        case sf::BlendMode::Equation::Add:             return D3D12_BLEND_OP_ADD;
        case sf::BlendMode::Equation::Subtract:        return D3D12_BLEND_OP_SUBTRACT;
        case sf::BlendMode::Equation::ReverseSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
        case sf::BlendMode::Equation::Min:             return D3D12_BLEND_OP_MIN;
        case sf::BlendMode::Equation::Max:             return D3D12_BLEND_OP_MAX;
    }
    // clang-format on
    return D3D12_BLEND_OP_ADD;
}


////////////////////////////////////////////////////////////
/// \brief Convert SFML primitive type to D3D12 topology
////////////////////////////////////////////////////////////
D3D_PRIMITIVE_TOPOLOGY toDX12Topology(sf::PrimitiveType type)
{
    // clang-format off
    switch (type)
    {
        case sf::PrimitiveType::Points:        return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        case sf::PrimitiveType::Lines:         return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case sf::PrimitiveType::LineStrip:     return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case sf::PrimitiveType::Triangles:     return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case sf::PrimitiveType::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    }
    // clang-format on
    return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}


////////////////////////////////////////////////////////////
/// \brief Get the coarse topology type for pipeline state creation
////////////////////////////////////////////////////////////
D3D12_PRIMITIVE_TOPOLOGY_TYPE toDX12TopologyType(sf::PrimitiveType type)
{
    // clang-format off
    switch (type)
    {
        case sf::PrimitiveType::Points:        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        case sf::PrimitiveType::Lines:
        case sf::PrimitiveType::LineStrip:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case sf::PrimitiveType::Triangles:
        case sf::PrimitiveType::TriangleStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
    // clang-format on
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
}


////////////////////////////////////////////////////////////
/// \brief Align a value up to the given alignment
////////////////////////////////////////////////////////////
UINT64 alignUp(UINT64 value, UINT64 alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}


} // anonymous namespace


namespace sf::priv
{

////////////////////////////////////////////////////////////
/// \brief Pipeline cache key combining blend mode + textured flag + RT format + topology
////////////////////////////////////////////////////////////
struct PipelineKey
{
    BlendMode                     blendMode;
    bool                          textured{};
    bool                          colorMask{true};
    DXGI_FORMAT                   rtFormat{DXGI_FORMAT_R8G8B8A8_UNORM};
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType{D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE};

    bool operator==(const PipelineKey& rhs) const
    {
        return blendMode == rhs.blendMode && textured == rhs.textured && colorMask == rhs.colorMask &&
               rtFormat == rhs.rtFormat && topologyType == rhs.topologyType;
    }
};

struct PipelineKeyHash
{
    std::size_t operator()(const PipelineKey& k) const
    {
        auto h = static_cast<std::size_t>(static_cast<int>(k.blendMode.colorSrcFactor));
        h = h * 31 + static_cast<std::size_t>(static_cast<int>(k.blendMode.colorDstFactor));
        h = h * 31 + static_cast<std::size_t>(static_cast<int>(k.blendMode.colorEquation));
        h = h * 31 + static_cast<std::size_t>(static_cast<int>(k.blendMode.alphaSrcFactor));
        h = h * 31 + static_cast<std::size_t>(static_cast<int>(k.blendMode.alphaDstFactor));
        h = h * 31 + static_cast<std::size_t>(static_cast<int>(k.blendMode.alphaEquation));
        h = h * 31 + static_cast<std::size_t>(k.textured);
        h = h * 31 + static_cast<std::size_t>(k.colorMask);
        h = h * 31 + static_cast<std::size_t>(k.rtFormat);
        h = h * 31 + static_cast<std::size_t>(k.topologyType);
        return h;
    }
};


////////////////////////////////////////////////////////////
/// \brief Uniform buffer layout matching the HLSL Uniforms cbuffer
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
    UINT                 rtvSlot{};
};


////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////
static constexpr UINT swapChainBufferCount = 2;
static constexpr UINT maxRtvDescriptors    = 64;
static constexpr UINT maxSrvDescriptors    = 1024;


////////////////////////////////////////////////////////////
struct DX12Backend::Impl
{
    // Core D3D12 objects
    ComPtr<IDXGIFactory4>             dxgiFactory;
    ComPtr<ID3D12Device>              device;
    ComPtr<ID3D12CommandQueue>        commandQueue;
    ComPtr<ID3D12CommandAllocator>    commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    bool                              commandListOpen{};

    // Synchronization
    ComPtr<ID3D12Fence> fence;
    UINT64              fenceValue{};
    HANDLE              fenceEvent{};

    // Root signature and compiled shaders
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3DBlob>            defaultVS;
    ComPtr<ID3DBlob>            texturedPS;
    ComPtr<ID3DBlob>            colorPS;

    // Pipeline state cache
    std::unordered_map<PipelineKey, ComPtr<ID3D12PipelineState>, PipelineKeyHash> pipelineCache;

    // Descriptor heaps
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> srvHeap; // shader-visible
    UINT                         rtvDescriptorSize{};
    UINT                         srvDescriptorSize{};
    UINT                         nextRtvSlot{};
    UINT                         nextSrvSlot{};

    // Per-texture metadata
    struct TextureInfo
    {
        Vector2u              size;
        bool                  flipped{};
        bool                  smooth{};
        bool                  repeated{};
        bool                  sRgb{};
        D3D12_RESOURCE_STATES currentState{D3D12_RESOURCE_STATE_COMMON};
        UINT                  srvSlot{};
    };

    // Resource maps
    std::unordered_map<std::uint64_t, ComPtr<ID3D12Resource>> textures;
    std::unordered_map<std::uint64_t, TextureInfo>            textureInfos;
    std::unordered_map<std::uint64_t, ComPtr<ID3D12Resource>> buffers;
    std::unordered_map<std::uint64_t, std::size_t>            bufferSizes; // in vertices
    std::unordered_map<std::uint64_t, FramebufferData>        framebuffers;

    std::uint64_t nextTextureHandle{1};
    std::uint64_t nextBufferHandle{1};
    std::uint64_t nextShaderHandle{1};
    std::uint64_t nextFramebufferHandle{1};

    // Current draw state
    Uniforms                 uniforms{};
    BlendMode                currentBlendMode;
    bool                     currentTextured{};
    bool                     currentColorMask{true};
    BackendTextureHandle     boundTextureHandle{};
    D3D12_VERTEX_BUFFER_VIEW currentVBView{};
    D3D12_VIEWPORT           viewport{};
    D3D12_RECT               scissorRect{};
    bool                     scissorEnabled{};
    Color                    clearColor;
    bool                     needsClear{};

    // Transient vertex buffers (released after GPU flush)
    std::vector<ComPtr<ID3D12Resource>> transientBuffers;

    // Render target tracking
    BackendFramebufferHandle    currentFramebuffer{};
    ID3D12Resource*             currentRenderTarget{};
    D3D12_CPU_DESCRIPTOR_HANDLE currentRtvHandle{};
    DXGI_FORMAT                 currentRtFormat{DXGI_FORMAT_R8G8B8A8_UNORM};
    bool                        renderTargetSet{};

    // Per-window rendering state
    struct WindowTarget
    {
        ComPtr<IDXGISwapChain3> swapChain;
        ComPtr<ID3D12Resource>  backBuffers[swapChainBufferCount];
        D3D12_RESOURCE_STATES   backBufferStates[swapChainBufferCount]{}; // PRESENT == COMMON == 0
        UINT                    rtvBaseSlot{};
        UINT                    currentBackBuffer{};
        bool                    vsync{};
    };

    std::unordered_map<void*, WindowTarget> windowTargets;
    void*                                   activeWindowHandle{};


    ////////////////////////////////////////////////////////////
    ~Impl()
    {
        flushCommandList();
        if (fenceEvent)
            CloseHandle(fenceEvent);
    }


    ////////////////////////////////////////////////////////////
    void waitForGpu()
    {
        if (!commandQueue || !fence)
            return;

        ++fenceValue;
        commandQueue->Signal(fence.Get(), fenceValue);

        if (fence->GetCompletedValue() < fenceValue)
        {
            fence->SetEventOnCompletion(fenceValue, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }


    ////////////////////////////////////////////////////////////
    void ensureCommandList()
    {
        if (commandListOpen)
            return;

        commandAllocator->Reset();
        commandList->Reset(commandAllocator.Get(), nullptr);
        commandListOpen = true;

        // Bind shader-visible descriptor heap
        ID3D12DescriptorHeap* heaps[] = {srvHeap.Get()};
        commandList->SetDescriptorHeaps(1, heaps);
        commandList->SetGraphicsRootSignature(rootSignature.Get());
    }


    ////////////////////////////////////////////////////////////
    void ensureRenderTarget()
    {
        ensureCommandList();

        if (renderTargetSet)
            return;

        ID3D12Resource*             target    = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {};
        DXGI_FORMAT                 format    = DXGI_FORMAT_R8G8B8A8_UNORM;

        if (currentFramebuffer == 0 && activeWindowHandle)
        {
            auto winIt = windowTargets.find(activeWindowHandle);
            if (winIt != windowTargets.end())
            {
                auto& wt             = winIt->second;
                wt.currentBackBuffer = wt.swapChain->GetCurrentBackBufferIndex();
                target               = wt.backBuffers[wt.currentBackBuffer].Get();
                rtvHandle            = getRtvCpuHandle(wt.rtvBaseSlot + wt.currentBackBuffer);
                format               = DXGI_FORMAT_R8G8B8A8_UNORM;

                if (wt.backBufferStates[wt.currentBackBuffer] != D3D12_RESOURCE_STATE_RENDER_TARGET)
                {
                    D3D12_RESOURCE_BARRIER barrier{};
                    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Transition.pResource   = target;
                    barrier.Transition.StateBefore = wt.backBufferStates[wt.currentBackBuffer];
                    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    commandList->ResourceBarrier(1, &barrier);
                    wt.backBufferStates[wt.currentBackBuffer] = D3D12_RESOURCE_STATE_RENDER_TARGET;
                }
            }
        }
        else if (currentFramebuffer != 0)
        {
            auto fbIt = framebuffers.find(currentFramebuffer);
            if (fbIt != framebuffers.end())
            {
                auto texIt = textures.find(fbIt->second.textureHandle);
                if (texIt != textures.end())
                {
                    target    = texIt->second.Get();
                    rtvHandle = getRtvCpuHandle(fbIt->second.rtvSlot);
                    format    = fbIt->second.sRgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

                    transitionTexture(fbIt->second.textureHandle, D3D12_RESOURCE_STATE_RENDER_TARGET);
                }
            }
        }

        if (!target)
            return;

        currentRenderTarget = target;
        currentRtvHandle    = rtvHandle;
        currentRtFormat     = format;

        commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        commandList->RSSetViewports(1, &viewport);

        if (scissorEnabled)
            commandList->RSSetScissorRects(1, &scissorRect);
        else
        {
            D3D12_RECT fullRect = {0, 0, LONG_MAX, LONG_MAX};
            commandList->RSSetScissorRects(1, &fullRect);
        }

        if (needsClear)
        {
            const float c[4] = {clearColor.r / 255.f, clearColor.g / 255.f, clearColor.b / 255.f, clearColor.a / 255.f};
            commandList->ClearRenderTargetView(rtvHandle, c, 0, nullptr);
            needsClear = false;
        }

        renderTargetSet = true;
    }


    ////////////////////////////////////////////////////////////
    void flushCommandList()
    {
        if (!commandListOpen)
            return;

        commandList->Close();

        ID3D12CommandList* lists[] = {commandList.Get()};
        commandQueue->ExecuteCommandLists(1, lists);

        waitForGpu();

        commandListOpen = false;
        renderTargetSet = false;
        transientBuffers.clear();
    }


    ////////////////////////////////////////////////////////////
    void transitionTexture(std::uint64_t handle, D3D12_RESOURCE_STATES newState)
    {
        auto texIt  = textures.find(handle);
        auto infoIt = textureInfos.find(handle);
        if (texIt == textures.end() || infoIt == textureInfos.end())
            return;

        if (infoIt->second.currentState != newState)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = texIt->second.Get();
            barrier.Transition.StateBefore = infoIt->second.currentState;
            barrier.Transition.StateAfter  = newState;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &barrier);
            infoIt->second.currentState = newState;
        }
    }


    ////////////////////////////////////////////////////////////
    D3D12_CPU_DESCRIPTOR_HANDLE getRtvCpuHandle(UINT slot) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(slot) * rtvDescriptorSize;
        return handle;
    }


    ////////////////////////////////////////////////////////////
    D3D12_CPU_DESCRIPTOR_HANDLE getSrvCpuHandle(UINT slot) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = srvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(slot) * srvDescriptorSize;
        return handle;
    }


    ////////////////////////////////////////////////////////////
    D3D12_GPU_DESCRIPTOR_HANDLE getSrvGpuHandle(UINT slot) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = srvHeap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<UINT64>(slot) * srvDescriptorSize;
        return handle;
    }


    ////////////////////////////////////////////////////////////
    ID3D12PipelineState* getOrCreatePipeline(DXGI_FORMAT colorFormat, D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType)
    {
        PipelineKey key{currentBlendMode, currentTextured, currentColorMask, colorFormat, topologyType};

        auto it = pipelineCache.find(key);
        if (it != pipelineCache.end())
            return it->second.Get();

        // Input layout matching sf::Vertex (20 bytes)
        D3D12_INPUT_ELEMENT_DESC inputElements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = rootSignature.Get();
        desc.VS             = {defaultVS->GetBufferPointer(), defaultVS->GetBufferSize()};
        desc.PS             = currentTextured
                                  ? D3D12_SHADER_BYTECODE{texturedPS->GetBufferPointer(), texturedPS->GetBufferSize()}
                                  : D3D12_SHADER_BYTECODE{colorPS->GetBufferPointer(), colorPS->GetBufferSize()};
        desc.InputLayout    = {inputElements, _countof(inputElements)};

        // Blend state
        auto& rt0                 = desc.BlendState.RenderTarget[0];
        rt0.BlendEnable           = TRUE;
        rt0.SrcBlend              = toDX12BlendFactor(currentBlendMode.colorSrcFactor);
        rt0.DestBlend             = toDX12BlendFactor(currentBlendMode.colorDstFactor);
        rt0.BlendOp               = toDX12BlendOp(currentBlendMode.colorEquation);
        rt0.SrcBlendAlpha         = toDX12BlendFactor(currentBlendMode.alphaSrcFactor);
        rt0.DestBlendAlpha        = toDX12BlendFactor(currentBlendMode.alphaDstFactor);
        rt0.BlendOpAlpha          = toDX12BlendOp(currentBlendMode.alphaEquation);
        rt0.RenderTargetWriteMask = currentColorMask ? D3D12_COLOR_WRITE_ENABLE_ALL : 0;

        // Rasterizer state
        desc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable = TRUE;

        // Depth-stencil disabled
        desc.DepthStencilState.DepthEnable   = FALSE;
        desc.DepthStencilState.StencilEnable = FALSE;

        desc.SampleMask            = UINT_MAX;
        desc.PrimitiveTopologyType = topologyType;
        desc.NumRenderTargets      = 1;
        desc.RTVFormats[0]         = colorFormat;
        desc.SampleDesc            = {1, 0};

        ComPtr<ID3D12PipelineState> pipeline;
        HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline));
        if (FAILED(hr))
        {
            err() << "Failed to create D3D12 pipeline state (HRESULT: 0x" << std::hex << hr << ")" << std::endl;
            return nullptr;
        }

        auto* result       = pipeline.Get();
        pipelineCache[key] = std::move(pipeline);
        return result;
    }
};


////////////////////////////////////////////////////////////
DX12Backend::DX12Backend() : m_impl(new Impl)
{
    HRESULT hr;

    // Create DXGI factory
    hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_impl->dxgiFactory));
    if (FAILED(hr))
    {
        err() << "Failed to create DXGI factory" << std::endl;
        return;
    }

    // Create D3D12 device with default adapter
    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_impl->device));
    if (FAILED(hr))
    {
        err() << "Failed to create D3D12 device" << std::endl;
        return;
    }

    // Create command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr             = m_impl->device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_impl->commandQueue));
    if (FAILED(hr))
    {
        err() << "Failed to create D3D12 command queue" << std::endl;
        return;
    }

    // Create command allocator and command list
    hr = m_impl->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&m_impl->commandAllocator));
    if (FAILED(hr))
    {
        err() << "Failed to create command allocator" << std::endl;
        return;
    }

    hr = m_impl->device->CreateCommandList(0,
                                           D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           m_impl->commandAllocator.Get(),
                                           nullptr,
                                           IID_PPV_ARGS(&m_impl->commandList));
    if (FAILED(hr))
    {
        err() << "Failed to create command list" << std::endl;
        return;
    }
    // Command list starts open; close it so ensureCommandList() can reset cleanly
    m_impl->commandList->Close();

    // Create fence for GPU synchronization
    hr = m_impl->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_impl->fence));
    if (FAILED(hr))
    {
        err() << "Failed to create D3D12 fence" << std::endl;
        return;
    }
    m_impl->fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    // Create RTV descriptor heap (non-shader-visible)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = maxRtvDescriptors;
        hr                      = m_impl->device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_impl->rtvHeap));
        if (FAILED(hr))
        {
            err() << "Failed to create RTV descriptor heap" << std::endl;
            return;
        }
        m_impl->rtvDescriptorSize =
            m_impl->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // Create SRV descriptor heap (shader-visible)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = maxSrvDescriptors;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = m_impl->device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_impl->srvHeap));
        if (FAILED(hr))
        {
            err() << "Failed to create SRV descriptor heap" << std::endl;
            return;
        }
        m_impl->srvDescriptorSize =
            m_impl->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // Create root signature
    //   Param 0: 48 root constants (3x float4x4) at b0, vertex-visible
    //   Param 1: Descriptor table with 1 SRV at t0, pixel-visible
    //   Static sampler at s0 (linear, clamp)
    {
        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors     = 1;
        srvRange.BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER rootParams[2]{};

        rootParams[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParams[0].Constants.ShaderRegister = 0;
        rootParams[0].Constants.Num32BitValues = 48;
        rootParams[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;

        rootParams[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[1].DescriptorTable.pDescriptorRanges   = &srvRange;
        rootParams[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC staticSampler{};
        staticSampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        staticSampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.MaxAnisotropy    = 1;
        staticSampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
        staticSampler.MaxLOD           = D3D12_FLOAT32_MAX;
        staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters     = 2;
        rsDesc.pParameters       = rootParams;
        rsDesc.NumStaticSamplers = 1;
        rsDesc.pStaticSamplers   = &staticSampler;
        rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> errorBlob;
        hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &errorBlob);
        if (FAILED(hr))
        {
            if (errorBlob)
                err() << "Root signature error: "
                      << static_cast<const char*>(errorBlob->GetBufferPointer()) << std::endl;
            return;
        }

        hr = m_impl->device->CreateRootSignature(
            0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_impl->rootSignature));
        if (FAILED(hr))
        {
            err() << "Failed to create root signature" << std::endl;
            return;
        }
    }

    // Compile default shaders
    {
        auto compile = [](const char* source, const char* entry, const char* target,
                          ComPtr<ID3DBlob>& blob) -> bool
        {
            ComPtr<ID3DBlob> errBlob;
            HRESULT          compileHr =
                D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entry, target, 0, 0, &blob, &errBlob);
            if (FAILED(compileHr))
            {
                if (errBlob)
                    sf::err() << "Shader compile error (" << entry
                              << "): " << static_cast<const char*>(errBlob->GetBufferPointer()) << std::endl;
                return false;
            }
            return true;
        };

        if (!compile(defaultShaderSource, "VSMain", "vs_5_0", m_impl->defaultVS) ||
            !compile(defaultShaderSource, "PSTextured", "ps_5_0", m_impl->texturedPS) ||
            !compile(defaultShaderSource, "PSColor", "ps_5_0", m_impl->colorPS))
        {
            err() << "Failed to compile default DX12 shaders" << std::endl;
            return;
        }
    }

    // Initialize identity matrices in uniforms
    static constexpr float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(m_impl->uniforms.projection, identity, sizeof(identity));
    std::memcpy(m_impl->uniforms.model, identity, sizeof(identity));
    std::memcpy(m_impl->uniforms.textureMatrix, identity, sizeof(identity));
}


////////////////////////////////////////////////////////////
DX12Backend::~DX12Backend()
{
    m_impl->flushCommandList();
    delete m_impl;
}


////////////////////////////////////////////////////////////
// Render target operations
////////////////////////////////////////////////////////////

void DX12Backend::clear(Color color)
{
    // Invalidate render target so the next ensureRenderTarget applies the clear
    m_impl->renderTargetSet = false;
    m_impl->clearColor      = color;
    m_impl->needsClear      = true;
}


////////////////////////////////////////////////////////////
void DX12Backend::clearStencil(StencilValue /* stencilValue */)
{
    // TODO: implement DX12 stencil clear
}


////////////////////////////////////////////////////////////
void DX12Backend::clear(Color color, StencilValue /* stencilValue */)
{
    clear(color);
    // TODO: also clear stencil
}


////////////////////////////////////////////////////////////
void DX12Backend::setViewport(const IntRect& viewport, unsigned int /* targetHeight */)
{
    // DX12 uses top-left origin like SFML, no Y-flip needed
    m_impl->viewport.TopLeftX = static_cast<float>(viewport.position.x);
    m_impl->viewport.TopLeftY = static_cast<float>(viewport.position.y);
    m_impl->viewport.Width    = static_cast<float>(viewport.size.x);
    m_impl->viewport.Height   = static_cast<float>(viewport.size.y);
    m_impl->viewport.MinDepth = 0.0f;
    m_impl->viewport.MaxDepth = 1.0f;

    if (m_impl->commandListOpen && m_impl->renderTargetSet)
        m_impl->commandList->RSSetViewports(1, &m_impl->viewport);
}


////////////////////////////////////////////////////////////
void DX12Backend::setScissor(const IntRect& scissor, bool enable, unsigned int /* targetHeight */)
{
    m_impl->scissorEnabled = enable;

    if (enable)
    {
        m_impl->scissorRect.left   = scissor.position.x;
        m_impl->scissorRect.top    = scissor.position.y;
        m_impl->scissorRect.right  = scissor.position.x + scissor.size.x;
        m_impl->scissorRect.bottom = scissor.position.y + scissor.size.y;
    }

    if (m_impl->commandListOpen && m_impl->renderTargetSet)
    {
        if (enable)
            m_impl->commandList->RSSetScissorRects(1, &m_impl->scissorRect);
        else
        {
            D3D12_RECT fullRect = {0, 0, LONG_MAX, LONG_MAX};
            m_impl->commandList->RSSetScissorRects(1, &fullRect);
        }
    }
}


////////////////////////////////////////////////////////////
void DX12Backend::setSrgb(bool /* enable */)
{
    // DX12 handles sRGB through resource formats, not a global toggle
}


////////////////////////////////////////////////////////////
// State management
////////////////////////////////////////////////////////////

void DX12Backend::applyBlendMode(const BlendMode& mode)
{
    m_impl->currentBlendMode = mode;
    // Pipeline state will be updated at draw time
}


////////////////////////////////////////////////////////////
void DX12Backend::applyStencilMode(const StencilMode& /* mode */)
{
    // TODO: configure depth-stencil state
}


////////////////////////////////////////////////////////////
void DX12Backend::setColorMask(bool enable)
{
    m_impl->currentColorMask = enable;
    // Pipeline state will be updated at draw time
}


////////////////////////////////////////////////////////////
// Drawing
////////////////////////////////////////////////////////////

void DX12Backend::setupVertexData(const Vertex* vertices, std::size_t count, bool textured)
{
    m_impl->currentTextured = textured;

    const auto byteSize = count * sizeof(Vertex);

    // Create transient upload buffer
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width            = byteSize;
    bufDesc.Height           = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels        = 1;
    bufDesc.SampleDesc       = {1, 0};
    bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadBuffer;
    HRESULT hr = m_impl->device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&uploadBuffer));
    if (FAILED(hr))
        return;

    // Copy vertex data
    void* mapped = nullptr;
    uploadBuffer->Map(0, nullptr, &mapped);
    std::memcpy(mapped, vertices, byteSize);
    uploadBuffer->Unmap(0, nullptr);

    // Set vertex buffer view
    m_impl->currentVBView.BufferLocation = uploadBuffer->GetGPUVirtualAddress();
    m_impl->currentVBView.SizeInBytes    = static_cast<UINT>(byteSize);
    m_impl->currentVBView.StrideInBytes  = sizeof(Vertex);

    // Keep alive until GPU flush
    m_impl->transientBuffers.push_back(std::move(uploadBuffer));
}


////////////////////////////////////////////////////////////
void DX12Backend::setupVertexBuffer(BackendBufferHandle buffer, bool textured)
{
    m_impl->currentTextured = textured;

    auto it     = m_impl->buffers.find(buffer);
    auto sizeIt = m_impl->bufferSizes.find(buffer);
    if (it != m_impl->buffers.end())
    {
        m_impl->currentVBView.BufferLocation = it->second->GetGPUVirtualAddress();
        m_impl->currentVBView.SizeInBytes =
            static_cast<UINT>((sizeIt != m_impl->bufferSizes.end() ? sizeIt->second : 0) * sizeof(Vertex));
        m_impl->currentVBView.StrideInBytes = sizeof(Vertex);
    }
}


////////////////////////////////////////////////////////////
void DX12Backend::applyTransform(const Transform& projection, const Transform& model)
{
    std::memcpy(m_impl->uniforms.projection, projection.getMatrix(), 16 * sizeof(float));
    std::memcpy(m_impl->uniforms.model, model.getMatrix(), 16 * sizeof(float));
}


////////////////////////////////////////////////////////////
void DX12Backend::drawPrimitives(PrimitiveType type, std::size_t firstVertex, std::size_t vertexCount)
{
    m_impl->ensureRenderTarget();
    if (!m_impl->currentRenderTarget)
        return;

    const auto topologyType = toDX12TopologyType(type);

    ID3D12PipelineState* pipeline = m_impl->getOrCreatePipeline(m_impl->currentRtFormat, topologyType);
    if (!pipeline)
        return;

    m_impl->commandList->SetPipelineState(pipeline);
    m_impl->commandList->IASetPrimitiveTopology(toDX12Topology(type));
    m_impl->commandList->IASetVertexBuffers(0, 1, &m_impl->currentVBView);

    // Upload uniforms via root constants
    m_impl->commandList->SetGraphicsRoot32BitConstants(0, 48, &m_impl->uniforms, 0);

    // Bind texture if textured
    if (m_impl->currentTextured && m_impl->boundTextureHandle)
    {
        auto infoIt = m_impl->textureInfos.find(m_impl->boundTextureHandle);
        if (infoIt != m_impl->textureInfos.end())
        {
            m_impl->transitionTexture(m_impl->boundTextureHandle, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            m_impl->commandList->SetGraphicsRootDescriptorTable(
                1, m_impl->getSrvGpuHandle(infoIt->second.srvSlot));
        }
    }

    m_impl->commandList->DrawInstanced(static_cast<UINT>(vertexCount), 1, static_cast<UINT>(firstVertex), 0);
}


////////////////////////////////////////////////////////////
// Texture operations
////////////////////////////////////////////////////////////

BackendTextureHandle DX12Backend::createTexture(Vector2u size, bool sRgb)
{
    const DXGI_FORMAT format = sRgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width            = size.x;
    texDesc.Height           = size.y;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels        = 1;
    texDesc.Format           = format;
    texDesc.SampleDesc       = {1, 0};
    texDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    ComPtr<ID3D12Resource> texture;
    HRESULT hr = m_impl->device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&texture));
    if (FAILED(hr))
        return 0;

    // Allocate SRV slot and create SRV
    const UINT srvSlot = m_impl->nextSrvSlot++;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format                    = format;
    srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels      = 1;

    m_impl->device->CreateShaderResourceView(texture.Get(), &srvDesc, m_impl->getSrvCpuHandle(srvSlot));

    const auto handle            = m_impl->nextTextureHandle++;
    m_impl->textures[handle]     = std::move(texture);
    m_impl->textureInfos[handle] = {size, false, false, false, sRgb, D3D12_RESOURCE_STATE_COMMON, srvSlot};

    return handle;
}


////////////////////////////////////////////////////////////
void DX12Backend::destroyTexture(BackendTextureHandle handle)
{
    m_impl->textures.erase(handle);
    m_impl->textureInfos.erase(handle);
}


////////////////////////////////////////////////////////////
void DX12Backend::updateTexture(BackendTextureHandle handle,
                                const std::uint8_t*  pixels,
                                Vector2u             size,
                                Vector2u             dest)
{
    auto texIt  = m_impl->textures.find(handle);
    auto infoIt = m_impl->textureInfos.find(handle);
    if (texIt == m_impl->textures.end() || !pixels)
        return;

    m_impl->ensureCommandList();

    const DXGI_FORMAT format   = (infoIt != m_impl->textureInfos.end() && infoIt->second.sRgb)
                                     ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                     : DXGI_FORMAT_R8G8B8A8_UNORM;
    const UINT        rowPitch = static_cast<UINT>(alignUp(size.x * 4, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
    const UINT64      uploadSize = static_cast<UINT64>(rowPitch) * size.y;

    // Create upload staging buffer
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width            = uploadSize;
    bufDesc.Height           = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels        = 1;
    bufDesc.SampleDesc       = {1, 0};
    bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadBuffer;
    HRESULT hr = m_impl->device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&uploadBuffer));
    if (FAILED(hr))
        return;

    // Copy pixels with row pitch alignment
    std::uint8_t* mapped = nullptr;
    uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    for (UINT row = 0; row < size.y; ++row)
        std::memcpy(mapped + row * rowPitch, pixels + row * size.x * 4, size.x * 4);
    uploadBuffer->Unmap(0, nullptr);

    // Transition texture to COPY_DEST
    m_impl->transitionTexture(handle, D3D12_RESOURCE_STATE_COPY_DEST);

    // Copy from upload buffer to texture
    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource                        = uploadBuffer.Get();
    srcLoc.Type                             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint.Footprint.Format   = format;
    srcLoc.PlacedFootprint.Footprint.Width    = size.x;
    srcLoc.PlacedFootprint.Footprint.Height   = size.y;
    srcLoc.PlacedFootprint.Footprint.Depth    = 1;
    srcLoc.PlacedFootprint.Footprint.RowPitch = rowPitch;

    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource        = texIt->second.Get();
    dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    m_impl->commandList->CopyTextureRegion(&dstLoc, dest.x, dest.y, 0, &srcLoc, nullptr);

    m_impl->transientBuffers.push_back(std::move(uploadBuffer));
}


////////////////////////////////////////////////////////////
void DX12Backend::updateTextureFromTexture(BackendTextureHandle handle,
                                           BackendTextureHandle srcHandle,
                                           Vector2u             srcSize,
                                           Vector2u             dest)
{
    auto dstIt = m_impl->textures.find(handle);
    auto srcIt = m_impl->textures.find(srcHandle);
    if (dstIt == m_impl->textures.end() || srcIt == m_impl->textures.end())
        return;

    m_impl->flushCommandList();
    m_impl->ensureCommandList();

    m_impl->transitionTexture(srcHandle, D3D12_RESOURCE_STATE_COPY_SOURCE);
    m_impl->transitionTexture(handle, D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource        = srcIt->second.Get();
    srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource        = dstIt->second.Get();
    dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_BOX srcBox = {0, 0, 0, srcSize.x, srcSize.y, 1};

    m_impl->commandList->CopyTextureRegion(&dstLoc, dest.x, dest.y, 0, &srcLoc, &srcBox);

    m_impl->flushCommandList();
}


////////////////////////////////////////////////////////////
void DX12Backend::updateTextureFromFramebuffer(BackendTextureHandle handle, Vector2u size, Vector2u dest)
{
    auto fbIt = m_impl->framebuffers.find(m_impl->currentFramebuffer);
    if (fbIt == m_impl->framebuffers.end())
        return;

    updateTextureFromTexture(handle, fbIt->second.textureHandle, size, dest);
}


////////////////////////////////////////////////////////////
void DX12Backend::bindTexture(BackendTextureHandle handle, CoordinateType coordinateType)
{
    if (handle)
    {
        m_impl->boundTextureHandle = handle;

        auto infoIt = m_impl->textureInfos.find(handle);

        // clang-format off
        float matrix[16] = {1.f, 0.f, 0.f, 0.f,
                            0.f, 1.f, 0.f, 0.f,
                            0.f, 0.f, 1.f, 0.f,
                            0.f, 0.f, 0.f, 1.f};
        // clang-format on

        if (infoIt != m_impl->textureInfos.end())
        {
            const auto& info = infoIt->second;

            if (coordinateType == CoordinateType::Pixels)
            {
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
        m_impl->boundTextureHandle = 0;

        static constexpr float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        std::memcpy(m_impl->uniforms.textureMatrix, identity, sizeof(identity));
    }
}


////////////////////////////////////////////////////////////
Image DX12Backend::readbackTexture(BackendTextureHandle handle, Vector2u size)
{
    auto texIt  = m_impl->textures.find(handle);
    auto infoIt = m_impl->textureInfos.find(handle);
    if (texIt == m_impl->textures.end())
        return {};

    m_impl->flushCommandList();
    m_impl->ensureCommandList();

    const UINT   rowPitch     = static_cast<UINT>(alignUp(size.x * 4, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
    const UINT64 readbackSize = static_cast<UINT64>(rowPitch) * size.y;

    // Create readback buffer
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width            = readbackSize;
    bufDesc.Height           = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels        = 1;
    bufDesc.SampleDesc       = {1, 0};
    bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> readbackBuffer;
    HRESULT hr = m_impl->device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&readbackBuffer));
    if (FAILED(hr))
        return {};

    m_impl->transitionTexture(handle, D3D12_RESOURCE_STATE_COPY_SOURCE);

    const DXGI_FORMAT format = (infoIt != m_impl->textureInfos.end() && infoIt->second.sRgb)
                                   ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                   : DXGI_FORMAT_R8G8B8A8_UNORM;

    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource        = texIt->second.Get();
    srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource                          = readbackBuffer.Get();
    dstLoc.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint.Footprint.Format   = format;
    dstLoc.PlacedFootprint.Footprint.Width    = size.x;
    dstLoc.PlacedFootprint.Footprint.Height   = size.y;
    dstLoc.PlacedFootprint.Footprint.Depth    = 1;
    dstLoc.PlacedFootprint.Footprint.RowPitch = rowPitch;

    m_impl->commandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    m_impl->flushCommandList();

    // Map and extract pixels
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size.x) * size.y * 4);
    std::uint8_t*             mapped = nullptr;
    readbackBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    for (UINT row = 0; row < size.y; ++row)
        std::memcpy(pixels.data() + row * size.x * 4, mapped + row * rowPitch, size.x * 4);
    readbackBuffer->Unmap(0, nullptr);

    return Image(size, pixels.data());
}


////////////////////////////////////////////////////////////
void DX12Backend::setTextureSmooth(BackendTextureHandle handle, bool smooth, bool /* hasMipmap */)
{
    auto it = m_impl->textureInfos.find(handle);
    if (it != m_impl->textureInfos.end())
        it->second.smooth = smooth;
}


////////////////////////////////////////////////////////////
void DX12Backend::setTextureRepeated(BackendTextureHandle handle, bool repeated)
{
    auto it = m_impl->textureInfos.find(handle);
    if (it != m_impl->textureInfos.end())
        it->second.repeated = repeated;
}


////////////////////////////////////////////////////////////
bool DX12Backend::generateMipmap(BackendTextureHandle /* handle */, Vector2u /* size */, bool /* smooth */)
{
    // TODO: implement mipmap generation (requires compute shader or multi-pass rendering)
    return false;
}


////////////////////////////////////////////////////////////
unsigned int DX12Backend::getMaxTextureSize() const
{
    return 16384; // D3D12 minimum guaranteed for feature level 11.0
}


////////////////////////////////////////////////////////////
void DX12Backend::setTextureFlipped(BackendTextureHandle handle, bool flipped)
{
    auto it = m_impl->textureInfos.find(handle);
    if (it != m_impl->textureInfos.end())
        it->second.flipped = flipped;
}


////////////////////////////////////////////////////////////
Vector2u DX12Backend::getTextureActualSize(BackendTextureHandle handle) const
{
    auto it = m_impl->textureInfos.find(handle);
    if (it == m_impl->textureInfos.end())
        return {};

    // DX12 always uses exact sizes (no power-of-two padding)
    return it->second.size;
}


////////////////////////////////////////////////////////////
// Shader operations (user shaders -- not yet implemented)
////////////////////////////////////////////////////////////

BackendShaderHandle DX12Backend::compileShader(std::string_view /* vertexShaderCode */,
                                               std::string_view /* geometryShaderCode */,
                                               std::string_view /* fragmentShaderCode */)
{
    // TODO: compile HLSL shaders (GLSL->HLSL cross-compilation or native HLSL)
    err() << "DX12 shader compilation not yet implemented" << std::endl;
    return 0;
}


////////////////////////////////////////////////////////////
void DX12Backend::destroyShader(BackendShaderHandle /* handle */) {}
void DX12Backend::bindShader(BackendShaderHandle /* handle */) {}
int  DX12Backend::getUniformLocation(BackendShaderHandle /* handle */, const std::string& /* name */) { return -1; }

void DX12Backend::setUniform(BackendShaderHandle, int, float) {}
void DX12Backend::setUniform(BackendShaderHandle, int, const Glsl::Vec2&) {}
void DX12Backend::setUniform(BackendShaderHandle, int, const Glsl::Vec3&) {}
void DX12Backend::setUniform(BackendShaderHandle, int, const Glsl::Vec4&) {}
void DX12Backend::setUniform(BackendShaderHandle, int, int) {}
void DX12Backend::setUniform(BackendShaderHandle, int, const Glsl::Ivec2&) {}
void DX12Backend::setUniform(BackendShaderHandle, int, const Glsl::Ivec3&) {}
void DX12Backend::setUniform(BackendShaderHandle, int, const Glsl::Ivec4&) {}
void DX12Backend::setUniform(BackendShaderHandle, int, bool) {}
void DX12Backend::setUniform(BackendShaderHandle, int, const Glsl::Bvec2&) {}
void DX12Backend::setUniform(BackendShaderHandle, int, const Glsl::Bvec3&) {}
void DX12Backend::setUniform(BackendShaderHandle, int, const Glsl::Bvec4&) {}
void DX12Backend::setUniform(BackendShaderHandle, int, const Glsl::Mat3&) {}
void DX12Backend::setUniform(BackendShaderHandle, int, const Glsl::Mat4&) {}

void DX12Backend::setUniformTexture(BackendShaderHandle, int, BackendTextureHandle, int) {}

void DX12Backend::setUniformArray(BackendShaderHandle, int, const float*, std::size_t) {}
void DX12Backend::setUniformArray(BackendShaderHandle, int, const Glsl::Vec2*, std::size_t) {}
void DX12Backend::setUniformArray(BackendShaderHandle, int, const Glsl::Vec3*, std::size_t) {}
void DX12Backend::setUniformArray(BackendShaderHandle, int, const Glsl::Vec4*, std::size_t) {}
void DX12Backend::setUniformArray(BackendShaderHandle, int, const Glsl::Mat3*, std::size_t) {}
void DX12Backend::setUniformArray(BackendShaderHandle, int, const Glsl::Mat4*, std::size_t) {}


////////////////////////////////////////////////////////////
// Vertex buffer operations
////////////////////////////////////////////////////////////

BackendBufferHandle DX12Backend::createBuffer(std::size_t vertexCount, VertexBuffer::Usage /* usage */)
{
    const auto byteSize = vertexCount * sizeof(Vertex);

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width            = byteSize;
    bufDesc.Height           = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels        = 1;
    bufDesc.SampleDesc       = {1, 0};
    bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> buffer;
    HRESULT hr = m_impl->device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&buffer));
    if (FAILED(hr))
        return 0;

    const auto handle            = m_impl->nextBufferHandle++;
    m_impl->buffers[handle]      = std::move(buffer);
    m_impl->bufferSizes[handle]  = vertexCount;
    return handle;
}


////////////////////////////////////////////////////////////
void DX12Backend::destroyBuffer(BackendBufferHandle handle)
{
    m_impl->buffers.erase(handle);
    m_impl->bufferSizes.erase(handle);
}


////////////////////////////////////////////////////////////
bool DX12Backend::updateBuffer(BackendBufferHandle handle,
                               const Vertex*       vertices,
                               std::size_t         count,
                               unsigned int        offset)
{
    auto it = m_impl->buffers.find(handle);
    if (it == m_impl->buffers.end() || !vertices)
        return false;

    const auto byteOffset = static_cast<std::size_t>(offset) * sizeof(Vertex);
    const auto byteSize   = count * sizeof(Vertex);

    void*        mapped    = nullptr;
    D3D12_RANGE  readRange = {0, 0};
    it->second->Map(0, &readRange, &mapped);
    std::memcpy(static_cast<char*>(mapped) + byteOffset, vertices, byteSize);
    it->second->Unmap(0, nullptr);

    return true;
}


////////////////////////////////////////////////////////////
bool DX12Backend::copyBuffer(BackendBufferHandle destHandle,
                             BackendBufferHandle srcHandle,
                             std::size_t         srcSize)
{
    auto dstIt = m_impl->buffers.find(destHandle);
    auto srcIt = m_impl->buffers.find(srcHandle);
    if (dstIt == m_impl->buffers.end() || srcIt == m_impl->buffers.end())
        return false;

    // Both buffers are on UPLOAD heap — do CPU-side copy via map
    const auto  byteSize  = srcSize * sizeof(Vertex);
    D3D12_RANGE readRange = {0, byteSize};
    D3D12_RANGE noRead    = {0, 0};

    void* srcMapped = nullptr;
    void* dstMapped = nullptr;
    srcIt->second->Map(0, &readRange, &srcMapped);
    dstIt->second->Map(0, &noRead, &dstMapped);

    std::memcpy(dstMapped, srcMapped, byteSize);

    D3D12_RANGE written = {0, byteSize};
    srcIt->second->Unmap(0, &noRead);
    dstIt->second->Unmap(0, &written);

    return true;
}


////////////////////////////////////////////////////////////
// Framebuffer operations
////////////////////////////////////////////////////////////

BackendFramebufferHandle DX12Backend::createFramebuffer(Vector2u /* size */,
                                                        BackendTextureHandle   texture,
                                                        const ContextSettings& /* settings */)
{
    const auto handle = m_impl->nextFramebufferHandle++;

    auto texInfoIt = m_impl->textureInfos.find(texture);
    const bool sRgb = (texInfoIt != m_impl->textureInfos.end()) && texInfoIt->second.sRgb;

    // Allocate RTV for the framebuffer texture
    const UINT rtvSlot = m_impl->nextRtvSlot++;

    auto resIt = m_impl->textures.find(texture);
    if (resIt != m_impl->textures.end())
    {
        const DXGI_FORMAT format = sRgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format        = format;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        m_impl->device->CreateRenderTargetView(resIt->second.Get(), &rtvDesc, m_impl->getRtvCpuHandle(rtvSlot));
    }

    m_impl->framebuffers[handle] = {texture, sRgb, rtvSlot};
    return handle;
}


////////////////////////////////////////////////////////////
void DX12Backend::destroyFramebuffer(BackendFramebufferHandle handle)
{
    m_impl->framebuffers.erase(handle);
}


////////////////////////////////////////////////////////////
bool DX12Backend::bindFramebuffer(BackendFramebufferHandle handle)
{
    if (handle != m_impl->currentFramebuffer)
    {
        m_impl->flushCommandList();
        m_impl->currentFramebuffer = handle;
        m_impl->renderTargetSet    = false;
    }
    return true;
}


////////////////////////////////////////////////////////////
bool DX12Backend::isFramebufferSrgb(BackendFramebufferHandle handle) const
{
    auto it = m_impl->framebuffers.find(handle);
    if (it == m_impl->framebuffers.end())
        return false;

    return it->second.sRgb;
}


////////////////////////////////////////////////////////////
void DX12Backend::updateFramebufferTexture(BackendFramebufferHandle handle, BackendTextureHandle texture)
{
    auto it = m_impl->framebuffers.find(handle);
    if (it == m_impl->framebuffers.end())
        return;

    if (handle == m_impl->currentFramebuffer)
        m_impl->flushCommandList();

    it->second.textureHandle = texture;

    auto texInfoIt = m_impl->textureInfos.find(texture);
    it->second.sRgb = (texInfoIt != m_impl->textureInfos.end()) && texInfoIt->second.sRgb;

    // Recreate RTV for new texture
    auto resIt = m_impl->textures.find(texture);
    if (resIt != m_impl->textures.end())
    {
        const DXGI_FORMAT format = it->second.sRgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format        = format;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        m_impl->device->CreateRenderTargetView(
            resIt->second.Get(), &rtvDesc, m_impl->getRtvCpuHandle(it->second.rtvSlot));
    }
}


////////////////////////////////////////////////////////////
// Capability queries
////////////////////////////////////////////////////////////

bool DX12Backend::isShaderAvailable() const
{
    return true;
}


////////////////////////////////////////////////////////////
bool DX12Backend::isGeometryShaderAvailable() const
{
    return true; // D3D12 feature level 11.0 supports geometry shaders
}


////////////////////////////////////////////////////////////
bool DX12Backend::isVertexBufferAvailable() const
{
    return true;
}


////////////////////////////////////////////////////////////
bool DX12Backend::isNonPowerOfTwoTextureSupported() const
{
    return true;
}


////////////////////////////////////////////////////////////
std::size_t DX12Backend::getMaxTextureUnits() const
{
    return 32;
}


////////////////////////////////////////////////////////////
// Pipeline operations
////////////////////////////////////////////////////////////

void DX12Backend::flushPipeline()
{
    m_impl->flushCommandList();
}


////////////////////////////////////////////////////////////
void DX12Backend::pushRenderStates()
{
    // No-op for DX12 (state is not global like OpenGL)
}


////////////////////////////////////////////////////////////
void DX12Backend::popRenderStates()
{
    // No-op for DX12 (state is not global like OpenGL)
}


////////////////////////////////////////////////////////////
void DX12Backend::bindBuffer(BackendBufferHandle buffer)
{
    auto it = m_impl->buffers.find(buffer);
    if (it != m_impl->buffers.end())
    {
        auto sizeIt                          = m_impl->bufferSizes.find(buffer);
        m_impl->currentVBView.BufferLocation = it->second->GetGPUVirtualAddress();
        m_impl->currentVBView.SizeInBytes =
            static_cast<UINT>((sizeIt != m_impl->bufferSizes.end() ? sizeIt->second : 0) * sizeof(Vertex));
        m_impl->currentVBView.StrideInBytes = sizeof(Vertex);
    }
    else
    {
        m_impl->currentVBView = {};
    }
}


////////////////////////////////////////////////////////////
unsigned int DX12Backend::getDefaultFramebufferBinding() const
{
    return 0;
}


////////////////////////////////////////////////////////////
bool DX12Backend::isFramebufferAvailable() const
{
    return true;
}


////////////////////////////////////////////////////////////
unsigned int DX12Backend::getMaxAntiAliasingLevel() const
{
    if (!m_impl->device)
        return 0;

    for (unsigned int level = 8; level >= 2; level /= 2)
    {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msData{};
        msData.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
        msData.SampleCount = level;

        if (SUCCEEDED(m_impl->device->CheckFeatureSupport(
                D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msData, sizeof(msData))) &&
            msData.NumQualityLevels > 0)
        {
            return level;
        }
    }

    return 0;
}


////////////////////////////////////////////////////////////
bool DX12Backend::isSrgbTextureAvailable() const
{
    return true;
}


////////////////////////////////////////////////////////////
void DX12Backend::resetStates()
{
    m_impl->currentBlendMode = BlendMode{};
    m_impl->currentTextured  = false;
    m_impl->currentColorMask = true;
    m_impl->boundTextureHandle = 0;

    static constexpr float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(m_impl->uniforms.projection, identity, sizeof(identity));
    std::memcpy(m_impl->uniforms.model, identity, sizeof(identity));
    std::memcpy(m_impl->uniforms.textureMatrix, identity, sizeof(identity));
}


////////////////////////////////////////////////////////////
bool DX12Backend::copyBufferFallback(BackendBufferHandle destHandle,
                                     BackendBufferHandle srcHandle,
                                     std::size_t         srcSize)
{
    return copyBuffer(destHandle, srcHandle, srcSize);
}


////////////////////////////////////////////////////////////
void DX12Backend::prepareUniformUpdate(BackendShaderHandle /* handle */) {}
void DX12Backend::finalizeUniformUpdate() {}


////////////////////////////////////////////////////////////
// Window rendering lifecycle
////////////////////////////////////////////////////////////

void DX12Backend::initializeWindowRendering(void* nativeHandle, Vector2u size, const ContextSettings& /* settings */)
{
    auto hwnd = static_cast<HWND>(nativeHandle);

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width       = size.x;
    swapChainDesc.Height      = size.y;
    swapChainDesc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc  = {1, 0};
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = swapChainBufferCount;
    swapChainDesc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> swapChain1;
    HRESULT hr = m_impl->dxgiFactory->CreateSwapChainForHwnd(
        m_impl->commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &swapChain1);
    if (FAILED(hr))
    {
        err() << "Failed to create DX12 swap chain" << std::endl;
        return;
    }

    // Disable Alt+Enter fullscreen toggle
    m_impl->dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    Impl::WindowTarget wt;
    hr = swapChain1.As(&wt.swapChain);
    if (FAILED(hr))
    {
        err() << "Failed to get IDXGISwapChain3 interface" << std::endl;
        return;
    }

    // Allocate RTV slots for back buffers
    wt.rtvBaseSlot = m_impl->nextRtvSlot;
    m_impl->nextRtvSlot += swapChainBufferCount;

    // Get back buffer resources and create RTVs
    for (UINT i = 0; i < swapChainBufferCount; ++i)
    {
        wt.swapChain->GetBuffer(i, IID_PPV_ARGS(&wt.backBuffers[i]));
        m_impl->device->CreateRenderTargetView(
            wt.backBuffers[i].Get(), nullptr, m_impl->getRtvCpuHandle(wt.rtvBaseSlot + i));
        wt.backBufferStates[i] = D3D12_RESOURCE_STATE_PRESENT;
    }

    wt.currentBackBuffer = wt.swapChain->GetCurrentBackBufferIndex();

    m_impl->windowTargets[nativeHandle] = std::move(wt);
    m_impl->activeWindowHandle          = nativeHandle;
}


////////////////////////////////////////////////////////////
void DX12Backend::destroyWindowRendering(void* nativeHandle)
{
    m_impl->flushCommandList();
    m_impl->windowTargets.erase(nativeHandle);

    if (m_impl->activeWindowHandle == nativeHandle)
        m_impl->activeWindowHandle = nullptr;
}


////////////////////////////////////////////////////////////
void DX12Backend::presentWindow(void* nativeHandle)
{
    auto winIt = m_impl->windowTargets.find(nativeHandle);
    if (winIt == m_impl->windowTargets.end())
        return;

    auto& wt = winIt->second;

    // If a clear is pending but no draws happened, apply it now
    if (m_impl->needsClear)
        m_impl->ensureRenderTarget();

    m_impl->ensureCommandList();

    // Transition back buffer to PRESENT
    if (wt.backBufferStates[wt.currentBackBuffer] != D3D12_RESOURCE_STATE_PRESENT)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = wt.backBuffers[wt.currentBackBuffer].Get();
        barrier.Transition.StateBefore = wt.backBufferStates[wt.currentBackBuffer];
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_impl->commandList->ResourceBarrier(1, &barrier);
        wt.backBufferStates[wt.currentBackBuffer] = D3D12_RESOURCE_STATE_PRESENT;
    }

    m_impl->flushCommandList();

    wt.swapChain->Present(wt.vsync ? 1 : 0, 0);
    wt.currentBackBuffer = wt.swapChain->GetCurrentBackBufferIndex();
}


////////////////////////////////////////////////////////////
void DX12Backend::setWindowVerticalSyncEnabled(void* nativeHandle, bool enabled)
{
    auto winIt = m_impl->windowTargets.find(nativeHandle);
    if (winIt != m_impl->windowTargets.end())
        winIt->second.vsync = enabled;
}


////////////////////////////////////////////////////////////
bool DX12Backend::setWindowActive(void* nativeHandle, bool active)
{
    if (active)
        m_impl->activeWindowHandle = nativeHandle;
    else if (m_impl->activeWindowHandle == nativeHandle)
        m_impl->activeWindowHandle = nullptr;

    return true;
}

} // namespace sf::priv

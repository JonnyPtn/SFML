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
#include <SFML/Graphics/Backend/Vulkan/VulkanBackend.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Transform.hpp>

#include <SFML/System/Err.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__linux__)
#include <X11/Xlib.h>
#define VK_USE_PLATFORM_XLIB_KHR
#endif

#include <vulkan/vulkan.h>

#include <ostream>
#include <vector>

#include <cstring>

// SPIR-V shader bytecode (generated at build time)
#include "default_vert_spv.h"
#include "textured_frag_spv.h"
#include "color_frag_spv.h"


namespace
{

////////////////////////////////////////////////////////////
/// \brief Convert SFML blend factor to Vulkan
////////////////////////////////////////////////////////////
VkBlendFactor toVkBlendFactor(sf::BlendMode::Factor factor)
{
    // clang-format off
    switch (factor)
    {
        case sf::BlendMode::Factor::Zero:             return VK_BLEND_FACTOR_ZERO;
        case sf::BlendMode::Factor::One:              return VK_BLEND_FACTOR_ONE;
        case sf::BlendMode::Factor::SrcColor:         return VK_BLEND_FACTOR_SRC_COLOR;
        case sf::BlendMode::Factor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case sf::BlendMode::Factor::DstColor:         return VK_BLEND_FACTOR_DST_COLOR;
        case sf::BlendMode::Factor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case sf::BlendMode::Factor::SrcAlpha:         return VK_BLEND_FACTOR_SRC_ALPHA;
        case sf::BlendMode::Factor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case sf::BlendMode::Factor::DstAlpha:         return VK_BLEND_FACTOR_DST_ALPHA;
        case sf::BlendMode::Factor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    // clang-format on
    return VK_BLEND_FACTOR_ZERO;
}


////////////////////////////////////////////////////////////
/// \brief Convert SFML blend equation to Vulkan
////////////////////////////////////////////////////////////
VkBlendOp toVkBlendOp(sf::BlendMode::Equation eq)
{
    // clang-format off
    switch (eq)
    {
        case sf::BlendMode::Equation::Add:             return VK_BLEND_OP_ADD;
        case sf::BlendMode::Equation::Subtract:        return VK_BLEND_OP_SUBTRACT;
        case sf::BlendMode::Equation::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case sf::BlendMode::Equation::Min:             return VK_BLEND_OP_MIN;
        case sf::BlendMode::Equation::Max:             return VK_BLEND_OP_MAX;
    }
    // clang-format on
    return VK_BLEND_OP_ADD;
}


////////////////////////////////////////////////////////////
/// \brief Convert SFML primitive type to Vulkan topology
////////////////////////////////////////////////////////////
VkPrimitiveTopology toVkTopology(sf::PrimitiveType type)
{
    // clang-format off
    switch (type)
    {
        case sf::PrimitiveType::Points:        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case sf::PrimitiveType::Lines:         return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case sf::PrimitiveType::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case sf::PrimitiveType::Triangles:     return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case sf::PrimitiveType::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    }
    // clang-format on
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}


} // anonymous namespace


namespace sf::priv
{

////////////////////////////////////////////////////////////
/// \brief Pipeline cache key combining blend mode + textured flag + colorMask + format + topology
////////////////////////////////////////////////////////////
struct PipelineKey
{
    BlendMode           blendMode;
    bool                textured{};
    bool                colorMask{true};
    VkFormat            format{VK_FORMAT_R8G8B8A8_UNORM};
    VkPrimitiveTopology topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};

    bool operator==(const PipelineKey& rhs) const
    {
        return blendMode == rhs.blendMode && textured == rhs.textured && colorMask == rhs.colorMask &&
               format == rhs.format && topology == rhs.topology;
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
        h = h * 31 + static_cast<std::size_t>(k.format);
        h = h * 31 + static_cast<std::size_t>(k.topology);
        return h;
    }
};


////////////////////////////////////////////////////////////
/// \brief Uniform layout matching push constant data (3x mat4 = 192 bytes)
////////////////////////////////////////////////////////////
struct Uniforms
{
    float projection[16];
    float model[16];
    float textureMatrix[16];
};


////////////////////////////////////////////////////////////
/// \brief Render pass cache key
////////////////////////////////////////////////////////////
struct RenderPassKey
{
    VkFormat             format;
    VkAttachmentLoadOp   loadOp;

    bool operator==(const RenderPassKey& rhs) const
    {
        return format == rhs.format && loadOp == rhs.loadOp;
    }
};

struct RenderPassKeyHash
{
    std::size_t operator()(const RenderPassKey& k) const
    {
        return std::hash<uint32_t>{}(static_cast<uint32_t>(k.format)) ^
               (std::hash<uint32_t>{}(static_cast<uint32_t>(k.loadOp)) << 16);
    }
};


////////////////////////////////////////////////////////////
/// \brief Framebuffer data for render-to-texture
////////////////////////////////////////////////////////////
struct FramebufferData
{
    BackendTextureHandle textureHandle{};
    bool                 sRgb{};
    VkFramebuffer        framebuffer{VK_NULL_HANDLE};
    VkRenderPass         compatibleRenderPass{VK_NULL_HANDLE};
    Vector2u             size;
};


////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////
static constexpr uint32_t swapChainBufferCount  = 2;
static constexpr uint32_t maxDescriptorSets     = 1024;


////////////////////////////////////////////////////////////
struct VulkanBackend::Impl
{
    // Core Vulkan objects
    VkInstance       instance{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice         device{VK_NULL_HANDLE};
    VkQueue          graphicsQueue{VK_NULL_HANDLE};
    uint32_t         graphicsQueueFamily{0};

    // Command recording
    VkCommandPool   commandPool{VK_NULL_HANDLE};
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
    bool            commandBufferActive{};
    bool            renderPassActive{};

    // Synchronization
    VkFence frameFence{VK_NULL_HANDLE};

    // Shader modules
    VkShaderModule vertexShader{VK_NULL_HANDLE};
    VkShaderModule texturedFragShader{VK_NULL_HANDLE};
    VkShaderModule colorFragShader{VK_NULL_HANDLE};

    // Pipeline layout
    VkDescriptorSetLayout descriptorSetLayout{VK_NULL_HANDLE};
    VkPipelineLayout      pipelineLayout{VK_NULL_HANDLE};

    // Descriptor pool
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};

    // Samplers
    VkSampler linearSampler{VK_NULL_HANDLE};
    VkSampler nearestSampler{VK_NULL_HANDLE};

    // Pipeline state cache
    std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> pipelineCache;

    // Render pass cache
    std::unordered_map<RenderPassKey, VkRenderPass, RenderPassKeyHash> renderPassCache;

    // Per-texture metadata
    struct TextureInfo
    {
        VkImage          image{VK_NULL_HANDLE};
        VkDeviceMemory   memory{VK_NULL_HANDLE};
        VkImageView      imageView{VK_NULL_HANDLE};
        VkDescriptorSet  descriptorSet{VK_NULL_HANDLE};
        VkImageLayout    currentLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        Vector2u         size;
        bool             flipped{};
        bool             smooth{};
        bool             repeated{};
        bool             sRgb{};
    };

    // Per-buffer metadata
    struct BufferInfo
    {
        VkBuffer       buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        std::size_t    vertexCount{};
    };

    // Resource maps
    std::unordered_map<std::uint64_t, TextureInfo>    textureInfos;
    std::unordered_map<std::uint64_t, BufferInfo>     bufferInfos;
    std::unordered_map<std::uint64_t, FramebufferData> framebuffers;

    std::uint64_t nextTextureHandle{1};
    std::uint64_t nextBufferHandle{1};
    std::uint64_t nextShaderHandle{1};
    std::uint64_t nextFramebufferHandle{1};

    // Current draw state
    Uniforms             uniforms{};
    BlendMode            currentBlendMode;
    bool                 currentTextured{};
    bool                 currentColorMask{true};
    BackendTextureHandle boundTextureHandle{};
    VkBuffer             currentVertexBuffer{VK_NULL_HANDLE};
    VkDeviceSize         currentVertexBufferOffset{};
    std::size_t          currentVertexCount{};
    VkViewport           viewport{};
    VkRect2D             scissorRect{};
    bool                 scissorEnabled{};
    Color                clearColor;
    bool                 needsClear{};

    // Transient vertex buffers (released after GPU flush)
    struct TransientBuffer
    {
        VkBuffer       buffer;
        VkDeviceMemory memory;
    };
    std::vector<TransientBuffer> transientBuffers;

    // Render target tracking
    BackendFramebufferHandle currentFramebuffer{};
    VkFormat                 currentRtFormat{VK_FORMAT_R8G8B8A8_UNORM};

    // Per-window rendering state
    struct WindowTarget
    {
        VkSurfaceKHR     surface{VK_NULL_HANDLE};
        VkSwapchainKHR   swapchain{VK_NULL_HANDLE};
        VkFormat         swapchainFormat{VK_FORMAT_B8G8R8A8_UNORM};
        VkExtent2D       swapchainExtent{};

        std::vector<VkImage>       swapchainImages;
        std::vector<VkImageView>   swapchainImageViews;
        std::vector<VkFramebuffer> swapchainFramebuffers;
        std::vector<VkImageLayout> swapchainImageLayouts;

        VkSemaphore imageAvailableSemaphore{VK_NULL_HANDLE};
        VkSemaphore renderFinishedSemaphore{VK_NULL_HANDLE};

        uint32_t currentImageIndex{0};
        bool     imageAcquired{};
        bool     vsync{true};
    };

    std::unordered_map<void*, WindowTarget> windowTargets;
    void*                                   activeWindowHandle{};

    // Physical device properties
    VkPhysicalDeviceProperties    deviceProperties{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};


    ////////////////////////////////////////////////////////////
    ~Impl()
    {
        if (device)
        {
            vkDeviceWaitIdle(device);

            // Destroy transient buffers
            for (auto& tb : transientBuffers)
            {
                vkDestroyBuffer(device, tb.buffer, nullptr);
                vkFreeMemory(device, tb.memory, nullptr);
            }
            transientBuffers.clear();

            // Destroy pipelines
            for (auto& [key, pipeline] : pipelineCache)
                vkDestroyPipeline(device, pipeline, nullptr);

            // Destroy render passes
            for (auto& [key, renderPass] : renderPassCache)
                vkDestroyRenderPass(device, renderPass, nullptr);

            // Destroy framebuffers
            for (auto& [handle, fb] : framebuffers)
            {
                if (fb.framebuffer)
                    vkDestroyFramebuffer(device, fb.framebuffer, nullptr);
            }

            // Destroy textures
            for (auto& [handle, info] : textureInfos)
            {
                if (info.imageView)
                    vkDestroyImageView(device, info.imageView, nullptr);
                if (info.image)
                    vkDestroyImage(device, info.image, nullptr);
                if (info.memory)
                    vkFreeMemory(device, info.memory, nullptr);
            }

            // Destroy buffers
            for (auto& [handle, info] : bufferInfos)
            {
                if (info.buffer)
                    vkDestroyBuffer(device, info.buffer, nullptr);
                if (info.memory)
                    vkFreeMemory(device, info.memory, nullptr);
            }

            // Destroy window targets
            for (auto& [handle, wt] : windowTargets)
                destroyWindowTarget(wt);

            // Destroy samplers
            if (linearSampler)
                vkDestroySampler(device, linearSampler, nullptr);
            if (nearestSampler)
                vkDestroySampler(device, nearestSampler, nullptr);

            // Destroy descriptor pool
            if (descriptorPool)
                vkDestroyDescriptorPool(device, descriptorPool, nullptr);

            // Destroy pipeline layout
            if (pipelineLayout)
                vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            if (descriptorSetLayout)
                vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

            // Destroy shader modules
            if (vertexShader)
                vkDestroyShaderModule(device, vertexShader, nullptr);
            if (texturedFragShader)
                vkDestroyShaderModule(device, texturedFragShader, nullptr);
            if (colorFragShader)
                vkDestroyShaderModule(device, colorFragShader, nullptr);

            // Destroy synchronization objects
            if (frameFence)
                vkDestroyFence(device, frameFence, nullptr);

            // Destroy command pool (implicitly frees command buffers)
            if (commandPool)
                vkDestroyCommandPool(device, commandPool, nullptr);

            vkDestroyDevice(device, nullptr);
        }

        if (instance)
            vkDestroyInstance(instance, nullptr);
    }


    ////////////////////////////////////////////////////////////
    void destroyWindowTarget(WindowTarget& wt)
    {
        for (auto fb : wt.swapchainFramebuffers)
        {
            if (fb)
                vkDestroyFramebuffer(device, fb, nullptr);
        }
        for (auto iv : wt.swapchainImageViews)
        {
            if (iv)
                vkDestroyImageView(device, iv, nullptr);
        }
        if (wt.swapchain)
            vkDestroySwapchainKHR(device, wt.swapchain, nullptr);
        if (wt.renderFinishedSemaphore)
            vkDestroySemaphore(device, wt.renderFinishedSemaphore, nullptr);
        if (wt.imageAvailableSemaphore)
            vkDestroySemaphore(device, wt.imageAvailableSemaphore, nullptr);
        if (wt.surface)
            vkDestroySurfaceKHR(instance, wt.surface, nullptr);
    }


    ////////////////////////////////////////////////////////////
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
    {
        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
        {
            if ((typeFilter & (1 << i)) &&
                (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }
        return 0;
    }


    ////////////////////////////////////////////////////////////
    VkBuffer createVkBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags memProps, VkDeviceMemory& outMemory) const
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size  = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer buffer;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
            return VK_NULL_HANDLE;

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, memProps);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS)
        {
            vkDestroyBuffer(device, buffer, nullptr);
            return VK_NULL_HANDLE;
        }

        vkBindBufferMemory(device, buffer, outMemory, 0);
        return buffer;
    }


    ////////////////////////////////////////////////////////////
    void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = oldLayout;
        barrier.newLayout           = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;

        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        {
            barrier.srcAccessMask = 0;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        {
            barrier.srcAccessMask = 0;
            srcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        }

        if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        {
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        {
            barrier.dstAccessMask = 0;
            dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        }

        vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);
    }


    ////////////////////////////////////////////////////////////
    void transitionTexture(std::uint64_t handle, VkImageLayout newLayout)
    {
        auto it = textureInfos.find(handle);
        if (it == textureInfos.end())
            return;

        if (it->second.currentLayout != newLayout)
        {
            transitionImageLayout(it->second.image, it->second.currentLayout, newLayout);
            it->second.currentLayout = newLayout;
        }
    }


    ////////////////////////////////////////////////////////////
    VkRenderPass getOrCreateRenderPass(VkFormat format, VkAttachmentLoadOp loadOp)
    {
        RenderPassKey key{format, loadOp};
        auto it = renderPassCache.find(key);
        if (it != renderPassCache.end())
            return it->second;

        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = format;
        colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp         = loadOp;
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments    = &colorAttachment;
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies   = &dependency;

        VkRenderPass renderPass;
        if (vkCreateRenderPass(device, &rpInfo, nullptr, &renderPass) != VK_SUCCESS)
        {
            sf::err() << "Failed to create Vulkan render pass" << std::endl;
            return VK_NULL_HANDLE;
        }

        renderPassCache[key] = renderPass;
        return renderPass;
    }


    ////////////////////////////////////////////////////////////
    void ensureCommandBuffer()
    {
        if (commandBufferActive)
            return;

        vkWaitForFences(device, 1, &frameFence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &frameFence);
        vkResetCommandPool(device, commandPool, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        commandBufferActive = true;
    }


    ////////////////////////////////////////////////////////////
    void ensureRenderPass()
    {
        ensureCommandBuffer();

        if (renderPassActive)
            return;

        VkImage        targetImage      = VK_NULL_HANDLE;
        VkImageView    targetImageView  = VK_NULL_HANDLE;
        VkFramebuffer  targetFramebuffer = VK_NULL_HANDLE;
        VkFormat       targetFormat     = VK_FORMAT_B8G8R8A8_UNORM;
        VkExtent2D     targetExtent{};
        VkImageLayout* targetLayoutPtr  = nullptr;

        if (currentFramebuffer == 0 && activeWindowHandle)
        {
            auto winIt = windowTargets.find(activeWindowHandle);
            if (winIt == windowTargets.end())
                return;

            auto& wt = winIt->second;

            // Acquire swapchain image if not yet acquired
            if (!wt.imageAcquired)
            {
                VkResult result = vkAcquireNextImageKHR(device, wt.swapchain, UINT64_MAX,
                                                         wt.imageAvailableSemaphore, VK_NULL_HANDLE,
                                                         &wt.currentImageIndex);
                if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
                {
                    sf::err() << "Failed to acquire swapchain image" << std::endl;
                    return;
                }
                wt.imageAcquired = true;
            }

            const auto idx = wt.currentImageIndex;
            targetImage     = wt.swapchainImages[idx];
            targetImageView = wt.swapchainImageViews[idx];
            targetFormat    = wt.swapchainFormat;
            targetExtent    = wt.swapchainExtent;
            targetLayoutPtr = &wt.swapchainImageLayouts[idx];

            // Transition to COLOR_ATTACHMENT_OPTIMAL
            if (*targetLayoutPtr != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            {
                transitionImageLayout(targetImage, *targetLayoutPtr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                *targetLayoutPtr = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }

        }
        else if (currentFramebuffer != 0)
        {
            auto fbIt = framebuffers.find(currentFramebuffer);
            if (fbIt == framebuffers.end())
                return;

            auto texIt = textureInfos.find(fbIt->second.textureHandle);
            if (texIt == textureInfos.end())
                return;

            targetImage     = texIt->second.image;
            targetImageView = texIt->second.imageView;
            targetFormat    = fbIt->second.sRgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            targetExtent    = {fbIt->second.size.x, fbIt->second.size.y};

            // Transition texture to COLOR_ATTACHMENT_OPTIMAL
            transitionTexture(fbIt->second.textureHandle, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }
        else
        {
            return;
        }

        if (targetImageView == VK_NULL_HANDLE)
            return;

        currentRtFormat = targetFormat;

        // Get/create render pass for the current load op
        auto loadOp = needsClear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        VkRenderPass renderPass = getOrCreateRenderPass(targetFormat, loadOp);

        // Create framebuffer for the current target and render pass
        {
            // Destroy previous framebuffer if any
            if (currentFramebuffer == 0 && activeWindowHandle)
            {
                auto winIt = windowTargets.find(activeWindowHandle);
                if (winIt != windowTargets.end())
                {
                    auto& wt  = winIt->second;
                    auto  idx = wt.currentImageIndex;
                    if (wt.swapchainFramebuffers[idx])
                        vkDestroyFramebuffer(device, wt.swapchainFramebuffers[idx], nullptr);

                    VkFramebufferCreateInfo fbInfo{};
                    fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                    fbInfo.renderPass      = renderPass;
                    fbInfo.attachmentCount = 1;
                    fbInfo.pAttachments    = &targetImageView;
                    fbInfo.width           = targetExtent.width;
                    fbInfo.height          = targetExtent.height;
                    fbInfo.layers          = 1;

                    vkCreateFramebuffer(device, &fbInfo, nullptr, &wt.swapchainFramebuffers[idx]);
                    targetFramebuffer = wt.swapchainFramebuffers[idx];
                }
            }
            else if (currentFramebuffer != 0)
            {
                auto fbIt = framebuffers.find(currentFramebuffer);
                if (fbIt != framebuffers.end())
                {
                    if (fbIt->second.framebuffer)
                        vkDestroyFramebuffer(device, fbIt->second.framebuffer, nullptr);

                    VkFramebufferCreateInfo fbInfo{};
                    fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                    fbInfo.renderPass      = renderPass;
                    fbInfo.attachmentCount = 1;
                    fbInfo.pAttachments    = &targetImageView;
                    fbInfo.width           = targetExtent.width;
                    fbInfo.height          = targetExtent.height;
                    fbInfo.layers          = 1;

                    vkCreateFramebuffer(device, &fbInfo, nullptr, &fbIt->second.framebuffer);
                    fbIt->second.compatibleRenderPass = renderPass;
                    targetFramebuffer = fbIt->second.framebuffer;
                }
            }
        }

        if (targetFramebuffer == VK_NULL_HANDLE)
            return;

        // Begin render pass
        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass        = renderPass;
        rpBegin.framebuffer       = targetFramebuffer;
        rpBegin.renderArea.offset = {0, 0};
        rpBegin.renderArea.extent = targetExtent;

        VkClearValue clearValue{};
        if (needsClear)
        {
            clearValue.color = {{clearColor.r / 255.f, clearColor.g / 255.f,
                                  clearColor.b / 255.f, clearColor.a / 255.f}};
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues    = &clearValue;
            needsClear = false;
        }

        vkCmdBeginRenderPass(commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        renderPassActive = true;

        // Set dynamic viewport (negative height for Y-flip, VK_KHR_maintenance1 / Vulkan 1.1)
        VkViewport vp = viewport;
        vkCmdSetViewport(commandBuffer, 0, 1, &vp);

        // Set scissor
        if (scissorEnabled)
            vkCmdSetScissor(commandBuffer, 0, 1, &scissorRect);
        else
        {
            VkRect2D fullScissor{{0, 0}, targetExtent};
            vkCmdSetScissor(commandBuffer, 0, 1, &fullScissor);
        }
    }


    ////////////////////////////////////////////////////////////
    void endRenderPass()
    {
        if (!renderPassActive)
            return;

        vkCmdEndRenderPass(commandBuffer);
        renderPassActive = false;
    }


    ////////////////////////////////////////////////////////////
    void flushCommandBuffer()
    {
        if (!commandBufferActive)
            return;

        endRenderPass();
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &commandBuffer;

        // If we have a window with an acquired image, use its semaphores
        if (activeWindowHandle)
        {
            auto winIt = windowTargets.find(activeWindowHandle);
            if (winIt != windowTargets.end() && winIt->second.imageAcquired)
            {
                VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                submitInfo.waitSemaphoreCount   = 1;
                submitInfo.pWaitSemaphores      = &winIt->second.imageAvailableSemaphore;
                submitInfo.pWaitDstStageMask    = &waitStage;
                submitInfo.signalSemaphoreCount = 1;
                submitInfo.pSignalSemaphores    = &winIt->second.renderFinishedSemaphore;
            }
        }

        vkQueueSubmit(graphicsQueue, 1, &submitInfo, frameFence);
        vkWaitForFences(device, 1, &frameFence, VK_TRUE, UINT64_MAX);

        commandBufferActive = false;
        renderPassActive    = false;

        // Free transient buffers
        for (auto& tb : transientBuffers)
        {
            vkDestroyBuffer(device, tb.buffer, nullptr);
            vkFreeMemory(device, tb.memory, nullptr);
        }
        transientBuffers.clear();
    }


    ////////////////////////////////////////////////////////////
    void updateTextureDescriptorSet(TextureInfo& info)
    {
        VkSampler sampler = info.smooth ? linearSampler : nearestSampler;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView   = info.imageView;
        imageInfo.sampler     = sampler;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = info.descriptorSet;
        write.dstBinding      = 0;
        write.dstArrayElement = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }


    ////////////////////////////////////////////////////////////
    VkPipeline getOrCreatePipeline(VkFormat colorFormat, VkPrimitiveTopology topology)
    {
        PipelineKey key{currentBlendMode, currentTextured, currentColorMask, colorFormat, topology};

        auto it = pipelineCache.find(key);
        if (it != pipelineCache.end())
            return it->second;

        // Shader stages
        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertexShader;
        vertStage.pName  = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = currentTextured ? texturedFragShader : colorFragShader;
        fragStage.pName  = "main";

        VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        // Vertex input matching sf::Vertex (20 bytes: float2 pos @0, unorm4 color @8, float2 uv @12)
        VkVertexInputBindingDescription bindingDesc{};
        bindingDesc.binding   = 0;
        bindingDesc.stride    = sizeof(Vertex);
        bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attrDescs[3]{};
        // Position: float2 at offset 0
        attrDescs[0].binding  = 0;
        attrDescs[0].location = 0;
        attrDescs[0].format   = VK_FORMAT_R32G32_SFLOAT;
        attrDescs[0].offset   = 0;
        // Color: unorm4 at offset 8
        attrDescs[1].binding  = 0;
        attrDescs[1].location = 1;
        attrDescs[1].format   = VK_FORMAT_R8G8B8A8_UNORM;
        attrDescs[1].offset   = 8;
        // TexCoords: float2 at offset 12
        attrDescs[2].binding  = 0;
        attrDescs[2].location = 2;
        attrDescs[2].format   = VK_FORMAT_R32G32_SFLOAT;
        attrDescs[2].offset   = 12;

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount   = 1;
        vertexInput.pVertexBindingDescriptions      = &bindingDesc;
        vertexInput.vertexAttributeDescriptionCount = 3;
        vertexInput.pVertexAttributeDescriptions    = attrDescs;

        // Input assembly
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = topology;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // Viewport state (dynamic)
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount  = 1;

        // Rasterizer
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable        = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth               = 1.0f;
        rasterizer.cullMode                = VK_CULL_MODE_NONE;
        rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable         = VK_FALSE;

        // Multisampling (disabled)
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable  = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // Depth-stencil (disabled)
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable  = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // Color blending
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable         = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor  = toVkBlendFactor(currentBlendMode.colorSrcFactor);
        colorBlendAttachment.dstColorBlendFactor  = toVkBlendFactor(currentBlendMode.colorDstFactor);
        colorBlendAttachment.colorBlendOp         = toVkBlendOp(currentBlendMode.colorEquation);
        colorBlendAttachment.srcAlphaBlendFactor  = toVkBlendFactor(currentBlendMode.alphaSrcFactor);
        colorBlendAttachment.dstAlphaBlendFactor  = toVkBlendFactor(currentBlendMode.alphaDstFactor);
        colorBlendAttachment.alphaBlendOp         = toVkBlendOp(currentBlendMode.alphaEquation);
        colorBlendAttachment.colorWriteMask       = currentColorMask
                                                        ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT)
                                                        : 0;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable   = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments    = &colorBlendAttachment;

        // Dynamic state
        VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates    = dynamicStates;

        // Get a compatible render pass for pipeline creation
        VkRenderPass compatRp = getOrCreateRenderPass(colorFormat, VK_ATTACHMENT_LOAD_OP_LOAD);

        // Create graphics pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount          = 2;
        pipelineInfo.pStages             = stages;
        pipelineInfo.pVertexInputState   = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState      = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState   = &multisampling;
        pipelineInfo.pDepthStencilState  = &depthStencil;
        pipelineInfo.pColorBlendState    = &colorBlending;
        pipelineInfo.pDynamicState       = &dynamicState;
        pipelineInfo.layout              = pipelineLayout;
        pipelineInfo.renderPass          = compatRp;
        pipelineInfo.subpass             = 0;

        VkPipeline pipeline;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
        {
            sf::err() << "Failed to create Vulkan graphics pipeline" << std::endl;
            return VK_NULL_HANDLE;
        }

        pipelineCache[key] = pipeline;
        return pipeline;
    }
};


////////////////////////////////////////////////////////////
VulkanBackend::VulkanBackend() : m_impl(new Impl)
{
    // Create Vulkan instance
    {
        VkApplicationInfo appInfo{};
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "SFML";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName        = "SFML";
        appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion         = VK_API_VERSION_1_1;

        const char* extensions[] = {
            VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined(__linux__)
            VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#endif
        };

        VkInstanceCreateInfo createInfo{};
        createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo        = &appInfo;
        createInfo.enabledExtensionCount   = sizeof(extensions) / sizeof(extensions[0]);
        createInfo.ppEnabledExtensionNames = extensions;

        if (vkCreateInstance(&createInfo, nullptr, &m_impl->instance) != VK_SUCCESS)
        {
            err() << "Failed to create Vulkan instance" << std::endl;
            return;
        }
    }

    // Pick physical device (first discrete GPU, or first available)
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_impl->instance, &deviceCount, nullptr);
        if (deviceCount == 0)
        {
            err() << "No Vulkan-capable GPU found" << std::endl;
            return;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_impl->instance, &deviceCount, devices.data());

        // Prefer discrete GPU
        m_impl->physicalDevice = devices[0];
        for (auto dev : devices)
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                m_impl->physicalDevice = dev;
                break;
            }
        }

        vkGetPhysicalDeviceProperties(m_impl->physicalDevice, &m_impl->deviceProperties);
        vkGetPhysicalDeviceMemoryProperties(m_impl->physicalDevice, &m_impl->memoryProperties);
    }

    // Find graphics queue family
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_impl->physicalDevice, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_impl->physicalDevice, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i)
        {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                m_impl->graphicsQueueFamily = i;
                break;
            }
        }
    }

    // Create logical device
    {
        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = m_impl->graphicsQueueFamily;
        queueCreateInfo.queueCount       = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        const char* deviceExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount    = 1;
        createInfo.pQueueCreateInfos       = &queueCreateInfo;
        createInfo.enabledExtensionCount   = 1;
        createInfo.ppEnabledExtensionNames = deviceExtensions;
        createInfo.pEnabledFeatures        = &deviceFeatures;

        if (vkCreateDevice(m_impl->physicalDevice, &createInfo, nullptr, &m_impl->device) != VK_SUCCESS)
        {
            err() << "Failed to create Vulkan logical device" << std::endl;
            return;
        }

        vkGetDeviceQueue(m_impl->device, m_impl->graphicsQueueFamily, 0, &m_impl->graphicsQueue);
    }

    // Create command pool
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_impl->graphicsQueueFamily;

        if (vkCreateCommandPool(m_impl->device, &poolInfo, nullptr, &m_impl->commandPool) != VK_SUCCESS)
        {
            err() << "Failed to create Vulkan command pool" << std::endl;
            return;
        }
    }

    // Allocate command buffer
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = m_impl->commandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_impl->device, &allocInfo, &m_impl->commandBuffer) != VK_SUCCESS)
        {
            err() << "Failed to allocate Vulkan command buffer" << std::endl;
            return;
        }
    }

    // Create fence (signaled initially so first wait succeeds)
    {
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateFence(m_impl->device, &fenceInfo, nullptr, &m_impl->frameFence) != VK_SUCCESS)
        {
            err() << "Failed to create Vulkan fence" << std::endl;
            return;
        }
    }

    // Create shader modules from SPIR-V bytecode
    {
        auto createModule = [this](const uint32_t* code, std::size_t codeSize) -> VkShaderModule
        {
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = codeSize;
            createInfo.pCode    = code;

            VkShaderModule module;
            if (vkCreateShaderModule(m_impl->device, &createInfo, nullptr, &module) != VK_SUCCESS)
                return VK_NULL_HANDLE;
            return module;
        };

        m_impl->vertexShader       = createModule(defaultVertSpv, sizeof(defaultVertSpv));
        m_impl->texturedFragShader = createModule(texturedFragSpv, sizeof(texturedFragSpv));
        m_impl->colorFragShader    = createModule(colorFragSpv, sizeof(colorFragSpv));

        if (!m_impl->vertexShader || !m_impl->texturedFragShader || !m_impl->colorFragShader)
        {
            err() << "Failed to create Vulkan shader modules" << std::endl;
            return;
        }
    }

    // Create descriptor set layout (1 combined image sampler at binding 0)
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding            = 0;
        binding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount    = 1;
        binding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings    = &binding;

        if (vkCreateDescriptorSetLayout(m_impl->device, &layoutInfo, nullptr,
                                         &m_impl->descriptorSetLayout) != VK_SUCCESS)
        {
            err() << "Failed to create Vulkan descriptor set layout" << std::endl;
            return;
        }
    }

    // Create pipeline layout (push constants: 192 bytes vertex stage, 1 descriptor set)
    {
        VkPushConstantRange pushConstant{};
        pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstant.offset     = 0;
        pushConstant.size       = sizeof(Uniforms);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount          = 1;
        pipelineLayoutInfo.pSetLayouts             = &m_impl->descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount  = 1;
        pipelineLayoutInfo.pPushConstantRanges     = &pushConstant;

        if (vkCreatePipelineLayout(m_impl->device, &pipelineLayoutInfo, nullptr,
                                    &m_impl->pipelineLayout) != VK_SUCCESS)
        {
            err() << "Failed to create Vulkan pipeline layout" << std::endl;
            return;
        }
    }

    // Create descriptor pool
    {
        VkDescriptorPoolSize poolSize{};
        poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = maxDescriptorSets;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets       = maxDescriptorSets;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes    = &poolSize;

        if (vkCreateDescriptorPool(m_impl->device, &poolInfo, nullptr, &m_impl->descriptorPool) != VK_SUCCESS)
        {
            err() << "Failed to create Vulkan descriptor pool" << std::endl;
            return;
        }
    }

    // Create samplers
    {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter    = VK_FILTER_LINEAR;
        samplerInfo.minFilter    = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod       = VK_LOD_CLAMP_NONE;

        if (vkCreateSampler(m_impl->device, &samplerInfo, nullptr, &m_impl->linearSampler) != VK_SUCCESS)
        {
            err() << "Failed to create Vulkan linear sampler" << std::endl;
            return;
        }

        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;

        if (vkCreateSampler(m_impl->device, &samplerInfo, nullptr, &m_impl->nearestSampler) != VK_SUCCESS)
        {
            err() << "Failed to create Vulkan nearest sampler" << std::endl;
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
VulkanBackend::~VulkanBackend()
{
    if (m_impl->device)
        m_impl->flushCommandBuffer();
    delete m_impl;
}


////////////////////////////////////////////////////////////
// Render target operations
////////////////////////////////////////////////////////////

void VulkanBackend::clear(Color color)
{
    // End any active render pass so the next ensureRenderPass uses LOAD_OP_CLEAR
    m_impl->endRenderPass();
    m_impl->clearColor = color;
    m_impl->needsClear = true;
}


////////////////////////////////////////////////////////////
void VulkanBackend::clearStencil(StencilValue /* stencilValue */)
{
    // TODO: implement Vulkan stencil clear
}


////////////////////////////////////////////////////////////
void VulkanBackend::clear(Color color, StencilValue /* stencilValue */)
{
    clear(color);
    // TODO: also clear stencil
}


////////////////////////////////////////////////////////////
void VulkanBackend::setViewport(const IntRect& viewport, unsigned int /* targetHeight */)
{
    // Vulkan uses negative viewport height for Y-flip (VK_KHR_maintenance1 / Vulkan 1.1)
    m_impl->viewport.x        = static_cast<float>(viewport.position.x);
    m_impl->viewport.y        = static_cast<float>(viewport.position.y + viewport.size.y);
    m_impl->viewport.width    = static_cast<float>(viewport.size.x);
    m_impl->viewport.height   = -static_cast<float>(viewport.size.y);
    m_impl->viewport.minDepth = 0.0f;
    m_impl->viewport.maxDepth = 1.0f;

    if (m_impl->commandBufferActive && m_impl->renderPassActive)
        vkCmdSetViewport(m_impl->commandBuffer, 0, 1, &m_impl->viewport);
}


////////////////////////////////////////////////////////////
void VulkanBackend::setScissor(const IntRect& scissor, bool enable, unsigned int /* targetHeight */)
{
    m_impl->scissorEnabled = enable;

    if (enable)
    {
        m_impl->scissorRect.offset.x      = scissor.position.x;
        m_impl->scissorRect.offset.y      = scissor.position.y;
        m_impl->scissorRect.extent.width   = static_cast<uint32_t>(scissor.size.x);
        m_impl->scissorRect.extent.height  = static_cast<uint32_t>(scissor.size.y);
    }

    if (m_impl->commandBufferActive && m_impl->renderPassActive)
    {
        if (enable)
            vkCmdSetScissor(m_impl->commandBuffer, 0, 1, &m_impl->scissorRect);
        else
        {
            // Determine current target extent for full-viewport scissor
            VkRect2D fullScissor{{0, 0}, {16384, 16384}};
            vkCmdSetScissor(m_impl->commandBuffer, 0, 1, &fullScissor);
        }
    }
}


////////////////////////////////////////////////////////////
void VulkanBackend::setSrgb(bool /* enable */)
{
    // Vulkan handles sRGB through image formats, not a global toggle
}


////////////////////////////////////////////////////////////
// State management
////////////////////////////////////////////////////////////

void VulkanBackend::applyBlendMode(const BlendMode& mode)
{
    m_impl->currentBlendMode = mode;
    // Pipeline state will be updated at draw time
}


////////////////////////////////////////////////////////////
void VulkanBackend::applyStencilMode(const StencilMode& /* mode */)
{
    // TODO: configure depth-stencil state
}


////////////////////////////////////////////////////////////
void VulkanBackend::setColorMask(bool enable)
{
    m_impl->currentColorMask = enable;
    // Pipeline state will be updated at draw time
}


////////////////////////////////////////////////////////////
// Drawing
////////////////////////////////////////////////////////////

void VulkanBackend::setupVertexData(const Vertex* vertices, std::size_t count, bool textured)
{
    m_impl->currentTextured = textured;

    const auto byteSize = static_cast<VkDeviceSize>(count * sizeof(Vertex));

    // Create transient upload buffer
    VkDeviceMemory memory;
    VkBuffer buffer = m_impl->createVkBuffer(
        byteSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memory);
    if (buffer == VK_NULL_HANDLE)
        return;

    // Copy vertex data
    void* mapped = nullptr;
    vkMapMemory(m_impl->device, memory, 0, byteSize, 0, &mapped);
    std::memcpy(mapped, vertices, count * sizeof(Vertex));
    vkUnmapMemory(m_impl->device, memory);

    m_impl->currentVertexBuffer       = buffer;
    m_impl->currentVertexBufferOffset = 0;
    m_impl->currentVertexCount        = count;

    // Keep alive until GPU flush
    m_impl->transientBuffers.push_back({buffer, memory});
}


////////////////////////////////////////////////////////////
void VulkanBackend::setupVertexBuffer(BackendBufferHandle buffer, bool textured)
{
    m_impl->currentTextured = textured;

    auto it = m_impl->bufferInfos.find(buffer);
    if (it != m_impl->bufferInfos.end())
    {
        m_impl->currentVertexBuffer       = it->second.buffer;
        m_impl->currentVertexBufferOffset = 0;
        m_impl->currentVertexCount        = it->second.vertexCount;
    }
}


////////////////////////////////////////////////////////////
void VulkanBackend::applyTransform(const Transform& projection, const Transform& model)
{
    std::memcpy(m_impl->uniforms.projection, projection.getMatrix(), 16 * sizeof(float));
    std::memcpy(m_impl->uniforms.model, model.getMatrix(), 16 * sizeof(float));
}


////////////////////////////////////////////////////////////
void VulkanBackend::drawPrimitives(PrimitiveType type, std::size_t firstVertex, std::size_t vertexCount)
{
    m_impl->ensureRenderPass();

    if (!m_impl->renderPassActive)
        return;

    const auto topology = toVkTopology(type);
    VkPipeline pipeline = m_impl->getOrCreatePipeline(m_impl->currentRtFormat, topology);
    if (pipeline == VK_NULL_HANDLE)
        return;

    vkCmdBindPipeline(m_impl->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Bind vertex buffer
    VkDeviceSize offset = m_impl->currentVertexBufferOffset;
    vkCmdBindVertexBuffers(m_impl->commandBuffer, 0, 1, &m_impl->currentVertexBuffer, &offset);

    // Upload uniforms via push constants
    vkCmdPushConstants(m_impl->commandBuffer, m_impl->pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Uniforms), &m_impl->uniforms);

    // Bind texture descriptor if textured
    if (m_impl->currentTextured && m_impl->boundTextureHandle)
    {
        auto infoIt = m_impl->textureInfos.find(m_impl->boundTextureHandle);
        if (infoIt != m_impl->textureInfos.end() && infoIt->second.descriptorSet)
        {
            vkCmdBindDescriptorSets(m_impl->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                     m_impl->pipelineLayout, 0, 1, &infoIt->second.descriptorSet,
                                     0, nullptr);
        }
    }

    vkCmdDraw(m_impl->commandBuffer, static_cast<uint32_t>(vertexCount),
              1, static_cast<uint32_t>(firstVertex), 0);
}


////////////////////////////////////////////////////////////
// Texture operations
////////////////////////////////////////////////////////////

BackendTextureHandle VulkanBackend::createTexture(Vector2u size, bool sRgb)
{
    const VkFormat format = sRgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = format;
    imageInfo.extent.width  = size.x;
    imageInfo.extent.height = size.y;
    imageInfo.extent.depth  = 1;
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image;
    if (vkCreateImage(m_impl->device, &imageInfo, nullptr, &image) != VK_SUCCESS)
        return 0;

    // Allocate and bind memory
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_impl->device, image, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = m_impl->findMemoryType(memReqs.memoryTypeBits,
                                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceMemory memory;
    if (vkAllocateMemory(m_impl->device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
    {
        vkDestroyImage(m_impl->device, image, nullptr);
        return 0;
    }
    vkBindImageMemory(m_impl->device, image, memory, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = image;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = format;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    VkImageView imageView;
    if (vkCreateImageView(m_impl->device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
    {
        vkFreeMemory(m_impl->device, memory, nullptr);
        vkDestroyImage(m_impl->device, image, nullptr);
        return 0;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo dsAllocInfo{};
    dsAllocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAllocInfo.descriptorPool     = m_impl->descriptorPool;
    dsAllocInfo.descriptorSetCount = 1;
    dsAllocInfo.pSetLayouts        = &m_impl->descriptorSetLayout;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(m_impl->device, &dsAllocInfo, &descriptorSet) != VK_SUCCESS)
    {
        vkDestroyImageView(m_impl->device, imageView, nullptr);
        vkFreeMemory(m_impl->device, memory, nullptr);
        vkDestroyImage(m_impl->device, image, nullptr);
        return 0;
    }

    const auto handle = m_impl->nextTextureHandle++;
    auto& info         = m_impl->textureInfos[handle];
    info.image         = image;
    info.memory        = memory;
    info.imageView     = imageView;
    info.descriptorSet = descriptorSet;
    info.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.size          = size;
    info.sRgb          = sRgb;

    // Write initial descriptor set (will be updated when smooth/sampler changes)
    m_impl->updateTextureDescriptorSet(info);

    return handle;
}


////////////////////////////////////////////////////////////
void VulkanBackend::destroyTexture(BackendTextureHandle handle)
{
    auto it = m_impl->textureInfos.find(handle);
    if (it == m_impl->textureInfos.end())
        return;

    m_impl->flushCommandBuffer();

    auto& info = it->second;
    if (info.descriptorSet)
        vkFreeDescriptorSets(m_impl->device, m_impl->descriptorPool, 1, &info.descriptorSet);
    if (info.imageView)
        vkDestroyImageView(m_impl->device, info.imageView, nullptr);
    if (info.image)
        vkDestroyImage(m_impl->device, info.image, nullptr);
    if (info.memory)
        vkFreeMemory(m_impl->device, info.memory, nullptr);

    m_impl->textureInfos.erase(it);
}


////////////////////////////////////////////////////////////
void VulkanBackend::updateTexture(BackendTextureHandle handle,
                                  const std::uint8_t*  pixels,
                                  Vector2u             size,
                                  Vector2u             dest)
{
    auto it = m_impl->textureInfos.find(handle);
    if (it == m_impl->textureInfos.end() || !pixels)
        return;

    m_impl->ensureCommandBuffer();

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(size.x) * size.y * 4;

    // Create staging buffer
    VkDeviceMemory stagingMemory;
    VkBuffer stagingBuffer = m_impl->createVkBuffer(
        imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingMemory);
    if (stagingBuffer == VK_NULL_HANDLE)
        return;

    // Copy pixel data to staging
    void* mapped = nullptr;
    vkMapMemory(m_impl->device, stagingMemory, 0, imageSize, 0, &mapped);
    std::memcpy(mapped, pixels, static_cast<std::size_t>(imageSize));
    vkUnmapMemory(m_impl->device, stagingMemory);

    // Transition image to TRANSFER_DST
    m_impl->transitionTexture(handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset = {static_cast<int32_t>(dest.x), static_cast<int32_t>(dest.y), 0};
    region.imageExtent = {size.x, size.y, 1};

    vkCmdCopyBufferToImage(m_impl->commandBuffer, stagingBuffer, it->second.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Keep staging buffer alive until flush
    m_impl->transientBuffers.push_back({stagingBuffer, stagingMemory});
}


////////////////////////////////////////////////////////////
void VulkanBackend::updateTextureFromTexture(BackendTextureHandle handle,
                                             BackendTextureHandle srcHandle,
                                             Vector2u             srcSize,
                                             Vector2u             dest)
{
    auto dstIt = m_impl->textureInfos.find(handle);
    auto srcIt = m_impl->textureInfos.find(srcHandle);
    if (dstIt == m_impl->textureInfos.end() || srcIt == m_impl->textureInfos.end())
        return;

    m_impl->flushCommandBuffer();
    m_impl->ensureCommandBuffer();

    m_impl->transitionTexture(srcHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    m_impl->transitionTexture(handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkImageCopy region{};
    region.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount     = 1;
    region.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount     = 1;
    region.srcOffset                     = {0, 0, 0};
    region.dstOffset                     = {static_cast<int32_t>(dest.x), static_cast<int32_t>(dest.y), 0};
    region.extent                        = {srcSize.x, srcSize.y, 1};

    vkCmdCopyImage(m_impl->commandBuffer,
                   srcIt->second.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dstIt->second.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &region);

    m_impl->flushCommandBuffer();
}


////////////////////////////////////////////////////////////
void VulkanBackend::updateTextureFromFramebuffer(BackendTextureHandle handle, Vector2u size, Vector2u dest)
{
    auto fbIt = m_impl->framebuffers.find(m_impl->currentFramebuffer);
    if (fbIt == m_impl->framebuffers.end())
        return;

    updateTextureFromTexture(handle, fbIt->second.textureHandle, size, dest);
}


////////////////////////////////////////////////////////////
void VulkanBackend::bindTexture(BackendTextureHandle handle, CoordinateType coordinateType)
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

            // Transition texture for shader reading if needed
            if (info.currentLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                info.currentLayout != VK_IMAGE_LAYOUT_UNDEFINED)
            {
                if (m_impl->commandBufferActive)
                {
                    m_impl->endRenderPass();
                    m_impl->transitionTexture(handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
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
Image VulkanBackend::readbackTexture(BackendTextureHandle handle, Vector2u size)
{
    auto it = m_impl->textureInfos.find(handle);
    if (it == m_impl->textureInfos.end())
        return {};

    m_impl->flushCommandBuffer();
    m_impl->ensureCommandBuffer();

    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(size.x) * size.y * 4;

    // Create readback buffer
    VkDeviceMemory readbackMemory;
    VkBuffer readbackBuffer = m_impl->createVkBuffer(
        bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, readbackMemory);
    if (readbackBuffer == VK_NULL_HANDLE)
        return {};

    m_impl->transitionTexture(handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy region{};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {size.x, size.y, 1};

    vkCmdCopyImageToBuffer(m_impl->commandBuffer, it->second.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuffer, 1, &region);

    m_impl->flushCommandBuffer();

    // Map and read pixels
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size.x) * size.y * 4);
    void* mapped = nullptr;
    vkMapMemory(m_impl->device, readbackMemory, 0, bufferSize, 0, &mapped);
    std::memcpy(pixels.data(), mapped, pixels.size());
    vkUnmapMemory(m_impl->device, readbackMemory);

    // Cleanup readback buffer
    vkDestroyBuffer(m_impl->device, readbackBuffer, nullptr);
    vkFreeMemory(m_impl->device, readbackMemory, nullptr);

    return Image(size, pixels.data());
}


////////////////////////////////////////////////////////////
void VulkanBackend::setTextureSmooth(BackendTextureHandle handle, bool smooth, bool /* hasMipmap */)
{
    auto it = m_impl->textureInfos.find(handle);
    if (it != m_impl->textureInfos.end())
    {
        it->second.smooth = smooth;
        m_impl->updateTextureDescriptorSet(it->second);
    }
}


////////////////////////////////////////////////////////////
void VulkanBackend::setTextureRepeated(BackendTextureHandle handle, bool repeated)
{
    auto it = m_impl->textureInfos.find(handle);
    if (it != m_impl->textureInfos.end())
        it->second.repeated = repeated;
    // TODO: update sampler address mode (requires per-texture sampler or immutable sampler array)
}


////////////////////////////////////////////////////////////
bool VulkanBackend::generateMipmap(BackendTextureHandle /* handle */, Vector2u /* size */, bool /* smooth */)
{
    // TODO: implement mipmap generation (requires blit chain or compute shader)
    return false;
}


////////////////////////////////////////////////////////////
unsigned int VulkanBackend::getMaxTextureSize() const
{
    return m_impl->deviceProperties.limits.maxImageDimension2D;
}


////////////////////////////////////////////////////////////
void VulkanBackend::setTextureFlipped(BackendTextureHandle handle, bool flipped)
{
    auto it = m_impl->textureInfos.find(handle);
    if (it != m_impl->textureInfos.end())
        it->second.flipped = flipped;
}


////////////////////////////////////////////////////////////
Vector2u VulkanBackend::getTextureActualSize(BackendTextureHandle handle) const
{
    auto it = m_impl->textureInfos.find(handle);
    if (it == m_impl->textureInfos.end())
        return {};

    // Vulkan always uses exact sizes (no power-of-two padding)
    return it->second.size;
}


////////////////////////////////////////////////////////////
// Shader operations (user shaders -- not yet implemented)
////////////////////////////////////////////////////////////

BackendShaderHandle VulkanBackend::compileShader(std::string_view /* vertexShaderCode */,
                                                 std::string_view /* geometryShaderCode */,
                                                 std::string_view /* fragmentShaderCode */)
{
    // TODO: compile GLSL->SPIR-V (requires shaderc or pre-compiled SPIR-V)
    err() << "Vulkan shader compilation not yet implemented" << std::endl;
    return 0;
}


////////////////////////////////////////////////////////////
void VulkanBackend::destroyShader(BackendShaderHandle /* handle */) {}
void VulkanBackend::bindShader(BackendShaderHandle /* handle */) {}
int  VulkanBackend::getUniformLocation(BackendShaderHandle /* handle */, const std::string& /* name */) { return -1; }

void VulkanBackend::setUniform(BackendShaderHandle, int, float) {}
void VulkanBackend::setUniform(BackendShaderHandle, int, const Glsl::Vec2&) {}
void VulkanBackend::setUniform(BackendShaderHandle, int, const Glsl::Vec3&) {}
void VulkanBackend::setUniform(BackendShaderHandle, int, const Glsl::Vec4&) {}
void VulkanBackend::setUniform(BackendShaderHandle, int, int) {}
void VulkanBackend::setUniform(BackendShaderHandle, int, const Glsl::Ivec2&) {}
void VulkanBackend::setUniform(BackendShaderHandle, int, const Glsl::Ivec3&) {}
void VulkanBackend::setUniform(BackendShaderHandle, int, const Glsl::Ivec4&) {}
void VulkanBackend::setUniform(BackendShaderHandle, int, bool) {}
void VulkanBackend::setUniform(BackendShaderHandle, int, const Glsl::Bvec2&) {}
void VulkanBackend::setUniform(BackendShaderHandle, int, const Glsl::Bvec3&) {}
void VulkanBackend::setUniform(BackendShaderHandle, int, const Glsl::Bvec4&) {}
void VulkanBackend::setUniform(BackendShaderHandle, int, const Glsl::Mat3&) {}
void VulkanBackend::setUniform(BackendShaderHandle, int, const Glsl::Mat4&) {}

void VulkanBackend::setUniformTexture(BackendShaderHandle, int, BackendTextureHandle, int) {}

void VulkanBackend::setUniformArray(BackendShaderHandle, int, const float*, std::size_t) {}
void VulkanBackend::setUniformArray(BackendShaderHandle, int, const Glsl::Vec2*, std::size_t) {}
void VulkanBackend::setUniformArray(BackendShaderHandle, int, const Glsl::Vec3*, std::size_t) {}
void VulkanBackend::setUniformArray(BackendShaderHandle, int, const Glsl::Vec4*, std::size_t) {}
void VulkanBackend::setUniformArray(BackendShaderHandle, int, const Glsl::Mat3*, std::size_t) {}
void VulkanBackend::setUniformArray(BackendShaderHandle, int, const Glsl::Mat4*, std::size_t) {}


////////////////////////////////////////////////////////////
// Vertex buffer operations
////////////////////////////////////////////////////////////

BackendBufferHandle VulkanBackend::createBuffer(std::size_t vertexCount, VertexBuffer::Usage /* usage */)
{
    const auto byteSize = static_cast<VkDeviceSize>(vertexCount * sizeof(Vertex));

    VkDeviceMemory memory;
    VkBuffer buffer = m_impl->createVkBuffer(
        byteSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memory);
    if (buffer == VK_NULL_HANDLE)
        return 0;

    const auto handle       = m_impl->nextBufferHandle++;
    auto& info              = m_impl->bufferInfos[handle];
    info.buffer             = buffer;
    info.memory             = memory;
    info.vertexCount        = vertexCount;
    return handle;
}


////////////////////////////////////////////////////////////
void VulkanBackend::destroyBuffer(BackendBufferHandle handle)
{
    auto it = m_impl->bufferInfos.find(handle);
    if (it == m_impl->bufferInfos.end())
        return;

    m_impl->flushCommandBuffer();

    vkDestroyBuffer(m_impl->device, it->second.buffer, nullptr);
    vkFreeMemory(m_impl->device, it->second.memory, nullptr);
    m_impl->bufferInfos.erase(it);
}


////////////////////////////////////////////////////////////
bool VulkanBackend::updateBuffer(BackendBufferHandle handle,
                                 const Vertex*       vertices,
                                 std::size_t         count,
                                 unsigned int        offset)
{
    auto it = m_impl->bufferInfos.find(handle);
    if (it == m_impl->bufferInfos.end() || !vertices)
        return false;

    const auto byteOffset = static_cast<VkDeviceSize>(offset) * sizeof(Vertex);
    const auto byteSize   = static_cast<VkDeviceSize>(count) * sizeof(Vertex);

    void* mapped = nullptr;
    vkMapMemory(m_impl->device, it->second.memory, byteOffset, byteSize, 0, &mapped);
    std::memcpy(mapped, vertices, count * sizeof(Vertex));
    vkUnmapMemory(m_impl->device, it->second.memory);

    return true;
}


////////////////////////////////////////////////////////////
bool VulkanBackend::copyBuffer(BackendBufferHandle destHandle,
                               BackendBufferHandle srcHandle,
                               std::size_t         srcSize)
{
    auto dstIt = m_impl->bufferInfos.find(destHandle);
    auto srcIt = m_impl->bufferInfos.find(srcHandle);
    if (dstIt == m_impl->bufferInfos.end() || srcIt == m_impl->bufferInfos.end())
        return false;

    // Both buffers are host-visible -- do CPU-side copy via map
    const auto byteSize = static_cast<VkDeviceSize>(srcSize) * sizeof(Vertex);

    void* srcMapped = nullptr;
    void* dstMapped = nullptr;
    vkMapMemory(m_impl->device, srcIt->second.memory, 0, byteSize, 0, &srcMapped);
    vkMapMemory(m_impl->device, dstIt->second.memory, 0, byteSize, 0, &dstMapped);

    std::memcpy(dstMapped, srcMapped, static_cast<std::size_t>(byteSize));

    vkUnmapMemory(m_impl->device, srcIt->second.memory);
    vkUnmapMemory(m_impl->device, dstIt->second.memory);

    return true;
}


////////////////////////////////////////////////////////////
// Framebuffer operations
////////////////////////////////////////////////////////////

BackendFramebufferHandle VulkanBackend::createFramebuffer(Vector2u /* size */,
                                                          BackendTextureHandle   texture,
                                                          const ContextSettings& /* settings */)
{
    const auto handle = m_impl->nextFramebufferHandle++;

    auto texIt = m_impl->textureInfos.find(texture);
    if (texIt == m_impl->textureInfos.end())
        return 0;

    const bool sRgb = texIt->second.sRgb;

    auto& fb           = m_impl->framebuffers[handle];
    fb.textureHandle   = texture;
    fb.sRgb            = sRgb;
    fb.size            = texIt->second.size;
    fb.framebuffer     = VK_NULL_HANDLE; // Created lazily in ensureRenderPass
    fb.compatibleRenderPass = VK_NULL_HANDLE;

    return handle;
}


////////////////////////////////////////////////////////////
void VulkanBackend::destroyFramebuffer(BackendFramebufferHandle handle)
{
    auto it = m_impl->framebuffers.find(handle);
    if (it == m_impl->framebuffers.end())
        return;

    m_impl->flushCommandBuffer();

    if (it->second.framebuffer)
        vkDestroyFramebuffer(m_impl->device, it->second.framebuffer, nullptr);

    m_impl->framebuffers.erase(it);
}


////////////////////////////////////////////////////////////
bool VulkanBackend::bindFramebuffer(BackendFramebufferHandle handle)
{
    if (handle != m_impl->currentFramebuffer)
    {
        m_impl->endRenderPass();
        m_impl->currentFramebuffer = handle;
    }
    return true;
}


////////////////////////////////////////////////////////////
bool VulkanBackend::isFramebufferSrgb(BackendFramebufferHandle handle) const
{
    auto it = m_impl->framebuffers.find(handle);
    if (it == m_impl->framebuffers.end())
        return false;

    return it->second.sRgb;
}


////////////////////////////////////////////////////////////
void VulkanBackend::updateFramebufferTexture(BackendFramebufferHandle handle, BackendTextureHandle texture)
{
    auto it = m_impl->framebuffers.find(handle);
    if (it == m_impl->framebuffers.end())
        return;

    if (handle == m_impl->currentFramebuffer)
        m_impl->flushCommandBuffer();

    // Destroy old framebuffer
    if (it->second.framebuffer)
    {
        vkDestroyFramebuffer(m_impl->device, it->second.framebuffer, nullptr);
        it->second.framebuffer = VK_NULL_HANDLE;
    }

    it->second.textureHandle = texture;

    auto texIt = m_impl->textureInfos.find(texture);
    if (texIt != m_impl->textureInfos.end())
    {
        it->second.sRgb = texIt->second.sRgb;
        it->second.size = texIt->second.size;
    }
}


////////////////////////////////////////////////////////////
// Capability queries
////////////////////////////////////////////////////////////

bool VulkanBackend::isShaderAvailable() const
{
    return true;
}


////////////////////////////////////////////////////////////
bool VulkanBackend::isGeometryShaderAvailable() const
{
    // Geometry shaders require VkPhysicalDeviceFeatures::geometryShader
    // For now return false since we don't request it
    return false;
}


////////////////////////////////////////////////////////////
bool VulkanBackend::isVertexBufferAvailable() const
{
    return true;
}


////////////////////////////////////////////////////////////
bool VulkanBackend::isNonPowerOfTwoTextureSupported() const
{
    return true;
}


////////////////////////////////////////////////////////////
std::size_t VulkanBackend::getMaxTextureUnits() const
{
    return 32;
}


////////////////////////////////////////////////////////////
// Pipeline operations
////////////////////////////////////////////////////////////

void VulkanBackend::flushPipeline()
{
    m_impl->flushCommandBuffer();
}


////////////////////////////////////////////////////////////
void VulkanBackend::pushRenderStates()
{
    // No-op for Vulkan (state is not global like OpenGL)
}


////////////////////////////////////////////////////////////
void VulkanBackend::popRenderStates()
{
    // No-op for Vulkan (state is not global like OpenGL)
}


////////////////////////////////////////////////////////////
void VulkanBackend::bindBuffer(BackendBufferHandle buffer)
{
    auto it = m_impl->bufferInfos.find(buffer);
    if (it != m_impl->bufferInfos.end())
    {
        m_impl->currentVertexBuffer       = it->second.buffer;
        m_impl->currentVertexBufferOffset = 0;
        m_impl->currentVertexCount        = it->second.vertexCount;
    }
    else
    {
        m_impl->currentVertexBuffer = VK_NULL_HANDLE;
        m_impl->currentVertexCount  = 0;
    }
}


////////////////////////////////////////////////////////////
unsigned int VulkanBackend::getDefaultFramebufferBinding() const
{
    return 0;
}


////////////////////////////////////////////////////////////
bool VulkanBackend::isFramebufferAvailable() const
{
    return true;
}


////////////////////////////////////////////////////////////
unsigned int VulkanBackend::getMaxAntiAliasingLevel() const
{
    VkSampleCountFlags counts = m_impl->deviceProperties.limits.framebufferColorSampleCounts;

    if (counts & VK_SAMPLE_COUNT_8_BIT)
        return 8;
    if (counts & VK_SAMPLE_COUNT_4_BIT)
        return 4;
    if (counts & VK_SAMPLE_COUNT_2_BIT)
        return 2;

    return 0;
}


////////////////////////////////////////////////////////////
bool VulkanBackend::isSrgbTextureAvailable() const
{
    return true;
}


////////////////////////////////////////////////////////////
void VulkanBackend::resetStates()
{
    m_impl->currentBlendMode    = BlendMode{};
    m_impl->currentTextured     = false;
    m_impl->currentColorMask    = true;
    m_impl->boundTextureHandle  = 0;

    static constexpr float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(m_impl->uniforms.projection, identity, sizeof(identity));
    std::memcpy(m_impl->uniforms.model, identity, sizeof(identity));
    std::memcpy(m_impl->uniforms.textureMatrix, identity, sizeof(identity));
}


////////////////////////////////////////////////////////////
bool VulkanBackend::copyBufferFallback(BackendBufferHandle destHandle,
                                       BackendBufferHandle srcHandle,
                                       std::size_t         srcSize)
{
    return copyBuffer(destHandle, srcHandle, srcSize);
}


////////////////////////////////////////////////////////////
void VulkanBackend::prepareUniformUpdate(BackendShaderHandle /* handle */) {}
void VulkanBackend::finalizeUniformUpdate() {}


////////////////////////////////////////////////////////////
// Window rendering lifecycle
////////////////////////////////////////////////////////////

void VulkanBackend::initializeWindowRendering(void* nativeHandle, Vector2u size, const ContextSettings& /* settings */)
{
    Impl::WindowTarget wt;

    // Create platform-specific surface
#ifdef _WIN32
    {
        VkWin32SurfaceCreateInfoKHR surfaceInfo{};
        surfaceInfo.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.hinstance = GetModuleHandle(nullptr);
        surfaceInfo.hwnd      = static_cast<HWND>(nativeHandle);

        if (vkCreateWin32SurfaceKHR(m_impl->instance, &surfaceInfo, nullptr, &wt.surface) != VK_SUCCESS)
        {
            err() << "Failed to create Vulkan Win32 surface" << std::endl;
            return;
        }
    }
#elif defined(__linux__)
    {
        // Note: nativeHandle must be cast appropriately; this assumes X11 Window handle
        VkXlibSurfaceCreateInfoKHR surfaceInfo{};
        surfaceInfo.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.dpy    = XOpenDisplay(nullptr); // TODO: pass Display* from window impl
        surfaceInfo.window = reinterpret_cast<::Window>(nativeHandle);

        if (vkCreateXlibSurfaceKHR(m_impl->instance, &surfaceInfo, nullptr, &wt.surface) != VK_SUCCESS)
        {
            err() << "Failed to create Vulkan Xlib surface" << std::endl;
            return;
        }
    }
#endif

    // Verify presentation support
    VkBool32 presentSupport = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(m_impl->physicalDevice, m_impl->graphicsQueueFamily,
                                          wt.surface, &presentSupport);
    if (!presentSupport)
    {
        err() << "Vulkan graphics queue does not support presentation to this surface" << std::endl;
        vkDestroySurfaceKHR(m_impl->instance, wt.surface, nullptr);
        return;
    }

    // Query surface capabilities
    VkSurfaceCapabilitiesKHR surfaceCaps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_impl->physicalDevice, wt.surface, &surfaceCaps);

    // Choose surface format (prefer B8G8R8A8_UNORM)
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_impl->physicalDevice, wt.surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_impl->physicalDevice, wt.surface, &formatCount, surfaceFormats.data());

    wt.swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    for (const auto& fmt : surfaceFormats)
    {
        if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            wt.swapchainFormat = fmt.format;
            break;
        }
    }

    // Choose extent
    if (surfaceCaps.currentExtent.width != UINT32_MAX)
    {
        wt.swapchainExtent = surfaceCaps.currentExtent;
    }
    else
    {
        wt.swapchainExtent.width  = std::max(surfaceCaps.minImageExtent.width,
                                              std::min(surfaceCaps.maxImageExtent.width, size.x));
        wt.swapchainExtent.height = std::max(surfaceCaps.minImageExtent.height,
                                              std::min(surfaceCaps.maxImageExtent.height, size.y));
    }

    // Choose image count
    uint32_t imageCount = surfaceCaps.minImageCount + 1;
    if (surfaceCaps.maxImageCount > 0 && imageCount > surfaceCaps.maxImageCount)
        imageCount = surfaceCaps.maxImageCount;

    // Choose present mode (FIFO is guaranteed, use for vsync)
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    // Create swapchain
    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface          = wt.surface;
    swapchainInfo.minImageCount    = imageCount;
    swapchainInfo.imageFormat      = wt.swapchainFormat;
    swapchainInfo.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainInfo.imageExtent      = wt.swapchainExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform     = surfaceCaps.currentTransform;
    swapchainInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode      = presentMode;
    swapchainInfo.clipped          = VK_TRUE;

    if (vkCreateSwapchainKHR(m_impl->device, &swapchainInfo, nullptr, &wt.swapchain) != VK_SUCCESS)
    {
        err() << "Failed to create Vulkan swapchain" << std::endl;
        vkDestroySurfaceKHR(m_impl->instance, wt.surface, nullptr);
        return;
    }

    // Get swapchain images
    uint32_t swapImageCount;
    vkGetSwapchainImagesKHR(m_impl->device, wt.swapchain, &swapImageCount, nullptr);
    wt.swapchainImages.resize(swapImageCount);
    vkGetSwapchainImagesKHR(m_impl->device, wt.swapchain, &swapImageCount, wt.swapchainImages.data());

    // Create image views
    wt.swapchainImageViews.resize(swapImageCount);
    for (uint32_t i = 0; i < swapImageCount; ++i)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                           = wt.swapchainImages[i];
        viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                          = wt.swapchainFormat;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(m_impl->device, &viewInfo, nullptr, &wt.swapchainImageViews[i]) != VK_SUCCESS)
        {
            err() << "Failed to create swapchain image view" << std::endl;
            return;
        }
    }

    // Initialize framebuffers (will be created lazily in ensureRenderPass)
    wt.swapchainFramebuffers.resize(swapImageCount, VK_NULL_HANDLE);

    // Initialize image layouts (all start as UNDEFINED)
    wt.swapchainImageLayouts.resize(swapImageCount, VK_IMAGE_LAYOUT_UNDEFINED);

    // Create semaphores
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    if (vkCreateSemaphore(m_impl->device, &semInfo, nullptr, &wt.imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(m_impl->device, &semInfo, nullptr, &wt.renderFinishedSemaphore) != VK_SUCCESS)
    {
        err() << "Failed to create Vulkan semaphores" << std::endl;
        return;
    }

    m_impl->windowTargets[nativeHandle] = std::move(wt);
    m_impl->activeWindowHandle          = nativeHandle;
}


////////////////////////////////////////////////////////////
void VulkanBackend::destroyWindowRendering(void* nativeHandle)
{
    m_impl->flushCommandBuffer();
    vkDeviceWaitIdle(m_impl->device);

    auto it = m_impl->windowTargets.find(nativeHandle);
    if (it != m_impl->windowTargets.end())
    {
        m_impl->destroyWindowTarget(it->second);
        m_impl->windowTargets.erase(it);
    }

    if (m_impl->activeWindowHandle == nativeHandle)
        m_impl->activeWindowHandle = nullptr;
}


////////////////////////////////////////////////////////////
void VulkanBackend::presentWindow(void* nativeHandle)
{
    auto winIt = m_impl->windowTargets.find(nativeHandle);
    if (winIt == m_impl->windowTargets.end())
        return;

    auto& wt = winIt->second;

    // If a clear is pending but no draws happened, apply it now
    if (m_impl->needsClear)
        m_impl->ensureRenderPass();

    // End render pass before present
    m_impl->endRenderPass();
    m_impl->ensureCommandBuffer();

    // Transition swapchain image to PRESENT_SRC
    if (wt.imageAcquired)
    {
        const auto idx = wt.currentImageIndex;
        if (wt.swapchainImageLayouts[idx] != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        {
            m_impl->transitionImageLayout(wt.swapchainImages[idx],
                                           wt.swapchainImageLayouts[idx],
                                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
            wt.swapchainImageLayouts[idx] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }
    }

    m_impl->flushCommandBuffer();

    // Present
    if (wt.imageAcquired)
    {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores    = &wt.renderFinishedSemaphore;
        presentInfo.swapchainCount     = 1;
        presentInfo.pSwapchains        = &wt.swapchain;
        presentInfo.pImageIndices      = &wt.currentImageIndex;

        vkQueuePresentKHR(m_impl->graphicsQueue, &presentInfo);

        wt.imageAcquired = false;
    }
}


////////////////////////////////////////////////////////////
void VulkanBackend::setWindowVerticalSyncEnabled(void* nativeHandle, bool enabled)
{
    auto winIt = m_impl->windowTargets.find(nativeHandle);
    if (winIt != m_impl->windowTargets.end())
        winIt->second.vsync = enabled;
    // TODO: recreate swapchain with different present mode (FIFO vs IMMEDIATE/MAILBOX)
}


////////////////////////////////////////////////////////////
bool VulkanBackend::setWindowActive(void* nativeHandle, bool active)
{
    if (active)
        m_impl->activeWindowHandle = nativeHandle;
    else if (m_impl->activeWindowHandle == nativeHandle)
        m_impl->activeWindowHandle = nullptr;

    return true;
}

} // namespace sf::priv

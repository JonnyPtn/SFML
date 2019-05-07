////////////////////////////////////////////////////////////
//
// SFML - Simple and Fast Multimedia Library
// Copyright (C) 2007-2022 Laurent Gomila (laurent@sfml-dev.org)
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
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/OpenGL/GL1/RenderTargetImplDefault.hpp>
#include <SFML/Graphics/Renderer.hpp>
#include <SFML/Graphics/VertexBuffer.hpp>

// Map to help us detect whether a different RenderTarget
// has been activated within a single context
using ContextRenderTargetMap = std::unordered_map<std::uint64_t, std::uint64_t>;
ContextRenderTargetMap contextRenderTargetMap;

// Check if a RenderTarget with the given ID is active in the current context
bool isActive(std::uint64_t id)
{
    auto it = contextRenderTargetMap.find(sf::Context::getActiveContextId());

    if ((it == contextRenderTargetMap.end()) || (it->second != id))
        return false;

    return true;
}

// Convert an sf::BlendMode::Factor constant to the corresponding OpenGL constant.
std::uint32_t factorToGlConstant(sf::BlendMode::Factor blendFactor)
{
    // clang-format off
    switch (blendFactor)
    {
        case sf::BlendMode::Zero:             return GL_ZERO;
        case sf::BlendMode::One:              return GL_ONE;
        case sf::BlendMode::SrcColor:         return GL_SRC_COLOR;
        case sf::BlendMode::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
        case sf::BlendMode::DstColor:         return GL_DST_COLOR;
        case sf::BlendMode::OneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
        case sf::BlendMode::SrcAlpha:         return GL_SRC_ALPHA;
        case sf::BlendMode::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        case sf::BlendMode::DstAlpha:         return GL_DST_ALPHA;
        case sf::BlendMode::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
    }
    // clang-format on

    sf::err() << "Invalid value for sf::BlendMode::Factor! Fallback to sf::BlendMode::Zero." << std::endl;
    assert(false);
    return GL_ZERO;
}


// Convert an sf::BlendMode::BlendEquation constant to the corresponding OpenGL constant.
std::uint32_t equationToGlConstant(sf::BlendMode::Equation blendEquation)
{
    switch (blendEquation)
    {
        case sf::BlendMode::Add:
            return GLEXT_GL_FUNC_ADD;
        case sf::BlendMode::Subtract:
            if (GLEXT_blend_subtract)
                return GLEXT_GL_FUNC_SUBTRACT;
            break;
        case sf::BlendMode::ReverseSubtract:
            if (GLEXT_blend_subtract)
                return GLEXT_GL_FUNC_REVERSE_SUBTRACT;
            break;
        case sf::BlendMode::Min:
            if (GLEXT_blend_minmax)
                return GLEXT_GL_MIN;
            break;
        case sf::BlendMode::Max:
            if (GLEXT_blend_minmax)
                return GLEXT_GL_MAX;
            break;
    }

    static bool warned = false;
    if (!warned)
    {
        sf::err() << "OpenGL extension EXT_blend_minmax or EXT_blend_subtract unavailable" << '\n'
                  << "Some blending equations will fallback to sf::BlendMode::Add" << '\n'
                  << "Ensure that hardware acceleration is enabled if available" << std::endl;

        warned = true;
    }

    return GLEXT_GL_FUNC_ADD;
}
} // namespace RenderTargetImpl
} // namespace


namespace sf
{
////////////////////////////////////////////////////////////
RenderTarget::RenderTarget() :
m_impl(NULL)
{
    if ((sf::getRenderer() == sf::Renderer::Default) || (sf::getRenderer() == sf::Renderer::OpenGL1))
        m_impl = new priv::RenderTargetImplDefault(this);
}


////////////////////////////////////////////////////////////
RenderTarget::~RenderTarget()
{
    delete m_impl;
}


////////////////////////////////////////////////////////////
void RenderTarget::clear(const Color& color)
{
    m_impl->clear(color);
}


////////////////////////////////////////////////////////////
void RenderTarget::setView(const View& view)
{
    m_impl->setView(view);
}


////////////////////////////////////////////////////////////
const View& RenderTarget::getView() const
{
    return m_impl->getView();
}


////////////////////////////////////////////////////////////
const View& RenderTarget::getDefaultView() const
{
    return m_impl->getDefaultView();
}


////////////////////////////////////////////////////////////
IntRect RenderTarget::getViewport(const View& view) const
{
    float            width    = static_cast<float>(getSize().x);
    float            height   = static_cast<float>(getSize().y);
    const FloatRect& viewport = view.getViewport();

    return IntRect({static_cast<int>(0.5f + width * viewport.left), static_cast<int>(0.5f + height * viewport.top)},
                   {static_cast<int>(0.5f + width * viewport.width), static_cast<int>(0.5f + height * viewport.height)});
}


////////////////////////////////////////////////////////////
Vector2f RenderTarget::mapPixelToCoords(const Vector2i& point) const
{
    return mapPixelToCoords(point, getView());
}


////////////////////////////////////////////////////////////
Vector2f RenderTarget::mapPixelToCoords(const Vector2i& point, const View& view) const
{
    // First, convert from viewport coordinates to homogeneous coordinates
    Vector2f  normalized;
    FloatRect viewport = FloatRect(getViewport(view));
    normalized.x       = -1.f + 2.f * (static_cast<float>(point.x) - viewport.left) / viewport.width;
    normalized.y       = 1.f - 2.f * (static_cast<float>(point.y) - viewport.top) / viewport.height;

    // Then transform by the inverse of the view matrix
    return view.getInverseTransform().transformPoint(normalized);
}


////////////////////////////////////////////////////////////
Vector2i RenderTarget::mapCoordsToPixel(const Vector2f& point) const
{
    return mapCoordsToPixel(point, getView());
}


////////////////////////////////////////////////////////////
Vector2i RenderTarget::mapCoordsToPixel(const Vector2f& point, const View& view) const
{
    // First, transform the point by the view matrix
    Vector2f normalized = view.getTransform().transformPoint(point);

    // Then convert to viewport coordinates
    Vector2i  pixel;
    FloatRect viewport = FloatRect(getViewport(view));
    pixel.x            = static_cast<int>((normalized.x + 1.f) / 2.f * viewport.width + viewport.left);
    pixel.y            = static_cast<int>((-normalized.y + 1.f) / 2.f * viewport.height + viewport.top);

    return pixel;
}


////////////////////////////////////////////////////////////
void RenderTarget::draw(const Drawable& drawable, const RenderStates& states)
{
    drawable.draw(*this, states);
}


////////////////////////////////////////////////////////////
void RenderTarget::draw(const Vertex* vertices, std::size_t vertexCount, PrimitiveType type, const RenderStates& states)
{
    m_impl->draw(vertices, vertexCount, type, states);
}


////////////////////////////////////////////////////////////
void RenderTarget::draw(const VertexBuffer& vertexBuffer, const RenderStates& states)
{
    draw(vertexBuffer, 0, vertexBuffer.getVertexCount(), states);
}


////////////////////////////////////////////////////////////
void RenderTarget::draw(const VertexBuffer& vertexBuffer, std::size_t firstVertex, std::size_t vertexCount, const RenderStates& states)
{
    m_impl->draw(vertexBuffer, firstVertex, vertexCount, states);
}


////////////////////////////////////////////////////////////
bool RenderTarget::isSrgb() const
{
    // By default sRGB encoding is not enabled for an arbitrary RenderTarget
    return false;
}


////////////////////////////////////////////////////////////
bool RenderTarget::setActive(bool active)
{
    return m_impl->setActive(active);
}


////////////////////////////////////////////////////////////
void RenderTarget::pushGLStates()
{
    m_impl->pushGLStates();
}


////////////////////////////////////////////////////////////
void RenderTarget::popGLStates()
{
    m_impl->popGLStates();
}


////////////////////////////////////////////////////////////
void RenderTarget::resetGLStates()
{
    m_impl->resetGLStates();
}


////////////////////////////////////////////////////////////
void RenderTarget::initialize()
{
    m_impl->initialize(getSize());
}

} // namespace sf

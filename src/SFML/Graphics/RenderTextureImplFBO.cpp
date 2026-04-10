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
#include <SFML/Graphics/RenderTextureImplFBO.hpp>

#include <SFML/Window/Context.hpp>
#include <SFML/Window/ContextSettings.hpp>

#include <SFML/System/Err.hpp>

#include <ostream>


namespace sf::priv
{
////////////////////////////////////////////////////////////
RenderTextureImplFBO::RenderTextureImplFBO() = default;


////////////////////////////////////////////////////////////
RenderTextureImplFBO::~RenderTextureImplFBO()
{
    if (m_framebufferHandle)
        getGraphicsBackend().destroyFramebuffer(m_framebufferHandle);
}


////////////////////////////////////////////////////////////
bool RenderTextureImplFBO::isAvailable()
{
    const BackendContextLock lock;

    return getGraphicsBackend().isFramebufferAvailable();
}


////////////////////////////////////////////////////////////
unsigned int RenderTextureImplFBO::getMaximumAntiAliasingLevel()
{
    const BackendContextLock lock;

    return getGraphicsBackend().getMaxAntiAliasingLevel();
}


////////////////////////////////////////////////////////////
void RenderTextureImplFBO::unbind()
{
    getGraphicsBackend().bindFramebuffer(0);
}


////////////////////////////////////////////////////////////
bool RenderTextureImplFBO::create(Vector2u size, std::uint64_t textureId, const ContextSettings& settings)
{
    const BackendContextLock lock;

    m_framebufferHandle = getGraphicsBackend().createFramebuffer(size, textureId, settings);

    return m_framebufferHandle != 0;
}


////////////////////////////////////////////////////////////
bool RenderTextureImplFBO::activate(bool active)
{
    // Unbind the FBO if requested
    if (!active)
        return getGraphicsBackend().bindFramebuffer(0);

    std::uint64_t contextId = Context::getActiveContextId();

    // In the odd case we have to activate and there is no active
    // context yet, we have to create one
    if (!contextId)
    {
        if (!m_context)
            m_context = std::make_unique<Context>();

        if (!m_context->setActive(true))
        {
            err() << "Failed to set context as active during render texture activation" << std::endl;
            return false;
        }

        contextId = Context::getActiveContextId();

        if (!contextId)
        {
            err() << "Impossible to activate render texture (failed to create backup context)" << std::endl;
            return false;
        }
    }

    return getGraphicsBackend().bindFramebuffer(m_framebufferHandle);
}


////////////////////////////////////////////////////////////
bool RenderTextureImplFBO::isSrgb() const
{
    return getGraphicsBackend().isFramebufferSrgb(m_framebufferHandle);
}


////////////////////////////////////////////////////////////
void RenderTextureImplFBO::updateTexture(std::uint64_t textureId)
{
    getGraphicsBackend().updateFramebufferTexture(m_framebufferHandle, textureId);
}

} // namespace sf::priv

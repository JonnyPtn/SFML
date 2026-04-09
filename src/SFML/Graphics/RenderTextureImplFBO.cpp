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
#include <SFML/Graphics/Backend/BackendFactory.hpp>
#include <SFML/Graphics/GLCheck.hpp>
#include <SFML/Graphics/GLExtensions.hpp>
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
    const TransientContextLock lock;

    // Make sure that extensions are initialized
    ensureExtensionsInit();

    return GLEXT_framebuffer_object != 0;
}


////////////////////////////////////////////////////////////
unsigned int RenderTextureImplFBO::getMaximumAntiAliasingLevel()
{
#ifdef SFML_OPENGL_ES

    return 0;

#else

    const TransientContextLock lock;
    GLint                      samples = 0;
    glCheck(glGetIntegerv(GLEXT_GL_MAX_SAMPLES, &samples));
    return static_cast<unsigned int>(samples);

#endif
}


////////////////////////////////////////////////////////////
void RenderTextureImplFBO::unbind()
{
    glCheck(GLEXT_glBindFramebuffer(GLEXT_GL_FRAMEBUFFER, 0));
}


////////////////////////////////////////////////////////////
bool RenderTextureImplFBO::create(Vector2u size, unsigned int textureId, const ContextSettings& settings)
{
    const TransientContextLock lock;

    m_framebufferHandle = getGraphicsBackend().createFramebuffer(size,
                                                                 static_cast<BackendTextureHandle>(textureId),
                                                                 settings);

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
void RenderTextureImplFBO::updateTexture(unsigned int textureId)
{
    getGraphicsBackend().updateFramebufferTexture(m_framebufferHandle,
                                                  static_cast<BackendTextureHandle>(textureId));
}

} // namespace sf::priv

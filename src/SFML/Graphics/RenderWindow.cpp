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
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderWindow.hpp>

#include <SFML/Window/VideoMode.hpp>

#if !defined(SFML_BACKEND_METAL) && !defined(SFML_BACKEND_DX12)
#include <SFML/Window/GlContext.hpp>
#endif

#include <SFML/Window/WindowImpl.hpp>

#include <SFML/System/Err.hpp>
#include <SFML/System/Sleep.hpp>


namespace sf
{

////////////////////////////////////////////////////////////
// Pimpl: backend-specific rendering context
////////////////////////////////////////////////////////////
struct RenderWindow::RenderContext
{
#if !defined(SFML_BACKEND_METAL) && !defined(SFML_BACKEND_DX12)
    std::shared_ptr<void>             sharedGlContext{priv::GlContext::getSharedContext()};
    std::unique_ptr<priv::GlContext>  glContext;
#endif
};


////////////////////////////////////////////////////////////
RenderWindow::RenderWindow() = default;


////////////////////////////////////////////////////////////
RenderWindow::~RenderWindow()
{
    close();
}


////////////////////////////////////////////////////////////
RenderWindow::RenderWindow(RenderWindow&&) noexcept = default;


////////////////////////////////////////////////////////////
RenderWindow& RenderWindow::operator=(RenderWindow&&) noexcept = default;


////////////////////////////////////////////////////////////
RenderWindow::RenderWindow(VideoMode mode, const String& title, std::uint32_t style, State state, const ContextSettings& settings)
{
    RenderWindow::create(mode, title, style, state, settings);
}


////////////////////////////////////////////////////////////
RenderWindow::RenderWindow(VideoMode mode, const String& title, State state, const ContextSettings& settings)
{
    RenderWindow::create(mode, title, Style::Default, state, settings);
}


////////////////////////////////////////////////////////////
RenderWindow::RenderWindow(WindowHandle handle, const ContextSettings& settings)
{
    RenderWindow::create(handle, settings);
}


////////////////////////////////////////////////////////////
void RenderWindow::create(VideoMode mode, const String& title, std::uint32_t style, State state, const ContextSettings& settings)
{
    // Close any existing window
    close();

    // Create the platform window
    m_impl = priv::WindowImpl::create(mode, title, style, state, settings);

    // Create the rendering context
    m_renderContext = std::make_unique<RenderContext>();

#if defined(SFML_BACKEND_METAL) || defined(SFML_BACKEND_DX12)
    auto& backend = priv::getGraphicsBackend();
    backend.initializeWindowRendering(getNativeHandle(), mode.size, settings);
    m_settings = settings;
#else
    m_renderContext->glContext = priv::GlContext::create(settings, *m_impl, mode.bitsPerPixel);
    m_renderContext->glContext->setActive(true);
    m_settings = m_renderContext->glContext->getSettings();
#endif

    // Set defaults and initialize (calls onCreate)
    setVerticalSyncEnabled(false);
    setFramerateLimit(0);
    m_clock.restart();
    WindowBase::initialize();
}


////////////////////////////////////////////////////////////
void RenderWindow::create(VideoMode mode, const String& title, std::uint32_t style, State state)
{
    create(mode, title, style, state, ContextSettings{});
}


////////////////////////////////////////////////////////////
void RenderWindow::create(VideoMode mode, const String& title, State state)
{
    create(mode, title, Style::Default, state, ContextSettings{});
}


////////////////////////////////////////////////////////////
void RenderWindow::create(WindowHandle handle)
{
    create(handle, ContextSettings{});
}


////////////////////////////////////////////////////////////
void RenderWindow::create(WindowHandle handle, const ContextSettings& settings)
{
    close();

    m_impl = priv::WindowImpl::create(handle);

    m_renderContext = std::make_unique<RenderContext>();

#if defined(SFML_BACKEND_METAL) || defined(SFML_BACKEND_DX12)
    auto& backend = priv::getGraphicsBackend();
    backend.initializeWindowRendering(getNativeHandle(), m_impl->getSize(), settings);
    m_settings = settings;
#else
    m_renderContext->glContext = priv::GlContext::create(settings, *m_impl, VideoMode::getDesktopMode().bitsPerPixel);
    m_renderContext->glContext->setActive(true);
    m_settings = m_renderContext->glContext->getSettings();
#endif

    setVerticalSyncEnabled(false);
    setFramerateLimit(0);
    m_clock.restart();
    WindowBase::initialize();
}


////////////////////////////////////////////////////////////
void RenderWindow::close()
{
#if defined(SFML_BACKEND_METAL) || defined(SFML_BACKEND_DX12)
    if (m_impl)
    {
        auto& backend = priv::getGraphicsBackend();
        backend.destroyWindowRendering(getNativeHandle());
    }
#endif

    m_renderContext.reset();
    WindowBase::close();
}


////////////////////////////////////////////////////////////
const ContextSettings& RenderWindow::getSettings() const
{
    return m_settings;
}


////////////////////////////////////////////////////////////
void RenderWindow::setVerticalSyncEnabled(bool enabled)
{
#if defined(SFML_BACKEND_METAL) || defined(SFML_BACKEND_DX12)
    if (m_impl)
    {
        auto& backend = priv::getGraphicsBackend();
        backend.setWindowVerticalSyncEnabled(getNativeHandle(), enabled);
    }
#else
    if (m_renderContext && m_renderContext->glContext && m_renderContext->glContext->setActive(true))
        m_renderContext->glContext->setVerticalSyncEnabled(enabled);
#endif
}


////////////////////////////////////////////////////////////
void RenderWindow::setFramerateLimit(unsigned int limit)
{
    if (limit > 0)
        m_frameTimeLimit = seconds(1.f / static_cast<float>(limit));
    else
        m_frameTimeLimit = Time::Zero;
}


////////////////////////////////////////////////////////////
bool RenderWindow::setActive(bool active)
{
    bool result = true;

#if defined(SFML_BACKEND_METAL) || defined(SFML_BACKEND_DX12)
    if (m_impl)
    {
        auto& backend = priv::getGraphicsBackend();
        result = backend.setWindowActive(getNativeHandle(), active);
    }
#else
    if (m_renderContext && m_renderContext->glContext)
    {
        result = m_renderContext->glContext->setActive(active);
        if (!result)
        {
            err() << "Failed to activate the window's context" << std::endl;
            return false;
        }
    }
#endif

    // Update RenderTarget tracking
    if (result)
        result = RenderTarget::setActive(active);

    // Bind the default framebuffer
    auto& backend = priv::getGraphicsBackend();
    if (active && result && backend.isFramebufferAvailable())
    {
        backend.bindFramebuffer(static_cast<priv::BackendFramebufferHandle>(m_defaultFrameBuffer));
        return true;
    }

    return result;
}


////////////////////////////////////////////////////////////
void RenderWindow::display()
{
#if defined(SFML_BACKEND_METAL) || defined(SFML_BACKEND_DX12)
    if (m_impl)
    {
        auto& backend = priv::getGraphicsBackend();
        backend.presentWindow(getNativeHandle());
    }
#else
    if (m_renderContext && m_renderContext->glContext)
    {
        if (m_renderContext->glContext->setActive(true))
            m_renderContext->glContext->display();
    }
#endif

    // Limit the framerate if needed
    if (m_frameTimeLimit != Time::Zero)
    {
        sleep(m_frameTimeLimit - m_clock.getElapsedTime());
        m_clock.restart();
    }
}


////////////////////////////////////////////////////////////
Vector2u RenderWindow::getSize() const
{
    return WindowBase::getSize();
}


////////////////////////////////////////////////////////////
void RenderWindow::setIcon(const Image& icon)
{
    setIcon(icon.getSize(), icon.getPixelsPtr());
}


////////////////////////////////////////////////////////////
bool RenderWindow::isSrgb() const
{
    return m_settings.sRgbCapable;
}


////////////////////////////////////////////////////////////
void RenderWindow::onCreate()
{
    auto& backend = priv::getGraphicsBackend();
    if (backend.isFramebufferAvailable())
    {
        m_defaultFrameBuffer = backend.getDefaultFramebufferBinding();
    }

    // Just initialize the render target part
    RenderTarget::initialize();
}


////////////////////////////////////////////////////////////
void RenderWindow::onResize()
{
    // Update the current view (recompute the viewport, which is stored in relative coordinates)
    setView(getView());
}

} // namespace sf

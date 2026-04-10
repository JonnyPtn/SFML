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
#include <SFML/Graphics/Export.hpp>

#include <SFML/Graphics/RenderTarget.hpp>

#include <SFML/Window/ContextSettings.hpp>
#include <SFML/Window/VideoMode.hpp>
#include <SFML/Window/WindowBase.hpp>
#include <SFML/Window/WindowEnums.hpp>
#include <SFML/Window/WindowHandle.hpp>

#include <SFML/System/Clock.hpp>
#include <SFML/System/Time.hpp>
#include <SFML/System/Vector2.hpp>

#include <memory>

#include <cstdint>


namespace sf
{
class Image;
class String;

////////////////////////////////////////////////////////////
/// \brief Window that can serve as a target for 2D drawing
///
////////////////////////////////////////////////////////////
class SFML_GRAPHICS_API RenderWindow : public WindowBase, public RenderTarget
{
public:
    ////////////////////////////////////////////////////////////
    /// \brief Default constructor
    ///
    /// This constructor doesn't actually create the window,
    /// use the other constructors or call `create()` to do so.
    ///
    ////////////////////////////////////////////////////////////
    RenderWindow();

    ////////////////////////////////////////////////////////////
    /// \brief Destructor
    ///
    ////////////////////////////////////////////////////////////
    ~RenderWindow() override;

    ////////////////////////////////////////////////////////////
    /// \brief Deleted copy constructor
    ///
    ////////////////////////////////////////////////////////////
    RenderWindow(const RenderWindow&) = delete;

    ////////////////////////////////////////////////////////////
    /// \brief Deleted copy assignment
    ///
    ////////////////////////////////////////////////////////////
    RenderWindow& operator=(const RenderWindow&) = delete;

    ////////////////////////////////////////////////////////////
    /// \brief Move constructor
    ///
    ////////////////////////////////////////////////////////////
    RenderWindow(RenderWindow&&) noexcept;

    ////////////////////////////////////////////////////////////
    /// \brief Move assignment operator
    ///
    ////////////////////////////////////////////////////////////
    RenderWindow& operator=(RenderWindow&&) noexcept;

    ////////////////////////////////////////////////////////////
    /// \brief Construct a new window
    ///
    /// \param mode     Video mode to use
    /// \param title    Title of the window
    /// \param style    %Window style
    /// \param state    %Window state
    /// \param settings Additional settings for the rendering context
    ///
    ////////////////////////////////////////////////////////////
    RenderWindow(VideoMode              mode,
                 const String&          title,
                 std::uint32_t          style    = Style::Default,
                 State                  state    = State::Windowed,
                 const ContextSettings& settings = {});

    ////////////////////////////////////////////////////////////
    /// \brief Construct a new window
    ///
    /// \param mode     Video mode to use
    /// \param title    Title of the window
    /// \param state    %Window state
    /// \param settings Additional settings for the rendering context
    ///
    ////////////////////////////////////////////////////////////
    RenderWindow(VideoMode mode, const String& title, State state, const ContextSettings& settings = {});

    ////////////////////////////////////////////////////////////
    /// \brief Construct the window from an existing control
    ///
    /// \param handle   Platform-specific handle of the control
    /// \param settings Additional settings for the rendering context
    ///
    ////////////////////////////////////////////////////////////
    explicit RenderWindow(WindowHandle handle, const ContextSettings& settings = {});

    ////////////////////////////////////////////////////////////
    /// \brief Create (or recreate) the window
    ///
    /// \param mode     Video mode to use
    /// \param title    Title of the window
    /// \param style    %Window style
    /// \param state    %Window state
    /// \param settings Additional settings for the rendering context
    ///
    ////////////////////////////////////////////////////////////
    void create(VideoMode mode, const String& title, std::uint32_t style, State state, const ContextSettings& settings);

    ////////////////////////////////////////////////////////////
    /// \brief Create (or recreate) the window
    ///
    ////////////////////////////////////////////////////////////
    void create(VideoMode mode, const String& title, std::uint32_t style = Style::Default, State state = State::Windowed) override;

    ////////////////////////////////////////////////////////////
    /// \brief Create (or recreate) the window
    ///
    ////////////////////////////////////////////////////////////
    void create(VideoMode mode, const String& title, State state) override;

    ////////////////////////////////////////////////////////////
    /// \brief Create (or recreate) the window from an existing control
    ///
    ////////////////////////////////////////////////////////////
    void create(WindowHandle handle) override;

    ////////////////////////////////////////////////////////////
    /// \brief Create (or recreate) the window from an existing control
    ///
    /// \param handle   Platform-specific handle of the control
    /// \param settings Additional settings for the rendering context
    ///
    ////////////////////////////////////////////////////////////
    void create(WindowHandle handle, const ContextSettings& settings);

    ////////////////////////////////////////////////////////////
    /// \brief Close the window and destroy all attached resources
    ///
    ////////////////////////////////////////////////////////////
    void close() override;

    ////////////////////////////////////////////////////////////
    /// \brief Get the settings of the rendering context
    ///
    /// \return Structure containing the context settings
    ///
    ////////////////////////////////////////////////////////////
    [[nodiscard]] const ContextSettings& getSettings() const;

    ////////////////////////////////////////////////////////////
    /// \brief Enable or disable vertical synchronization
    ///
    /// \param enabled `true` to enable v-sync, `false` to deactivate it
    ///
    ////////////////////////////////////////////////////////////
    void setVerticalSyncEnabled(bool enabled);

    ////////////////////////////////////////////////////////////
    /// \brief Limit the framerate to a maximum fixed frequency
    ///
    /// \param limit Framerate limit, in frames per seconds (use 0 to disable limit)
    ///
    ////////////////////////////////////////////////////////////
    void setFramerateLimit(unsigned int limit);

    ////////////////////////////////////////////////////////////
    /// \brief Activate or deactivate the window as the current
    ///        target for rendering
    ///
    /// \param active `true` to activate, `false` to deactivate
    ///
    /// \return `true` if operation was successful, `false` otherwise
    ///
    ////////////////////////////////////////////////////////////
    [[nodiscard]] bool setActive(bool active = true) override;

    ////////////////////////////////////////////////////////////
    /// \brief Display on screen what has been rendered so far
    ///
    /// This function swaps the back and front buffers after
    /// rendering has been done for the current frame.
    ///
    ////////////////////////////////////////////////////////////
    void display();

    ////////////////////////////////////////////////////////////
    /// \brief Get the size of the rendering region of the window
    ///
    /// \return Size in pixels
    ///
    ////////////////////////////////////////////////////////////
    [[nodiscard]] Vector2u getSize() const override;

    ////////////////////////////////////////////////////////////
    /// \brief Change the window's icon
    ///
    /// \param icon Image to use as the icon
    ///
    ////////////////////////////////////////////////////////////
    void setIcon(const Image& icon);
    using WindowBase::setIcon;

    ////////////////////////////////////////////////////////////
    /// \brief Tell if the window will use sRGB encoding when drawing on it
    ///
    /// \return `true` if the window uses sRGB encoding, `false` otherwise
    ///
    ////////////////////////////////////////////////////////////
    [[nodiscard]] bool isSrgb() const override;

protected:
    ////////////////////////////////////////////////////////////
    /// \brief Function called after the window has been created
    ///
    ////////////////////////////////////////////////////////////
    void onCreate() override;

    ////////////////////////////////////////////////////////////
    /// \brief Function called after the window has been resized
    ///
    ////////////////////////////////////////////////////////////
    void onResize() override;

private:
    ////////////////////////////////////////////////////////////
    // Member data
    ////////////////////////////////////////////////////////////
    struct RenderContext;

    std::unique_ptr<RenderContext> m_renderContext; //!< Backend-specific rendering context
    ContextSettings               m_settings;      //!< Cached context settings
    unsigned int                  m_defaultFrameBuffer{}; //!< Framebuffer to bind when targeting this window
    Clock                         m_clock;         //!< Clock for frame rate limiting
    Time                          m_frameTimeLimit; //!< Minimum time between frames
};

} // namespace sf

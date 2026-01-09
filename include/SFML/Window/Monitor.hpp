////////////////////////////////////////////////////////////
//
// SFML - Simple and Fast Multimedia Library
// Copyright (C) 2007-2025 Laurent Gomila (laurent@sfml-dev.org)
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
#include "SFML/System/String.hpp"

#include <SFML/Window/VideoMode.hpp>

namespace sf
{
namespace priv
{
class WindowImplWin32;
}

////////////////////////////////////////////////////////////
/// \brief A Monitor device
///
////////////////////////////////////////////////////////////
class SFML_WINDOW_API Monitor
{
public:

    Monitor(String name, bool isDefault) : m_name(name), m_default(isDefault)
    {
    }

    ////////////////////////////////////////////////////////////
    /// \brief Get the video modes supported by this monitor
    ////////////////////////////////////////////////////////////
    std::vector<VideoMode> getVideoModes() const;

    ////////////////////////////////////////////////////////////
    /// \brief Get the video mode currently being used
    ////////////////////////////////////////////////////////////
    VideoMode getVideoMode() const;

    ////////////////////////////////////////////////////////////
    /// \brief Get all the available monitors
    ////////////////////////////////////////////////////////////
    static std::vector<Monitor> getAllMonitors();

    ////////////////////////////////////////////////////////////
    /// \brief Get the name of this monitor
    ////////////////////////////////////////////////////////////
    String getName() const
    {
        return m_name;
    }

    ////////////////////////////////////////////////////////////
    /// \brief Is this the default monitor
    ////////////////////////////////////////////////////////////
    bool isDefault() const
    {
        return m_default;
    }

    ////////////////////////////////////////////////////////////
    /// \brief Get the position of this monitor
    ////////////////////////////////////////////////////////////
    Vector2i getPosition() const;

private:

    String m_name; ///< The name of the monitor
    bool   m_default; ///< True if this is the default display
};
} // namespace sf

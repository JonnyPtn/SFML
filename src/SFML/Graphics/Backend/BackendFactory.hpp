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
#include <SFML/Graphics/Backend/GraphicsBackend.hpp>

#if defined(SFML_BACKEND_METAL)
#include <SFML/Graphics/Backend/Metal/MetalBackend.hpp>
#elif defined(SFML_BACKEND_DX12)
#include <SFML/Graphics/Backend/DX12/DX12Backend.hpp>
#elif defined(SFML_BACKEND_VULKAN)
#include <SFML/Graphics/Backend/Vulkan/VulkanBackend.hpp>
#else
#include <SFML/Graphics/Backend/OpenGL/GLBackend.hpp>
#endif


namespace sf::priv
{

////////////////////////////////////////////////////////////
/// \brief Get the active graphics backend instance
///
/// Returns the singleton backend selected at compile time.
///
////////////////////////////////////////////////////////////
inline GraphicsBackend& getGraphicsBackend()
{
#if defined(SFML_BACKEND_METAL)
    static MetalBackend backend;
#elif defined(SFML_BACKEND_DX12)
    static DX12Backend backend;
#elif defined(SFML_BACKEND_VULKAN)
    static VulkanBackend backend;
#else
    static GLBackend backend;
#endif
    return backend;
}

} // namespace sf::priv

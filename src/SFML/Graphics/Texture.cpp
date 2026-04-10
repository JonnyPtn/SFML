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
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <SFML/Window/Context.hpp>
#include <SFML/Window/Window.hpp>

#include <SFML/System/Err.hpp>
#include <SFML/System/Exception.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <ostream>
#include <utility>

#include <cassert>


namespace
{
// A nested named namespace is used here to allow unity builds of SFML.
namespace TextureImpl
{
// Thread-safe unique identifier generator,
// is used for states cache (see RenderTarget)
std::uint64_t getUniqueId() noexcept
{
    static std::atomic<std::uint64_t> id(1); // start at 1, zero is "no texture"

    return id.fetch_add(1);
}
} // namespace TextureImpl
} // namespace


namespace sf
{
////////////////////////////////////////////////////////////
Texture::Texture() : m_cacheId(TextureImpl::getUniqueId())
{
}


////////////////////////////////////////////////////////////
Texture::Texture(const std::filesystem::path& filename, bool sRgb) : Texture()
{
    if (!loadFromFile(filename, sRgb))
        throw Exception("Failed to load texture from file");
}


////////////////////////////////////////////////////////////
Texture::Texture(const std::filesystem::path& filename, bool sRgb, const IntRect& area) : Texture()
{
    if (!loadFromFile(filename, sRgb, area))
        throw Exception("Failed to load texture from file");
}


////////////////////////////////////////////////////////////
Texture::Texture(const void* data, std::size_t size, bool sRgb) : Texture()
{
    if (!loadFromMemory(data, size, sRgb))
        throw Exception("Failed to load texture from memory");
}


////////////////////////////////////////////////////////////
Texture::Texture(const void* data, std::size_t size, bool sRgb, const IntRect& area) : Texture()
{
    if (!loadFromMemory(data, size, sRgb, area))
        throw Exception("Failed to load texture from memory");
}


////////////////////////////////////////////////////////////
Texture::Texture(InputStream& stream, bool sRgb) : Texture()
{
    if (!loadFromStream(stream, sRgb))
        throw Exception("Failed to load texture from stream");
}


////////////////////////////////////////////////////////////
Texture::Texture(InputStream& stream, bool sRgb, const IntRect& area) : Texture()
{
    if (!loadFromStream(stream, sRgb, area))
        throw Exception("Failed to load texture from stream");
}


////////////////////////////////////////////////////////////
Texture::Texture(const Image& image, bool sRgb) : Texture()
{
    if (!loadFromImage(image, sRgb))
        throw Exception("Failed to load texture from image");
}


////////////////////////////////////////////////////////////
Texture::Texture(const Image& image, bool sRgb, const IntRect& area) : Texture()
{
    if (!loadFromImage(image, sRgb, area))
        throw Exception("Failed to load texture from image");
}


////////////////////////////////////////////////////////////
Texture::Texture(Vector2u size, bool sRgb) : Texture()
{
    if (!resize(size, sRgb))
        throw Exception("Failed to create texture");
}


////////////////////////////////////////////////////////////
Texture::Texture(const Texture& copy) :
    m_isSmooth(copy.m_isSmooth),
    m_sRgb(copy.m_sRgb),
    m_isRepeated(copy.m_isRepeated),
    m_cacheId(TextureImpl::getUniqueId())
{
    if (!copy.m_texture)
    {
        return;
    }

    if (resize(copy.getSize(), copy.isSrgb()))
    {
        update(copy);
    }
    else
    {
        err() << "Failed to copy texture, failed to resize texture" << std::endl;
    }
}


////////////////////////////////////////////////////////////
Texture::~Texture()
{
    // Destroy the texture via the backend
    if (m_texture)
    {
        const priv::BackendContextLock lock;
        priv::getGraphicsBackend().destroyTexture(m_texture);
    }

#ifndef NDEBUG
    // Set m_texture and m_cacheId to an invalid value to help the assert and glIsTexture in bind detect trying
    // to bind this texture in cases where it has already been destroyed but its memory not yet deallocated
    m_texture = 0xFFFFFFFFFFFFFFFFull;
    m_cacheId = 0xFFFFFFFFFFFFFFFFull;
#endif
}

////////////////////////////////////////////////////////////
Texture::Texture(Texture&& right) noexcept :
    m_size(std::exchange(right.m_size, {})),
    m_texture(std::exchange(right.m_texture, 0)),
    m_isSmooth(std::exchange(right.m_isSmooth, false)),
    m_sRgb(std::exchange(right.m_sRgb, false)),
    m_isRepeated(std::exchange(right.m_isRepeated, false)),
    m_fboAttachment(std::exchange(right.m_fboAttachment, false)),
    m_hasMipmap(std::exchange(right.m_hasMipmap, false)),
    m_cacheId(std::exchange(right.m_cacheId, 0))
{
}

////////////////////////////////////////////////////////////
Texture& Texture::operator=(Texture&& right) noexcept
{
    // Catch self-moving.
    if (&right == this)
    {
        return *this;
    }

    // Destroy the texture via the backend
    if (m_texture)
    {
        const priv::BackendContextLock lock;
        priv::getGraphicsBackend().destroyTexture(m_texture);
    }

    // Move old to new.
    m_size          = std::exchange(right.m_size, {});
    m_texture       = std::exchange(right.m_texture, 0);
    m_isSmooth      = std::exchange(right.m_isSmooth, false);
    m_sRgb          = std::exchange(right.m_sRgb, false);
    m_isRepeated    = std::exchange(right.m_isRepeated, false);
    m_fboAttachment = std::exchange(right.m_fboAttachment, false);
    m_hasMipmap     = std::exchange(right.m_hasMipmap, false);
    m_cacheId       = std::exchange(right.m_cacheId, 0);
    return *this;
}


////////////////////////////////////////////////////////////
bool Texture::resize(Vector2u size, bool sRgb)
{
    // Check if texture parameters are valid before creating it
    if ((size.x == 0) || (size.y == 0))
    {
        err() << "Failed to resize texture, invalid size (" << size.x << "x" << size.y << ")" << std::endl;
        return false;
    }

    const priv::BackendContextLock lock;


    // Check the maximum texture size
    const unsigned int maxSize = getMaximumSize();
    if ((size.x > maxSize) || (size.y > maxSize))
    {
        err() << "Failed to create texture, its size is too high "
              << "(" << size.x << "x" << size.y << ", "
              << "maximum is " << maxSize << "x" << maxSize << ")" << std::endl;
        return false;
    }

    // All the validity checks passed, we can store the new texture settings
    m_size          = size;
    m_fboAttachment = false;

    auto& backend = priv::getGraphicsBackend();

    // Destroy old texture if it exists, then create a new one
    if (m_texture)
        backend.destroyTexture(m_texture);

    m_sRgb = sRgb;

    // Check sRGB support
    if (m_sRgb && !backend.isSrgbTextureAvailable())
    {
        static bool warned = false;
        if (!warned)
        {
            err() << "sRGB texture extension unavailable" << '\n'
                  << "Automatic sRGB to linear conversion disabled" << std::endl;
            warned = true;
        }
        m_sRgb = false;
    }

    const auto handle = backend.createTexture(m_size, m_sRgb);
    if (!handle)
    {
        err() << "Failed to create texture" << std::endl;
        m_texture = 0;
        return false;
    }

    m_texture = handle;

    // Apply current smooth and repeat settings
    backend.setTextureSmooth(handle, m_isSmooth, false);
    backend.setTextureRepeated(handle, m_isRepeated);

    m_cacheId = TextureImpl::getUniqueId();
    m_hasMipmap = false;

    return true;
}


////////////////////////////////////////////////////////////
bool Texture::loadFromFile(const std::filesystem::path& filename, bool sRgb, const IntRect& area)
{
    Image image;
    return image.loadFromFile(filename) && loadFromImage(image, sRgb, area);
}


////////////////////////////////////////////////////////////
bool Texture::loadFromMemory(const void* data, std::size_t size, bool sRgb, const IntRect& area)
{
    Image image;
    return image.loadFromMemory(data, size) && loadFromImage(image, sRgb, area);
}


////////////////////////////////////////////////////////////
bool Texture::loadFromStream(InputStream& stream, bool sRgb, const IntRect& area)
{
    Image image;
    return image.loadFromStream(stream) && loadFromImage(image, sRgb, area);
}


////////////////////////////////////////////////////////////
bool Texture::loadFromImage(const Image& image, bool sRgb, const IntRect& area)
{
    // Retrieve the image size
    const auto size = Vector2i(image.getSize());

    // Load the entire image if the source area is either empty or contains the whole image
    if (area.size.x == 0 || (area.size.y == 0) ||
        ((area.position.x <= 0) && (area.position.y <= 0) && (area.size.x >= size.x) && (area.size.y >= size.y)))
    {
        // Load the entire image
        if (resize(image.getSize(), sRgb))
        {
            update(image);
            return true;
        }

        // Error message generated in called function.
        return false;
    }

    // Load a sub-area of the image
    assert(area.size.x > 0 && "Area size x cannot be negative");
    assert(area.size.y > 0 && "Area size y cannot be negative");
    assert(area.position.x < size.x && "Area position x is out of image bounds");
    assert(area.position.y < size.y && "Area position y is out of image bounds");

    // Adjust the rectangle to the size of the image
    IntRect rectangle    = area;
    rectangle.position.x = std::max(rectangle.position.x, 0);
    rectangle.position.y = std::max(rectangle.position.y, 0);
    rectangle.size.x     = std::min(rectangle.size.x, size.x - rectangle.position.x);
    rectangle.size.y     = std::min(rectangle.size.y, size.y - rectangle.position.y);

    // Create the texture and upload the pixels row by row
    if (resize(Vector2u(rectangle.size), sRgb))
    {
        const priv::BackendContextLock lock;


        const std::uint8_t* pixels = image.getPixelsPtr() + 4 * (rectangle.position.x + (size.x * rectangle.position.y));
        for (int i = 0; i < rectangle.size.y; ++i)
        {
            priv::getGraphicsBackend().updateTexture(m_texture,
                                                     pixels,
                                                     Vector2u(Vector2i(rectangle.size.x, 1)),
                                                     Vector2u(Vector2i(0, i)));
            pixels += 4 * size.x;
        }

        m_hasMipmap = false;
        return true;
    }

    // Error message generated in called function.
    return false;
}


////////////////////////////////////////////////////////////
Vector2u Texture::getSize() const
{
    return m_size;
}


////////////////////////////////////////////////////////////
Image Texture::copyToImage() const
{
    if (!m_texture)
        return {};

    const priv::BackendContextLock lock;


    // Read back the texture via the backend (handles NPOT cropping and Y-flip internally)
    return priv::getGraphicsBackend().readbackTexture(m_texture, m_size);
}


////////////////////////////////////////////////////////////
void Texture::update(const std::uint8_t* pixels)
{
    // Update the whole texture
    update(pixels, m_size, {0, 0});
}


////////////////////////////////////////////////////////////
void Texture::update(const std::uint8_t* pixels, Vector2u size, Vector2u dest)
{
    assert(dest.x + size.x <= m_size.x && "Destination x coordinate is outside of texture");
    assert(dest.y + size.y <= m_size.y && "Destination y coordinate is outside of texture");

    if (!pixels || !m_texture)
        return;

    const priv::BackendContextLock lock;


    auto& backend = priv::getGraphicsBackend();
    const auto handle = m_texture;
    backend.updateTexture(handle, pixels, size, dest);

    m_hasMipmap = false;
    backend.setTextureFlipped(handle, false);
    m_cacheId = TextureImpl::getUniqueId();
}


////////////////////////////////////////////////////////////
void Texture::update(const Texture& texture)
{
    // Update the whole texture
    update(texture, {0, 0});
}


////////////////////////////////////////////////////////////
void Texture::update(const Texture& texture, Vector2u dest)
{
    assert(dest.x + texture.m_size.x <= m_size.x && "Destination x coordinate is outside of texture");
    assert(dest.y + texture.m_size.y <= m_size.y && "Destination y coordinate is outside of texture");

    if (!m_texture || !texture.m_texture)
        return;

    const priv::BackendContextLock lock;

    auto& backend = priv::getGraphicsBackend();
    const auto handle = m_texture;
    backend.updateTextureFromTexture(handle,
                                     texture.m_texture,
                                     texture.m_size,
                                     dest);

    m_hasMipmap = false;
    backend.setTextureFlipped(handle, false);
    m_cacheId = TextureImpl::getUniqueId();
}


////////////////////////////////////////////////////////////
void Texture::update(const Image& image)
{
    // Update the whole texture
    update(image.getPixelsPtr(), image.getSize(), {0, 0});
}


////////////////////////////////////////////////////////////
void Texture::update(const Image& image, Vector2u dest)
{
    update(image.getPixelsPtr(), image.getSize(), dest);
}


////////////////////////////////////////////////////////////
void Texture::update(const Window& window)
{
    update(window, {0, 0});
}


////////////////////////////////////////////////////////////
void Texture::update(const Window& window, Vector2u dest)
{
    assert(dest.x + window.getSize().x <= m_size.x && "Destination x coordinate is outside of texture");
    assert(dest.y + window.getSize().y <= m_size.y && "Destination y coordinate is outside of texture");

    if (!m_texture || !window.setActive(true))
        return;

    const priv::BackendContextLock lock;


    auto& backend = priv::getGraphicsBackend();
    const auto handle = m_texture;
    backend.updateTextureFromFramebuffer(handle, window.getSize(), dest);

    m_hasMipmap = false;
    backend.setTextureFlipped(handle, true);
    m_cacheId = TextureImpl::getUniqueId();
}


////////////////////////////////////////////////////////////
void Texture::setSmooth(bool smooth)
{
    if (smooth == m_isSmooth)
        return;

    m_isSmooth = smooth;

    if (!m_texture)
        return;

    const priv::BackendContextLock lock;


    priv::getGraphicsBackend().setTextureSmooth(m_texture, m_isSmooth, m_hasMipmap);
}


////////////////////////////////////////////////////////////
bool Texture::isSmooth() const
{
    return m_isSmooth;
}


////////////////////////////////////////////////////////////
bool Texture::isSrgb() const
{
    return m_sRgb;
}


////////////////////////////////////////////////////////////
void Texture::setRepeated(bool repeated)
{
    if (repeated == m_isRepeated)
        return;

    m_isRepeated = repeated;

    if (!m_texture)
        return;

    const priv::BackendContextLock lock;


    priv::getGraphicsBackend().setTextureRepeated(m_texture, m_isRepeated);
}


////////////////////////////////////////////////////////////
bool Texture::isRepeated() const
{
    return m_isRepeated;
}


////////////////////////////////////////////////////////////
bool Texture::generateMipmap()
{
    if (!m_texture)
        return false;

    const priv::BackendContextLock lock;


    if (!priv::getGraphicsBackend().generateMipmap(m_texture, m_size, m_isSmooth))
        return false;

    m_hasMipmap = true;
    return true;
}


////////////////////////////////////////////////////////////
void Texture::invalidateMipmap()
{
    if (!m_hasMipmap)
        return;

    const priv::BackendContextLock lock;


    priv::getGraphicsBackend().setTextureSmooth(m_texture, m_isSmooth, false);

    m_hasMipmap = false;
}


////////////////////////////////////////////////////////////
void Texture::bind(const Texture* texture, CoordinateType coordinateType)
{
    const priv::BackendContextLock lock;

    auto& backend = priv::getGraphicsBackend();

    if (texture && texture->m_texture)
        backend.bindTexture(texture->m_texture, coordinateType);
    else
        backend.bindTexture(0, coordinateType);
}


////////////////////////////////////////////////////////////
unsigned int Texture::getMaximumSize()
{
    static const unsigned int size = []
    {
        const priv::BackendContextLock transientLock;
        return priv::getGraphicsBackend().getMaxTextureSize();
    }();

    return size;
}


////////////////////////////////////////////////////////////
Texture& Texture::operator=(const Texture& right)
{
    Texture temp(right);

    swap(temp);

    return *this;
}


////////////////////////////////////////////////////////////
void Texture::swap(Texture& right) noexcept
{
    std::swap(m_size, right.m_size);
    std::swap(m_texture, right.m_texture);
    std::swap(m_isSmooth, right.m_isSmooth);
    std::swap(m_sRgb, right.m_sRgb);
    std::swap(m_isRepeated, right.m_isRepeated);
    std::swap(m_fboAttachment, right.m_fboAttachment);
    std::swap(m_hasMipmap, right.m_hasMipmap);
    std::swap(m_cacheId, right.m_cacheId);
}


////////////////////////////////////////////////////////////
unsigned int Texture::getNativeHandle() const
{
    return static_cast<unsigned int>(m_texture);
}


////////////////////////////////////////////////////////////
void swap(Texture& left, Texture& right) noexcept
{
    left.swap(right);
}

} // namespace sf

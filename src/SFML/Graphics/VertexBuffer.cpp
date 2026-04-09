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
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Vertex.hpp>
#include <SFML/Graphics/VertexBuffer.hpp>

#include <SFML/System/Err.hpp>

#include <ostream>
#include <utility>

#include <cstddef>
#include <cstring>


namespace sf
{
////////////////////////////////////////////////////////////
VertexBuffer::VertexBuffer(PrimitiveType type) : m_primitiveType(type)
{
}


////////////////////////////////////////////////////////////
VertexBuffer::VertexBuffer(Usage usage) : m_usage(usage)
{
}


////////////////////////////////////////////////////////////
VertexBuffer::VertexBuffer(PrimitiveType type, Usage usage) : m_primitiveType(type), m_usage(usage)
{
}


////////////////////////////////////////////////////////////
VertexBuffer::VertexBuffer(const VertexBuffer& copy) :
    GlResource(copy),
    m_primitiveType(copy.m_primitiveType),
    m_usage(copy.m_usage)
{
    if (copy.m_buffer && copy.m_size)
    {
        if (!create(copy.m_size))
        {
            err() << "Could not create vertex buffer for copying" << std::endl;
            return;
        }

        if (!update(copy))
            err() << "Could not copy vertex buffer" << std::endl;
    }
}


////////////////////////////////////////////////////////////
VertexBuffer::~VertexBuffer()
{
    if (m_buffer)
    {
        const TransientContextLock contextLock;
        priv::getGraphicsBackend().destroyBuffer(static_cast<priv::BackendBufferHandle>(m_buffer));
    }
}


////////////////////////////////////////////////////////////
bool VertexBuffer::create(std::size_t vertexCount)
{
    if (!isAvailable())
        return false;

    const TransientContextLock contextLock;

    if (!m_buffer)
    {
        const auto handle = priv::getGraphicsBackend().createBuffer(vertexCount, m_usage);
        if (!handle)
        {
            err() << "Could not create vertex buffer, generation failed" << std::endl;
            return false;
        }
        m_buffer = static_cast<unsigned int>(handle);
    }
    else
    {
        // Buffer already exists, reallocate
        priv::getGraphicsBackend().updateBuffer(static_cast<priv::BackendBufferHandle>(m_buffer), nullptr, vertexCount, 0);
    }

    m_size = vertexCount;
    return true;
}


////////////////////////////////////////////////////////////
std::size_t VertexBuffer::getVertexCount() const
{
    return m_size;
}


////////////////////////////////////////////////////////////
bool VertexBuffer::update(const Vertex* vertices)
{
    return update(vertices, m_size, 0);
}


////////////////////////////////////////////////////////////
bool VertexBuffer::update(const Vertex* vertices, std::size_t vertexCount, unsigned int offset)
{
    if (!m_buffer || !vertices)
        return false;

    if (offset && (offset + vertexCount > m_size))
        return false;

    const TransientContextLock contextLock;

    auto& backend = priv::getGraphicsBackend();

    // Check if we need to resize or orphan the buffer
    if (vertexCount >= m_size)
    {
        // Recreate the buffer with the new size
        backend.destroyBuffer(static_cast<priv::BackendBufferHandle>(m_buffer));
        const auto handle = backend.createBuffer(vertexCount, m_usage);
        if (!handle)
            return false;
        m_buffer = static_cast<unsigned int>(handle);
        m_size   = vertexCount;
    }

    return backend.updateBuffer(static_cast<priv::BackendBufferHandle>(m_buffer), vertices, vertexCount, offset);
}


////////////////////////////////////////////////////////////
bool VertexBuffer::update(const VertexBuffer& vertexBuffer)
{
    if (!m_buffer || !vertexBuffer.m_buffer)
        return false;

    const TransientContextLock contextLock;

    // Make sure that extensions are initialized
    priv::ensureExtensionsInit();

    auto& backend = priv::getGraphicsBackend();

    if (backend.copyBuffer(static_cast<priv::BackendBufferHandle>(m_buffer),
                           static_cast<priv::BackendBufferHandle>(vertexBuffer.m_buffer),
                           vertexBuffer.m_size))
    {
        return true;
    }

    // Fallback: read back source and upload to dest
    // This path uses direct GL because it requires glMapBuffer which
    // isn't in the backend interface (it's a legacy GL pattern)
#ifndef SFML_OPENGL_ES
    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, m_buffer));
    glCheck(GLEXT_glBufferData(GLEXT_GL_ARRAY_BUFFER,
                               static_cast<GLsizeiptrARB>(sizeof(Vertex) * vertexBuffer.m_size),
                               nullptr,
                               GLEXT_GL_STREAM_DRAW));

    void* const destination = glCheck(GLEXT_glMapBuffer(GLEXT_GL_ARRAY_BUFFER, GLEXT_GL_WRITE_ONLY));

    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, vertexBuffer.m_buffer));

    const void* const source = glCheck(GLEXT_glMapBuffer(GLEXT_GL_ARRAY_BUFFER, GLEXT_GL_READ_ONLY));

    std::memcpy(destination, source, sizeof(Vertex) * vertexBuffer.m_size);

    const GLboolean sourceResult = glCheck(GLEXT_glUnmapBuffer(GLEXT_GL_ARRAY_BUFFER));

    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, m_buffer));

    const GLboolean destinationResult = glCheck(GLEXT_glUnmapBuffer(GLEXT_GL_ARRAY_BUFFER));

    glCheck(GLEXT_glBindBuffer(GLEXT_GL_ARRAY_BUFFER, 0));

    return (sourceResult == GL_TRUE) && (destinationResult == GL_TRUE);
#else
    return false;
#endif
}


////////////////////////////////////////////////////////////
VertexBuffer& VertexBuffer::operator=(const VertexBuffer& right)
{
    VertexBuffer temp(right);

    swap(temp);

    return *this;
}


////////////////////////////////////////////////////////////
void VertexBuffer::swap(VertexBuffer& right) noexcept
{
    std::swap(m_size, right.m_size);
    std::swap(m_buffer, right.m_buffer);
    std::swap(m_primitiveType, right.m_primitiveType);
    std::swap(m_usage, right.m_usage);
}


////////////////////////////////////////////////////////////
unsigned int VertexBuffer::getNativeHandle() const
{
    return m_buffer;
}


////////////////////////////////////////////////////////////
void VertexBuffer::bind(const VertexBuffer* vertexBuffer)
{
    if (!isAvailable())
        return;

    const TransientContextLock lock;

    priv::getGraphicsBackend().bindBuffer(
        static_cast<priv::BackendBufferHandle>(vertexBuffer ? vertexBuffer->m_buffer : 0));
}


////////////////////////////////////////////////////////////
void VertexBuffer::setPrimitiveType(PrimitiveType type)
{
    m_primitiveType = type;
}


////////////////////////////////////////////////////////////
PrimitiveType VertexBuffer::getPrimitiveType() const
{
    return m_primitiveType;
}


////////////////////////////////////////////////////////////
void VertexBuffer::setUsage(Usage usage)
{
    m_usage = usage;
}


////////////////////////////////////////////////////////////
VertexBuffer::Usage VertexBuffer::getUsage() const
{
    return m_usage;
}


////////////////////////////////////////////////////////////
bool VertexBuffer::isAvailable()
{
    static const bool available = []
    {
        const TransientContextLock contextLock;

        // Make sure that extensions are initialized
        priv::ensureExtensionsInit();

        return GLEXT_vertex_buffer_object != 0;
    }();

    return available;
}


////////////////////////////////////////////////////////////
void VertexBuffer::draw(RenderTarget& target, RenderStates states) const
{
    if (m_buffer && m_size)
        target.draw(*this, 0, m_size, states);
}


////////////////////////////////////////////////////////////
void swap(VertexBuffer& left, VertexBuffer& right) noexcept
{
    left.swap(right);
}

} // namespace sf

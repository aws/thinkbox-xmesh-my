// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#ifndef _BOOL
#define _BOOL
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <iostream>

using std::cerr;
using std::endl;

#include "SequenceXMeshGeometryOverride.hpp"

#if MAYA_API_VERSION >= 201300

#include "SequenceXMeshNode.hpp"

#include <maya/MHWGeometryUtilities.h>
#include <maya/MShaderManager.h>

#include <frantic/geometry/trimesh3.hpp>
#include <frantic/maya/graphics/maya_space.hpp>

#include <xmesh/fractional_index_iterator.hpp>

static const MString ICON_ITEM_NAME = "icon";
static const MString VERTICES_ITEM_NAME = "vertices";
static const MString BBOX_ITEM_NAME = "boundingBox";

using namespace MHWRender;

namespace {

MRenderItem* create_render_item( const MString& name, MGeometry::Primitive primitive, MGeometry::DrawMode mode,
                                 bool raiseAboveShaded ) {
#if MAYA_API_VERSION >= 201400
    return MRenderItem::Create( name, primitive, mode, raiseAboveShaded );
#else
    return new MRenderItem( name, primitive, mode, raiseAboveShaded );
#endif
}

void release_shader( const MShaderManager& shaderManager, MShaderInstance* shader ) {
    if( shader ) {
#if MAYA_API_VERSION >= 201400
        shaderManager.releaseShader( shader );
#else
        delete shader;
#endif
    }
}

template <class T>
T* acquire_buffer( MHWRender::MVertexBuffer* vertexBuffer, unsigned int size, bool writeOnly ) {
    if( vertexBuffer ) {
#if MAYA_API_VERSION >= 201400
        return reinterpret_cast<T*>( vertexBuffer->acquire( size, writeOnly ) );
#else
        return reinterpret_cast<T*>( vertexBuffer->acquire( size ) );
#endif
    }
    return NULL;
}

template <class T>
T* acquire_buffer( MHWRender::MIndexBuffer* indexBuffer, unsigned int size, bool writeOnly ) {
    if( indexBuffer ) {
#if MAYA_API_VERSION >= 201400
        return reinterpret_cast<T*>( indexBuffer->acquire( size, writeOnly ) );
#else
        return reinterpret_cast<T*>( indexBuffer->acquire( size ) );
#endif
    }
    return NULL;
}

} // anonymous namespace

MPxGeometryOverride* SequenceXMeshGeometryOverride::create( const MObject& obj ) {
    return new SequenceXMeshGeometryOverride( obj );
}

SequenceXMeshGeometryOverride::SequenceXMeshGeometryOverride( const MObject& obj )
    : MPxGeometryOverride( obj )
    , m_obj( obj )
    , m_vertexBuffer( NULL )
    , m_seqXMeshNode( NULL ) {
    MStatus stat;
    MFnDependencyNode node( obj, &stat );

    if( stat ) {
        m_seqXMeshNode = dynamic_cast<SequenceXMeshNode*>( node.userNode() );
    }
}

// updateDG and related helper functions
//
// caches information from associated helper functions
void SequenceXMeshGeometryOverride::updateDG() {
    MStatus status;
    MFnDagNode objFunctionSet;

    status = objFunctionSet.setObject( m_obj );

    if( status ) {
        cache_wireframe_color( objFunctionSet );
        cache_mesh_geometry();

        m_cachedDisplayMode = m_seqXMeshNode->get_display_mode();
        m_cachedVertexFraction = m_seqXMeshNode->get_display_fraction();
    }
}

void SequenceXMeshGeometryOverride::cache_wireframe_color( const MFnDagNode& dagNodeFunctionSet ) {
    MStatus status;
    MDagPath dagPath;

    status = dagNodeFunctionSet.getPath( dagPath );

    if( status ) {
        m_cachedColor = MGeometryUtilities::wireframeColor( dagPath );
    }
}

void SequenceXMeshGeometryOverride::cache_mesh_geometry() {
    m_cachedMesh = m_seqXMeshNode->get_cached_mesh();
    m_cachedBoundBox = m_seqXMeshNode->get_mesh_bounding_box();
}

void SequenceXMeshGeometryOverride::updateRenderItems( const MDagPath& /*path*/, MRenderItemList& list ) {
    MRenderer* renderer = MRenderer::theRenderer();
    if( !renderer )
        return;

    const MShaderManager* shaderManager = renderer->getShaderManager();
    if( !shaderManager )
        return;

    // mesh vertices render item setup
    setup_render_item( VERTICES_ITEM_NAME, MGeometry::kPoints, list, *shaderManager );

    // icon render item setup
    setup_render_item( ICON_ITEM_NAME, MGeometry::kTriangles, list, *shaderManager );

    // bounding box render item setup
    setup_render_item( BBOX_ITEM_NAME, MGeometry::kLines, list, *shaderManager );
}

void SequenceXMeshGeometryOverride::setup_render_item( const MString& renderItemName,
                                                       const MGeometry::Primitive geometryType,
                                                       MRenderItemList& renderItemList,
                                                       const MShaderManager& shaderManager ) {
    int index = renderItemList.indexOf( renderItemName, geometryType, MGeometry::kAll );
    MRenderItem* renderItem = NULL;

    if( index < 0 ) {
        // Create the render item and append it to the list of render items
        renderItem = create_render_item( renderItemName, geometryType, MGeometry::kAll, false );
        renderItemList.append( renderItem );
    } else {
        renderItem = renderItemList.itemAt( index );
    }

    // Get shader and enable render item
    if( renderItem ) {
        MShaderInstance* shader = shaderManager.getStockShader( MShaderManager::k3dSolidShader );

        if( shader ) {
            set_shader_color( *shader );
            renderItem->setShader( shader );
            release_shader( shaderManager, shader );

            enable_render_items( *renderItem );
        }
    }
}

void SequenceXMeshGeometryOverride::set_shader_color( MShaderInstance& shader ) {
    float shaderColor[4];

    shaderColor[0] = m_cachedColor.r;
    shaderColor[1] = m_cachedColor.g;
    shaderColor[2] = m_cachedColor.b;
    shaderColor[3] = m_cachedColor.a;

    shader.setParameter( "solidColor", shaderColor );
}

void SequenceXMeshGeometryOverride::enable_render_items( MRenderItem& renderItem ) {
    const MString renderItemName = renderItem.name();

    if( renderItemName == ICON_ITEM_NAME ) {
        renderItem.enable( true );
    } else if( renderItemName == BBOX_ITEM_NAME && m_cachedDisplayMode == DISPLAY_MODE_BOX ) {
        renderItem.enable( true );
    } else if( renderItemName == VERTICES_ITEM_NAME && m_cachedDisplayMode == DISPLAY_MODE_VERTEX ) {
        renderItem.enable( true );
    } else {
        renderItem.enable( false );
    }
}

#if MAYA_API_VERSION >= 201400
void SequenceXMeshGeometryOverride::populateGeometry( const MGeometryRequirements& requirements,
                                                      const MRenderItemList& renderItems, MGeometry& data )
#else
void SequenceXMeshGeometryOverride::populateGeometry( const MGeometryRequirements& requirements,
                                                      MRenderItemList& renderItems, MGeometry& data )
#endif
{
    const std::size_t numBoundingBoxVertices = 8; // Number of vertices of the bounding box

    boost::shared_ptr<frantic::geometry::trimesh3> iconMesh = SequenceXMeshNode::get_icon_mesh();
    int numItems = renderItems.length();

    // Create the vertex buffer if it has not already been created
    create_vertex_buffer( requirements.vertexRequirements(), data );

    size_t vertexCount;
    if( m_cachedMesh && m_cachedDisplayMode == DISPLAY_MODE_VERTEX )
        vertexCount = xmesh::fractional_index_iterator( m_cachedMesh->vertex_count(), (float)m_cachedVertexFraction )
                          .num_indices();
    else
        vertexCount = 0;
    populate_vertex_buffer( numBoundingBoxVertices, iconMesh->vertex_count(), vertexCount, iconMesh );

    // populate the index buffers of the render items
    for( int i = 0; i < numItems; ++i ) {
        const MRenderItem* item = renderItems.itemAt( i );

        if( !item )
            continue;

        if( item->name() == BBOX_ITEM_NAME ) {
            populate_bounding_box_indices( data, *item );
        } else if( item->name() == ICON_ITEM_NAME ) {
            populate_icon_mesh_indices( data, *item, numBoundingBoxVertices, iconMesh );
        } else if( item->name() == VERTICES_ITEM_NAME ) {
            populate_mesh_object_indices( data, *item, numBoundingBoxVertices + iconMesh->vertex_count(), vertexCount );
        }
    }
}

/**
 * Creates the vertex buffer using the requirements
 *
 * Buffer is organized as follows:
 * 8 vertices - bounding box
 * n vertices - icon mesh vertices
 * m vertices - mesh vertices
 */
void SequenceXMeshGeometryOverride::create_vertex_buffer( const MVertexBufferDescriptorList& vertexRequirements,
                                                          MGeometry& data ) {
    // Handle the vertex requirements
    for( int j = 0; j < vertexRequirements.length(); ++j ) {
        MVertexBufferDescriptor desc;
        vertexRequirements.getDescriptor( j, desc );

        switch( desc.semantic() ) {
        case MGeometry::kPosition:
            // Create the vertex buffer
            m_vertexBuffer = data.createVertexBuffer( desc );
            break;
        }
    }
}

/**
 * Populates the vertex buffer with the bounding box, icon mesh, and mesh vertices
 *
 * See comment before createVertexBuffer() for a description of how the vertices are laid
 * out in the buffer
 */
void SequenceXMeshGeometryOverride::populate_vertex_buffer(
    const size_t numBoundingBoxVertices, const size_t numIconVertices, const size_t numMeshVertices,
    const boost::shared_ptr<frantic::geometry::trimesh3> iconMesh ) {
    const size_t numTotalVertices = numBoundingBoxVertices + numIconVertices + numMeshVertices;
    float* bufferPositions =
        acquire_buffer<float>( m_vertexBuffer, static_cast<unsigned int>( numTotalVertices ), true );

    if( bufferPositions ) {
        populate_bounding_box_vertices( bufferPositions );
        populate_icon_mesh_vertices( bufferPositions, numBoundingBoxVertices, iconMesh );
        populate_mesh_object_vertices( bufferPositions, numBoundingBoxVertices + numIconVertices );

        m_vertexBuffer->commit( bufferPositions );
    }
}

/**
 * Populates the beginning of the vertex buffer with the bounding boxes vertices
 */
void SequenceXMeshGeometryOverride::populate_bounding_box_vertices( float* bufferPositions ) {
    for( int corner = 0; corner < 8; ++corner ) {
        const frantic::graphics::vector3f currCorner = m_cachedBoundBox.get_corner( corner );
        bufferPositions[3 * corner] = currCorner.x;
        bufferPositions[3 * corner + 1] = currCorner.y;
        bufferPositions[3 * corner + 2] = currCorner.z;
    }
}

/**
 * Populates the middle of the vertexBuffer with the verices of the icon mesh
 */
void SequenceXMeshGeometryOverride::populate_icon_mesh_vertices(
    float* bufferPositions, const std::size_t vertexIndexOffset,
    const boost::shared_ptr<frantic::geometry::trimesh3> iconMesh ) {
    const std::vector<frantic::graphics::vector3f>& iconMeshVertices = iconMesh->vertices_ref();

    for( int i = 0; i < iconMesh->vertex_count(); ++i ) {
        bufferPositions[3 * ( i + vertexIndexOffset )] = iconMeshVertices[i].x;
        bufferPositions[3 * ( i + vertexIndexOffset ) + 1] = iconMeshVertices[i].y;
        bufferPositions[3 * ( i + vertexIndexOffset ) + 2] = iconMeshVertices[i].z;
    }
}

/**
 * Populates the end of the vertexBuffer with vertices of the loaded/created mesh object
 */
void SequenceXMeshGeometryOverride::populate_mesh_object_vertices( float* bufferPositions,
                                                                   const size_t vertexIndexOffset ) {
    std::size_t vertexCount = m_cachedMesh ? m_cachedMesh->vertex_count() : 0;
    if( vertexCount > 0 && m_cachedDisplayMode == DISPLAY_MODE_VERTEX ) {
        std::size_t currBufferPos = vertexIndexOffset;
        for( xmesh::fractional_index_iterator i( vertexCount, (float)m_cachedVertexFraction ), ie; i != ie;
             ++i, ++currBufferPos ) {
            const frantic::graphics::vector3f currVertex =
                frantic::maya::graphics::to_maya_space( m_cachedMesh->get_vertex( *i ) );
            bufferPositions[3 * currBufferPos] = currVertex.x;
            bufferPositions[3 * currBufferPos + 1] = currVertex.y;
            bufferPositions[3 * currBufferPos + 2] = currVertex.z;
        }
    }
}

/**
 * Creates and populates an index buffer for the bounding box
 */
void SequenceXMeshGeometryOverride::populate_bounding_box_indices( MGeometry& geometryData,
                                                                   const MRenderItem& renderItem ) {
    MIndexBuffer* indexBuffer = geometryData.createIndexBuffer( MGeometry::kUnsignedInt32 );

    if( indexBuffer ) {
        // 2 * 12 since a cube is composed of 12 edges and each edge requires 2 vertices to specify it
        unsigned int* buffer = acquire_buffer<unsigned int>( indexBuffer, 24, true );

        if( buffer ) {
            buffer[0] = 0;
            buffer[1] = 1;
            buffer[2] = 1;
            buffer[3] = 3;
            buffer[4] = 3;
            buffer[5] = 2;
            buffer[6] = 2;
            buffer[7] = 0;

            buffer[8] = 4;
            buffer[9] = 5;
            buffer[10] = 5;
            buffer[11] = 7;
            buffer[12] = 7;
            buffer[13] = 6;
            buffer[14] = 6;
            buffer[15] = 4;

            buffer[16] = 0;
            buffer[17] = 4;
            buffer[18] = 1;
            buffer[19] = 5;
            buffer[20] = 3;
            buffer[21] = 7;
            buffer[22] = 2;
            buffer[23] = 6;
        }

        indexBuffer->commit( buffer );
        renderItem.associateWithIndexBuffer( indexBuffer );
    }
}

/**
 * Creates and populates an index buffer for the icon mesh
 */
void SequenceXMeshGeometryOverride::populate_icon_mesh_indices(
    MGeometry& geometryData, const MRenderItem& renderItem, const std::size_t vertexIndexOffset,
    const boost::shared_ptr<frantic::geometry::trimesh3> iconMesh ) {
    MIndexBuffer* indexBuffer = geometryData.createIndexBuffer( MGeometry::kUnsignedInt32 );
    const std::vector<frantic::graphics::vector3>& meshFaces = iconMesh->faces_ref();

    if( indexBuffer ) {
        unsigned int* buffer =
            acquire_buffer<unsigned int>( indexBuffer, 3 * static_cast<unsigned int>( iconMesh->face_count() ), true );

        // Populate the index buffer
        if( buffer ) {
            for( int i = 0; i < iconMesh->face_count(); ++i ) {
                buffer[3 * i] = meshFaces[i].x + static_cast<unsigned int>( vertexIndexOffset );
                buffer[3 * i + 1] = meshFaces[i].y + static_cast<unsigned int>( vertexIndexOffset );
                buffer[3 * i + 2] = meshFaces[i].z + static_cast<unsigned int>( vertexIndexOffset );
            }
        }

        indexBuffer->commit( buffer );
        renderItem.associateWithIndexBuffer( indexBuffer );
    }
}

/**
 * Creates and populates an index buffer for the vertices of the mesh object
 */
void SequenceXMeshGeometryOverride::populate_mesh_object_indices( MGeometry& geometryData,
                                                                  const MRenderItem& renderItem,
                                                                  const size_t vertexIndexOffset,
                                                                  const size_t vertexCount ) {
    if( !m_cachedMesh ) {
        return;
    }

    MIndexBuffer* indexBuffer = geometryData.createIndexBuffer( MGeometry::kUnsignedInt32 );

    if( indexBuffer ) {
        unsigned int* buffer =
            acquire_buffer<unsigned int>( indexBuffer, static_cast<unsigned int>( vertexCount ), true );

        if( buffer ) {
            unsigned int offsetIndex = static_cast<unsigned int>( vertexIndexOffset );
            for( int i = 0; i < vertexCount; ++i, ++offsetIndex ) {
                // Must add 8 vertices for the bounding box and the number of vertices in the icon mesh since the mesh
                // vertices occur at the end of the vertex buffer
                buffer[i] = offsetIndex;
            }
        }

        indexBuffer->commit( buffer );
        renderItem.associateWithIndexBuffer( indexBuffer );
    }
}

void SequenceXMeshGeometryOverride::cleanUp() {
    m_vertexBuffer = NULL;
    m_cachedMesh = NULL;
}

#if MAYA_API_VERSION >= 201600

MHWRender::DrawAPI SequenceXMeshGeometryOverride::supportedDrawAPIs() const { return kOpenGL | kOpenGLCoreProfile; }

#endif

#endif

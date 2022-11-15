// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <maya/MTypes.h>

#if MAYA_API_VERSION >= 201300

#include <maya/MFnMesh.h>
#include <maya/MHWGeometry.h>
#include <maya/MObject.h>
#include <maya/MPxGeometryOverride.h>
#include <maya/MShaderManager.h>

#include "SequenceXMeshNode.hpp"
#include <frantic/geometry/polymesh3.hpp>

class SequenceXMeshGeometryOverride : public MHWRender::MPxGeometryOverride {
  public:
    static MHWRender::MPxGeometryOverride* create( const MObject& obj );

    SequenceXMeshGeometryOverride( const MObject& obj );

    void updateDG();

    void updateRenderItems( const MDagPath& path, MHWRender::MRenderItemList& list );

#if MAYA_API_VERSION >= 201400
    void populateGeometry( const MHWRender::MGeometryRequirements& requirements,
                           const MHWRender::MRenderItemList& renderItems, MHWRender::MGeometry& data );
#else
    void populateGeometry( const MHWRender::MGeometryRequirements& requirements,
                           MHWRender::MRenderItemList& renderItems, MHWRender::MGeometry& data );
#endif

#if MAYA_API_VERSION >= 201600
    virtual MHWRender::DrawAPI supportedDrawAPIs() const;
#endif

    void cleanUp();

  private:
    MHWRender::MVertexBuffer* m_vertexBuffer;
    MObject m_obj;
    MColor m_cachedColor;
    frantic::graphics::boundbox3f m_cachedBoundBox;
    frantic::geometry::const_polymesh3_ptr m_cachedMesh;
    display_mode m_cachedDisplayMode;
    SequenceXMeshNode* m_seqXMeshNode;
    double m_cachedVertexFraction;

    /**
     * Caches whether or not the the object is selected
     */
    void cache_wireframe_color( const MFnDagNode& dagNodeFunctionSet );

    /**
     * Caches the poly mesh object and its bounding box
     */
    void cache_mesh_geometry();

    /**
     * Sets up a render item of a specified name
     */
    void setup_render_item( const MString& renderItemName, const MHWRender::MGeometry::Primitive geometryType,
                            MHWRender::MRenderItemList& renderItemList,
                            const MHWRender::MShaderManager& shaderManager );

    /**
     * Sets up the shaders for coloring the render items
     */
    void set_shader_color( MHWRender::MShaderInstance& shader );

    /**
     * Enables/disables the appropriate render items according to the display mode
     */
    void enable_render_items( MHWRender::MRenderItem& renderItem );

    /**
     * Creates the vertex buffer using the vertexRequirements
     */
    void create_vertex_buffer( const MHWRender::MVertexBufferDescriptorList& vertexRequirements,
                               MHWRender::MGeometry& data );

    /**
     * Acquires and populates the vertexBuffer
     */
    void populate_vertex_buffer( const size_t numIconVertices, const size_t numMeshVertices,
                                 const size_t numBoundingBoxVertices,
                                 const boost::shared_ptr<frantic::geometry::trimesh3> iconMesh );

    /**
     * Populates the beginning of the vertexBuffer with the vertices of the bounding box
     */
    void populate_bounding_box_vertices( float* bufferPositions );

    /**
     * Populates the middle of the vertexBuffer with the vertices of the icon mesh
     */
    void populate_icon_mesh_vertices( float* bufferPositions, const size_t vertexIndexOffset,
                                      const boost::shared_ptr<frantic::geometry::trimesh3> iconMesh );

    /**
     * Populates the end of the vertexBuffer with vertices of the loaded/created mesh object
     */
    void populate_mesh_object_vertices( float* bufferPositions, const size_t vertexIndexOffset );

    /**
     * Creates and populates an index buffer for the bounding box
     */
    void populate_bounding_box_indices( MHWRender::MGeometry& geometryData, const MHWRender::MRenderItem& renderItem );

    /**
     * Creates and populates an index buffer for the icon mesh
     */
    void populate_icon_mesh_indices( MHWRender::MGeometry& geometryData, const MHWRender::MRenderItem& renderItem,
                                     const size_t vertexIndexOffset,
                                     const boost::shared_ptr<frantic::geometry::trimesh3> iconMesh );

    /**
     * Creates and populates an index buffer for the vertices of the mesh object
     */
    void populate_mesh_object_indices( MHWRender::MGeometry& geometryData, const MHWRender::MRenderItem& renderItem,
                                       const size_t vertexIndexOffset, const size_t vertexCount );
};

#endif

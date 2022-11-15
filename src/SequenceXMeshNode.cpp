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

#include "SequenceXMeshNode.hpp"

#include <maya/MAnimControl.h>
#include <maya/MFloatArray.h>
#include <maya/MFloatPointArray.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnMesh.h>
#include <maya/MFnMeshData.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MIntArray.h>
#include <maya/MItMeshEdge.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MSceneMessage.h>
#include <maya/MTime.h>
#include <maya/MUintArray.h>

#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <boost/make_shared.hpp>
#include <boost/optional.hpp>

#include <frantic/files/files.hpp>

#include <frantic/geometry/polymesh3.hpp>
#include <frantic/geometry/xmesh_metadata.hpp>
#include <frantic/geometry/xmesh_reader.hpp>

#include <frantic/maya/convert.hpp>
#include <frantic/maya/graphics/maya_space.hpp>
#include <frantic/maya/graphics/opengl.hpp>
#include <frantic/maya/util.hpp>

#include "XMeshLogoMesh.hpp"
#include "maya_xmesh_timing.hpp"
#include "vertices_to_edge_map.hpp"

#include <xmesh/fractional_index_iterator.hpp>

using namespace frantic::geometry;
using namespace xmesh;

MObject SequenceXMeshNode::seqPath;
MObject SequenceXMeshNode::seqProxyPath;
MObject SequenceXMeshNode::outMesh;
MObject SequenceXMeshNode::inTime;
MObject SequenceXMeshNode::inGroupIds;
MObject SequenceXMeshNode::inPlaybackGraph;
MObject SequenceXMeshNode::inEnablePlaybackGraph;
MObject SequenceXMeshNode::inFrameOffset;
MObject SequenceXMeshNode::inUseCustomRange;
MObject SequenceXMeshNode::inCustomRangeStart;
MObject SequenceXMeshNode::inCustomRangeEnd;
MObject SequenceXMeshNode::inCustomRangeStartClampMode;
MObject SequenceXMeshNode::inCustomRangeEndClampMode;
MObject SequenceXMeshNode::inSingleFileOnly;

MObject SequenceXMeshNode::inCustomScale;
MObject SequenceXMeshNode::inLengthUnit;
MObject SequenceXMeshNode::inLoadingMode;
MObject SequenceXMeshNode::inAutoProxyPath;

MObject SequenceXMeshNode::inViewportSource;
MObject SequenceXMeshNode::inDisplayMode;
MObject SequenceXMeshNode::inDisplayPercent;
MObject SequenceXMeshNode::inRenderSource;
MObject SequenceXMeshNode::inRender;

MObject SequenceXMeshNode::outMinimumAvailableFileIndex;
MObject SequenceXMeshNode::outMaximumAvailableFileIndex;

MTypeId SequenceXMeshNode::typeID( 0x00117481 ); // todo: confirm global ID from thinkbox
MString SequenceXMeshNode::drawClassification( "drawdb/geometry/sequenceXMesh" );
MString SequenceXMeshNode::drawRegistrantId( "XMeshPlugin" );
/*
// plugin callbacks
void beginRender( void* clientData ) {}
void endRender( void* clientData ) {}
*/

void computeRender( void* clientData ) {
    SequenceXMeshNode* sxmn = (SequenceXMeshNode*)clientData;
    MPlug plug( sxmn->thisMObject(), SequenceXMeshNode::inRender );

    plug.setBool( true );
}

void computeViewport( void* clientData ) {
    SequenceXMeshNode* sxmn = (SequenceXMeshNode*)clientData;
    MPlug plug( sxmn->thisMObject(), SequenceXMeshNode::inRender );

    plug.setBool( false );
}

namespace {

void getXMeshColors( const frantic::geometry::const_polymesh3_ptr polymesh, const frantic::tstring& channel,
                     const std::vector<boost::uint32_t>& mayaToPolymeshFace, MColorArray& outColorData,
                     MFnMesh::MColorRepresentation& outColorFormat, MIntArray& outColorIndices ) {
    frantic::geometry::polymesh3_const_vertex_accessor<frantic::graphics::vector3f> geomAcc =
        polymesh->get_const_vertex_accessor<frantic::graphics::vector3f>( _T("verts") );
    frantic::geometry::polymesh3_const_vertex_accessor<void> acc = polymesh->get_const_vertex_accessor( channel );

    std::size_t nColors = acc.vertex_count();
    outColorData.setLength( (int)nColors );

    if( acc.get_type() != frantic::channels::data_type_float32 ) {
        throw std::runtime_error( "channel \"" + frantic::strings::to_string( channel ) +
                                  "\" has unexpected type. Should be float32." );
    }

    if( acc.get_arity() == 1 ) {
        for( std::size_t i = 0; i < nColors; ++i ) {
            float* pData = (float*)acc.get_vertex( i );
            outColorData.set( (int)i, 0.0f, 0.0f, 0.0f, pData[0] );
            outColorFormat = MFnMesh::kAlpha;
        }
    } else if( acc.get_arity() == 2 ) {
        for( std::size_t i = 0; i < nColors; ++i ) {
            float* pData = (float*)acc.get_vertex( i );
            outColorData.set( (int)i, pData[0], pData[1], 0 );
            outColorFormat = MFnMesh::kRGB;
        }
    } else if( acc.get_arity() == 3 ) {
        for( std::size_t i = 0; i < nColors; ++i ) {
            float* pData = (float*)acc.get_vertex( i );
            outColorData.set( (int)i, pData[0], pData[1], pData[2] );
            outColorFormat = MFnMesh::kRGB;
        }
    } else if( acc.get_arity() == 4 ) {
        for( std::size_t i = 0; i < nColors; ++i ) {
            float* pData = (float*)acc.get_vertex( i );
            outColorData.set( (int)i, pData[0], pData[1], pData[2], pData[3] );
            outColorFormat = MFnMesh::kRGBA;
        }
    } else {
        throw std::runtime_error( "channel \"" + frantic::strings::to_string( channel ) +
                                  "\" has unexpected arity. Should have arity of 1, 3, or 4." );
    }

    if( acc.has_custom_faces() ) {
        std::size_t nColorFaces = acc.face_count();
        if( nColorFaces != polymesh->face_count() ) {
            throw std::runtime_error( "channel \"" + frantic::strings::to_string( channel ) +
                                      "\" has unexpected count. Should match the \"faces\" channel" );
        }

        outColorIndices.setLength( 3 * (int)nColorFaces );
        outColorIndices.clear();

        for( std::size_t i = 0; i < mayaToPolymeshFace.size(); ++i ) {
            frantic::geometry::polymesh3_const_face_range r = acc.get_face( mayaToPolymeshFace[i] );
            for( ; r.first != r.second; ++r.first ) {
                outColorIndices.append( *r.first );
            }
        }
    } else {
        if( nColors != polymesh->vertex_count() ) {
            throw std::runtime_error( "channel \"" + frantic::strings::to_string( channel ) +
                                      "\" has unexpected count. Should match the \"verts\" channel" );
        }
        outColorIndices.clear();
    }
}

MStatus copyColors( MFnMesh& out, const MIntArray& polyIndices, const MIntArray& colorIndices,
                    const MColorArray& colorData, const MFnMesh::MColorRepresentation& outColorFormat,
                    MString* colorSet = NULL ) {
    MStatus stat;

    if( colorData.length() > 0 ) {
        if( colorSet ) {
            stat = out.createColorSetDataMesh( *colorSet );
            CHECK_MSTATUS_AND_RETURN_IT( stat );
        }
        stat = out.setColors( colorData, colorSet, outColorFormat );
        CHECK_MSTATUS_AND_RETURN_IT( stat );

        if( colorIndices.length() > 0 ) {
            stat = out.assignColors( colorIndices, colorSet );
        } else {
            stat = out.assignColors( polyIndices, colorSet );
        }
        CHECK_MSTATUS_AND_RETURN_IT( stat );
    }

    return stat;
}

MStatus assignXMeshColors( MObject& meshData, const frantic::geometry::const_polymesh3_ptr polymesh,
                           const std::vector<boost::uint32_t>& mayaToPolymeshFace, const MIntArray& polyIndices ) {
    MStatus stat;

    MFnMesh fnMesh( meshData, &stat );
    CHECK_MSTATUS_AND_RETURN_IT( stat );

    MIntArray colorIndices;
    MColorArray colorData;
    MFnMesh::MColorRepresentation colorFormat;

    if( polymesh->has_channel( _T("Color") ) ) {
        getXMeshColors( polymesh, _T("Color"), mayaToPolymeshFace, colorData, colorFormat, colorIndices );

        MString colorSetName( "color" );
        stat = copyColors( fnMesh, polyIndices, colorIndices, colorData, colorFormat, &colorSetName );
        CHECK_MSTATUS_AND_RETURN_IT( stat );
    }

    return MStatus::kSuccess;
}

void getEdgeCreases( const MObject& meshData, const frantic::geometry::const_polymesh3_ptr polymesh,
                     const frantic::tstring& channel, const std::vector<boost::uint32_t>& mayaToPolymeshFace,
                     MUintArray& edgeIDs, MDoubleArray& creaseData ) {
    polymesh3_const_cvt_vertex_accessor<float> acc = polymesh->get_const_cvt_vertex_accessor<float>( channel );

    std::vector<float> creaseDataVect;
    std::vector<int> edgeIDVect;

    std::size_t nFaces = acc.face_count();
    if( nFaces != polymesh->face_count() ) {
        throw std::runtime_error( "channel \"" + frantic::strings::to_string( channel ) +
                                  "\" has unexpected count. Should match the \"faces\" channel" );
    }

    std::set<int> addedEdgeIDs;

    MItMeshPolygon itPoly( meshData );

    for( std::size_t i = 0; i < mayaToPolymeshFace.size(); ++i ) {
        frantic::geometry::polymesh3_const_face_range f = acc.get_face( mayaToPolymeshFace[i] );

        MIntArray mayaEdges;
        itPoly.getEdges( mayaEdges );
        int curEdgeIndex = 0;

        for( frantic::geometry::polymesh3_const_face_iterator it = f.first, ie = f.second; it != ie; ++it ) {

            if( acc.get_vertex( *it ) > 0 ) {
                int edgeID = mayaEdges[curEdgeIndex];
                if( addedEdgeIDs.find( edgeID ) == addedEdgeIDs.end() ) {
                    edgeIDVect.push_back( edgeID );
                    addedEdgeIDs.insert( edgeID );

                    creaseDataVect.push_back( acc.get_vertex( *it ) );
                }
            }
            curEdgeIndex++;
        }
        if( !itPoly.isDone() ) {
            itPoly.next();
        }
    }

    edgeIDs.setLength( (int)edgeIDVect.size() );
    for( int i = 0; i < edgeIDVect.size(); ++i ) {
        edgeIDs[i] = edgeIDVect[i];
    }

    creaseData.setLength( (int)creaseDataVect.size() );
    for( int i = 0; i < creaseDataVect.size(); ++i ) {
        creaseData[i] = creaseDataVect[i];
    }
}

MStatus copyEdgeCreases( MFnMesh& out, const MUintArray& edgeIDs, const MDoubleArray& creaseData ) {
    MStatus stat;

    if( creaseData.length() > 0 && edgeIDs.length() > 0 ) {
        stat = out.setCreaseEdges( edgeIDs, creaseData );
        CHECK_MSTATUS_AND_RETURN_IT( stat );
    }
    return stat;
}

MStatus assignEdgeCreases( MObject& meshData, const frantic::geometry::const_polymesh3_ptr polymesh,
                           const std::vector<boost::uint32_t>& mayaToPolymeshFace ) {
    MStatus stat;

    MFnMesh fnMesh( meshData, &stat );
    CHECK_MSTATUS_AND_RETURN_IT( stat );

    MUintArray edgeIDs;
    MDoubleArray creaseData;

    if( polymesh->has_channel( _T("EdgeSharpness") ) ) {
        getEdgeCreases( meshData, polymesh, _T("EdgeSharpness"), mayaToPolymeshFace, edgeIDs, creaseData );

        stat = copyEdgeCreases( fnMesh, edgeIDs, creaseData );
        CHECK_MSTATUS_AND_RETURN_IT( stat );
    }
    return MStatus::kSuccess;
}

void getVertexCreases( const frantic::geometry::const_polymesh3_ptr polymesh, const frantic::tstring& channel,
                       MUintArray& vertexIDs, MDoubleArray& creaseData ) {
    polymesh3_const_cvt_vertex_accessor<float> acc = polymesh->get_const_cvt_vertex_accessor<float>( channel );
    std::size_t nVertices = acc.vertex_count();

    std::vector<float> creaseDataVect;
    std::vector<int> creaseIDVect;

    if( nVertices != polymesh->vertex_count() ) {
        throw std::runtime_error( "channel \"" + frantic::strings::to_string( channel ) +
                                  "\" has unexpected count. Should match the \"verts\" channel" );
    }

    for( int i = 0; i < nVertices; ++i ) {
        float creaseMagnitude = acc.get_vertex( i );
        if( creaseMagnitude > 0 ) {
            creaseDataVect.push_back( creaseMagnitude );
            creaseIDVect.push_back( i );
        }
    }

    creaseData.setLength( (int)creaseIDVect.size() );
    vertexIDs.setLength( (int)creaseIDVect.size() );

    for( int i = 0; i < creaseIDVect.size(); ++i ) {
        creaseData[i] = creaseDataVect[i];
        vertexIDs[i] = creaseIDVect[i];
    }
}

MStatus copyVertexCreases( MFnMesh& out, const MUintArray& vertexIDs, const MDoubleArray& creaseData ) {
    MStatus stat;

    if( creaseData.length() > 0 && vertexIDs.length() > 0 ) {
        stat = out.setCreaseVertices( vertexIDs, creaseData );
        CHECK_MSTATUS_AND_RETURN_IT( stat );
    }
    return stat;
}

MStatus assignVertexCreases( MObject& meshData, const frantic::geometry::const_polymesh3_ptr polymesh ) {
    MStatus stat;

    MFnMesh fnMesh( meshData, &stat );
    CHECK_MSTATUS_AND_RETURN_IT( stat );

    MUintArray vertexIDs;
    MDoubleArray creaseData;

    if( polymesh->has_channel( _T("VertexSharpness") ) ) {
        getVertexCreases( polymesh, _T("VertexSharpness"), vertexIDs, creaseData );

        stat = copyVertexCreases( fnMesh, vertexIDs, creaseData );
        CHECK_MSTATUS_AND_RETURN_IT( stat );
    }

    return MStatus::kSuccess;
}

void getXMeshVelocity( const frantic::geometry::const_polymesh3_ptr polymesh, const frantic::tstring& channel,
                       double fps, float timeDerivative, MColorArray& outVelocityData,
                       MFnMesh::MColorRepresentation& outVelocityFormat, MIntArray& outVelocityIndices ) {
    if( fps <= 0 ) {
        throw std::runtime_error( "fps has unexpected value. Should be greater than zero." );
    }

    frantic::geometry::polymesh3_const_cvt_vertex_accessor<frantic::graphics::vector3f> acc =
        polymesh->get_const_cvt_vertex_accessor<frantic::graphics::vector3f>( channel );

    std::size_t nVelocity = acc.vertex_count();
    outVelocityData.setLength( (int)nVelocity );

    if( polymesh->vertex_count() != acc.vertex_count() ) {
        throw std::runtime_error( "channel \"" + frantic::strings::to_string( channel ) +
                                  "\" has unexpected count. Should match the \"verts\" channel" );
    }
    outVelocityIndices.clear();

    float timeScale = static_cast<float>( 1 / fps );
    for( std::size_t i = 0; i < nVelocity; ++i ) {
        const frantic::graphics::vector3f vel =
            frantic::maya::graphics::to_maya_space( acc.get_vertex( i ) ) * timeScale * timeDerivative;
        outVelocityData.set( (int)i, vel.x, vel.y, vel.z );
    }
    outVelocityFormat = MFnMesh::kRGB;
}

MStatus assignXMeshVelocityColorSet( MObject& meshData, const frantic::geometry::const_polymesh3_ptr polymesh,
                                     const MIntArray& polyIndices, double fps, float timeDerivative ) {
    if( polymesh->has_channel( _T("Velocity") ) ) {
        MStatus stat;

        MFnMesh fnMesh( meshData, &stat );
        CHECK_MSTATUS_AND_RETURN_IT( stat );

        MIntArray velocityIndices;
        MColorArray velocityData;
        MFnMesh::MColorRepresentation velocityFormat;

        getXMeshVelocity( polymesh, _T("Velocity"), fps, timeDerivative, velocityData, velocityFormat,
                          velocityIndices );

        MString velocitySetName( "velocityPV" );
        stat = copyColors( fnMesh, polyIndices, velocityIndices, velocityData, velocityFormat, &velocitySetName );
        CHECK_MSTATUS_AND_RETURN_IT( stat );
    }

    return MStatus::kSuccess;
}

void getXMeshUVs( const frantic::geometry::const_polymesh3_ptr polymesh, const frantic::tstring& channel,
                  const std::vector<boost::uint32_t>& mayaToPolymeshFace, MFloatArray& outUData, MFloatArray& outVData,
                  MIntArray& outUVCounts, MIntArray& outUVIndices ) {
    frantic::geometry::polymesh3_const_vertex_accessor<frantic::graphics::vector3f> geomAcc =
        polymesh->get_const_vertex_accessor<frantic::graphics::vector3f>( _T("verts") );
    frantic::geometry::polymesh3_const_vertex_accessor<void> acc = polymesh->get_const_vertex_accessor( channel );

    if( acc.get_type() != frantic::channels::data_type_float32 || acc.get_arity() < 2 ) {
        throw std::runtime_error( "channel \"" + frantic::strings::to_string( channel ) +
                                  "\" has unexpected type. Should be float32[2]" );
    }

    std::size_t nUVs = acc.vertex_count();
    outUData.setLength( (int)nUVs );
    outVData.setLength( (int)nUVs );

    for( std::size_t i = 0; i < nUVs; ++i ) {
        float* pData = (float*)acc.get_vertex( i );
        outUData[(int)i] = pData[0];
        outVData[(int)i] = pData[1];
    }

    if( acc.has_custom_faces() ) {
        std::size_t nUVFaces = acc.face_count();
        if( nUVFaces != polymesh->face_count() ) {
            throw std::runtime_error( "channel \"" + frantic::strings::to_string( channel ) +
                                      "\" has unexpected count. Should match the \"faces\" channel" );
        }

        outUVCounts.setLength( (int)nUVFaces );
        outUVCounts.clear();
        outUVIndices.setLength( 3 * (int)nUVFaces );
        outUVIndices.clear();

        for( std::size_t i = 0; i < mayaToPolymeshFace.size(); ++i ) {
            frantic::geometry::polymesh3_const_face_range r = acc.get_face( mayaToPolymeshFace[i] );
            outUVCounts.append( (int)( r.second - r.first ) );
            for( ; r.first != r.second; ++r.first ) {
                outUVIndices.append( *r.first );
            }
        }
    } else {
        if( nUVs != polymesh->vertex_count() ) {
            throw std::runtime_error( "channel \"" + frantic::strings::to_string( channel ) +
                                      "\" has unexpected count. Should match the \"verts\" channel" );
        }
        outUVCounts.clear();
        outUVIndices.clear();
    }
}

MStatus copyUVs( MFnMesh& out, const MIntArray& polyCounts, const MIntArray& polyIndices, const MIntArray& uvCounts,
                 const MIntArray& uvIndices, const MFloatArray& uData, const MFloatArray& vData,
                 MString* uvSet = NULL ) {
    MStatus stat;

    // Temporary name for the uvSet.
    // createUVSetDataMeshWithName() can return a new name if the desired name is already used.
    MString uvSetName;

    // Don't create the channel if the data is empty.
    // TODO: Such files (with TextureCoord vertex count == 0, face count != 0)
    // are apparently being saved by out Maya XMesh Saver.  Why? That seems wrong to me.
    if( uData.length() > 0 && vData.length() > 0 ) {
        if( uvSet ) {
            uvSetName = out.createUVSetDataMeshWithName( *uvSet, &stat );
            CHECK_MSTATUS_AND_RETURN_IT( stat );
            uvSet = &uvSetName;
        }

        stat = out.setUVs( uData, vData, uvSet );
        CHECK_MSTATUS_AND_RETURN_IT( stat );

        if( uvCounts.length() > 0 ) {
            stat = out.assignUVs( uvCounts, uvIndices, uvSet );
        } else {
            stat = out.assignUVs( polyCounts, polyIndices, uvSet );
        }
        CHECK_MSTATUS_AND_RETURN_IT( stat );
    }

    return stat;
}

MStatus assignXMeshUVs( MObject& meshData, const frantic::geometry::const_polymesh3_ptr polymesh,
                        const std::vector<boost::uint32_t>& mayaToPolymeshFace, const MIntArray& polyCounts,
                        const MIntArray& polyIndices ) {
    MStatus stat;

    MFnMesh fnMesh( meshData, &stat );
    CHECK_MSTATUS_AND_RETURN_IT( stat );

    MFloatArray uData;
    MFloatArray vData;
    MIntArray uvCounts;
    MIntArray uvIndices;

    // Handle TextureCoord individually, sticking it in map1
    if( polymesh->has_channel( _T("TextureCoord") ) ) {
        getXMeshUVs( polymesh, _T("TextureCoord"), mayaToPolymeshFace, uData, vData, uvCounts, uvIndices );

        stat = copyUVs( fnMesh, polyCounts, polyIndices, uvCounts, uvIndices, uData, vData );
        CHECK_MSTATUS_AND_RETURN_IT( stat );
    }

    // Iterate over each vertex channel. For now, only add channels called Mapping##.
    // They probably have custom faces, so we need to build that array as well.
    for( frantic::geometry::polymesh3::iterator it = polymesh->begin(), itEnd = polymesh->end(); it != itEnd; ++it ) {
        if( !boost::algorithm::starts_with( it->first, _T("Mapping") ) ) {
            continue;
        }

        getXMeshUVs( polymesh, it->first, mayaToPolymeshFace, uData, vData, uvCounts, uvIndices );

        MString uvSetName = MString( "map" ) + MString( it->first.substr( 7 ).c_str() );

        stat = copyUVs( fnMesh, polyCounts, polyIndices, uvCounts, uvIndices, uData, vData, &uvSetName );
        CHECK_MSTATUS_AND_RETURN_IT( stat );
    }

    return MStatus::kSuccess;
}

boost::optional<int> try_get_constant_smoothing_group( frantic::geometry::const_polymesh3_ptr polymesh ) {
    if( !polymesh || !polymesh->has_face_channel( _T("SmoothingGroup") ) ) {
        return boost::optional<int>();
    }

    frantic::geometry::polymesh3_const_face_accessor<int> smAcc =
        polymesh->get_const_face_accessor<int>( _T("SmoothingGroup") );

    if( smAcc.face_count() == 0 ) {
        return boost::optional<int>();
    }

    int value = smAcc.get_face( 0 );

    for( std::size_t i = 0, ie = smAcc.face_count(); i < ie; ++i ) {
        if( smAcc.get_face( i ) != value ) {
            return boost::optional<int>();
        }
    }

    return boost::optional<int>( value );
}

MStatus assignEdgeSmoothing( MObject& meshObj, frantic::geometry::const_polymesh3_ptr polymesh,
                             const std::vector<boost::uint32_t>& mayaToPolymeshFace ) {
    if( !polymesh->has_face_channel( _T("SmoothingGroup") ) ) {
        return MStatus::kSuccess;
    }

    MStatus stat;
    MFnMesh fnMesh( meshObj, &stat );
    CHECK_MSTATUS_AND_RETURN_IT( stat );

    const int faceCount = fnMesh.numPolygons();
    const int edgeCount = fnMesh.numEdges();

    boost::optional<int> constantSmoothingGroup = try_get_constant_smoothing_group( polymesh );
    if( constantSmoothingGroup ) {
        const bool smooth = ( *constantSmoothingGroup ) != 0;
        for( int edgeIndex = 0; edgeIndex < edgeCount; ++edgeIndex ) {
            fnMesh.setEdgeSmoothing( edgeIndex, smooth );
        }
    } else {
        frantic::geometry::polymesh3_const_face_accessor<int> smAcc =
            polymesh->get_const_face_accessor<int>( _T("SmoothingGroup") );

        vertices_to_edge_map verticesToEdgeMap( meshObj );

        std::vector<boost::int32_t> edgeSmoothing( edgeCount, 0xffffffff );

        MIntArray mayaCounts;
        MIntArray mayaIndices;
        fnMesh.getVertices( mayaCounts, mayaIndices );

        unsigned int offset = 0;
        for( int faceIndex = 0; faceIndex < faceCount; ++faceIndex ) {
            for( int corner = 0, cornerEnd = mayaCounts[faceIndex]; corner < cornerEnd; ++corner ) {
                int nextCorner = corner + 1;
                if( nextCorner == cornerEnd ) {
                    nextCorner = 0;
                }
                int edgeIndex;
                if( verticesToEdgeMap.get_edge(
                        std::pair<int, int>( mayaIndices[offset + corner], mayaIndices[offset + nextCorner] ),
                        edgeIndex ) ) {
                    edgeSmoothing[edgeIndex] &= smAcc.get_face( mayaToPolymeshFace[faceIndex] );
                }
            }
            offset += mayaCounts[faceIndex];
        }

        for( int edgeIndex = 0; edgeIndex < edgeCount; ++edgeIndex ) {
            fnMesh.setEdgeSmoothing( edgeIndex, edgeSmoothing[edgeIndex] != 0 );
        }
    }

    fnMesh.updateSurface();

    return MStatus::kSuccess;
}

std::size_t get_face_vertex_count( const frantic::geometry::const_polymesh3_ptr polymesh,
                                   const std::vector<boost::uint32_t>& mayaToPolymeshFace ) {
    frantic::geometry::polymesh3_const_vertex_accessor<frantic::graphics::vector3f> geomAcc =
        polymesh->get_const_vertex_accessor<frantic::graphics::vector3f>( _T("verts") );

    std::size_t sum = 0;

    BOOST_FOREACH( boost::uint32_t faceIndex, mayaToPolymeshFace ) {
        sum += geomAcc.get_face_degree( faceIndex );
    }

    return sum;
}

template <class T>
void reserve( T& v, unsigned int length ) {
    v.setLength( length );
    v.setLength( 0 );
}

// TODO: could change to assign per-vertex normals from custom vertex channel
// when all faces use the same index
void getXMeshNormals( frantic::geometry::const_polymesh3_ptr polymesh, const frantic::tstring& normalChannelName,
                      const std::vector<boost::uint32_t>& mayaToPolymeshFace, MVectorArray& outNormals,
                      MIntArray& outVertexList, MVectorArray& outFaceVertexNormals, MIntArray& outFaceVertexFaceList,
                      MIntArray& outFaceVertexVertexList ) {
    frantic::geometry::polymesh3_const_vertex_accessor<frantic::graphics::vector3f> geomAcc =
        polymesh->get_const_vertex_accessor<frantic::graphics::vector3f>( _T("verts") );
    frantic::geometry::polymesh3_const_cvt_vertex_accessor<frantic::graphics::vector3f> acc =
        polymesh->get_const_cvt_vertex_accessor<frantic::graphics::vector3f>( normalChannelName );

    outNormals.setLength( 0 );
    outVertexList.setLength( 0 );
    outFaceVertexNormals.setLength( 0 );
    outFaceVertexFaceList.setLength( 0 );
    outFaceVertexVertexList.setLength( 0 );

    if( acc.has_custom_faces() ) {
        const unsigned int faceVertexCount =
            static_cast<unsigned int>( get_face_vertex_count( polymesh, mayaToPolymeshFace ) );
        reserve( outFaceVertexNormals, faceVertexCount );
        reserve( outFaceVertexFaceList, faceVertexCount );
        reserve( outFaceVertexVertexList, faceVertexCount );

        // copy normals
        BOOST_FOREACH( const boost::uint32_t faceIndex, mayaToPolymeshFace ) {
            frantic::geometry::polymesh3_const_face_range faceRange = acc.get_face( faceIndex );
            for( frantic::geometry::polymesh3_const_face_iterator vertexIndex = faceRange.first;
                 vertexIndex != faceRange.second; ++vertexIndex ) {
                const frantic::graphics::vector3f normal =
                    frantic::maya::graphics::to_maya_space( acc.get_vertex( *vertexIndex ) );
                outFaceVertexNormals.append( frantic::maya::to_maya_t( normal ) );
            }
        }

        // copy face-vertex indices
        for( boost::uint32_t mayaFaceIndex = 0,
                             mayaFaceIndexEnd = static_cast<boost::uint32_t>( mayaToPolymeshFace.size() );
             mayaFaceIndex < mayaFaceIndexEnd; ++mayaFaceIndex ) {
            const boost::uint32_t polymeshFaceIndex = mayaToPolymeshFace[mayaFaceIndex];

            frantic::geometry::polymesh3_const_face_range faceRange = geomAcc.get_face( polymeshFaceIndex );
            for( frantic::geometry::polymesh3_const_face_iterator vertexIndex = faceRange.first;
                 vertexIndex != faceRange.second; ++vertexIndex ) {
                outFaceVertexFaceList.append( mayaFaceIndex );
                outFaceVertexVertexList.append( *vertexIndex );
            }
        }
    } else {
        const unsigned int vertexCount = static_cast<unsigned int>( geomAcc.vertex_count() );

        outNormals.setLength( vertexCount );
        outVertexList.setLength( vertexCount );

        for( unsigned int i = 0; i < vertexCount; ++i ) {
            outNormals[i] = frantic::maya::to_maya_t( frantic::maya::graphics::to_maya_space( acc.get_vertex( i ) ) );
            outVertexList[i] = i;
        }
    }
}

MStatus copyNormals( MFnMesh& fnMesh, MVectorArray& normals, MIntArray& vertexList, MVectorArray& faceVertexNormals,
                     MIntArray& faceVertexFaceList, MIntArray& faceVertexVertexList ) {
    MStatus stat;

    if( normals.length() > 0 ) {
        stat = fnMesh.setVertexNormals( normals, vertexList );
        CHECK_MSTATUS_AND_RETURN_IT( stat );
    }

    if( faceVertexNormals.length() > 0 ) {
        stat = fnMesh.setFaceVertexNormals( faceVertexNormals, faceVertexFaceList, faceVertexVertexList );
        CHECK_MSTATUS_AND_RETURN_IT( stat );
    }

    return stat;
}

MStatus assignNormals( MObject& meshData, frantic::geometry::const_polymesh3_ptr polymesh,
                       const std::vector<boost::uint32_t>& mayaToPolymeshFace ) {
    MStatus stat;

    MFnMesh fnMesh( meshData, &stat );
    CHECK_MSTATUS_AND_RETURN_IT( stat );

    const frantic::tstring normalChannelName( _T("Normal") );

    if( polymesh->has_vertex_channel( normalChannelName ) ) {
        MVectorArray normals;
        MIntArray vertexList;

        MVectorArray faceVertexNormals;
        MIntArray faceVertexVertexList;
        MIntArray faceVertexFaceList;

        getXMeshNormals( polymesh, normalChannelName, mayaToPolymeshFace, normals, vertexList, faceVertexNormals,
                         faceVertexFaceList, faceVertexVertexList );

        stat = copyNormals( fnMesh, normals, vertexList, faceVertexNormals, faceVertexFaceList, faceVertexVertexList );
        CHECK_MSTATUS_AND_RETURN_IT( stat );
    }

    return stat;
}

MStatus assignMaterialIDs( MFnMeshData& fnMeshData, frantic::geometry::const_polymesh3_ptr polymesh,
                           const std::vector<boost::uint32_t>& mayaToPolymeshFace, MArrayDataHandle inGroupIdsData ) {
    MStatus stat = MStatus::kSuccess;

    if( polymesh->has_face_channel( _T("MaterialID") ) ) {
        const unsigned int groupIdCount = inGroupIdsData.elementCount( &stat );
        CHECK_MSTATUS_AND_RETURN_IT( stat );
        if( groupIdCount > 0 ) {
            frantic::geometry::polymesh3_const_cvt_face_accessor<int> matIDAcc =
                polymesh->get_const_cvt_face_accessor<int>( _T("MaterialID") );

            // Build the MaterialID to GroupID mapping.

            // inGroupIdsData has a sparse index, while matIDToGroupID
            // has a dense index.  I'm using matIDAlive to keep track of which
            // indices are uses, so that the output objectGroups[] has the same
            // entries as the input inGroupIdsData[].  This may be unnecessary.
            std::vector<char> matIDAlive;
            std::vector<unsigned int> matIDToGroupID;
            const unsigned int matIDLimit = 65535;
            bool doneLimitWarning = false;

            for( unsigned int i = 0; i < groupIdCount; ++i ) {
                inGroupIdsData.jumpToArrayElement( i );
                unsigned int matID = inGroupIdsData.elementIndex( &stat );
                if( matID > matIDLimit ) {
                    matID = matIDLimit;
                    if( !doneLimitWarning ) {
                        MGlobal::displayWarning(
                            ( "Clamped MaterialID to " + boost::lexical_cast<std::string>( matIDLimit ) ).c_str() );
                        doneLimitWarning = true;
                    }
                }
                if( matID >= matIDToGroupID.size() ) {
                    matIDToGroupID.resize( matID + 1 );
                    matIDAlive.resize( matID + 1 );
                }

                MDataHandle groupIdData = inGroupIdsData.inputValue( &stat );
                CHECK_MSTATUS_AND_RETURN_IT( stat );

                int groupId = groupIdData.asInt();
                if( groupId == 0 ) {
                    throw std::runtime_error( "groupId is zero" );
                } else if( groupId < 0 ) {
                    throw std::runtime_error( "groupId is negative" );
                }

                matIDToGroupID[matID] = static_cast<unsigned int>( groupId );
                matIDAlive[matID] = 1;
            }

            if( matIDToGroupID.size() > 0 ) {
                // find the faces that belong to each MaterialID
                std::vector<MIntArray> matIDFaces( matIDToGroupID.size() );

                for( std::size_t faceIndex = 0; faceIndex < mayaToPolymeshFace.size(); ++faceIndex ) {
                    const int matId = matIDAcc.get_face( mayaToPolymeshFace[faceIndex] );
                    if( matId < 0 ) {
                        throw std::runtime_error( "MaterialID is negative" );
                    }

                    if( matId < matIDFaces.size() ) {
                        matIDFaces[matId].append( static_cast<int>( faceIndex ) );
                    }
                }

                // create an object group for each MaterialID
                for( std::size_t i = 0; i < matIDToGroupID.size(); ++i ) {
                    if( matIDAlive[i] ) {
                        MFnSingleIndexedComponent compFn;
                        MObject faceComp = compFn.create( MFn::kMeshPolygonComponent );
                        compFn.addElements( matIDFaces[i] );
                        // compFn.setCompleteData( matIDFaces[i].length() );

                        const unsigned int groupID = matIDToGroupID[i];
                        if( groupID > 0 ) {
                            fnMeshData.addObjectGroup( groupID );
                            fnMeshData.addObjectGroupComponent( groupID, faceComp );
                        }
                    }
                }
            }
        }
    }

    return stat;
}

MStatus polymesh_copy( MObject& meshDataBlock, const_polymesh3_ptr mesh, MArrayDataHandle& inGroupIdsData,
                       float timeOffset = 0.f, float faceFraction = 1.f, double fps = 0, float timeDerivative = 1.0 ) {
    using frantic::graphics::vector3f;

    MStatus stat;

    if( !mesh ) {
        throw std::runtime_error( "polymesh_copy Error: mesh is NULL" );
    }

    // This vector will hold a sorted list of faces which need to be skipped since they are invalid.
    // This is neccessary, since Maya will discard weird faces (ie. Triangles with two shared verts),
    // and we need to know which faces to discard when assigning UVs.
    //  Note: for now I'm using a mayaToPolymeshFace mapping instead because it seems to make things
    //  easier.
    // std::vector<std::size_t> skipFaces;

    MFloatPointArray meshPts;
    MIntArray polyCounts;
    MIntArray polyIndices;

    // Reserve the correct amount of space in each array
    meshPts.setLength( (int)mesh->vertex_count() );
    meshPts.clear();

    polyCounts.setLength( (int)mesh->face_count() );
    polyCounts.clear();

    // This call takes a guess at the number of indices used by all faces in the mesh.
    // It makes a conservative guess that every face is a triangle.
    polyIndices.setLength( 3 * (int)mesh->face_count() );
    polyIndices.clear();

    polymesh3_const_vertex_accessor<vector3f> geomAcc = mesh->get_const_vertex_accessor<vector3f>( _T("verts") );

    // Swap the vertices into y-up
    if( mesh->has_channel( _T("Velocity") ) && std::abs( timeOffset ) > 1e-5f ) {
        polymesh3_const_vertex_accessor<vector3f> velAcc = mesh->get_const_vertex_accessor<vector3f>( _T("Velocity") );
        // std::cout << "   has velocity" << std::endl;

        if( geomAcc.vertex_count() != velAcc.vertex_count() ) {
            throw std::runtime_error( "channel \"Velocity\" has unexpected count. Should match the \"verts\" channel" );
        }

        for( std::size_t i = 0, iEnd = geomAcc.vertex_count(); i < iEnd; ++i ) {
            vector3f p =
                frantic::maya::graphics::to_maya_space( geomAcc.get_vertex( i ) + timeOffset * velAcc.get_vertex( i ) );
            meshPts.append( p.x, p.y, p.z );
        }
    } else {
        for( std::size_t i = 0, iEnd = geomAcc.vertex_count(); i < iEnd; ++i ) {
            vector3f p = frantic::maya::graphics::to_maya_space( geomAcc.get_vertex( i ) );
            meshPts.append( p.x, p.y, p.z );
        }
    }

    for( fractional_index_iterator i( geomAcc.face_count(), faceFraction ), ie; i != ie; ++i ) {
        polymesh3_const_face_range r = geomAcc.get_face( *i );
        for( polymesh3_const_face_iterator it = r.first; it != r.second; ++it ) {
            polyIndices.append( *it );
        }
        polyCounts.append( (int)( r.second - r.first ) );
    }

    MFnMesh fnMesh;
    MFnMeshData fnMeshData;

    meshDataBlock = fnMeshData.create( &stat );
    CHECK_MSTATUS_AND_RETURN_IT( stat );

    // Prevent Maya from deleting faces on me.
    fnMesh.setCheckSamePointTwice( false );

    MObject meshData =
        fnMesh.create( meshPts.length(), polyCounts.length(), meshPts, polyCounts, polyIndices, meshDataBlock, &stat );
    CHECK_MSTATUS_AND_RETURN( stat, MStatus::kFailure );

    try {
        // Use a Maya to XMesh face index mapping to handle faces that are discarded by Maya.
        // TODO: Handle discarded and added verts too
        std::vector<boost::uint32_t> mayaToPolymeshFace;

        mayaToPolymeshFace.reserve( geomAcc.face_count() );
        for( fractional_index_iterator i( geomAcc.face_count(), faceFraction ), ie; i != ie; ++i ) {
            polymesh3_const_face_range f = geomAcc.get_face( *i );
            if( f.second > f.first && *f.first != *( f.second - 1 ) ) {
                mayaToPolymeshFace.push_back( static_cast<boost::uint32_t>( *i ) );
            }
        }

        const int numPolygons = fnMesh.numPolygons( &stat );
        CHECK_MSTATUS_AND_RETURN( stat, MStatus::kFailure );
        if( numPolygons != static_cast<int>( mayaToPolymeshFace.size() ) ) {
            throw std::runtime_error( "Mismatch between real and expected polygon count in Maya mesh (" +
                                      boost::lexical_cast<std::string>( numPolygons ) + " vs " +
                                      boost::lexical_cast<std::string>( mayaToPolymeshFace.size() ) + ")" );
        }

        stat = assignXMeshUVs( meshData, mesh, mayaToPolymeshFace, polyCounts, polyIndices );
        CHECK_MSTATUS_AND_RETURN_IT( stat );

        stat = assignXMeshColors( meshData, mesh, mayaToPolymeshFace, polyIndices );
        CHECK_MSTATUS_AND_RETURN_IT( stat );

        stat = assignEdgeCreases( meshData, mesh, mayaToPolymeshFace );
        CHECK_MSTATUS_AND_RETURN_IT( stat );

        stat = assignVertexCreases( meshData, mesh );
        CHECK_MSTATUS_AND_RETURN_IT( stat );

        stat = assignXMeshVelocityColorSet( meshData, mesh, polyIndices, fps, timeDerivative );
        CHECK_MSTATUS_AND_RETURN_IT( stat );

        stat = assignEdgeSmoothing( meshData, mesh, mayaToPolymeshFace );
        CHECK_MSTATUS_AND_RETURN_IT( stat );

        stat = assignNormals( meshData, mesh, mayaToPolymeshFace );
        CHECK_MSTATUS_AND_RETURN_IT( stat );

        stat = assignMaterialIDs( fnMeshData, mesh, mayaToPolymeshFace, inGroupIdsData );
        CHECK_MSTATUS_AND_RETURN_IT( stat );
    } catch( const std::exception& e ) {
        MGlobal::displayError( e.what() );
        stat = MStatus::kFailure;
    }

    CHECK_MSTATUS_AND_RETURN_IT( stat );

    return stat;
}

MStatus CreateEmptyMesh( MDataHandle& outData ) {
    MStatus stat;
    MFnMesh fnMesh;
    MFnMeshData fnMeshData;

    MObject meshDataBlock = fnMeshData.create( &stat );
    CHECK_MSTATUS_AND_RETURN_IT( stat );

    MObject meshData = fnMesh.create( 0, 0, MFloatPointArray(), MIntArray(), MIntArray(), meshDataBlock, &stat );
    CHECK_MSTATUS_AND_RETURN_IT( stat );

    outData.set( meshDataBlock );

    return MStatus::kSuccess;
}

boost::shared_ptr<frantic::geometry::trimesh3> BuildIconMesh() {
    boost::shared_ptr<frantic::geometry::trimesh3> result = boost::make_shared<frantic::geometry::trimesh3>();
    BuildMesh_XMeshLogoMesh( *result );
    return result;
}

void gl_draw_verts( frantic::geometry::const_polymesh3_ptr mesh, float drawFraction ) {
    if( !mesh ) {
        throw std::runtime_error( "gl_draw_verts Error: mesh is NULL" );
    }

    glPushAttrib( GL_CURRENT_BIT );
    glPushClientAttrib( GL_CLIENT_VERTEX_ARRAY_BIT );

    glEnableClientState( GL_VERTEX_ARRAY );

    glBegin( GL_POINTS );

    frantic::geometry::polymesh3_const_vertex_accessor<frantic::graphics::vector3f> geomAcc =
        mesh->get_const_vertex_accessor<frantic::graphics::vector3f>( _T("verts") );

    const std::size_t vertexCount = mesh->vertex_count();
    for( fractional_index_iterator i( vertexCount, drawFraction ), ie; i != ie; ++i ) {
        const vector3f p = frantic::maya::graphics::to_maya_space( geomAcc.get_vertex( *i ) );
        glVertex3f( p.x, p.y, p.z );
    }

    glEnd();

    glPopClientAttrib();
    glPopAttrib();
}

} // anonymous namespace

boost::shared_ptr<frantic::geometry::trimesh3> SequenceXMeshNode::g_iconMesh = BuildIconMesh();

frantic::tstring SequenceXMeshNode::get_auto_proxy_path() {
    MPlug plug( thisMObject(), SequenceXMeshNode::seqPath );

    const frantic::tstring renderPath( frantic::strings::to_tstring( plug.asString().asUTF8() ) ); // get_render_path();
    if( renderPath.empty() ) {
        return _T("");
    }
    const frantic::tstring proxyFilenameInRenderDirectory =
        frantic::files::filename_pattern::add_before_sequence_number( renderPath, _T("_proxy") );
    const frantic::tstring proxyFilename = frantic::files::filename_from_path( proxyFilenameInRenderDirectory );
    return frantic::files::to_tstring( boost::filesystem::path( get_auto_proxy_directory() ) / proxyFilename );
}

frantic::tstring SequenceXMeshNode::get_auto_proxy_directory() {
    MPlug plug( thisMObject(), SequenceXMeshNode::seqPath );
    const frantic::tstring renderPath( frantic::strings::to_tstring( plug.asString().asUTF8() ) ); // get_render_path();
    if( renderPath.empty() ) {
        return _T("");
    }

    frantic::files::filename_sequence renderSequence( renderPath );
    const frantic::tstring renderPrefix = renderSequence.get_filename_pattern().get_prefix();
    const frantic::tstring proxyDirPrefix =
        renderPrefix + /*( boost::algorithm::iends_with( renderPrefix, "_" ) ? "" : "_" ) +*/ _T("_proxy");
    return renderSequence.get_filename_pattern().get_directory( true ) + proxyDirPrefix;
}

void SequenceXMeshNode::check_auto_proxy_path() {
    const frantic::tstring proxyPath = get_auto_proxy_path();

    if( proxyPath.empty() ) {
        return;
    }

    const frantic::tstring proxyDir = frantic::files::directory_from_path( proxyPath );

    if( !frantic::files::directory_exists( proxyDir ) ) {
        const frantic::tstring msg = _T("Missing automatic proxy path:\n\n") + proxyPath;
        std::cout << "XMESHLOADER: " << frantic::strings::to_string( msg ) << std::endl;
        return;
    }

    bool hasSequence = false;
    try {
        frantic::files::filename_sequence seq( proxyPath );
        seq.sync_frame_set();
        hasSequence = !seq.get_frame_set().empty();
    } catch( std::exception& ) {
        //
    }

    if( !hasSequence ) {
        if( !frantic::files::file_exists( proxyPath ) ) {
            const frantic::tstring msg = _T("Missing automatic proxy path:\n\n") + proxyPath;
            std::cout << "XMESHLOADER: " << frantic::strings::to_string( msg ) << std::endl;
            return;
        }
    }
}

clamp_mode SequenceXMeshNode::get_start_clamp_mode() const {
    return (clamp_mode)MPlug( thisMObject(), inCustomRangeStartClampMode ).asInt();
}

clamp_mode SequenceXMeshNode::get_end_clamp_mode() const {
    return (clamp_mode)MPlug( thisMObject(), inCustomRangeEndClampMode ).asInt();
}

frantic::tstring SequenceXMeshNode::get_sequence_path( seq_ID seqId ) {
    if( seqId == SEQ_PROXY ) {
        if( MPlug( thisMObject(), inAutoProxyPath ).asBool() ) {
            return get_auto_proxy_path();
        } else {
            return get_proxy_path();
        }
    } else {
        return get_render_path();
    }
}

display_mode SequenceXMeshNode::get_display_mode() {
    return static_cast<display_mode>( MPlug( thisMObject(), inDisplayMode ).asInt() );
}

display_mode SequenceXMeshNode::get_effective_display_mode() {
    bool inRenderingMode = MPlug( thisMObject(), inRender ).asBool();

    return inRenderingMode ? DISPLAY_MODE_MESH
                           : static_cast<display_mode>( MPlug( thisMObject(), inDisplayMode ).asInt() );
}

float SequenceXMeshNode::get_display_fraction() {
    return frantic::math::clamp<float>(
        static_cast<float>( MPlug( thisMObject(), inDisplayPercent ).asDouble() / 100.0 ), 0.f, 1.f );
}

frantic::tstring SequenceXMeshNode::get_render_path() {
    return frantic::maya::from_maya_t( MPlug( thisMObject(), seqPath ).asString() );
}

frantic::tstring SequenceXMeshNode::get_proxy_path() {
    return frantic::maya::from_maya_t( MPlug( thisMObject(), seqProxyPath ).asString() );
}

void SequenceXMeshNode::load_mesh_at_frame( seq_ID seqId, double frame, int loadMask ) {
    frantic::files::filename_sequence& sequence = get_sequence( seqId );

    if( !sequence.get_frame_set().empty() && sequence.get_frame_set().frame_exists( frame ) ) {
        // If the cache wasn't empty, check to make sure the file is in the cache, and load it
        m_cachedPolymesh3.reset();
        m_metadata.clear();
        m_cachedPolymesh3 = m_polymesh3Loader.load( sequence[frame], &m_metadata, loadMask );
    } else {
        // The file requested isn't there.
        throw std::runtime_error( "SequenceXMeshNode::load_mesh_at_frame: File '" +
                                  frantic::strings::to_string( sequence[frame] ) + "' requested does not exist." );
    }
}

void SequenceXMeshNode::load_mesh_interval( seq_ID seqId, std::pair<double, double> interval, int loadMask ) {
    frantic::files::filename_sequence& sequence = get_sequence( seqId );

    // check for file existence
    if( !sequence.get_frame_set().frame_exists( interval.first ) ) {
        throw std::runtime_error( "SequenceXMeshNode::load_mesh_interpolated: Frame " +
                                  boost::lexical_cast<std::string>( interval.first ) +
                                  " does not exist in the selected sequence." );
    } else if( !sequence.get_frame_set().frame_exists( interval.second ) ) {
        throw std::runtime_error( "SequenceXMeshNode::load_mesh_interpolated: Frame " +
                                  boost::lexical_cast<std::string>( interval.second ) +
                                  " does not exist in the selected sequence." );
    } else {
        // Load the meshes
        const bool loadMetadataFromFirst = true; // alpha < 0.5f;
        m_metadata.clear();
        m_cachedPolymesh3Interval.first.reset();
        m_cachedPolymesh3Interval.first = m_polymesh3Loader.load(
            sequence[interval.first], (xmesh_metadata*)( loadMetadataFromFirst ? &m_metadata : 0 ), loadMask );

        m_cachedPolymesh3Interval.second.reset();
        m_cachedPolymesh3Interval.second = m_polymesh3Loader.load(
            sequence[interval.second], (xmesh_metadata*)( loadMetadataFromFirst ? 0 : &m_metadata ), loadMask );
    }
}

frantic::files::filename_sequence& SequenceXMeshNode::get_sequence( seq_ID seqId, bool throwIfMissing ) {
    const frantic::tstring seqPath = get_sequence_path( seqId );

    frantic::tstring& cachedSeqPath =
        ( seqId == SEQ_RENDER ? m_cachedFilenameSequencePath : m_cachedProxyFilenameSequencePath );
    frantic::files::filename_sequence& seq =
        ( seqId == SEQ_RENDER ? m_cachedFilenameSequence : m_cachedProxyFilenameSequence );

    if( cachedSeqPath != seqPath ) {
        cachedSeqPath.clear();
        if( seqPath.empty() ) {
            seq = frantic::files::filename_sequence();
        } else {
            seq = frantic::files::filename_sequence( seqPath );
            bool success = false;
            std::string errorMessage;
            try {
                seq.sync_frame_set();
                success = true;
            } catch( std::exception& e ) {
                errorMessage = e.what();
            }
            if( !success ) {
                seq.get_frame_set().clear();
                if( throwIfMissing ) {
                    throw std::runtime_error( errorMessage + "\nPath: " + frantic::strings::to_string( seqPath ) );
                }
            }
        }
        cachedSeqPath = seqPath;
    }
    return seq;
}

void SequenceXMeshNode::cache_bounding_box() {
    using frantic::graphics::vector3f;

    m_meshBoundingBox.set_to_empty();

    if( get_effective_display_mode() == DISPLAY_MODE_BOX && m_metadata.has_boundbox() ) {
        const frantic::graphics::boundbox3f bbox( m_metadata.get_boundbox() );
        if( !bbox.is_empty() ) {
            m_meshBoundingBox += frantic::maya::graphics::ToMayaSpace * bbox.minimum();
            m_meshBoundingBox += frantic::maya::graphics::ToMayaSpace * bbox.maximum();
        }
    } else if( m_cachedPolymesh3 ) {
        polymesh3_const_vertex_accessor<vector3f> geomAcc =
            m_cachedPolymesh3->get_const_vertex_accessor<vector3f>( _T("verts") );

        for( std::size_t i = 0, iEnd = geomAcc.vertex_count(); i < iEnd; ++i ) {
            const vector3f p = frantic::maya::graphics::to_maya_space( geomAcc.get_vertex( i ) );
            m_meshBoundingBox += vector3f( p.x, p.y, p.z );
        }
    }

    m_boundingBox.clear();

    // make sure the bounding box includes the root object as well
    m_boundingBox.expand( MPoint( -1, -1, -1 ) );
    m_boundingBox.expand( MPoint( 1, 1, 1 ) );

    if( !m_meshBoundingBox.is_empty() ) {
        m_boundingBox.expand( frantic::maya::to_maya_t( m_meshBoundingBox ) );
    }
}

const frantic::graphics::boundbox3f& SequenceXMeshNode::get_mesh_bounding_box() { return m_meshBoundingBox; }

SequenceXMeshNode::SequenceXMeshNode() { cache_bounding_box(); }

SequenceXMeshNode::~SequenceXMeshNode() {
    if( m_computeRenderCallbackId ) {
        MSceneMessage::removeCallback( m_computeRenderCallbackId );
    }
    if( m_computeViewportCallbackId ) {
        MSceneMessage::removeCallback( m_computeViewportCallbackId );
    }
}

void SequenceXMeshNode::postConstructor() {
    MStatus result = MStatus::kSuccess;

    m_computeRenderCallbackId = 0;
    m_computeViewportCallbackId = 0;

    m_computeRenderCallbackId =
        MSceneMessage::addCallback( MSceneMessage::kBeforeSoftwareRender, computeRender, (void*)this, &result );
    m_computeViewportCallbackId =
        MSceneMessage::addCallback( MSceneMessage::kAfterSoftwareRender, computeViewport, (void*)this, &result );
}

void* SequenceXMeshNode::creator() {
    //#if defined(__TIMESTAMP__)
    //	std::cout << "XMESH compiled:" << __TIMESTAMP__ << std::endl; //remove from release build
    //#endif
    return new SequenceXMeshNode;
}

MStatus SequenceXMeshNode::initialize() {
    MStatus result = MStatus::kSuccess;

    MFnTypedAttribute tAttr;
    seqPath = tAttr.create( "seqPath", "path", MFnData::kString, MObject::kNullObj );
    tAttr.setInternal( true ); // By setting this value as 'internal', we can receive notification of its changes via
                               // 'setInternalValueInContext'
    tAttr.setUsedAsFilename( true );
    result = addAttribute( seqPath );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    seqProxyPath = tAttr.create( "seqProxyPath", "proxyPath", MFnData::kString, MObject::kNullObj );
    tAttr.setInternal( true ); // By setting this value as 'internal', we can receive notification of its changes via
                               // 'setInternalValueInContext'
    tAttr.setUsedAsFilename( true );
    result = addAttribute( seqProxyPath );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    outMesh = tAttr.create( "outMesh", "om", MFnData::kMesh, MObject::kNullObj, &result );
    CHECK_MSTATUS_AND_RETURN_IT( result );
    tAttr.setHidden( true );
    tAttr.setWritable( false );
    tAttr.setStorable( false );
    result = addAttribute( outMesh );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    MFnUnitAttribute fnUnitAttr;
    inPlaybackGraph = fnUnitAttr.create( "inPlaybackGraph", "playbackGraph", MTime( 1.0, MTime::uiUnit() ), &result );
    fnUnitAttr.setKeyable( true );
    fnUnitAttr.setWritable( true );
    CHECK_MSTATUS_AND_RETURN_IT( result );
    result = addAttribute( inPlaybackGraph );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    inTime = fnUnitAttr.create( "inTime", "time", MFnUnitAttribute::kTime, 0, &result );
    CHECK_MSTATUS_AND_RETURN_IT( result );
    result = addAttribute( inTime );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    MFnNumericAttribute nAttr;

    // inFrameOffset
    inFrameOffset = nAttr.create( "inFrameOffset", "frameOffset", MFnNumericData::kInt, 0.0 );
    result = addAttribute( inFrameOffset );
    CHECK_MSTATUS_AND_RETURN_IT( result );
    // inUseCustomRange
    inUseCustomRange = nAttr.create( "inUseCustomRange", "useRange", MFnNumericData::kBoolean, 0.0 );
    result = addAttribute( inUseCustomRange );
    CHECK_MSTATUS_AND_RETURN_IT( result );
    // inCustomRangeStart
    inCustomRangeStart = nAttr.create( "inCustomRangeStart", "rangeStart", MFnNumericData::kInt, 0 );
    result = addAttribute( inCustomRangeStart );
    CHECK_MSTATUS_AND_RETURN_IT( result );
    // inCustomRangeEnd
    inCustomRangeEnd = nAttr.create( "inCustomRangeEnd", "rangeEnd", MFnNumericData::kInt, 100 );
    result = addAttribute( inCustomRangeEnd );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    // inRender
    inRender = nAttr.create( "inRender", "inRender", MFnNumericData::kBoolean, 0.0 );
    nAttr.setHidden( true );
    result = addAttribute( inRender );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    // inSingleFileOnly
    // inSingleFileOnly = nAttr.create( "inSingleFileOnly", "singleFile", MFnNumericData::kBoolean, 0 );
    // result = addAttribute( inSingleFileOnly );
    // CHECK_MSTATUS_AND_RETURN_IT( result );

    inAutoProxyPath = nAttr.create( "inAutoProxyPath", "autoProxyPath", MFnNumericData::kBoolean, true );
    result = addAttribute( inAutoProxyPath );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    // outMinimumAvailableFileIndex
    outMinimumAvailableFileIndex = nAttr.create( "outMinimumAvailableFileIndex", "minFile", MFnNumericData::kInt, 0 );
    nAttr.setCached( false );
    nAttr.setStorable( false );
    nAttr.setHidden( true );
    nAttr.setWritable( false );
    nAttr.setInternal( true ); // by setting these attributes as 'internal', their access is controlled by
                               // 'getInternalValueInContext', where we can recompute the value as needed
    nAttr.setConnectable( false );
    result = addAttribute( outMinimumAvailableFileIndex );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    // outMaximumAvailableFileIndex
    outMaximumAvailableFileIndex = nAttr.create( "outMaximumAvailableFileIndex", "maxFile", MFnNumericData::kInt, 0 );
    nAttr.setCached( false );
    nAttr.setStorable( false );
    nAttr.setHidden( true );
    nAttr.setWritable( false );
    nAttr.setInternal( true );
    nAttr.setConnectable( false );
    result = addAttribute( outMaximumAvailableFileIndex );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    inGroupIds = nAttr.create( "inGroupIds", "groupIds", MFnNumericData::kInt, 0, &result );
    CHECK_MSTATUS_AND_RETURN_IT( result );
    result = nAttr.setHidden( false );
    CHECK_MSTATUS_AND_RETURN_IT( result );
    result = nAttr.setReadable( false );
    CHECK_MSTATUS_AND_RETURN_IT( result );
    result = nAttr.setStorable( true );
    CHECK_MSTATUS_AND_RETURN_IT( result );
    result = nAttr.setArray( true );
    CHECK_MSTATUS_AND_RETURN_IT( result );
    result = addAttribute( inGroupIds );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    inEnablePlaybackGraph = nAttr.create( "inEnablePlaybackGraph", "enablePlaybackGraph", MFnNumericData::kBoolean, 0 );
    result = addAttribute( inEnablePlaybackGraph );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    MFnEnumAttribute fnEnumAttribute;

    inCustomRangeStartClampMode =
        fnEnumAttribute.create( "inCustomRangeStartClampMode", "rangeStartClampMode", CLAMP_MODE_HOLD, &result );
    fnEnumAttribute.addField( "Hold First", CLAMP_MODE_HOLD );
    fnEnumAttribute.addField( "Blank", CLAMP_MODE_BLANK );
    result = addAttribute( inCustomRangeStartClampMode );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    inCustomRangeEndClampMode =
        fnEnumAttribute.create( "inCustomRangeEndClampMode", "rangeEndClampMode", CLAMP_MODE_HOLD, &result );
    fnEnumAttribute.addField( "Hold Last", CLAMP_MODE_HOLD );
    fnEnumAttribute.addField( "Blank", CLAMP_MODE_BLANK );
    result = addAttribute( inCustomRangeEndClampMode );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    inLoadingMode = fnEnumAttribute.create( "inLoadingMode", "loadingMode", LOADMODE_VELOCITY_OFFSET, &result );
    fnEnumAttribute.addField( "Velocity Offset", LOADMODE_VELOCITY_OFFSET );
    // fnEnumAttribute.addField( "Nearest Frame, Velocity Offset"	, LOADMODE_NEAREST_VELOCITY_OFFSET );
    fnEnumAttribute.addField( "Velocity Offset, Subframes", LOADMODE_SUBFRAME_VELOCITY_OFFSET );
    fnEnumAttribute.addField( "Frame Interpolation", LOADMODE_FRAME_INTERPOLATION );
    fnEnumAttribute.addField( "Frame Interpolation, Subframes", LOADMODE_SUBFRAME_INTERPOLATION );
    fnEnumAttribute.addField( "Single File Only", LOADMODE_STATIC );
    fnEnumAttribute.addField( "None", LOADMODE_BLANK );
    result = addAttribute( inLoadingMode );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    inViewportSource = fnEnumAttribute.create( "inViewportSource", "viewportSource", SEQ_RENDER, &result );
    fnEnumAttribute.addField( "Render Sequence", SEQ_RENDER );
    fnEnumAttribute.addField( "Proxy Sequence", SEQ_PROXY );
    result = addAttribute( inViewportSource );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    inRenderSource = fnEnumAttribute.create( "inRenderSource", "renderSource", SEQ_RENDER, &result );
    fnEnumAttribute.addField( "Render Sequence", SEQ_RENDER );
    fnEnumAttribute.addField( "Proxy Sequence", SEQ_PROXY );
    result = addAttribute( inRenderSource );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    inDisplayMode = fnEnumAttribute.create( "inDisplayStyle", "displayStyle", DISPLAY_MODE_MESH, &result );
    fnEnumAttribute.addField( "Mesh", DISPLAY_MODE_MESH );
    fnEnumAttribute.addField( "Bounding Box", DISPLAY_MODE_BOX );
    fnEnumAttribute.addField( "Vertices", DISPLAY_MODE_VERTEX );
    fnEnumAttribute.addField( "Faces", DISPLAY_MODE_FACE );
    result = addAttribute( inDisplayMode );
    CHECK_MSTATUS_AND_RETURN_IT( result );

    inDisplayPercent = nAttr.create( "inDisplayPercent", "displayPercent", MFnNumericData::kFloat, 100.0f );
    nAttr.setMin( 0.0 );
    nAttr.setMax( 100.0 );
    result = addAttribute( inDisplayPercent );

    /*
            inCustomScale = nAttr.create( "inCustomScale", "customScale", MFnNumericData::kFloat, 1.0f, &result );
            CHECK_MSTATUS_AND_RETURN_IT(result);
            //result = nAttr.setKeyable( false );
            nAttr.setMin( 0.000001L );
            nAttr.setMax( 100000.0L );
            result = addAttribute( inCustomScale );
            CHECK_MSTATUS_AND_RETURN_IT(result);

            inLengthUnit = fnEnumAttribute.create( "inLengthUnit", "lengthUnit", UNITS_GENERIC, &result );
            fnEnumAttribute.addField( "Scene Units"	, UNITS_GENERIC );
            fnEnumAttribute.addField( "Centimeters"	, UNITS_CM );
            fnEnumAttribute.addField( "Millimeters"	, UNITS_MM );
            fnEnumAttribute.addField( "Meters"		, UNITS_M  );
            fnEnumAttribute.addField( "Inches"		, UNITS_IN );
            fnEnumAttribute.addField( "Feet"		, UNITS_FT );
            fnEnumAttribute.addField( "Kilometers"	, UNITS_KM );
            fnEnumAttribute.addField( "Miles"		, UNITS_MILES );
            fnEnumAttribute.addField( "Custom"		, UNITS_CUSTOM );
            result = addAttribute( inLengthUnit );
            CHECK_MSTATUS_AND_RETURN_IT(result);
    */
    // attributeAffects( inSingleFileOnly, outMesh );
    attributeAffects( seqPath, outMesh );
    attributeAffects( seqProxyPath, outMesh );
    attributeAffects( inTime, outMesh );
    attributeAffects( inGroupIds, outMesh );
    attributeAffects( inPlaybackGraph, outMesh );
    attributeAffects( inEnablePlaybackGraph, outMesh );
    attributeAffects( inFrameOffset, outMesh );
    attributeAffects( inUseCustomRange, outMesh );
    attributeAffects( inCustomRangeStart, outMesh );
    attributeAffects( inCustomRangeEnd, outMesh );
    attributeAffects( inCustomRangeStartClampMode, outMesh );
    attributeAffects( inCustomRangeEndClampMode, outMesh );
    attributeAffects( inLoadingMode, outMesh );
    attributeAffects( inAutoProxyPath, outMesh );
    attributeAffects( inViewportSource, outMesh );
    attributeAffects( inDisplayMode, outMesh );
    attributeAffects( inDisplayPercent, outMesh );
    attributeAffects( inRenderSource, outMesh );
    attributeAffects( inRender, outMesh );

    return MStatus::kSuccess;
}

MBoundingBox SequenceXMeshNode::boundingBox() const { return m_boundingBox; }

bool SequenceXMeshNode::isBounded() const { return true; }

void SequenceXMeshNode::draw( M3dView& view, const MDagPath& /*path*/, M3dView::DisplayStyle /*style*/,
                              M3dView::DisplayStatus status ) {
    MColor currentColor =
        ( status == M3dView::kActive || status == M3dView::kLead ) ? MColor( 0.0, 1.0, 0.0 ) : colorRGB( status );

    view.beginGL();

    glColor3f( currentColor.r, currentColor.g, currentColor.b );

    frantic::maya::graphics::gl_draw( *g_iconMesh );

    if( get_display_mode() == DISPLAY_MODE_BOX ) {
        frantic::maya::graphics::gl_draw_box_wireframe( get_mesh_bounding_box() );
    } else if( get_display_mode() == DISPLAY_MODE_VERTEX && m_cachedPolymesh3 ) {
        gl_draw_verts( m_cachedPolymesh3, get_display_fraction() );
    }

    view.endGL();
}

// TODO: exception handling
#if MAYA_API_VERSION >= 201800
bool SequenceXMeshNode::getInternalValue( const MPlug& plug, MDataHandle& dataHandle ) {
#else
bool SequenceXMeshNode::getInternalValueInContext( const MPlug& plug, MDataHandle& dataHandle,
                                                   MDGContext& /*currentContext*/ ) {
#endif
    if( plug == outMinimumAvailableFileIndex || plug == outMaximumAvailableFileIndex ) {
        const frantic::files::filename_sequence& seq = get_sequence( SEQ_RENDER, false );
        if( seq.directory_exists() ) {
            const frantic::files::frame_set& frames = seq.get_frame_set();

            if( frames.empty() ) {
                dataHandle.setInt( 0 );
            } else {
                if( plug == outMinimumAvailableFileIndex ) {
                    dataHandle.setInt( (int)*frames.begin() );
                } else {
                    dataHandle.setInt( (int)*frames.rbegin() );
                }
            }
        } else {
            dataHandle.setInt( 0 );
        }

        return true;
    }

    return false;
}

// TODO: remove this?

#if MAYA_API_VERSION >= 201800
bool SequenceXMeshNode::setInternalValue( const MPlug&, const MDataHandle& ) {
#else
bool SequenceXMeshNode::setInternalValueInContext( const MPlug& /*plug*/, const MDataHandle& /*dataHandle*/,
                                                   MDGContext& /*currentContext*/ ) {
#endif
    return false;
}

MStatus SequenceXMeshNode::compute( const MPlug& plug, MDataBlock& data ) {
    using frantic::graphics::vector3;
    using frantic::graphics::vector3f;

    MStatus stat;

    // Output Request from 'outMesh'
    if( plug == outMesh ) {
        try {
            //-------------------------------------------------------------
            // Update Settings
            //-------------------------------------------------------------

            bool invalidCache = false;

            // In Render Mode?
            const bool inRenderingMode = MPlug( thisMObject(), inRender ).asBool();

            // Render or Proxy Sequence
            seq_ID seqId;
            if( inRenderingMode ) {
                seqId = (seq_ID)MPlug( thisMObject(), inRenderSource ).asInt();
            } else {
                seqId = (seq_ID)MPlug( thisMObject(), inViewportSource ).asInt();
            }

            // filename from the active sequence
            const frantic::tstring filename = get_sequence_path( seqId );

            // Display Mode
            display_mode displayMode = get_effective_display_mode();

            // Load Mask
            int loadMask = 0;
            if( displayMode == DISPLAY_MODE_BOX ) {
                loadMask |= LOAD_POLYMESH3_MASK::BOX;
            } else if( displayMode == DISPLAY_MODE_MESH || displayMode == DISPLAY_MODE_FACE ) {
                loadMask |= LOAD_POLYMESH3_MASK::STATIC_MESH;
            } else if( displayMode == DISPLAY_MODE_VERTEX ) {
                loadMask |= LOAD_POLYMESH3_MASK::VERTS;
            }

            // In Time / Out Time
            MDataHandle inTimeData = data.inputValue( inTime, &stat );
            CHECK_MSTATUS_AND_RETURN_IT( stat );

            // Group IDs
            MArrayDataHandle inGroupIdsData = data.inputArrayValue( inGroupIds, &stat );
            CHECK_MSTATUS_AND_RETURN_IT( stat );

            // Out Mesh
            MDataHandle outData = data.outputValue( outMesh, &stat );
            CHECK_MSTATUS_AND_RETURN_IT( stat );

            // OutFrame
            MTime outTime = inTimeData.asTime();
            double outFrame = outTime.asUnits( MTime::uiUnit() );

            maya_xmesh_timing xmeshTiming;
            xmeshTiming.set_offset( data.inputValue( inFrameOffset ).asInt() );

            bool useCustomRange = data.inputValue( inUseCustomRange ).asBool();
            if( useCustomRange ) {
                xmeshTiming.set_range( data.inputValue( inCustomRangeStart ).asInt(),
                                       data.inputValue( inCustomRangeEnd ).asInt() );
            }

            bool enablePlaybackGraph = data.inputValue( inEnablePlaybackGraph ).asBool();
            if( enablePlaybackGraph ) {
                xmeshTiming.set_playback_graph( thisMObject(), inPlaybackGraph );
            }

            float timeOffset = 0;
            float timeDerivative = 1;

            xmeshTiming.set_sequence_name( filename );

            // Load Mode
            load_mode loadMode = (load_mode)MPlug( thisMObject(), inLoadingMode ).asInt();

            if( filename.empty() ) {
                loadMode = LOADMODE_BLANK;
            }

            //--------------------------------------------------------------------------------------------------------------
            // BLANK FRAME LOAD
            if( loadMode == LOADMODE_BLANK ) {
                m_cachedPolymesh3.reset();
                m_metadata.clear();
                // Empty Mesh handled by catch all.

                //-------------------------------------------------------------------------------------------------------------
                // SINGLE FRAME LOAD
            } else if( loadMode == LOADMODE_STATIC ) {
                // check cache
                if( m_cachedFilenamePattern != filename || m_cachedLoadingMode != loadMode ||
                    m_cachedLoadMask != loadMask || !m_cachedPolymesh3 ) {
                    m_cachedPolymesh3.reset();
                    m_metadata.clear();

                    if( frantic::files::file_exists( filename ) ) {
                        m_cachedPolymesh3 = m_polymesh3Loader.load( filename, &m_metadata, loadMask );
                    } else {
                        throw std::runtime_error( "File does not exist: \"" + frantic::strings::to_string( filename ) +
                                                  "\"" );
                    }

                    invalidCache = true;
                }
                //-------------------------------------------------------------------------------------------------------------
                // VELOCITY OFFSET MODES
            } else if( loadMode == LOADMODE_VELOCITY_OFFSET || loadMode == LOADMODE_SUBFRAME_VELOCITY_OFFSET ) {
                xmesh_timing::range_region rangeRegion;
                double sampleFrameNumber;
                double frameOffset;
                if( loadMode == LOADMODE_VELOCITY_OFFSET ) {
                    xmeshTiming.get_frame_velocity_offset( outFrame, get_sequence( seqId ).get_frame_set(), rangeRegion,
                                                           sampleFrameNumber, frameOffset );
                } else {
                    xmeshTiming.get_subframe_velocity_offset( outFrame, get_sequence( seqId ).get_frame_set(),
                                                              rangeRegion, sampleFrameNumber, frameOffset );
                }

                const bool useEmptyMesh =
                    rangeRegion == xmesh_timing::RANGE_BEFORE && get_start_clamp_mode() == CLAMP_MODE_BLANK ||
                    rangeRegion == xmesh_timing::RANGE_AFTER && get_end_clamp_mode() == CLAMP_MODE_BLANK;

                if( useEmptyMesh ) {
                    m_cachedPolymesh3.reset();
                    m_metadata.clear();
                } else {
                    if( displayMode != DISPLAY_MODE_BOX ) {
                        loadMask |= LOAD_POLYMESH3_MASK::VELOCITY;
                    }

                    if( m_cachedFilenamePattern != filename || m_cachedLoadingMode != loadMode ||
                        m_cachedLoadMask != loadMask || m_cachedFrame != sampleFrameNumber || !m_cachedPolymesh3 ) {
                        invalidCache = true;
                        load_mesh_at_frame( seqId, sampleFrameNumber, loadMask );
                        m_cachedFrame = sampleFrameNumber;
                    }
                    timeOffset = static_cast<float>( MTime( frameOffset, MTime::uiUnit() ).as( MTime::kSeconds ) );
                    timeDerivative = static_cast<float>( xmeshTiming.get_time_derivative( outFrame, 0.25 ) );
                }
            }
            //-------------------------------------------------------------------------------------------------------------
            // FRAME INTERPOLATE MODES
            else if( loadMode == LOADMODE_FRAME_INTERPOLATION || loadMode == LOADMODE_SUBFRAME_INTERPOLATION ) {
                xmesh_timing::range_region rangeRegion;
                std::pair<double, double> sampleFrameBracket;
                double alpha;
                if( loadMode == LOADMODE_FRAME_INTERPOLATION ) {
                    xmeshTiming.get_frame_interpolation( outFrame, get_sequence( seqId ).get_frame_set(), rangeRegion,
                                                         sampleFrameBracket, alpha );
                } else {
                    xmeshTiming.get_subframe_interpolation( outFrame, get_sequence( seqId ).get_frame_set(),
                                                            rangeRegion, sampleFrameBracket, alpha );
                }

                const bool useEmptyMesh =
                    rangeRegion == xmesh_timing::RANGE_BEFORE && get_start_clamp_mode() == CLAMP_MODE_BLANK ||
                    rangeRegion == xmesh_timing::RANGE_AFTER && get_end_clamp_mode() == CLAMP_MODE_BLANK;

                if( useEmptyMesh ) {
                    m_cachedPolymesh3.reset();
                    m_metadata.clear();
                } else {
                    if( m_cachedFilenamePattern != filename || m_cachedLoadingMode != loadMode ||
                        m_cachedLoadMask != loadMask || m_cachedInterval != sampleFrameBracket ||
                        !m_cachedPolymesh3Interval.first || !m_cachedPolymesh3Interval.second ) {
                        invalidCache = true;
                        load_mesh_interval( seqId, sampleFrameBracket, loadMask );
                        m_cachedInterval = sampleFrameBracket;
                    }

                    if( loadMask == LOAD_POLYMESH3_MASK::BOX ) {
                        m_cachedPolymesh3 = m_cachedPolymesh3Interval.first;
                    } else {
                        if( alpha == 0 ) {
                            m_cachedPolymesh3 = m_cachedPolymesh3Interval.first;
                        } else if( alpha == 1.f ) {
                            m_cachedPolymesh3 = m_cachedPolymesh3Interval.second;
                        } else {
                            m_cachedPolymesh3 =
                                linear_interpolate( m_cachedPolymesh3Interval.first, m_cachedPolymesh3Interval.second,
                                                    static_cast<float>( alpha ) );
                        }
                    }
                }
            } else {
                throw std::runtime_error( "Unknown loading mode \"" + boost::lexical_cast<std::string>( loadMode ) +
                                          "\"" );
            }

            if( m_cachedPolymesh3 && ( displayMode == DISPLAY_MODE_MESH || displayMode == DISPLAY_MODE_FACE ) ) {
                const float faceFraction = ( displayMode == DISPLAY_MODE_FACE ? get_display_fraction() : 1.f );

                double fps = frantic::maya::get_fps();
                // TO DO:   use the following code when fixing fps in both velocity color set and position offset
                /*if( m_metadata.has_frames_per_second() ) {
                        boost::rational<boost::int64_t> xmeshFpsRatio = m_metadata.get_frames_per_second();
                        fps = static_cast<float>( xmeshFpsRatio.numerator() ) / static_cast<float>(
                xmeshFpsRatio.denominator() ); } else { fps = frantic::maya::get_fps();
                }*/

                MObject meshDataBlock;
                stat = polymesh_copy( meshDataBlock, m_cachedPolymesh3, inGroupIdsData, timeOffset, faceFraction, fps,
                                      timeDerivative );
                cache_bounding_box();
                CHECK_MSTATUS_AND_RETURN_IT( stat );
                stat = outData.set( meshDataBlock );
                CHECK_MSTATUS_AND_RETURN_IT( stat );
                data.setClean( plug );
            } else {
                // Catch All for Empty Meshes
                cache_bounding_box();
                CreateEmptyMesh( outData );
                data.setClean( plug );
            }

            // save cache data
            if( invalidCache ) {
                m_cachedFilenamePattern = filename;
                m_cachedLoadingMode = loadMode;
                m_cachedLoadMask = loadMask;
            }

            // TODO: option to clear mesh in memory?

        } catch( std::exception& e ) {
            m_cachedPolymesh3.reset();
            m_metadata.clear();
            cache_bounding_box();

            MStatus emptyMeshStat;
            MDataHandle outData = data.outputValue( outMesh, &emptyMeshStat );
            if( emptyMeshStat ) {
                emptyMeshStat = CreateEmptyMesh( outData );
            }
            if( emptyMeshStat ) {
                data.setClean( plug );
            }

            std::stringstream ss;
            ss << "SequenceXMeshNode::compute: " << e.what() << std::endl;
            MGlobal::displayError( ss.str().c_str() );

            return MStatus::kFailure;
        }
    } else {
        return MStatus::kUnknownParameter;
    }

    return MStatus::kSuccess;
}

boost::shared_ptr<frantic::geometry::trimesh3> SequenceXMeshNode::get_icon_mesh() { return g_iconMesh; }

frantic::geometry::const_polymesh3_ptr SequenceXMeshNode::get_cached_mesh() { return m_cachedPolymesh3; }

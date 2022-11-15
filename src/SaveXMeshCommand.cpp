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

#include <boost/algorithm/cxx11/copy_if.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/make_shared.hpp>
#include <boost/regex.hpp>
using namespace boost::assign;

#include "SaveXMeshCommand.hpp"
#include "SequenceSaverHelper.hpp"

#include <maya/MAnimControl.h>
#include <maya/MArgList.h>
#include <maya/MDagPath.h>
#include <maya/MFloatPointArray.h>
#include <maya/MFnMesh.h>
#include <maya/MGlobal.h>
#include <maya/MItMeshVertex.h>
#include <maya/MPlug.h>
#include <maya/MSelectionList.h>
#include <maya/MSyntax.h>
#include <maya/MThreadUtils.h>
#include <maya/MTime.h>

#include <frantic/maya/convert.hpp>
#include <frantic/maya/geometry/mesh.hpp>
#include <frantic/maya/graphics/maya_space.hpp>
#include <frantic/maya/util.hpp>

#include <frantic/files/paths.hpp>
#include <frantic/geometry/polymesh3.hpp>
#include <frantic/geometry/polymesh3_builder.hpp>
#include <frantic/geometry/polymesh3_file_io.hpp>
#include <frantic/math/fractions.hpp>

#include <frantic/diagnostics/profiling_section.hpp>
#include <frantic/logging/logging_level.hpp>

#include "progress_bar_progress_logger.hpp"

#include "material_id_map.hpp"
#include "material_utils.hpp"

namespace {

struct profiling_sections {
    frantic::diagnostics::profiling_section frameTime;
    frantic::diagnostics::profiling_section meshGetTime;
    frantic::diagnostics::profiling_section meshWriteTime;

    profiling_sections()
        : frameTime( _T("Frame") )
        , meshGetTime( _T("Get Mesh") )
        , meshWriteTime( _T("Save Mesh") ) {}
};

template <class CharType>
inline std::basic_ostream<CharType>& operator<<( std::basic_ostream<CharType>& out,
                                                 const struct profiling_sections& ps ) {
    return out << ps.frameTime << endl << ps.meshGetTime << endl << ps.meshWriteTime;
}

template <class Operator>
MStatus for_each_mesh_in_selection_list( const MSelectionList& selList, Operator op ) {
    MStatus stat;

    for( unsigned int selIndex = 0; selIndex < selList.length(); ++selIndex ) {
        MObject obj;
        if( stat )
            stat = selList.getDependNode( selIndex, obj );

        MFnDagNode fnDag;
        if( stat )
            stat = fnDag.setObject( obj );

        MDagPath dagPath;
        if( stat )
            stat = fnDag.getPath( dagPath );

        unsigned int numberOfShapes = 0;
        if( stat )
            stat = dagPath.numberOfShapesDirectlyBelow( numberOfShapes );

        size_t meshCount = 0;

        if( numberOfShapes > 0 ) {
            for( unsigned int i = 0; i < numberOfShapes; ++i ) {
                MDagPath childPath( dagPath );

                if( stat )
                    stat = childPath.extendToShapeDirectlyBelow( i );

                MFnMesh fnMesh;
                if( stat )
                    stat = fnMesh.setObject( childPath );
                if( stat ) {
                    op( fnMesh, childPath );
                    ++meshCount;
                }
            }
        } else {
            stat = selList.getDagPath( selIndex, dagPath );

            MFnMesh fnMesh;
            if( stat )
                stat = fnMesh.setObject( dagPath );
            if( stat ) {
                op( fnMesh, dagPath );
                ++meshCount;
            }
        }

        if( meshCount == 0 ) {
            throw std::runtime_error( "Selection must be a mesh object" );
        }
    }

    return MStatus::kSuccess;
}

template <class Operator>
MStatus for_each_mesh_in_selection( Operator op ) {
    MStatus stat;
    MSelectionList selList;
    stat = MGlobal::getActiveSelectionList( selList );
    if( !stat ) {
        throw std::runtime_error( "for_each_mesh_in_selection Error: unable to get active selection list" );
    }

    return for_each_mesh_in_selection_list( selList, op );
}

/**
 * Adds the given dagPath to the given vector outDagPaths.
 *
 * @param		dagPath			The path to be added to the collection.
 * @param[out]	outDagPaths		The collection to contain the path.
 */
void collect_dagpaths( const MDagPath& dagPath, std::vector<MDagPath>& outDagPaths ) {
    outDagPaths.push_back( dagPath );
}

/**
 * Gets the mesh shape nodes associated with the currently-selected objects in Maya.
 *
 * @param[out]	dagPaths		A container for the paths to the shape nodes.
 */
void get_selected_mesh_shapes( std::vector<MDagPath>& dagPaths ) {
    dagPaths.clear();
    for_each_mesh_in_selection( boost::bind( collect_dagpaths, _2, boost::ref( dagPaths ) ) );
}

void get_selected_mesh_shapes( const MSelectionList& selectionList, std::vector<MDagPath>& dagPaths ) {
    dagPaths.clear();
    for_each_mesh_in_selection_list( selectionList, boost::bind( collect_dagpaths, _2, boost::ref( dagPaths ) ) );
}

void collect_vertices( MFnMesh& fnMesh, bool worldSpace, frantic::geometry::polymesh3_builder& builder ) {
    MStatus stat;
    MFloatPointArray mayaVerts;
    stat = fnMesh.getPoints( mayaVerts, worldSpace ? MSpace::kWorld : MSpace::kObject );
    if( !stat ) {
        throw std::runtime_error( "collect_vertices Error: unable to get points from mesh" );
    }

    for( int i = 0, iEnd = mayaVerts.length(); i < iEnd; ++i ) {
        builder.add_vertex( mayaVerts[i].x, mayaVerts[i].y, mayaVerts[i].z );
    }
}

frantic::geometry::polymesh3_ptr create_combined_polymesh3_from_verts( const std::vector<MDagPath>& dagPaths,
                                                                       bool worldSpace ) {
    frantic::geometry::polymesh3_builder builder;
    BOOST_FOREACH( const MDagPath& dagPath, dagPaths ) {
        MStatus stat;
        MFnMesh fnMesh( dagPath, &stat );
        if( !stat ) {
            throw std::runtime_error( "create_combined_polymesh3_from_verts Error: unable to get mesh from dag path" );
        }
        collect_vertices( fnMesh, worldSpace, builder );
    }
    return builder.finalize();
}

void exclude_channel( frantic::channels::channel_propagation_policy& cpp, const frantic::tstring& channelName ) {
    if( cpp.is_include_list() ) {
        cpp.remove_channel( channelName );
    } else {
        cpp.add_channel( channelName );
    }
}

bool get_bool_attribute( MFnDependencyNode& fnDependencyNode, const MString& attributeName ) {
    MStatus stat;

    MPlug plug = fnDependencyNode.findPlug( attributeName, true, &stat );
    if( !stat ) {
        throw std::runtime_error( "get_bool_attribute Error: unable to find \'" +
                                  std::string( attributeName.asChar() ) + "\' plug" );
    }

    bool result;
    stat = plug.getValue( result );
    if( !stat ) {
        throw std::runtime_error( "get_bool_attribute Error: unable to get \'" + std::string( attributeName.asChar() ) +
                                  "\' value" );
    }
    return result;
}

bool get_bool_attribute_with_default( MFnDependencyNode& fnDependencyNode, const MString& attributeName,
                                      bool defaultValue ) {
    MStatus stat;

    MPlug plug = fnDependencyNode.findPlug( attributeName, true, &stat );
    if( !stat ) {
        return defaultValue;
    }

    bool result;
    stat = plug.getValue( result );
    if( !stat ) {
        throw std::runtime_error( "get_bool_attribute_with_default Error: unable to get \'" +
                                  std::string( attributeName.asChar() ) + "\' value" );
    }
    return result;
}

bool is_visible( MDagPath dagPath ) {
#if MAYA_API_VERSION >= 201300
    return dagPath.isVisible();
#else
    MStatus stat;

    if( !dagPath.isValid() ) {
        return false;
    }

    // after each iteration, move up one level in the dag path
    // stop iteration after checking the root (length() == 0)
    for( ;; ) {
        MFnDependencyNode fnDependencyNode( dagPath.node(), &stat );
        if( !stat ) {
            throw std::runtime_error( "is_visible Error: unable to get dependency node from dag path" );
        }

        bool visible = true;

        if( visible ) {
            visible = get_bool_attribute_with_default( fnDependencyNode, "visibility", false );
        }

        if( visible ) {
            visible = get_bool_attribute_with_default( fnDependencyNode, "lodVisibility", false );
        }

        if( visible ) {
            visible = !get_bool_attribute_with_default( fnDependencyNode, "intermediateObject", false );
        }

        if( visible && get_bool_attribute_with_default( fnDependencyNode, "overrideEnabled", false ) ) {
            visible = get_bool_attribute( fnDependencyNode, "overrideVisibility" );
        }

        if( !visible ) {
            return false;
        }

        if( dagPath.length() == 0 ) {
            return true;
        }

        dagPath.pop();
    }

    return true;
#endif
}

frantic::geometry::polymesh3_ptr
create_polymesh3( const MDagPath& dagPath, bool worldSpace, bool colorFromCurrentColorSet,
                  bool textureCoordFromCurrentUVSet,
                  std::map<std::string, std::vector<boost::uint32_t>>* pSmoothingGroupAssignmentCache,
                  material_id_map* materialIDMap, const frantic::channels::channel_propagation_policy& cpp ) {
    MStatus stat;

    MFnMesh fnMesh( dagPath, &stat );
    if( !stat ) {
        throw std::runtime_error( "create_polymesh3 Error: unable to get mesh from dagPath" );
    }

    frantic::channels::channel_propagation_policy fromMayaCPP( cpp );
    exclude_channel( fromMayaCPP, _T("MaterialID") );
    exclude_channel( fromMayaCPP, _T("SmoothingGroup") );

    frantic::geometry::polymesh3_ptr polymesh = frantic::maya::geometry::polymesh_copy(
        dagPath, worldSpace, fromMayaCPP, colorFromCurrentColorSet, textureCoordFromCurrentUVSet );

    if( cpp.is_channel_included( _T("SmoothingGroup") ) ) {
        std::vector<boost::uint32_t>* pSmoothingGroupAssignment = 0;

        if( pSmoothingGroupAssignmentCache ) {
            MString fullPathName( dagPath.fullPathName( &stat ) );
            if( !stat ) {
                throw std::runtime_error( "create_polymesh3 Error: unable to get fullPathName from dagPath" );
            }

            pSmoothingGroupAssignment = &( *pSmoothingGroupAssignmentCache )[fullPathName.asUTF8()];
        }

        if( pSmoothingGroupAssignment ) {
            frantic::maya::geometry::create_smoothing_groups( fnMesh, *pSmoothingGroupAssignment, polymesh );
        } else {
            frantic::maya::geometry::create_smoothing_groups( fnMesh, polymesh );
        }
    }
    if( cpp.is_channel_included( _T("MaterialID") ) ) {
        if( !materialIDMap ) {
            throw std::runtime_error( "create_polymesh3 Error: materialIDMap is NULL" );
        }
        create_material_id_channel( dagPath, fnMesh, polymesh, *materialIDMap );
    }
    if( cpp.is_channel_included( _T("EdgeSharpness") ) ) {
        frantic::maya::geometry::copy_edge_creases( dagPath, fnMesh, polymesh );
    }
    if( cpp.is_channel_included( _T("VertexSharpness") ) ) {
        frantic::maya::geometry::copy_vertex_creases( dagPath, fnMesh, polymesh );
    }

    return polymesh;
}

bool is_consistent_topology( const std::vector<frantic::geometry::polymesh3_ptr>& meshes,
                             const std::vector<frantic::geometry::polymesh3_ptr>& stepMeshes ) {
    if( meshes.size() != stepMeshes.size() ) {
        return false;
    }

    for( std::size_t i = 0; i < meshes.size(); ++i ) {
        if( !frantic::geometry::is_consistent_topology( meshes[i], stepMeshes[i] ) ) {
            return false;
        }
    }

    return true;
}

void add_zero_velocity_channel( frantic::geometry::polymesh3_ptr mesh ) {
    frantic::graphics::raw_byte_buffer dataBuffer;
    const std::size_t numBytes = mesh->vertex_count() * sizeof( frantic::graphics::vector3f );
    dataBuffer.resize( numBytes );
    memset( dataBuffer.begin(), 0, numBytes );

    mesh->add_vertex_channel( _T("Velocity"), frantic::channels::data_type_float32, 3, dataBuffer );
}

bool any_mesh_has_velocity_channel( const std::vector<frantic::geometry::polymesh3_ptr>& meshes ) {
    BOOST_FOREACH( const frantic::geometry::polymesh3_ptr& mesh, meshes ) {
        if( mesh->has_vertex_channel( _T("Velocity") ) ) {
            return true;
        }
    }
    return false;
}

void ensure_every_mesh_has_velocity_channel( std::vector<frantic::geometry::polymesh3_ptr>& meshes ) {
    BOOST_FOREACH( frantic::geometry::polymesh3_ptr& mesh, meshes ) {
        if( !mesh->has_vertex_channel( _T("Velocity") ) ) {
            add_zero_velocity_channel( mesh );
        }
    }
}

/**
 * Create the velocity channel in the given mesh using the given per-vertex velocity vectors.
 *
 * @param[in,out]	mesh		Specifies the polymesh that will have a velocity channel added to it.
 * @param			velocities	Describes the velocity of each vertex in mesh.
 */
void add_velocity_channel( frantic::geometry::polymesh3_ptr& mesh,
                           const std::vector<frantic::graphics::vector3f>& velocities ) {
    // Ensure we have the appropriate number of velocities.
    if( mesh->vertex_count() == velocities.size() ) {
        mesh->add_empty_vertex_channel( _T("Velocity"), frantic::channels::data_type_float32, 3 );
        frantic::geometry::polymesh3_vertex_accessor<frantic::graphics::vector3f> velAcc =
            mesh->get_vertex_accessor<frantic::graphics::vector3f>( _T("Velocity") );

        // Iterate over the parallel vectors and write values to the velocity channel.
        for( std::size_t index = 0; index < mesh->vertex_count(); ++index ) {
            velAcc.get_vertex( index ) = velocities[index];
        }
    } else { // Otherwise, die.
        throw std::runtime_error(
            "add_velocity_channel: The number of velocities did not match the number of vertices." );
    }
}

void create_velocity_channel_from_consistent_mesh_sample( frantic::geometry::polymesh3_ptr mainMesh,
                                                          frantic::geometry::polymesh3_ptr checkMesh,
                                                          const float timeDifferenceInSeconds ) {
    mainMesh->add_empty_vertex_channel( _T("Velocity"), frantic::channels::data_type_float32, 3 );
    frantic::geometry::polymesh3_vertex_accessor<frantic::graphics::vector3f> velAcc =
        mainMesh->get_vertex_accessor<frantic::graphics::vector3f>( _T("Velocity") );

    frantic::geometry::polymesh3_const_vertex_accessor<frantic::graphics::vector3f> mainAcc =
        mainMesh->get_const_vertex_accessor<frantic::graphics::vector3f>( _T("verts") );
    frantic::geometry::polymesh3_const_vertex_accessor<frantic::graphics::vector3f> checkAcc =
        checkMesh->get_const_vertex_accessor<frantic::graphics::vector3f>( _T("verts") );

    for( std::size_t i = 0; i < mainMesh->vertex_count(); ++i ) {
        const frantic::graphics::vector3f velocity( ( checkAcc.get_vertex( i ) - mainAcc.get_vertex( i ) ) /
                                                    timeDifferenceInSeconds );
        velAcc.get_vertex( i ) = velocity;
    }
}

void create_velocity_channel_from_consistent_mesh_sample(
    std::vector<frantic::geometry::polymesh3_ptr>& meshes,
    const std::vector<frantic::geometry::polymesh3_ptr>& stepMeshes, float timeDifferenceInSeconds ) {
    for( std::size_t i = 0; i < meshes.size(); ++i ) {
        create_velocity_channel_from_consistent_mesh_sample( meshes[i], stepMeshes[i], timeDifferenceInSeconds );
    }
}

/**
 * Get the per-vertex velocities for the shape using the Motion Vector Color Set (MVCS).
 *
 * @param		dagPath			The DAG path to the shape to get velocities from.
 * @param[out]	meshVelocities	This will be filled with per-vertex velocities. Each element of this vector describes
 *the velocity for each vertex in the shape. If the shape has no MVCS information, meshVelocities is not modified.
 * @param		worldSpace		Describes whether or not the velocities need to be in object space or world
 *space. If in world space, then they must include transformations.
 */
void get_vertex_velocities_from_mvcs( const MDagPath& dagPath, std::vector<frantic::graphics::vector3f>& meshVelocities,
                                      const bool worldSpace ) {
    MStatus status;

    // Make a mesh and get the motion vector color set attribute.
    MFnMesh currentMesh( dagPath, &status );
    MPlug mvcsPlug;
    if( status )
        mvcsPlug = currentMesh.findPlug( "motionVectorColorSet", &status );

    // Get the name of the motion vector color set, since it can be customized.
    MString mvcsName;
    if( status )
        status = mvcsPlug.getValue( mvcsName );

    // Get the velocities and add them to the output variable.
    if( status && mvcsName.length() > 0 ) {

        // If we're in world coordinates, get the transformtion matrix for this object. We need to apply it to vertex
        // velocities.
        frantic::graphics::transform4f transformDerivative;
        frantic::graphics::transform4f startTransform;
        if( worldSpace ) {
            float timeStepInFrames = 0.25f;
            MTime currentTime = MAnimControl::currentTime();

            MDGContext currentContext( currentTime );
            frantic::maya::get_object_world_matrix( dagPath, currentContext, startTransform );

            currentContext = MDGContext( currentTime + timeStepInFrames );
            frantic::graphics::transform4f endTransform;
            frantic::maya::get_object_world_matrix( dagPath, currentContext, endTransform );

            transformDerivative = ( ( endTransform - startTransform ) / timeStepInFrames );
        }

        float fps = (float)frantic::maya::get_fps();
        MItMeshVertex vertIter( dagPath );
        meshVelocities.clear();
        meshVelocities.reserve( vertIter.count() );
        while( !vertIter.isDone() ) {

            // Get the vertex velocity (Maya stores this as a color). This is in terms of object space.
            MColor currentColor( 0.0f, 0.0f, 0.0f );
            vertIter.getColor( currentColor, &mvcsName );
            frantic::graphics::vector3f vertexVelocity( currentColor[0], currentColor[1], currentColor[2] );

            // If we're in world space, modify the velocity with the world transformation  on the mesh.
            if( worldSpace ) {
                // Get the vertex position.
                frantic::graphics::vector3f vertexPosition =
                    frantic::maya::from_maya_t( vertIter.position( MSpace::kWorld, &status ) );

                // Calculate the vertex velocity in world space.
                vertexVelocity =
                    startTransform.transform_no_translation( vertexVelocity ) + transformDerivative * vertexPosition;
            }

            // Add the vertex velocity (in terms of velocity per second, not per frame) to the output buffer.
            meshVelocities.push_back( vertexVelocity * fps );
            vertIter.next();
        }
    }
}

/**
 * Build a list of prospective time steps and return them in a vector.
 *
 * @param		startTime	Describes the starting time from which to start the time setps.
 * @param[out]	timeSteps	Contains the calculated time steps.
 */
void get_time_steps( const MTime& startTime, std::vector<MTime>& timeSteps ) {
    const double initialTimeStep = 0.25;
    const double timeStepScale = 0.5;
    const std::size_t maxPositiveTimeStepCount = 24;

    MTime lastSampleTime( startTime );
    for( std::size_t i = 0; i < maxPositiveTimeStepCount; ++i ) {
        MTime timeStep( initialTimeStep * std::pow( timeStepScale, static_cast<double>( i ) ), startTime.unit() );
        MTime sampleTime = startTime + timeStep;
        if( sampleTime == startTime || sampleTime == lastSampleTime ) {
            break;
        }
        lastSampleTime = sampleTime;
        timeSteps.push_back( timeStep );
    }
    if( timeSteps.empty() ) {
        timeSteps.push_back( MTime( 1.0, MTime::k6000FPS ) );
    }
    timeSteps.push_back( MTime( 0.0, startTime.unit() ) - timeSteps.back() );
}

/**
 * Attempt to create a channel in each polymesh to store per-vertex velocity information. If this information is present
 * in Maya as Motion Vector Color Sets (MVCS), then those values are used for that mesh. Otherwise, velocity is
 * approximated by comparing vertex positions over time.
 *
 * @param			currentTime		The time within the animation for which to determine velocities.
 * @param			worldSpace		Whether the velocities need to be in world space or not.
 * @param			dagPaths		dagPath used to create the meshes, and to get velocity information for
 * them.
 * @param[in,out]	meshes			Contains the polymesh3 meshes to be modified with a new velocity
 * channel.
 */
void try_create_velocity_channel( const MTime& currentTime, bool worldSpace, const std::vector<MDagPath>& dagPaths,
                                  std::vector<frantic::geometry::polymesh3_ptr>& meshes ) {
    if( dagPaths.size() != meshes.size() ) {
        throw std::runtime_error(
            "try_create_velocity_channel Error: mismatch between number of dag paths and number of meshes" );
    }

    // Get time steps used in the sampling method just in case we need them.
    std::vector<MTime> timeSteps;
    get_time_steps( currentTime, timeSteps );

    // Store whether or not we've finished velocities for each mesh.
    size_t numNeedingVelocities = dagPaths.size();
    std::vector<char> hasVelocity;
    hasVelocity.resize( numNeedingVelocities );

    // Check for Motion Vector Color Set velocities for each mesh.
    for( size_t pathIndex = 0; pathIndex < dagPaths.size(); ++pathIndex ) {
        MDagPath currentPath( dagPaths[pathIndex] );

        // Get Motion Vector Color Set per-vertex velocities for the current FnMesh if they are available.
        std::vector<frantic::graphics::vector3f> currentVelocities;
        get_vertex_velocities_from_mvcs( currentPath, currentVelocities, worldSpace );

        if( currentVelocities.size() > 0 ) {
            add_velocity_channel( meshes[pathIndex], currentVelocities );
            hasVelocity[pathIndex] = true;
            --numNeedingVelocities;
        }
    }

    // If any meshes still need velocities, sample vertex positions over time to calculate a velocity.
    BOOST_FOREACH( const MTime& timeStep, timeSteps ) {
        if( numNeedingVelocities == 0 ) {
            break;
        }

        MGlobal::viewFrame( currentTime + timeStep );

        for( size_t pathIndex = 0; pathIndex < dagPaths.size(); ++pathIndex ) {
            MDagPath currentPath( dagPaths[pathIndex] );
            if( !hasVelocity[pathIndex] ) { // We'll skip this mesh if we already have a velocity for it.

                // Get the polymesh for the FnMesh (at the given time since we moved the frame).
                frantic::channels::channel_propagation_policy noChannels( true );
                frantic::geometry::polymesh3_ptr stepMesh =
                    frantic::maya::geometry::polymesh_copy( currentPath, worldSpace, noChannels );

                // Test that the vertices in the current mesh and the future mesh match up. If so, we can calculate a
                // velocity.
                if( frantic::geometry::is_consistent_topology( meshes[pathIndex], stepMesh ) ) {
                    const float timeStepInSeconds = static_cast<float>( timeStep.as( MTime::kSeconds ) );
                    create_velocity_channel_from_consistent_mesh_sample( meshes[pathIndex], stepMesh,
                                                                         timeStepInSeconds );
                    hasVelocity[pathIndex] = true;
                    --numNeedingVelocities;
                }
            }
        }
    }
}

frantic::geometry::polymesh3_ptr
create_combined_polymesh3( const std::vector<MDagPath>& dagPaths, bool worldSpace, bool colorFromCurrentColorSet,
                           bool textureCoordFromCurrentUVSet,
                           std::map<std::string, std::vector<boost::uint32_t>>* pSmoothingGroupAssignmentCache,
                           material_id_map& materialIDMap, const frantic::channels::channel_propagation_policy& cpp ) {
    const MTime mainTime = MAnimControl::currentTime();

    std::vector<frantic::geometry::polymesh3_ptr> meshes;
    meshes.reserve( dagPaths.size() );
    BOOST_FOREACH( const MDagPath& dagPath, dagPaths ) {
        meshes.push_back( create_polymesh3( dagPath, worldSpace, colorFromCurrentColorSet, textureCoordFromCurrentUVSet,
                                            pSmoothingGroupAssignmentCache, &materialIDMap, cpp ) );
    }

    // Here is where the velocity work will be done. This function is independent of the polymesh creation
    // and so will be ideal for making more of them w/o risking a recursive loop.
    if( cpp.is_channel_included( _T("Velocity") ) ) {
        try_create_velocity_channel( mainTime, worldSpace, dagPaths, meshes );

        // If any mesh has a Velocity channel, then make sure they all do.
        // This is to avoid creating a Velocity channel with custom faces
        // inside combine().  Such a channel will cause problems at load time.
        if( any_mesh_has_velocity_channel( meshes ) ) {
            ensure_every_mesh_has_velocity_channel( meshes );
        }
    }

    return frantic::geometry::combine( meshes );
}

void set_metadata_from_scene( frantic::geometry::xmesh_metadata& metadata ) {
    MTime::Unit timeUnit = MTime::uiUnit();
    if( timeUnit != MTime::kInvalid ) {
        const double fps = MTime( 1.0, MTime::kSeconds ).as( timeUnit );
        const std::pair<boost::int64_t, boost::int64_t> rationalFPS = frantic::math::get_rational_representation( fps );
        metadata.set_frames_per_second( rationalFPS.first, rationalFPS.second );
    }
    metadata.set_length_unit( 1.0, frantic::geometry::xmesh_metadata::length_unit_centimeters );
}

class set_time_on_scope_exit {
    MTime m_time;

  public:
    set_time_on_scope_exit( MTime time ) { m_time = time; }
    ~set_time_on_scope_exit() { MGlobal::viewFrame( m_time ); }
};

std::size_t get_io_thread_count() {
    const std::size_t mayaThreadCount = MThreadUtils::getNumThreads();
    return std::max<std::size_t>( 1, std::min<std::size_t>( 2, mayaThreadCount ) );
}

/**
 * Validates each channel label in channelLabels against the set of acceptable channel labels.
 *
 * @param		channelMap	A set of channel labels to be validated.
 * @param[out]	badChannel	The name of the first invalid channel found.
 *
 * @return True if all channels are valid, false otherwise. If false is returned, badChannel contains the first invalid
 * label.
 */
bool is_valid_channel_map( const std::set<frantic::tstring>& channelLabels, frantic::tstring& badChannel ) {
    std::set<frantic::tstring> acceptedChannels = list_of( _T("Velocity") )( _T("MaterialID") )( _T("SmoothingGroup") )(
        _T("Color") )( _T("TextureCoord") )( _T("Normal") )( _T("EdgeSharpness") )( _T("VertexSharpness") );
    boost::basic_regex<frantic::tchar> expression(
        _T("Mapping([2-9]|[1-9][0-9])") ); // Defines the set of strings from 'Mapping2' through 'Mapping99'.

    BOOST_FOREACH( const frantic::tstring& channel, channelLabels ) {
        if( acceptedChannels.count( channel ) == 0 ) {
            // Before we decide it's an invalid channel, check if it's "Mapping2" through "Mapping99"
            boost::match_results<frantic::tstring::const_iterator> what;
            if( !( boost::regex_match( channel, what, expression ) ) ) {
                badChannel = channel;
                return false; // Early return if an invalid label is found.
            }
        }
    }

    return true;
}

template <class OutputIterator>
void get_csv_string_items( const frantic::tstring& input, OutputIterator out ) {
    std::vector<frantic::tstring> splitChannelLabels;

    frantic::strings::split( input, splitChannelLabels, _T(",") );

    for( unsigned i = 0; i < splitChannelLabels.size(); ++i ) {
        *out = frantic::strings::trim( splitChannelLabels[i] );
        ++out;
    }
}

/**
 * Converts a string of comma-separated values into a set of strings. Trims whitespace around values.
 *
 * @param		input	A string of comma-separated values.
 *
 * @return A set of the contained values.
 */
void convert_csv_string_to_set( const frantic::tstring& input, std::set<frantic::tstring>& output ) {
    std::set<frantic::tstring> channelMap;
    get_csv_string_items( input, std::inserter( channelMap, channelMap.begin() ) );
    output = channelMap;
}

void assert_indices_in_bounds( const frantic::geometry::polymesh3_ptr mesh ) {
    using frantic::geometry::polymesh3;

    for( polymesh3::iterator i = mesh->begin(), ie = mesh->end(); i != ie; ++i ) {
        if( i->second.is_vertex_channel() ) {
            const frantic::tstring vertexChannelName( i->first );
            frantic::geometry::polymesh3_const_vertex_accessor<void> acc =
                mesh->get_const_vertex_accessor( vertexChannelName );
            if( acc.has_custom_faces() ) {
                const std::size_t vertexCount = acc.vertex_count();
                for( std::size_t faceIndex = 0, faceIndexEnd = acc.face_count(); faceIndex < faceIndexEnd;
                     ++faceIndex ) {
                    frantic::geometry::polymesh3_const_face_range f = acc.get_face( faceIndex );
                    for( frantic::geometry::polymesh3_const_face_iterator i = f.first, ie = f.second; i != ie; ++i ) {
                        if( *i < 0 ) {
                            throw std::runtime_error( "assert_indices_in_bounds Error: negative index (" +
                                                      boost::lexical_cast<std::string>( *i ) +
                                                      ") in vertex channel \"" +
                                                      frantic::strings::to_string( vertexChannelName ) + "\"" );
                        }
                        if( *i >= vertexCount ) {
                            throw std::runtime_error( "assert_indices_in_bounds Error: index out of bounds (" +
                                                      boost::lexical_cast<std::string>( *i ) +
                                                      " >= " + boost::lexical_cast<std::string>( vertexCount ) +
                                                      ") in vertex channel \"" +
                                                      frantic::strings::to_string( vertexChannelName ) + "\"" );
                        }
                    }
                }
            }
        }
    }
}

void assert_valid( const frantic::geometry::polymesh3_ptr mesh ) { assert_indices_in_bounds( mesh ); }

void get_paths( MArgDatabase& argData, std::vector<MString>& outPaths ) {
    MStatus stat;

    const unsigned int flagUses = argData.numberOfFlagUses( "-p" );

    outPaths.clear();
    outPaths.reserve( flagUses );

    for( unsigned int i = 0; i < flagUses; ++i ) {
        MArgList pathArgs;
        stat = argData.getFlagArgumentList( "-p", i, pathArgs );
        MString path = pathArgs.asString( 0, &stat );
        if( !stat ) {
            throw std::runtime_error( "Unable to get -p flag argument" );
        }
        outPaths.push_back( path );
    }
}

MString get_path( MArgDatabase& argData ) {
    std::vector<MString> paths;
    get_paths( argData, paths );
    if( paths.size() < 1 ) {
        throw std::runtime_error( "Must specify a path to save to" );
    }
    return paths[0];
}

bool get_world_space( MArgDatabase& argData ) {
    bool worldSpace = false;
    if( argData.isFlagSet( "-ws" ) ) {
        worldSpace = true;
    }
    return worldSpace;
}

bool get_visible_only( MArgDatabase& argData ) {
    MStatus stat;
    bool visibleOnly = false;
    if( argData.isFlagSet( "-vis" ) ) {
        visibleOnly = true;
    }
    return visibleOnly;
}

std::size_t get_material_id_map_count( const MArgDatabase& argData ) {
    return static_cast<std::size_t>( argData.numberOfFlagUses( "-mm" ) );
}

void get_material_id_map( MArgDatabase& argData, std::size_t index, material_id_map& outMaterialIDMap ) {
    MStatus stat;
    MArgList arg;
    stat = argData.getFlagArgumentList( "-mm", static_cast<unsigned int>( index ), arg );
    if( !stat ) {
        throw std::runtime_error( "get_material_id_map Error: unable to get materialIDMap flag argument list" );
    }
    MString mm = arg.asString( 0, &stat );
    if( !stat ) {
        throw std::runtime_error( "get_material_id_map Error: unable to get materialIDMap flag argument" );
    }
    if( mm.length() > 0 ) {
        parse_material_id_map( outMaterialIDMap, frantic::maya::from_maya_t( mm ) );
        outMaterialIDMap.lock();
    }
}

void get_material_id_map( MArgDatabase& argData, material_id_map& outMaterialIDMap ) {
    if( get_material_id_map_count( argData ) > 0 ) {
        get_material_id_map( argData, 0, outMaterialIDMap );
    }
}

void get_channel_propagation_policy( MArgDatabase& argData, frantic::channels::channel_propagation_policy& outCPP ) {
    MStatus stat;

    typedef std::set<frantic::tstring> channel_map_t;
    channel_map_t channelMap;
    if( argData.isFlagSet( "-cm" ) ) {
        MString cmMString;
        stat = argData.getFlagArgument( "-cm", 0, cmMString );
        if( !stat ) {
            throw std::runtime_error( "get_channel_propagation_policy Error: unable to get channelMap flag argument" );
        }
        convert_csv_string_to_set( frantic::maya::from_maya_t( cmMString ), channelMap );
    } else {
        channelMap = list_of( _T("Velocity") )( _T("MaterialID") )( _T("SmoothingGroup") )( _T("TextureCoord") )
                         .convert_to_container<channel_map_t>();
    }
    frantic::tstring badChannel;
    if( !is_valid_channel_map( channelMap, badChannel ) ) {
        throw std::runtime_error(
            "get_channel_propagation_policy Error: channel list value contained an invalid channel label \'" +
            frantic::strings::to_string( badChannel ) + "\'." );
    }

    outCPP.set_to_include_policy();
    outCPP.set_channels( channelMap );
}

void assert_valid_channels( const std::set<frantic::tstring>& channels ) {
    frantic::tstring badChannel;
    if( !is_valid_channel_map( channels, badChannel ) ) {
        throw std::runtime_error(
            "assert_valid_channels Error: channel list value contained an invalid channel label \'" +
            frantic::strings::to_string( badChannel ) + "\'." );
    }
}

void assert_valid_channel_parameters( const std::map<frantic::tstring, frantic::tstring>& channelParameters ) {
    for( std::map<frantic::tstring, frantic::tstring>::const_iterator i = channelParameters.begin();
         i != channelParameters.end(); ++i ) {
        if( i->first == _T("TextureCoord") ) {
            if( i->second == _T("currentUVSet") ) {
                continue; // pass
            }
        } else if( i->first == _T("Color") ) {
            if( i->second == _T("currentColorSet") ) {
                continue; // pass
            }
        }
        throw std::runtime_error( "assert_valid_channel_parameters Error: invalid parameter for channel \"" +
                                  frantic::strings::to_string( i->first ) + "\": \"" +
                                  frantic::strings::to_string( i->second ) + "\"" );
    }
}

void get_channel_propagation_policy_and_parameters(
    MArgDatabase& argData, frantic::channels::channel_propagation_policy& outCPP,
    std::map<frantic::tstring, frantic::tstring>& outChannelParameters ) {
    MStatus stat;

    typedef std::vector<frantic::tstring> channel_map_t;
    channel_map_t channelMapEntries;
    if( argData.isFlagSet( "-cm" ) ) {
        MString cmString;
        stat = argData.getFlagArgument( "-cm", 0, cmString );
        if( !stat ) {
            throw std::runtime_error( "get_channel_propagation_policy Error: unable to get channelMap flag argument" );
        }
        get_csv_string_items( frantic::maya::from_maya_t( cmString ), std::back_inserter( channelMapEntries ) );
    } else {
        channelMapEntries = list_of( _T("Velocity") )( _T("MaterialID") )( _T("SmoothingGroup") )( _T("TextureCoord") )
                                .convert_to_container<channel_map_t>();
    }

    std::set<frantic::tstring> channels;
    std::map<frantic::tstring, frantic::tstring> channelParameters;
    BOOST_FOREACH( const frantic::tstring& channelMapEntry, channelMapEntries ) {
        std::vector<frantic::tstring> splitEntry;
        frantic::strings::split( channelMapEntry, splitEntry, _T("=") );
        const frantic::tstring channelName = frantic::strings::trim( splitEntry[0] );
        if( splitEntry.size() == 2 ) {
            channelParameters[channelName] = frantic::strings::trim( splitEntry[1] );
        } else if( splitEntry.size() > 2 ) {
            throw std::runtime_error( "Expected zero or one \'=\' per entry in channelMap, but found " +
                                      boost::lexical_cast<std::string>( splitEntry.size() - 1 ) +
                                      " in entry for channel \"" + frantic::strings::to_string( channelName ) + "\"" );
        }
        if( channels.count( channelName ) == 0 ) {
            channels.insert( channelName );
        } else {
            throw std::runtime_error( "Duplicate channel \"" + frantic::strings::to_string( channelName ) +
                                      "\" in channelMap" );
        }
    }

    assert_valid_channels( channels );
    assert_valid_channel_parameters( channelParameters );

    outCPP.set_to_include_policy();
    outCPP.set_channels( channels );

    std::swap( channelParameters, outChannelParameters );
}

std::size_t get_object_list_count( const MArgDatabase& argData ) {
    return static_cast<std::size_t>( argData.numberOfFlagUses( "-o" ) );
}

void get_object_list( const MArgDatabase& argData, std::size_t index, std::vector<MDagPath>& outDagPaths ) {
    MStatus stat;
    MArgList objectArgs;
    stat = argData.getFlagArgumentList( "-o", static_cast<unsigned int>( index ), objectArgs );
    if( !stat ) {
        throw std::runtime_error( "get_object_list Error: unable to get -o flag argument list" );
    }
    MString objects = objectArgs.asString( 0, &stat );
    if( !stat ) {
        throw std::runtime_error( "get_object_list Error: unable to get -o argument" );
    } else {
        std::vector<frantic::tstring> pathStrings;
        get_csv_string_items( frantic::maya::from_maya_t( objects ), std::back_inserter( pathStrings ) );
        MSelectionList selectionList;
        BOOST_FOREACH( const frantic::tstring& pathString, pathStrings ) {
            stat = selectionList.add( MString( pathString.c_str() ) );
            if( !stat ) {
                throw std::runtime_error( "get_object_list Error: unable to add string to selection list: " +
                                          frantic::strings::to_string( pathString ) );
            }
        }
        get_selected_mesh_shapes( selectionList, outDagPaths );
    }
}

bool get_enable_color_from_current_color_set( const std::map<frantic::tstring, frantic::tstring>& channelParameters ) {
    std::map<frantic::tstring, frantic::tstring>::const_iterator i = channelParameters.find( _T("Color") );
    if( i != channelParameters.end() && i->second == _T("currentColorSet") ) {
        return true;
    }
    return false;
}

bool get_enable_texture_coord_from_current_uv_set(
    const std::map<frantic::tstring, frantic::tstring>& channelParameters ) {
    std::map<frantic::tstring, frantic::tstring>::const_iterator i = channelParameters.find( _T("TextureCoord") );
    if( i != channelParameters.end() && i->second == _T("currentUVSet") ) {
        return true;
    }
    return false;
}

void zero_channels( frantic::geometry::polymesh3_ptr mesh ) {
    for( frantic::geometry::polymesh3::iterator ch( mesh->begin() ), chEnd( mesh->end() ); ch != chEnd; ++ch ) {
        if( ch->first == _T("verts") ) {
            continue;
        }
        const std::size_t elementSize = ch->second.element_size();
        if( ch->second.is_vertex_channel() ) {
            frantic::geometry::polymesh3_vertex_accessor<void> acc( mesh->get_vertex_accessor( ch->first ) );
            for( std::size_t i( 0 ), ie( acc.vertex_count() ); i != ie; ++i ) {
                memset( acc.get_vertex( i ), 0, elementSize );
            }
        } else {
            frantic::geometry::polymesh3_face_accessor<void> acc( mesh->get_face_accessor( ch->first ) );
            for( std::size_t i( 0 ), ie( acc.face_count() ); i != ie; ++i ) {
                memset( acc.get_face( i ), 0, elementSize );
            }
        }
    }
}

boost::shared_ptr<frantic::logging::progress_logger> create_progress_logger() {
    if( MGlobal::mayaState() == MGlobal::kInteractive ) {
        return boost::make_shared<progress_bar_progress_logger>();
    } else {
        // Use a null_progress_logger when in non-interactive mode.
        // This is intended to fix a '"$gMainProgressBar" is an undeclared variable'
        // error when saving through mayapy.
        return boost::make_shared<frantic::logging::null_progress_logger>();
    }
}

} // anonymous namespace

////////////////////////////////////
// SaveXMesh Command (legacy)
////////////////////////////////////

void* SaveXMeshCommand::creator() { return new SaveXMeshCommand; }

MSyntax SaveXMeshCommand::newSyntax() {
    MSyntax syntax;
    // syntax.enableEdit();
    // syntax.enableQuery();
    syntax.addFlag( "-p", "-path", MSyntax::kString );
    syntax.addFlag( "-sv", "-saveVelocity", MSyntax::kBoolean );
    syntax.addFlag( "-vo", "-vertsOnly", MSyntax::kNoArg );
    syntax.addFlag( "-ws", "-worldSpace", MSyntax::kNoArg );
    syntax.addFlag( "-mm", "-materialIDMap", MSyntax::kString );
    syntax.addFlag( "-cm", "-channelMap", MSyntax::kString );
    syntax.addFlag( "-vis", "-visibleOnly", MSyntax::kNoArg );
    return syntax;
}

MStatus SaveXMeshCommand::doIt( const MArgList& args ) {
    try {
        struct profiling_sections ps;

        ps.frameTime.enter();

        MStatus stat;

        MArgDatabase argData( syntax(), args, &stat );
        CHECK_MSTATUS_AND_RETURN_IT( stat );

        MString path = get_path( argData );

        bool vertsOnly = false;
        if( argData.isFlagSet( "-vo" ) )
            vertsOnly = true;

        bool worldSpace = get_world_space( argData );

        bool saveVelocity = false;
        if( argData.isFlagSet( "-sv" ) ) {
            stat = argData.getFlagArgument( "-sv", 0, saveVelocity );
            CHECK_MSTATUS_AND_RETURN_IT( stat );
        }

        material_id_map materialIDMap;
        get_material_id_map( argData, materialIDMap );

        frantic::channels::channel_propagation_policy cpp;
        std::map<frantic::tstring, frantic::tstring> channelParameters;
        get_channel_propagation_policy_and_parameters( argData, cpp, channelParameters );

        bool colorFromCurrentColorSet = get_enable_color_from_current_color_set( channelParameters );
        bool textureCoordFromCurrentUVSet = get_enable_texture_coord_from_current_uv_set( channelParameters );

        bool visibleOnly = get_visible_only( argData );

        std::vector<MDagPath> selectedDagPaths;
        get_selected_mesh_shapes( selectedDagPaths );

        std::vector<MDagPath> dagPaths;
        if( visibleOnly ) {
            boost::algorithm::copy_if( selectedDagPaths.begin(), selectedDagPaths.end(), std::back_inserter( dagPaths ),
                                       is_visible );
        } else {
            dagPaths = selectedDagPaths;
        }

        frantic::geometry::polymesh3_ptr mesh;
        ps.meshGetTime.enter();
        if( vertsOnly ) {
            mesh = create_combined_polymesh3_from_verts( dagPaths, worldSpace );
        } else {
            mesh = create_combined_polymesh3(
                dagPaths, worldSpace, colorFromCurrentColorSet, textureCoordFromCurrentUVSet,
                reinterpret_cast<std::map<std::string, std::vector<boost::uint32_t>>*>( NULL ), materialIDMap, cpp );
        }
        ps.meshGetTime.exit();

        frantic::geometry::transform( mesh, frantic::maya::graphics::FromMayaSpace );

        frantic::geometry::xmesh_metadata metadata;
        set_metadata_from_scene( metadata );
        metadata.set_boundbox( frantic::geometry::compute_boundbox( mesh ) );

        assert_valid( mesh );

        ps.meshWriteTime.enter();
        frantic::geometry::write_polymesh_file( frantic::maya::from_maya_t( path ), mesh, metadata );
        ps.meshWriteTime.exit();
        ps.frameTime.exit();

        FF_LOG( stats ) << ps << endl;

    } catch( std::exception& e ) {
        FF_LOG( error ) << e.what() << endl;
        return MStatus::kFailure;
    }

    return MStatus::kSuccess;
}

////////////////////////////////////
// Save XMesh Sequence Command
////////////////////////////////////

void* SaveXMeshSequenceCommand::creator() { return new SaveXMeshSequenceCommand; }

MSyntax SaveXMeshSequenceCommand::newSyntax() {
    MSyntax syntax;
    syntax.addFlag( "-p", "-path", MSyntax::kString );
    syntax.makeFlagMultiUse( "-p" );
    syntax.addFlag( "-ws", "-worldSpace", MSyntax::kNoArg );
    syntax.addFlag( "-mm", "-materialIDMap", MSyntax::kString );
    syntax.makeFlagMultiUse( "-mm" );
    syntax.addFlag( "-fr", "-frameRange", MSyntax::kDouble, MSyntax::kDouble );
    syntax.addFlag( "-s", "-step", MSyntax::kDouble );
    syntax.addFlag( "-cm", "-channelMap", MSyntax::kString );
    syntax.addFlag( "-vis", "-visibleOnly", MSyntax::kNoArg );
    syntax.addFlag( "-o", "-objects", MSyntax::kString );
    syntax.makeFlagMultiUse( "-o" );
    return syntax;
}

MStatus SaveXMeshSequenceCommand::doIt( const MArgList& args ) {
    try {
        set_time_on_scope_exit resetTimeOnScopeExit( MAnimControl::currentTime() );

        MStatus stat;

        frantic::diagnostics::profiling_section psTotal( _T("Total") );
        psTotal.enter();

        MArgDatabase argData( syntax(), args, &stat );
        CHECK_MSTATUS_AND_RETURN_IT( stat );

        std::vector<SequenceSaverHelper> sshs;

        // size of sshs is determined by the number of paths
        { // scope for paths
            std::vector<MString> paths;
            get_paths( argData, paths );
            if( paths.size() == 0 ) {
                throw std::runtime_error( "Must specify a path to save to" );
            }
            sshs.resize( paths.size() );
            for( std::size_t i = 0; i < paths.size(); ++i ) {
                sshs[i].m_filenamePattern.set( frantic::maya::from_maya_t( paths[i] ) );
            }
        }

        BOOST_FOREACH( SequenceSaverHelper& ssh, sshs ) {
            ssh.m_xss.set_thread_count( get_io_thread_count() );
            ssh.m_xss.set_compression_level( 1 ); // Z_BEST_SPEED
        }

        const bool worldSpace = get_world_space( argData );

        const std::size_t materialIDMapCount = get_material_id_map_count( argData );
        if( materialIDMapCount > 0 ) {
            if( materialIDMapCount == sshs.size() ) {
                for( std::size_t i = 0; i < materialIDMapCount; ++i ) {
                    get_material_id_map( argData, i, sshs[i].m_materialIDMap );
                }
            } else {
                throw std::runtime_error( "Mismatch between number of paths, and number of materialIDMaps" );
            }
        }

        MTime startTime = MAnimControl::minTime();
        MTime endTime = MAnimControl::maxTime();
        MTime timeStep = MTime( 1.0, MTime::uiUnit() );

        if( argData.isFlagSet( "-fr" ) ) {
            double startFrame;
            stat = argData.getFlagArgument( "-fr", 0, startFrame );
            CHECK_MSTATUS_AND_RETURN_IT( stat );
            startTime = MTime( startFrame, MTime::uiUnit() );

            double endFrame;
            stat = argData.getFlagArgument( "-fr", 1, endFrame );
            CHECK_MSTATUS_AND_RETURN_IT( stat );
            endTime = MTime( endFrame, MTime::uiUnit() );
        }

        if( endTime < startTime ) {
            throw std::runtime_error( "frameRange\'s first argument (start frame) must be less than or equal to its "
                                      "second argument (end frame)" );
        }

        if( argData.isFlagSet( "-s" ) ) {
            double step;
            stat = argData.getFlagArgument( "-s", 0, step );
            CHECK_MSTATUS_AND_RETURN_IT( stat );
            timeStep = MTime( step, MTime::uiUnit() );
        }

        if( timeStep <= MTime( 0.0 ) ) {
            throw std::runtime_error( "step must be a positive number" );
        }

        frantic::channels::channel_propagation_policy cpp;
        std::map<frantic::tstring, frantic::tstring> channelParameters;
        get_channel_propagation_policy_and_parameters( argData, cpp, channelParameters );

        bool colorFromCurrentColorSet = get_enable_color_from_current_color_set( channelParameters );
        bool textureCoordFromCurrentUVSet = get_enable_texture_coord_from_current_uv_set( channelParameters );

        frantic::channels::channel_propagation_policy cppWithoutVelocity( cpp );
        exclude_channel( cppWithoutVelocity, _T("Velocity") );

        const bool visibleOnly = get_visible_only( argData );

        const std::size_t objectListCount = get_object_list_count( argData );
        if( objectListCount == 0 ) {
            if( sshs.size() == 1 ) {
                get_selected_mesh_shapes( sshs[0].m_dagPaths );
            } else {
                throw std::runtime_error( "More than one path specified, but no objects were specified" );
            }
        } else if( objectListCount == sshs.size() ) {
            for( std::size_t i = 0; i < objectListCount; ++i ) {
                get_object_list( argData, i, sshs[i].m_dagPaths );
            }
        } else {
            throw std::runtime_error( "Mismatch between number of paths, and number of objects to save." );
        }

        boost::shared_ptr<frantic::logging::progress_logger> logger = create_progress_logger();
        logger->set_title( _T("Saving...") );

        std::vector<MTime> sampleTimes;
        for( MTime t = startTime; t <= endTime; t += timeStep ) {
            sampleTimes.push_back( t );
        }
        if( sampleTimes.empty() || sampleTimes.back() != endTime ) {
            sampleTimes.push_back( endTime );
        }

        for( std::size_t sampleIndex = 0; sampleIndex < sampleTimes.size(); ++sampleIndex ) {
            struct profiling_sections ps;
            ps.frameTime.enter();

            FF_LOG( progress ) << "Saving frame "
                               << boost::lexical_cast<frantic::tstring>(
                                      sampleTimes[sampleIndex].as( MTime::uiUnit() ) )
                               << endl;

            MTime t( sampleTimes[sampleIndex] );
            MGlobal::viewFrame( t );

            std::vector<frantic::geometry::polymesh3_ptr> allMeshes;
            std::vector<MDagPath> allDagPaths;

            ps.meshGetTime.enter();

            BOOST_FOREACH( SequenceSaverHelper& ssh, sshs ) {
                ssh.m_meshes.clear();
                BOOST_FOREACH( MDagPath& dagPath, ssh.m_dagPaths ) {
                    if( !visibleOnly || is_visible( dagPath ) ) {
                        frantic::geometry::polymesh3_ptr mesh = create_polymesh3(
                            dagPath, worldSpace, colorFromCurrentColorSet, textureCoordFromCurrentUVSet,
                            &ssh.m_smoothingGroupAssignments, &ssh.m_materialIDMap, cppWithoutVelocity );
                        ssh.m_meshes.push_back( mesh );
                        allMeshes.push_back( mesh );
                        allDagPaths.push_back( dagPath );
                    }
                }
            }

            // get velocity
            // we want to do this for all meshes simultaneously to avoid extra
            // time changes (viewFrame() calls)
            if( cpp.is_channel_included( _T("Velocity") ) ) {
                try_create_velocity_channel( t, worldSpace, allDagPaths, allMeshes );
            }

            ps.meshGetTime.exit();

            BOOST_FOREACH( SequenceSaverHelper& ssh, sshs ) {
                frantic::geometry::polymesh3_ptr mesh = frantic::geometry::combine( ssh.m_meshes );
                ssh.m_meshes.clear();

                frantic::geometry::transform( mesh, frantic::maya::graphics::FromMayaSpace );

                frantic::geometry::xmesh_metadata metadata;
                set_metadata_from_scene( metadata );
                metadata.set_boundbox( frantic::geometry::compute_boundbox( mesh ) );

                assert_valid( mesh );

                const double frameNumber = t.asUnits( MTime::uiUnit() );

                const frantic::tstring filename = ssh.m_filenamePattern[frameNumber];
                const frantic::tstring filenameExt =
                    frantic::strings::to_lower( frantic::files::extension_from_path( filename ) );
                if( filenameExt == _T(".xmesh") ) {
                    ps.meshWriteTime.enter();
                    ssh.m_xss.write_xmesh( mesh, metadata, filename );
                    ps.meshWriteTime.exit();
                } else {
                    throw std::runtime_error( "Unrecognized extension \'" + frantic::strings::to_string( filenameExt ) +
                                              "\' in path.  Valid extensions are: \'.xmesh\'" );
                }
            }

            logger->update_progress( sampleIndex + 1, sampleTimes.size() );

            ps.frameTime.exit();

            FF_LOG( stats ) << ps << endl;
        }

        psTotal.exit();
        FF_LOG( stats ) << psTotal << endl;
        // refactor plans
        //
        // vertsOnly? keep or toss
        // saveVelocity? keep or toss? use in channel propagation policy
        // mesh mode:
        // combine + worldspace. this only makes sense once done as callback.
        // ("Velocity","Color","MaterialID","Normals","TextureCoord","Selection","FaceSelection") //check these channels

        //			m_xss.set_compression_level( get_zlib_compression_level() );
        //			m_xss.set_thread_count( m_threadCount );
    } catch( const std::exception& e ) {
        FF_LOG( error ) << e.what() << endl;
        return MStatus::kFailure;
    }

    return MStatus::kSuccess;
}

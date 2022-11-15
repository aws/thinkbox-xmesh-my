// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#ifndef _BOOL
#define _BOOL
#endif

#include <iostream>

#include "material_utils.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include <maya/MDagPath.h>
#include <maya/MFnMesh.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>

#include <frantic/maya/convert.hpp>

#include "material_id_map.hpp"

namespace {

unsigned int get_instance_number( const MDagPath& dagPath ) {
    MStatus stat;

    bool isInstanced = dagPath.isInstanced( &stat );
    if( !stat ) {
        throw std::runtime_error( "get_instance_number Error: error calling isInstanced()" );
    }

    if( isInstanced ) {
        unsigned int instanceNumber = dagPath.instanceNumber( &stat );
        if( !stat ) {
            throw std::runtime_error( "get_instance_number Error: error calling instanceNumber()" );
        }
        return instanceNumber;
    } else {
        return 0;
    }
}

bool try_get_shading_engine_name( const MFnDependencyNode& shadingEngineObject, frantic::tstring& outName ) {
    MStatus stat;
    const frantic::tstring shadingEngineName = frantic::maya::from_maya_t( shadingEngineObject.name( &stat ) );
    if( stat ) {
        outName = shadingEngineName;
        return true;
    } else {
        return false;
    }
}

bool try_get_surface_shader_name( const MFnDependencyNode& shadingEngineObject, frantic::tstring& outName ) {
    MStatus stat;

    MPlug plug( shadingEngineObject.findPlug( "surfaceShader", &stat ) );

    if( !stat )
        return false;

    MPlugArray connectionPlugs;
    bool isConnected = plug.connectedTo( connectionPlugs, true, false, &stat );
    if( !stat )
        return false;

    if( isConnected && connectionPlugs.length() == 1 ) {
        MFnDependencyNode shaderNode( connectionPlugs[0].node(), &stat );
        if( !stat )
            return false;
        const frantic::tstring surfaceShaderName = frantic::maya::from_maya_t( shaderNode.name( &stat ) );
        if( !stat )
            return false;
        outName = surfaceShaderName;
        return true;
    }

    return false;
}

boost::uint16_t get_material_id( material_id_map& materialIDMap, const MFnDependencyNode& shadingEngineObject ) {
    frantic::tstring materialName;
    if( try_get_shading_engine_name( shadingEngineObject, materialName ) ) {
        if( materialIDMap.has_material( materialName ) ) {
            return materialIDMap.get_material_id( materialName );
        }
    }
    if( try_get_surface_shader_name( shadingEngineObject, materialName ) ) {
        return materialIDMap.get_material_id( materialName );
    }
    return materialIDMap.get_undefined_material_id();
}

} // anonymous namespace

void create_material_id_channel( const MDagPath& dagPath, MFnMesh& fnMesh, frantic::geometry::polymesh3_ptr& mesh,
                                 material_id_map& materialIDMap ) {
    const frantic::tstring materialIDChannelName( _T("MaterialID") );

    if( !mesh ) {
        throw std::runtime_error( "create_material_id_channel Error: mesh is NULL" );
    }

    const unsigned int instanceNumber = get_instance_number( dagPath );

    MObjectArray shaders;
    MIntArray indices;
    MStatus getConnectedShadersStat = fnMesh.getConnectedShaders( instanceNumber, shaders, indices );

    const std::size_t faceCount = mesh->face_count();

    frantic::graphics::raw_byte_buffer materialIDBuffer;
    materialIDBuffer.resize( faceCount * sizeof( boost::uint16_t ) );
    boost::uint16_t* materialIDArray = reinterpret_cast<boost::uint16_t*>( materialIDBuffer.begin() );

    if( getConnectedShadersStat ) {
        if( indices.length() != faceCount ) {
            throw std::runtime_error( "create_material_id_channel Error: number of shader indices (" +
                                      boost::lexical_cast<std::string>( indices.length() ) +
                                      ") does not match number of faces in the mesh (" +
                                      boost::lexical_cast<std::string>( faceCount ) + ")" );
        }

        std::vector<boost::uint16_t> shaderIndexToMaterialID;
        shaderIndexToMaterialID.reserve( shaders.length() );
        for( unsigned i = 0, ie = shaders.length(); i < ie; ++i ) {
            shaderIndexToMaterialID.push_back( get_material_id( materialIDMap, shaders[i] ) );
        }

        for( std::size_t faceIndex = 0; faceIndex < faceCount; ++faceIndex ) {
            const int shaderIndex = indices[static_cast<unsigned>( faceIndex )];
            if( shaderIndex >= 0 && shaderIndex < static_cast<int>( shaderIndexToMaterialID.size() ) ) {
                materialIDArray[faceIndex] = shaderIndexToMaterialID[shaderIndex];
            } else {
                materialIDArray[faceIndex] = materialIDMap.get_undefined_material_id();
            }
        }
    } else {
        const boost::uint16_t undefinedMaterialID = materialIDMap.get_undefined_material_id();
        for( std::size_t faceIndex = 0; faceIndex < faceCount; ++faceIndex ) {
            materialIDArray[faceIndex] = undefinedMaterialID;
        }
    }

    mesh->add_face_channel( materialIDChannelName, frantic::channels::data_type_uint16, 1, materialIDBuffer );
}

void parse_material_id_map( material_id_map& outMaterialIDMap, const frantic::tstring& s ) {
    material_id_map result;

    std::vector<frantic::tstring> lines;
    boost::algorithm::split( lines, s, boost::is_any_of( _T(",") ) );

    BOOST_FOREACH( frantic::tstring line, lines ) {
        boost::algorithm::trim( line );

        if( boost::algorithm::starts_with( line, _T("=") ) )
            throw std::runtime_error( "parse_material_id_map: entry must not begin with '='" );

        const std::size_t assignmentCharacterCount = std::count( line.begin(), line.end(), _T( '=' ) );
        if( assignmentCharacterCount != 1 )
            throw std::runtime_error( "parse_material_id_map: entry must contain exactly one '=', but found " +
                                      boost::lexical_cast<std::string>( assignmentCharacterCount ) + " instead." );

        std::vector<frantic::tstring> tokens;
        boost::algorithm::split( tokens, line, boost::is_any_of( "=" ) );
        if( tokens.size() == 0 )
            throw std::runtime_error( "parse_material_id_map: no tokens found in entry" );

        BOOST_FOREACH( frantic::tstring& token, tokens ) {
            boost::algorithm::trim( token );
        }

        int intID = 0;
        try {
            intID = boost::lexical_cast<int>( tokens[0] );
        } catch( boost::bad_lexical_cast& ) {
            throw std::runtime_error(
                "parse_material_id_map: left side of '=' must be an integer, but instead it is '" +
                frantic::strings::to_string( tokens[0] ) + "'" );
        }

        boost::uint16_t id = 0;
        try {
            id = boost::numeric_cast<boost::uint16_t>( intID );
        } catch( boost::bad_numeric_cast& e ) {
            throw std::runtime_error( std::string() + "parse_material_id_map: unable to convert id '" +
                                      frantic::strings::to_string( tokens[0] ) + "' to uint16: " + e.what() );
        }

        frantic::tstring name;
        if( tokens.size() > 1 ) {
            name = tokens[1];
        }

        if( name.empty() ) {
            if( result.has_undefined_material() ) {
                throw std::runtime_error(
                    "parse_material_id_map: found empty (undefined) material name more than once" );
            }
            result.insert_undefined_material( id );
        } else {
            if( result.has_material( name ) ) {
                throw std::runtime_error( "parse_material_id_map: found material name '" +
                                          frantic::strings::to_string( name ) + "' more than once" );
            }
            result.insert_material( id, name );
        }
    }

    outMaterialIDMap.swap( result );
}

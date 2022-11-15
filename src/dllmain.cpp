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

#include "SaveXMeshCommand.hpp"
#include "SequenceSaverHelper.hpp"
#include "SequenceXMeshGeometryOverride.hpp"
#include "SequenceXMeshNode.hpp"
#include "XMeshLoggingCommand.hpp"
#include "XMeshSaverUISettingsNode.hpp"

#include <maya/MGlobal.h>

#include <frantic/geometry/xmesh_sequence_saver.hpp>
#include <frantic/maya/plugin_manager.hpp>

#include <fstream>

#include "XMeshVersion.h"

#ifdef _WIN32
#define EXPORT __declspec( dllexport )
#elif __GNUC__ >= 4
#define EXPORT __attribute__( ( visibility( "default" ) ) )
#else
#define EXPORT
#endif

#ifdef _WIN32
HINSTANCE hInstance;

BOOL WINAPI DllMain( HINSTANCE hinstDLL, ULONG fdwReason, LPVOID /*lpvReserved*/ ) {
    if( fdwReason == DLL_PROCESS_ATTACH )
        hInstance = hinstDLL; // Hang on to this DLL's instance handle.
    return TRUE;
}
#endif

namespace {

std::string create_set_xmesh_render_command( const std::string& commandName, bool render ) {
    std::ostringstream ss;

    ss << "global proc " << commandName << "() {";
    ss << "  string $xmeshNodes[] = `ls -type \"sequenceXMesh\"`;";
    ss << "  for ($xmeshNode in $xmeshNodes) {";
    ss << "    if (`objExists ($xmeshNode + \".inRender\")`) {";
    ss << "      setAttr ($xmeshNode + \".inRender\") " << ( render ? "1" : "0" ) << ";";
    ss << "    }";
    ss << "  }";
    ss << "}";

    return ss.str().c_str();
}

bool command_exists( const std::string& commandName ) {
    MStatus stat;
    MString result;
    stat = MGlobal::executeCommand( ( "whatIs " + commandName ).c_str(), result );
    if( !stat ) {
        throw std::runtime_error( "unable to execute whatIs command" );
    }
    return result == "Command";
}

// This manages all data related to the plugin, so that it can easily be unloaded if anything goes wrong
frantic::maya::plugin_manager s_pluginManager;

} // anonymous namespace

using namespace frantic::maya;

// static MCallbackId beginRenderId = 0;
// static MCallbackId endRenderId = 0;

MStatus EXPORT initializePlugin( MObject obj ) {

    MStatus status;
    XMeshLoggingCommand::initialize_logging();
    s_pluginManager.initialize( obj, _T("Thinkbox Software"), frantic::strings::to_tstring( FRANTIC_VERSION ),
                                _T("Any") );

    try {
        // beginRenderId = MSceneMessage::addCallback( MSceneMessage::kBeforeSoftwareRender, beginRender);
        // endRenderId = MSceneMessage::addCallback( MSceneMessage::kAfterSoftwareRender, endRender);

        // Check if saveXMesh command already exists.  Without this check, that would
        // produce an "Unexpected Internal Failure" error.
        // I think this was happening to some beta testers who had the old XMeshLoader.mll
        // plugin installed.
        if( command_exists( "saveXMesh" ) ) {
            throw std::runtime_error( "saveXMesh command already exists.  Are you loading the XMesh plugin twice?" );
        }

        status =
            s_pluginManager.register_command( "saveXMesh", &SaveXMeshCommand::creator, &SaveXMeshCommand::newSyntax );
        CHECK_MSTATUS_AND_RETURN_IT( status );
        status = s_pluginManager.register_command( "saveXMeshSequence", &SaveXMeshSequenceCommand::creator,
                                                   &SaveXMeshSequenceCommand::newSyntax );
        CHECK_MSTATUS_AND_RETURN_IT( status );
        status = s_pluginManager.register_command( "xmeshLogging", &XMeshLoggingCommand::creator,
                                                   &XMeshLoggingCommand::newSyntax );
        CHECK_MSTATUS_AND_RETURN_IT( status );
        //		status = s_pluginManager.register_command( "clearXMeshCache", &ClearXMeshCommand::creator,
        //&ClearXMeshCommand::newSyntax ); 		CHECK_MSTATUS_AND_RETURN_IT(status); 		status =
        //s_pluginManager.register_command( "setXMeshActiveChannels", &SetXMeshChannelCommand::creator,
        //&SetXMeshChannelCommand::newSyntax ); 		CHECK_MSTATUS_AND_RETURN_IT(status);

        status = s_pluginManager.register_ui( "XMeshCreateUI", "XMeshDeleteUI" );
        CHECK_MSTATUS_AND_RETURN_IT( status );

        status = s_pluginManager.register_node( "sequenceXMesh", SequenceXMeshNode::typeID, &SequenceXMeshNode::creator,
                                                &SequenceXMeshNode::initialize, MPxNode::kLocatorNode,
                                                &SequenceXMeshNode::drawClassification );
        CHECK_MSTATUS_AND_RETURN_IT( status );

        status = s_pluginManager.register_node( "xmeshSaverUISettings", XMeshSaverUISettingsNode::typeId,
                                                &XMeshSaverUISettingsNode::creator,
                                                &XMeshSaverUISettingsNode::initialize, MPxNode::kDependNode );
        CHECK_MSTATUS_AND_RETURN_IT( status );

#if MAYA_API_VERSION >= 201300
        status = s_pluginManager.register_geometry_override_creator( SequenceXMeshNode::drawClassification,
                                                                     SequenceXMeshNode::drawRegistrantId,
                                                                     SequenceXMeshGeometryOverride::create );
        CHECK_MSTATUS_AND_RETURN_IT( status );
#endif

        // Define the xmeshPreRender and xmeshPostRender procs.  These procs
        // are used to switch between the viewport
        // We also do this using the kBeforeSoftwareRender and
        // kAfterSoftwareRender callbacks, but those only seem to work for the
        // Maya Software renderer.
        MGlobal::executeCommand( create_set_xmesh_render_command( "xmeshPreRender", true ).c_str() );
        MGlobal::executeCommand( create_set_xmesh_render_command( "xmeshPostRender", false ).c_str() );
    } catch( std::exception& e ) {
        // if we run into absolutely anything wrong, we should just bail and put up an error
        s_pluginManager.unregister_all();
        status.perror( e.what() );
        status = MStatus::kFailure;
    } catch( ... ) {
        // if we run into absolutely anything wrong, we should just bail and put up an error
        s_pluginManager.unregister_all();
        status.perror( "XMesh: Unknown exception thrown during initialization." );
        status = MStatus::kFailure;
    }

    return status;
}

MStatus EXPORT uninitializePlugin( MObject obj ) {
    s_pluginManager.unregister_all();
    // if (beginRenderId)
    //{
    //	MMessage::removeCallback(beginRenderId);
    //	beginRenderId = 0;
    // }
    //
    // if (endRenderId)
    //{
    //	MMessage::removeCallback(endRenderId);
    //	endRenderId = 0;
    // }

    return MS::kSuccess;
}

// Workaround for "unresolved external symbol __iob_func" linker error when
// using FlexLM with MSVC2015.
// This can be removed when we get a version of Flex which supports Visual Studio 2015
#if defined( WIN32 ) && MAYA_API_VERSION >= 201800
extern "C" {
FILE __iob_func[3] = { *stdin, *stdout, *stderr };
}
#endif

// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#ifndef _BOOL
#define _BOOL
#endif

#include "maya_xmesh_timing.hpp"

#include <maya/MPlug.h>
#include <maya/MStatus.h>
#include <maya/MTime.h>

#if MAYA_API_VERSION >= 201800
#include <maya/MDGContextGuard.h>
#endif

maya_xmesh_timing::maya_xmesh_timing()
    : m_enablePlaybackGraph( false ) {}

void maya_xmesh_timing::set_playback_graph( const MObject& node, const MObject& attribute ) {
    m_node = node;
    m_attribute = attribute;
    m_enablePlaybackGraph = true;
}

double maya_xmesh_timing::evaluate_playback_graph( double frame ) const {
    if( m_enablePlaybackGraph ) {
        MStatus stat;
        if( m_node.isNull() ) {
            throw std::runtime_error( "maya_xmesh_timing::evaluate_playback_graph Error: node is NULL" );
        }
        if( m_attribute.isNull() ) {
            throw std::runtime_error( "maya_xmesh_timing::evaluate_playback_graph Error: attribute is NULL" );
        }

        MDGContext context( MTime( frame, MTime::uiUnit() ) );
        MPlug playbackGraphPlug( m_node, m_attribute );

#if MAYA_API_VERSION >= 201800
        MDGContextGuard contextGuard( context );
        MTime result = playbackGraphPlug.asMTime( &stat );
#else
        MTime result = playbackGraphPlug.asMTime( context, &stat );
#endif
        if( !stat ) {
            throw std::runtime_error(
                "maya_xmesh_timing::evaluate_playback_graph Error: unable to evaluate playback graph" );
        }

        return result.as( MTime::uiUnit() );
    } else {
        return frame;
    }
}

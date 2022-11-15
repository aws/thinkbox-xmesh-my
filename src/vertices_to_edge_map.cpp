// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#ifndef _BOOL
#define _BOOL
#endif

#include "vertices_to_edge_map.hpp"

#include <algorithm>
#include <stdexcept>

#include <maya/MFnMesh.h>
#include <maya/MStatus.h>

namespace {

struct comp_first {
    bool operator()( const std::pair<int, int>& a, const std::pair<int, int>& b ) { return a.first < b.first; }
};

} // anonymous namespace

vertices_to_edge_map::vertices_to_edge_map( MObject& polyObject ) {
    MStatus stat;

    MFnMesh fnMesh( polyObject, &stat );
    if( !stat ) {
        throw std::runtime_error( "vertices_to_edge_map Error: unable to attach MFnMesh to object" );
    }

    const int vertexCount = fnMesh.numVertices();
    const int edgeCount = fnMesh.numEdges();

    // Each edge has two vertices.
    // Let lesserVertex = min(vertices).
    // m_lesserVertexOffset[lesserVertex] is the first index in m_greaterVertexToEdge
    // used by edges that have this lesserVertex.
    m_lesserVertexOffset.reserve( vertexCount + 1 );
    m_lesserVertexOffset.resize( vertexCount );

    for( int edgeIndex = 0; edgeIndex < edgeCount; ++edgeIndex ) {
        int2 vertices;
        stat = fnMesh.getEdgeVertices( edgeIndex, vertices );
        if( !stat ) {
            throw std::runtime_error( "vertices_to_edge_map Error: unable to get vertices" );
        }
        if( vertices[0] > vertices[1] ) {
            std::swap( vertices[0], vertices[1] );
        }
        ++m_lesserVertexOffset[vertices[0]];
    }

    // extra entry for one past the end
    m_lesserVertexOffset.push_back( 0 );

    int sum = 0;
    for( std::size_t i = 0; i < m_lesserVertexOffset.size(); ++i ) {
        const int val = m_lesserVertexOffset[i];
        m_lesserVertexOffset[i] = sum;
        sum += val;
    }

    std::vector<int> nextFreeIndex( m_lesserVertexOffset );

    // Each edge has two vertices.
    // Let greaterVertex = max(vertices).
    // m_greaterVertexToEdge is a list of (greaterVertex,edgeIndex) pairs.
    m_greaterVertexToEdge.clear();
    m_greaterVertexToEdge.resize( sum, std::pair<int, int>( -1, -1 ) );

    for( int edgeIndex = 0; edgeIndex < edgeCount; ++edgeIndex ) {
        int2 vertices;
        stat = fnMesh.getEdgeVertices( edgeIndex, vertices );
        if( !stat ) {
            throw std::runtime_error( "vertices_to_edge_map Error: unable to get vertices" );
        }
        if( vertices[0] > vertices[1] ) {
            std::swap( vertices[0], vertices[1] );
        }
        const int i = nextFreeIndex[vertices[0]];
        ++nextFreeIndex[vertices[0]];
        m_greaterVertexToEdge[i] = std::pair<int, int>( vertices[1], edgeIndex );
    }

    for( int vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex ) {
        std::sort( m_greaterVertexToEdge.begin() + m_lesserVertexOffset[vertexIndex],
                   m_greaterVertexToEdge.begin() + m_lesserVertexOffset[vertexIndex + 1], comp_first() );
    }
}

bool vertices_to_edge_map::get_edge( std::pair<int, int> vertices, int& outEdgeIndex ) const {
    typedef std::vector<std::pair<int, int>>::const_iterator iter_t;

    if( vertices.first > vertices.second ) {
        std::swap( vertices.first, vertices.second );
    }

    iter_t begin = m_greaterVertexToEdge.begin() + m_lesserVertexOffset[vertices.first];
    iter_t end = m_greaterVertexToEdge.begin() + m_lesserVertexOffset[vertices.first + 1];

    iter_t i = std::lower_bound( begin, end, std::pair<int, int>( vertices.second, 0 ), comp_first() );
    if( i != end && i->first == vertices.second ) {
        outEdgeIndex = i->second;
        return true;
    }

    return false;
}

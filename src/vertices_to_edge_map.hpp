// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#include <utility>
#include <vector>

#include <maya/MObject.h>

class vertices_to_edge_map {
  public:
    vertices_to_edge_map( MObject& polyObject );
    bool get_edge( std::pair<int, int> vertices, int& outEdgeIndex ) const;

  private:
    std::vector<int> m_lesserVertexOffset;
    std::vector<std::pair<int, int>> m_greaterVertexToEdge;
};

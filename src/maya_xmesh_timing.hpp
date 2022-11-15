// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#include <maya/MObject.h>

#include <xmesh/xmesh_timing.hpp>

class maya_xmesh_timing : public xmesh::xmesh_timing {
  public:
    maya_xmesh_timing();

    void set_playback_graph( const MObject& node, const MObject& attribute );

  protected:
    virtual double evaluate_playback_graph( double frame ) const;

  private:
    bool m_enablePlaybackGraph;
    MObject m_node;
    MObject m_attribute;
};

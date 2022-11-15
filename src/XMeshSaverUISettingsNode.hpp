// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <maya/MPxNode.h>

class XMeshSaverUISettingsNode : public MPxNode {
  public:
    XMeshSaverUISettingsNode( void );
    ~XMeshSaverUISettingsNode( void );

    static void* creator();
    static MStatus initialize();
    static MTypeId typeId;
};

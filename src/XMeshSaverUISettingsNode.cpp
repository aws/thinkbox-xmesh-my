// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#ifndef _BOOL
#define _BOOL
#endif

#include <iostream>

#include "XMeshSaverUISettingsNode.hpp"

MTypeId XMeshSaverUISettingsNode::typeId( 0x00117487 );

XMeshSaverUISettingsNode::XMeshSaverUISettingsNode( void ) {}

XMeshSaverUISettingsNode::~XMeshSaverUISettingsNode( void ) {}

void* XMeshSaverUISettingsNode::creator() { return new XMeshSaverUISettingsNode; }

MStatus XMeshSaverUISettingsNode::initialize() { return MStatus::kSuccess; }

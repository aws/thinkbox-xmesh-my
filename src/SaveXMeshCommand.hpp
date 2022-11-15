// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <maya/MArgDatabase.h>
#include <maya/MObject.h>
#include <maya/MPxCommand.h>

class SaveXMeshCommand : public MPxCommand {

  public:
    static void* creator();
    static MSyntax newSyntax();

    virtual MStatus doIt( const MArgList& args );
};

class SaveXMeshSequenceCommand : public MPxCommand {

  public:
    static void* creator();
    static MSyntax newSyntax();

    virtual MStatus doIt( const MArgList& args );
};

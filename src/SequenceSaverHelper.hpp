// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <frantic/files/filename_sequence.hpp>
#include <frantic/geometry/polymesh3.hpp>
#include <frantic/geometry/xmesh_sequence_saver.hpp>

#include <maya/MDagPath.h>

#include "material_id_map.hpp"

class SequenceSaverHelper {
  public:
    frantic::files::filename_pattern m_filenamePattern;
    frantic::geometry::xmesh_sequence_saver m_xss;
    std::map<std::string, std::vector<boost::uint32_t>>
        m_smoothingGroupAssignments; // utf8 dag path to previous smoothing group assignment
    material_id_map m_materialIDMap;
    std::vector<MDagPath> m_dagPaths;
    std::vector<frantic::geometry::polymesh3_ptr> m_meshes;

  public:
    void Clear();

    // outmesh metadata, sequencename[frame]
};

// extern //?
// SequenceSaverHelper& GetSequenceSaverHelper();

// class ClearXMeshCommand : public MPxCommand
//{
// public:
//     static void* creator();
//	static MSyntax newSyntax();
//
//	virtual MStatus doIt( const MArgList& args );
// };

// class SetXMeshChannelCommand : public MPxCommand
//{
// public:
//     static void* creator();
//	static MSyntax newSyntax();
//
//	virtual MStatus doIt( const MArgList& args );
// };

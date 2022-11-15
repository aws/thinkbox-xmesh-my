// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#include <maya/MDagPath.h>
#include <maya/MFnMesh.h>

#include <frantic/geometry/polymesh3.hpp>

class material_id_map;

void create_material_id_channel( const MDagPath& dagPath, MFnMesh& fnMesh, frantic::geometry::polymesh3_ptr& mesh,
                                 material_id_map& materialIDMap );

void parse_material_id_map( material_id_map& materialIDMap, const frantic::tstring& s );

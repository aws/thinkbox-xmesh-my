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

#include "SequenceSaverHelper.hpp"

#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MGlobal.h>
#include <maya/MSyntax.h>

// SequenceSaverHelper& GetSequenceSaverHelper() {
//	static SequenceSaverHelper theSequenceSaverHelper;
//	return theSequenceSaverHelper;
// }

void SequenceSaverHelper::Clear() {
    // MGlobal::displayInfo("\tSequenceSaverHelper::Clear()");
    m_filenamePattern = frantic::files::filename_pattern();
    m_xss.clear();
    m_smoothingGroupAssignments.clear();
    m_materialIDMap.clear();
    m_dagPaths.clear();
    m_meshes.clear();
}

//------------------------------------------------------------
// void* ClearXMeshCommand::creator(){
//	//MGlobal::displayInfo("SequenceSaverHelper::creator()");
//	return new ClearXMeshCommand;
//}
//
//
// MSyntax ClearXMeshCommand::newSyntax(){
//	MSyntax syntax;
//	syntax.enableEdit(); //todo
//	syntax.enableQuery(); //todo
//	return syntax;
//}
//
// MStatus ClearXMeshCommand::doIt( const MArgList& /*args*/ ){
//
//	MStatus stat;
//	//MGlobal::displayInfo("ClearXMeshCommand");
//	SequenceSaverHelper& ssh = GetSequenceSaverHelper();
//	ssh.Clear();
//	return stat;
//}

//------------------------------------------------------------
// void* SetXMeshChannelCommand::creator(){
//	return new SetXMeshChannelCommand;
//}
//
// MSyntax SetXMeshChannelCommand::newSyntax(){
//	MSyntax syntax;
//	syntax.enableEdit();
//	syntax.enableQuery();
//	syntax.addArg(MSyntax::kString);
//	syntax.addFlag( "-g", "-get", MSyntax::kString);
//	syntax.setObjectType(MSyntax::kStringObjects);
//	return syntax;
//}
//
////setXMeshActiveChannels
// MStatus SetXMeshChannelCommand::doIt( const MArgList& args ){
//
//	MStatus stat;
//	char buf [4000];
//	MArgDatabase argData(syntax(),args,&stat);
//	MArgList argList;
//	unsigned int i, idx;
//	argData.getFlagArgumentList("-g", i, argList);
//	for (int j = 0; j < argData.numberOfFlagUses("-g"); j++)
//	{
//		MString m;
//		argData.getFlagArgument("-g", j, m);
//		MGlobal::displayInfo(m);
//	}
//
//	idx = 0;
//
//	MStringArray str_ary = args.asStringArray( idx, &stat);
//	//MGlobal::displayInfo("ClearXMeshCommand");
//	SequenceSaverHelper& ssh = GetSequenceSaverHelper();
//	ssh.cpp.set_to_exclude_policy();
//
//	//if( !argData.isFlagSet( "-p" ) ){
//	MGlobal::displayInfo(str_ary[0]);
//		return MStatus::kFailure;
//	//}
//	return stat;
// }

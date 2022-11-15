// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <maya/MArgDatabase.h>
#include <maya/MPxCommand.h>

#include <frantic/strings/tstring.hpp>

class XMeshLoggingCommand : public MPxCommand {
  public:
    static void* creator();
    static MSyntax newSyntax();
    static void initialize_logging();

    virtual MStatus doIt( const MArgList& args );

  private:
    static void to_error_log( const frantic::tchar* szMsg );
    static void to_warning_log( const frantic::tchar* szMsg );
    static void to_debug_log( const frantic::tchar* szMsg );
    static void to_stats_log( const frantic::tchar* szMsg );
    static void to_progress_log( const frantic::tchar* szMsg );
};

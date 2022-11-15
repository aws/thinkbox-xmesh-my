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

#include <iostream>
using std::cerr;
using std::endl;

#include "XMeshLoggingCommand.hpp"

#include <maya/MGlobal.h>
#include <maya/MSyntax.h>

#include <frantic/logging/logging_level.hpp>

void* XMeshLoggingCommand::creator() { return new XMeshLoggingCommand; }

MSyntax XMeshLoggingCommand::newSyntax() {
    MSyntax syntax;
    syntax.enableQuery();

    syntax.addFlag( "-lvl", "-loggingLevel", MSyntax::kUnsigned );

    return syntax;
}

MStatus XMeshLoggingCommand::doIt( const MArgList& args ) {
    try {
        MStatus stat;

        MArgDatabase argData( syntax(), args, &stat );
        CHECK_MSTATUS_AND_RETURN_IT( stat );

        int loggingLevel;

        if( argData.isQuery() ) {
            if( argData.isFlagSet( "-lvl" ) ) {
                loggingLevel = frantic::logging::get_logging_level();
                setResult( loggingLevel );
            }
        } else {
            if( argData.isFlagSet( "-lvl" ) ) {
                stat = argData.getFlagArgument( "-lvl", 0, loggingLevel );
                CHECK_MSTATUS_AND_RETURN_IT( stat );

                frantic::logging::set_logging_level( loggingLevel );
            }
        }

    } catch( const std::exception& e ) {
        FF_LOG( error ) << e.what() << endl;
        return MStatus::kFailure;
    }

    return MStatus::kSuccess;
}

void XMeshLoggingCommand::to_progress_log( const frantic::tchar* szMsg ) {
    if( frantic::logging::is_logging_progress() ) {
        std::cout << "PRG: " << szMsg << std::endl;
    }
}

void XMeshLoggingCommand::to_debug_log( const frantic::tchar* szMsg ) {
    if( frantic::logging::is_logging_debug() ) {
        std::cout << "DBG: " << szMsg << std::endl;
    }
}

void XMeshLoggingCommand::to_stats_log( const frantic::tchar* szMsg ) {
    if( frantic::logging::is_logging_stats() ) {
        std::cout << "STS: " << szMsg << std::endl;
    }
}

void XMeshLoggingCommand::to_warning_log( const frantic::tchar* szMsg ) {
    if( frantic::logging::is_logging_warnings() ) {
        MGlobal::displayWarning( szMsg );
    }
}

void XMeshLoggingCommand::to_error_log( const frantic::tchar* szMsg ) {
    if( frantic::logging::is_logging_errors() ) {
        MGlobal::displayError( szMsg );
    }
}

void XMeshLoggingCommand::initialize_logging() {
    frantic::logging::set_logging_level( frantic::logging::LOG_WARNINGS );

    frantic::logging::error.rdbuf( frantic::logging::new_ffstreambuf( &to_error_log ) );
    frantic::logging::warning.rdbuf( frantic::logging::new_ffstreambuf( &to_warning_log ) );
    frantic::logging::stats.rdbuf( frantic::logging::new_ffstreambuf( &to_stats_log ) );
    frantic::logging::debug.rdbuf( frantic::logging::new_ffstreambuf( &to_debug_log ) );
    frantic::logging::progress.rdbuf( frantic::logging::new_ffstreambuf( &to_progress_log ) );
}

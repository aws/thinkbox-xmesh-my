// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#include "progress_bar_progress_logger.hpp"

#ifndef _BOOL
#define _BOOL
#endif

#include <maya/MGlobal.h>

#include <sstream>

namespace {

std::string escape_mel_string( const std::string& inString ) {
    std::string resultString( inString );
    size_t currentPos = 0;

    while( currentPos < resultString.length() ) {
        currentPos = resultString.find_first_of( '\"', currentPos );

        if( currentPos == std::string::npos ) {
            break;
        } else {
            resultString.replace( currentPos, 1, "\\\"" );
            currentPos += 2;
        }
    }

    return "\"" + resultString + "\"";
}

void set_progress_min_max( int minValue, int maxValue ) {
    std::ostringstream os;
    os << "progressBar -edit -minValue " << minValue << " -maxValue " << maxValue << " $gMainProgressBar;";
    MGlobal::executeCommand( os.str().c_str() );
}

void begin_display() {
    std::ostringstream os;
    os << "progressBar -edit -isInterruptable true -beginProgress $gMainProgressBar;";
    MGlobal::executeCommand( os.str().c_str() );
}

void end_display() {
    std::ostringstream os;
    os << "progressBar -edit -endProgress $gMainProgressBar;";
    MGlobal::executeCommand( os.str().c_str() );
}

void set_progress( float progress ) {
    std::ostringstream os;
    os << "progressBar -edit -progress " << int( (progress)*100.0f ) << " $gMainProgressBar;";
    MGlobal::executeCommand( os.str().c_str() );
}

bool is_cancelled() {
    std::ostringstream os;
    os << "progressBar -query -isCancelled $gMainProgressBar;";
    int result;
    MGlobal::executeCommand( os.str().c_str(), result );
    return result ? true : false;
}

} // namespace

progress_bar_progress_logger::progress_bar_progress_logger() {
    set_progress_min_max( 0, 10000 );

    begin_display();

    // seems to get stuck sometimes? TODO: investigate this
    if( is_cancelled() ) {
        end_display();
        begin_display();
    }

    set_progress( 0.f );
}

progress_bar_progress_logger::~progress_bar_progress_logger() { end_display(); }

void progress_bar_progress_logger::set_title( const frantic::tstring& title ) {
    std::ostringstream os;
    os << "progressBar -edit -status " << escape_mel_string( frantic::strings::to_string( title ) )
       << " $gMainProgressBar;";
    MGlobal::executeCommand( os.str().c_str() );
}

void progress_bar_progress_logger::update_progress( long long completed, long long maximum ) {
    update_progress( 100.f * static_cast<float>( completed ) / maximum );
}

void progress_bar_progress_logger::update_progress( float percent ) {
    check_for_abort();
    set_progress( percent );
}

void progress_bar_progress_logger::check_for_abort() {
    if( is_cancelled() ) {
        throw frantic::logging::progress_cancel_exception( "Operation cancelled" );
    }
}

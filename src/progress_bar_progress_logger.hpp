// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <frantic/logging/progress_logger.hpp>

class progress_bar_progress_logger : public frantic::logging::progress_logger {
  public:
    progress_bar_progress_logger();
    virtual ~progress_bar_progress_logger();

    virtual void set_title( const frantic::tstring& title );
    virtual void update_progress( long long completed, long long maximum );
    virtual void update_progress( float percent );

    virtual void check_for_abort();
};

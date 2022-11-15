# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required( VERSION 3.20 FATAL_ERROR )

project( XMeshMY )

find_package( thinkboxcmlibrary REQUIRED )
include( PrecompiledHeader)
include( ThinkboxCMLibrary)

set( SUPPORTED_MAYA_VERSIONS 2022 2023 )

option( MAYA_VERSION "The version of Maya SDK to build the library against." 2022 )

if( NOT MAYA_VERSION IN_LIST SUPPORTED_MAYA_VERSIONS )
  message( FATAL_ERROR "ERROR: Cannot build for unsupported Maya version ${MAYA_VERSION}" )
endif()

add_library( xmeshmy SHARED )

set_target_properties( xmeshmy PROPERTIES OUTPUT_NAME "XMesh" )
set_target_properties( xmeshmy PROPERTIES PREFIX "" )

target_include_directories( xmeshmy PUBLIC
                            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                            $<INSTALL_INTERFACE:include> )

if( WIN32 )
	set_target_properties( xmeshmy PROPERTIES SUFFIX ".mll" )
elseif( APPLE )
	set_target_properties( xmeshmy PROPERTIES SUFFIX ".bundle" )
elseif( UNIX )
	set_target_properties( xmeshmy PROPERTIES SUFFIX ".so" )
endif()

file( GLOB H_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "src/*.hpp" )	
file( GLOB CXX_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "src/*.cpp" )

target_sources( xmeshmy PRIVATE
	"resource.h"
	"XMeshVersion.h"
	${H_FILES}
	${CXX_FILES}
)

# The Conan version of Boost was built with this, and it changes the library names.
# As a result, we need to set this to tell Boost to look for the right libraries to
# link against.
target_compile_definitions( xmeshmy PUBLIC BOOST_AUTO_LINK_SYSTEM )

find_package( thinkboxlibrary REQUIRED )
find_package( mayasdk REQUIRED )
find_package( thinkboxmylibrary REQUIRED )
find_package( xmeshcore REQUIRED )
find_package( Boost REQUIRED )
find_package( OpenEXR REQUIRED )
find_package( ZLIB REQUIRED )
find_package( TBB REQUIRED )
find_package( xxHash REQUIRED )

target_include_directories( xmeshmy PUBLIC ${thinkboxlibrary_INCLUDE_DIRS} )
target_include_directories( xmeshmy PUBLIC ${mayasdk_INCLUDE_DIRS} )
target_include_directories( xmeshmy PUBLIC ${thinkboxmylibrary_INCLUDE_DIRS} )
target_include_directories( xmeshmy PUBLIC ${xmeshcore_INCLUDE_DIRS} )
target_include_directories( xmeshmy PUBLIC ${Boost_INCLUDE_DIRS} )
target_include_directories( xmeshmy PUBLIC ${OpenEXR_INCLUDE_DIRS} )
target_include_directories( xmeshmy PUBLIC ${ZLIB_INCLUDE_DIRS} )
target_include_directories( xmeshmy PUBLIC ${TBB_INCLUDE_DIRS} )
target_include_directories( xmeshmy PUBLIC ${xxHash_INCLUDE_DIRS} )

target_link_libraries( xmeshmy PUBLIC thinkboxlibrary::thinkboxlibrary )
target_link_libraries( xmeshmy PUBLIC mayasdk::mayasdk )
target_link_libraries( xmeshmy PUBLIC thinkboxmylibrary::thinkboxmylibrary )
target_link_libraries( xmeshmy PUBLIC xmeshcore::xmeshcore )
target_link_libraries( xmeshmy PUBLIC Boost::Boost )
target_link_libraries( xmeshmy PUBLIC OpenEXR::OpenEXR )
target_link_libraries( xmeshmy PUBLIC ZLIB::ZLIB )
target_link_libraries( xmeshmy PUBLIC TBB::tbb )
target_link_libraries( xmeshmy PUBLIC xxHash::xxHash )

find_package( OpenGL REQUIRED )
include_directories( ${OPENGL_INCLUDE_DIRS} )
target_link_libraries( xmeshmy PUBLIC ${OPENGL_LIBRARIES} )

if( UNIX )
	set_target_properties( xmeshmy PROPERTIES COMPILE_FLAGS "-fPIC -pthread -O3" )
	if( APPLE )
		set_property( TARGET xmeshmy APPEND_STRING PROPERTY LINK_FLAGS " -Wl,-exported_symbols_list ${CMAKE_CURRENT_SOURCE_DIR}/XMesh.exp" )
	else()
		set_property( TARGET xmeshmy APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/XMesh.map -s" )
	endif()
endif()

frantic_common_platform_setup( xmeshmy )
frantic_default_source_groups( xmeshmy HEADERDIR include SOURCEDIR src )

# Disable optimization for the RelWithDebInfo configuration on Windows.
# This allows breakpoints to be hit reliably when debugging in Visual Studio.
if( WIN32 )
	target_compile_options( xmeshmy PRIVATE "$<$<CONFIG:Release>:/O2>$<$<CONFIG:RelWithDebInfo>:/Od>" )
endif()

install( TARGETS xmeshmy
          RUNTIME DESTINATION bin
          LIBRARY DESTINATION lib
          ARCHIVE DESTINATION lib
)

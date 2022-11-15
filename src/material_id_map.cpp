// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#include "material_id_map.hpp"

#include <limits>
#include <stdexcept>

material_id_map::material_id_map() { clear(); }

void material_id_map::swap( material_id_map& other ) {
    m_map.swap( other.m_map );
    m_usedIDs.swap( other.m_usedIDs );
    std::swap( m_nextIDHint, other.m_nextIDHint );
    std::swap( m_locked, other.m_locked );
    std::swap( m_hasUndefinedID, other.m_hasUndefinedID );
    std::swap( m_undefinedID, other.m_undefinedID );
}

void material_id_map::clear() {
    m_map.clear();
    m_usedIDs.clear();
    m_nextIDHint = 0;
    m_locked = false;
    m_hasUndefinedID = false;
    m_undefinedID = 0;
}

void material_id_map::lock() { m_locked = true; }

bool material_id_map::has_material( const frantic::tstring& name ) const { return m_map.count( name ) != 0; }

bool material_id_map::has_undefined_material() const { return m_hasUndefinedID; }

boost::uint16_t material_id_map::get_material_id( const frantic::tstring& name ) {
    map_t::iterator i = m_map.find( name );
    if( i == m_map.end() ) {
        if( m_locked ) {
            return get_undefined_material_id();
        } else {
            const boost::uint16_t id = get_next_unused_id();
            insert_material( id, name );
            return id;
        }
    } else {
        return i->second;
    }
}

boost::uint16_t material_id_map::get_undefined_material_id() {
    if( m_hasUndefinedID ) {
        return m_undefinedID;
    } else {
        const boost::uint16_t id = get_next_unused_id();
        m_undefinedID = id;
        m_hasUndefinedID = true;
        return m_undefinedID;
    }
}

void material_id_map::insert_material( boost::uint16_t id, const frantic::tstring& name ) {
    map_t::iterator i = m_map.find( name );
    if( i == m_map.end() ) {
        m_map[name] = id;
    }
    m_usedIDs.insert( id );
}

void material_id_map::insert_undefined_material( boost::uint16_t id ) {
    if( !m_hasUndefinedID ) {
        m_undefinedID = id;
        m_hasUndefinedID = true;
    }
    m_usedIDs.insert( id );
}

boost::uint16_t material_id_map::get_next_unused_id() {
    while( m_usedIDs.count( m_nextIDHint ) ) {
        if( m_nextIDHint == std::numeric_limits<boost::uint16_t>::max() ) {
            throw std::runtime_error( "material_id_map::get_next_unused_id Error: exhausted available Material IDs.  "
                                      "Please contact Thinkbox Support." );
        }
        ++m_nextIDHint;
    }
    return m_nextIDHint;
}

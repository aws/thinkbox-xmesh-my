// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <set>

#include <boost/cstdint.hpp>

#include <frantic/strings/tstring.hpp>

class material_id_map {
  public:
    material_id_map();

    void swap( material_id_map& other );

    void clear();
    void lock();

    bool has_material( const frantic::tstring& name ) const;
    bool has_undefined_material() const;
    boost::uint16_t get_material_id( const frantic::tstring& name );
    boost::uint16_t get_undefined_material_id();

    void insert_material( boost::uint16_t id, const frantic::tstring& name );
    void insert_undefined_material( boost::uint16_t id );

  private:
    boost::uint16_t get_next_unused_id();

    typedef std::map<frantic::tstring, boost::uint16_t> map_t;
    map_t m_map;
    std::set<boost::uint16_t> m_usedIDs;
    boost::uint16_t m_nextIDHint;
    bool m_locked;
    bool m_hasUndefinedID;
    boost::uint16_t m_undefinedID;
};

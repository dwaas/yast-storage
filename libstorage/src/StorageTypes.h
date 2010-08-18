/*
 * Copyright (c) [2004-2009] Novell, Inc.
 *
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, contact Novell, Inc.
 *
 * To contact Novell about this file by physical or electronic mail, you may
 * find current contact information at www.novell.com.
 */


#ifndef STORAGE_TYPES_H
#define STORAGE_TYPES_H

#include <vector>
#include <string>
#include <iostream>
#include <boost/algorithm/string.hpp>

#include "y2storage/Regex.h"
#include "y2storage/AppUtil.h"
#include "y2storage/StorageInterface.h"

namespace storage
{
    using std::string;
    using std::vector;

inline bool operator<(CType a, CType b)
{
    static const int order[COTYPE_LAST_ENTRY] = {
	0, // CUNKNOWN
	1, // DISK
	5, // MD
	8, // LOOP
	7, // LVM
	6, // DM
	2, // DMRAID
	9, // NFSC
	3, // DMMULTIPATH
	4  // MDPART
    };

    bool ret = order[a] < order[b];
    y2mil("a:" << a << " o(a):" << order[a] << " b:" << b << " o(b):" << order[b] << " ret:" << ret);
    return ret;
}

inline bool operator<=( CType a, CType b )
    {
    return( a==b || a<b );
    }

inline bool operator>=( CType a, CType b )
    {
    return( !(a<b) );
    }

inline bool operator>( CType a, CType b )
    {
    return( a!=b && !(a<b) );
    }

struct contOrder
    {
    contOrder(CType t) : order(0)
	{
	if( t==LOOP )
	    order=1;
	}
    operator unsigned() const { return( order ); }
    protected:
	unsigned order;
    };

typedef enum { DECREASE, INCREASE, FORMAT, MOUNT } CommitStage;

class Volume;
class Container;

struct commitAction
    {
    commitAction( CommitStage s, CType t, const string& d, const Volume* v,
                  bool destr=false )
	{ stage=s; type=t; descr=d; destructive=destr; container=false;
	  u.vol=v; }
    commitAction( CommitStage s, CType t, const string& d, const Container* co,
                  bool destr=false )
	{ stage=s; type=t; descr=d; destructive=destr; container=true;
	  u.co=co; }
    commitAction( CommitStage s, CType t, const Volume* v )
	{ stage=s; type=t; destructive=false; container=false; u.vol=v; }
    commitAction( CommitStage s, CType t, const Container* c )
	{ stage=s; type=t; destructive=false; container=true; u.co=c; }
    CommitStage stage;
    CType type;
    string descr;
    bool destructive;
    bool container;
    union
	{
	const Volume* vol;
	const Container* co;
	} u;
    const Container* co() const { return( container?u.co:NULL ); }
    const Volume* vol() const { return( container?NULL:u.vol ); }
    bool operator==( const commitAction& rhs ) const
	{ return( stage==rhs.stage && type==rhs.type ); }
    bool operator<( const commitAction& rhs ) const;
    bool operator<=( const commitAction& rhs ) const
	{ return( *this < rhs || *this == rhs ); }
    bool operator>=( const commitAction& rhs ) const
	{ return( ! (*this < rhs) ); }
    bool operator>( const commitAction& rhs ) const
	{ return( !(*this < rhs && *this == rhs) ); }
    };


class usedBy
{
    // TODO: save device instead of name?

public:
    usedBy() : ub_type(storage::UB_NONE) {}
    usedBy(storage::UsedByType type, const string& name) : ub_type(type), ub_name(name) {}

    void clear() { ub_type = storage::UB_NONE; ub_name.erase(); }
    void set(storage::UsedByType type, const string& name)
	{ ub_type = type; (ub_type==storage::UB_NONE)?ub_name.erase():ub_name = name; }

    bool operator==(const usedBy& rhs) const
	{ return ub_type == rhs.ub_type && ub_name == rhs.ub_name; }
    bool operator!=(const usedBy& rhs) const
	{ return !(*this == rhs); }

    operator string() const;

    storage::UsedByType type() const { return ub_type; }
    const string& name() const { return ub_name; }
    const string device() const;

    friend std::ostream& operator<<(std::ostream&, const usedBy&);

private:
    storage::UsedByType ub_type;
    string ub_name;
};

    struct string_starts_with
    {
        string_starts_with(const string& t) : val(t) {}
        bool operator()(const string& s) const { return boost::starts_with(s, val); }
        const string& val;
    };

    struct string_contains
    {
        string_contains(const string& t) : val(t) {}
        bool operator()(const string& s) const { return boost::contains(s, val); }
        const string& val;
    };


template <class Pred>
vector<string>::iterator
find_if(vector<string>& lines, Pred pred)
    {
    return std::find_if(lines.begin(), lines.end(), pred);
    }

template <class Pred>
vector<string>::const_iterator
find_if(const vector<string>& lines, Pred pred)
    {
    return std::find_if(lines.begin(), lines.end(), pred);
    }

struct match_string
    {
    match_string(const string& t) : val(t) {}
    bool operator()(const string&s) { return s == val; }
    const string& val;
    };

struct match_regex
    {
    match_regex(const Regex& t) : r(t) {}
    bool operator()(const string&s) { return r.match(s); }
    const Regex& r;
    };

struct find_begin
    {
    find_begin(const string& t) : val(t) {}
    bool operator()(const string&s) { return s.find(val) == 0; }
    const string& val;
    };

struct find_any
    {
    find_any(const string& t) : val(t) {}
    bool operator()(const string&s) { return s.find(val) != string::npos; }
    const string& val;
    };

}

#endif

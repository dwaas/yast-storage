/*
 * File: MdPartCo.cc
 *
 * Implementation of MdPartCo class which represents single MD Device (RAID
 * Volume) like md126 which is a Container for partitions.
 *
 * Copyright (c) 2009, Intel Corporation.
 * Copyright (c) 2009 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
  Textdomain    "storage"
*/

#include <sstream>
#include <algorithm>
#include <cctype>
#include <string>

#include <unistd.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <boost/algorithm/string.hpp>

#include "y2storage/MdPartCo.h"
#include "y2storage/MdPart.h"
#include "y2storage/ProcPart.h"
#include "y2storage/Partition.h"
#include "y2storage/SystemCmd.h"
#include "y2storage/AppUtil.h"
#include "y2storage/Storage.h"
#include "y2storage/StorageDefines.h"
#include "y2storage/Regex.h"
#include "y2storage/EtcRaidtab.h"


namespace storage
{
    using namespace std;


MdPartCo::MdPartCo( Storage * const s,
                    const string& name,
                    ProcPart* ppart )
     : Container(s,"",staticType()) // MD?
    {
    y2mil("constructing MdPartCo : " << name);
    makeDevName(name);
    nm = undevName(name);

    getMajorMinor();

    del_ptable = false;
    disk = NULL;

    /* First Initialize RAID properties. */
    initMd();
    /* Initialize 'disk' part, partitions.*/
    init( ppart );

    y2mil("MdPartCo (nm=" << nm << ", dev=" << dev << ", level=" << md_type << ", disks=" << devs << ") ready.");

    }

MdPartCo::~MdPartCo()
    {
    if( disk )
        {
        delete disk;
        disk = NULL;
        }
    y2deb("destructed MdPartCo : " << dev);
    }

bool MdPartCo::isMdPart(const string& name)
{
  string n = undevName(name);
  static Regex mdpart( "^md[0123456789]+p[0123456789]+$" );
  return (mdpart.match(n));
}

void MdPartCo::getPartNum(const string& device, unsigned& num)
{
  string dev = device;
  string::size_type pos;

  pos = dev.find("p");
  if( pos != string::npos )
    {
      /* md125p12 - after p is 12, this will be returned. */
      dev.substr(pos+1) >> num;
    }
  else
    {
    num = 0;
    }
}

/* Add new partition after creation. Called in 'CreatePartition' */
int
MdPartCo::addNewDev(string& device)
{
    int ret = 0;
    if ( isMdPart(device) == false )
      {
        ret = MD_PARTITION_NOT_FOUND;
      }
    else
    {
        unsigned number;
        const string tmpS(device);
        getPartNum(tmpS,number);
        device = "/dev/" + numToName(number);
        Partition *p = getPartition( number, false );
        if( p==NULL )
          {
            ret = MDPART_PARTITION_NOT_FOUND;
          }
        else
        {
            MdPart * md = NULL;
            newP( md, p->nr(), p );
            md->getFsInfo( p );
            md->setCreated();
            addToList( md );
            y2mil("device:" << device << " was added to MdPartCo : " << dev);

        }
        handleWholeDevice();
    }
    if( ret != 0 )
      {
      y2war("device:" << device << " was not added to MdPartCo : " << dev);
      }
    return ret;
}


int
MdPartCo::createPartition( storage::PartitionType type,
                           long unsigned start,
                           long unsigned len,
                           string& device,
                           bool checkRelaxed )
    {
    y2mil("begin type:" << type << " start:" << start << " len:" << len << " relaxed:" << checkRelaxed);
    int ret = disk ? 0 : MDPART_INTERNAL_ERR;
    if( ret==0 && readonly() )
        ret = MDPART_CHANGE_READONLY;
    if( ret==0 )
      {
        ret = disk->createPartition( type, start, len, device, checkRelaxed );
      }
    if( ret==0 )
      {
        ret = addNewDev( device );
      }
    y2mil("ret:" << ret);
    return( ret );
    }

int
MdPartCo::createPartition( long unsigned len, string& device, bool checkRelaxed )
    {
    y2mil("len:" << len << " relaxed:" << checkRelaxed);
    int ret = disk ? 0 : MDPART_INTERNAL_ERR;
    if( ret==0 && readonly() )
        ret = MDPART_CHANGE_READONLY;
    if( ret==0 )
        ret = disk->createPartition( len, device, checkRelaxed );
    if( ret==0 )
        ret = addNewDev( device );
    y2mil("ret:" << ret);
    return( ret );
    }

int
MdPartCo::createPartition( storage::PartitionType type, string& device )
    {
    y2mil("type:" << type);
    int ret = disk ? 0 : MDPART_INTERNAL_ERR;
    if( ret==0 && readonly() )
        ret = MDPART_CHANGE_READONLY;
    if( ret==0 )
        ret = disk->createPartition( type, device );
    if( ret==0 )
        ret = addNewDev( device );
    y2mil("ret:" << ret);
    return( ret );
    }

int MdPartCo::updateDelDev()
    {
    int ret = 0;
    list<Volume*> l;
    MdPartPair p=mdpartPair();
    MdPartIter i=p.begin();
    while( i!=p.end() )
        {
        Partition *p = i->getPtr();
        if( p && validPartition( p ) )
            {
            if( i->nr()!=p->nr() )
                {
                i->updateName();
                y2mil( "updated name md:" << *i );
                }
            if( i->deleted() != p->deleted() )
                {
                i->setDeleted( p->deleted() );
                y2mil( "updated del md:" << *i );
                }
            }
        else
            l.push_back( &(*i) );
        ++i;
        }
    list<Volume*>::iterator vi = l.begin();
    while( ret==0 && vi!=l.end() )
        {
        if( !removeFromList( *vi ))
            ret = MDPART_PARTITION_NOT_FOUND;
        ++vi;
        }
    handleWholeDevice();
    return( ret );
    }

int
MdPartCo::removePartition( unsigned nr )
    {
    int ret = disk ? 0 : MDPART_INTERNAL_ERR;
    if( ret==0 && readonly() )
        ret = MDPART_CHANGE_READONLY;
    if( ret==0 )
        {
        if( nr>0 )
          {
            ret = disk->removePartition( nr );
          }
        else
            ret = MDPART_PARTITION_NOT_FOUND;
        }
    if( ret==0 )
        ret = updateDelDev();
    y2mil("ret:" << ret);
    return( ret );
    }

int
MdPartCo::removeVolume( Volume* v )
    {
    int ret = disk ? 0 : MDPART_INTERNAL_ERR;
    if( ret==0 && readonly() )
        ret = MDPART_CHANGE_READONLY;
    if( ret==0 )
        {
        unsigned num = v->nr();
        if( num>0 )
            ret = disk->removePartition( v->nr() );
        }
    if( ret==0 )
        ret = updateDelDev();
    getStorage()->logCo( this );
    y2mil("ret:" << ret);
    return( ret );
    }


int
MdPartCo::freeCylindersAfterPartition(const MdPart* dm, unsigned long& freeCyls) const
{
    const Partition* p = dm->getPtr();
    int ret = p ? 0 : MDPART_PARTITION_NOT_FOUND;
    if (ret == 0)
    {
        ret = disk->freeCylindersAfterPartition(p, freeCyls);
    }
    y2mil("ret:" << ret);
    return ret;
}


int MdPartCo::resizePartition( MdPart* dm, unsigned long newCyl )
    {
    Partition * p = dm->getPtr();
    int ret = p?0:MDPART_PARTITION_NOT_FOUND;
    if( ret==0 )
        {
        p->getFsInfo( dm );
        ret = disk->resizePartition( p, newCyl );
        dm->updateSize();
        }
    y2mil( "dm:" << *dm );
    y2mil("ret:" << ret);
    return( ret );
    }

int
MdPartCo::resizeVolume( Volume* v, unsigned long long newSize )
    {
    int ret = disk ? 0 : MDPART_INTERNAL_ERR;
    if( ret==0 && readonly() )
        ret = MDPART_CHANGE_READONLY;
    MdPart * l = dynamic_cast<MdPart *>(v);
    if( ret==0 && l==NULL )
        ret = MDPART_INVALID_VOLUME;
    if( ret==0 )
        {
        Partition *p = l->getPtr();
        unsigned num = v->nr();
        if( num>0 && p!=NULL )
            {
            p->getFsInfo( v );
            ret = disk->resizeVolume( p, newSize );
            }
        else
            ret = MDPART_PARTITION_NOT_FOUND;
        }
    if( ret==0 )
        {
        l->updateSize();
        }
    y2mil("ret:" << ret);
    return( ret );
    }


void
MdPartCo::init( ProcPart* ppart )
{
  const string tmpS(nm);
  if( ppart )
    {
    ppart->getSize( nm, size_k );
    }
  y2mil( " nm: " << nm << " size_k: " << size_k);
  createDisk( ppart );
  getVolumes( ppart );
}

void
MdPartCo::createDisk( ProcPart* ppart )
    {
    if( disk )
        delete disk;
    disk = new Disk( getStorage(), dev, size_k );
    disk->setNumMinor( 64 );
    disk->setSilent();
    disk->setSlave();
    if( ppart )
      {
      disk->detect( *ppart );
      }
    }

// Creates new partition.
void
MdPartCo::newP( MdPart*& dm, unsigned num, Partition* p )
    {
    dm = new MdPart( *this, num, p );
    }

//This seems to detect partitions from ppart and adds them to Container.
void
MdPartCo::getVolumes( ProcPart* ppart )
    {
    vols.clear();
    num_part = 0;
    Disk::PartPair pp = disk->partPair();
    Disk::PartIter i = pp.begin();
    MdPart * p = NULL;
    while( i!=pp.end() )
        {
        newP( p, i->nr(), &(*i) );
        if( ppart )
          p->updateSize( *ppart );
        num_part++;
        addToList( p );
        ++i;
        }
    handleWholeDevice();
    }

void MdPartCo::handleWholeDevice()
    {
    Disk::PartPair pp = disk->partPair( Partition::notDeleted );
    y2mil("empty:" << pp.empty());

    if( pp.empty() )
        {
        MdPart * p = NULL;
        newP( p, 0, NULL );
        p->setSize( size_k );
        addToList( p );
        }
    else
        {
        MdPartIter i;
        if( findMdPart( 0, i ))
            {
            MdPart* md = &(*i);
            if( !removeFromList( md ))
                y2err( "not found:" << *i );
            }
        }
    }

Partition*
MdPartCo::getPartition( unsigned nr, bool del )
    {
    Partition* ret = NULL;
    Disk::PartPair pp = disk->partPair();
    Disk::PartIter i = pp.begin();
    while( i!=pp.end() &&
           (nr!=i->nr() || del!=i->deleted()) )
      {
        ++i;
      }
    if( i!=pp.end() )
        ret = &(*i);
    if( ret )
      {
        y2mil( "nr:" << nr << " del:" << del << " *p:" << *ret );
      }
    else
      {
        y2mil( "nr:" << nr << " del:" << del << " p:NULL" );
      }
    return( ret );
    }

bool
MdPartCo::validPartition( const Partition* p )
    {
    bool ret = false;
    Disk::PartPair pp = disk->partPair();
    Disk::PartIter i = pp.begin();
    while( i!=pp.end() && p != &(*i) )
        ++i;
    ret = i!=pp.end();
    y2mil( "nr:" << p->nr() << " ret:" << ret );
    return( ret );
    }

void MdPartCo::updatePointers( bool invalid )
    {
    MdPartPair p=mdpartPair();
    MdPartIter i=p.begin();
    while( i!=p.end() )
        {
        if( invalid )
            i->setPtr( getPartition( i->nr(), i->deleted() ));
        i->updateName();
        ++i;
        }
    }

void MdPartCo::updateMinor()
    {
    MdPartPair p=mdpartPair();
    MdPartIter i=p.begin();
    while( i!=p.end() )
        {
        i->updateMinor();
        ++i;
        }
    }

// Makes complete partition name (like md125p5)
string MdPartCo::numToName( unsigned mdNum ) const
    {
    string ret = nm;
    if( mdNum>0 )
        {
        ret += "p";
        ret += decString(mdNum);
        }
    return( ret );
    }

int MdPartCo::nr(const string& name)
{
  string tmp = name;
  int n;
  tmp.erase(0,2) >> n;
  return n;
}

int MdPartCo::nr()
{
  return mnr;
}


//
// Assumption is that we're using /dev not /dev/md
// directory.
string MdPartCo::undevName( const string& name )
    {
    string ret = name;
    if( ret.find( "/dev/" ) == 0 )
        ret.erase( 0, 5 );
    return( ret );
    }

int MdPartCo::destroyPartitionTable( const string& new_label )
    {
    y2mil("begin");
    int ret = disk->destroyPartitionTable( new_label );
    if( ret==0 )
        {
        VIter j = vols.begin();
        while( j!=vols.end() )
            {
            if( (*j)->created() )
                {
                delete( *j );
                j = vols.erase( j );
                }
            else
                ++j;
            }
        bool save = getStorage()->getRecursiveRemoval();
        getStorage()->setRecursiveRemoval(true);
        if( getUsedByType() != UB_NONE )
            {
            getStorage()->removeUsing( device(), getUsedBy() );
            }
        ronly = false;
        RVIter i = vols.rbegin();
        while( i!=vols.rend() )
            {
            if( !(*i)->deleted() )
                getStorage()->removeVolume( (*i)->device() );
            ++i;
            }
        getStorage()->setRecursiveRemoval(save);
        del_ptable = true;
        }
    y2mil("ret:" << ret);
    return( ret );
    }

int MdPartCo::changePartitionId( unsigned nr, unsigned id )
    {
    int ret = nr>0?0:MDPART_PARTITION_NOT_FOUND;
    if( ret==0 )
        {
        ret = disk->changePartitionId( nr, id );
        }
    y2mil("ret:" << ret);
    return( ret );
    }

int MdPartCo::forgetChangePartitionId( unsigned nr )
    {
    int ret = nr>0?0:MDPART_PARTITION_NOT_FOUND;
    if( ret==0 )
        {
        ret = disk->forgetChangePartitionId( nr );
        }
    y2mil("ret:" << ret);
    return( ret );
    }


int
MdPartCo::nextFreePartition(PartitionType type, unsigned& nr, string& device) const
{
    int ret = disk->nextFreePartition( type, nr, device );
    if( ret==0 )
        {
        device = "/dev/" + numToName(nr);
        }
    y2mil("ret:" << ret << " nr:" << nr << " device:" << device);
    return ret;
}


int MdPartCo::changePartitionArea( unsigned nr, unsigned long start,
                                   unsigned long len, bool checkRelaxed )
    {
    int ret = nr>0?0:MDPART_PARTITION_NOT_FOUND;
    if( ret==0 )
        {
        ret = disk->changePartitionArea( nr, start, len, checkRelaxed );
        MdPartIter i;
        if( findMdPart( nr, i ))
            i->updateSize();
        }
    y2mil("ret:" << ret);
    return( ret );
    }

bool MdPartCo::findMdPart( unsigned nr, MdPartIter& i )
    {
    MdPartPair p = mdpartPair( MdPart::notDeleted );
    i=p.begin();
    while( i!=p.end() && i->nr()!=nr )
        ++i;
    return( i!=p.end() );
    }

//Do we need to activate partition? it will be activated already
void MdPartCo::activate_part( bool val )
    {
  (void)val;
    }

int MdPartCo::doSetType( MdPart* md )
    {
    y2mil("doSetType container:" << name() << " name:" << md->name());
    Partition * p = md->getPtr();
    int ret = p?0:MDPART_PARTITION_NOT_FOUND;
    if( ret==0 )
        {
        if( !silent )
            {
            getStorage()->showInfoCb( md->setTypeText(true) );
            }
        ret = disk->doSetType( p );
        }
    y2mil("ret:" << ret);
    return( ret );
    }

int MdPartCo::doCreateLabel()
    {
    y2mil("label:" << labelName());
    int ret = 0;
    if( !silent )
        {
        getStorage()->showInfoCb( setDiskLabelText(true) );
        }
    getStorage()->removeDmMapsTo( device() );
    removePresentPartitions();
    ret = disk->doCreateLabel();
    if( ret==0 )
        {
        del_ptable = false;
        removeFromMemory();
        handleWholeDevice();
        getStorage()->waitForDevice();
        }
    y2mil("ret:" << ret);
    return( ret );
    }

int
MdPartCo::removeMdPart()
    {
    int ret = 0;
    y2mil("begin");
    if( readonly() )
        {
        y2war("Read-Only RAID.");
        ret = MDPART_CHANGE_READONLY;
        }
    if( ret==0 && !created() )
        {
        //Remove partitions
        MdPartPair p=mdpartPair(MdPart::notDeleted);
        for( MdPartIter i=p.begin(); i!=p.end(); ++i )
            {
            if( i->nr()>0 )
              {
                ret = removePartition( i->nr() );
                if( ret != 0 )
                  {
                  // Error. Break.
                  break;
                  }

              }
            }
        //Remove 'whole device' it was created when last partition was deleted.
        p=mdpartPair(MdPart::notDeleted);
        if( p.begin()!=p.end() && p.begin()->nr()==0 )
            {
            if( !removeFromList( &(*p.begin()) ))
              {
                y2err( "not found:" << *p.begin() );
                ret = MDPART_PARTITION_NOT_FOUND;
              }
            }
        }
    if( ret==0 )
      {
      unuseDevs();
      setDeleted( true );
      destrSb = true;
      del_ptable = true;
      }
    y2mil("ret:" << ret);
    return( ret );
    }

int MdPartCo::unuseDevs(void)
{
  list<string> rdevs;
  getDevs( rdevs );
  for( list<string>::const_iterator s=rdevs.begin();
      s!=rdevs.end(); s++ )
    {
    getStorage()->clearUsedBy(*s);
    }
  return 0;
}



void MdPartCo::removePresentPartitions()
    {
    VolPair p = volPair();
    if( !p.empty() )
        {
        bool save=silent;
        setSilent( true );
        list<VolIterator> l;
        for( VolIterator i=p.begin(); i!=p.end(); ++i )
            {
            y2mil( "rem:" << *i );
            if( !i->created() )
                l.push_front( i );
            }
        for( list<VolIterator>::const_iterator i=l.begin(); i!=l.end(); ++i )
            {
            doRemove( &(**i) );
            }
        setSilent( save );
        }
    }

void MdPartCo::removeFromMemory()
    {
    VIter i = vols.begin();
    while( i!=vols.end() )
        {
        y2mil( "rem:" << *i );
        if( !(*i)->created() )
            {
            i = vols.erase( i );
            }
        else
            ++i;
        }
    }

static bool toChangeId( const MdPart&d )
    {
    Partition* p = d.getPtr();
    return( p!=NULL && !d.deleted() && Partition::toChangeId(*p) );
    }

int MdPartCo::getToCommit( CommitStage stage, list<Container*>& col,
                           list<Volume*>& vol )
    {
    int ret = 0;
    y2mil("ret:" << ret << " col:" << col.size() << " << vol:" << vol.size());
    getStorage()->logCo( this );
    unsigned long oco = col.size();
    unsigned long ovo = vol.size();
    Container::getToCommit( stage, col, vol );
    if( stage==INCREASE )
        {
        MdPartPair p = mdpartPair( toChangeId );
        for( MdPartIter i=p.begin(); i!=p.end(); ++i )
            if( find( vol.begin(), vol.end(), &(*i) )==vol.end() )
                vol.push_back( &(*i) );
        }
    if( del_ptable && find( col.begin(), col.end(), this )==col.end() )
        col.push_back( this );
    if( col.size()!=oco || vol.size()!=ovo )
        y2mil("ret:" << ret << " col:" << col.size() << " vol:" << vol.size());
    return( ret );
    }


int MdPartCo::commitChanges( CommitStage stage, Volume* vol )
    {
    y2mil("name:" << name() << " stage:" << stage);
    int ret = Container::commitChanges( stage, vol );
    if( ret==0 && stage==INCREASE )
        {
        MdPart * dm = dynamic_cast<MdPart *>(vol);
        if( dm!=NULL )
            {
            Partition* p = dm->getPtr();
            if( p && disk && Partition::toChangeId( *p ) )
                ret = doSetType( dm );
            }
        else
            ret = MDPART_INVALID_VOLUME;
        }
    y2mil("ret:" << ret);
    return( ret );
    }

int MdPartCo::commitChanges( CommitStage stage )
    {
    y2mil("name:" << name() << " stage:" << stage);
    int ret = 0;
    if( stage==DECREASE && deleted() )
        {
        ret = doRemove();
        }
    else if( stage==DECREASE && del_ptable )
        {
        ret = doCreateLabel();
        }
    else
        ret = MDPART_COMMIT_NOTHING_TODO;
    y2mil("ret:" << ret);
    return( ret );
    }

void MdPartCo::getCommitActions( list<commitAction*>& l ) const
    {
    y2mil( "l:" << l );
    Container::getCommitActions( l );
    y2mil( "l:" << l );
    if( deleted() || del_ptable )
        {
        list<commitAction*>::iterator i = l.begin();
        while( i!=l.end() )
            {
            if( (*i)->stage==DECREASE )
                {
                delete( *i );
                i=l.erase( i );
                }
            else
                ++i;
            }
        string txt = deleted() ? removeText(false) :
                                 setDiskLabelText(false);
        l.push_front( new commitAction( DECREASE, staticType(),
                                        txt, this, true ));
        }
    y2mil( "l:" << l );
    }

int
MdPartCo::doCreate( Volume* v )
    {
    y2mil("name:" << name() << " v->name:" << v->name());
    MdPart * l = dynamic_cast<MdPart *>(v);
    int ret = disk ? 0 : MDPART_INTERNAL_ERR;
    if( ret==0 && l == NULL )
        ret = MDPART_INVALID_VOLUME;
    Partition *p = NULL;
    if( ret==0 )
        {
        if( !silent )
            {
            getStorage()->showInfoCb( l->createText(true) );
            }
        p = l->getPtr();
        if( p==NULL )
            ret = MDPART_PARTITION_NOT_FOUND;
        else
            ret = disk->doCreate( p );
        if( ret==0 && p->id()!=Partition::ID_LINUX )
            ret = doSetType( l );
        }
    if( ret==0 )
        {
        l->setCreated(false);
        if( active )
            {
            activate_part(false);
            activate_part(true);
            ProcPart pp;
            updateMinor();
            l->updateSize( pp );
            }
        if( p->type()!=EXTENDED )
            getStorage()->waitForDevice( l->device() );
        }
    y2mil("ret:" << ret);
    return( ret );
    }

//Remove MDPART unless:
//1. It's IMSM or DDF SW RAID
//2. It contains partitions.
int MdPartCo::doRemove()
    {
    y2mil("begin");
    // 1. Check Metadata.
    if( sb_ver == "imsm" || sb_ver == "ddf" )
      {
      y2error("Cannot remove IMSM or DDF SW RAIDs.");
      return (MDPART_NO_REMOVE);
      }
    // 2. Check for partitions.
    if( disk!=NULL && disk->numPartitions()>0 )
      {
      int permitRemove=1;
      //handleWholeDevice: partition 0.
      if( disk->numPartitions() == 1 )
        {
        //Find partition '0' if it exists then this 'whole device'
        MdPartIter i;
        if( findMdPart( 0, i ) == true)
          {
          //Single case when removal is allowed.
          permitRemove = 0;
          }
        }
      if( permitRemove == 1 )
        {
        y2error("Cannot remove RAID with partitions.");
        return (MDPART_NO_REMOVE);
        }
      }
    /* Try to remove this. */
    y2milestone( "Raid:%s is going to be removed permanently.", name().c_str() );
    int ret = 0;
    if( deleted() )
      {
      string cmd = MDADMBIN " --stop " + quote(device());
      SystemCmd c( cmd );
      if( c.retcode()!=0 )
        {
        ret = MD_REMOVE_FAILED;
        setExtError( c );
        }
      if( !silent )
        {
        getStorage()->showInfoCb( removeText(true) );
        }
      if( ret==0 && destrSb )
        {
        SystemCmd c;
        list<string> d;
        getDevs( d );
        for( list<string>::const_iterator i=d.begin(); i!=d.end(); ++i )
          {
          c.execute(MDADMBIN " --zero-superblock " + quote(*i));
          }
        }
      if( ret==0 )
        {
        EtcRaidtab* tab = getStorage()->getRaidtab();
        if( tab!=NULL )
          {
          tab->removeEntry( nr() );
          }
        }
      }
    y2mil("Done, ret:" << ret);
    return( ret );
    }

int MdPartCo::doRemove( Volume* v )
    {
    y2mil("name:" << name() << " v->name:" << v->name());
    MdPart * l = dynamic_cast<MdPart *>(v);
    int ret = disk ? 0 : MDPART_INTERNAL_ERR;
    if( ret==0 && l == NULL )
        ret = MDPART_INVALID_VOLUME;
    if( ret==0 )
        {
        if( !silent )
            {
            getStorage()->showInfoCb( l->removeText(true) );
            }
        ret = v->prepareRemove();
        }
    if( ret==0 )
        {
        Partition *p = l->getPtr();
        if( p==NULL )
          {
            y2error("Partition not found");
            ret = MDPART_PARTITION_NOT_FOUND;
          }
        else
          {
          ret = disk->doRemove( p );
          }
        }
    if( ret==0 )
        {
        if( !removeFromList( l ) )
          {
            y2warning("Couldn't remove parititon from list.");
            ret = MDPART_REMOVE_PARTITION_LIST_ERASE;
          }
        }
    if( ret==0 )
        getStorage()->waitForDevice();
    y2mil("Done, ret:" << ret);
    return( ret );
    }

int MdPartCo::doResize( Volume* v )
    {
    y2mil("name:" << name() << " v->name:" << v->name());
    MdPart * l = dynamic_cast<MdPart *>(v);
    int ret = disk ? 0 : MDPART_INTERNAL_ERR;
    if( ret==0 && l == NULL )
        ret = MDPART_INVALID_VOLUME;
    bool remount = false;
    bool needExtend = false;
    if( ret==0 )
        {
        needExtend = !l->needShrink();
        if( !silent )
            {
            getStorage()->showInfoCb( l->resizeText(true) );
            }
        if( l->isMounted() )
            {
            ret = l->umount();
            if( ret==0 )
                remount = true;
            }
        if( ret==0 && !needExtend && l->getFs()!=VFAT && l->getFs()!=FSNONE )
            ret = l->resizeFs();
        }
    if( ret==0 )
        {
        Partition *p = l->getPtr();
        if( p==NULL )
            ret = MDPART_PARTITION_NOT_FOUND;
        else
            ret = disk->doResize( p );
        }
    if( ret==0 && active )
        {
        activate_part(false);
        activate_part(true);
        }
    if( ret==0 && needExtend && l->getFs()!=VFAT && l->getFs()!=FSNONE )
        ret = l->resizeFs();
    if( ret==0 )
        {
        ProcPart pp;
        updateMinor();
        l->updateSize( pp );
        getStorage()->waitForDevice( l->device() );
        }
    if( ret==0 && remount )
        ret = l->mount();
    y2mil("ret:" << ret);
    return( ret );
    }

string MdPartCo::setDiskLabelText( bool doing ) const
    {
    string txt;
    string d = dev;
    if( doing )
        {
        // displayed text during action, %1$s is replaced by name (e.g. pdc_igeeeadj),
        // %2$s is replaced by label name (e.g. msdos)
        txt = sformat( _("Setting disk label of %1$s to %2$s"),
                       d.c_str(), labelName().c_str() );
        }
    else
        {
        // displayed text before action, %1$s is replaced by name (e.g. pdc_igeeeadj),
        // %2$s is replaced by label name (e.g. msdos)
        txt = sformat( _("Set disk label of %1$s to %2$s"),
                      d.c_str(), labelName().c_str() );
        }
    return( txt );
    }

string MdPartCo::removeText( bool doing ) const
    {
    string txt;
    if( doing )
        {
        // displayed text during action, %1$s is replaced by a name (e.g. pdc_igeeeadj),
        txt = sformat( _("Removing %1$s"), name().c_str() );
        }
    else
        {
        // displayed text before action, %1$s is replaced by a name (e.g. pdc_igeeeadj),
        txt = sformat( _("Remove %1$s"), name().c_str() );
        }
    return( txt );
    }


void
MdPartCo::setUdevData(const list<string>& id)
{
  y2mil("disk:" << nm << " id:" << id);
  udev_id = id;
    partition(udev_id.begin(), udev_id.end(), find_begin("md-uuid-"));
  y2mil("id:" << udev_id);
    if (disk)
    {
	disk->setUdevData("", udev_id);
    }
    MdPartPair pp = mdpartPair();
    for( MdPartIter p=pp.begin(); p!=pp.end(); ++p )
      {
      p->addUdevData();
      }
}


void MdPartCo::getInfo( MdPartCoInfo& tinfo ) const
    {
    if( disk )
        {
        disk->getInfo( info.d );
        }
    info.minor = mnr;

    info.devices = boost::join(devs, " ");
    info.spares = boost::join(devs, " ");

    info.level = md_type;
    info.nr = mnr;
    info.parity = md_parity;
    info.uuid = md_uuid;
    info.sb_ver = sb_ver;
    info.chunk = chunk_size;
    info.md_name = md_name;

    tinfo = info;
    }


int MdPartCo::getPartitionInfo(deque<storage::PartitionInfo>& plist)
{
  int ret = 0;
  if( !disk )
    {
    ret = MDPART_INTERNAL_ERR;
    return ret;
    }
  Disk::PartPair p = disk->partPair (Disk::notDeleted);
  for (Disk::PartIter i = p.begin(); i != p.end(); ++i)
      {
      plist.push_back( PartitionInfo() );
      i->getInfo( plist.back() );
      }
  return ret;
}


std::ostream& operator<< (std::ostream& s, const MdPartCo& d )
    {
    s << *((Container*)&d);
    s << " Name:" << d.md_name
      << " MdNr:" << d.mnr
      << " PNum:" << d.num_part;
    if( !d.udev_id.empty() )
        s << " UdevId:" << d.udev_id;
    if( d.del_ptable )
      s << " delPT";
    if( !d.active )
      s << " inactive";
    return( s );
    }


string MdPartCo::getDiffString( const Container& d ) const
    {
    string log = Container::getDiffString( d );
    const MdPartCo* p = dynamic_cast<const MdPartCo*>(&d);
    if( p )
        {
        if( del_ptable!=p->del_ptable )
            {
            if( p->del_ptable )
                log += " -->delPT";
            else
                log += " delPT-->";
            }
        if( active!=p->active )
            {
            if( p->active )
                log += " -->active";
            else
                log += " active-->";
            }
        }
    return( log );
    }

void MdPartCo::logDifference( const MdPartCo& d ) const
    {
    string log = getDiffString( d );

    if( md_type!=d.md_type )
        log += " Personality:" + md_names[md_type] + "-->" +
               md_names[d.md_type];
    if( md_parity!=d.md_parity )
        log += " Parity:" + par_names[md_parity] + "-->" +
               par_names[d.md_parity];
    if( chunk_size!=d.chunk_size )
        log += " Chunk:" + decString(chunk_size) + "-->" + decString(d.chunk_size);
    if( sb_ver!=d.sb_ver )
        log += " SbVer:" + sb_ver + "-->" + d.sb_ver;
    if( md_uuid!=d.md_uuid )
        log += " MD-UUID:" + md_uuid + "-->" + d.md_uuid;
    if( md_name!=d.md_name )
      {
        log += " MDName:" + md_name + "-->" + d.md_name;
      }
    if( destrSb!=d.destrSb )
        {
        if( d.destrSb )
            log += " -->destrSb";
        else
            log += " destrSb-->";
        }
    if( devs!=d.devs )
        {
        std::ostringstream b;
        classic(b);
        b << " Devices:" << devs << "-->" << d.devs;
        log += b.str();
        }
    if( spare!=d.spare )
        {
        std::ostringstream b;
        classic(b);
        b << " Spares:" << spare << "-->" << d.spare;
        log += b.str();
        }
    if( parent_container!=d.parent_container )
        log += " ParentContainer:" + parent_container + "-->" + d.parent_container;
    if( parent_md_name!=d.parent_md_name )
        log += " ParentContMdName:" + parent_md_name + "-->" + d.parent_md_name;
    if( parent_metadata!=d.parent_metadata )
        log += " ParentContMetadata:" + parent_metadata + "-->" + d.parent_metadata;
    if( parent_uuid!=d.parent_uuid )
        log += " ParentContUUID:" + parent_uuid + "-->" + d.parent_uuid;

    y2mil(log);
    ConstMdPartPair pp=mdpartPair();
    ConstMdPartIter i=pp.begin();
    while( i!=pp.end() )
        {
        ConstMdPartPair pc=d.mdpartPair();
        ConstMdPartIter j = pc.begin();
        while( j!=pc.end() &&
               (i->device()!=j->device() || i->created()!=j->created()) )
            ++j;
        if( j!=pc.end() )
            {
            if( !i->equalContent( *j ) )
                i->logDifference( *j );
            }
        else
            y2mil( "  -->" << *i );
        ++i;
        }
    pp=d.mdpartPair();
    i=pp.begin();
    while( i!=pp.end() )
        {
        ConstMdPartPair pc=mdpartPair();
        ConstMdPartIter j = pc.begin();
        while( j!=pc.end() &&
               (i->device()!=j->device() || i->created()!=j->created()) )
            ++j;
        if( j==pc.end() )
            y2mil( "  <--" << *i );
        ++i;
        }
    }

bool MdPartCo::equalContent( const Container& rhs ) const
    {
    bool ret = Container::equalContent(rhs);
    if( ret )
      {
      const MdPartCo* mdp = dynamic_cast<const MdPartCo*>(&rhs);
      if( mdp == 0 )
        {
        return false;
        }
      ret = ret &&
          active==mdp->active &&
          del_ptable==mdp->del_ptable;

      ret = ret &&
          (chunk_size == mdp->chunk_size &&
              md_type == mdp->md_type &&
              md_parity == mdp->md_parity &&
              md_state == mdp->md_state &&
              sb_ver == mdp->sb_ver &&
              devs == mdp->devs &&
              spare == mdp->spare &&
              md_uuid == mdp->md_uuid &&
              destrSb == mdp->destrSb &&
              md_name == mdp->md_name);
      if( ret )
        {
        if( has_container )
          {
          ret = ret &&
              (parent_container == mdp->parent_container &&
               parent_md_name == mdp->parent_md_name &&
               parent_metadata == mdp->parent_metadata &&
               parent_uuid == mdp->parent_uuid);
          }
        }
      if( ret )
        {
        ConstMdPartPair pp = mdpartPair();
        ConstMdPartPair pc = mdp->mdpartPair();
        ConstMdPartIter i = pp.begin();
        ConstMdPartIter j = pc.begin();
        while( ret && i!=pp.end() && j!=pc.end() )
          {
          ret = ret && i->equalContent( *j );
          ++i;
          ++j;
          }
        ret = ret && i==pp.end() && j==pc.end();
        }
      }
    return( ret );
    }

MdPartCo::MdPartCo( const MdPartCo& rhs ) : Container(rhs)
    {
    y2deb("constructed MdPartCo by copy constructor from " << rhs.nm);
    active = rhs.active;
    del_ptable = rhs.del_ptable;
    chunk_size = rhs.chunk_size;
    md_type = rhs.md_type;
    md_parity = rhs.md_parity;
    md_state = rhs.md_state;
    has_container = rhs.has_container;
      parent_container = rhs.parent_container;
      parent_md_name = rhs.parent_md_name;
      parent_metadata = rhs.parent_metadata;
      parent_uuid = rhs.parent_uuid;
    md_uuid = rhs.md_uuid;
    sb_ver = rhs.sb_ver;
    destrSb = rhs.destrSb;
    devs = rhs.devs;
    spare = rhs.spare;
    md_name = rhs.md_name;

    udev_path = rhs.udev_path;
    udev_id = rhs.udev_id;

    disk = NULL;
    if( rhs.disk )
        disk = new Disk( *rhs.disk );
    getStorage()->waitForDevice();
    ConstMdPartPair p = rhs.mdpartPair();
    for( ConstMdPartIter i = p.begin(); i!=p.end(); ++i )
        {
        MdPart * p = new MdPart( *this, *i );
        vols.push_back( p );
        }
    updatePointers(true);
    num_part = rhs.num_part;
    }

bool MdPartCo::isMdName(const string& name)
{
  static Regex md("^md[0123456789]+");
  return (md.match(name));
}
// Get list of active MD RAID's
// cat /proc/mdstat
// If we're looking for Volume then
// find devname and in next line will be: 'external:imsm'
//Personalities : [raid0] [raid1]
//md125 : active (auto-read-only) raid1 sdb[1] sdc[0]
//      130071552 blocks super external:/md127/1 [2/2] [UU]
//
//md126 : active raid0 sdb[1] sdc[0]
//      52428800 blocks super external:/md127/0 128k chunks
//
//md127 : inactive sdc[1](S) sdb[0](S)
//      4514 blocks super external:imsm
//
//unused devices: <none>


list<string>
MdPartCo::getMdRaids()
{
  y2mil( " called " );
  list<string> l;
  string line;
  string dev_name;
  std::ifstream file( "/proc/mdstat" );
  classic(file);
  getline( file, line );
  while( file.good() )
  {
    string dev_name = extractNthWord( 0, line );
    if( isMdName(dev_name) )
      {
      string line2;
      getline(file,line2);
      if( line2.find("external:imsm") == string::npos &&
          line2.find("external:imsm") == string::npos)
        {
          // external:imsm or ddf not found. Assume that this is a Volume.
        l.push_back(dev_name);
        }
      }
    getline( file, line );
    }
    file.close();
    file.clear();

    y2mil("detected md devs : " << l);
    return l;
}

void
MdPartCo::getDevs( list<string>& devices, bool all, bool spares ) const
    {
    if( !all )
        devices = spares ? spare : devs;
    else
        {
        devices = devs;
        devices.insert( devices.end(), spare.begin(), spare.end() );
        }
    }


void MdPartCo::getSpareDevs(std::list<string>& devices )
{
  devices = spare;
}


bool MdPartCo::matchMdRegex( const string& dev )
    {
    static Regex md( "^md[0123456789]+$" );
    return( md.match(dev));
    }


unsigned MdPartCo::mdMajor()
    {
    if( md_major==0 )
        getMdMajor();
    return( md_major );
    }

void MdPartCo::getMdMajor()
    {
    md_major = getMajorDevices( "md" );
    }

void MdPartCo::setSize(unsigned long long size )
{
  size_k = size;
}

void MdPartCo::getMdProps()
{
  y2mil("Called");

  string property;

  if( !readProp(METADATA, md_metadata) )
    {
      y2war("Failed to read metadata");
    }

   property.clear();
   if( !readProp(COMPONENT_SIZE, property) )
     {
       y2war("Failed to read component_size");
       setSize(0);
     }
   else
     {
     unsigned long long tmpSize;
     property >> tmpSize;
     setSize(tmpSize);
     }

   property.clear();
   if( !readProp(CHUNK_SIZE, property) )
     {
       y2war("Failed to read chunk_size");
       chunk_size = 0;
     }
   else
     {
     property >> chunk_size;
     /* From 'B' in file to 'Kb' here. */
     chunk_size /= 1024;
     }

   property.clear();
   if( !readProp(ARRAY_STATE, property) )
     {
     md_state = storage::UNKNOWN;
     y2war("array state unknown ");
     }
   else
     {
     if( property == "readonly" )
       {
         //setReadonly();
       }
     md_state = toMdArrayState(property);
     }

    if( !readProp(LEVEL, property) )
      {
      y2war("RAID type unknown");
      md_type = storage::RAID_UNK;
      }
    else
      {
      md_type = toMdType(property);
      }

    //
    setMdParity();
    setMdDevs();
    setSpares();
    setMetaData();
    MdPartCo::getUuidName(nm,md_uuid,md_name);
    if( has_container )
      {
      MdPartCo::getUuidName(parent_container,parent_uuid,parent_md_name);
      y2mil("md_name="<<md_name<<", parent_container="<<parent_container
          <<", parent_uuid="<<parent_uuid<<", parent_md_name"<<parent_md_name);
      }
    y2mil("Done");
}

bool MdPartCo::readProp(enum MdProperty prop, string& val)
{
  string path = sysfs_path + nm + "/md/" + md_props[prop];

  if( access( path.c_str(), R_OK )==0 )
  {
    std::ifstream file( path.c_str() );
    classic(file);
    file >> val;
    file.close();
    file.clear();
  }
  else
  {
    y2war("File: " << sysfs_path << md_props[prop] << " = FAILED");
     return false;
  }
  return true;
}



void MdPartCo::getSlaves(const string name, std::list<string>& devs_list )
{
  string path = sysfs_path + name + "/slaves";
  DIR* dir;

  devs_list.clear();

  if ((dir = opendir(path.c_str())) != NULL)
    {
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
      {
      string tmpS(entry->d_name);
      y2mil("Entry :  " << tmpS);
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
        continue;
        }
      devs_list.push_back( ("/dev/"+tmpS) );
      }
    closedir(dir);
    }
  else
    {
    y2mil("Failed to opend directory");
    }
}


void MdPartCo::setMdDevs()
{
  getSlaves(nm,devs);

  for( list<string>::iterator s=devs.begin(); s!=devs.end(); ++s )
  {
    //It will be set always to last RAID that was detected.
    getStorage()->setUsedBy( *s, UB_MDPART, nm );
  }
}


void
MdPartCo::getParent()
{
  string ret;
  string con = md_metadata;
  string::size_type pos1;
  string::size_type pos2;

  parent_container.clear();
  has_container = false;
  if( md_metadata.empty() )
    {
      (void)readProp(METADATA, md_metadata);
      con = md_metadata;

    }
  if( con.find("external:")==0 )
    {
      if( (pos1=con.find_first_of("/")) != string::npos )
        {
        if( (pos2=con.find_last_of("/")) != string::npos)
          {
          if( pos1 != pos2)
            {
            //Typically: external:/md127/0
            parent_container.clear();
            parent_container = con.substr(pos1+1,pos2-pos1-1);
            has_container = true;
            }
          }
        }
      else
        {
        // this is the Container
        }
    }
  else
    {
    // No external metadata.
    // Possibly this is raid with persistent metadata and no container.
    }
}

void MdPartCo::setMetaData()
{
  if( parent_container.empty () )
    {
    getParent();
    }
  if( has_container == false )
    {
    // No parent container.
    sb_ver = md_metadata;
    parent_metadata.clear();
    return;
    }

  string path = sysfs_path + parent_container + "/md/" + md_props[METADATA];
  string val;

  if( access( path.c_str(), R_OK )==0 )
    {
    std::ifstream file( path.c_str() );
    classic(file);
    file >> val;
    file.close();
    file.clear();

    // It will be 'external:XXXX'
    string::size_type pos = val.find(":");
    if( pos != string::npos )
      {
      sb_ver = val.erase(0,pos+1);
      parent_metadata = sb_ver;
      }
    }
  return;
}

void MdPartCo::setMdParity()
{
  md_parity = PAR_NONE;
  //Level 5 & 6 - left-symmetric
  //Level 10 - n2 layout (0x102)
  if( hasParity() )
    {
    switch( md_type )
    {
    case RAID5:
    case RAID6:
      md_parity = LEFT_ASYMMETRIC;
    case RAID10:
      /* Parity 'n2' */
      //md_parity = PARITY_N2;
    default:
      return;
    }
    }
}


/* Spares: any disk that is in container and not in RAID. */
void MdPartCo::setSpares()
{
  std::list<string> parent_devs;
  std::list<string> diff_devs;

  parent_devs.clear();
  diff_devs.clear();
  int found;

  list<string>::const_iterator it1;
  list<string>::const_iterator it2;

  getParent();
  if( has_container == false )
    {
    spare.clear();
    return;
    }
  getSlaves(parent_container,parent_devs);

  for( it1 = parent_devs.begin(); it1 != parent_devs.end(); it1++ )
    {
      found = 0;
      for(it2 = devs.begin(); it2 != devs.end(); it2++ )
        {
          if( *it1 == *it2 )
            {
              found++;
              break;
            }
        }
      if( found == 0 )
        {
          diff_devs.push_back(*it1);
        }
    }
    spare = diff_devs;

    for( list<string>::iterator s=spare.begin(); s!=spare.end(); ++s )
    {
      //It will be set always to last RAID that was detected.
      getStorage()->setUsedBy( *s, UB_MDPART, nm );
    }
}

bool MdPartCo::findMdMap(std::ifstream& file)
{
  const char* mdadm_map[] = {"/var/run/mdadm/map",
                        "/var/run/mdadm.map",
                        "/dev/.mdadm.map",
                        0};
  classic(file);
  int i=0;
  while( mdadm_map[i] )
  {
    file.open( mdadm_map[i] );
      if( file.is_open() )
        {
         return true;
        }
        else
        {
          i++;
        }
  }
  y2war(" Map File not found");
  return false;
}


/* Will try to set: UUID, Name.*/
/* Format: mdX metadata uuid /dev/md/md_name */
bool MdPartCo::getUuidName(const string dev,string& uuid, string& mdName)
{
  std::ifstream file;
  string line;
  classic(file);

  uuid.clear();
  mdName.clear();
  /* Got file, now parse output. */
  if( MdPartCo::findMdMap(file) )
  {
    while( !file.eof() )
      {
      string val;
      getline(file,line);
      val = extractNthWord( MAP_DEV, line );
      if( val == dev )
        {
        size_t pos;
        uuid = extractNthWord( MAP_UUID, line );
        val = extractNthWord( MAP_NAME, line );
        // if md_name then /dev/md/name other /dev/mdxxx
        if( val.find("/md/")!=string::npos)
          {
          pos = val.find_last_of("/");
          mdName = val.substr(pos+1);
          }
        file.close();
        return true;
        }
      }
      file.close();
  }
  else
    {
    string tmp;
    string::size_type pos;
    //No file, employ mdadm -D name --export
    SystemCmd c(MDADMBIN " --detail " + quote(dev) + " --export");
    if( c.retcode() != 0 )
      {
      return false;
      }
    if(c.select( "MD_UUID" ) > 0)
      {
      tmp = *c.getLine(0,true);
      pos = tmp.find("=");
      tmp.erase(0,pos+1);
      uuid = tmp;
      }
    if( c.select( "MD_DEVNAME" ) > 0)
      {
      tmp = *c.getLine(0,true);
      pos = tmp.find("=");
      tmp.erase(0,pos+1);
      mdName = tmp;
      }
    if( !mdName.empty() && !uuid.empty() )
      {
      return true;
      }
    }
  return false;
}


void MdPartCo::initMd()
{
  /* Name is 'nm' read all props. */
  getMdProps();
}


void MdPartCo::activate( bool val, const string& tmpDir  )
{
  if( active!=val )
      {
      MdCo::activate(val,tmpDir);
      active = val;
      }
}

bool MdPartCo::matchRegex( const string& dev )
    {
    static Regex md( "^md[0123456789]+$" );
    return( md.match(dev));
    }

bool MdPartCo::mdStringNum( const string& name, unsigned& num )
    {
    bool ret=false;
    string d = undevDevice(name);
    if( matchRegex( d ))
        {
        d.substr( 2 )>>num;
        ret = true;
        }
    return( ret );
    }


MdType
MdPartCo::toMdType( const string& val )
    {
    enum MdType ret = MULTIPATH;
    while( ret!=RAID_UNK && val!=md_names[ret] )
        {
        ret = MdType(ret-1);
        }
    return( ret );
    }

MdParity
MdPartCo::toMdParity( const string& val )
    {
    enum MdParity ret = RIGHT_SYMMETRIC;
    while( ret!=PAR_NONE && val!=par_names[ret] )
        {
        ret = MdParity(ret-1);
        }
    return( ret );
    }



storage::MdArrayState
MdPartCo::toMdArrayState( const string& val )
{
    enum storage::MdArrayState ret = storage::ACTIVE_IDLE;
    while( ret!=storage::UNKNOWN && val!=md_states[ret] )
        {
        ret = storage::MdArrayState(ret-1);
        }
    return( ret );
}


void MdPartCo::getMdPartCoState(storage::MdPartCoStateInfo& info)
{
  string prop;

  readProp(ARRAY_STATE,prop);

  info.state = toMdArrayState(prop);

  info.active = true; //?
  info.degraded = false; //?
}


void MdPartCo::getMajorMinor()
{
  string path = sysfs_path + nm + "/dev";

  if( access( path.c_str(), R_OK )==0 )
  {
    string val;
    unsigned pos;

    std::ifstream file( path.c_str() );
    classic(file);
    file >> val;

    pos = val.find(":");
    val.substr(0,pos) >> mjr;
    val.substr(pos+1) >> mnr;

    file.close();
    file.clear();
  }

}

void MdPartCo::makeDevName(const string& name )
{
  if( name.find("/dev/") != string::npos )
    {
    dev = name;
    }
  else
    {
    dev = "/dev/" + name;
    }
}


bool MdPartCo::isImsmPlatform()
{
  bool ret = false;
  SystemCmd c;

  c.execute(MDADMBIN " --detail-platform");
  c.select( "Platform : " );
  //c.retcode()==0 && - mdadm returns 1.
  if(  c.numLines(true)>0 )
    {
    const string line = *c.getLine(0,true);
    if( line.find("Intel(R) Matrix Storage Manager") != string::npos )
      {
      ret = true;
      }
    }
  return ret;
}

/*
 * Return true if on RAID Volume has a partition table
 *
 * : /usr/sbin/parted name print - will return:
 * 1. List of partitions if partition table exits.
 * 2. Error if no partition table is on device.
 *
 * Ad 2. Clean newly created device or FS on device.
 */
bool MdPartCo::hasPartitionTable(const string& name )
{
  //bool ret = false;
  SystemCmd c;
  bool ret = false;

  string cmd = PARTEDCMD " " + quote("/dev/" + name) + " print";

  c.execute(cmd);

  //For clear md125 (just created)
  //Error: /dev/md125: unrecognised disk label
  //so $? contains 1.
  //If dev has partition table then $? contains 0
  if( c.retcode() == 0 )
    {
    ret = true;
    //Still - it can contain:
    //    del: Unknown (unknown)
    //    Disk /dev/md125: 133GB
    //    Sector size (logical/physical): 512B/512B
    //    Partition Table: loop

    c.select("Partition Table:");
    if(  c.numLines(true) > 0 )
      {
      string loop = *c.getLine(0,true);
      if( loop.find("loop") != string::npos )
        {
        // It has 'loop' partition table so it's actually a volume.
        ret = false;
        }
      }
    }
return ret;
}


/* Return true if there is no partition table and no FS */
bool MdPartCo::hasFileSystem(const string& name)
{
  //bool ret = false;
  SystemCmd c;
  string cmd = BLKIDBIN " -c /dev/null " + quote("/dev/" + name);

  c.execute(cmd);
  // IF filesystem was bit found then it will return no output end error core 2.
  if( c.retcode() != 0 )
    {
    return false;
    }
  // if FS is on device then it will be in TYPE="fsType" pair.
  return true;
}


void
MdPartCo::syncRaidtab()
{
    updateEntry();
}

int MdPartCo::getContMember()
{
  string::size_type pos = md_metadata.find_last_of("/");
  if( pos != string::npos )
    {
    unsigned mem;
    string tmp = md_metadata;
    tmp.erase(0,pos+1);
    tmp >> mem;
    return mem;
    }
  return -1;
}
void MdPartCo::updateEntry()
    {
    EtcRaidtab* tab = getStorage()->getRaidtab();
    if( tab )
      {
      EtcRaidtab::mdconf_info info;
      if( !md_name.empty() )
        {
        //Raid name is preferred.
        info.fs_name = "/dev/md/" + md_name;
        }
      else
        {
        info.fs_name = dev;
        }
      info.md_uuid = md_uuid;
      if( has_container )
        {
        info.container_present = true;
        info.container_info.md_uuid = parent_uuid;
        info.container_info.metadata = parent_metadata;
        info.member = getContMember();
        }
      else
        {
        info.container_present = false;
        }
      tab->updateEntry( info );
      }
    }

string MdPartCo::mdadmLine() const
    {
    string line = "ARRAY " + device() + " level=" + pName();
    line += " UUID=" + md_uuid;
    y2mil("line:" << line);
    return( line );
    }

void MdPartCo::raidtabLines( list<string>& lines ) const
    {
    lines.clear();
    lines.push_back( "raiddev " + device() );
    string tmp = "   raid-level            ";
    switch( md_type )
        {
        case RAID1:
            tmp += "1";
            break;
        case RAID5:
            tmp += "5";
            break;
        case RAID6:
            tmp += "6";
            break;
        case RAID10:
            tmp += "10";
            break;
        case MULTIPATH:
            tmp += "multipath";
            break;
        default:
            tmp += "0";
            break;
        }
    lines.push_back( tmp );
    lines.push_back( "   nr-raid-disks         " + decString(devs.size()));
    lines.push_back( "   nr-spare-disks        " + decString(spare.size()));
    lines.push_back( "   persistent-superblock 1" );
    if( md_parity!=PAR_NONE )
        lines.push_back( "   parity-algorithm      " + ptName());
    if( chunk_size>0 )
        lines.push_back( "   chunk-size            " + decString(chunk_size));
    unsigned cnt = 0;
    for( list<string>::const_iterator i=devs.begin(); i!=devs.end(); ++i )
        {
        lines.push_back( "   device                " + *i);
        lines.push_back( "   raid-disk             " + decString(cnt++));
        }
    cnt = 0;
    for( list<string>::const_iterator i=spare.begin(); i!=spare.end(); ++i )
        {
        lines.push_back( "   device                " + *i);
        lines.push_back( "   spare-disk            " + decString(cnt++));
        }
    }

int MdPartCo::scanForRaid(list<string>& raidNames)
{
  int ret = -1;
  SystemCmd c(MDADMBIN " -Es ");
  raidNames.clear();

  if( c.retcode() == 0 )
    {
    raidNames.clear();
    for(unsigned i = 0; i < c.numLines(false); i++ )
      {
      //Example:
      //ARRAY metadata=imsm UUID=b...5
      //ARRAY /dev/md/Vol_r5 container=b...5 member=0 UUID=0...c
      //ARRAY metadata=imsm UUID=8...b
      //ARRAY /dev/md/Vol0 container=8...b member=0 UUID=7...9
      string line = *c.getLine(i);
      string dev_name = extractNthWord( 1, line );
      if( dev_name.find("/dev/md/") == 0 )
        {
        dev_name.erase(0,8);
        raidNames.push_back(dev_name);
        }
      }
    ret = 0;
    }
  y2mil(" Detected list of MD RAIDs : " << raidNames);
  return ret;
}

storage::CType
MdPartCo::envSelection(const string& name)
{
  string big = name;
  std::transform(name.begin(), name.end(),
      big.begin(),(int(*)(int))std::toupper);
  string str = "YAST_STORAGE_" + big;
  char * tenv = getenv( str.c_str() );
  if( tenv == NULL )
    {
    return CUNKNOWN;
    }
  string isMd(tenv);
  if( isMd == "MD" )
    {
    return MD;
    }
  if( isMd == "MDPART" )
    {
    return MDPART;
    }
  return CUNKNOWN;
}

bool MdPartCo::havePartsInProc(const string& name, ProcPart& ppart)
{
  string reg;
  list <string> parts;
  // Search /proc/partitions for partitions.
  reg = name + "p[1-9]+";
  parts.clear();
  parts = ppart.getMatchingEntries( reg );
  if( !parts.empty() )
    {
    return true;
    }
  return false;
}

list<string> MdPartCo::filterMdPartCo(list<string>& raidList,
                                      ProcPart& ppart,
                                      bool isInst)
{
  y2mil(" called ");
  list<string> mdpList;

  for( list<string>::const_iterator i=raidList.begin(); i!=raidList.end(); ++i )
    {
    storage::CType ct = MdPartCo::envSelection(*i);
    if( ct == MD )
      {
      // skip
      continue;
      }
    if (ct == MDPART )
      {
      mdpList.push_back(*i);
      continue;
      }
    if( MdPartCo::havePartsInProc(*i,ppart) )
      {
      mdpList.push_back(*i);
      continue;
      }
    if( isInst )
      {
      // 1. With Partition Table
      // 2. Without Partition Table and without FS on it.
      // 3. this gives: No FS.
      if (!MdPartCo::hasFileSystem(*i))
        {
        mdpList.push_back(*i);
        }
      }
    else
      {
      // In 'normal' mode ONLY volume with Partition Table.
      // Partitions should be visible already so check it.
      if( MdPartCo::hasPartitionTable(*i))
        {
        mdpList.push_back(*i);
        }
      }
    } // for
  y2mil("List of partitionable devs: " << mdpList);
  return mdpList;
}

string MdPartCo::md_names[] = { "unknown", "raid0", "raid1", "raid5", "raid6",
                          "raid10", "multipath" };
string MdPartCo::par_names[] = { "none", "left-asymmetric", "left-symmetric",
                           "right-asymmetric", "right-symmetric" };
/* */
string MdPartCo::md_states[] = {"clear", "inactive", "suspended", "readonly",
                          "read-auto", "clean", "active", "write-pending",
                          "active-idle"};

string MdPartCo::md_props[] = {"metadata_version", "component_size", "chunk_size",
                       "array_state", "level", "layout" };
/* */
string MdPartCo::sysfs_path = "/sys/devices/virtual/block/";

unsigned MdPartCo::md_major = 0;

bool MdPartCo::active = false;

void MdPartCo::logData( const string& Dir ) {}


}
// Maintainer: fehr@suse.de

#include "config.h"

#include <ctype.h>
#include <string>
#include <sstream>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mount.h>         /* for BLKGETSIZE */
#include <linux/hdreg.h>       /* for HDIO_GETGEO */
#include <linux/fs.h>          /* for BLKGETSIZE64 */

#include <ycp/y2log.h>
#include "AppUtil.h"
#include "SystemCmd.h"
#include "InterCmd.h"
#include "AsciiFile.h"
#include "DiskAcc.h"

DiskAccess::DiskAccess(string Disk_Cv)
  : Disk_C(Disk_Cv),
    Changed_b(false),
    BsdLabel_b(false)
{
  y2debug( "Constructor called Disk:%s", Disk_Cv.c_str() );

#ifdef __alpha__
  AsciiFile CpuInfo_Ci("/proc/cpuinfo");
  string Line_Ci;
  if (SearchFile(CpuInfo_Ci, "system serial", Line_Ci))
    {
      Line_Ci.erase(0, Line_Ci.find(':') + 1);
      BsdLabel_b = Line_Ci.find("MILO") == string::npos;
    }
#endif

  if (Disk_C.length() > 0)
    {
      Head_i = Cylinder_i = Sector_i = 16;
      int Fd_ii = open (Disk_C.c_str(), O_RDONLY);
      int Ret_ii;
      if (Fd_ii >= 0)
	{
	struct hd_geometry Geometry_ri;
	Ret_ii = ioctl(Fd_ii, HDIO_GETGEO, &Geometry_ri );
	if( Ret_ii==0 )
	    {
	    Head_i = Geometry_ri.heads>0?Geometry_ri.heads:Head_i;
	    Sector_i = Geometry_ri.sectors>0?Geometry_ri.sectors:Sector_i;
	    Cylinder_i = Geometry_ri.cylinders>0?Geometry_ri.cylinders:Cylinder_i;
	    }
	y2milestone( "After HDIO_GETGEO ret %d Head:%u Sector:%u Cylinder:%u", 
	             Ret_ii, Head_i, Sector_i, Cylinder_i );
	__uint64_t Sect_uli;
	Sect_uli = 0;
	Ret_ii = ioctl( Fd_ii, BLKGETSIZE64, &Sect_uli);
	y2milestone( "BLKGETSIZE64 Ret:%d Bytes:%llu", Ret_ii, Sect_uli );
	if( Ret_ii==0 && Sect_uli!=0 )
	    {
	    Sect_uli /= 512;
	    Cylinder_i = (unsigned)(Sect_uli / (__uint64_t)(Head_i*Sector_i));
	    y2milestone( "BLKGETSIZE64 Head:%u Sector:%u Cylinder:%u", 
	                 Head_i, Sector_i, Cylinder_i );
	    }
	else
	    {
	    unsigned long Sect_li;
	    Ret_ii = ioctl( Fd_ii, BLKGETSIZE, &Sect_li);
	    y2milestone( "BLKGETSIZE Ret:%d Sect:%lu", Ret_ii, Sect_li );
	    if( Ret_ii==0 && Sect_li!=0 )
		{
		Cylinder_i = Sect_li / (unsigned long)(Head_i*Sector_i);
		}
	    y2milestone( "BLKGETSIZE Head:%u Sector:%u Cylinder:%u", 
	                 Head_i, Sector_i, Cylinder_i );
	    }
	close (Fd_ii);
	}
      ByteCyl_l = Head_i * Sector_i * 512;
    }
}

DiskAccess::~DiskAccess()
{
  y2debug( "Destructor called Disk:%s", Disk_C.c_str() );
}

vector<PartInfo>&
DiskAccess::Partitions()
{
  return Part_C;
}

string
DiskAccess::Disk()
{
  return Disk_C;
}

unsigned
DiskAccess::PrimaryMax()
{
#if defined(__sparc__)
  return 8;
#else
  return BsdLabel_b ? 8 : 4;
#endif
}

unsigned long
DiskAccess::CylinderToKb(int Cylinder_iv)
{
  return (unsigned long long)ByteCyl_l * Cylinder_iv / 1024;
}

int
DiskAccess::KbToCylinder(unsigned long Kb_lv)
{
  unsigned long long Bytes_li = Kb_lv;
  Bytes_li *= 1024;
  Bytes_li += ByteCyl_l - 1;
  int Cyl_ii = Bytes_li / (unsigned long long)ByteCyl_l;
  return (Cyl_ii);
}

int
DiskAccess::NumCylinder()
{
  return Cylinder_i;
}

string
DiskAccess::Stderr()
{
  return Stderr_C;
}

unsigned long
DiskAccess::CapacityInKb()
{
  return (unsigned long long)ByteCyl_l * Cylinder_i / 1024;
}

bool
DiskAccess::IsKnownDevice(const string& Part_Cv)
    {
    bool Ret_bi = false;
    struct stat sbuf;
    if( stat( Part_Cv.c_str(), &sbuf )==0 && S_ISBLK(sbuf.st_mode) &&
        Part_Cv.find("/dev/ram")!=0 && Part_Cv.find("/dev/loop")!=0 &&
        Part_Cv.find("/dev/evms")!=0 && Part_Cv.find("/dev/mapper")!=0 )
	{
	Ret_bi = true;
	}
    return( Ret_bi );
    }

int
DiskAccess::GetPartNumber( const string& Part_Cv )
    {
    int Ret_ii=0;
    string Tmp_Ci = Part_Cv;
    Tmp_Ci.erase( 0, GetDiskName(Part_Cv).length() );
    if( Tmp_Ci.length()>0 && Tmp_Ci[0]=='p' )
        {
        Tmp_Ci.erase( 0, 1 );
        }
    sscanf( Tmp_Ci.c_str(), "%d", &Ret_ii );
    return( Ret_ii );
    }


string
DiskAccess::GetDiskName(string Part_Cv)
{
  string::size_type Idx_ii;

  if (Part_Cv.find("/dev/sd") == 0 || Part_Cv.find("/dev/hd") == 0 ||
      Part_Cv.find("/dev/ed") == 0)
    return Part_Cv.substr(0, 8);

  if( Part_Cv.find( "/dev/i2o/hd" ) == 0 )
    return Part_Cv.substr (0, 12);

  if( (Part_Cv.find("/dev/ida/")==0
       || Part_Cv.find("/dev/rd/")==0
       || Part_Cv.find("/dev/cciss/")==0) &&
      (Idx_ii=Part_Cv.find('p')) != string::npos)
    return Part_Cv.substr (0, Idx_ii);

  if (Part_Cv.find("/dev/dasd")==0 )
    return Part_Cv.substr(0, Part_Cv.length()-1);

  return Part_Cv;
}





string
DiskAccess::GetPartDeviceName(int Num_iv)
{
  return GetPartDeviceName(Num_iv, Disk());
}

string
DiskAccess::GetPartDeviceName(int Num_iv, string Disk_Cv)
{
  string Ret_Ci = Disk_Cv;
  if (Disk_Cv.find("/dev/cciss/") == 0 || Disk_Cv.find("/dev/ida/") == 0 ||
      Disk_Cv.find("/dev/ataraid/") == 0 || Disk_Cv.find("/dev/rd/") == 0 ||
      Disk_Cv.find("/dev/etherd/") == 0 )
      Ret_Ci += "p";
  Ret_Ci += dec_string(Num_iv);
  return Ret_Ci;
}

string 
DiskAccess::DiskLabel()
    {
    return( Label_C );
    }


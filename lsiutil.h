#pragma once

#define LSIUTIL_VERSION "Version 1.71, Sep 18, 2013"

/* SAS3108 FPGA-specific defines*/
#define SAS3108_FPGA_WORKAROUND (1)
#define SAS3108_FPGA_VENDORID   (0x702)


#ifndef MAX_DEVICES
#define MAX_DEVICES 99
#endif


#ifndef REGISTER_ACCESS
#define REGISTER_ACCESS 1
#endif


#ifndef VERIFY_ENDIANNESS
#define VERIFY_ENDIANNESS 0
#endif
#define		MEGA_DEVICE	"/dev/megaraid_sas_ioctl_node"


#if !EFI
#include <fcntl.h>
#if WIN32
#include <io.h>
#else
#define _cdecl
#endif
#include <sys/stat.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <syslog.h>
#if WIN32
#include <windows.h>
#include "inc/devioctl.h"
#include <basetsd.h>
#include <errno.h>
#ifdef _CONSOLE
#pragma warning(disable:4242)
#include "inc/getopt.h"
#define sleep(x) _sleep(x*1000)
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#if !_WIN64
typedef unsigned long ULONG_PTR;
#endif
#define stricmp _stricmp
#define strnicmp _strnicmp
#define open _open
#define close _close
#define read _read
#define write _write
#define stat _stat
#define fstat _fstat
#define INT64_FMT "I64"
#else
#define INT64_FMT "ll"
#include <unistd.h>
#include <getopt.h>
int optopt;
int optind;
#endif
#define strcasecmp stricmp
#define strncasecmp strnicmp
#include "inc/ntddscsi.h"
/* MINGW is a little different from the DDK */
#if __MINGW32__
#define offsetof(type,member) ((size_t)&((type *)0)->member)
#define sleep(x) Sleep(x*1000)
#endif
#endif
#if __linux__ || __sparc__ || __irix__ || __alpha__
#include <stdarg.h>
#include <unistd.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#define FALSE 0
#define TRUE 1
typedef int HANDLE;
#define O_BINARY 0
#define min(x,y) ((int)(x) < (int)(y) ? (x) : (y))
#define max(x,y) ((int)(x) > (int)(y) ? (x) : (y))
#define INT64_FMT "ll"
#endif
#if __linux__
#include <linux/stddef.h>
#ifndef offsetof
#define offsetof(type,member) ((size_t)&((type *)0)->member)
#endif
#define TotalBufferSize DataSize
#define DataBuffer DiagnosticData
#include <scsi/scsi.h>
#if i386
#include <sys/io.h>
#endif
#define LINUX_MOD_DEVICETABLE_H
#include <linux/pci.h>
#include <sys/mman.h>
#define REG_IO_READ 1
#define REG_IO_WRITE 2
#define REG_MEM_READ 3
#define REG_MEM_WRITE 4
#define REG_DIAG_READ 5
#define REG_DIAG_WRITE 6
#define REG_DIAG_WRITE_BYTE 7
#endif
#if __sparc__
#include <libdevinfo.h>
#include <stddef.h>
#include <sys/param.h>
#include <sys/mkdev.h>
typedef struct
{
    caddr_t          client;
    caddr_t          phci;
    caddr_t          addr;
    uint_t           buf_elem;
    void            *ret_buf;
    uint_t          *ret_elem;
} sv_iocdata_t;
#define SCSI_VHCI_GET_CLIENT_NAME (('x' << 8) + 0x03)
#define NAME_MAX MAXNAMLEN
#define getmajor(x) (((x)>>NBITSMINOR)&MAXMAJ)
#define getminor(x) ((x)&MAXMIN)
#define MINOR2INST(x) ((x)>>6)
#endif
#if DOS
#include <unistd.h>
#include <conio.h>
#include <dos.h>
#include <time.h>
#include <errno.h>
#include <stddef.h>
#include <stdarg.h>
#include "inc/getopt.h"
#define FALSE 0
#define TRUE 1
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
typedef unsigned int PHYSICAL_ADDRESS;
typedef unsigned int mpt_bus_addr_t;
typedef struct mpt_adap *HANDLE;
#define strcasecmp stricmp
#define strncasecmp strnicmp
#define DELAY(n) mpt_delay(adap, n)
#define REG_IO_READ 1
#define REG_IO_WRITE 2
#define REG_MEM_READ 3
#define REG_MEM_WRITE 4
#define REG_DIAG_READ 5
#define REG_DIAG_WRITE 6
#define REG_DIAG_WRITE_BYTE 7
#define INT64_FMT "I64"
#endif
#endif
#if EFI
#if EFIEBC
#pragma warning(disable:175)
#endif
#define _cdecl
#include "helper.h"
#include "getopt.h"
#define O_BINARY 0
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
typedef EFI_PHYSICAL_ADDRESS mpt_bus_addr_t;
typedef struct mpt_adap *HANDLE;
#define min(x,y) ((int)(x) < (int)(y) ? (x) : (y))
#define max(x,y) ((int)(x) > (int)(y) ? (x) : (y))
#define DELAY(n) mpt_delay(adap, n)
#define REG_IO_READ 1
#define REG_IO_WRITE 2
#define REG_MEM_READ 3
#define REG_MEM_WRITE 4
#define REG_DIAG_READ 5
#define REG_DIAG_WRITE 6
#define REG_DIAG_WRITE_BYTE 7
#define INT64_FMT "ll"
extern EFI_HANDLE gImageHandle;
extern EFI_LOADED_IMAGE *gLoadedImage;
#endif
#if DOS || EFI
#define CHUNK_SIZE 0x10000
#else
#define CHUNK_SIZE 0x4000
#endif


typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int U32;
typedef struct { U32 Low; U32 High; } U64;
#define MPI_POINTER *
#define MPI2_POINTER *


#if VERIFY_ENDIANNESS
typedef U16 * _U16;
typedef U32 * _U32;
typedef struct { _U32 Low; _U32 High; } _U64;
#else
typedef U16 _U16;
typedef U32 _U32;
typedef U64 _U64;
#endif


#if VERIFY_ENDIANNESS
#define U16 _U16
#define U32 _U32
#define U64 _U64
#endif
#if WIN32 || __linux__ || __sparc__ || DOS || EFI
#pragma pack(1)
#include "lsi/mpi.h"
#include "lsi/mpi_ioc.h"
#include "lsi/mpi_cnfg.h"
#include "lsi/mpi_init.h"
#include "lsi/mpi_fc.h"
#include "lsi/mpi_sas.h"
#include "lsi/mpi_raid.h"
#include "lsi/mpi_tool.h"
#include "lsi/mpi2.h"
#include "lsi/mpi2_ioc.h"
#include "lsi/mpi2_cnfg.h"
#include "lsi/mpi2_init.h"
#include "lsi/mpi2_sas.h"
#include "lsi/mpi2_raid.h"
#include "lsi/mpi2_tool.h"
#pragma pack()
#endif
#if VERIFY_ENDIANNESS
#undef U16
#undef U32
#undef U64
#endif


#if WIN32
#include "inc/sym_dmi.h"
#define ISSUE_BUS_RESET 0x800000FF
typedef struct
{
    SRB_IO_CONTROL   Sic;
    UCHAR            Buf[8+128+1024];
} SRB_BUFFER;
typedef struct
{
    SRB_IO_CONTROL   Sic;
    ULONG            DiagType;
    UCHAR            PageVersion[4];
    UCHAR            Buf[64+32768];
} SRB_DIAG_BUFFER;
#endif
#if __linux__
#ifndef __user
#define __user
#endif
typedef U8 u8;
typedef U16 u16;
typedef U32 u32;
#include "inc/mptctl.h"
typedef U8 uint8_t;
typedef U16 uint16_t;
typedef U32 uint32_t;
#include "inc/mpt2sas_ctl.h"
#define IOCTL_NAME "/dev/" MPT_MISCDEV_BASENAME
#define IOCTL_NAME2 "/dev/" MPT2SAS_DEV_NAME
#define IOCTL_NAME3 "/dev/mpt3ctl"
#ifdef MPI_FW_DIAG_IOCTL
#define LINUX_DIAG 1
typedef struct
{
    struct mpt2_ioctl_header hdr;
    unsigned char    buf[64+32768];
} IOCTL_DIAG_BUFFER;
#endif
#endif
#if __sparc__
#define TARGET_MPTx
typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
#include "inc/dmi_ioctl.h"
#include "inc/mptsas_ioctl.h"
#endif
#if __irix__
#define MPI_POINTER *
typedef uint8_t U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef struct
{
    U32      Low;
    U32      High;
} mpiU64;
#pragma pack(1)
#define U64 mpiU64
#include "mpi.h"
#include "mpi_ioc.h"
#include "mpi_cnfg.h"
#include "mpi_init.h"
#include "mpi_fc.h"
#include "mpi_sas.h"
#include "mpi_raid.h"
#include "mpi_tool.h"
#pragma pack(0)
#undef U64
#define TARGET_MPT
typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
#include "dmi_ioctl.h"
#include <sys/scsi.h>
#endif
#if __alpha__
typedef unsigned __int64 uint64_t;
#define MPI_POINTER *
typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int U32;
typedef unsigned long U64;
typedef struct
{
    U32      Low;
    U32      High;
} mpiU64;
#pragma pack(1)
#define U64 mpiU64
#include "mpi.h"
#include "mpi_ioc.h"
#include "mpi_cnfg.h"
#include "mpi_init.h"
#include "mpi_fc.h"
#include "mpi_sas.h"
#include "mpi_raid.h"
#include "mpi_tool.h"
#pragma pack(0)
#undef U64
typedef U8 u8;
typedef U16 u16;
typedef U32 u32;
typedef U64 u64;
#include "mptctl.h"
#endif
#if DOS
#include "pcidefs.h"
#include "pcilib.h"
#include "dpmilib.h"
#endif


#undef __LSIUTIL_BIG_ENDIAN__
#define swap16(x)            \
    ((((U16)(x)>>8)&0xff) |  \
     (((U16)(x)&0xff)<<8))
#define swap32(x)                 \
    ((((U32)(x)>>24)&0xff) |      \
    ((((U32)(x)>>16)&0xff)<<8) |  \
    ((((U32)(x)>>8)&0xff)<<16) |  \
     (((U32)(x)&0xff)<<24))
#if WIN32 || __alpha__ || DOS || EFI
#define get16(x) (x)
#define get32(x) (x)
#define set16(x) (x)
#define set32(x) (x)
#define get16x(x) (x)
#define get32x(x) (x)
#define set16x(x) (x)
#define set32x(x) (x)
#define get16x_be(x) swap16(x)
#define get32x_be(x) swap32(x)
#define set16x_be(x) swap16(x)
#define set32x_be(x) swap32(x)
#endif
#if __linux__
#include <endian.h>
#include <linux/types.h>
#if __BYTE_ORDER == __BIG_ENDIAN
#include <linux/byteorder/big_endian.h>
#define __LSIUTIL_BIG_ENDIAN__      1
#endif
#if __BYTE_ORDER == __LITTLE_ENDIAN
#include <linux/byteorder/little_endian.h>
#endif
#define get16(x) __le16_to_cpu(x)
#define get32(x) __le32_to_cpu(x)
#define set16(x) __cpu_to_le16(x)
#define set32(x) __cpu_to_le32(x)
#define get16x(x) __le16_to_cpu(x)
#define get32x(x) __le32_to_cpu(x)
#define set16x(x) __cpu_to_le16(x)
#define set32x(x) __cpu_to_le32(x)
#define get16x_be(x) __be16_to_cpu(x)
#define get32x_be(x) __be32_to_cpu(x)
#define set16x_be(x) __cpu_to_be16(x)
#define set32x_be(x) __cpu_to_be32(x)
#endif
#if __sparc__ || __irix__
#if i386
#define get16(x) (x)
#define get32(x) (x)
#define set16(x) (x)
#define set32(x) (x)
#define get16x(x) (x)
#define get32x(x) (x)
#define set16x(x) (x)
#define set32x(x) (x)
#define get16x_be(x) swap16(x)
#define get32x_be(x) swap32(x)
#define set16x_be(x) swap16(x)
#define set32x_be(x) swap32(x)
#else
#define get16(x) swap16(x)
#define get32(x) swap32(x)
#define set16(x) swap16(x)
#define set32(x) swap32(x)
#define get16x(x) swap16(x)
#define get32x(x) swap32(x)
#define set16x(x) swap16(x)
#define set32x(x) swap32(x)
#define get16x_be(x) (x)
#define get32x_be(x) (x)
#define set16x_be(x) (x)
#define set32x_be(x) (x)
#define __LSIUTIL_BIG_ENDIAN__      1
#endif
#endif
#define get64(x) (((uint64_t)get32x(((U32 *)&(x))[1])<<32) | get32x(((U32 *)&(x))[0]))
#define get64x(x) (((uint64_t)get32x(((U32 *)&(x))[1])<<32) | get32x(((U32 *)&(x))[0]))


/* These need to be included after the __LSIUTIL_BIG_ENDIAN__ define as they rely on it */
#include "inc/ata.h"
#include "inc/sas.h"


#if VERIFY_ENDIANNESS
#undef get16
#undef get32
#undef set16
#undef set32
U16 get16(U16 *x) { return 0; }
U32 get32(U32 *x) { return 0; }
U16 *set16(int x) { return NULL; }
U32 *set32(int x) { return NULL; }
#endif


#define get2bytes(x, y) (((x[y] << 8) + x[y+1]) & 0xffff)
#define get3bytes(x, y) (((x[y] << 16) + (x[y+1] << 8) + x[y+2]) & 0xffffff)
#define get4bytes(x, y) (((x[y] << 24) + (x[y+1] << 16) + (x[y+2] << 8) + x[y+3]) & 0xffffffff)
#define get8bytes(x, y) (((uint64_t)get4bytes(x, y) << 32) + get4bytes(x, y+4))
#define put2bytes(x, y, z)    \
    x[y] = (U8)((z) >> 8);    \
    x[y+1] = (U8)(z)
#define put3bytes(x, y, z)    \
    x[y] = (U8)((z) >> 16);   \
    x[y+1] = (U8)((z) >> 8);  \
    x[y+2] = (U8)(z)
#define put4bytes(x, y, z)     \
    x[y] = (U8)((z) >> 24);    \
    x[y+1] = (U8)((z) >> 16);  \
    x[y+2] = (U8)((z) >> 8);   \
    x[y+3] = (U8)(z)


#if REGISTER_ACCESS
#define readl(addr, data) \
    { \
        U32 temp; \
        if (doReadRegister(port, MPI_##addr##_OFFSET, &temp) != 1) \
        { \
            printf("Failed to read register!\n"); \
            return 0; \
        } \
        data = temp; \
    }


#define writel(addr, data) \
    { \
        U32 temp = data; \
        if (doWriteRegister(port, MPI_##addr##_OFFSET, &temp) != 1) \
        { \
            printf("Failed to write register!\n"); \
            return 0; \
        } \
    }
#endif


#define IO_TIME 20
#define RESET_TIME 60
#define SHORT_TIME 10
#define LONG_TIME 120


#define BOBCAT_FW_HEADER_SIGNATURE_0       ( 0xB0EABCA7 )
#define BOBCAT_FW_HEADER_SIGNATURE_1       ( 0xB0BCEAA7 )
#define BOBCAT_FW_HEADER_SIGNATURE_2       ( 0xB0BCA7EA )

#define COBRA_FW_HEADER_SIGNATURE_0       ( 0xC0EABAA0 )
#define COBRA_FW_HEADER_SIGNATURE_1       ( 0xC0BAEAA0 )
#define COBRA_FW_HEADER_SIGNATURE_2       ( 0xC0BAA0EA )


#define ALLOCATED_RESP_LEN    0xff
#define NUM_PORTS 64


typedef struct
{
    U8               signature[4];
   _U16              vendorId;
   _U16              deviceId;
    U8               reserved1[2];
   _U16              pcirLength;
    U8               pcirRevision;
    U8               classCode[3];
   _U16              imageLength;
   _U16              imageRevision;
    U8               type;
    U8               indicator;
    U8               reserved2[2];
} PCIR;


typedef struct
{
    int              portNumber;
    char             portName[16];
#if WIN32
    char             driverName[32];
#endif
#if __sparc__
    char             pathName[PATH_MAX];
    U32              portPhyMap[32];
#endif
    HANDLE           fileHandle;
    int              ioctlValue;
    int              iocNumber;
    int              hostNumber;
    int              mptVersion;
    int              fwVersion;
    int              whoInit;
    U16              deviceIdRaw;
    U16              deviceId;
    U8               revisionId;
    U16              productId;
    int              pidType;
    U32              capabilities;
    U8               flags;
    U32              fwImageSize;
    char            *chipName;
    char            *chipNameRev;
    char            *pciType;
    U32              seqCodeVersion;
    int              payOff;
    int              portType;
    int              maxPersistentIds;
    int              maxBuses;
    int              minTargets;
    int              maxTargets;
    int              maxLuns;
    int              maxDevHandle;
    int              numPhys;
    int              hostScsiId;
    int              protocolFlags;
    int              lastEvent;
#if LINUX_DIAG
    int              diagBufferSizes[MPI_DIAG_BUF_TYPE_COUNT];
#endif
#if __linux__
    off_t            ioPhys;
    off_t            memPhys;
    U32             *memVirt;
    off_t            diagPhys;
    U32             *diagVirt;
#endif
    int              notOperational;
    int              pciSegment;
    int              pciBus;
    int              pciDevice;
    int              pciFunction;
    int              raidPassthru;
    int              raidBus;
    int              raidTarget;
    int              raidPhysdisk;
    int              fastpathCapable;
    U16              ioc_status;        // Currently only updated during (get|set)ConfigPage()
} MPT_PORT;


typedef struct
{
    SCSIIOReply_t    reply;
    U8               sense[32];
} SCSI_REPLY;


typedef struct
{
    Mpi2SCSIIOReply_t    reply;
    U8                   sense[32];
} SCSI_REPLY2;


typedef struct
{
    int              slot;
    int              encl_id_l;
    int              encl_id_h;
} PATH;


typedef struct
{
    int              bus;
    int              target;
    int              lun;
    PATH             path;
    int              mode;
    unsigned int     size;
    uint64_t         size64;
    int              eedp;
} DIAG_TARGET;


#define EXPANDER_TYPE_LSI_GEN1_YETI     1
#define EXPANDER_TYPE_LSI_GEN1_X12      2
#define EXPANDER_TYPE_LSI_GEN2_BOBCAT   3
#define EXPANDER_TYPE_LSI_GEN3_COBRA    4
#define EXPANDER_TYPE_3RD_PARTY         8
#define EXPANDER_TYPE_UNKNOWN           9

typedef struct
{
    int              bus;
    int              target;
    int              handle;
   _U64              sas_address;
    U8               physical_port;
    int              expanderType;
} EXP_TARGET;


typedef struct
{
    int              type;
    int              number;
    int              data[2];
} EVENT;


typedef struct
{
    int              type;
    int              number;
    int              data[48];
} EVENT2;


typedef struct
{
   _U32              Size;
   _U32              DiagVersion;
    U8               BufferType;
    U8               Reserved[3];
   _U32              Reserved1;
   _U32              Reserved2;
   _U32              Reserved3;
} DIAG_BUFFER_START;


typedef struct
{
   _U32              Size;
   _U16              Type;
    U8               Version;
    U8               Reserved;
   _U32              CapabilitiesFlags;
   _U32              FWVersion;
   _U16              ProductId;
   _U16              Reserved1;
} DIAG_HEADER_FIRM_IDENTIFICATION;


typedef struct
{
   _U32              Size;
   _U16              Type;
    U8               Version;
    U8               Reserved;
    U8               HostInfo[256];
} DIAG_HEADER_HOST_IDENTIFICATION;


/* diag register for gen 2 */
typedef struct _MPI2_FW_DIAG_REGISTER
{
    U8                  Reserved;
    U8                  BufferType;
    U16                 AppFlags;
    U32                 DiagFlags;
    U32                 ProductSpecific[23];
    U32                 RequestedBufferSize;
    U32                 UniqueId;
} MPI2_FW_DIAG_REGISTER, *PTR_MPI2_FW_DIAG_REGISTER;


/* diag query for gen 2 */
typedef struct _MPI2_FW_DIAG_QUERY
{
    U8                  Reserved;
    U8                  BufferType;
    U16                 AppFlags;
    U32                 DiagFlags;
    U32                 ProductSpecific[23];
    U32                 TotalBufferSize;
    U32                 DriverAddedBufferSize;
    U32                 UniqueId;
} MPI2_FW_DIAG_QUERY, *PTR_MPI2_FW_DIAG_QUERY;


#if WIN32
#define IOCTL_STORAGE_BASE FILE_DEVICE_MASS_STORAGE
#define IOCTL_STORAGE_QUERY_PROPERTY   CTL_CODE(IOCTL_STORAGE_BASE, 0x0500, METHOD_BUFFERED, FILE_ANY_ACCESS)


typedef struct _STORAGE_ADAPTER_DESCRIPTOR {
  ULONG   Version;
  ULONG   Size;
  ULONG   MaximumTransferLength;
  ULONG   MaximumPhysicalPages;
  ULONG   AlignmentMask;
  BOOLEAN AdapterUsesPio;
  BOOLEAN AdapterScansDown;
  BOOLEAN CommandQueueing;
  BOOLEAN AcceleratedTransfer;
  UCHAR   BusType;
  USHORT  BusMajorVersion;
  USHORT  BusMinorVersion;
} STORAGE_ADAPTER_DESCRIPTOR, *PSTORAGE_ADAPTER_DESCRIPTOR;


typedef enum _STORAGE_PROPERTY_ID {
  StorageDeviceProperty                   = 0,
  StorageAdapterProperty,
  StorageDeviceIdProperty,
  StorageDeviceUniqueIdProperty,
  StorageDeviceWriteCacheProperty,
  StorageMiniportProperty,
  StorageAccessAlignmentProperty,
  StorageDeviceSeekPenaltyProperty,
  StorageDeviceTrimProperty,
  StorageDeviceWriteAggregationProperty
} STORAGE_PROPERTY_ID, *PSTORAGE_PROPERTY_ID;


typedef enum _STORAGE_QUERY_TYPE {
  PropertyStandardQuery     = 0,
  PropertyExistsQuery,
  PropertyMaskQuery,
  PropertyQueryMaxDefined
} STORAGE_QUERY_TYPE, *PSTORAGE_QUERY_TYPE;


typedef struct _STORAGE_PROPERTY_QUERY {
  STORAGE_PROPERTY_ID PropertyId;
  STORAGE_QUERY_TYPE  QueryType;
  UCHAR               AdditionalParameters[1];
} STORAGE_PROPERTY_QUERY, *PSTORAGE_PROPERTY_QUERY;
#endif //WIN32

typedef struct
{
    char             name[NAME_MAX];
    char             link[PATH_MAX];
} NAME_LINK;

#define MAX_DISK_NUM 48

typedef struct LsiDisk {
    int target_id;
    int slot;
}LsiDisk_t;




#define JUST_FC 1
#define JUST_SCSI 2
#define JUST_SAS 4
#define JUST_ALL (JUST_FC | JUST_SCSI | JUST_SAS)




#define MPI1    if (mpi1)
#define MPI2    if (mpi2)
#define MPI20   if (mpi20)
#define MPI25   if (mpi25)
#define EXP     if (expert)

#define FLASH_RESET_INTEL   0xff
#define FLASH_RESET_AMD     0xf0
#define FLASH_IDENTIFY      0x90
#define FLASH_CFI_QUERY     0x98


// MPI2 2.0.10 header file retired CurReplyFrameSize field
// in IOC Facts reply.  Define OldReplyFrameSize to use as
// backward compatible reference
#define OldReplyFrameSize IOCMaxChainSegmentSize


/* user command line arguments */
typedef struct
{
    int              portNums[NUM_PORTS];   /* port numbers specified */
    int              numPortNums;           /* number of port numbers specified */
    int              boardInfo;             /* board info wanted */
    int              scan;                  /* boolean */
    int              info;                  /* boolean */
    int              dump;                  /* boolean */
    char             linkspeed;             /* desired link speed ('a', '1', '2', or '4') */
    char             topology;              /* desired topology ('a', '1', or '2') */
    int              reset;                 /* boolean for chip reset */
    int              linkReset;             /* boolean for link reset */
    int              linkResetFlag;         /* boolean for link reset type */
    int              coalescing;            /* boolean */
    int              ic_depth;              /* desired interrupt coalescing depth */
    int              ic_timeout;            /* desired interrupt coalescing timeout */
    int              monitorInterval;       /* desired monitoring interval */
    int              monitorDuration;       /* desired monitoring duration */
} CMNDLINE_ARGS;


int fprintfPaged(FILE *fp, const char *format, ...);
int getPortInfo(MPT_PORT *port);
int closePort(MPT_PORT *port);
int getIocFacts(MPT_PORT *port, IOCFactsReply_t *rep);
int getPortInfo2(MPT_PORT *port);
int getPortFacts(MPT_PORT *port, PortFactsReply_t *rep);
int getConfigPage(MPT_PORT *port, int type, int number, int address, void *page, int pageSize);
int getChipName(MPT_PORT *port);
int getIocFacts2(MPT_PORT *port, Mpi2IOCFactsReply_t *rep);
int setMaxBusTarget(MPT_PORT *port);
int getPortFacts2(MPT_PORT *port, Mpi2PortFactsReply_t *rep);
int doMptCommand(MPT_PORT *port, void *req, int reqSize, void *rep, int repSize,
             void *payIn, int payInSize, void *payOut, int payOutSize, int timeOut);
int mapBusTargetToDevHandle(MPT_PORT *port, int bus, int target, int *dev_handle);
int mapDevHandleToBusTarget(MPT_PORT *port, int dev_handle, int *bus, int *target);
int mapOsToHwTarget(MPT_PORT *port, int target);
int mapBTDH(MPT_PORT *port, int *bus, int *target, int *dev_handle);
int doScsiIo(MPT_PORT *port, int bus, int target, int raid, void *req, int reqSize, SCSI_REPLY *rep1, int rep1Size,
         void *payIn, int payInSize, void *payOut, int payOutSize, int timeOut);
char *translateIocStatus(int ioc_status);
int doMptCommandCheck(MPT_PORT *port, void *req, int reqSize, void *rep, int repSize,
                  void *payIn, int payInSize, void *payOut, int payOutSize, int timeOut);



int singlePortGetTargetAndSlotId(LsiDisk_t *lsiDisks);
int getTargetandSlotIdFromMegaraid(LsiDisk_t *LsiDisks);



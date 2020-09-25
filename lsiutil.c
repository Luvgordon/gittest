/***************************************************************************
 *                                                                         *
 *  Copyright (c) 2002-2013 LSI Corporation.  All rights reserved.         *
 *                                                                         *
 *  This file is confidential and a trade secret of LSI Corporation.  The  *
 *  receipt of or possession of this file does not convey any rights to    *
 *  reproduce or disclose its contents or to manufacture, use, or sell     *
 *  anything it may describe, in whole, or in part, without the specific   *
 *  written consent of LSI Corporation.                                    *
 *                                                                         *
 ***************************************************************************
 * LSIUtil -- configuration utility for MPT adapters (FC, SCSI, and SAS/SATA)
 *
 * Written by Stephen F. Shirron, October 11, 2002
*/
#include "lsiutil.h"
#include <stdio.h>
//MPT_PORT            *mptPorts[NUM_PORTS];


MPT_PORT            *mappedPort;
int                  mappedBus;
int                  mappedTarget;
int                  mappedDevHandle;
int                  mappedValue;
int                  tagType;
int                  expert;
int                  maxLuns = 256;

#if __linux__
int                  workaroundsTried = FALSE;
int                  oldMptBaseDetected = 0;
int                  newMptBaseDetected = 0;
#endif


#if __linux__ || __alpha__
HANDLE               globalFileHandle;
HANDLE               globalFileHandle2;
HANDLE               globalFileHandle3;
#endif
int                 just = 0;
int                 virtInit = 0;

#define mpi1    (port->mptVersion < MPI2_VERSION_02_00)
#define mpi2    (port->mptVersion >= MPI2_VERSION_02_00)
#define mpi20   ((port->mptVersion >= MPI2_VERSION_02_00) && (port->mptVersion < MPI2_VERSION_02_05))
#define mpi25   (port->mptVersion >= MPI2_VERSION_02_05)


int
doReadWriteRegister(MPT_PORT *port, U32 offset, U32 *data, int command)
{
#if __linux__
#if i386
    if (command == REG_IO_READ)
    {
        if (port->ioPhys == 0)
            return 0;

        *data = inl(port->ioPhys + offset);
        return 1;
    }

    if (command == REG_IO_WRITE)
    {
        if (port->ioPhys == 0)
            return 0;

        outl(*data, port->ioPhys + offset);
        return 1;
    }
#endif

    if (command == REG_MEM_READ)
    {
        if (port->memPhys == 0)
            return 0;

        *data = get32x(*(port->memVirt + offset / 4));
        return 1;
    }

    if (command == REG_MEM_WRITE)
    {
        if (port->memPhys == 0)
            return 0;

        *(port->memVirt + offset / 4) = set32x(*data);
        return 1;
    }

    if (command == REG_DIAG_READ)
    {
        if (port->diagPhys == 0)
            return 0;

        *data = get32x(*(port->diagVirt + offset / 4));
        return 1;
    }

    if (command == REG_DIAG_WRITE)
    {
        if (port->diagPhys == 0)
            return 0;

        *(port->diagVirt + offset / 4) = set32x(*data);
        return 1;
    }

    if (command == REG_DIAG_WRITE_BYTE)
    {
        if (port->diagPhys == 0)
            return 0;

        *((U8 *)port->diagVirt + offset) = (U8)*data;
        return 1;
    }

    return 0;
#endif
}

int
doReadRegister(MPT_PORT *port, U32 offset, U32 *data)
{
    int command = REG_MEM_READ;

    if (port->deviceId == MPI_MANUFACTPAGE_DEVID_53C1030 &&
        (offset == MPI_DIAG_RW_DATA_OFFSET || offset == MPI_DIAG_RW_ADDRESS_OFFSET))
    {
        command = REG_IO_READ;
    }

    return doReadWriteRegister(port, offset, data, command);
}


int
checkOperational(MPT_PORT *port, int flag)
{
#if REGISTER_ACCESS
#if WIN32 || __linux__ || __sparc__
    U32      data;

    if (doReadRegister(port, MPI_DOORBELL_OFFSET, &data) == 1)
    {
        if ((data & MPI_IOC_STATE_MASK) != MPI_IOC_STATE_OPERATIONAL)
        {
            if (!port->notOperational)
            {
                port->notOperational = 1;
                printf("\n%s is not in Operational state!  Doorbell is %08x\n",
                       port->portName, data);
            }

            return 0;
        }

        port->notOperational = 0;
    }

    return 1;
#endif
#else
    return 1;
#endif
}


int
findPorts(MPT_PORT            **mptports)
{
    int                              numPorts;
    int                              status;
    HANDLE                           fileHandle;
    HANDLE                           fileHandle1;
    HANDLE                           fileHandle2;
    HANDLE                           fileHandle3;
    FILE                            *portFile;
    char                             portName[64];
    int                              portNumber;
    char                             pathName[PATH_MAX];
    MPT_PORT                        *port;
    struct mpt_ioctl_eventquery      eventquery;
    struct mpt2_ioctl_eventquery     eventquery2;
    struct mpt_ioctl_iocinfo        *iocinfo;
    struct mpt_ioctl_iocinfo_rev1   *iocinfo2;
    int                              domain = 0;
    int                              bus = 0;
    int                              device = 0;
    int                              function = 0;
    HANDLE                           pciHandle;
    unsigned char                    config[64];
    char                            *p;
    char                            *q;
    char                             iocEntry[64];
    FILE                            *iocFile;
    int                              i;
    U16                              deviceIdRaw;
    U16                              deviceId;
#if REGISTER_ACCESS
    char                             resource[64];
    uint64_t                         t, ts;
    off_t                            bar0;
    off_t                            bar1;
    off_t                            bar2;
    size_t                           bar0size;
    size_t                           bar1size;
    size_t                           bar2size;
    char                             portName1[64];
    char                             portName2[64];
    HANDLE                           pciHandle1;
    HANDLE                           pciHandle2;
#endif

  
    fileHandle1 = open(IOCTL_NAME, O_RDWR);
    fileHandle2 = open(IOCTL_NAME2, O_RDWR);
    fileHandle3 = open(IOCTL_NAME3, O_RDWR);

    if (fileHandle1 < 0 && fileHandle2 < 0 && fileHandle3 < 0)
    {
        printf("Couldn't open " IOCTL_NAME " or " IOCTL_NAME2 " or " IOCTL_NAME3 "!\n");
        return 0;
    }

    //globalFileHandle = fileHandle;
    //globalFileHandle2 = fileHandle2;
    //globalFileHandle3 = fileHandle3;

    memset(&eventquery, 0, sizeof eventquery);
    eventquery.hdr.maxDataSize = sizeof eventquery;

    memset(&eventquery2, 0, sizeof eventquery2);
    eventquery2.hdr.max_data_size = sizeof eventquery2;

    iocinfo = (struct mpt_ioctl_iocinfo *)malloc(sizeof *iocinfo);
    memset(iocinfo, 0, sizeof *iocinfo);
    iocinfo->hdr.maxDataSize = sizeof *iocinfo;

    iocinfo2 = (struct mpt_ioctl_iocinfo_rev1 *)malloc(sizeof *iocinfo2);
    memset(iocinfo2, 0, sizeof *iocinfo2);
    iocinfo2->hdr.maxDataSize = sizeof *iocinfo2;

    numPorts = 0;
    fileHandle = fileHandle1;
    if (fileHandle < 0)
        fileHandle = fileHandle2;
    if (fileHandle < 0)
        fileHandle = fileHandle3;
probe_again:
    for (portNumber = 0; portNumber < NUM_PORTS; portNumber++)
    {
        sprintf(portName, "/proc/mpt/ioc%d", portNumber);
        portFile = fopen(portName, "r");
        if (portFile == NULL)
            sprintf(portName, "ioc%d", portNumber);
        else
            fclose(portFile);

        eventquery.hdr.iocnum = portNumber;
        eventquery2.hdr.ioc_number = portNumber;

        if ( (fileHandle == fileHandle2) || (fileHandle == fileHandle3) )
            status = ioctl(fileHandle, MPT2EVENTQUERY, &eventquery2);
        else
            status = ioctl(fileHandle, MPTEVENTQUERY, &eventquery);

        if (status == 0)
        {
            port = (MPT_PORT *)malloc(sizeof *port);

            memset(port, 0, sizeof *port);

            /* since the global 'mpi2' is based on mptVersion, seed it with an MPI2 base value
             * so it can be used until we get the real value from IOCFacts
             */
            if ( (fileHandle == fileHandle2) || (fileHandle == fileHandle3) )
                port->mptVersion    = MPI2_VERSION_02_00;

            port->portNumber    = portNumber;
            port->hostNumber    = -1;
            port->fileHandle    = fileHandle;
            strcpy(port->portName, portName);

            for (i = 0; i < 32; i++)
            {
                if (mpi2)
                {
                    sprintf(pathName, "/sys/class/scsi_host/host%d/proc_name", i);
                    iocFile = fopen(pathName, "r");
                    if (!iocFile)
                        continue;
                    p = fgets(iocEntry, sizeof iocEntry, iocFile);
                    fclose(iocFile);
                    if (!p)
                        continue;
                    if (strncmp(p, "mpt2sas", 7))
                        continue;
                    sprintf(pathName, "/sys/class/scsi_host/host%d/unique_id", i);
                    iocFile = fopen(pathName, "r");
                    if (!iocFile)
                        continue;
                    p = fgets(iocEntry, sizeof iocEntry, iocFile);
                    fclose(iocFile);
                    if (!p)
                        continue;
                }
                else
                {
                    sprintf(pathName, "/proc/scsi/mptscsih/%d", i);
                    iocFile = fopen(pathName, "r");
                    if (!iocFile)
                    {
                        sprintf(pathName, "/proc/scsi/mptfc/%d", i);
                        iocFile = fopen(pathName, "r");
                    }
                    if (!iocFile)
                    {
                        sprintf(pathName, "/proc/scsi/mptsas/%d", i);
                        iocFile = fopen(pathName, "r");
                    }
                    if (!iocFile)
                    {
                        sprintf(pathName, "/proc/scsi/mptspi/%d", i);
                        iocFile = fopen(pathName, "r");
                    }
                    if (!iocFile)
                        continue;
                    p = fgets(iocEntry, sizeof iocEntry, iocFile);
                    fclose(iocFile);
                    if (!p)
                        continue;
                    p = strstr(iocEntry, "ioc");
                    if (!p)
                        continue;
                    p += 3;
                    q = strstr(p, ":");
                    if (!q)
                        continue;
                    q[0] = '\0';
                }
                if (portNumber == atoi(p))
                {
                    port->hostNumber = i;
                    break;
                }
            }

            iocinfo->hdr.iocnum = portNumber;

            if (mpi2)
                status = ioctl(fileHandle, MPT2IOCINFO, iocinfo);
            else
                status = ioctl(fileHandle, MPTIOCINFO, iocinfo);

            if (status == 0)
            {
                domain = iocinfo->pciInfo.segmentID;
                bus = iocinfo->pciInfo.u.bits.busNumber;
                device = iocinfo->pciInfo.u.bits.deviceNumber;
                function = iocinfo->pciInfo.u.bits.functionNumber;
            }
            else
            {
                iocinfo2->hdr.iocnum = portNumber;

                status = ioctl(fileHandle, MPTIOCINFO2, iocinfo2);

                if (status == 0)
                {
                    domain = 0;
                    bus = iocinfo->pciInfo.u.bits.busNumber;
                    device = iocinfo->pciInfo.u.bits.deviceNumber;
                    function = iocinfo->pciInfo.u.bits.functionNumber;
                }
            }

            if (status == 0)
            {
                sprintf(portName, "/proc/bus/pci/%04x:%02x/%02x.%d",
                        domain, bus, device, function);

                pciHandle = open(portName, O_RDWR);

                if (pciHandle < 0)
                {
                    sprintf(portName, "/proc/bus/pci/%02x/%02x.%d",
                            bus, device, function);

                    pciHandle = open(portName, O_RDWR);
                }

                if (pciHandle >= 0)
                {
                    if (read(pciHandle, config, sizeof config) == sizeof config)
                    {
                        deviceIdRaw = get16x(*(U16 *)&config[2]);

                        /* the following three want to be set to the device ID that doesnt include ZC*/
                        if ( (deviceIdRaw == MPI_MANUFACTPAGE_DEVID_53C1030ZC) ||
                             (deviceIdRaw == MPI_MANUFACTPAGE_DEVID_1030ZC_53C1035) ||
                             (deviceIdRaw == MPI_MANUFACTPAGE_DEVID_53C1035ZC))
                        {
                            deviceId = deviceIdRaw & ~1;
                        }
                        else 
                        {
                            deviceId = deviceIdRaw;
                        }

                        port->deviceId = deviceId;
                        port->deviceIdRaw = deviceIdRaw;

#if REGISTER_ACCESS
                        sprintf(portName, "/sys/bus/pci/devices/%04x:%02x:%02x.%d/resource",
                                domain, bus, device, function);

                        portFile = fopen(portName, "r");

                        if (portFile == NULL)
                        {
                            sprintf(portName, "/sys/bus/pci/devices/%02x:%02x.%d/resource",
                                    bus, device, function);

                            portFile = fopen(portName, "r");
                        }

                        if (portFile)
                        {
                            bar0 = 0;
                            bar1 = 0;
                            bar2 = 0;
                            bar0size = 0;
                            bar1size = 0;
                            bar2size = 0;

                            if (fgets(resource, sizeof resource, portFile))
                            {
                                if (sscanf(resource, "%llx %llx", &t, &ts) == 2)
                                {
                                    if (deviceId == MPI_MANUFACTPAGE_DEVID_SAS1078)
                                    {
                                        bar1 = t;
                                        bar1size = ts - t + 1;
                                    }
                                    else
                                    {
                                        bar0 = t;
                                        bar0size = ts - t + 1;
                                    }
                                }
                                if (fgets(resource, sizeof resource, portFile))
                                {
                                    if (sscanf(resource, "%llx %llx", &t, &ts) == 2)
                                    {
                                        if (deviceId == MPI_MANUFACTPAGE_DEVID_SAS1078)
                                        {
                                            bar0 = t;
                                            bar0size = ts - t + 1;
                                        }
                                        else
                                        {
                                            bar1 = t;
                                            bar1size = ts - t + 1;
                                        }
                                    }
                                    if (fgets(resource, sizeof resource, portFile))
                                    {
                                        if (fgets(resource, sizeof resource, portFile))
                                        {
                                            if (sscanf(resource, "%llx %llx", &t, &ts) == 2)
                                            {
                                                bar2 = t;
                                                bar2size = ts - t + 1;
                                            }
                                        }
                                    }
                                }
                            }

                            fclose(portFile);
                        }
                        else
                        {
                            if (deviceId == MPI_MANUFACTPAGE_DEVID_SAS1078)
                            {
                                bar1 = get64x(         config[0x10]) & ~0xF;
                                bar0 = get32x(*(U32 *)&config[0x18]) & ~0xF;
                            }
                            else
                            {
                                bar0 = get32x(*(U32 *)&config[0x10]) & ~0xF;
                                bar1 = get64x(         config[0x14]) & ~0xF;
                            }
                            bar2     = get64x(         config[0x1c]) & ~0xF;
                            bar0size = 256;
                            bar1size = 256;
                            bar2size = 65536;
                        }

                        port->ioPhys = bar0;
                        port->memPhys = bar1;
                        port->diagPhys = bar2;

                        ioctl(pciHandle, PCIIOC_MMAP_IS_MEM);

                        if (deviceId == MPI_MANUFACTPAGE_DEVID_SAS1078)
                            sprintf(portName1, "%s0", portName);
                        else
                            sprintf(portName1, "%s1", portName);
                        sprintf(portName2, "%s3", portName);

                        pciHandle1 = open(portName1, O_RDWR);
                        if (pciHandle1)
                        {
                            errno = 0;
                            port->memVirt = mmap(NULL, bar1size, PROT_READ | PROT_WRITE,
                                                 MAP_SHARED, pciHandle1, 0);
                            if (errno) {
                                port->memVirt = NULL;
                            }
                            close(pciHandle1);
                        }

                        if (!port->memVirt && bar1 && bar1size)
                        {
                            errno = 0;
                            port->memVirt = mmap(NULL, bar1size, PROT_READ | PROT_WRITE,
                                                 MAP_SHARED, pciHandle, bar1);
                            if (errno)
                                port->memVirt = NULL;
                        }

                        if (!port->memVirt)
                            port->memPhys = 0;

                        pciHandle2 = open(portName2, O_RDWR);
                        if (pciHandle2)
                        {
                            errno = 0;
                            port->diagVirt = mmap(NULL, bar2size, PROT_READ | PROT_WRITE,
                                                 MAP_SHARED, pciHandle2, 0);
                            if (errno) {
                                port->diagVirt = NULL;
                            }
                            close(pciHandle2);
                        }

                        if (!port->diagVirt && bar2 && bar2size)
                        {
                            errno = 0;
                            port->diagVirt = mmap(NULL, bar2size, PROT_READ | PROT_WRITE,
                                                  MAP_SHARED, pciHandle, bar2);
                            if (errno)
                                port->diagVirt = NULL;
                        }

                        if (!port->diagVirt)
                            port->diagPhys = 0;

#if i386
                        if (deviceId == MPI_MANUFACTPAGE_DEVID_53C1030)
                        {
                            iopl(3);

                            if (!(config[0x04] & 1))
                            {
                                config[0x04] |= 1;
                                lseek(pciHandle, 0x04, SEEK_SET);
                                write(pciHandle, config + 0x04, 1);
                            }
                        }
#endif
#endif
                    }
                    close(pciHandle);
                }
            }

            if (getPortInfo(port) == 1)
            {
                mptports[numPorts++] = port;
            }
            else
            {
                free(port);
            }
        }
    }
    if (fileHandle == fileHandle1)
    {
        fileHandle = fileHandle2;
        if (fileHandle >= 0)
            goto probe_again;
    }
    if (fileHandle == fileHandle2)
    {
        fileHandle = fileHandle3;
        if (fileHandle >= 0)
            goto probe_again;
    }

    free(iocinfo);
    free(iocinfo2);

    

    if (numPorts == 0)
    {
        if (fileHandle1 >= 0)
            close(fileHandle1);
        if (fileHandle2 >= 0)
            close(fileHandle2);
        if (fileHandle3 >= 0)
            close(fileHandle3);
    } 
    else
    {
        for(i = 0; i < numPorts; i++)
        {
            if(mptports[i]->fileHandle != fileHandle1 && fileHandle1 > 0)
            {
                close(fileHandle1);
            }
            if(mptports[i]->fileHandle != fileHandle2 && fileHandle2 > 0)
            {
                close(fileHandle2);
            }
            if(mptports[i]->fileHandle != fileHandle3 && fileHandle3 > 0)
            {
                close(fileHandle3);
            }
        }
    }

    return numPorts;
}


int
closePorts(int numPorts,MPT_PORT            **mptports)
{
    MPT_PORT    *port;
    int          i;

    for (i = 0; i < numPorts; i++)
    {
        port = mptports[i];

        if (port)
            closePort(port);
    }
 
    return 1;
}


int
closePort(MPT_PORT *port)
{
    close(port->fileHandle);
    free(port->chipName);
    free(port->chipNameRev);
    free(port);

    return 1;
}


char *deviceType[32] =
{
    "Disk",
    "Tape",
    "Printer",
    "Processor",
    "WriteOnce",
    "CDROM",
    "Scanner",
    "Optical",
    "Jukebox",
    "Comm",
    "0Ah",
    "0Bh",
    "RAIDArray",
    "EnclServ",
    "0Eh",
    "0Fh",
    "10h",
    "11h",
    "12h",
    "13h",
    "14h",
    "15h",
    "16h",
    "17h",
    "18h",
    "19h",
    "1Ah",
    "1Bh",
    "1Ch",
    "1Dh",
    "1Eh",
    ""
};


int
getPortInfo(MPT_PORT *port)
{
    IOCFactsReply_t      IOCFacts;
    PortFactsReply_t     PortFacts;
#if !DOS && !EFI
    IOCPage0_t           IOCPage0;
#endif
    SasIOUnitPage0_t     SASIOUnitPage0;

    if (checkOperational(port, 0) != 1)
        return 1;

    port->lastEvent = -1;

    port->payOff = 0;

    if (getIocFacts(port, &IOCFacts) != 1)
        return 0;

//  dumpMemoryWide(&IOCFacts, sizeof IOCFacts, "IOCFactsReply");

    port->mptVersion = get16(IOCFacts.MsgVersion);

    if (mpi2)
        return getPortInfo2(port);

    port->iocNumber = IOCFacts.IOCNumber;
    port->whoInit = IOCFacts.WhoInit;
    port->productId = get16(IOCFacts.ProductID);
    port->capabilities = get32(IOCFacts.IOCCapabilities);
    port->flags = IOCFacts.Flags;
    port->fwImageSize = get32(IOCFacts.FWImageSize);
    port->payOff = get16(IOCFacts.CurReplyFrameSize);
    port->maxBuses = IOCFacts.MaxBuses;
    if (port->maxBuses == 0)
        port->maxBuses = 1;
    port->minTargets = 0;
    port->maxTargets = IOCFacts.MaxDevices;
    if (port->maxTargets == 0)
        port->maxTargets = 255;  /* Linux limit! */
    port->maxLuns = maxLuns;

    if (port->mptVersion < MPI_VERSION_01_02)
        port->fwVersion = get16(IOCFacts.Reserved_0101_FWVersion);
    else
        port->fwVersion = get32(IOCFacts.FWVersion.Word);

    if (port->mptVersion < MPI_VERSION_01_02 &&
        port->productId == MPI_MANUFACTPAGE_DEVICEID_FC909)
        port->productId = MPI_FW_HEADER_PID_FAMILY_909_FC |
                          MPI_FW_HEADER_PID_TYPE_FC;

    port->pidType = port->productId & MPI_FW_HEADER_PID_TYPE_MASK;

    if (getPortFacts(port, &PortFacts) != 1)
        return 0;

//  dumpMemoryWide(&PortFacts, sizeof PortFacts, "PortFactsReply");

    port->portType = PortFacts.PortType;
    port->maxPersistentIds = get16(PortFacts.MaxPersistentIDs);
    port->hostScsiId = get16(PortFacts.PortSCSIID);
    port->protocolFlags = get16(PortFacts.ProtocolFlags);

    if (port->pidType == MPI_FW_HEADER_PID_TYPE_SAS)
    {
        if (port->maxTargets > port->hostScsiId + 1)
            port->maxTargets = port->hostScsiId + 1;
    }
    else
    {
        if (port->maxTargets > get16(PortFacts.MaxDevices))
            port->maxTargets = get16(PortFacts.MaxDevices);
    }

#if !DOS && !EFI
    if (getConfigPage(port, MPI_CONFIG_PAGETYPE_IOC, 0, 0, &IOCPage0, sizeof IOCPage0) != 1)
        return 0;

    if (get16(IOCPage0.VendorID) != MPI_MANUFACTPAGE_VENDORID_LSILOGIC)
        return 0;

    port->deviceIdRaw = get16(IOCPage0.DeviceID);

    /* the following three want to be set to the device ID that doesnt include ZC*/
    if ( (port->deviceIdRaw == MPI_MANUFACTPAGE_DEVID_53C1030ZC) ||
         (port->deviceIdRaw == MPI_MANUFACTPAGE_DEVID_1030ZC_53C1035) ||
         (port->deviceIdRaw == MPI_MANUFACTPAGE_DEVID_53C1035ZC))
    {
        port->deviceId = port->deviceIdRaw & ~1;
    }
    else 
    {
        port->deviceId = port->deviceIdRaw;
    }

    port->revisionId = IOCPage0.RevisionID;

    getChipName(port);
#endif

    if (port->pidType == MPI_FW_HEADER_PID_TYPE_SAS)
    {
        if (getConfigPage(port, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT, 0, 0,
                          &SASIOUnitPage0, sizeof SASIOUnitPage0) == 1)
        {
            port->numPhys = SASIOUnitPage0.NumPhys;
        }
        else
        {
            switch (port->deviceId)
            {
            case MPI_MANUFACTPAGE_DEVID_SAS1064:
            case MPI_MANUFACTPAGE_DEVID_SAS1064E:
                port->numPhys = 4;
                break;
            case MPI_MANUFACTPAGE_DEVID_SAS1066:
            case MPI_MANUFACTPAGE_DEVID_SAS1066E:
                port->numPhys = 6;
                break;
            case MPI_MANUFACTPAGE_DEVID_SAS1068:
            case MPI_MANUFACTPAGE_DEVID_SAS1068E:
            case MPI_MANUFACTPAGE_DEVID_SAS1078:
                port->numPhys = 8;
                break;
            }
        }
    }

    return 1;
}


int
getChipName(MPT_PORT *port)
{
    char                *string;
    char                *chipName;
    char                *chipNameRev;
    int                  family;
    int                  revision;
    char                *type;
    int                  i;
    U32                  seqcodeversion = 0;

    family = port->productId & MPI_FW_HEADER_PID_FAMILY_MASK;
    revision = port->revisionId;
    switch (port->deviceId)
    {
    case MPI_MANUFACTPAGE_DEVICEID_FC909:
        string = "FC909 B1";
        type = "PCI";
        break;
    case MPI_MANUFACTPAGE_DEVICEID_FC919:
        string = "FC919 B0";
        type = "PCI";
        break;
    case MPI_MANUFACTPAGE_DEVICEID_FC929:
        string = "FC929 B0";
        type = "PCI";
        break;
    case MPI_MANUFACTPAGE_DEVICEID_FC919X:
        if (revision < 0x80)
            string = "FC919X A0";
        else
            string = "FC919XL A1";
        type = "PCI-X";
        break;
    case MPI_MANUFACTPAGE_DEVICEID_FC929X:
        if (revision < 0x80)
            string = "FC929X A0";
        else
            string = "FC929XL A1";
        type = "PCI-X";
        break;
    case MPI_MANUFACTPAGE_DEVICEID_FC939X:
        string = "FC939X A1";
        type = "PCI-X";
        break;
    case MPI_MANUFACTPAGE_DEVICEID_FC949X:
        string = "FC949X A1";
        type = "PCI-X";
        break;
    case MPI_MANUFACTPAGE_DEVICEID_FC949E:
        switch (revision)
        {
        case 0x00:
            string = "FC949E A0";
            break;
        case 0x01:
            string = "FC949E A1";
            break;
        case 0x02:
            string = "FC949E A2";
            break;
        default:
            string = "FC949E xx";
            break;
        }
        type = "PCI-E";
        break;
    case MPI_MANUFACTPAGE_DEVID_53C1030:
        switch (revision)
        {
        case 0x00:
            string = "53C1030 A0";
            break;
        case 0x01:
            string = "53C1030 B0";
            break;
        case 0x03:
            string = "53C1030 B1";
            break;
        case 0x07:
            string = "53C1030 B2";
            break;
        case 0x08:
            string = "53C1030 C0";
            break;
        case 0x80:
            string = "53C1030T A0";
            break;
        case 0x83:
            string = "53C1030T A2";
            break;
        case 0x87:
            string = "53C1030T A3";
            break;
        case 0xc1:
            string = "53C1020A A1";
            break;
        default:
            string = "53C1030 xx";
            break;
        }
        type = "PCI-X";
        break;
    case MPI_MANUFACTPAGE_DEVID_1030_53C1035:
        switch (revision)
        {
        case 0x03:
            string = "53C1035 A2";
            break;
        case 0x04:
            string = "53C1035 B0";
            break;
        default:
            string = "53C1035 xx";
            break;
        }
        type = "PCI-X";
        break;
    case MPI_MANUFACTPAGE_DEVID_SAS1064:
        switch (revision)
        {
        case 0x00:
            string = "SAS1064 A1";  seqcodeversion = 0x1064a1;
            break;
        case 0x01:
            string = "SAS1064 A2";  seqcodeversion = 0x1064a2;
            break;
        case 0x02:
            string = "SAS1064 A3";  seqcodeversion = 0x1064a3;
            break;
        case 0x03:
            string = "SAS1064 A4";  seqcodeversion = 0x1064a4;
            break;
        default:
            string = "SAS1064 xx";  seqcodeversion = 0x1064ff;
            break;
        }
        type = "PCI-X";
        break;
    case MPI_MANUFACTPAGE_DEVID_SAS1064E:
        switch (revision)
        {
        case 0x00:
            string = "SAS1064E A0";  seqcodeversion = 0x106ea0;
            break;
        case 0x01:
            string = "SAS1064E B0";  seqcodeversion = 0x106eb0;
            break;
        case 0x02:
            string = "SAS1064E B1";  seqcodeversion = 0x106eb1;
            break;
        case 0x04:
            string = "SAS1064E B2";  seqcodeversion = 0x106eb2;
            break;
        case 0x08:
            string = "SAS1064E B3";  seqcodeversion = 0x106eb3;
            break;
        case 0x10:
            string = "SAS1064E C0";  seqcodeversion = 0x106ec0;
            break;
        default:
            string = "SAS1064E xx";  seqcodeversion = 0x106eff;
            break;
        }
        type = "PCI-E";
        break;
    case MPI_MANUFACTPAGE_DEVID_SAS1066:
        string = "SAS1066 xx";
        type = "PCI-X";
        break;
    case MPI_MANUFACTPAGE_DEVID_SAS1066E:
        string = "SAS1066E xx";
        type = "PCI-E";
        break;
    case MPI_MANUFACTPAGE_DEVID_SAS1068:
        switch (revision)
        {
        case 0x00:
            string = "SAS1068 A0";  seqcodeversion = 0x1068a0;
            break;
        case 0x01:
            string = "SAS1068 B0";  seqcodeversion = 0x1068b0;
            break;
        case 0x02:
            string = "SAS1068 B1";  seqcodeversion = 0x1068b1;
            break;
        default:
            string = "SAS1068 xx";  seqcodeversion = 0x1068ff;
            break;
        }
        type = "PCI-X";
        break;
    case MPI_MANUFACTPAGE_DEVID_SAS1068E:
        switch (revision)
        {
        case 0x00:
            string = "SAS1068E A0";  seqcodeversion = 0x106ea0;
            break;
        case 0x01:
            string = "SAS1068E B0";  seqcodeversion = 0x106eb0;
            break;
        case 0x02:
            string = "SAS1068E B1";  seqcodeversion = 0x106eb1;
            break;
        case 0x04:
            string = "SAS1068E B2";  seqcodeversion = 0x106eb2;
            break;
        case 0x08:
            string = "SAS1068E B3";  seqcodeversion = 0x106eb3;
            break;
        case 0x10:
            string = "SAS1068E C0";  seqcodeversion = 0x106ec0;
            break;
        default:
            string = "SAS1068E xx";  seqcodeversion = 0x106eff;
            break;
        }
        type = "PCI-E";
        break;
    case MPI_MANUFACTPAGE_DEVID_SAS1078:
        switch (revision)
        {
        case 0x00:
            string = "SAS1078 A0";  seqcodeversion = 0x1078a0;
            break;
        case 0x01:
            string = "SAS1078 B0";  seqcodeversion = 0x1078b0;
            break;
        case 0x02:
            string = "SAS1078 C0";  seqcodeversion = 0x1078c0;
            break;
        case 0x03:
            string = "SAS1078 C1";  seqcodeversion = 0x1078c1;
            break;
        case 0x04:
            string = "SAS1078 C2";  seqcodeversion = 0x1078c2;
            break;
        default:
            string = "SAS1078 xx";  seqcodeversion = 0x1078ff;
            break;
        }
        type = "PCI-E";
        break;
    case MPI2_MFGPAGE_DEVID_SAS2004:
        switch (revision)
        {
        case 0x00:
            string = "SAS2004 A0";
            break;
        case 0x01:
            string = "SAS2004 B0";
            break;
        case 0x02:
            string = "SAS2004 B1";
            break;
        case 0x03:
            string = "SAS2004 B2";
            break;
        default:
            string = "SAS2004 xx";
            break;
        }
        type = "PCI-E";
        break;
    case MPI2_MFGPAGE_DEVID_SAS2008:
        switch (revision)
        {
        case 0x00:
            string = "SAS2008 A0";
            break;
        case 0x01:
            string = "SAS2008 B0";
            break;
        case 0x02:
            string = "SAS2008 B1";
            break;
        case 0x03:
            string = "SAS2008 B2";
            break;
        default:
            string = "SAS2008 xx";
            break;
        }
        type = "PCI-E";
        break;
    case MPI2_MFGPAGE_DEVID_SAS2108_1:
    case MPI2_MFGPAGE_DEVID_SAS2108_2:
    case MPI2_MFGPAGE_DEVID_SAS2108_3:
        switch (revision)
        {
        case 0x00:
            string = "SAS2108 A0";
            break;
        case 0xFF:
            string = "SAS2 FPGA A0";
            break;
        /* the PCI Revision ID was not bumped between B0 and B1.  Since B0 is not supported
         * and had limited use (pre-production only), don't worry about identifying it.
         * NOTE: PCI config space will always report a 1 for B0 or B1.  The firmware
         * (IOCPage0->RevisionID) is supposed to report a 1 for B0 and a 2 for B1 but it does not
         * always do so.  Therefore we consider either a 1 or 2 to be a B1 chip.
         */
        case 0x01:
        case 0x02:
            string = "SAS2108 B1";
            break;
        case 0x03:
            string = "SAS2108 B2";
            break;
        case 0x04:
            string = "SAS2108 B3";
            break;
        case 0x05:
            string = "SAS2108 B4";
            break;
        default:
            string = "SAS2108 xx";
            break;
        }
        type = "PCI-E";
        break;
    case MPI2_MFGPAGE_DEVID_SAS2116_1:
    case MPI2_MFGPAGE_DEVID_SAS2116_2:
        switch (revision)
        {
        case 0x00:
            string = "SAS2116 A0";
            break;
        case 0x01:
            string = "SAS2116 B0";
            break;
        case 0x02:
            string = "SAS2116 B1";
            break;
        default:
            string = "SAS2116 xx";
            break;
        }
        type = "PCI-E";
        break;
    case MPI2_MFGPAGE_DEVID_SAS2208_1:
    case MPI2_MFGPAGE_DEVID_SAS2208_2:
    case MPI2_MFGPAGE_DEVID_SAS2208_3:
    case MPI2_MFGPAGE_DEVID_SAS2208_4:
    case MPI2_MFGPAGE_DEVID_SAS2208_5:
    case MPI2_MFGPAGE_DEVID_SAS2208_6:
        switch (revision)
        {
        case 0x00:
            string = "SAS2208 A0";
            break;
        case 0x01:
            string = "SAS2208 B0";
            break;
        case 0x02:
            string = "SAS2208 C0";
            break;
        case 0x03:
            string = "SAS2208 C1";
            break;
        case 0x04:
            string = "SAS2208 D0";
            break;
        case 0x05:
            string = "SAS2208 D1";
            break;
        default:
            string = "SAS2208 xx";
            break;
        }
        type = "PCI-E";
        break;
    case MPI2_MFGPAGE_DEVID_SAS2308_1:
    case MPI2_MFGPAGE_DEVID_SAS2308_2:
    case MPI2_MFGPAGE_DEVID_SAS2308_3:
        switch (revision)
        {
        case 0x00:
            string = "SAS2308 A0";
            break;
        case 0x01:
            string = "SAS2308 B0";
            break;
        case 0x02:
            string = "SAS2308 C0";
            break;
        case 0x03:
            string = "SAS2308 C1";
            break;
        case 0x04:
            string = "SAS2308 D0";
            break;
        case 0x05:
            string = "SAS2308 D1";
            break;
        default:
            string = "SAS2308 xx";
            break;
        }
        type = "PCI-E";
        break;
    case MPI25_MFGPAGE_DEVID_SAS3004:
        switch (revision)
        {
        case 0x00:
            string = "SA3004 A0";
            break;
        case 0x01:
            string = "SAS3004 B0";
            break;
        case 0x02:
            string = "SAS3004 C0";
            break;
        default:
            string = "SAS3004 xx";
            break;
        }
        type = "PCI-E";
        break;
    case MPI25_MFGPAGE_DEVID_SAS3008:
        switch (revision)
        {
        case 0x00:
            string = "SA3008 A0";
            break;
        case 0x01:
            string = "SAS3008 B0";
            break;
        case 0x02:
            string = "SAS3008 C0";
            break;
        default:
            string = "SAS3008 xx";
            break;
        }
        type = "PCI-E";
        break;
    case MPI25_MFGPAGE_DEVID_SAS3108_1:
    case MPI25_MFGPAGE_DEVID_SAS3108_2:
    case MPI25_MFGPAGE_DEVID_SAS3108_5:
    case MPI25_MFGPAGE_DEVID_SAS3108_6:
        switch (revision)
        {
        case 0x00:
            string = "SAS3108 A0";
            break;
        case 0x01:
            string = "SAS3108 B0";
            break;
        case 0x02:
            string = "SAS3108 C0";
            break;
        default:
            string = "SAS3108 xx";
            break;
        }
        type = "PCI-E";
        break;
#ifdef SAS3108_FPGA_WORKAROUND
    case 0x100:
    case 0x092:
        string = "SAS3108 FPGA";
        type = "PCI-E";
        break;
#endif
    case MPI2_MFGPAGE_DEVID_SSS6200:
        switch (revision)
        {
        case 0x00:
            string = "SSS6200 A0";
            break;
        case 0x01:
            string = "SSS6200 B0";
            break;
        case 0x02:
            string = "SSS6200 C0";
            break;
        default:
            string = "SSS6200 xx";
            break;
        }
        type = "PCI-E";
        break;
    default:
        string = "xxxx xx";
        type = NULL;
        break;
    }

    port->seqCodeVersion = seqcodeversion;

    chipNameRev = malloc(strlen(string) + 1);
    strcpy(chipNameRev, string);

    i = (int)strlen(chipNameRev) - 2;

    if (strncmp(chipNameRev + 0, "xxxx", 4) == 0)
        sprintf(chipNameRev + 0, "%04x %02x", port->deviceId, port->revisionId);
    else if (strncmp(chipNameRev + i, "xx", 2) == 0)
        sprintf(chipNameRev + i, "%02x", port->revisionId);

    port->chipNameRev = chipNameRev;

    chipName = malloc(strlen(chipNameRev) + 1);
    strcpy(chipName, chipNameRev);

    i = (int)strlen(chipNameRev) - 3;
    chipName[i] = '\0';

    port->chipName = chipName;

    port->pciType = type;

    return 1;
}


int
getPortInfo2(MPT_PORT *port)
{
    Mpi2IOCFactsReply_t      IOCFacts;
    Mpi2PortFactsReply_t     PortFacts;
#if !DOS && !EFI
    Mpi2IOCPage0_t           IOCPage0;
#endif
    Mpi2SasIOUnitPage0_t     SASIOUnitPage0;

    if (getIocFacts2(port, &IOCFacts) != 1)
        return 0;

//  dumpMemoryWide(&IOCFacts, sizeof IOCFacts, "IOCFactsReply");

    port->iocNumber = IOCFacts.IOCNumber;
    port->whoInit = IOCFacts.WhoInit;
    port->productId = get16(IOCFacts.ProductID);
    port->capabilities = get32(IOCFacts.IOCCapabilities);

    // ReplyFrameSize moved within IOCFacts and went from
    // indicating the number of bytes to indicating the
    // number of dwords.  Maintain backward compatibility
    if (mpi25)
    {
        port->payOff = IOCFacts.ReplyFrameSize * 4;
    }
    else if (IOCFacts.OldReplyFrameSize)
    {
        port->payOff = get16(IOCFacts.OldReplyFrameSize);
    }
    else
    {
        port->payOff = IOCFacts.ReplyFrameSize * 4;
    }

    port->maxDevHandle = get16(IOCFacts.MaxDevHandle) + 1;

    setMaxBusTarget(port);

    port->maxLuns = maxLuns;
    port->protocolFlags = get16(IOCFacts.ProtocolFlags);
    port->maxPersistentIds = get16(IOCFacts.MaxPersistentEntries);

    port->fwVersion = get32(IOCFacts.FWVersion.Word);

    port->fastpathCapable = port->capabilities & MPI25_IOCFACTS_CAPABILITY_FAST_PATH_CAPABLE ? 1 : 0;

    port->pidType = port->productId & MPI2_FW_HEADER_PID_TYPE_MASK;

    if (getPortFacts2(port, &PortFacts) != 1)
        return 0;

//  dumpMemoryWide(&PortFacts, sizeof PortFacts, "PortFactsReply");

    port->portType = PortFacts.PortType;

#if !DOS && !EFI
    if (getConfigPage(port, MPI2_CONFIG_PAGETYPE_IOC, 0, 0, &IOCPage0, sizeof IOCPage0) != 1)
        return 0;

#if SAS3108_FPGA_WORKAROUND
    if (get16(IOCPage0.VendorID) != MPI2_MFGPAGE_VENDORID_LSI && get16(IOCPage0.VendorID) != SAS3108_FPGA_VENDORID)
        return 0;
#else
    if (get16(IOCPage0.VendorID) != MPI2_MFGPAGE_VENDORID_LSI)
        return 0;
#endif
    port->deviceIdRaw = get16(IOCPage0.DeviceID);
    /* the following three want to be set to the device ID that doesnt include ZC*/
    if ( (port->deviceIdRaw == MPI_MANUFACTPAGE_DEVID_53C1030ZC) ||
         (port->deviceIdRaw == MPI_MANUFACTPAGE_DEVID_1030ZC_53C1035) ||
         (port->deviceIdRaw == MPI_MANUFACTPAGE_DEVID_53C1035ZC))
    {
        port->deviceId = port->deviceIdRaw & ~1;
    }
    else 
    {
        port->deviceId = port->deviceIdRaw;
    }
    port->revisionId = IOCPage0.RevisionID;

    getChipName(port);
#endif

    if (port->pidType == MPI2_FW_HEADER_PID_TYPE_SAS)
    {
        if (getConfigPage(port, MPI2_CONFIG_EXTPAGETYPE_SAS_IO_UNIT, 0, 0,
                          &SASIOUnitPage0, sizeof SASIOUnitPage0) == 1)
        {
            port->numPhys = SASIOUnitPage0.NumPhys;
        }
        else
        {
            switch (port->deviceId)
            {
            case MPI2_MFGPAGE_DEVID_SAS2004:
            case MPI25_MFGPAGE_DEVID_SAS3004:
                port->numPhys = 4;
                break;
            case MPI2_MFGPAGE_DEVID_SAS2008:
            case MPI2_MFGPAGE_DEVID_SAS2108_1:
            case MPI2_MFGPAGE_DEVID_SAS2108_2:
            case MPI2_MFGPAGE_DEVID_SAS2108_3:
            case MPI2_MFGPAGE_DEVID_SAS2208_1:
            case MPI2_MFGPAGE_DEVID_SAS2208_2:
            case MPI2_MFGPAGE_DEVID_SAS2208_3:
            case MPI2_MFGPAGE_DEVID_SAS2208_4:
            case MPI2_MFGPAGE_DEVID_SAS2208_5:
            case MPI2_MFGPAGE_DEVID_SAS2208_6:
            case MPI2_MFGPAGE_DEVID_SAS2308_1:
            case MPI2_MFGPAGE_DEVID_SAS2308_2:
            case MPI2_MFGPAGE_DEVID_SAS2308_3:
            case MPI25_MFGPAGE_DEVID_SAS3008:
            case MPI25_MFGPAGE_DEVID_SAS3108_1:
            case MPI25_MFGPAGE_DEVID_SAS3108_2:
            case MPI25_MFGPAGE_DEVID_SAS3108_5:
            case MPI25_MFGPAGE_DEVID_SAS3108_6:
            case MPI2_MFGPAGE_DEVID_SSS6200:
                port->numPhys = 8;
                break;
            case MPI2_MFGPAGE_DEVID_SAS2116_1:
            case MPI2_MFGPAGE_DEVID_SAS2116_2:
                port->numPhys = 16;
                break;
            }
        }
    }

    return 1;
}


int
getIocFacts(MPT_PORT *port, IOCFactsReply_t *rep)
{
    IOCFacts_t   req;

    memset(&req, 0, sizeof req);
    memset(rep, 0, sizeof *rep);

    req.Function            = MPI_FUNCTION_IOC_FACTS;

    return doMptCommand(port, &req, sizeof req, rep, sizeof *rep, NULL, 0, NULL, 0, SHORT_TIME);
}


int
getIocFacts2(MPT_PORT *port, Mpi2IOCFactsReply_t *rep)
{
    Mpi2IOCFactsRequest_t    req;

    memset(&req, 0, sizeof req);
    memset(rep, 0, sizeof *rep);

    req.Function            = MPI2_FUNCTION_IOC_FACTS;

    return doMptCommand(port, &req, sizeof req, rep, sizeof *rep, NULL, 0, NULL, 0, SHORT_TIME);
}


int
getPortFacts(MPT_PORT *port, PortFactsReply_t *rep)
{
    PortFacts_t  req;

    memset(&req, 0, sizeof req);
    memset(rep, 0, sizeof *rep);

    req.Function            = MPI_FUNCTION_PORT_FACTS;

    return doMptCommand(port, &req, sizeof req, rep, sizeof *rep, NULL, 0, NULL, 0, SHORT_TIME);
}


int
getPortFacts2(MPT_PORT *port, Mpi2PortFactsReply_t *rep)
{
    Mpi2PortFactsRequest_t   req;

    memset(&req, 0, sizeof req);
    memset(rep, 0, sizeof *rep);

    req.Function            = MPI2_FUNCTION_PORT_FACTS;

    return doMptCommand(port, &req, sizeof req, rep, sizeof *rep, NULL, 0, NULL, 0, SHORT_TIME);
}


int
doConfigPageRequest(MPT_PORT *port, void *req, int reqSize, void *rep, int repSize,
                    void *payIn, int payInSize, void *payOut, int payOutSize, int timeOut)
{
    int    i;

    for (i = 0; i < 120; i++)
    {
        if (doMptCommand(port, req, reqSize, rep, repSize, payIn, payInSize, payOut, payOutSize, timeOut) != 1)
            return 0;

        if (get16(((ConfigReply_t *)rep)->IOCStatus) != MPI_IOCSTATUS_BUSY)
        {
            if (i > 0)
                printf("SUCCESS\n");
            return 1;
        }

        if (i == 0)
            printf("Firmware returned busy status, retrying.");
        else
            printf(".");

        fflush(stdout);

        sleep(1);
    }

    printf("\nRetries exhausted.  Giving up request!\n");
    return 1;
}


int
getConfigPageHeader(MPT_PORT *port, int type, int number, int address, ConfigReply_t *repOut)
{
    Config_t         req;
    ConfigReply_t    rep;
    int              ioc_status;

    memset(&req, 0, sizeof req);
    memset(&rep, 0, sizeof rep);

    req.Function            = MPI_FUNCTION_CONFIG;
    req.AliasIndex          = virtInit;
    req.Action              = MPI_CONFIG_ACTION_PAGE_HEADER;
    if (type > MPI_CONFIG_PAGETYPE_EXTENDED)
    {
        req.Header.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
        req.ExtPageType     = type;
    }
    else
    {
        req.Header.PageType = type;
    }
    req.Header.PageNumber   = number;
    req.PageAddress         = set32(address);

    if (doConfigPageRequest(port, &req, sizeof req - sizeof req.PageBufferSGE, &rep, sizeof rep,
                            NULL, 0, NULL, 0, SHORT_TIME) != 1)
        return 0;

    ioc_status = get16(rep.IOCStatus) & MPI_IOCSTATUS_MASK;

    if (ioc_status != MPI_IOCSTATUS_SUCCESS)
        return 0;

    if (repOut != NULL)
        memcpy(repOut, &rep, sizeof rep);

    return 1;
}


int
getConfigPageLength(MPT_PORT *port, int type, int number, int address, int *length)
{
    Config_t         req;
    ConfigReply_t    rep;
    int              ioc_status;

    memset(&req, 0, sizeof req);
    memset(&rep, 0, sizeof rep);

    req.Function            = MPI_FUNCTION_CONFIG;
    req.AliasIndex          = virtInit;
    req.Action              = MPI_CONFIG_ACTION_PAGE_HEADER;
    if (type > MPI_CONFIG_PAGETYPE_EXTENDED)
    {
        req.Header.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
        req.ExtPageType     = type;
    }
    else
    {
        req.Header.PageType = type;
    }
    req.Header.PageNumber   = number;
    req.PageAddress         = set32(address);

    if (doConfigPageRequest(port, &req, sizeof req - sizeof req.PageBufferSGE, &rep, sizeof rep,
                            NULL, 0, NULL, 0, SHORT_TIME) != 1)
        return 0;

    ioc_status = get16(rep.IOCStatus) & MPI_IOCSTATUS_MASK;

    if (ioc_status != MPI_IOCSTATUS_SUCCESS)
        return 0;

    if (type > MPI_CONFIG_PAGETYPE_EXTENDED)
        *length = get16(rep.ExtPageLength) * 4;
    else
        *length = rep.Header.PageLength * 4;

    return 1;
}


int
getConfigPageAction(MPT_PORT *port, int action, int type, int number, int address, void *page, int pageSize)
{
    Config_t             req;
    ConfigReply_t        rep, rep1;
    ConfigPageHeader_t   header;
    int                  length;
    int                  t;
    int                  ioc_status;

    memset(&req, 0, sizeof req);
    memset(&rep, 0, sizeof rep);

    if (getConfigPageHeader(port, type, number, address, &rep) != 1)
        return 0;

    memcpy(&rep1, &rep, sizeof rep);

    header = rep.Header;
    length = get16(rep.ExtPageLength);

    req.Function            = MPI_FUNCTION_CONFIG;
    req.AliasIndex          = virtInit;
    if (action != -1)
        req.Action          = action;
    else if ((rep.Header.PageType & MPI_CONFIG_PAGEATTR_MASK) == MPI_CONFIG_PAGEATTR_PERSISTENT ||
             (rep.Header.PageType & MPI_CONFIG_PAGEATTR_MASK) == MPI_CONFIG_PAGEATTR_RO_PERSISTENT)
        req.Action          = MPI_CONFIG_ACTION_PAGE_READ_NVRAM;
    else
        req.Action          = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
    if (req.Action == MPI_CONFIG_ACTION_PAGE_READ_NVRAM && port->mptVersion < MPI_VERSION_01_01)
        req.Action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
    req.ExtPageType         = rep.ExtPageType;
    req.ExtPageLength       = rep.ExtPageLength;
    req.Header              = rep.Header;
    req.PageAddress         = set32(address);

    if (doConfigPageRequest(port, &req, sizeof req - sizeof req.PageBufferSGE, &rep, sizeof rep,
                            page, pageSize, NULL, 0, SHORT_TIME) != 1)
        return 0;

    port->ioc_status = ioc_status = get16(rep.IOCStatus) & MPI_IOCSTATUS_MASK;

    if (ioc_status == MPI_IOCSTATUS_CONFIG_INVALID_PAGE ||
        ioc_status == MPI_IOCSTATUS_CONFIG_INVALID_DATA)
    {
        if (action == MPI_CONFIG_ACTION_PAGE_READ_NVRAM)
        {
            printf("\nNon-volatile storage for this page is invalid!\n");
#if 0
            printf("The current values for this page will be used instead\n");
            req.Action = MPI_CONFIG_ACTION_PAGE_READ_NVRAM;
#else
            return 0;
#endif
        }

        if (req.Action == MPI_CONFIG_ACTION_PAGE_READ_NVRAM)
        {
            req.Action      = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

            if (doConfigPageRequest(port, &req, sizeof req - sizeof req.PageBufferSGE, &rep, sizeof rep,
                                    page, pageSize, NULL, 0, SHORT_TIME) != 1)
                return 0;

            port->ioc_status = ioc_status = get16(rep.IOCStatus) & MPI_IOCSTATUS_MASK;
        }
    }

    if (ioc_status != MPI_IOCSTATUS_SUCCESS)
        return 0;

    if (type > MPI_CONFIG_PAGETYPE_EXTENDED)
    {
        if (get16(rep.ExtPageLength) == 0)
            return 0;
        if (memcmp(&header, &rep.Header, sizeof header) != 0)
            printf("Warning, header in HEADER reply does not match header in READ reply\n  (%08x vs. %08x)\n",
                   get32x(*(U32 *)&header), get32x(*(U32 *)&rep.Header));
        if (length != get16(rep.ExtPageLength))
            printf("Warning, length in HEADER reply does not match length in READ reply\n  (%d vs. %d)\n",
                   length, get16(rep.ExtPageLength));
        t = get16(((pConfigExtendedPageHeader_t)page)->ExtPageLength);
        if (t && get16(rep.ExtPageLength) != t)
            printf("Warning, page length in reply does not match page length in buffer\n  (%d vs. %d)\n",
                   get16(rep.ExtPageLength), t);
    }
    else
    {
        if (rep.Header.PageLength == 0)
            return 0;
        if (memcmp(&header, &rep.Header, sizeof header) != 0)
            printf("Warning, header in HEADER reply does not match header in READ reply\n  (%08x vs. %08x)\n",
                   get32x(*(U32 *)&header), get32x(*(U32 *)&rep.Header));
        t = ((pConfigPageHeader_t)page)->PageLength;
        if (t && rep.Header.PageLength != t)
            printf("Warning, page length in reply does not match page length in buffer\n  (%d vs. %d)\n",
                   rep.Header.PageLength, t);
    }

    return 1;
}


int
getConfigPage(MPT_PORT *port, int type, int number, int address, void *page, int pageSize)
{
    return getConfigPageAction(port, -1, type, number, address, page, pageSize);
}


void *
getConfigPageActionAlloc(MPT_PORT *port, int action, int type, int number, int address, int *length)
{
    void    *page;

    if (getConfigPageLength(port, type, number, address, length) == 1)
    {
        page = malloc(*length);

        if (getConfigPageAction(port, action, type, number, address, page, *length) == 1)
        {
            return page;
        }

        free(page);
    }

    return NULL;
}


void *
getConfigPageAlloc(MPT_PORT *port, int type, int number, int address, int *length)
{
    return getConfigPageActionAlloc(port, -1, type, number, address, length);
}


int
setConfigPageAction(MPT_PORT *port, int action, int type, int number, int address, void *page, int pageSize)
{
    Config_t             req;
    ConfigReply_t        rep;
    ConfigPageHeader_t   header;
    int                  length;
    int                  t;
    int                  ioc_status;

    memset(&req, 0, sizeof req);
    memset(&rep, 0, sizeof rep);

    if (getConfigPageHeader(port, type, number, address, &rep) != 1)
        return 0;

    header = rep.Header;
    length = get16(rep.ExtPageLength);

    req.Function            = MPI_FUNCTION_CONFIG;
    req.AliasIndex          = virtInit;
    if (action != -1)
        req.Action          = action;
    else if ((rep.Header.PageType & MPI_CONFIG_PAGEATTR_MASK) == MPI_CONFIG_PAGEATTR_PERSISTENT ||
             (rep.Header.PageType & MPI_CONFIG_PAGEATTR_MASK) == MPI_CONFIG_PAGEATTR_RO_PERSISTENT)
        req.Action          = MPI_CONFIG_ACTION_PAGE_WRITE_NVRAM;
    else
        req.Action          = MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT;
    req.ExtPageType         = rep.ExtPageType;
    req.ExtPageLength       = rep.ExtPageLength;
    req.Header              = rep.Header;
    req.PageAddress         = set32(address);

    if (doConfigPageRequest(port, &req, sizeof req - sizeof req.PageBufferSGE, &rep, sizeof rep,
                            NULL, 0, page, pageSize, SHORT_TIME) != 1)
        return 0;

    ioc_status = get16(rep.IOCStatus) & MPI_IOCSTATUS_MASK;

    if (ioc_status != MPI_IOCSTATUS_SUCCESS)
    {
        port->ioc_status = ioc_status;
        return 0;
    }

    if (type > MPI_CONFIG_PAGETYPE_EXTENDED)
    {
        if (get16(rep.ExtPageLength) == 0)
            return 0;
        if (memcmp(&header, &rep.Header, sizeof header) != 0)
            printf("Warning, header in HEADER reply does not match header in WRITE reply\n  (%08x vs. %08x)\n",
                   get32x(*(U32 *)&header), get32x(*(U32 *)&rep.Header));
        if (length != get16(rep.ExtPageLength))
            printf("Warning, length in HEADER reply does not match length in WRITE reply\n  (%d vs. %d)\n",
                   length, get16(rep.ExtPageLength));
        t = get16(((pConfigExtendedPageHeader_t)page)->ExtPageLength);
        if (t && get16(rep.ExtPageLength) != t)
            printf("Warning, page length in reply does not match page length in buffer\n  (%d vs. %d)\n",
                   get16(rep.ExtPageLength), t);
    }
    else
    {
        if (rep.Header.PageLength == 0)
            return 0;
        if (memcmp(&header, &rep.Header, sizeof header) != 0)
            printf("Warning, header in HEADER reply does not match header in WRITE reply\n  (%08x vs. %08x)\n",
                   get32x(*(U32 *)&header), get32x(*(U32 *)&rep.Header));
        t = ((pConfigPageHeader_t)page)->PageLength;
        if (t && rep.Header.PageLength != t)
            printf("Warning, page length in reply does not match page length in buffer\n  (%d vs. %d)\n",
                   rep.Header.PageLength, t);
    }

    return 1;
}


#if __sparc__
int
getProperty(MPT_PORT *port, char *name, char *buf, int bufLen)
{
    SYM_GET_PROPERTY     getProperty;

    getProperty.PtrName         = (UINT64)(UINT32)name;
    getProperty.PtrBuffer       = (UINT64)(UINT32)buf;
    getProperty.NameLen         = strlen(name);
    getProperty.BufferLen       = bufLen;
    getProperty.PropertyLen     = 0;

    if (ioctl(port->fileHandle, SYMIOCTL_GET_PROPERTY, &getProperty) == 0)
        return getProperty.PropertyLen;

    return 0;
}
#endif


void
setName(MPT_PORT *port, int bus, int target, void *req)
{
    SCSIIORequest_t         *req1 = (pSCSIIORequest_t)req;
    Mpi2SCSIIORequest_t     *req2 = (pMpi2SCSIIORequest_t)req;
    int                      dev_handle;


    if (mpi2)
    {
        if (mapBusTargetToDevHandle(port, bus, target, &dev_handle) != 1)
            dev_handle = 0;

        req2->DevHandle = set16(dev_handle);
    }
    else
    {
        req1->TargetID = mapOsToHwTarget(port, target);
        req1->Bus = bus;
    }
}


int
setMaxBusTarget(MPT_PORT *port)
{
    /* DOS, EFI, and SAS2 Solaris do not support devHandle to
     * bus/target mapping so we "fudge" things here
     */
#if DOS || EFI || __sparc__
    int   n;

    n = port->maxDevHandle;
    if (n > 256)
    {
        port->maxBuses = (n + 255) / 256;
        port->maxTargets = 256;
    }
    else
    {
        port->maxBuses = 1;
        port->maxTargets = n;
    }
#else
    int   maxBuses;
    int   minTargets;
    int   maxTargets;
    int   bus;
    int   target;
    int   devHandle;

    maxBuses = 0;
    minTargets = 0xffff;
    maxTargets = 0;
    for (devHandle = 0; devHandle < port->maxDevHandle; devHandle++)
    {
        if (mapDevHandleToBusTarget(port, devHandle, &bus, &target) == 1)
        {
            if (bus > maxBuses)
                maxBuses = bus;
            if (target < minTargets)
                minTargets = target;
            if (target > maxTargets)
                maxTargets = target;
        }
    }
    port->maxBuses = maxBuses + 1;
    port->minTargets = minTargets == 0xffff ? 0 : minTargets;
    port->maxTargets = maxTargets + 1;
#endif

    return 1;
}


int
mapDevHandleToBusTarget(MPT_PORT *port, int dev_handle, int *bus, int *target)
{
    int      t;

    if (port == mappedPort && dev_handle == mappedDevHandle)
    {
        *bus = mappedBus;
        *target = mappedTarget;
#if __sparc__
        if (*bus == 0xffff || *target == 0xffff)
            return 0;
#endif
        if (*bus == 0xffffffff || *target == 0xffffffff)
            return 0;
        else
            return 1;
    }

    *bus = 0xffffffff;
    *target = 0xffffffff;

    t = mapBTDH(port, bus, target, &dev_handle);

    if (t == 1)
    {
        mappedPort      = port;
        mappedBus       = *bus;
        mappedTarget    = *target;
        mappedDevHandle = dev_handle;

#if __sparc__
        if (*bus == 0xffff || *target == 0xffff)
            t = 0;
#endif
        if (*bus == 0xffffffff || *target == 0xffffffff)
            t = 0;
    }

    return t;
}


int
mapBusTargetToDevHandle(MPT_PORT *port, int bus, int target, int *dev_handle)
{
    int      t;

    if (port == mappedPort && bus == mappedBus && target == mappedTarget)
    {
        *dev_handle = mappedDevHandle;
        if (*dev_handle == 0xffff)
            return 0;
        else
            return 1;
    }

    *dev_handle = 0xffff;

    t = mapBTDH(port, &bus, &target, dev_handle);

    if (t == 1)
    {
        mappedPort      = port;
        mappedBus       = bus;
        mappedTarget    = target;
        mappedDevHandle = *dev_handle;

        if (*dev_handle == 0xffff)
            t = 0;
    }

    return t;
}


int
mapBTDH(MPT_PORT *port, int *bus, int *target, int *dev_handle)
{
#if WIN32
    int                  status;
    MPI_BTDH_MAP_SRB     srb;
    int                  inLen;
    int                  outLen;
    DWORD                retLen;

    memset(&srb, 0, sizeof srb);

    srb.Sic.Length          = sizeof srb - sizeof srb.Sic;
    srb.Sic.ControlCode     = MPI_BTDH_MAPPING;
    srb.Sic.HeaderLength    = sizeof srb.Sic;
    srb.Sic.Timeout         = SHORT_TIME;

    memcpy((char *)&srb.Sic.Signature, "4.00    ", 8);

    srb.Bus                 = *bus;
    srb.TargetID            = *target;
    srb.DevHandle           = *dev_handle;

    inLen                   = sizeof srb;
    outLen                  = sizeof srb;
    retLen                  = 0;

    status = DeviceIoControl(port->fileHandle, port->ioctlValue,
                             &srb, inLen, &srb, outLen, &retLen, NULL);

    *bus                    = srb.Bus;
    *target                 = srb.TargetID;
    *dev_handle             = srb.DevHandle;

    return status;
#endif
#if __linux__
    int                              status;
    struct mpt2_ioctl_btdh_mapping   btdh_mapping;

    memset(&btdh_mapping, 0, sizeof btdh_mapping);

    btdh_mapping.hdr.ioc_number = port->portNumber;

    btdh_mapping.bus        = *bus;
    btdh_mapping.id         = *target;
    btdh_mapping.handle     = *dev_handle;

    status = ioctl(port->fileHandle, MPT2BTDHMAPPING, &btdh_mapping);

    *bus                    = btdh_mapping.bus;
    *target                 = btdh_mapping.id;
    *dev_handle             = btdh_mapping.handle;

    return status == 0;
#endif
#if __sparc__
    int                  status;
    SYM_BTDH_MAPPING     btdhMapping;

    if (port->ioctlValue == MPTIOCTL_PASS_THRU) // this matches the mpt_sas driver
    {
        if (*bus == 0xffffffff && *target == 0xffffffff && *dev_handle != 0xffff)
        {
            *bus        = (*dev_handle >> 8) & 0xff;
            *target     = (*dev_handle >> 0) & 0xff;

            return 1;
        }

        if (*bus != 0xffffffff && *target != 0xffffffff && *dev_handle == 0xffff)
        {
            *dev_handle = ((*bus & 0xff) << 8) | ((*target & 0xff) << 0);

            return 1;
        }

        return 0;
    }
    else // legacy support for the lsimpt SAS2 driver
    {
        btdhMapping.Bus         = *bus;
        btdhMapping.TargetID    = *target;
        btdhMapping.DevHandle   = *dev_handle;

        status = ioctl(port->fileHandle, SYMIOCTL_BTDH_MAPPING, &btdhMapping);

        *bus                    = btdhMapping.Bus;
        *target                 = btdhMapping.TargetID;
        *dev_handle             = btdhMapping.DevHandle;

        return status == 0;
    }
#endif
#if DOS || EFI
    if (*bus == 0xffffffff && *target == 0xffffffff && *dev_handle != 0xffff)
    {
        *bus        = (*dev_handle >> 8) & 0xff;
        *target     = (*dev_handle >> 0) & 0xff;

        return 1;
    }

    if (*bus != 0xffffffff && *target != 0xffffffff && *dev_handle == 0xffff)
    {
        *dev_handle = ((*bus & 0xff) << 8) | ((*target & 0xff) << 0);

        return 1;
    }

    return 0;
#endif
}


int
mapOsToHwTarget(MPT_PORT *port, int target)
{
#if __sparc__
    if (port->pidType == MPI_FW_HEADER_PID_TYPE_FC)
    {
        char                 name[32];
        char                 buffer[16];
        U32                  wwnh;
        U32                  wwnl;
        U32                  port_id;
        FCDevicePage0_t      FCDevicePage0;
        int                  i;
        int                  t;

        if (port == mappedPort && target == mappedTarget)
            return mappedValue;

        mappedPort = port;
        mappedTarget = target;
        mappedValue = port->hostScsiId;

        sprintf(name, "target-%d-wwn-nv", target);
        t = getProperty(port, name, buffer, sizeof buffer);

        if (t != 8)
        {
            sprintf(name, "target-%d-wwn-cf", target);
            t = getProperty(port, name, buffer, sizeof buffer);
        }

        if (t != 8)
        {
            sprintf(name, "target-%d-wwn-hw", target);
            t = getProperty(port, name, buffer, sizeof buffer);
        }

        if (t == 8)
        {
#if i386
            wwnl = ((U32 *)buffer)[0];
            wwnh = ((U32 *)buffer)[1];
#else
            wwnh = ((U32 *)buffer)[0];
            wwnl = ((U32 *)buffer)[1];
#endif
            for (i = 0; i < 256; i++)
            {
                if (getConfigPage(port, MPI_CONFIG_PAGETYPE_FC_DEVICE, 0,
                                  MPI_FC_DEVICE_PGAD_FORM_BUS_TID + i,
                                  &FCDevicePage0, sizeof FCDevicePage0) == 1)
                {
                    if (wwnh == get32(FCDevicePage0.WWPN.High) &&
                        wwnl == get32(FCDevicePage0.WWPN.Low))
                    {
                        mappedValue = i;
                        return i;
                    }
                }
            }

            return mappedValue;
        }

        sprintf(name, "target-%d-did-nv", target);
        t = getProperty(port, name, buffer, sizeof buffer);

        if (t != 4)
        {
            sprintf(name, "target-%d-did-cf", target);
            t = getProperty(port, name, buffer, sizeof buffer);
        }

        if (t != 4)
        {
            sprintf(name, "target-%d-did-hw", target);
            t = getProperty(port, name, buffer, sizeof buffer);
        }

        if (t == 4)
        {
            port_id = ((U32 *)buffer)[0];

            for (i = 0; i < 256; i++)
            {
                if (getConfigPage(port, MPI_CONFIG_PAGETYPE_FC_DEVICE, 0,
                                  MPI_FC_DEVICE_PGAD_FORM_BUS_TID + i,
                                  &FCDevicePage0, sizeof FCDevicePage0) == 1)
                {
                    if (port_id == get32(FCDevicePage0.PortIdentifier))
                    {
                        mappedValue = i;
                        return i;
                    }
                }
            }

            return mappedValue;
        }

        target = port->hostScsiId;
    }
#endif
    return target;
}


int
doInquiry(MPT_PORT *port, int bus, int target, int lun, unsigned char *buf, int len)
{
    SCSIIORequest_t  req;
    SCSI_REPLY       rep;

    memset(&req, 0, sizeof req);

    req.Function            = MPI_FUNCTION_SCSI_IO_REQUEST;
    req.CDBLength           = 6;
    req.AliasIndex          = virtInit;
    req.LUN[1]              = lun;
    req.Control             = set32(MPI_SCSIIO_CONTROL_READ | tagType);
    req.CDB[0]              = 0x12;
    req.CDB[1]              = 0;
    req.CDB[2]              = 0;
    req.CDB[3]              = 0;
    req.CDB[4]              = len;
    req.CDB[5]              = 0;
    req.DataLength          = set32(len);

    return doScsiIo(port, bus, target, 0, &req, sizeof req - sizeof req.SGL, &rep, sizeof rep, buf, len, NULL, 0, IO_TIME);
}


int
doScsiIo(MPT_PORT *port, int bus, int target, int raid, void *req, int reqSize, SCSI_REPLY *rep1, int rep1Size,
         void *payIn, int payInSize, void *payOut, int payOutSize, int timeOut)
{
    int                      ioc_status;
    int                      ioc_loginfo;
    SCSIIORequest_t         *req1;
    Mpi2SCSIIORequest_t      req2;
    Mpi25SCSIIORequest_t     req25;
    SCSI_REPLY2              rep2;
    void                    *rep;
    int                      repSize;
    int                      command = -1;

    req1 = req;
    if (req1->Function == MPI_FUNCTION_SCSI_IO_REQUEST)
    {
        command = req1->CDB[0];

        setName(port, bus, target, req1);
        if (raid)
        {
            req1->Function = MPI_FUNCTION_RAID_SCSI_IO_PASSTHROUGH;
            if (mpi1)
                setName(port, 0, port->raidPhysdisk, req1);
        }

        if (mpi20)
        {
            memset(&req2, 0, sizeof req2);

            // convert from MPI 1.x format to MPI 2.x format
            req2.Function       = req1->Function;
            req2.DevHandle      = ((pMpi2SCSIIORequest_t)req1)->DevHandle;
            req2.Control        = req1->Control;
            req2.IoFlags        = set16(req1->CDBLength);
            req2.DataLength     = req1->DataLength;

            memcpy(req2.LUN, req1->LUN, sizeof req1->LUN);
            memcpy(req2.CDB.CDB32, req1->CDB, sizeof req1->CDB);

            req2.SGLOffset0     = offsetof(Mpi2SCSIIORequest_t, SGL) / 4;

            req = &req2;
            reqSize = sizeof req2 - sizeof req2.SGL;
        }
        else if (mpi25)
        {
            memset(&req25, 0, sizeof req25);

            // convert from MPI 1.x format to MPI 2.5 format
            req25.Function       = req1->Function;
            req25.DevHandle      = ((pMpi25SCSIIORequest_t)req1)->DevHandle;
            req25.Control        = req1->Control;
            req25.IoFlags        = set16(req1->CDBLength);
            req25.DataLength     = req1->DataLength;

            memcpy(req25.LUN, req1->LUN, sizeof req1->LUN);
            memcpy(req25.CDB.CDB32, req1->CDB, sizeof req1->CDB);

            req25.SGLOffset0     = offsetof(Mpi25SCSIIORequest_t, SGL) / 4;

            req     = &req25;
            reqSize = sizeof req25 - sizeof req25.SGL;
        }
    }

    if (mpi2)
    {
        rep = &rep2;
        repSize = sizeof rep2;
    }
    else
    {
        rep = rep1;
        repSize = rep1Size;
    }
    memset(rep, 0, repSize);

    if (doMptCommand(port, req, reqSize, rep, repSize,
                     payIn, payInSize, payOut, payOutSize, timeOut) != 1)
    {
#if __linux__
        if (errno == EFAULT)
            printf("SCSI command failed, mptscsih might not be loaded\n");
#endif
        return 0;
    }

    if (mpi2)
    {
        memcpy(&rep1->reply, &rep2.reply, sizeof rep1->reply);
        memcpy(&rep1->sense, &rep2.sense, sizeof rep1->sense);
    }

    ioc_status = get16(rep1->reply.IOCStatus) & MPI_IOCSTATUS_MASK;

    if (ioc_status == MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE)
        return 0;

    if (ioc_status == MPI_IOCSTATUS_BUSY ||
        ioc_status == MPI_IOCSTATUS_SCSI_IOC_TERMINATED ||
        ioc_status == MPI_IOCSTATUS_SCSI_RESIDUAL_MISMATCH ||
        (rep1->reply.SCSIStatus == MPI_SCSI_STATUS_CHECK_CONDITION && !(rep1->sense[2] & 0xf0)) ||
        rep1->reply.SCSIStatus == MPI_SCSI_STATUS_BUSY ||
        rep1->reply.SCSIStatus == MPI_SCSI_STATUS_TASK_SET_FULL)
    {
        memset(rep, 0, repSize);

        if (doMptCommand(port, req, reqSize, rep, repSize,
                         payIn, payInSize, payOut, payOutSize, timeOut) != 1)
            return 0;

        if (mpi2)
        {
            memcpy(&rep1->reply, &rep2.reply, sizeof rep1->reply);
            memcpy(&rep1->sense, &rep2.sense, sizeof rep1->sense);
        }

        ioc_status = get16(rep1->reply.IOCStatus) & MPI_IOCSTATUS_MASK;
    }

    if (ioc_status != MPI_IOCSTATUS_SUCCESS)
    {
        if (ioc_status == MPI_IOCSTATUS_SCSI_DATA_UNDERRUN ||
            ioc_status == MPI_IOCSTATUS_SCSI_RESIDUAL_MISMATCH)
        {
            if (rep1->reply.SCSIStatus == MPI_SCSI_STATUS_SUCCESS)
            {
                if (rep1->reply.TransferCount == 0)
                {
//                    printf("ScsiIo to Bus %d Target %d failed, IOCStatus = %04x (%s)\n",
//                           bus, target, ioc_status, translateIocStatus(ioc_status));
                    return 0;
                }
                else
                    return 1;
            }
        }
        else
        {
            if ((get16(rep1->reply.IOCStatus) & MPI_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE) != 0)
            {
                ioc_loginfo = get32(rep1->reply.IOCLogInfo);
                printf("ScsiIo to Bus %d Target %d failed, IOCStatus = %04x (%s), IOCLogInfo = %08x\n",
                       bus, target, ioc_status, translateIocStatus(ioc_status), ioc_loginfo);
            }
            else
                printf("ScsiIo to Bus %d Target %d failed, IOCStatus = %04x (%s)\n",
                       bus, target, ioc_status, translateIocStatus(ioc_status));

            return 0;
        }
    }

    if (rep1->reply.SCSIStatus != MPI_SCSI_STATUS_SUCCESS)
    {
        if (rep1->reply.SCSIStatus == MPI_SCSI_STATUS_CHECK_CONDITION)
        {
            if (rep1->sense[2] == 5 && rep1->sense[12] == 0x20 && rep1->sense[13] == 0x00)
                if (payInSize == 36 || payInSize == 32)
                    return 0;

            if (rep1->sense[2] == 5 && rep1->sense[12] == 0x24 && rep1->sense[13] == 0x00)
                if (payInSize == 36 || payInSize == 32 || (command == 0x3c && payInSize <= 4))
                    return 0;

            if (rep1->sense[2] == 5 && rep1->sense[12] == 0x25 && rep1->sense[13] == 0x00)
                if (payInSize == 36 || payInSize == 32 || (command == 0x3c && payInSize <= 4))
                    return 0;

            if (rep1->sense[2] == 0 && rep1->sense[12] == 0x00 && rep1->sense[13] == 0x00)
                return 0;

            if (rep1->sense[2] == 4 && rep1->sense[12] == 0x44 && rep1->sense[13] == 0x00)
                return 0;

            if (rep1->sense[2] == 0x08)
                return 0;

            if (rep1->sense[0] == 0xf0 && rep1->sense[2] & 0xf0)
                return 0;

            if (rep1->sense[0] & 0x80)
                printf("ScsiIo to Bus %d Target %d failed, Check Condition, Key = %d, ASC/ASCQ = %02Xh/%02Xh, Info = %08Xh (%d)\n",
                       bus, target, rep1->sense[2] & 0x0f, rep1->sense[12], rep1->sense[13], get4bytes(rep1->sense, 3), get4bytes(rep1->sense, 3));
            else
                printf("ScsiIo to Bus %d Target %d failed, Check Condition, Key = %d, ASC/ASCQ = %02Xh/%02Xh\n",
                       bus, target, rep1->sense[2] & 0x0f, rep1->sense[12], rep1->sense[13]);

//          dumpMemory(rep1->sense, get32(rep1->reply.SenseCount), "Sense Data");
        }
        else
        {
            printf("ScsiIo to Bus %d Target %d failed, SCSIStatus = %02x\n",
                   bus, target, rep1->reply.SCSIStatus);
        }
        return 0;
    }

    return 1;
}


int
doReadLongSata(MPT_PORT *port, int bus, int target, int lun,
               unsigned int lbn, int mode, unsigned char *buf, int len)
{
    SataPassthroughRequest_t     req;
    SataPassthroughReply_t       rep;
    unsigned char                cmd[512];

    memset(cmd, 0, sizeof cmd);

    cmd[0]                  = 1;
    cmd[2]                  = 1;
    cmd[4]                  = lbn;
    cmd[5]                  = lbn >> 8;
    cmd[6]                  = lbn >> 16;
    cmd[7]                  = lbn >> 24;

    memset(&req, 0, sizeof req);
    memset(&rep, 0, sizeof rep);

    req.Function            = MPI_FUNCTION_SATA_PASSTHROUGH;
    req.PassthroughFlags    = set16(MPI_SATA_PT_REQ_PT_FLAGS_PIO | MPI_SATA_PT_REQ_PT_FLAGS_WRITE);
    req.DataLength          = set32(sizeof cmd);
    req.CommandFIS[0]       = 0x27;
    req.CommandFIS[1]       = 0x80;
    req.CommandFIS[2]       = 0xb0;
    req.CommandFIS[3]       = 0xd6;
    req.CommandFIS[4]       = 0xe0;
    req.CommandFIS[5]       = 0x4f;
    req.CommandFIS[6]       = 0xc2;
    req.CommandFIS[12]      = 0x01;

    setName(port, bus, target, &req);

    if (doMptCommandCheck(port, &req, sizeof req - sizeof req.SGL, &rep, sizeof rep,
                          NULL, 0, cmd, sizeof cmd, SHORT_TIME) != 1)
    {
        printf("SataPassThrough (SCT / Long Sector Read / E0) failed!\n");
        return 0;
    }

    /* if the statusFIS error field has any bits set, then the request was not successful */
    if (rep.StatusFIS[3])
    {
        printf("SataPassThrough (SCT / Long Sector Read / E0) failed!\n");
        return 0;
    }
//    printf("StatusFIS->Error:   %02x\n", rep.StatusFIS[3]);
//    printf("StatusFIS->Status:  %02x\n", rep.StatusFIS[2]);

    req.PassthroughFlags    = set16(MPI_SATA_PT_REQ_PT_FLAGS_PIO | MPI_SATA_PT_REQ_PT_FLAGS_READ);
    req.DataLength          = set32(len);
    req.CommandFIS[3]       = 0xd5;
    req.CommandFIS[4]       = 0xe1;
    req.CommandFIS[12]      = 0x02;

    if (doMptCommandCheck(port, &req, sizeof req - sizeof req.SGL, &rep, sizeof rep,
                          buf, len, NULL, 0, SHORT_TIME) != 1)
    {
        printf("SataPassThrough (SCT / Long Sector Read / E1) failed!\n");
        return 0;
    }

    /* if the statusFIS error field has any bits set, then the request was not successful */
    if (rep.StatusFIS[3])
    {
        printf("SataPassThrough (SCT / Long Sector Read / E1) failed!\n");
        return 0;
    }
//    printf("StatusFIS->Error:   %02x\n", rep.StatusFIS[3]);
//    printf("StatusFIS->Status:  %02x\n", rep.StatusFIS[2]);

    return 1;
}


int
doFwUpload(MPT_PORT *port, int type, unsigned char *buf, int len, int offset, int *outLen)
{
    FWUpload_t              *req1;
    Mpi2FWUploadRequest_t    req;
    Mpi25FWUploadRequest_t   req25;
    FWUploadReply_t          rep;
    FWUploadTCSGE_t         *tc;
    int                      req_size;
    int                      size;
    int                      actual_size;
    int                      ioc_status;
    int                      mpt_return = 0;

    memset(&req,   0, sizeof req);
    memset(&req25, 0, sizeof req25);
    memset(&rep,   0, sizeof rep);

    req.Function  = req25.Function  = MPI_FUNCTION_FW_UPLOAD;
    req.ImageType = req25.ImageType = type;

    if (mpi20)
    {
       tc       = (pFWUploadTCSGE_t)&req.SGL;
       req_size = sizeof req - sizeof req.SGL + sizeof *tc;
    }
    else if (mpi25)
    {
        req_size = sizeof(req25) - sizeof(req25.SGL);
    }
    else
    {
        req1     = (pFWUpload_t)&req;
        tc       = (pFWUploadTCSGE_t)&req1->SGL;
        req_size = sizeof *req1 - sizeof req1->SGL + sizeof *tc;
    }

    if(port->mptVersion < MPI2_VERSION_02_05)
    {
        tc->ContextSize   = 0;
        tc->DetailsLength = 12;
        tc->Flags         = MPI_SGE_FLAGS_TRANSACTION_ELEMENT;
    }

    actual_size = 0;
    while (len > 0)
    {
        size = min(len, CHUNK_SIZE);

        if (mpi25)
        {
            req25.ImageSize   = set32(size);
            req25.ImageOffset = set32(offset);
            mpt_return = doMptCommand(port, &req25, req_size, &rep, sizeof rep,
                                      buf, size, NULL, 0, LONG_TIME);
        }
        else
        {
            tc->ImageSize   = set32(size);
            tc->ImageOffset = set32(offset);
            mpt_return = doMptCommand(port, &req, req_size, &rep, sizeof rep,
                                      buf, size, NULL, 0, LONG_TIME);
        }

        if (mpt_return != 1)
        {
            printf("Upload failed\n");
            return 0;
        }

        ioc_status = get16(rep.IOCStatus) & MPI_IOCSTATUS_MASK;
        actual_size = get32(rep.ActualImageSize);

        if (ioc_status != MPI_IOCSTATUS_SUCCESS)
        {
            if (ioc_status == MPI_IOCSTATUS_INVALID_FIELD && actual_size != 0)
            {
                if (offset + size > actual_size)
                {
                    len = actual_size - offset;
                    continue;
                }
            }

            printf("Upload failed, IOCStatus = %04x (%s)\n", ioc_status, translateIocStatus(ioc_status));
            return 0;
        }

        buf += size;
        len -= size;
        offset += size;
    }

    *outLen = actual_size;

    return 1;
}


int
doIocInit(MPT_PORT *port, int WhoInit)
{
    IOCInit_t        req;
    IOCInitReply_t   rep;
    IOCFactsReply_t  IOCFacts;

    if (mpi2)
        return 1;

    if (getIocFacts(port, &IOCFacts) != 1)
        return 0;

    if (IOCFacts.WhoInit == WhoInit)
        return 1;

    memset(&req, 0, sizeof req);
    memset(&rep, 0, sizeof rep);

    req.Function                    = MPI_FUNCTION_IOC_INIT;
    req.WhoInit                     = WhoInit;
    req.Flags                       = 0;
    req.MaxDevices                  = IOCFacts.MaxDevices;
    req.MaxBuses                    = IOCFacts.MaxBuses;
    req.ReplyFrameSize              = IOCFacts.CurReplyFrameSize;
    req.HostMfaHighAddr             = IOCFacts.CurrentHostMfaHighAddr;
    req.SenseBufferHighAddr         = IOCFacts.CurrentSenseBufferHighAddr;
    req.ReplyFifoHostSignalingAddr  = IOCFacts.ReplyFifoHostSignalingAddr;
    req.HostPageBufferSGE           = IOCFacts.HostPageBufferSGE;
    req.MsgVersion                  = set16(MPI_VERSION_01_05);
    req.HeaderVersion               = set16(MPI_HEADER_VERSION);

    if (IOCFacts.Flags & MPI_IOCFACTS_FLAGS_REPLY_FIFO_HOST_SIGNAL)
        req.Flags |= MPI_IOCINIT_FLAGS_REPLY_FIFO_HOST_SIGNAL;

#if __linux__
    if (oldMptBaseDetected)
    {
        // make the Linux IOCTL driver a bit happier
        if (req.MaxDevices > port->maxTargets)
            req.MaxDevices = port->maxTargets;
        if (req.MaxDevices == 0)
            req.MaxDevices = 255;
    }
    if (newMptBaseDetected)
    {
        // make the Linux IOCTL driver a bit happier
        req.MaxDevices = 0;
        req.MaxBuses = 0;
    }
#endif

    return doMptCommandCheck(port, &req, sizeof req, &rep, sizeof rep, NULL, 0, NULL, 0, SHORT_TIME);
}


char *
translateSmpFunctionResult(int functionResult)
{
    switch (functionResult)
    {
        case SMP_RESPONSE_FUNCTION_RESULT_ACCEPTED:                             return "Function Accepted";
        case SMP_RESPONSE_FUNCTION_RESULT_UNKNOWN_SMP_FUNCTION:                 return "Unknown Function";
        case SMP_RESPONSE_FUNCTION_RESULT_SMP_FUNCTION_FAILED:                  return "Function Failed";
        case SMP_RESPONSE_FUNCTION_RESULT_INVALID_REQUEST_LENGTH:               return "Invalid Request Length";
        case SMP_RESPONSE_FUNCTION_RESULT_INVALID_EXP_CHANGE_COUNT:             return "Invalid Expander Change Count";
        case SMP_RESPONSE_FUNCTION_RESULT_BUSY:                                 return "Busy";
        case SMP_RESPONSE_FUNCTION_RESULT_INCOMPLETE_DESCRIPTOR_LIST:           return "Incomplete Descriptor List";
        case SMP_RESPONSE_FUNCTION_RESULT_PHY_DOES_NOT_EXIST:                   return "Phy Does Not Exist";
        case SMP_RESPONSE_FUNCTION_RESULT_INDEX_DOES_NOT_EXIST:                 return "Index Does Not Exist";
        case SMP_RESPONSE_FUNCTION_RESULT_PHY_DOES_NOT_SUPPORT_SATA:            return "Phy Does Not Support SATA";
        case SMP_RESPONSE_FUNCTION_RESULT_UNKNOWN_PHY_OPERATION:                return "Unknown Phy Operation";
        case SMP_RESPONSE_FUNCTION_RESULT_UNKNOWN_PHY_TEST_FUNCTION:            return "Unknown Phy Test Function";
        case SMP_RESPONSE_FUNCTION_RESULT_PHY_TEST_FUNCTION_IN_PROG:            return "Phy Test Function In Progress";
        case SMP_RESPONSE_FUNCTION_RESULT_PHY_VACANT:                           return "Phy Vacant (No Access)";
        case SMP_RESPONSE_FUNCTION_RESULT_UNKNOWN_PHY_EVENT_SOURCE:             return "Unknown Phy Event Source";
        case SMP_RESPONSE_FUNCTION_RESULT_UNKNOWN_DESCRIPTOR_TYPE:              return "Unknown Descriptor Type";
        case SMP_RESPONSE_FUNCTION_RESULT_UNKNOWN_PHY_FILTER:                   return "Unknown Phy Filter";
        case SMP_RESPONSE_FUNCTION_RESULT_AFFILIATION_VIOLATION:                return "Affiliation Violation";
        case SMP_RESPONSE_FUNCTION_RESULT_SMP_ZONE_VIOLATION:                   return "Zone Violation";
        case SMP_RESPONSE_FUNCTION_RESULT_NO_MANAGEMENT_ACCESS_RIGHTS:          return "No Management Access Rights";
        case SMP_RESPONSE_FUNCTION_RESULT_UNKNOWN_ENABLE_DISABLE_ZONING_VALUE:  return "Unknown Enable Disable Zoning Value";
        case SMP_RESPONSE_FUNCTION_RESULT_ZONE_LOCK_VIOLATION:                  return "Zone Lock Violation";
        case SMP_RESPONSE_FUNCTION_RESULT_NOT_ACTIVATED:                        return "Not Activated";
        case SMP_RESPONSE_FUNCTION_RESULT_ZONE_GROUP_OUT_OF_RANGE:              return "Zone Group Out Of Range";
        case SMP_RESPONSE_FUNCTION_RESULT_NO_PHYSICAL_PRESENCE:                 return "No Physical Presence";
        case SMP_RESPONSE_FUNCTION_RESULT_SAVING_NOT_SUPPORTED:                 return "Saving Not Supported";
        case SMP_RESPONSE_FUNCTION_RESULT_SOURCE_ZONE_GROUP_DOES_NOT_EXIST:     return "Source Zone Group Does Not Exist";
        case SMP_RESPONSE_FUNCTION_RESULT_DISABLED_PASSWORD_NOT_SUPPORTED:      return "Disabled Password Not Supported";
    }

    return "";
}


int
doMptCommand(MPT_PORT *port, void *req, int reqSize, void *rep, int repSize,
             void *payIn, int payInSize, void *payOut, int payOutSize, int timeOut)
{

#if __linux__ || __alpha__
    int                          status;
    int                          extra;
    struct mpt_ioctl_command    *command;
    int                          retry;
    int                          function = ((pMPIHeader_t)req)->Function;

    extra = max(0, reqSize - sizeof command->MF);
    command = (struct mpt_ioctl_command *)malloc(sizeof *command + extra);

    memset(command, 0, sizeof *command);

    command->hdr.iocnum = port->portNumber;

    command->timeout            = timeOut;
    command->replyFrameBufPtr   = rep;
    command->dataInBufPtr       = payIn;
    command->dataOutBufPtr      = payOut;
    command->maxReplyBytes      = repSize;
    command->dataInSize         = payInSize;
    command->dataOutSize        = payOutSize;
    command->dataSgeOffset      = reqSize / 4;

    if (function == MPI_FUNCTION_SCSI_IO_REQUEST ||
        function == MPI_FUNCTION_RAID_SCSI_IO_PASSTHROUGH ||
        function == MPI_FUNCTION_SCSI_IO_32)
    {
        if (mpi2)
        {
            Mpi2SCSIIOReply_t   *scsiRep = (pMpi2SCSIIOReply_t)rep;

            command->senseDataPtr   = (char *)(scsiRep + 1);
            command->maxSenseBytes  = repSize - sizeof *scsiRep;
            command->maxReplyBytes  = sizeof *scsiRep;
        }
        else
        {
            SCSIIOReply_t   *scsiRep = (pSCSIIOReply_t)rep;

            command->senseDataPtr   = (char *)(scsiRep + 1);
            command->maxSenseBytes  = repSize - sizeof *scsiRep;
            command->maxReplyBytes  = sizeof *scsiRep;
        }
    }

    memcpy(command->MF, req, reqSize);


    for (retry = 0; retry < 10; retry++)
    {
        errno = 0;

        if (mpi2)
            status = ioctl(port->fileHandle, MPT2COMMAND, command);
        else
            status = ioctl(port->fileHandle, MPTCOMMAND, command);

        if (status != 0 && errno == EAGAIN)
        {
            sleep(1);
        }
        else
        {
            break;
        }
    }


#if __linux__
    if (status != 0)
    {
        if (errno == EFAULT)
        {
            if (((pMPIHeader_t)req)->Function == MPI_FUNCTION_IOC_INIT)
            {
                IOCInit_t   *iocinitReq = (pIOCInit_t)req;
                int          maxDevices;

                free(command);

                if (workaroundsTried == TRUE)
                    return 0;

                workaroundsTried = TRUE;

                // try to make the Linux IOCTL driver a bit happier

                maxDevices = iocinitReq->MaxDevices;
                if (iocinitReq->MaxDevices > port->maxTargets)
                    iocinitReq->MaxDevices = port->maxTargets;
                if (iocinitReq->MaxDevices == 0)
                    iocinitReq->MaxDevices = 255;
                if (iocinitReq->MaxDevices != maxDevices)
                {
                    status = doMptCommand(port, req, reqSize, rep, repSize,
                                          payIn, payInSize, payOut, payOutSize, timeOut);

                    if (status == 1)
                    {
                        oldMptBaseDetected = 1;
                        return 1;
                    }
                }

                if (iocinitReq->MaxDevices != 0 || iocinitReq->MaxBuses != 0)
                {
                    iocinitReq->MaxDevices = 0;
                    iocinitReq->MaxBuses = 0;

                    status = doMptCommand(port, req, reqSize, rep, repSize,
                                          payIn, payInSize, payOut, payOutSize, timeOut);

                    if (status == 1)
                    {
                        newMptBaseDetected = 1;
                        return 1;
                    }
                }

                return 0;
            }

            if (((pMPIHeader_t)req)->Function != MPI_FUNCTION_SCSI_IO_REQUEST)
            {
                printf("Command rejected by mptctl!\n");
            }
        }
    }
#endif

    free(command);

    return status == 0;
#endif

}


int
doMptCommandCheck(MPT_PORT *port, void *req, int reqSize, void *rep, int repSize,
                  void *payIn, int payInSize, void *payOut, int payOutSize, int timeOut)
{
    MPIDefaultReply_t   *defaultRep;
    int                  ioc_status;
    int                  ioc_loginfo;

    if (doMptCommand(port, req, reqSize, rep, repSize,
                     payIn, payInSize, payOut, payOutSize, timeOut) != 1)
    {
        printf("\nFailed to issue command\n");
        return 0;
    }

    defaultRep = (pMPIDefaultReply_t)rep;
    ioc_status = get16(defaultRep->IOCStatus) & MPI_IOCSTATUS_MASK;

    if (ioc_status != MPI_IOCSTATUS_SUCCESS)
    {
        if ((get16(defaultRep->IOCStatus) & MPI_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE) != 0)
        {
            ioc_loginfo = get32(defaultRep->IOCLogInfo);
            printf("\nCommand failed with IOCStatus = %04x (%s), IOCLogInfo = %08x\n",
                   ioc_status, translateIocStatus(ioc_status), ioc_loginfo);
        }
        else
            printf("\nCommand failed with IOCStatus = %04x (%s)\n",
                   ioc_status, translateIocStatus(ioc_status));

        return 0;
    }

    if (defaultRep->Function == MPI_FUNCTION_RAID_ACTION)
    {
        ioc_loginfo = get32(defaultRep->IOCLogInfo);
        if (ioc_loginfo)
        {
            printf("\nRAID ACTION returned IOCLogInfo = %08x\n", ioc_loginfo);
        }
    }

    return 1;
}


char *
translateIocStatus(int ioc_status)
{
    // Bit 15 means "Log Info Available".  Therefore,
    // we only want bits 0 through 14.  Otherwise,
    // this routine won't work when log info is available.
    ioc_status &= MPI_IOCSTATUS_MASK;

    switch (ioc_status)
    {
    case MPI_IOCSTATUS_SUCCESS:                     return "Success";
    case MPI_IOCSTATUS_INVALID_FUNCTION:            return "Invalid Function";
    case MPI_IOCSTATUS_BUSY:                        return "IOC Busy";
    case MPI_IOCSTATUS_INVALID_SGL:                 return "Invalid SGL";
    case MPI_IOCSTATUS_INTERNAL_ERROR:              return "Internal Error";
    case MPI_IOCSTATUS_INSUFFICIENT_RESOURCES:      return "Insufficient Resources";
    case MPI_IOCSTATUS_INVALID_FIELD:               return "Invalid Field";
    case MPI_IOCSTATUS_INVALID_STATE:               return "Invalid State";
    case MPI_IOCSTATUS_OP_STATE_NOT_SUPPORTED:      return "Operational State Not Supported";
    case MPI_IOCSTATUS_CONFIG_INVALID_ACTION:       return "Invalid Action";
    case MPI_IOCSTATUS_CONFIG_INVALID_TYPE:         return "Invalid Type";
    case MPI_IOCSTATUS_CONFIG_INVALID_PAGE:         return "Invalid Page";
    case MPI_IOCSTATUS_CONFIG_INVALID_DATA:         return "Invalid Data";
    case MPI_IOCSTATUS_CONFIG_NO_DEFAULTS:          return "No Defaults";
    case MPI_IOCSTATUS_CONFIG_CANT_COMMIT:          return "Can't Commit";
    case MPI_IOCSTATUS_SCSI_RECOVERED_ERROR:        return "Recovered Error";
    case MPI_IOCSTATUS_SCSI_INVALID_BUS:            return "Invalid Bus";
    case MPI_IOCSTATUS_SCSI_INVALID_TARGETID:       return "Invalid Target";
    case MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE:       return "Device Not There";
    case MPI_IOCSTATUS_SCSI_DATA_OVERRUN:           return "Data Overrun";
    case MPI_IOCSTATUS_SCSI_DATA_UNDERRUN:          return "Data Underrun";
    case MPI_IOCSTATUS_SCSI_IO_DATA_ERROR:          return "I/O Data Error";
    case MPI_IOCSTATUS_SCSI_PROTOCOL_ERROR:         return "Protocol Error";
    case MPI_IOCSTATUS_SCSI_TASK_TERMINATED:        return "Task Terminated";
    case MPI_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:      return "Residual Mismatch";
    case MPI_IOCSTATUS_SCSI_TASK_MGMT_FAILED:       return "Task Managment Failed";
    case MPI_IOCSTATUS_SCSI_IOC_TERMINATED:         return "IOC Terminated";
    case MPI_IOCSTATUS_SCSI_EXT_TERMINATED:         return "Externally Terminated";
    case MPI_IOCSTATUS_EEDP_GUARD_ERROR:            return "EEDP Guard Error";
    case MPI_IOCSTATUS_EEDP_REF_TAG_ERROR:          return "EEDP Reference Tag Error";
    case MPI_IOCSTATUS_EEDP_APP_TAG_ERROR:          return "EEDP Application Tag Error";
    case MPI_IOCSTATUS_TARGET_PRIORITY_IO:          return "Target Priority I/O";
    case MPI_IOCSTATUS_TARGET_INVALID_PORT:         return "Invalid Port";
    case MPI_IOCSTATUS_TARGET_INVALID_IO_INDEX:     return "Invalid I/O Index";
    case MPI_IOCSTATUS_TARGET_ABORTED:              return "Target Aborted";
    case MPI_IOCSTATUS_TARGET_NO_CONN_RETRYABLE:    return "No Connection, Retryable";
    case MPI_IOCSTATUS_TARGET_NO_CONNECTION:        return "No Connection";
    case MPI_IOCSTATUS_TARGET_XFER_COUNT_MISMATCH:  return "Transfer Count Mismatch";
    case MPI_IOCSTATUS_TARGET_STS_DATA_NOT_SENT:    return "Status Data Not Sent";
    case MPI_IOCSTATUS_TARGET_DATA_OFFSET_ERROR:    return "Data Offset Error";
    case MPI_IOCSTATUS_TARGET_TOO_MUCH_WRITE_DATA:  return "Too Much Write Data";
    case MPI_IOCSTATUS_TARGET_IU_TOO_SHORT:         return "Target IU Too Short";
    case MPI_IOCSTATUS_FC_ABORTED:                  return "FC Aborted";
    case MPI_IOCSTATUS_FC_RX_ID_INVALID:            return "RX_ID Invalid";
    case MPI_IOCSTATUS_FC_DID_INVALID:              return "D_ID Invalid";
    case MPI_IOCSTATUS_FC_NODE_LOGGED_OUT:          return "Node Logged Out";
    case MPI_IOCSTATUS_FC_EXCHANGE_CANCELED:        return "Exchange Canceled";
    case MPI_IOCSTATUS_LAN_DEVICE_NOT_FOUND:        return "LAN Device Not Found";
    case MPI_IOCSTATUS_LAN_DEVICE_FAILURE:          return "LAN Device Failure";
    case MPI_IOCSTATUS_LAN_TRANSMIT_ERROR:          return "LAN Transmit Error";
    case MPI_IOCSTATUS_LAN_TRANSMIT_ABORTED:        return "LAN Transmit Aborted";
    case MPI_IOCSTATUS_LAN_RECEIVE_ERROR:           return "LAN Receive Error";
    case MPI_IOCSTATUS_LAN_RECEIVE_ABORTED:         return "LAN Receive Aborted";
    case MPI_IOCSTATUS_LAN_PARTIAL_PACKET:          return "LAN Partial Packet";
    case MPI_IOCSTATUS_LAN_CANCELED:                return "LAN Canceled";
    case MPI_IOCSTATUS_SAS_SMP_REQUEST_FAILED:      return "SMP Request Failed";
    case MPI_IOCSTATUS_SAS_SMP_DATA_OVERRUN:        return "SMP Data Overrun";
    case MPI_IOCSTATUS_INBAND_ABORTED:              return "Inband Aborted";
    case MPI_IOCSTATUS_INBAND_NO_CONNECTION:        return "Inband No Connection";
    case MPI_IOCSTATUS_DIAGNOSTIC_RELEASED:         return "Diagnostic Buffer Released";
    case MPI2_IOCSTATUS_RAID_ACCEL_ERROR:           return "RAID Accelerator Error";
    }

    return "";
}



typedef struct
{
    CONFIG_PAGE_HEADER      Header;
    MPI_CHIP_REVISION_ID    ChipId;

    U8      SubsystemVendorId_0[2];
    U8      SubsystemId_0[2];
    U8      ClassCode_0[3];
    U8      PciMemory;
    U8      HardwareConfig[1];
    U8      SubsystemVendorId_1[2];
    U8      SubsystemId_1[2];
    U8      ClassCode_1[3];
    U8      Checksum;
    U8      Filler[3];
} ManufacturingPage2_929_t;


typedef struct
{
    CONFIG_PAGE_HEADER      Header;
    MPI_CHIP_REVISION_ID    ChipId;

    U8      SubsystemVendorId_0[2];
    U8      SubsystemId_0[2];
    U8      ClassCode_0[3];
    U8      PciMemory;
    U8      HardwareConfig[2];
    U8      SubsystemVendorId_1[2];
    U8      SubsystemId_1[2];
    U8      ClassCode_1[3];
    U8      Checksum;
    U8      Filler[2];
} ManufacturingPage2_929X_t;


typedef struct
{
    CONFIG_PAGE_HEADER      Header;
    MPI_CHIP_REVISION_ID    ChipId;

    U8      SubsystemVendorId[2];
    U8      SubsystemId[2];
    U8      ClassCode[3];
    U8      PciMemory;
    U8      PciPowerBudgetData[8][3];
    U8      Checksum;
    U8      Filler[3];
} ManufacturingPage2_949E_t;


typedef struct
{
    CONFIG_PAGE_HEADER      Header;
    MPI_CHIP_REVISION_ID    ChipId;

   _U64     WWPN_0;
   _U64     WWNN_0;
   _U32     PhyRegs1_0;
   _U32     PhyRegs2_0;
   _U32     PhyRegs2_Alt_0;
    U8      MfgSupportedSpeeds_0;
    U8      MfgLinkType_0;
    U8      MfgConnectorType_0;
    U8      Filler_0;
   _U64     WWPN_1;
   _U64     WWNN_1;
   _U32     PhyRegs1_1;
   _U32     PhyRegs2_1;
   _U32     PhyRegs2_Alt_1;
    U8      MfgSupportedSpeeds_1;
    U8      MfgLinkType_1;
    U8      MfgConnectorType_1;
    U8      Filler_1;
} ManufacturingPage3_929_t;


typedef struct
{
    CONFIG_PAGE_HEADER      Header;
    MPI_CHIP_REVISION_ID    ChipId;

   _U64     WWPN_0;
   _U64     WWNN_0;
   _U32     PhyRegs1_0;
    U32     Unused1_0;
    U32     Unused2_0;
    U8      MfgSupportedSpeeds_0;
    U8      MfgLinkType_0;
    U8      MfgConnectorType_0;
    U8      Filler_0;
   _U64     WWPN_1;
   _U64     WWNN_1;
   _U32     PhyRegs1_1;
    U32     Unused1_1;
    U32     Unused2_1;
    U8      MfgSupportedSpeeds_1;
    U8      MfgLinkType_1;
    U8      MfgConnectorType_1;
    U8      Filler_1;
   _U32     PhyRegs234[3*2*3];
} ManufacturingPage3_949_t;


#undef mpi1
#undef mpi2
#undef mpi20
#undef mpi25
#undef MPI1
#undef MPI2
#undef MPI20
#undef MPI25


typedef struct
{
   _U32          Signature;
    U8           State;
    U8           Checksum;
   _U16          TotalBytes;
   _U16          NvdataVersion;
   _U16          MpiVersion;
    U8           CdhSize;
    U8           CdeSize;
    U8           PphSize;
    U8           ProdIdSize;
   _U32          NbrDirEntries;
   _U32          NbrPersistDirEntries;
   _U32          SeepromFwVarsOffset;
   _U32          SeepromBufferOffset;
    U32          Reserved;
} CONFIG_DIR_HEADER;

typedef struct
{
   _U32          Signature;
    U8           State;
    U8           Reserved1;
   _U16          TotalBytes;
   _U16          NvdataVersion;
   _U16          MpiVersion;
    U8           CdhSize;
    U8           CdeSize;
    U8           PphSize;
    U8           ProdIdSize;
   _U32          NbrDirEntries;
   _U32          NbrPersistDirEntries;
    U32          Reserved3;
   _U16          ProductIdOffset;
   _U16          DirEntryOffset;
   _U32          VendorNvramVersion;
} CONFIG_DIR_HEADER2;

#define CONFIG_DIR_HEADER_SIGNATURE             (0x4E69636B)

#define CONFIG_DIR_HEADER_STATE_ERASED          (0xFF)
#define CONFIG_DIR_HEADER_STATE_INITIALIZATION  (CONFIG_DIR_HEADER_STATE_ERASED         & ~0x01)
#define CONFIG_DIR_HEADER_STATE_RCV_DATA        (CONFIG_DIR_HEADER_STATE_INITIALIZATION & ~0x02)
#define CONFIG_DIR_HEADER_STATE_VALID           (CONFIG_DIR_HEADER_STATE_RCV_DATA       & ~0x04)
#define CONFIG_DIR_HEADER_STATE_XFER_COMPLETE   (CONFIG_DIR_HEADER_STATE_VALID          & ~0x08)


typedef struct
{
   _U32          Signature;
    U8           VendorId[8];
    U8           ProductId[16];
    U8           ProductRevision[4];
    U32          Reserved1;
    U32          Reserved2;
    U32          Reserved3;
    U32          Reserved4;
    U32          Reserved5;
    U32          Reserved6;
    U32          Reserved7;
    U32          Reserved8;
} CONFIG_PROD_ID;

#define CONFIG_PROD_ID_SIGNATURE                (0x4672617A)


typedef struct
{
    U32          State              : 4;
    U32          AllocUnits         : 12;
    U32          PageType           : 8;
    U32          PageNum            : 4;
    U32          ForceNvdataUpdate  : 1;
    U32          PersistPageUpdated : 1;
    U32          FlagRsvd1          : 1;
    U32          FlagRsvd2          : 1;
    U32          DwordOffset        : 15;
    U32          IocNum             : 1;
    U32          PageAddress        : 16;
} CONFIG_DIR_ENTRY;

typedef struct
{
    U8           State;
    U8           Reserved;
   _U16          AllocUnits;
    U8           PageType;
    U8           PageNum;
    U8           UpdateFlags;
    U8           IocNum;
   _U32          DwordOffset;
   _U32          PageAddress;
} CONFIG_DIR_ENTRY2;

#define CONFIG_DIR_ENTRY_STATE_ERASED           (0xF)
#define CONFIG_DIR_ENTRY_STATE_BEGIN_UPDATE     (CONFIG_DIR_ENTRY_STATE_ERASED          & ~0x1)
#define CONFIG_DIR_ENTRY_STATE_IN_USE           (CONFIG_DIR_ENTRY_STATE_BEGIN_UPDATE    & ~0x2)


typedef struct
{
    U8           State;
    U8           Checksum;
   _U16          DwordOffset;
} PERSISTENT_PAGE_HEADER;

typedef struct
{
    U8           State;
    U8           Checksum;
    U16          Reserved;
   _U32          DwordOffset;
} PERSISTENT_PAGE_HEADER2;

#define CONFIG_PERSISTENT_HEADER_STATE_ERASED           (0xFF)
#define CONFIG_PERSISTENT_HEADER_STATE_BEGIN_UPATE      (CONFIG_PERSISTENT_HEADER_STATE_ERASED          & ~0x01)
#define CONFIG_PERSISTENT_HEADER_STATE_UPDATE_COMPLETE  (CONFIG_PERSISTENT_HEADER_STATE_BEGIN_UPATE     & ~0x02)
#define CONFIG_PERSISTENT_HEADER_STATE_USE_NEXT_COPY    (CONFIG_PERSISTENT_HEADER_STATE_UPDATE_COMPLETE & ~0x04)


#if VERIFY_ENDIANNESS && EFIEBC


int
concatenateSasFirmwareNvdata(void)
{
    return 0;
}


#else


typedef struct
{
    CONFIG_PAGE_HEADER       Header;
    MPI_CHIP_REVISION_ID     ChipId;
    U16                      SubSystemIDFunc0;
    U16                      SubSystemVendorIDFunc0;
    U8                       PCIMemDiagSize;
    U8                       Reserved05;
    U16                      SubSystemIDFunc1;
    U16                      SubSystemVendorIDFunc1;
    U8                       AutoDownloadChecksum;
    U8                       Reserved0B;
    U8                       VendorIDDeviceIDLock;
    U8                       Reserved0D;
    U16                      VendorID0;
    U16                      DeviceID0;
    U16                      VendorID1;
    U16                      DeviceID1;
    U8                       ClassCode0[3];
    U8                       Reserved19;
    U8                       ClassCode1[3];
    U8                       Reserved1D;
    U16                      HardwareConfig;
    U32                      OptionRomOffset0;
    U32                      OptionRomOffset1;
} ManufacturingPage2_SAS_t, *pManufacturingPage2_SAS_t;


typedef struct
{
    MPI2_CONFIG_PAGE_HEADER  Header;
    MPI2_CHIP_REVISION_ID    ChipId;
    U32                      Foo[19];
} ManufacturingPage2_SAS2_t, *pManufacturingPage2_SAS2_t;


#define IOC_MFG_PAGE3_GPIO_DEFS                 (8)
#define IOC_MFG_PAGE3_NUM_PHYS_PER_QUAD         (4)
#define IOC_MFG_PAGE3_NUM_QUADS                 (2)

typedef struct
{
    U32                      GigablazeConfig[4];
    U32                      Reserved[1];
} IOC_PHY_CONFIG;

typedef struct
{
    U8                       HotPlugTimeout;
    U8                       MaxCmdFrames;
    U16                      Reserved1;
    U32                      Reserved2[4];
    IOC_PHY_CONFIG           PhyConfig[IOC_MFG_PAGE3_NUM_PHYS_PER_QUAD];
} IOC_QUAD_CONFIG;

typedef struct
{
    CONFIG_PAGE_HEADER       Header;
    MPI_CHIP_REVISION_ID     ChipId;
    U16                      GPIODefinition[IOC_MFG_PAGE3_GPIO_DEFS];
    U8                       FlashTime;
    U8                       NVTime;
    U8                       Flag;
    U8                       RuntimeConfig;
    U8                       SGPIOType;
    U8                       SEPType;
    U8                       PCIELaneConfig;
    U8                       Reserved0;
    U32                      PCIEConfig2;
    U32                      Reserved2;
    U8                       Reserved3[2];
    U8                       Reserved4;
    U8                       Reserved5;
    IOC_QUAD_CONFIG          QuadConfig[IOC_MFG_PAGE3_NUM_QUADS];
} ManufacturingPage3_SAS_t, *pManufacturingPage3_SAS_t;


#define IOC_MFG_PAGE3_NUM_PHYS                  (16)

typedef struct _IOC_PHY_GROUP
{
    U32                      Misc;
    U32                      Sas1G1Low;
    U32                      Sas1G1High;
    U32                      Sas1G2Low;
    U32                      Sas1G2High;
    U32                      SasOobLow;
    U32                      SasOobHigh;
    U32                      Sas2G1Low;
    U32                      Sas2G1High;
    U32                      Sas2G2Low;
    U32                      Sas2G2High;
    U32                      Sas2G3Low;
    U32                      Sas2G3High;
    U32                      SataG1Low;
    U32                      SataG1High;
    U32                      SataG2Low;
    U32                      SataG2High;
    U32                      SataG3Low;
    U32                      SataG3High;
    U32                      SataOobLow;
    U32                      SataOobHigh;
} IOC_PHY_GROUP;

typedef struct
{
    MPI2_CONFIG_PAGE_HEADER  Header;
    MPI2_CHIP_REVISION_ID    ChipId;
    U32                      Reserved1;
    U32                      Reserved2;
    U32                      Reserved3;
    U32                      Reserved4;
    IOC_PHY_GROUP            PhyGroup[4];
    U8                       NumPhys;
    U8                       Reserved5;
    U8                       Reserved6;
    U8                       Reserved7;
    U32                      Phy[IOC_MFG_PAGE3_NUM_PHYS];
} ManufacturingPage3_SAS2_t, *pManufacturingPage3_SAS2_t;


#define IOC_MFG_PAGE6_GPIO_DEFS                 (32)

typedef struct
{
    U8                       FunctionCode;
    U8                       Flags;
    U8                       Param1;
    U8                       Param2;
    U32                      Param3;
} IOC_CFG_MFG_6_GPIO_DEF;

typedef struct
{
    MPI2_CONFIG_PAGE_HEADER  Header;
    U8                       NumGPIO;
    U8                       Reserved1[3];
    U32                      Reserved2;
    U32                      Reserved3;
    IOC_CFG_MFG_6_GPIO_DEF   GPIODefinition[IOC_MFG_PAGE6_GPIO_DEFS];
} ManufacturingPage6_SAS2_t, *pManufacturingPage6_SAS2_t;


#define IOC_CFG_MFG9_NUMBER_OF_RESOURCES        (13)

typedef struct
{
    U32                      Maximum;
    U32                      Decrement;
    U32                      Minimum;
    U32                      Actual;
} IOC_CFG_MFG9_RESOURCE;

typedef struct
{
    MPI2_CONFIG_PAGE_HEADER  Header;
    U32                      MaxAttempts;
    U32                      NumResources;
    U32                      Reserved1;
    U32                      Reserved2;
    IOC_CFG_MFG9_RESOURCE    ResourceArray[IOC_CFG_MFG9_NUMBER_OF_RESOURCES];
} ManufacturingPage9_SAS2_t, *pManufacturingPage9_SAS2_t;


typedef struct
{
    U8                       Flags;
    U8                       MoreFlags;
    U16                      TO;
    U32                      BaudRate;
} IOC_CFG_UART_SETTINGS;

typedef struct
{
    MPI2_CONFIG_PAGE_HEADER  Header;
    U8                       FlashTime;
    U8                       NVTime;
    U8                       Flag;
    U8                       Reserved1;
    U8                       HotPlugTimeout;
    U8                       Reserved[3];
    U8                       MaxCmdFrames[4];
    U32                      SysRefClk;
    U32                      Reserved2;
    U32                      ExtUartClk;
    IOC_CFG_UART_SETTINGS    UartSettings[2];
} ManufacturingPage11_SAS2_t, *pManufacturingPage11_SAS2_t;


#define IOC_MAN_PAGE_12_SGPIO_INFO_ENTRIES         (4)

typedef struct
{
    U32                      Flags;
    U32                      BitOrderSelect[12];
} SGPIO_CONFIG_INFO;

typedef struct
{
    MPI2_CONFIG_PAGE_HEADER  Header;
    U32                      Flags;
    U32                      Reserved1;
    U32                      Reserved2;
    U32                      SGPIOCfg1;
    U8                       NumSGPIO;
    U8                       SGPIOType;
    U16                      ClkDivide;
    U32                      DefaultTxCtrl;
    U32                      SGPIOPatDef0;
    U32                      SGPIOPatDef1;
    U32                      SGPIOPatDef2;
    U32                      SGPIOPatDef3;
    SGPIO_CONFIG_INFO        SGPIOInfo[IOC_MAN_PAGE_12_SGPIO_INFO_ENTRIES];
} ManufacturingPage12_SAS2_t, *pManufacturingPage12_SAS2_t;


#define IOC_MAN_PAGE_13_SGPIO_ENTRIES         (4)

typedef struct _SGPIO_TRANSLATION_DATA
{
    U32                      Mask;
    U32                      SlotStatus;
    U8                       TxControl[4];
} SGPIO_TRANSLATION_DATA, *PTR_SGPIO_TRANSLATION_DATA;

typedef struct
{
    MPI2_CONFIG_PAGE_HEADER  Header;
    U8                       NumSgpioEntries;
    U8                       Reserved0;
    U16                      Reserved1;
    U32                      Reserved2;
    SGPIO_TRANSLATION_DATA   SGPIOData[IOC_MAN_PAGE_13_SGPIO_ENTRIES];
} ManufacturingPage13_SAS2_t, *pManufacturingPage13_SAS2_t;


typedef struct
{
    SAS_ADDRESS  SasAddress;
    U32          Reserved;
} SAS_PERSISTENT_ID_ENTRY;

#define SAS_NUM_PERSIST_IDS_PER_PAGE            (0x01)

typedef struct
{
    CONFIG_EXTENDED_PAGE_HEADER  Header;
    SAS_PERSISTENT_ID_ENTRY      PersistId[SAS_NUM_PERSIST_IDS_PER_PAGE];
} PersistentId_SAS_t, *pPersistentId_SAS_t;


#define STR     (1<<0)  // item is a string
#define OPT     (1<<1)  // item is optional
#define DUP     (1<<2)  // item is a duplicate
#define BIT     (1<<3)  // item size is in bits, not bytes
#define IGN     (1<<4)  // item should be ignored if zero
#define MPI1    (1<<5)  // item only applies to MPI 1.x
#define MPI2    (1<<6)  // item only applies to MPI 2.0
//TMC: MPI2.5 TODO

typedef struct
{
    char        *name;
    int          offset;
    int          size;
    int          flags;
} ITEM;

#define EXT     (1<<0)  // section is an extended config page
#define GEN     (1<<1)  // section is "General"
#define PID     (1<<2)  // section is "Persistent ID"
#define MP2     (1<<3)  // section is "Manufacturing Page 2"

typedef struct
{
    char        *name;
    ITEM        *items;
    int          size;
    int          flags;
} SECTION;

typedef struct
{
    U8           SasAddress[6];
    U8           UserVersion;
    U8           VendorId[8];
    U8           ProductId[16];
    U8           ProductRevision[4];
} GeneralData_t, *pGeneralData_t;

#undef data
#define data(x) (int)(size_t)&((pGeneralData_t)0)->x, sizeof(((pGeneralData_t)0)->x)

ITEM general_data_items[] =
{
    {"SAS_ADRS_PREFIX",         data(SasAddress),       STR | OPT},
    {"USER_VERSION",            data(UserVersion),      0},
    {"NVDATA_VENDORID",         data(VendorId),         STR},
    {"NVDATA_PRODUCTID",        data(ProductId),        STR},
    {"NVDATA_PRODUCT_REVISION", data(ProductRevision),  STR},
    {0}
};

ITEM special_item =
{
    " SPECIAL ", 0, 0, OPT
};

ITEM forceupdate_item =
{
    "FORCEUPDATE", 0, 1, BIT
};

#undef data
#define data(x) (int)(size_t)&((pConfigPageHeader_t)0)->x, sizeof(((pConfigPageHeader_t)0)->x)

ITEM header_items[] =
{
    {"PAGE_VERSION",    data(PageVersion),  0},
    {"PAGE_LENGTH",     data(PageLength),   0},
    {"PAGE_NUMBER",     data(PageNumber),   0},
    {"PAGE_TYPE",       data(PageType),     0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pConfigExtendedPageHeader_t)0)->x, sizeof(((pConfigExtendedPageHeader_t)0)->x)

ITEM ext_header_items[] =
{
    {"PAGE_VERSION",                            data(PageVersion),      0},
    {"RESERVED1",                               data(Reserved1),        OPT},
    {"CONFIG_EXTENDED_PAGE_HEADER_RESERVED1",   data(Reserved1),        OPT},
    {"PAGE_NUMBER",                             data(PageNumber),       0},
    {"PAGE_TYPE",                               data(PageType),         0},
    {"EXT_PAGE_LENGTH",                         data(ExtPageLength),    0},
    {"EXT_PAGE_TYPE",                           data(ExtPageType),      0},
    {"RESERVED2",                               data(Reserved2),        OPT},
    {"CONFIG_EXTENDED_PAGE_HEADER_RESERVED2",   data(Reserved2),        OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pManufacturingPage0_t)0)->x, sizeof(((pManufacturingPage0_t)0)->x)

ITEM manufacturing_page_0_items[] =
{
    {"CHIP_NAME",           data(ChipName),             STR},
    {"CHIP_REVISION",       data(ChipRevision),         STR},
    {"BOARD_NAME",          data(BoardName),            STR},
    {"BOARD_ASSEMBLY",      data(BoardAssembly),        STR},
    {"BOARD_TRACER_NUMBER", data(BoardTracerNumber),    STR},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pManufacturingPage1_t)0)->x, sizeof(((pManufacturingPage1_t)0)->x)

ITEM manufacturing_page_1_items[] =
{
    {"VPD", data(VPD), STR},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pManufacturingPage2_SAS_t)0)->x, sizeof(((pManufacturingPage2_SAS_t)0)->x)

ITEM manufacturing_page_2_items[] =
{
    {"DEVICE_ID",                       data(ChipId.DeviceID),          0},
    {"PCI_REVISION",                    data(ChipId.PCIRevisionID),     0},
    {"MPI_CHIP_REVISION_ID_RESERVED",   data(ChipId.Reserved),          OPT},
    {"RESERVED",                        data(ChipId.Reserved),          OPT},
    {"SSID_FCN_0",                      data(SubSystemIDFunc0),         0},
    {"SSVID_FCN_0",                     data(SubSystemVendorIDFunc0),   0},
    {"PCI_MEM_DIAG_SIZE",               data(PCIMemDiagSize),           0},
    {"RESERVED0",                       data(Reserved05),               OPT},
    {"RESERVED05",                      data(Reserved05),               OPT},
    {"SSID_FCN_1",                      data(SubSystemIDFunc1),         0},
    {"SSVID_FCN_1",                     data(SubSystemVendorIDFunc1),   0},
    {"AUTODOWNLOAD_CHECKSUM",           data(AutoDownloadChecksum),     0},
    {"RESERVED1",                       data(Reserved0B),               OPT},
    {"RESERVED0B",                      data(Reserved0B),               OPT},
    {"VENDORIDDEVICEIDLOCK",            data(VendorIDDeviceIDLock),     0},
    {"RESERVED2",                       data(Reserved0D),               OPT},
    {"RESERVED0D",                      data(Reserved0D),               OPT},
    {"VENDOR_ID_0",                     data(VendorID0),                0},
    {"DEVICE_ID_0",                     data(DeviceID0),                0},
    {"VENDOR_ID_1",                     data(VendorID1),                0},
    {"DEVICE_ID_1",                     data(DeviceID1),                0},
    {"CC_0_SPECIFIC_CLASS",             data(ClassCode0[0]),            0},
    {"CC_0_SUB_CLASS",                  data(ClassCode0[1]),            0},
    {"CC_0_BASE_CLASS",                 data(ClassCode0[2]),            0},
    {"RESERVED3",                       data(Reserved19),               OPT},
    {"RESERVED19",                      data(Reserved19),               OPT},
    {"CC_1_SPECIFIC_CLASS",             data(ClassCode1[0]),            0},
    {"CC_1_SUB_CLASS",                  data(ClassCode1[1]),            0},
    {"CC_1_BASE_CLASS",                 data(ClassCode1[2]),            0},
    {"RESERVED4",                       data(Reserved1D),               OPT},
    {"RESERVED1D",                      data(Reserved1D),               OPT},
    {"HARDWARECONFIG",                  data(HardwareConfig),           0},
    {"OPTIONROMOFFSETFUNC0",            data(OptionRomOffset0),         0},
    {"OPTIONROMOFFSETFUNC1",            data(OptionRomOffset1),         0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pManufacturingPage2_SAS2_t)0)->x, sizeof(((pManufacturingPage2_SAS2_t)0)->x)

ITEM manufacturing_page_2_items2[] =
{
    {"DEVICE_ID",                       data(ChipId.DeviceID),          0},
    {"PCI_REVISION",                    data(ChipId.PCIRevisionID),     0},
    {"MPI_CHIP_REVISION_ID_RESERVED",   data(ChipId.Reserved),          OPT},
//  {"FOO_0",                           data(Foo[0]),                   0},
//  {"FOO_1",                           data(Foo[1]),                   0},
//  {"FOO_2",                           data(Foo[2]),                   0},
//  {"FOO_3",                           data(Foo[3]),                   0},
//  {"FOO_4",                           data(Foo[4]),                   0},
//  {"FOO_5",                           data(Foo[5]),                   0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pManufacturingPage3_SAS_t)0)->x, sizeof(((pManufacturingPage3_SAS_t)0)->x)

ITEM manufacturing_page_3_items[] =
{
    {"DEVICE_ID",                           data(ChipId.DeviceID),                                  0},
    {"PCI_REVISION_ID",                     data(ChipId.PCIRevisionID),                             0},
    {"RESERVED",                            data(ChipId.Reserved),                                  OPT},
    {"MPI_CHIP_REVISION_ID_RESERVED",       data(ChipId.Reserved),                                  OPT},
    {"GPIODEFINITION_0",                    data(GPIODefinition[0]),                                0},
    {"GPIODEFINITION_1",                    data(GPIODefinition[1]),                                0},
    {"GPIODEFINITION_2",                    data(GPIODefinition[2]),                                0},
    {"GPIODEFINITION_3",                    data(GPIODefinition[3]),                                0},
    {"GPIODEFINITION_4",                    data(GPIODefinition[4]),                                0},
    {"GPIODEFINITION_5",                    data(GPIODefinition[5]),                                0},
    {"GPIODEFINITION_6",                    data(GPIODefinition[6]),                                0},
    {"GPIODEFINITION_7",                    data(GPIODefinition[7]),                                0},
    {"FLASH_TIME",                          data(FlashTime),                                        0},
    {"NVS_TIME",                            data(NVTime),                                           0},
    {"FLAG",                                data(Flag),                                             0},
    {"RUNTIMECONFIG",                       data(RuntimeConfig),                                    0},
    {"SGPIOTYPE",                           data(SGPIOType),                                        0},
    {"MP3_SEPTYPE",                         data(SEPType),                                          0},
    {"PCIELANECONFIG",                      data(PCIELaneConfig),                                   OPT},
    {"RESERVED",                            data(PCIELaneConfig),                                   OPT},
    {"RESERVED0",                           data(Reserved0),                                        OPT},
    {"PCIECONFIG2",                         data(PCIEConfig2),                                      OPT},
    {"RESERVED1",                           data(PCIEConfig2),                                      OPT},
    {"RESERVED2",                           data(Reserved2),                                        OPT},
    {"RESERVED3_0",                         data(Reserved3[0]),                                     OPT},
    {"RESERVED3_1",                         data(Reserved3[1]),                                     OPT},
    {"RESERVED4",                           data(Reserved4),                                        OPT},
    {"RESERVED5",                           data(Reserved5),                                        OPT},
    {"HOT_PLUG_TIM_OUT",                    data(QuadConfig[0].HotPlugTimeout),                     DUP},
    {"IOC_QUAD_CONFIG0_HOT_PLUG_TIM_OUT",   data(QuadConfig[0].HotPlugTimeout),                     0},
    {"MAX_CMD_FRAMES",                      data(QuadConfig[0].MaxCmdFrames),                       DUP},
    {"IOC_QUAD_CONFIG0_MAX_CMD_FRAMES",     data(QuadConfig[0].MaxCmdFrames),                       0},
    {"RESERVED1",                           data(QuadConfig[0].Reserved1),                          OPT},
    {"IOC_QUAD_CONFIG0_RESERVED1",          data(QuadConfig[0].Reserved1),                          OPT},
    {"RESERVED2",                           data(QuadConfig[0].Reserved2),                          OPT},
    {"IOC_QUAD_CONFIG0_RESERVED2_0",        data(QuadConfig[0].Reserved2[0]),                       OPT},
    {"IOC_QUAD_CONFIG0_RESERVED2_1",        data(QuadConfig[0].Reserved2[1]),                       OPT},
    {"IOC_QUAD_CONFIG0_RESERVED2_2",        data(QuadConfig[0].Reserved2[2]),                       OPT},
    {"IOC_QUAD_CONFIG0_RESERVED2_3",        data(QuadConfig[0].Reserved2[3]),                       OPT},
    {"QUAD0_PHY0_GIG_CONFIG0",              data(QuadConfig[0].PhyConfig[0].GigablazeConfig[0]),    0},
    {"QUAD0_PHY0_GIG_CONFIG1",              data(QuadConfig[0].PhyConfig[0].GigablazeConfig[1]),    0},
    {"QUAD0_PHY0_GIG_CONFIG2",              data(QuadConfig[0].PhyConfig[0].GigablazeConfig[2]),    0},
    {"QUAD0_PHY0_GIG_CONFIG3",              data(QuadConfig[0].PhyConfig[0].GigablazeConfig[3]),    0},
    {"RESERVED1",                           data(QuadConfig[0].PhyConfig[0].Reserved),              OPT},
    {"QUAD0_PHY0_GIG_CONFIG3_RESERVED",     data(QuadConfig[0].PhyConfig[0].Reserved),              OPT},
    {"QUAD0_PHY1_GIG_CONFIG0",              data(QuadConfig[0].PhyConfig[1].GigablazeConfig[0]),    0},
    {"QUAD0_PHY1_GIG_CONFIG1",              data(QuadConfig[0].PhyConfig[1].GigablazeConfig[1]),    0},
    {"QUAD0_PHY1_GIG_CONFIG2",              data(QuadConfig[0].PhyConfig[1].GigablazeConfig[2]),    0},
    {"QUAD0_PHY1_GIG_CONFIG3",              data(QuadConfig[0].PhyConfig[1].GigablazeConfig[3]),    0},
    {"RESERVED1",                           data(QuadConfig[0].PhyConfig[1].Reserved),              OPT},
    {"QUAD0_PHY1_GIG_CONFIG3_RESERVED",     data(QuadConfig[0].PhyConfig[1].Reserved),              OPT},
    {"QUAD0_PHY2_GIG_CONFIG0",              data(QuadConfig[0].PhyConfig[2].GigablazeConfig[0]),    0},
    {"QUAD0_PHY2_GIG_CONFIG1",              data(QuadConfig[0].PhyConfig[2].GigablazeConfig[1]),    0},
    {"QUAD0_PHY2_GIG_CONFIG2",              data(QuadConfig[0].PhyConfig[2].GigablazeConfig[2]),    0},
    {"QUAD0_PHY2_GIG_CONFIG3",              data(QuadConfig[0].PhyConfig[2].GigablazeConfig[3]),    0},
    {"RESERVED1",                           data(QuadConfig[0].PhyConfig[2].Reserved),              OPT},
    {"QUAD0_PHY2_GIG_CONFIG3_RESERVED",     data(QuadConfig[0].PhyConfig[2].Reserved),              OPT},
    {"QUAD0_PHY3_GIG_CONFIG0",              data(QuadConfig[0].PhyConfig[3].GigablazeConfig[0]),    0},
    {"QUAD0_PHY3_GIG_CONFIG1",              data(QuadConfig[0].PhyConfig[3].GigablazeConfig[1]),    0},
    {"QUAD0_PHY3_GIG_CONFIG2",              data(QuadConfig[0].PhyConfig[3].GigablazeConfig[2]),    0},
    {"QUAD0_PHY3_GIG_CONFIG3",              data(QuadConfig[0].PhyConfig[3].GigablazeConfig[3]),    0},
    {"RESERVED1",                           data(QuadConfig[0].PhyConfig[3].Reserved),              OPT},
    {"QUAD0_PHY3_GIG_CONFIG3_RESERVED",     data(QuadConfig[0].PhyConfig[3].Reserved),              OPT},
    {"HOT_PLUG_TIM_OUT",                    data(QuadConfig[1].HotPlugTimeout),                     DUP},
    {"IOC_QUAD_CONFIG1_HOT_PLUG_TIM_OUT",   data(QuadConfig[1].HotPlugTimeout),                     0},
    {"MAX_CMD_FRAMES",                      data(QuadConfig[1].MaxCmdFrames),                       DUP},
    {"IOC_QUAD_CONFIG1_MAX_CMD_FRAMES",     data(QuadConfig[1].MaxCmdFrames),                       0},
    {"RESERVED1",                           data(QuadConfig[1].Reserved1),                          OPT},
    {"IOC_QUAD_CONFIG1_RESERVED1",          data(QuadConfig[1].Reserved1),                          OPT},
    {"RESERVED2",                           data(QuadConfig[1].Reserved2),                          OPT},
    {"IOC_QUAD_CONFIG1_RESERVED2_0",        data(QuadConfig[1].Reserved2[0]),                       OPT},
    {"IOC_QUAD_CONFIG1_RESERVED2_1",        data(QuadConfig[1].Reserved2[1]),                       OPT},
    {"IOC_QUAD_CONFIG1_RESERVED2_2",        data(QuadConfig[1].Reserved2[2]),                       OPT},
    {"IOC_QUAD_CONFIG1_RESERVED2_3",        data(QuadConfig[1].Reserved2[3]),                       OPT},
    {"QUAD1_PHY0_GIG_CONFIG0",              data(QuadConfig[1].PhyConfig[0].GigablazeConfig[0]),    0},
    {"QUAD1_PHY0_GIG_CONFIG1",              data(QuadConfig[1].PhyConfig[0].GigablazeConfig[1]),    0},
    {"QUAD1_PHY0_GIG_CONFIG2",              data(QuadConfig[1].PhyConfig[0].GigablazeConfig[2]),    0},
    {"QUAD1_PHY0_GIG_CONFIG3",              data(QuadConfig[1].PhyConfig[0].GigablazeConfig[3]),    0},
    {"RESERVED1",                           data(QuadConfig[1].PhyConfig[0].Reserved),              OPT},
    {"QUAD1_PHY0_GIG_CONFIG3_RESERVED",     data(QuadConfig[1].PhyConfig[0].Reserved),              OPT},
    {"QUAD1_PHY1_GIG_CONFIG0",              data(QuadConfig[1].PhyConfig[1].GigablazeConfig[0]),    0},
    {"QUAD1_PHY1_GIG_CONFIG1",              data(QuadConfig[1].PhyConfig[1].GigablazeConfig[1]),    0},
    {"QUAD1_PHY1_GIG_CONFIG2",              data(QuadConfig[1].PhyConfig[1].GigablazeConfig[2]),    0},
    {"QUAD1_PHY1_GIG_CONFIG3",              data(QuadConfig[1].PhyConfig[1].GigablazeConfig[3]),    0},
    {"RESERVED1",                           data(QuadConfig[1].PhyConfig[1].Reserved),              OPT},
    {"QUAD1_PHY1_GIG_CONFIG3_RESERVED",     data(QuadConfig[1].PhyConfig[1].Reserved),              OPT},
    {"QUAD1_PHY2_GIG_CONFIG0",              data(QuadConfig[1].PhyConfig[2].GigablazeConfig[0]),    0},
    {"QUAD1_PHY2_GIG_CONFIG1",              data(QuadConfig[1].PhyConfig[2].GigablazeConfig[1]),    0},
    {"QUAD1_PHY2_GIG_CONFIG2",              data(QuadConfig[1].PhyConfig[2].GigablazeConfig[2]),    0},
    {"QUAD1_PHY2_GIG_CONFIG3",              data(QuadConfig[1].PhyConfig[2].GigablazeConfig[3]),    0},
    {"RESERVED1",                           data(QuadConfig[1].PhyConfig[2].Reserved),              OPT},
    {"QUAD1_PHY2_GIG_CONFIG3_RESERVED",     data(QuadConfig[1].PhyConfig[2].Reserved),              OPT},
    {"QUAD1_PHY3_GIG_CONFIG0",              data(QuadConfig[1].PhyConfig[3].GigablazeConfig[0]),    0},
    {"QUAD1_PHY3_GIG_CONFIG1",              data(QuadConfig[1].PhyConfig[3].GigablazeConfig[1]),    0},
    {"QUAD1_PHY3_GIG_CONFIG2",              data(QuadConfig[1].PhyConfig[3].GigablazeConfig[2]),    0},
    {"QUAD1_PHY3_GIG_CONFIG3",              data(QuadConfig[1].PhyConfig[3].GigablazeConfig[3]),    0},
    {"RESERVED1",                           data(QuadConfig[1].PhyConfig[3].Reserved),              OPT},
    {"QUAD1_PHY3_GIG_CONFIG3_RESERVED",     data(QuadConfig[1].PhyConfig[3].Reserved),              OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pManufacturingPage3_SAS2_t)0)->x, sizeof(((pManufacturingPage3_SAS2_t)0)->x)

ITEM manufacturing_page_3_items2[] =
{
    {"DEVICE_ID",                       data(ChipId.DeviceID),          0},
    {"PCI_REVISION_ID",                 data(ChipId.PCIRevisionID),     0},
    {"MPI_CHIP_REVISION_ID_RESERVED",   data(ChipId.Reserved),          OPT},
    {"RESERVED1",                       data(Reserved1),                OPT},
    {"RESERVED2",                       data(Reserved2),                OPT},
    {"RESERVED3",                       data(Reserved3),                OPT},
    {"RESERVED4",                       data(Reserved4),                OPT},
    {"GROUP0_MISC",                     data(PhyGroup[0].Misc),         0},
    {"GROUP0_SAS1G1LOW",                data(PhyGroup[0].Sas1G1Low),    0},
    {"GROUP0_SAS1G1HIGH",               data(PhyGroup[0].Sas1G1High),   0},
    {"GROUP0_SAS1G2LOW",                data(PhyGroup[0].Sas1G2Low),    0},
    {"GROUP0_SAS1G2HIGH",               data(PhyGroup[0].Sas1G2High),   0},
    {"GROUP0_SASOOBLOW",                data(PhyGroup[0].SasOobLow),    0},
    {"GROUP0_SASOOBHIGH",               data(PhyGroup[0].SasOobHigh),   0},
    {"GROUP0_SAS2G1LOW",                data(PhyGroup[0].Sas2G1Low),    0},
    {"GROUP0_SAS2G1HIGH",               data(PhyGroup[0].Sas2G1High),   0},
    {"GROUP0_SAS2G2LOW",                data(PhyGroup[0].Sas2G2Low),    0},
    {"GROUP0_SAS2G2HIGH",               data(PhyGroup[0].Sas2G2High),   0},
    {"GROUP0_SAS2G3LOW",                data(PhyGroup[0].Sas2G3Low),    0},
    {"GROUP0_SAS2G3HIGH",               data(PhyGroup[0].Sas2G3High),   0},
    {"GROUP0_SATAG1LOW",                data(PhyGroup[0].SataG1Low),    0},
    {"GROUP0_SATAG1HIGH",               data(PhyGroup[0].SataG1High),   0},
    {"GROUP0_SATAG2LOW",                data(PhyGroup[0].SataG2Low),    0},
    {"GROUP0_SATAG2HIGH",               data(PhyGroup[0].SataG2High),   0},
    {"GROUP0_SATAG3LOW",                data(PhyGroup[0].SataG3Low),    0},
    {"GROUP0_SATAG3HIGH",               data(PhyGroup[0].SataG3High),   0},
    {"GROUP0_SATAOOBLOW",               data(PhyGroup[0].SataOobLow),   0},
    {"GROUP0_SATAOOBHIGH",              data(PhyGroup[0].SataOobHigh),  0},
    {"GROUP1_MISC",                     data(PhyGroup[1].Misc),         0},
    {"GROUP1_SAS1G1LOW",                data(PhyGroup[1].Sas1G1Low),    0},
    {"GROUP1_SAS1G1HIGH",               data(PhyGroup[1].Sas1G1High),   0},
    {"GROUP1_SAS1G2LOW",                data(PhyGroup[1].Sas1G2Low),    0},
    {"GROUP1_SAS1G2HIGH",               data(PhyGroup[1].Sas1G2High),   0},
    {"GROUP1_SASOOBLOW",                data(PhyGroup[1].SasOobLow),    0},
    {"GROUP1_SASOOBHIGH",               data(PhyGroup[1].SasOobHigh),   0},
    {"GROUP1_SAS2G1LOW",                data(PhyGroup[1].Sas2G1Low),    0},
    {"GROUP1_SAS2G1HIGH",               data(PhyGroup[1].Sas2G1High),   0},
    {"GROUP1_SAS2G2LOW",                data(PhyGroup[1].Sas2G2Low),    0},
    {"GROUP1_SAS2G2HIGH",               data(PhyGroup[1].Sas2G2High),   0},
    {"GROUP1_SAS2G3LOW",                data(PhyGroup[1].Sas2G3Low),    0},
    {"GROUP1_SAS2G3HIGH",               data(PhyGroup[1].Sas2G3High),   0},
    {"GROUP1_SATAG1LOW",                data(PhyGroup[1].SataG1Low),    0},
    {"GROUP1_SATAG1HIGH",               data(PhyGroup[1].SataG1High),   0},
    {"GROUP1_SATAG2LOW",                data(PhyGroup[1].SataG2Low),    0},
    {"GROUP1_SATAG2HIGH",               data(PhyGroup[1].SataG2High),   0},
    {"GROUP1_SATAG3LOW",                data(PhyGroup[1].SataG3Low),    0},
    {"GROUP1_SATAG3HIGH",               data(PhyGroup[1].SataG3High),   0},
    {"GROUP1_SATAOOBLOW",               data(PhyGroup[1].SataOobLow),   0},
    {"GROUP1_SATAOOBHIGH",              data(PhyGroup[1].SataOobHigh),  0},
    {"GROUP2_MISC",                     data(PhyGroup[2].Misc),         0},
    {"GROUP2_SAS1G1LOW",                data(PhyGroup[2].Sas1G1Low),    0},
    {"GROUP2_SAS1G1HIGH",               data(PhyGroup[2].Sas1G1High),   0},
    {"GROUP2_SAS1G2LOW",                data(PhyGroup[2].Sas1G2Low),    0},
    {"GROUP2_SAS1G2HIGH",               data(PhyGroup[2].Sas1G2High),   0},
    {"GROUP2_SASOOBLOW",                data(PhyGroup[2].SasOobLow),    0},
    {"GROUP2_SASOOBHIGH",               data(PhyGroup[2].SasOobHigh),   0},
    {"GROUP2_SAS2G1LOW",                data(PhyGroup[2].Sas2G1Low),    0},
    {"GROUP2_SAS2G1HIGH",               data(PhyGroup[2].Sas2G1High),   0},
    {"GROUP2_SAS2G2LOW",                data(PhyGroup[2].Sas2G2Low),    0},
    {"GROUP2_SAS2G2HIGH",               data(PhyGroup[2].Sas2G2High),   0},
    {"GROUP2_SAS2G3LOW",                data(PhyGroup[2].Sas2G3Low),    0},
    {"GROUP2_SAS2G3HIGH",               data(PhyGroup[2].Sas2G3High),   0},
    {"GROUP2_SATAG1LOW",                data(PhyGroup[2].SataG1Low),    0},
    {"GROUP2_SATAG1HIGH",               data(PhyGroup[2].SataG1High),   0},
    {"GROUP2_SATAG2LOW",                data(PhyGroup[2].SataG2Low),    0},
    {"GROUP2_SATAG2HIGH",               data(PhyGroup[2].SataG2High),   0},
    {"GROUP2_SATAG3LOW",                data(PhyGroup[2].SataG3Low),    0},
    {"GROUP2_SATAG3HIGH",               data(PhyGroup[2].SataG3High),   0},
    {"GROUP2_SATAOOBLOW",               data(PhyGroup[2].SataOobLow),   0},
    {"GROUP2_SATAOOBHIGH",              data(PhyGroup[2].SataOobHigh),  0},
    {"GROUP3_MISC",                     data(PhyGroup[3].Misc),         0},
    {"GROUP3_SAS1G1LOW",                data(PhyGroup[3].Sas1G1Low),    0},
    {"GROUP3_SAS1G1HIGH",               data(PhyGroup[3].Sas1G1High),   0},
    {"GROUP3_SAS1G2LOW",                data(PhyGroup[3].Sas1G2Low),    0},
    {"GROUP3_SAS1G2HIGH",               data(PhyGroup[3].Sas1G2High),   0},
    {"GROUP3_SASOOBLOW",                data(PhyGroup[3].SasOobLow),    0},
    {"GROUP3_SASOOBHIGH",               data(PhyGroup[3].SasOobHigh),   0},
    {"GROUP3_SAS2G1LOW",                data(PhyGroup[3].Sas2G1Low),    0},
    {"GROUP3_SAS2G1HIGH",               data(PhyGroup[3].Sas2G1High),   0},
    {"GROUP3_SAS2G2LOW",                data(PhyGroup[3].Sas2G2Low),    0},
    {"GROUP3_SAS2G2HIGH",               data(PhyGroup[3].Sas2G2High),   0},
    {"GROUP3_SAS2G3LOW",                data(PhyGroup[3].Sas2G3Low),    0},
    {"GROUP3_SAS2G3HIGH",               data(PhyGroup[3].Sas2G3High),   0},
    {"GROUP3_SATAG1LOW",                data(PhyGroup[3].SataG1Low),    0},
    {"GROUP3_SATAG1HIGH",               data(PhyGroup[3].SataG1High),   0},
    {"GROUP3_SATAG2LOW",                data(PhyGroup[3].SataG2Low),    0},
    {"GROUP3_SATAG2HIGH",               data(PhyGroup[3].SataG2High),   0},
    {"GROUP3_SATAG3LOW",                data(PhyGroup[3].SataG3Low),    0},
    {"GROUP3_SATAG3HIGH",               data(PhyGroup[3].SataG3High),   0},
    {"GROUP3_SATAOOBLOW",               data(PhyGroup[3].SataOobLow),   0},
    {"GROUP3_SATAOOBHIGH",              data(PhyGroup[3].SataOobHigh),  0},
    {"NUM_PHYS",                        data(NumPhys),                  0},
    {"RESERVED5",                       data(Reserved5),                OPT},
    {"RESERVED6",                       data(Reserved6),                OPT},
    {"RESERVED7",                       data(Reserved7),                OPT},
    {"PHY_0",                           data(Phy[0]),                   0},
    {"PHY_1",                           data(Phy[1]),                   0},
    {"PHY_2",                           data(Phy[2]),                   0},
    {"PHY_3",                           data(Phy[3]),                   0},
    {"PHY_4",                           data(Phy[4]),                   0},
    {"PHY_5",                           data(Phy[5]),                   0},
    {"PHY_6",                           data(Phy[6]),                   0},
    {"PHY_7",                           data(Phy[7]),                   0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pManufacturingPage4_t)0)->x, sizeof(((pManufacturingPage4_t)0)->x)
#define off(x) (int)(size_t)&((pManufacturingPage4_t)0)->x

ITEM manufacturing_page_4_items[] =
{
    {"RESERVED1",           data(Reserved1),            OPT},
    {"INFO_OFFSET_0",       data(InfoOffset0),          0},
    {"INFO_SIZE_0",         data(InfoSize0),            0},
    {"INFO_OFFSET_1",       data(InfoOffset1),          0},
    {"INFO_SIZE_1",         data(InfoSize1),            0},
    {"INQUIRY_SIZE",        data(InquirySize),          0},
    {"MP4_FLAGS",           data(Flags),                0},
    {"RESERVED2",           data(ExtFlags),             OPT},
    {"EXTENDED_FLAGS",      data(ExtFlags),             OPT},
    {"DEVICE_TYPE",         off(InquiryData[0]), 1,     0},
    {"DEVICE_TYPE_MOD",     off(InquiryData[1]), 1,     0},
    {"VERSIONS",            off(InquiryData[2]), 1,     0},
    {"DATA_FORMAT",         off(InquiryData[3]), 1,     0},
    {"ADDITIONAL_LENGTH",   off(InquiryData[4]), 1,     0},
    {"CAPABILITY_BITS",     off(InquiryData[7]), 1,     0},
    {"VENDOR_ID",           off(InquiryData[8]), 8,     STR},
    {"PRODUCT_ID",          off(InquiryData[16]), 16,   STR},
    {"PRODUCT_REV",         off(InquiryData[32]), 4,    STR},
    {"VENDOR_SPECIFIC",     off(InquiryData[36]), 20,   STR},
    {"ISVOLUMESETTINGS",    data(ISVolumeSettings),     0},
    {"IMEVOLUMESETTINGS",   data(IMEVolumeSettings),    0},
    {"IMVOLUMESETTINGS",    data(IMVolumeSettings),     0},
    {"RESERVED3",           data(Reserved3),            OPT},
    {"RESERVED4",           data(Reserved4),            OPT},
    {"RESERVED5",           data(Reserved5),            OPT},
    {"IME_DATASCRUBRATE",   data(IMEDataScrubRate),     0},
    {"IME_RESYNCRATE",      data(IMEResyncRate),        0},
    {"RESERVED6",           data(Reserved6),            OPT},
    {"IM_DATASCRUBRATE",    data(IMDataScrubRate),      0},
    {"IM_RESYNCRATE",       data(IMResyncRate),         0},
    {"RESERVED7",           data(Reserved7),            OPT},
    {"RESERVED8",           data(Reserved8),            OPT},
    {"RESERVED9",           data(Reserved9),            OPT},
    {0}
};

#undef off
#undef data
#define data(x) (int)(size_t)&((pMpi2ManufacturingPage4_t)0)->x, sizeof(((pMpi2ManufacturingPage4_t)0)->x)
#define off(x) (int)(size_t)&((pMpi2ManufacturingPage4_t)0)->x

ITEM manufacturing_page_4_items2[] =
{
    {"RESERVED1",               data(Reserved1),                                        OPT},
    {"FLAGS",                   data(Flags),                                            0},
    {"INQUIRY_SIZE",            data(InquirySize),                                      0},
    {"RESERVED2",               data(Reserved2),                                        OPT},
    {"RESERVED3",               data(Reserved3),                                        OPT},
    {"DEVICE_TYPE",             off(InquiryData[0]), 1,                                 0},
    {"DEVICE_TYPE_MOD",         off(InquiryData[1]), 1,                                 0},
    {"VERSIONS",                off(InquiryData[2]), 1,                                 0},
    {"DATA_FORMAT",             off(InquiryData[3]), 1,                                 0},
    {"ADDITIONAL_LENGTH",       off(InquiryData[4]), 1,                                 0},
    {"CAPABILITY_BITS",         off(InquiryData[7]), 1,                                 0},
    {"VENDOR_ID",               off(InquiryData[8]), 8,                                 STR},
    {"PRODUCT_ID",              off(InquiryData[16]), 16,                               STR},
    {"PRODUCT_REV",             off(InquiryData[32]), 4,                                STR},
    {"VENDOR_SPECIFIC",         off(InquiryData[36]), 20,                               STR},
    {"RAID0VOLUMESETTINGS",     data(RAID0VolumeSettings),                              0},
    {"RAID1EVOLUMESETTINGS",    data(RAID1EVolumeSettings),                             0},
    {"RAID1VOLUMESETTINGS",     data(RAID1VolumeSettings),                              0},
    {"RAID10VOLUMESETTINGS",    data(RAID10VolumeSettings),                             0},
    {"RESERVED4",               data(Reserved4),                                        OPT},
    {"RESERVED5",               data(Reserved5),                                        OPT},
    {"POWERSAVEFLAGS",          data(PowerSaveSettings.PowerSaveFlags),                 0},
    {"INTOPSLEEPTIME",          data(PowerSaveSettings.InternalOperationsSleepTime),    0},
    {"INTOPRUNTIME",            data(PowerSaveSettings.InternalOperationsRunTime),      0},
    {"HOSTIDLETIME",            data(PowerSaveSettings.HostIdleTime),                   0},
    {"MAXOCEDISKS",             data(MaxOCEDisks),                                      0},
    {"RESYNCRATE",              data(ResyncRate),                                       0},
    {"DATASCRUBDURATION",       data(DataScrubDuration),                                0},
    {"MAXHOTSPARES",            data(MaxHotSpares),                                     0},
    {"MAXPHYSDISKSPERVOL",      data(MaxPhysDisksPerVol),                               0},
    {"MAXPHYSDISKS",            data(MaxPhysDisks),                                     0},
    {"MAXVOLUMES",              data(MaxVolumes),                                       0},
    {0}
};

#undef off
#undef data
#define data(x) (int)(size_t)&((pManufacturingPage5_t)0)->x, sizeof(((pManufacturingPage5_t)0)->x)

ITEM manufacturing_page_5_items_25[] =
{
    {"Base_WWID_Low",       data(BaseWWID.Low),     0},
    {"Base_WWID_Hi",        data(BaseWWID.High),    0},
    {"MANUFACT_5_FLAGS",    data(Flags),            0},
    {0}
};

#define manufacturing_page_5_size_25 (int)(size_t)&((pManufacturingPage5_t)0)->Reserved3

ITEM manufacturing_page_5_items[] =
{
    {"Base_WWID_Low",           data(BaseWWID.Low),         0},
    {"Base_WWID_Hi",            data(BaseWWID.High),        0},
    {"MANUFACT_5_FLAGS",        data(Flags),                0},
    {"MAN_5_NUM_FORCE_WWID",    data(NumForceWWID),         0},
    {"MAN_5_RESERVED",          data(Reserved2),            OPT},
    {"RESERVED2",               data(Reserved2),            OPT},
    {"RESERVED3",               data(Reserved3),            OPT},
    {"RESERVED4",               data(Reserved4),            OPT},
    {"FORCE_WWID_0_LOW",        data(ForceWWID[0].Low),     0},
    {"FORCE_WWID_0_HI",         data(ForceWWID[0].High),    0},
    {"FORCE_WWID_1_LOW",        data(ForceWWID[1].Low),     0},
    {"FORCE_WWID_1_HI",         data(ForceWWID[1].High),    0},
    {"FORCE_WWID_2_LOW",        data(ForceWWID[2].Low),     0},
    {"FORCE_WWID_2_HI",         data(ForceWWID[2].High),    0},
    {"FORCE_WWID_3_LOW",        data(ForceWWID[3].Low),     0},
    {"FORCE_WWID_3_HI",         data(ForceWWID[3].High),    0},
    {"FORCE_WWID_4_LOW",        data(ForceWWID[4].Low),     0},
    {"FORCE_WWID_4_HI",         data(ForceWWID[4].High),    0},
    {"FORCE_WWID_5_LOW",        data(ForceWWID[5].Low),     0},
    {"FORCE_WWID_5_HI",         data(ForceWWID[5].High),    0},
    {"FORCE_WWID_6_LOW",        data(ForceWWID[6].Low),     0},
    {"FORCE_WWID_6_HI",         data(ForceWWID[6].High),    0},
    {"FORCE_WWID_7_LOW",        data(ForceWWID[7].Low),     0},
    {"FORCE_WWID_7_HI",         data(ForceWWID[7].High),    0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pMpi2ManufacturingPage5_t)0)->x, sizeof(((pMpi2ManufacturingPage5_t)0)->x)

ITEM manufacturing_page_5_items2[] =
{
    {"NUM_PHYS",            data(NumPhys),                  0},
    {"RESERVED1",           data(Reserved1),                OPT},
    {"RESERVED2",           data(Reserved2),                OPT},
    {"RESERVED3",           data(Reserved3),                OPT},
    {"RESERVED4",           data(Reserved4),                OPT},
    {"PHY0_WWID_LOW",       data(Phy[0].WWID.Low),          0},
    {"PHY0_WWID_HI",        data(Phy[0].WWID.High),         0},
    {"PHY0_DEVICENAME_LOW", data(Phy[0].DeviceName.Low),    0},
    {"PHY0_DEVICENAME_HI",  data(Phy[0].DeviceName.High),   0},
    {"PHY1_WWID_LOW",       data(Phy[1].WWID.Low),          0},
    {"PHY1_WWID_HI",        data(Phy[1].WWID.High),         0},
    {"PHY1_DEVICENAME_LOW", data(Phy[1].DeviceName.Low),    0},
    {"PHY1_DEVICENAME_HI",  data(Phy[1].DeviceName.High),   0},
    {"PHY2_WWID_LOW",       data(Phy[2].WWID.Low),          0},
    {"PHY2_WWID_HI",        data(Phy[2].WWID.High),         0},
    {"PHY2_DEVICENAME_LOW", data(Phy[2].DeviceName.Low),    0},
    {"PHY2_DEVICENAME_HI",  data(Phy[2].DeviceName.High),   0},
    {"PHY3_WWID_LOW",       data(Phy[3].WWID.Low),          0},
    {"PHY3_WWID_HI",        data(Phy[3].WWID.High),         0},
    {"PHY3_DEVICENAME_LOW", data(Phy[3].DeviceName.Low),    0},
    {"PHY3_DEVICENAME_HI",  data(Phy[3].DeviceName.High),   0},
    {"PHY4_WWID_LOW",       data(Phy[4].WWID.Low),          0},
    {"PHY4_WWID_HI",        data(Phy[4].WWID.High),         0},
    {"PHY4_DEVICENAME_LOW", data(Phy[4].DeviceName.Low),    0},
    {"PHY4_DEVICENAME_HI",  data(Phy[4].DeviceName.High),   0},
    {"PHY5_WWID_LOW",       data(Phy[5].WWID.Low),          0},
    {"PHY5_WWID_HI",        data(Phy[5].WWID.High),         0},
    {"PHY5_DEVICENAME_LOW", data(Phy[5].DeviceName.Low),    0},
    {"PHY5_DEVICENAME_HI",  data(Phy[5].DeviceName.High),   0},
    {"PHY6_WWID_LOW",       data(Phy[6].WWID.Low),          0},
    {"PHY6_WWID_HI",        data(Phy[6].WWID.High),         0},
    {"PHY6_DEVICENAME_LOW", data(Phy[6].DeviceName.Low),    0},
    {"PHY6_DEVICENAME_HI",  data(Phy[6].DeviceName.High),   0},
    {"PHY7_WWID_LOW",       data(Phy[7].WWID.Low),          0},
    {"PHY7_WWID_HI",        data(Phy[7].WWID.High),         0},
    {"PHY7_DEVICENAME_LOW", data(Phy[7].DeviceName.Low),    0},
    {"PHY7_DEVICENAME_HI",  data(Phy[7].DeviceName.High),   0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pManufacturingPage6_SAS2_t)0)->x, sizeof(((pManufacturingPage6_SAS2_t)0)->x)

ITEM manufacturing_page_6_items2[] =
{
    {"NUM_GPIO",            data(NumGPIO),                          0},
    {"RESERVED1_0",         data(Reserved1[0]),                     OPT},
    {"RESERVED1_1",         data(Reserved1[1]),                     OPT},
    {"RESERVED1_2",         data(Reserved1[2]),                     OPT},
    {"RESERVED2",           data(Reserved2),                        OPT},
    {"RESERVED3",           data(Reserved3),                        OPT},
    {"GPIO_0_FUNC_CODE",    data(GPIODefinition[0].FunctionCode),   0},
    {"GPIO_0_FLAGS",        data(GPIODefinition[0].Flags),  0},
    {"GPIO_0_PARAM1",       data(GPIODefinition[0].Param1), 0},
    {"GPIO_0_PARAM2",       data(GPIODefinition[0].Param2), 0},
    {"GPIO_0_PARAM3",       data(GPIODefinition[0].Param3), 0},
    {"GPIO_1_FUNC_CODE",    data(GPIODefinition[1].FunctionCode),   0},
    {"GPIO_1_FLAGS",        data(GPIODefinition[1].Flags),  0},
    {"GPIO_1_PARAM1",       data(GPIODefinition[1].Param1), 0},
    {"GPIO_1_PARAM2",       data(GPIODefinition[1].Param2), 0},
    {"GPIO_1_PARAM3",       data(GPIODefinition[1].Param3), 0},
    {"GPIO_2_FUNC_CODE",    data(GPIODefinition[2].FunctionCode),   0},
    {"GPIO_2_FLAGS",        data(GPIODefinition[2].Flags),  0},
    {"GPIO_2_PARAM1",       data(GPIODefinition[2].Param1), 0},
    {"GPIO_2_PARAM2",       data(GPIODefinition[2].Param2), 0},
    {"GPIO_2_PARAM3",       data(GPIODefinition[2].Param3), 0},
    {"GPIO_3_FUNC_CODE",    data(GPIODefinition[3].FunctionCode),   0},
    {"GPIO_3_FLAGS",        data(GPIODefinition[3].Flags),  0},
    {"GPIO_3_PARAM1",       data(GPIODefinition[3].Param1), 0},
    {"GPIO_3_PARAM2",       data(GPIODefinition[3].Param2), 0},
    {"GPIO_3_PARAM3",       data(GPIODefinition[3].Param3), 0},
    {"GPIO_4_FUNC_CODE",    data(GPIODefinition[4].FunctionCode),   0},
    {"GPIO_4_FLAGS",        data(GPIODefinition[4].Flags),  0},
    {"GPIO_4_PARAM1",       data(GPIODefinition[4].Param1), 0},
    {"GPIO_4_PARAM2",       data(GPIODefinition[4].Param2), 0},
    {"GPIO_4_PARAM3",       data(GPIODefinition[4].Param3), 0},
    {"GPIO_5_FUNC_CODE",    data(GPIODefinition[5].FunctionCode),   0},
    {"GPIO_5_FLAGS",        data(GPIODefinition[5].Flags),  0},
    {"GPIO_5_PARAM1",       data(GPIODefinition[5].Param1), 0},
    {"GPIO_5_PARAM2",       data(GPIODefinition[5].Param2), 0},
    {"GPIO_5_PARAM3",       data(GPIODefinition[5].Param3), 0},
    {"GPIO_6_FUNC_CODE",    data(GPIODefinition[6].FunctionCode),   0},
    {"GPIO_6_FLAGS",        data(GPIODefinition[6].Flags),  0},
    {"GPIO_6_PARAM1",       data(GPIODefinition[6].Param1), 0},
    {"GPIO_6_PARAM2",       data(GPIODefinition[6].Param2), 0},
    {"GPIO_6_PARAM3",       data(GPIODefinition[6].Param3), 0},
    {"GPIO_7_FUNC_CODE",    data(GPIODefinition[7].FunctionCode),   0},
    {"GPIO_7_FLAGS",        data(GPIODefinition[7].Flags),  0},
    {"GPIO_7_PARAM1",       data(GPIODefinition[7].Param1), 0},
    {"GPIO_7_PARAM2",       data(GPIODefinition[7].Param2), 0},
    {"GPIO_7_PARAM3",       data(GPIODefinition[7].Param3), 0},
    {"GPIO_8_FUNC_CODE",    data(GPIODefinition[8].FunctionCode),   0},
    {"GPIO_8_FLAGS",        data(GPIODefinition[8].Flags),  0},
    {"GPIO_8_PARAM1",       data(GPIODefinition[8].Param1), 0},
    {"GPIO_8_PARAM2",       data(GPIODefinition[8].Param2), 0},
    {"GPIO_8_PARAM3",       data(GPIODefinition[8].Param3), 0},
    {"GPIO_9_FUNC_CODE",    data(GPIODefinition[9].FunctionCode),   0},
    {"GPIO_9_FLAGS",        data(GPIODefinition[9].Flags),  0},
    {"GPIO_9_PARAM1",       data(GPIODefinition[9].Param1), 0},
    {"GPIO_9_PARAM2",       data(GPIODefinition[9].Param2), 0},
    {"GPIO_9_PARAM3",       data(GPIODefinition[9].Param3), 0},
    {"GPIO_10_FUNC_CODE",   data(GPIODefinition[10].FunctionCode),  0},
    {"GPIO_10_FLAGS",       data(GPIODefinition[10].Flags), 0},
    {"GPIO_10_PARAM1",      data(GPIODefinition[10].Param1),    0},
    {"GPIO_10_PARAM2",      data(GPIODefinition[10].Param2),    0},
    {"GPIO_10_PARAM3",      data(GPIODefinition[10].Param3),    0},
    {"GPIO_11_FUNC_CODE",   data(GPIODefinition[11].FunctionCode),  0},
    {"GPIO_11_FLAGS",       data(GPIODefinition[11].Flags), 0},
    {"GPIO_11_PARAM1",      data(GPIODefinition[11].Param1),    0},
    {"GPIO_11_PARAM2",      data(GPIODefinition[11].Param2),    0},
    {"GPIO_11_PARAM3",      data(GPIODefinition[11].Param3),    0},
    {"GPIO_12_FUNC_CODE",   data(GPIODefinition[12].FunctionCode),  0},
    {"GPIO_12_FLAGS",       data(GPIODefinition[12].Flags), 0},
    {"GPIO_12_PARAM1",      data(GPIODefinition[12].Param1),    0},
    {"GPIO_12_PARAM2",      data(GPIODefinition[12].Param2),    0},
    {"GPIO_12_PARAM3",      data(GPIODefinition[12].Param3),    0},
    {"GPIO_13_FUNC_CODE",   data(GPIODefinition[13].FunctionCode),  0},
    {"GPIO_13_FLAGS",       data(GPIODefinition[13].Flags), 0},
    {"GPIO_13_PARAM1",      data(GPIODefinition[13].Param1),    0},
    {"GPIO_13_PARAM2",      data(GPIODefinition[13].Param2),    0},
    {"GPIO_13_PARAM3",      data(GPIODefinition[13].Param3),    0},
    {"GPIO_14_FUNC_CODE",   data(GPIODefinition[14].FunctionCode),  0},
    {"GPIO_14_FLAGS",       data(GPIODefinition[14].Flags), 0},
    {"GPIO_14_PARAM1",      data(GPIODefinition[14].Param1),    0},
    {"GPIO_14_PARAM2",      data(GPIODefinition[14].Param2),    0},
    {"GPIO_14_PARAM3",      data(GPIODefinition[14].Param3),    0},
    {"GPIO_15_FUNC_CODE",   data(GPIODefinition[15].FunctionCode),  0},
    {"GPIO_15_FLAGS",       data(GPIODefinition[15].Flags), 0},
    {"GPIO_15_PARAM1",      data(GPIODefinition[15].Param1),    0},
    {"GPIO_15_PARAM2",      data(GPIODefinition[15].Param2),    0},
    {"GPIO_15_PARAM3",      data(GPIODefinition[15].Param3),    0},
    {"GPIO_16_FUNC_CODE",   data(GPIODefinition[16].FunctionCode),  0},
    {"GPIO_16_FLAGS",       data(GPIODefinition[16].Flags), 0},
    {"GPIO_16_PARAM1",      data(GPIODefinition[16].Param1),    0},
    {"GPIO_16_PARAM2",      data(GPIODefinition[16].Param2),    0},
    {"GPIO_16_PARAM3",      data(GPIODefinition[16].Param3),    0},
    {"GPIO_17_FUNC_CODE",   data(GPIODefinition[17].FunctionCode),  0},
    {"GPIO_17_FLAGS",       data(GPIODefinition[17].Flags), 0},
    {"GPIO_17_PARAM1",      data(GPIODefinition[17].Param1),    0},
    {"GPIO_17_PARAM2",      data(GPIODefinition[17].Param2),    0},
    {"GPIO_17_PARAM3",      data(GPIODefinition[17].Param3),    0},
    {"GPIO_18_FUNC_CODE",   data(GPIODefinition[18].FunctionCode),  0},
    {"GPIO_18_FLAGS",       data(GPIODefinition[18].Flags), 0},
    {"GPIO_18_PARAM1",      data(GPIODefinition[18].Param1),    0},
    {"GPIO_18_PARAM2",      data(GPIODefinition[18].Param2),    0},
    {"GPIO_18_PARAM3",      data(GPIODefinition[18].Param3),    0},
    {"GPIO_19_FUNC_CODE",   data(GPIODefinition[19].FunctionCode),  0},
    {"GPIO_19_FLAGS",       data(GPIODefinition[19].Flags), 0},
    {"GPIO_19_PARAM1",      data(GPIODefinition[19].Param1),    0},
    {"GPIO_19_PARAM2",      data(GPIODefinition[19].Param2),    0},
    {"GPIO_19_PARAM3",      data(GPIODefinition[19].Param3),    0},
    {"GPIO_20_FUNC_CODE",   data(GPIODefinition[20].FunctionCode),  0},
    {"GPIO_20_FLAGS",       data(GPIODefinition[20].Flags), 0},
    {"GPIO_20_PARAM1",      data(GPIODefinition[20].Param1),    0},
    {"GPIO_20_PARAM2",      data(GPIODefinition[20].Param2),    0},
    {"GPIO_20_PARAM3",      data(GPIODefinition[20].Param3),    0},
    {"GPIO_21_FUNC_CODE",   data(GPIODefinition[21].FunctionCode),  0},
    {"GPIO_21_FLAGS",       data(GPIODefinition[21].Flags), 0},
    {"GPIO_21_PARAM1",      data(GPIODefinition[21].Param1),    0},
    {"GPIO_21_PARAM2",      data(GPIODefinition[21].Param2),    0},
    {"GPIO_21_PARAM3",      data(GPIODefinition[21].Param3),    0},
    {"GPIO_22_FUNC_CODE",   data(GPIODefinition[22].FunctionCode),  0},
    {"GPIO_22_FLAGS",       data(GPIODefinition[22].Flags), 0},
    {"GPIO_22_PARAM1",      data(GPIODefinition[22].Param1),    0},
    {"GPIO_22_PARAM2",      data(GPIODefinition[22].Param2),    0},
    {"GPIO_22_PARAM3",      data(GPIODefinition[22].Param3),    0},
    {"GPIO_23_FUNC_CODE",   data(GPIODefinition[23].FunctionCode),  0},
    {"GPIO_23_FLAGS",       data(GPIODefinition[23].Flags), 0},
    {"GPIO_23_PARAM1",      data(GPIODefinition[23].Param1),    0},
    {"GPIO_23_PARAM2",      data(GPIODefinition[23].Param2),    0},
    {"GPIO_23_PARAM3",      data(GPIODefinition[23].Param3),    0},
    {"GPIO_24_FUNC_CODE",   data(GPIODefinition[24].FunctionCode),  0},
    {"GPIO_24_FLAGS",       data(GPIODefinition[24].Flags), 0},
    {"GPIO_24_PARAM1",      data(GPIODefinition[24].Param1),    0},
    {"GPIO_24_PARAM2",      data(GPIODefinition[24].Param2),    0},
    {"GPIO_24_PARAM3",      data(GPIODefinition[24].Param3),    0},
    {"GPIO_25_FUNC_CODE",   data(GPIODefinition[25].FunctionCode),  0},
    {"GPIO_25_FLAGS",       data(GPIODefinition[25].Flags), 0},
    {"GPIO_25_PARAM1",      data(GPIODefinition[25].Param1),    0},
    {"GPIO_25_PARAM2",      data(GPIODefinition[25].Param2),    0},
    {"GPIO_25_PARAM3",      data(GPIODefinition[25].Param3),    0},
    {"GPIO_26_FUNC_CODE",   data(GPIODefinition[26].FunctionCode),  0},
    {"GPIO_26_FLAGS",       data(GPIODefinition[26].Flags), 0},
    {"GPIO_26_PARAM1",      data(GPIODefinition[26].Param1),    0},
    {"GPIO_26_PARAM2",      data(GPIODefinition[26].Param2),    0},
    {"GPIO_26_PARAM3",      data(GPIODefinition[26].Param3),    0},
    {"GPIO_27_FUNC_CODE",   data(GPIODefinition[27].FunctionCode),  0},
    {"GPIO_27_FLAGS",       data(GPIODefinition[27].Flags), 0},
    {"GPIO_27_PARAM1",      data(GPIODefinition[27].Param1),    0},
    {"GPIO_27_PARAM2",      data(GPIODefinition[27].Param2),    0},
    {"GPIO_27_PARAM3",      data(GPIODefinition[27].Param3),    0},
    {"GPIO_28_FUNC_CODE",   data(GPIODefinition[28].FunctionCode),  0},
    {"GPIO_28_FLAGS",       data(GPIODefinition[28].Flags), 0},
    {"GPIO_28_PARAM1",      data(GPIODefinition[28].Param1),    0},
    {"GPIO_28_PARAM2",      data(GPIODefinition[28].Param2),    0},
    {"GPIO_28_PARAM3",      data(GPIODefinition[28].Param3),    0},
    {"GPIO_29_FUNC_CODE",   data(GPIODefinition[29].FunctionCode),  0},
    {"GPIO_29_FLAGS",       data(GPIODefinition[29].Flags), 0},
    {"GPIO_29_PARAM1",      data(GPIODefinition[29].Param1),    0},
    {"GPIO_29_PARAM2",      data(GPIODefinition[29].Param2),    0},
    {"GPIO_29_PARAM3",      data(GPIODefinition[29].Param3),    0},
    {"GPIO_30_FUNC_CODE",   data(GPIODefinition[30].FunctionCode),  0},
    {"GPIO_30_FLAGS",       data(GPIODefinition[30].Flags), 0},
    {"GPIO_30_PARAM1",      data(GPIODefinition[30].Param1),    0},
    {"GPIO_30_PARAM2",      data(GPIODefinition[30].Param2),    0},
    {"GPIO_30_PARAM3",      data(GPIODefinition[30].Param3),    0},
    {"GPIO_31_FUNC_CODE",   data(GPIODefinition[31].FunctionCode),  0},
    {"GPIO_31_FLAGS",       data(GPIODefinition[31].Flags), 0},
    {"GPIO_31_PARAM1",      data(GPIODefinition[31].Param1),    0},
    {"GPIO_31_PARAM2",      data(GPIODefinition[31].Param2),    0},
    {"GPIO_31_PARAM3",      data(GPIODefinition[31].Param3),    0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pManufacturingPage7_t)0)->x, sizeof(((pManufacturingPage7_t)0)->x)

ITEM manufacturing_page_7_items[] =
{
    {"RESERVED1",               data(Reserved1),                    OPT},
    {"RESERVED2",               data(Reserved2),                    OPT},
    {"MP7_FLAGS",               data(Flags),                        0},
    {"ENCLOSURE_NAME",          data(EnclosureName),                STR},
    {"NUM_PHYS",                data(NumPhys),                      0},
    {"RESERVED3",               data(Reserved3),                    OPT},
    {"RESERVED4",               data(Reserved4),                    OPT},
    {"CONN_INFO_0_PINOUT",      data(ConnectorInfo[0].Pinout),      0},
    {"CONN_INFO_0_CONNECTOR",   data(ConnectorInfo[0].Connector),   STR},
    {"CONN_INFO_0_LOCATION",    data(ConnectorInfo[0].Location),    0},
    {"CONN_INFO_0_RESERVED1",   data(ConnectorInfo[0].Reserved1),   OPT},
    {"CONN_INFO_0_SLOT",        data(ConnectorInfo[0].Slot),        0},
    {"CONN_INFO_0_RESERVED2",   data(ConnectorInfo[0].Reserved2),   OPT},
    {"CONN_INFO_1_PINOUT",      data(ConnectorInfo[1].Pinout),      0},
    {"CONN_INFO_1_CONNECTOR",   data(ConnectorInfo[1].Connector),   STR},
    {"CONN_INFO_1_LOCATION",    data(ConnectorInfo[1].Location),    0},
    {"CONN_INFO_1_RESERVED1",   data(ConnectorInfo[1].Reserved1),   OPT},
    {"CONN_INFO_1_SLOT",        data(ConnectorInfo[1].Slot),        0},
    {"CONN_INFO_1_RESERVED2",   data(ConnectorInfo[1].Reserved2),   OPT},
    {"CONN_INFO_2_PINOUT",      data(ConnectorInfo[2].Pinout),      0},
    {"CONN_INFO_2_CONNECTOR",   data(ConnectorInfo[2].Connector),   STR},
    {"CONN_INFO_2_LOCATION",    data(ConnectorInfo[2].Location),    0},
    {"CONN_INFO_2_RESERVED1",   data(ConnectorInfo[2].Reserved1),   OPT},
    {"CONN_INFO_2_SLOT",        data(ConnectorInfo[2].Slot),        0},
    {"CONN_INFO_2_RESERVED2",   data(ConnectorInfo[2].Reserved2),   OPT},
    {"CONN_INFO_3_PINOUT",      data(ConnectorInfo[3].Pinout),      0},
    {"CONN_INFO_3_CONNECTOR",   data(ConnectorInfo[3].Connector),   STR},
    {"CONN_INFO_3_LOCATION",    data(ConnectorInfo[3].Location),    0},
    {"CONN_INFO_3_RESERVED1",   data(ConnectorInfo[3].Reserved1),   OPT},
    {"CONN_INFO_3_SLOT",        data(ConnectorInfo[3].Slot),        0},
    {"CONN_INFO_3_RESERVED2",   data(ConnectorInfo[3].Reserved2),   OPT},
    {"CONN_INFO_4_PINOUT",      data(ConnectorInfo[4].Pinout),      0},
    {"CONN_INFO_4_CONNECTOR",   data(ConnectorInfo[4].Connector),   STR},
    {"CONN_INFO_4_LOCATION",    data(ConnectorInfo[4].Location),    0},
    {"CONN_INFO_4_RESERVED1",   data(ConnectorInfo[4].Reserved1),   OPT},
    {"CONN_INFO_4_SLOT",        data(ConnectorInfo[4].Slot),        0},
    {"CONN_INFO_4_RESERVED2",   data(ConnectorInfo[4].Reserved2),   OPT},
    {"CONN_INFO_5_PINOUT",      data(ConnectorInfo[5].Pinout),      0},
    {"CONN_INFO_5_CONNECTOR",   data(ConnectorInfo[5].Connector),   STR},
    {"CONN_INFO_5_LOCATION",    data(ConnectorInfo[5].Location),    0},
    {"CONN_INFO_5_RESERVED1",   data(ConnectorInfo[5].Reserved1),   OPT},
    {"CONN_INFO_5_SLOT",        data(ConnectorInfo[5].Slot),        0},
    {"CONN_INFO_5_RESERVED2",   data(ConnectorInfo[5].Reserved2),   OPT},
    {"CONN_INFO_6_PINOUT",      data(ConnectorInfo[6].Pinout),      0},
    {"CONN_INFO_6_CONNECTOR",   data(ConnectorInfo[6].Connector),   STR},
    {"CONN_INFO_6_LOCATION",    data(ConnectorInfo[6].Location),    0},
    {"CONN_INFO_6_RESERVED1",   data(ConnectorInfo[6].Reserved1),   OPT},
    {"CONN_INFO_6_SLOT",        data(ConnectorInfo[6].Slot),        0},
    {"CONN_INFO_6_RESERVED2",   data(ConnectorInfo[6].Reserved2),   OPT},
    {"CONN_INFO_7_PINOUT",      data(ConnectorInfo[7].Pinout),      0},
    {"CONN_INFO_7_CONNECTOR",   data(ConnectorInfo[7].Connector),   STR},
    {"CONN_INFO_7_LOCATION",    data(ConnectorInfo[7].Location),    0},
    {"CONN_INFO_7_RESERVED1",   data(ConnectorInfo[7].Reserved1),   OPT},
    {"CONN_INFO_7_SLOT",        data(ConnectorInfo[7].Slot),        0},
    {"CONN_INFO_7_RESERVED2",   data(ConnectorInfo[7].Reserved2),   OPT},
    {"CONN_INFO_8_PINOUT",      data(ConnectorInfo[8].Pinout),      OPT},
    {"CONN_INFO_8_CONNECTOR",   data(ConnectorInfo[8].Connector),   STR | OPT},
    {"CONN_INFO_8_LOCATION",    data(ConnectorInfo[8].Location),    OPT},
    {"CONN_INFO_8_RESERVED1",   data(ConnectorInfo[8].Reserved1),   OPT},
    {"CONN_INFO_8_SLOT",        data(ConnectorInfo[8].Slot),        OPT},
    {"CONN_INFO_8_RESERVED2",   data(ConnectorInfo[8].Reserved2),   OPT},
    {"CONN_INFO_9_PINOUT",      data(ConnectorInfo[9].Pinout),      OPT},
    {"CONN_INFO_9_CONNECTOR",   data(ConnectorInfo[9].Connector),   STR | OPT},
    {"CONN_INFO_9_LOCATION",    data(ConnectorInfo[9].Location),    OPT},
    {"CONN_INFO_9_RESERVED1",   data(ConnectorInfo[9].Reserved1),   OPT},
    {"CONN_INFO_9_SLOT",        data(ConnectorInfo[9].Slot),        OPT},
    {"CONN_INFO_9_RESERVED2",   data(ConnectorInfo[9].Reserved2),   OPT},
    {"CONN_INFO_10_PINOUT",     data(ConnectorInfo[10].Pinout),     OPT},
    {"CONN_INFO_10_CONNECTOR",  data(ConnectorInfo[10].Connector),  STR | OPT},
    {"CONN_INFO_10_LOCATION",   data(ConnectorInfo[10].Location),   OPT},
    {"CONN_INFO_10_RESERVED1",  data(ConnectorInfo[10].Reserved1),  OPT},
    {"CONN_INFO_10_SLOT",       data(ConnectorInfo[10].Slot),       OPT},
    {"CONN_INFO_10_RESERVED2",  data(ConnectorInfo[10].Reserved2),  OPT},
    {"CONN_INFO_11_PINOUT",     data(ConnectorInfo[11].Pinout),     OPT},
    {"CONN_INFO_11_CONNECTOR",  data(ConnectorInfo[11].Connector),  STR | OPT},
    {"CONN_INFO_11_LOCATION",   data(ConnectorInfo[11].Location),   OPT},
    {"CONN_INFO_11_RESERVED1",  data(ConnectorInfo[11].Reserved1),  OPT},
    {"CONN_INFO_11_SLOT",       data(ConnectorInfo[11].Slot),       OPT},
    {"CONN_INFO_11_RESERVED2",  data(ConnectorInfo[11].Reserved2),  OPT},
    {"CONN_INFO_12_PINOUT",     data(ConnectorInfo[12].Pinout),     OPT},
    {"CONN_INFO_12_CONNECTOR",  data(ConnectorInfo[12].Connector),  STR | OPT},
    {"CONN_INFO_12_LOCATION",   data(ConnectorInfo[12].Location),   OPT},
    {"CONN_INFO_12_RESERVED1",  data(ConnectorInfo[12].Reserved1),  OPT},
    {"CONN_INFO_12_SLOT",       data(ConnectorInfo[12].Slot),       OPT},
    {"CONN_INFO_12_RESERVED2",  data(ConnectorInfo[12].Reserved2),  OPT},
    {"CONN_INFO_13_PINOUT",     data(ConnectorInfo[13].Pinout),     OPT},
    {"CONN_INFO_13_CONNECTOR",  data(ConnectorInfo[13].Connector),  STR | OPT},
    {"CONN_INFO_13_LOCATION",   data(ConnectorInfo[13].Location),   OPT},
    {"CONN_INFO_13_RESERVED1",  data(ConnectorInfo[13].Reserved1),  OPT},
    {"CONN_INFO_13_SLOT",       data(ConnectorInfo[13].Slot),       OPT},
    {"CONN_INFO_13_RESERVED2",  data(ConnectorInfo[13].Reserved2),  OPT},
    {"CONN_INFO_14_PINOUT",     data(ConnectorInfo[14].Pinout),     OPT},
    {"CONN_INFO_14_CONNECTOR",  data(ConnectorInfo[14].Connector),  STR | OPT},
    {"CONN_INFO_14_LOCATION",   data(ConnectorInfo[14].Location),   OPT},
    {"CONN_INFO_14_RESERVED1",  data(ConnectorInfo[14].Reserved1),  OPT},
    {"CONN_INFO_14_SLOT",       data(ConnectorInfo[14].Slot),       OPT},
    {"CONN_INFO_14_RESERVED2",  data(ConnectorInfo[14].Reserved2),  OPT},
    {"CONN_INFO_15_PINOUT",     data(ConnectorInfo[15].Pinout),     OPT},
    {"CONN_INFO_15_CONNECTOR",  data(ConnectorInfo[15].Connector),  STR | OPT},
    {"CONN_INFO_15_LOCATION",   data(ConnectorInfo[15].Location),   OPT},
    {"CONN_INFO_15_RESERVED1",  data(ConnectorInfo[15].Reserved1),  OPT},
    {"CONN_INFO_15_SLOT",       data(ConnectorInfo[15].Slot),       OPT},
    {"CONN_INFO_15_RESERVED2",  data(ConnectorInfo[15].Reserved2),  OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pManufacturingPage8_t)0)->x, sizeof(((pManufacturingPage8_t)0)->x)

ITEM manufacturing_page_8_items2[] =
{
    {"PRODSPECIFICINFO",    data(ProductSpecificInfo),  0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pManufacturingPage9_SAS2_t)0)->x, sizeof(((pManufacturingPage9_SAS2_t)0)->x)

ITEM manufacturing_page_9_items2[] =
{
    {"MAX_ATTEMPTS",            data(MaxAttempts),                  0},
    {"NUM_RESOURCES",           data(NumResources),                 0},
    {"RESERVED1",               data(Reserved1),                    OPT},
    {"RESERVED2",               data(Reserved2),                    OPT},
    {"NUM_VFS_MAX",             data(ResourceArray[0].Maximum),     0},
    {"NUM_VFS_DEC",             data(ResourceArray[0].Decrement),   0},
    {"NUM_VFS_MIN",             data(ResourceArray[0].Minimum),     0},
    {"NUM_VFS_ACT",             data(ResourceArray[0].Actual),      0},
    {"NUM_VPS_MAX",             data(ResourceArray[1].Maximum),     0},
    {"NUM_VPS_DEC",             data(ResourceArray[1].Decrement),   0},
    {"NUM_VPS_MIN",             data(ResourceArray[1].Minimum),     0},
    {"NUM_VPS_ACT",             data(ResourceArray[1].Actual),      0},
    {"HOST_CRED_PER_VF_MAX",    data(ResourceArray[2].Maximum),     0},
    {"HOST_CRED_PER_VF_DEC",    data(ResourceArray[2].Decrement),   0},
    {"HOST_CRED_PER_VF_MIN",    data(ResourceArray[2].Minimum),     0},
    {"HOST_CRED_PER_VF_ACT",    data(ResourceArray[2].Actual),      0},
    {"HIPRI_QDEPTH_PER_VF_MAX", data(ResourceArray[3].Maximum),     0},
    {"HIPRI_QDEPTH_PER_VF_DEC", data(ResourceArray[3].Decrement),   0},
    {"HIPRI_QDEPTH_PER_VF_MIN", data(ResourceArray[3].Minimum),     0},
    {"HIPRI_QDEPTH_PER_VF_ACT", data(ResourceArray[3].Actual),      0},
    {"TARGETS_MAX",             data(ResourceArray[4].Maximum),     0},
    {"TARGETS_DEC",             data(ResourceArray[4].Decrement),   0},
    {"TARGETS_MIN",             data(ResourceArray[4].Minimum),     0},
    {"TARGETS_ACT",             data(ResourceArray[4].Actual),      0},
    {"INITIATORS_MAX",          data(ResourceArray[5].Maximum),     0},
    {"INITIATORS_DEC",          data(ResourceArray[5].Decrement),   0},
    {"INITIATORS_MIN",          data(ResourceArray[5].Minimum),     0},
    {"INITIATORS_ACT",          data(ResourceArray[5].Actual),      0},
    {"TGT_CMD_BUFS_PER_VP_MAX", data(ResourceArray[6].Maximum),     0},
    {"TGT_CMD_BUFS_PER_VP_DEC", data(ResourceArray[6].Decrement),   0},
    {"TGT_CMD_BUFS_PER_VP_MIN", data(ResourceArray[6].Minimum),     0},
    {"TGT_CMD_BUFS_PER_VP_ACT", data(ResourceArray[6].Actual),      0},
    {"EXPANDERS_MAX",           data(ResourceArray[7].Maximum),     0},
    {"EXPANDERS_DEC",           data(ResourceArray[7].Decrement),   0},
    {"EXPANDERS_MIN",           data(ResourceArray[7].Minimum),     0},
    {"EXPANDERS_ACT",           data(ResourceArray[7].Actual),      0},
    {"PHYS_MAX",                data(ResourceArray[8].Maximum),     0},
    {"PHYS_DEC",                data(ResourceArray[8].Decrement),   0},
    {"PHYS_MIN",                data(ResourceArray[8].Minimum),     0},
    {"PHYS_ACT",                data(ResourceArray[8].Actual),      0},
    {"ENCLOSURES_MAX",          data(ResourceArray[9].Maximum),     0},
    {"ENCLOSURES_DEC",          data(ResourceArray[9].Decrement),   0},
    {"ENCLOSURES_MIN",          data(ResourceArray[9].Minimum),     0},
    {"ENCLOSURES_ACT",          data(ResourceArray[9].Actual),      0},
    {"RING_BUF_SIZE_MAX",       data(ResourceArray[10].Maximum),    0},
    {"RING_BUF_SIZE_DEC",       data(ResourceArray[10].Decrement),  0},
    {"RING_BUF_SIZE_MIN",       data(ResourceArray[10].Minimum),    0},
    {"RING_BUF_SIZE_ACT",       data(ResourceArray[10].Actual),     0},
    {"IR_BUFFER_SIZE_MAX",      data(ResourceArray[11].Maximum),    0},
    {"IR_BUFFER_SIZE_DEC",      data(ResourceArray[11].Decrement),  0},
    {"IR_BUFFER_SIZE_MIN",      data(ResourceArray[11].Minimum),    0},
    {"IR_BUFFER_SIZE_ACT",      data(ResourceArray[11].Actual),     0},
    {"NUM_ROUTE_TABLE_ENT_MAX", data(ResourceArray[12].Maximum),    0},
    {"NUM_ROUTE_TABLE_ENT_DEC", data(ResourceArray[12].Decrement),  0},
    {"NUM_ROUTE_TABLE_ENT_MIN", data(ResourceArray[12].Minimum),    0},
    {"NUM_ROUTE_TABLE_ENT_ACT", data(ResourceArray[12].Actual),     0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pManufacturingPage10_t)0)->x, sizeof(((pManufacturingPage10_t)0)->x)

ITEM manufacturing_page_10_items2[] =
{
    {"PRODSPECIFICINFO",    data(ProductSpecificInfo),  0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pManufacturingPage11_SAS2_t)0)->x, sizeof(((pManufacturingPage11_SAS2_t)0)->x)

ITEM manufacturing_page_11_items2[] =
{
    {"FLASH_TIME",          data(FlashTime),                    0},
    {"NVS_TIME",            data(NVTime),                       0},
    {"FLAG",                data(Flag),                         0},
    {"RESERVED1",           data(Reserved1),                    OPT},
    {"HOT_PLUG_TIM_OUT",    data(HotPlugTimeout),               0},
    {"RESERVED_0",          data(Reserved[0]),                  OPT},
    {"RESERVED_1",          data(Reserved[1]),                  OPT},
    {"RESERVED_2",          data(Reserved[2]),                  OPT},
    {"MAX_CMD_FRAMES_0",    data(MaxCmdFrames[0]),              0},
    {"MAX_CMD_FRAMES_1",    data(MaxCmdFrames[1]),              0},
    {"MAX_CMD_FRAMES_2",    data(MaxCmdFrames[2]),              0},
    {"MAX_CMD_FRAMES_3",    data(MaxCmdFrames[3]),              0},
    {"SYS_REF_CLK",         data(SysRefClk),                    0},
    {"RESERVED2",           data(Reserved2),                    OPT},
    {"EXT_UART_CLK",        data(ExtUartClk),                   0},
    {"UART0_FLAGS",         data(UartSettings[0].Flags),        0},
    {"UART0_MORE_FLAGS",    data(UartSettings[0].MoreFlags),    0},
    {"UART0_TO",            data(UartSettings[0].TO),           0},
    {"UART0_BAUD_RATE",     data(UartSettings[0].BaudRate),     0},
    {"UART1_FLAGS",         data(UartSettings[1].Flags),        0},
    {"UART1_MORE_FLAGS",    data(UartSettings[1].MoreFlags),    0},
    {"UART1_TO",            data(UartSettings[1].TO),           0},
    {"UART1_BAUD_RATE",     data(UartSettings[1].BaudRate),     0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pManufacturingPage12_SAS2_t)0)->x, sizeof(((pManufacturingPage12_SAS2_t)0)->x)

ITEM manufacturing_page_12_items2[] =
{
    {"COMMON_FLAGS",            data(Flags),                            0},
    {"RESERVED1",               data(Reserved1),                        OPT},
    {"RESERVED2",               data(Reserved2),                        OPT},
    {"SGPIO_CFG1",              data(SGPIOCfg1),                        0},
    {"NUM_SGPIO",               data(NumSGPIO),                         0},
    {"SGPIO_TYPE",              data(SGPIOType),                        0},
    {"CLK_DIVIDE",              data(ClkDivide),                        0},
    {"DEFAULT_TX_CTRL",         data(DefaultTxCtrl),                    0},
    {"SGPIO_PAT_DEF0",          data(SGPIOPatDef0),                     0},
    {"SGPIO_PAT_DEF1",          data(SGPIOPatDef1),                     0},
    {"SGPIO_PAT_DEF2",          data(SGPIOPatDef2),                     0},
    {"SGPIO_PAT_DEF3",          data(SGPIOPatDef3),                     0},
    {"SGPIO_0_FLAGS",           data(SGPIOInfo[0].Flags),               0},
    {"SGPIO_0_BIT_ORDER_0",     data(SGPIOInfo[0].BitOrderSelect[0]),   0},
    {"SGPIO_0_BIT_ORDER_1",     data(SGPIOInfo[0].BitOrderSelect[1]),   0},
    {"SGPIO_0_BIT_ORDER_2",     data(SGPIOInfo[0].BitOrderSelect[2]),   0},
    {"SGPIO_0_BIT_ORDER_3",     data(SGPIOInfo[0].BitOrderSelect[3]),   0},
    {"SGPIO_0_BIT_ORDER_4",     data(SGPIOInfo[0].BitOrderSelect[4]),   0},
    {"SGPIO_0_BIT_ORDER_5",     data(SGPIOInfo[0].BitOrderSelect[5]),   0},
    {"SGPIO_0_BIT_ORDER_6",     data(SGPIOInfo[0].BitOrderSelect[6]),   0},
    {"SGPIO_0_BIT_ORDER_7",     data(SGPIOInfo[0].BitOrderSelect[7]),   0},
    {"SGPIO_0_BIT_ORDER_8",     data(SGPIOInfo[0].BitOrderSelect[8]),   0},
    {"SGPIO_0_BIT_ORDER_9",     data(SGPIOInfo[0].BitOrderSelect[9]),   0},
    {"SGPIO_0_BIT_ORDER_10",    data(SGPIOInfo[0].BitOrderSelect[10]),  0},
    {"SGPIO_0_BIT_ORDER_11",    data(SGPIOInfo[0].BitOrderSelect[11]),  0},
    {"SGPIO_1_FLAGS",           data(SGPIOInfo[1].Flags),               0},
    {"SGPIO_1_BIT_ORDER_0",     data(SGPIOInfo[1].BitOrderSelect[0]),   0},
    {"SGPIO_1_BIT_ORDER_1",     data(SGPIOInfo[1].BitOrderSelect[1]),   0},
    {"SGPIO_1_BIT_ORDER_2",     data(SGPIOInfo[1].BitOrderSelect[2]),   0},
    {"SGPIO_1_BIT_ORDER_3",     data(SGPIOInfo[1].BitOrderSelect[3]),   0},
    {"SGPIO_1_BIT_ORDER_4",     data(SGPIOInfo[1].BitOrderSelect[4]),   0},
    {"SGPIO_1_BIT_ORDER_5",     data(SGPIOInfo[1].BitOrderSelect[5]),   0},
    {"SGPIO_1_BIT_ORDER_6",     data(SGPIOInfo[1].BitOrderSelect[6]),   0},
    {"SGPIO_1_BIT_ORDER_7",     data(SGPIOInfo[1].BitOrderSelect[7]),   0},
    {"SGPIO_1_BIT_ORDER_8",     data(SGPIOInfo[1].BitOrderSelect[8]),   0},
    {"SGPIO_1_BIT_ORDER_9",     data(SGPIOInfo[1].BitOrderSelect[9]),   0},
    {"SGPIO_1_BIT_ORDER_10",    data(SGPIOInfo[1].BitOrderSelect[10]),  0},
    {"SGPIO_1_BIT_ORDER_11",    data(SGPIOInfo[1].BitOrderSelect[11]),  0},
    {"SGPIO_2_FLAGS",           data(SGPIOInfo[2].Flags),               0},
    {"SGPIO_2_BIT_ORDER_0",     data(SGPIOInfo[2].BitOrderSelect[0]),   0},
    {"SGPIO_2_BIT_ORDER_1",     data(SGPIOInfo[2].BitOrderSelect[1]),   0},
    {"SGPIO_2_BIT_ORDER_2",     data(SGPIOInfo[2].BitOrderSelect[2]),   0},
    {"SGPIO_2_BIT_ORDER_3",     data(SGPIOInfo[2].BitOrderSelect[3]),   0},
    {"SGPIO_2_BIT_ORDER_4",     data(SGPIOInfo[2].BitOrderSelect[4]),   0},
    {"SGPIO_2_BIT_ORDER_5",     data(SGPIOInfo[2].BitOrderSelect[5]),   0},
    {"SGPIO_2_BIT_ORDER_6",     data(SGPIOInfo[2].BitOrderSelect[6]),   0},
    {"SGPIO_2_BIT_ORDER_7",     data(SGPIOInfo[2].BitOrderSelect[7]),   0},
    {"SGPIO_2_BIT_ORDER_8",     data(SGPIOInfo[2].BitOrderSelect[8]),   0},
    {"SGPIO_2_BIT_ORDER_9",     data(SGPIOInfo[2].BitOrderSelect[9]),   0},
    {"SGPIO_2_BIT_ORDER_10",    data(SGPIOInfo[2].BitOrderSelect[10]),  0},
    {"SGPIO_2_BIT_ORDER_11",    data(SGPIOInfo[2].BitOrderSelect[11]),  0},
    {"SGPIO_3_FLAGS",           data(SGPIOInfo[3].Flags),               0},
    {"SGPIO_3_BIT_ORDER_0",     data(SGPIOInfo[3].BitOrderSelect[0]),   0},
    {"SGPIO_3_BIT_ORDER_1",     data(SGPIOInfo[3].BitOrderSelect[1]),   0},
    {"SGPIO_3_BIT_ORDER_2",     data(SGPIOInfo[3].BitOrderSelect[2]),   0},
    {"SGPIO_3_BIT_ORDER_3",     data(SGPIOInfo[3].BitOrderSelect[3]),   0},
    {"SGPIO_3_BIT_ORDER_4",     data(SGPIOInfo[3].BitOrderSelect[4]),   0},
    {"SGPIO_3_BIT_ORDER_5",     data(SGPIOInfo[3].BitOrderSelect[5]),   0},
    {"SGPIO_3_BIT_ORDER_6",     data(SGPIOInfo[3].BitOrderSelect[6]),   0},
    {"SGPIO_3_BIT_ORDER_7",     data(SGPIOInfo[3].BitOrderSelect[7]),   0},
    {"SGPIO_3_BIT_ORDER_8",     data(SGPIOInfo[3].BitOrderSelect[8]),   0},
    {"SGPIO_3_BIT_ORDER_9",     data(SGPIOInfo[3].BitOrderSelect[9]),   0},
    {"SGPIO_3_BIT_ORDER_10",    data(SGPIOInfo[3].BitOrderSelect[10]),  0},
    {"SGPIO_3_BIT_ORDER_11",    data(SGPIOInfo[3].BitOrderSelect[11]),  0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pManufacturingPage13_SAS2_t)0)->x, sizeof(((pManufacturingPage13_SAS2_t)0)->x)

ITEM manufacturing_page_13_items2[] =
{
    {"NUM_SGPIO_ENTRIES",   data(NumSgpioEntries),              0},
    {"RESERVED0",           data(Reserved0),                    OPT},
    {"RESERVED1",           data(Reserved1),                    OPT},
    {"RESERVED2",           data(Reserved2),                    OPT},
    {"SGPIO_0_MASK",        data(SGPIOData[0].Mask),            0},
    {"SGPIO_0_SLOT_STATUS", data(SGPIOData[0].SlotStatus),      0},
    {"SGPIO_0_TX_CTRL_0",   data(SGPIOData[0].TxControl[0]),    0},
    {"SGPIO_0_TX_CTRL_1",   data(SGPIOData[0].TxControl[1]),    0},
    {"SGPIO_0_TX_CTRL_2",   data(SGPIOData[0].TxControl[2]),    0},
    {"SGPIO_0_TX_CTRL_3",   data(SGPIOData[0].TxControl[3]),    0},
    {"SGPIO_1_MASK",        data(SGPIOData[0].Mask),            0},
    {"SGPIO_1_SLOT_STATUS", data(SGPIOData[1].SlotStatus),      0},
    {"SGPIO_1_TX_CTRL_0",   data(SGPIOData[1].TxControl[0]),    0},
    {"SGPIO_1_TX_CTRL_1",   data(SGPIOData[1].TxControl[1]),    0},
    {"SGPIO_1_TX_CTRL_2",   data(SGPIOData[1].TxControl[2]),    0},
    {"SGPIO_1_TX_CTRL_3",   data(SGPIOData[1].TxControl[3]),    0},
    {"SGPIO_2_MASK",        data(SGPIOData[1].Mask),            0},
    {"SGPIO_2_SLOT_STATUS", data(SGPIOData[2].SlotStatus),      0},
    {"SGPIO_2_TX_CTRL_0",   data(SGPIOData[2].TxControl[0]),    0},
    {"SGPIO_2_TX_CTRL_1",   data(SGPIOData[2].TxControl[1]),    0},
    {"SGPIO_2_TX_CTRL_2",   data(SGPIOData[2].TxControl[2]),    0},
    {"SGPIO_2_TX_CTRL_3",   data(SGPIOData[2].TxControl[3]),    0},
    {"SGPIO_3_MASK",        data(SGPIOData[3].Mask),            0},
    {"SGPIO_3_SLOT_STATUS", data(SGPIOData[3].SlotStatus),      0},
    {"SGPIO_3_TX_CTRL_0",   data(SGPIOData[3].TxControl[0]),    0},
    {"SGPIO_3_TX_CTRL_1",   data(SGPIOData[3].TxControl[1]),    0},
    {"SGPIO_3_TX_CTRL_2",   data(SGPIOData[3].TxControl[2]),    0},
    {"SGPIO_3_TX_CTRL_3",   data(SGPIOData[3].TxControl[3]),    0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pIOUnitPage0_t)0)->x, sizeof(((pIOUnitPage0_t)0)->x)

ITEM io_unit_page_0_items[] =
{
    {"UNIQUE_VALUE",        data(UniqueValue),      DUP},
    {"UNIQUE_VALUE_LOW",    data(UniqueValue.Low),  0},
    {"UNIQUE_VALUE_HI",     data(UniqueValue.High), OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pIOUnitPage1_t)0)->x, sizeof(((pIOUnitPage1_t)0)->x)

ITEM io_unit_page_1_items[] =
{
    {"IO_UNIT_PAGE_1_FLAGS",    data(Flags),    0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pIOUnitPage2_t)0)->x, sizeof(((pIOUnitPage2_t)0)->x)

ITEM io_unit_page_2_items[] =
{
    {"IO_UNIT_PAGE_2_FLAGS",    data(Flags),                                        0},
    {"BIOS_VERSION",            data(BiosVersion),                                  0},
    {"PCI_BUS_NUMBER",          data(AdapterOrder[0].PciBusNumber),                 DUP},
    {"PCI_BUS_NUMBER_0",        data(AdapterOrder[0].PciBusNumber),                 0},
    {"PCI_DEVICE_FUNCTION",     data(AdapterOrder[0].PciDeviceAndFunctionNumber),   DUP},
    {"PCI_DEVICE_FUNCTION_0",   data(AdapterOrder[0].PciDeviceAndFunctionNumber),   0},
    {"ADAPTER_FLAGS",           data(AdapterOrder[0].AdapterFlags),                 DUP},
    {"ADAPTER_FLAGS_0",         data(AdapterOrder[0].AdapterFlags),                 0},
    {"PCI_BUS_NUMBER_1",        data(AdapterOrder[1].PciBusNumber),                 OPT},
    {"PCI_DEVICE_FUNCTION_1",   data(AdapterOrder[1].PciDeviceAndFunctionNumber),   OPT},
    {"ADAPTER_FLAGS_1",         data(AdapterOrder[1].AdapterFlags),                 OPT},
    {"PCI_BUS_NUMBER_2",        data(AdapterOrder[2].PciBusNumber),                 OPT},
    {"PCI_DEVICE_FUNCTION_2",   data(AdapterOrder[2].PciDeviceAndFunctionNumber),   OPT},
    {"ADAPTER_FLAGS_2",         data(AdapterOrder[2].AdapterFlags),                 OPT},
    {"PCI_BUS_NUMBER_3",        data(AdapterOrder[3].PciBusNumber),                 OPT},
    {"PCI_DEVICE_FUNCTION_3",   data(AdapterOrder[3].PciDeviceAndFunctionNumber),   OPT},
    {"ADAPTER_FLAGS_3",         data(AdapterOrder[3].AdapterFlags),                 OPT},
    {"RESERVED1",               data(Reserved1),                                    OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pIOUnitPage3_t)0)->x, sizeof(((pIOUnitPage3_t)0)->x)

#define io_unit_page_3_size_25 (int)(size_t)&((pIOUnitPage3_t)0)->GPIOVal[8]

ITEM io_unit_page_3_items[] =
{
    {"GPIOCOUNT",   data(GPIOCount),    0},
    {"RESERVED1",   data(Reserved1),    OPT},
    {"RESERVED2",   data(Reserved2),    OPT},
    {"GPIOVAL_0",   data(GPIOVal[0]),   0},
    {"GPIOVAL_1",   data(GPIOVal[1]),   0},
    {"GPIOVAL_2",   data(GPIOVal[2]),   0},
    {"GPIOVAL_3",   data(GPIOVal[3]),   0},
    {"GPIOVAL_4",   data(GPIOVal[4]),   0},
    {"GPIOVAL_5",   data(GPIOVal[5]),   0},
    {"GPIOVAL_6",   data(GPIOVal[6]),   0},
    {"GPIOVAL_7",   data(GPIOVal[7]),   0},
    {"GPIOVAL_8",   data(GPIOVal[8]),   OPT},
    {"GPIOVAL_9",   data(GPIOVal[9]),   OPT},
    {"GPIOVAL_10",  data(GPIOVal[10]),  OPT},
    {"GPIOVAL_11",  data(GPIOVal[11]),  OPT},
    {"GPIOVAL_12",  data(GPIOVal[12]),  OPT},
    {"GPIOVAL_13",  data(GPIOVal[13]),  OPT},
    {"GPIOVAL_14",  data(GPIOVal[14]),  OPT},
    {"GPIOVAL_15",  data(GPIOVal[15]),  OPT},
    {"GPIOVAL_16",  data(GPIOVal[16]),  OPT},
    {"GPIOVAL_17",  data(GPIOVal[17]),  OPT},
    {"GPIOVAL_18",  data(GPIOVal[18]),  OPT},
    {"GPIOVAL_19",  data(GPIOVal[19]),  OPT},
    {"GPIOVAL_20",  data(GPIOVal[20]),  OPT},
    {"GPIOVAL_21",  data(GPIOVal[21]),  OPT},
    {"GPIOVAL_22",  data(GPIOVal[22]),  OPT},
    {"GPIOVAL_23",  data(GPIOVal[23]),  OPT},
    {"GPIOVAL_24",  data(GPIOVal[24]),  OPT},
    {"GPIOVAL_25",  data(GPIOVal[25]),  OPT},
    {"GPIOVAL_26",  data(GPIOVal[26]),  OPT},
    {"GPIOVAL_27",  data(GPIOVal[27]),  OPT},
    {"GPIOVAL_28",  data(GPIOVal[28]),  OPT},
    {"GPIOVAL_29",  data(GPIOVal[29]),  OPT},
    {"GPIOVAL_30",  data(GPIOVal[30]),  OPT},
    {"GPIOVAL_31",  data(GPIOVal[31]),  OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pIOUnitPage4_t)0)->x, sizeof(((pIOUnitPage4_t)0)->x)

ITEM io_unit_page_4_items[] =
{
    {"IOUNIT_4_RESERVED1",  data(Reserved1),                    OPT},
    {"FWIMAGE_FLAGS",       data(FWImageSGE.FlagsLength),       0},
    {"FWIMAGE_64HIGH",      data(FWImageSGE.u.Address64.High),  0},
    {"FWIMAGE_64LOW",       data(FWImageSGE.u.Address64.Low),   0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pIOCPage0_t)0)->x, sizeof(((pIOCPage0_t)0)->x)

ITEM ioc_page_0_items[] =
{
    {"TOTAL_NV_STORE",  data(TotalNVStore),         0},
    {"FREE_NV_STORE",   data(FreeNVStore),          0},
    {"VENDOR_ID",       data(VendorID),             0},
    {"DEVICE_ID",       data(DeviceID),             0},
    {"REVISION_ID",     data(RevisionID),           0},
    {"RESERVED",        data(Reserved),             OPT},
    {"RESERVED_0",      data(Reserved[0]),          OPT},
    {"RESERVED_1",      data(Reserved[1]),          OPT},
    {"RESERVED_2",      data(Reserved[2]),          OPT},
    {"CLASS_CODE",      data(ClassCode),            0},
    {"SS_VNDR_ID",      data(SubsystemVendorID),    0},
    {"SS_ID",           data(SubsystemID),          0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pIOCPage1_t)0)->x, sizeof(((pIOCPage1_t)0)->x)

ITEM ioc_page_1_items[] =
{
    {"IOC_PAGE_1_FLAGS",    data(Flags),                0},
    {"COALESCING_TIMEOUT",  data(CoalescingTimeout),    0},
    {"COALESCING_DEPTH",    data(CoalescingDepth),      0},
    {"PCI_SLOT_NUM",        data(PCISlotNum),           0},
    {"RESERVED",            data(Reserved),             OPT},
    {"RESERVED_0",          data(Reserved[0]),          OPT},
    {"RESERVED_1",          data(Reserved[1]),          OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pMpi2IOCPage1_t)0)->x, sizeof(((pMpi2IOCPage1_t)0)->x)

ITEM ioc_page_1_items2[] =
{
    {"IOC_PAGE_1_FLAGS",    data(Flags),                0},
    {"COALESCING_TIMEOUT",  data(CoalescingTimeout),    0},
    {"COALESCING_DEPTH",    data(CoalescingDepth),      0},
    {"PCI_SLOT_NUM",        data(PCISlotNum),           0},
    {"PCI_BUS_NUM",         data(PCIBusNum),            0},
    {"PCI_DOMAIN_SEGMENT",  data(PCIDomainSegment),     0},
    {"RESERVED1",           data(Reserved1),            OPT},
    {"RESERVED2",           data(Reserved2),            OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pIOCPage2_t)0)->x, sizeof(((pIOCPage2_t)0)->x)

ITEM ioc_page_2_items[] =
{
    {"CAP_FLAGS",               data(CapabilitiesFlags),                0},
    {"NUM_ACTIVE_VOLS",         data(NumActiveVolumes),                 0},
    {"MAX_VOLS",                data(MaxVolumes),                       0},
    {"NUM_ACTIVE_PHYS_DSKS",    data(NumActivePhysDisks),               0},
    {"MAX_PHYS_DSKS",           data(MaxPhysDisks),                     0},
    {"VOL_ID",                  data(RaidVolume[0].VolumeID),           0},
    {"VOL_BUS",                 data(RaidVolume[0].VolumeBus),          0},
    {"VOL_IOC",                 data(RaidVolume[0].VolumeIOC),          0},
    {"VOL_PAGE_NUM",            data(RaidVolume[0].VolumePageNumber),   0},
    {"VOL_TYPE",                data(RaidVolume[0].VolumeType),         0},
    {"FLAGS",                   data(RaidVolume[0].Flags),              0},
    {"RESERVED",                data(RaidVolume[0].Reserved3),          OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pIOCPage3_t)0)->x, sizeof(((pIOCPage3_t)0)->x)

ITEM ioc_page_3_items[] =
{
    {"NUM_PHYS_DSKS",   data(NumPhysDisks),             0},
    {"RESERVED1",       data(Reserved1),                OPT},
    {"RESERVED2",       data(Reserved2),                OPT},
    {"PHYS_DSK_ID",     data(PhysDisk[0].PhysDiskID),   0},
    {"PHYS_DSK_BUS",    data(PhysDisk[0].PhysDiskBus),  0},
    {"PHYS_DSK_IOC",    data(PhysDisk[0].PhysDiskIOC),  0},
    {"PHYS_DSE_NUM",    data(PhysDisk[0].PhysDiskNum),  DUP},
    {"PHYS_DSK_NUM",    data(PhysDisk[0].PhysDiskNum),  0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pIOCPage4_t)0)->x, sizeof(((pIOCPage4_t)0)->x)

ITEM ioc_page_4_items[] =
{
    {"ACTIVE_SEP",          data(ActiveSEP),            0},
    {"MAX_SEP",             data(MaxSEP),               0},
    {"RESERVED",            data(Reserved1),            OPT},
    {"RESERVED1",           data(Reserved1),            OPT},
    {"SEP_TARGET_ID",       data(SEP[0].SEPTargetID),   0},
    {"SEP_BUS",             data(SEP[0].SEPBus),        0},
    {"RESERVED",            data(SEP[0].Reserved),      OPT},
    {"IOC_4_SEP_RESERVED",  data(SEP[0].Reserved),      OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pIOCPage5_t)0)->x, sizeof(((pIOCPage5_t)0)->x)

ITEM ioc_page_5_items[] =
{
    {"RESERVED1",                   data(Reserved1),                OPT},
    {"NUM_HOT_SPARES",              data(NumHotSpares),             0},
    {"RESERVED2",                   data(Reserved2),                OPT},
    {"RESERVED3",                   data(Reserved3),                OPT},
    {"PHYS_DSK_NUM",                data(HotSpare[0].PhysDiskNum),  0},
    {"RESERVED",                    data(HotSpare[0].Reserved),     OPT},
    {"IOC_5_HOT_SPARE_RESERVED",    data(HotSpare[0].Reserved),     OPT},
    {"HOT_SPARE_POOL",              data(HotSpare[0].HotSparePool), 0},
    {"FLAGS",                       data(HotSpare[0].Flags),        0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pIOCPage6_t)0)->x, sizeof(((pIOCPage6_t)0)->x)

ITEM ioc_page_6_items[] =
{
    {"CAPABILITIES_FLAGS",              data(CapabilitiesFlags),            OPT},
    {"MAX_DRIVES_IS",                   data(MaxDrivesIS),                  OPT},
    {"MAX_DRIVES_IM",                   data(MaxDrivesIM),                  OPT},
    {"MAX_DRIVES_IME",                  data(MaxDrivesIME),                 OPT},
    {"RESERVED1",                       data(Reserved1),                    OPT},
    {"MIN_DRIVES_IS",                   data(MinDrivesIS),                  OPT},
    {"MIN_DRIVES_IM",                   data(MinDrivesIM),                  OPT},
    {"MIN_DRIVES_IME",                  data(MinDrivesIME),                 OPT},
    {"RESERVED2",                       data(Reserved2),                    OPT},
    {"MAX_GLOBAL_HOTSPARES",            data(MaxGlobalHotSpares),           OPT},
    {"RESERVED3",                       data(Reserved3),                    OPT},
    {"RESERVED4",                       data(Reserved4),                    OPT},
    {"RESERVED5",                       data(Reserved5),                    OPT},
    {"SUPPORTED_STRIPE_SIZE_MAP_IS",    data(SupportedStripeSizeMapIS),     OPT},
    {"SUPPORTED_STRIPE_SIZE_MAP_IME",   data(SupportedStripeSizeMapIME),    OPT},
    {"RESERVED6",                       data(Reserved6),                    OPT},
    {"METADATA_SIZE",                   data(MetadataSize),                 OPT},
    {"RESERVED7",                       data(Reserved7),                    OPT},
    {"RESERVED8",                       data(Reserved8),                    OPT},
    {"MAX_BAD_BLOCK_TABLE_ENTRIES",     data(MaxBadBlockTableEntries),      OPT},
    {"RESERVED9",                       data(Reserved9),                    OPT},
    {"IR_NVSRAM_USAGE",                 data(IRNvsramUsage),                OPT},
    {"RESERVED10",                      data(Reserved10),                   OPT},
    {"IR_NVSRAM_VERSION",               data(IRNvsramVersion),              OPT},
    {"RESERVED11",                      data(Reserved11),                   OPT},
    {"RESERVED12",                      data(Reserved12),                   OPT},
    {"ALL", sizeof(ConfigPageHeader_t), sizeof(IOCPage6_t) - sizeof(ConfigPageHeader_t), OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pMpi2IOCPage8_t)0)->x, sizeof(((pMpi2IOCPage8_t)0)->x)

ITEM ioc_page_8_items2[] =
{
    {"NUM_DEVS_PER_ENCL",       data(NumDevsPerEnclosure),      0},
    {"RESERVED1",               data(Reserved1),                OPT},
    {"RESERVED2",               data(Reserved2),                OPT},
    {"MAX_PERSIST_ENTRIES",     data(MaxPersistentEntries),     0},
    {"MAX_NUM_PHYS_MAPPED_IDS", data(MaxNumPhysicalMappedIDs),  0},
    {"FLAGS",                   data(Flags),                    0},
    {"RESERVED3",               data(Reserved3),                OPT},
    {"IR_VOLUME_MAPPING_FLAGS", data(IRVolumeMappingFlags),     0},
    {"RESERVED4",               data(Reserved4),                OPT},
    {"RESERVED5",               data(Reserved5),                OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pSasIOUnitPage0_t)0)->x, sizeof(((pSasIOUnitPage0_t)0)->x)

ITEM sas_io_unit_page_0_items[] =
{
    {"NVDATA_VER_DEFAULT",      data(NvdataVersionDefault),                 0},
    {"NVDATA_VER_PERSISTENT",   data(NvdataVersionPersistent),              0},
    {"NUM_PHYS",                data(NumPhys),                              0},
    {"RESERVED2",               data(Reserved2),                            OPT},
    {"RESERVED1",               data(Reserved3),                            OPT},
    {"RESERVED3",               data(Reserved3),                            OPT},
    {"PORT",                    data(PhyData[0].Port),                      0},
    {"PORT_FLGS",               data(PhyData[0].PortFlags),                 0},
    {"PHY_FLGS",                data(PhyData[0].PhyFlags),                  0},
    {"NEGOT_LINK_RATE",         data(PhyData[0].NegotiatedLinkRate),        0},
    {"CNTLR_PHY_DEV_INFO",      data(PhyData[0].ControllerPhyDeviceInfo),   0},
    {"ATTCH_DEV_HNDL",          data(PhyData[0].AttachedDeviceHandle),      0},
    {"CNTLR_DEV_HNDL",          data(PhyData[0].ControllerDevHandle),       0},
    {"SAS0_DISCOVERYSTATUS",    data(PhyData[0].DiscoveryStatus),           0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pSasIOUnitPage1_t)0)->x, sizeof(((pSasIOUnitPage1_t)0)->x)

ITEM sas_io_unit_page_1_items[] =
{
    {"CONTROLFLAGS",                                data(ControlFlags),                         0},
    {"MAXSATATARGETS",                              data(MaxNumSATATargets),                    0},
    {"ADDCONTROLFLAGS",                             data(AdditionalControlFlags),               0},
    {"RESERVED1",                                   data(Reserved1),                            OPT},
    {"NUM_PHYS",                                    data(NumPhys),                              0},
    {"SATAMAXQDEPTH",                               data(SATAMaxQDepth),                        0},
    {"REPDEVMISSINGDELAY",                          data(ReportDeviceMissingDelay),             0},
    {"IODEVMISSINGDELAY",                           data(IODeviceMissingDelay),                 0},
    {"Port",                                        data(PhyData[0].Port),                      DUP},
    {"SAS_IO_UNIT1_PHY_0_PORT",                     data(PhyData[0].Port),                      0},
    {"Port_Flgs",                                   data(PhyData[0].PortFlags),                 DUP},
    {"SAS_IO_UNIT1_PHY_0_PORT_FLGS",                data(PhyData[0].PortFlags),                 0},
    {"PHY_FLGS",                                    data(PhyData[0].PhyFlags),                  DUP},
    {"SAS_IO_UNIT1_PHY_0_PHY_FLGS",                 data(PhyData[0].PhyFlags),                  0},
    {"MIN_MAX_LINK_RATE",                           data(PhyData[0].MaxMinLinkRate),            DUP},
    {"SAS_IO_UNIT1_PHY_0_MIN_MAX_LINK_RATE",        data(PhyData[0].MaxMinLinkRate),            0},
    {"CNTLR_PHY_DEV_INFO",                          data(PhyData[0].ControllerPhyDeviceInfo),   DUP},
    {"SAS_IO_UNIT1_PHY_0_CNTLR_PHY_DEV_INFO",       data(PhyData[0].ControllerPhyDeviceInfo),   0},
    {"MAX_TARG_PORT_CONN_TIME",                     data(PhyData[0].MaxTargetPortConnectTime),  DUP},
    {"SAS_IO_UNIT1_PHY_0_MAX_TARG_PORT_CONN_TIME",  data(PhyData[0].MaxTargetPortConnectTime),  0},
    {"RESERVED",                                    data(PhyData[0].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_0_RESERVED1",                data(PhyData[0].Reserved1),                 OPT},
    {"Port",                                        data(PhyData[1].Port),                      DUP},
    {"SAS_IO_UNIT1_PHY_1_PORT",                     data(PhyData[1].Port),                      0},
    {"Port_Flgs",                                   data(PhyData[1].PortFlags),                 DUP},
    {"SAS_IO_UNIT1_PHY_1_PORT_FLGS",                data(PhyData[1].PortFlags),                 0},
    {"PHY_FLGS",                                    data(PhyData[1].PhyFlags),                  DUP},
    {"SAS_IO_UNIT1_PHY_1_PHY_FLGS",                 data(PhyData[1].PhyFlags),                  0},
    {"MIN_MAX_LINK_RATE",                           data(PhyData[1].MaxMinLinkRate),            DUP},
    {"SAS_IO_UNIT1_PHY_1_MIN_MAX_LINK_RATE",        data(PhyData[1].MaxMinLinkRate),            0},
    {"CNTLR_PHY_DEV_INFO",                          data(PhyData[1].ControllerPhyDeviceInfo),   DUP},
    {"SAS_IO_UNIT1_PHY_1_CNTLR_PHY_DEV_INFO",       data(PhyData[1].ControllerPhyDeviceInfo),   0},
    {"MAX_TARG_PORT_CONN_TIME",                     data(PhyData[1].MaxTargetPortConnectTime),  DUP},
    {"SAS_IO_UNIT1_PHY_1_MAX_TARG_PORT_CONN_TIME",  data(PhyData[1].MaxTargetPortConnectTime),  0},
    {"RESERVED",                                    data(PhyData[1].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_1_RESERVED1",                data(PhyData[1].Reserved1),                 OPT},
    {"Port",                                        data(PhyData[2].Port),                      DUP},
    {"SAS_IO_UNIT1_PHY_2_PORT",                     data(PhyData[2].Port),                      0},
    {"Port_Flgs",                                   data(PhyData[2].PortFlags),                 DUP},
    {"SAS_IO_UNIT1_PHY_2_PORT_FLGS",                data(PhyData[2].PortFlags),                 0},
    {"PHY_FLGS",                                    data(PhyData[2].PhyFlags),                  DUP},
    {"SAS_IO_UNIT1_PHY_2_PHY_FLGS",                 data(PhyData[2].PhyFlags),                  0},
    {"MIN_MAX_LINK_RATE",                           data(PhyData[2].MaxMinLinkRate),            DUP},
    {"SAS_IO_UNIT1_PHY_2_MIN_MAX_LINK_RATE",        data(PhyData[2].MaxMinLinkRate),            0},
    {"CNTLR_PHY_DEV_INFO",                          data(PhyData[2].ControllerPhyDeviceInfo),   DUP},
    {"SAS_IO_UNIT1_PHY_2_CNTLR_PHY_DEV_INFO",       data(PhyData[2].ControllerPhyDeviceInfo),   0},
    {"MAX_TARG_PORT_CONN_TIME",                     data(PhyData[2].MaxTargetPortConnectTime),  DUP},
    {"SAS_IO_UNIT1_PHY_2_MAX_TARG_PORT_CONN_TIME",  data(PhyData[2].MaxTargetPortConnectTime),  0},
    {"RESERVED",                                    data(PhyData[2].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_2_RESERVED1",                data(PhyData[2].Reserved1),                 OPT},
    {"Port",                                        data(PhyData[3].Port),                      DUP},
    {"SAS_IO_UNIT1_PHY_3_PORT",                     data(PhyData[3].Port),                      0},
    {"Port_Flgs",                                   data(PhyData[3].PortFlags),                 DUP},
    {"SAS_IO_UNIT1_PHY_3_PORT_FLGS",                data(PhyData[3].PortFlags),                 0},
    {"PHY_FLGS",                                    data(PhyData[3].PhyFlags),                  DUP},
    {"SAS_IO_UNIT1_PHY_3_PHY_FLGS",                 data(PhyData[3].PhyFlags),                  0},
    {"MIN_MAX_LINK_RATE",                           data(PhyData[3].MaxMinLinkRate),            DUP},
    {"SAS_IO_UNIT1_PHY_3_MIN_MAX_LINK_RATE",        data(PhyData[3].MaxMinLinkRate),            0},
    {"CNTLR_PHY_DEV_INFO",                          data(PhyData[3].ControllerPhyDeviceInfo),   DUP},
    {"SAS_IO_UNIT1_PHY_3_CNTLR_PHY_DEV_INFO",       data(PhyData[3].ControllerPhyDeviceInfo),   0},
    {"MAX_TARG_PORT_CONN_TIME",                     data(PhyData[3].MaxTargetPortConnectTime),  DUP},
    {"SAS_IO_UNIT1_PHY_3_MAX_TARG_PORT_CONN_TIME",  data(PhyData[3].MaxTargetPortConnectTime),  0},
    {"RESERVED",                                    data(PhyData[3].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_3_RESERVED1",                data(PhyData[3].Reserved1),                 OPT},
    {"Port",                                        data(PhyData[4].Port),                      DUP},
    {"SAS_IO_UNIT1_PHY_4_PORT",                     data(PhyData[4].Port),                      0},
    {"Port_Flgs",                                   data(PhyData[4].PortFlags),                 DUP},
    {"SAS_IO_UNIT1_PHY_4_PORT_FLGS",                data(PhyData[4].PortFlags),                 0},
    {"PHY_FLGS",                                    data(PhyData[4].PhyFlags),                  DUP},
    {"SAS_IO_UNIT1_PHY_4_PHY_FLGS",                 data(PhyData[4].PhyFlags),                  0},
    {"MIN_MAX_LINK_RATE",                           data(PhyData[4].MaxMinLinkRate),            DUP},
    {"SAS_IO_UNIT1_PHY_4_MIN_MAX_LINK_RATE",        data(PhyData[4].MaxMinLinkRate),            0},
    {"CNTLR_PHY_DEV_INFO",                          data(PhyData[4].ControllerPhyDeviceInfo),   DUP},
    {"SAS_IO_UNIT1_PHY_4_CNTLR_PHY_DEV_INFO",       data(PhyData[4].ControllerPhyDeviceInfo),   0},
    {"MAX_TARG_PORT_CONN_TIME",                     data(PhyData[4].MaxTargetPortConnectTime),  DUP},
    {"SAS_IO_UNIT1_PHY_4_MAX_TARG_PORT_CONN_TIME",  data(PhyData[4].MaxTargetPortConnectTime),  0},
    {"RESERVED",                                    data(PhyData[4].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_4_RESERVED1",                data(PhyData[4].Reserved1),                 OPT},
    {"Port",                                        data(PhyData[5].Port),                      DUP},
    {"SAS_IO_UNIT1_PHY_5_PORT",                     data(PhyData[5].Port),                      0},
    {"Port_Flgs",                                   data(PhyData[5].PortFlags),                 DUP},
    {"SAS_IO_UNIT1_PHY_5_PORT_FLGS",                data(PhyData[5].PortFlags),                 0},
    {"PHY_FLGS",                                    data(PhyData[5].PhyFlags),                  DUP},
    {"SAS_IO_UNIT1_PHY_5_PHY_FLGS",                 data(PhyData[5].PhyFlags),                  0},
    {"MIN_MAX_LINK_RATE",                           data(PhyData[5].MaxMinLinkRate),            DUP},
    {"SAS_IO_UNIT1_PHY_5_MIN_MAX_LINK_RATE",        data(PhyData[5].MaxMinLinkRate),            0},
    {"CNTLR_PHY_DEV_INFO",                          data(PhyData[5].ControllerPhyDeviceInfo),   DUP},
    {"SAS_IO_UNIT1_PHY_5_CNTLR_PHY_DEV_INFO",       data(PhyData[5].ControllerPhyDeviceInfo),   0},
    {"MAX_TARG_PORT_CONN_TIME",                     data(PhyData[5].MaxTargetPortConnectTime),  DUP},
    {"SAS_IO_UNIT1_PHY_5_MAX_TARG_PORT_CONN_TIME",  data(PhyData[5].MaxTargetPortConnectTime),  0},
    {"RESERVED",                                    data(PhyData[5].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_5_RESERVED1",                data(PhyData[5].Reserved1),                 OPT},
    {"Port",                                        data(PhyData[6].Port),                      DUP},
    {"SAS_IO_UNIT1_PHY_6_PORT",                     data(PhyData[6].Port),                      0},
    {"Port_Flgs",                                   data(PhyData[6].PortFlags),                 DUP},
    {"SAS_IO_UNIT1_PHY_6_PORT_FLGS",                data(PhyData[6].PortFlags),                 0},
    {"PHY_FLGS",                                    data(PhyData[6].PhyFlags),                  DUP},
    {"SAS_IO_UNIT1_PHY_6_PHY_FLGS",                 data(PhyData[6].PhyFlags),                  0},
    {"MIN_MAX_LINK_RATE",                           data(PhyData[6].MaxMinLinkRate),            DUP},
    {"SAS_IO_UNIT1_PHY_6_MIN_MAX_LINK_RATE",        data(PhyData[6].MaxMinLinkRate),            0},
    {"CNTLR_PHY_DEV_INFO",                          data(PhyData[6].ControllerPhyDeviceInfo),   DUP},
    {"SAS_IO_UNIT1_PHY_6_CNTLR_PHY_DEV_INFO",       data(PhyData[6].ControllerPhyDeviceInfo),   0},
    {"MAX_TARG_PORT_CONN_TIME",                     data(PhyData[6].MaxTargetPortConnectTime),  DUP},
    {"SAS_IO_UNIT1_PHY_6_MAX_TARG_PORT_CONN_TIME",  data(PhyData[6].MaxTargetPortConnectTime),  0},
    {"RESERVED",                                    data(PhyData[6].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_6_RESERVED1",                data(PhyData[6].Reserved1),                 OPT},
    {"Port",                                        data(PhyData[7].Port),                      DUP},
    {"SAS_IO_UNIT1_PHY_7_PORT",                     data(PhyData[7].Port),                      0},
    {"Port_Flgs",                                   data(PhyData[7].PortFlags),                 DUP},
    {"SAS_IO_UNIT1_PHY_7_PORT_FLGS",                data(PhyData[7].PortFlags),                 0},
    {"PHY_FLGS",                                    data(PhyData[7].PhyFlags),                  DUP},
    {"SAS_IO_UNIT1_PHY_7_PHY_FLGS",                 data(PhyData[7].PhyFlags),                  0},
    {"MIN_MAX_LINK_RATE",                           data(PhyData[7].MaxMinLinkRate),            DUP},
    {"SAS_IO_UNIT1_PHY_7_MIN_MAX_LINK_RATE",        data(PhyData[7].MaxMinLinkRate),            0},
    {"CNTLR_PHY_DEV_INFO",                          data(PhyData[7].ControllerPhyDeviceInfo),   DUP},
    {"SAS_IO_UNIT1_PHY_7_CNTLR_PHY_DEV_INFO",       data(PhyData[7].ControllerPhyDeviceInfo),   0},
    {"MAX_TARG_PORT_CONN_TIME",                     data(PhyData[7].MaxTargetPortConnectTime),  DUP},
    {"SAS_IO_UNIT1_PHY_7_MAX_TARG_PORT_CONN_TIME",  data(PhyData[7].MaxTargetPortConnectTime),  0},
    {"RESERVED",                                    data(PhyData[7].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_7_RESERVED1",                data(PhyData[7].Reserved1),                 OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pMpi2SasIOUnitPage1_t)0)->x, sizeof(((pMpi2SasIOUnitPage1_t)0)->x)

ITEM sas_io_unit_page_1_items2[] =
{
    {"CONTROLFLAGS",                                data(ControlFlags),                         0},
    {"SASNARROWMAXQDEPTH",                          data(SASNarrowMaxQueueDepth),               0},
    {"ADDCONTROLFLAGS",                             data(AdditionalControlFlags),               0},
    {"SASWIDEMAXQDEPTH",                            data(SASWideMaxQueueDepth),                 0},
    {"NUM_PHYS",                                    data(NumPhys),                              0},
    {"SATAMAXQDEPTH",                               data(SATAMaxQDepth),                        0},
    {"REPDEVMISSINGDELAY",                          data(ReportDeviceMissingDelay),             0},
    {"IODEVMISSINGDELAY",                           data(IODeviceMissingDelay),                 0},
    {"SAS_IO_UNIT1_PHY_0_PORT",                     data(PhyData[0].Port),                      0},
    {"SAS_IO_UNIT1_PHY_0_PORT_FLGS",                data(PhyData[0].PortFlags),                 0},
    {"SAS_IO_UNIT1_PHY_0_PHY_FLGS",                 data(PhyData[0].PhyFlags),                  0},
    {"SAS_IO_UNIT1_PHY_0_MIN_MAX_LINK_RATE",        data(PhyData[0].MaxMinLinkRate),            0},
    {"SAS_IO_UNIT1_PHY_0_CNTLR_PHY_DEV_INFO",       data(PhyData[0].ControllerPhyDeviceInfo),   0},
    {"SAS_IO_UNIT1_PHY_0_MAX_TARG_PORT_CONN_TIME",  data(PhyData[0].MaxTargetPortConnectTime),  0},
    {"SAS_IO_UNIT1_PHY_0_RESERVED1",                data(PhyData[0].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_1_PORT",                     data(PhyData[1].Port),                      0},
    {"SAS_IO_UNIT1_PHY_1_PORT_FLGS",                data(PhyData[1].PortFlags),                 0},
    {"SAS_IO_UNIT1_PHY_1_PHY_FLGS",                 data(PhyData[1].PhyFlags),                  0},
    {"SAS_IO_UNIT1_PHY_1_MIN_MAX_LINK_RATE",        data(PhyData[1].MaxMinLinkRate),            0},
    {"SAS_IO_UNIT1_PHY_1_CNTLR_PHY_DEV_INFO",       data(PhyData[1].ControllerPhyDeviceInfo),   0},
    {"SAS_IO_UNIT1_PHY_1_MAX_TARG_PORT_CONN_TIME",  data(PhyData[1].MaxTargetPortConnectTime),  0},
    {"SAS_IO_UNIT1_PHY_1_RESERVED1",                data(PhyData[1].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_2_PORT",                     data(PhyData[2].Port),                      0},
    {"SAS_IO_UNIT1_PHY_2_PORT_FLGS",                data(PhyData[2].PortFlags),                 0},
    {"SAS_IO_UNIT1_PHY_2_PHY_FLGS",                 data(PhyData[2].PhyFlags),                  0},
    {"SAS_IO_UNIT1_PHY_2_MIN_MAX_LINK_RATE",        data(PhyData[2].MaxMinLinkRate),            0},
    {"SAS_IO_UNIT1_PHY_2_CNTLR_PHY_DEV_INFO",       data(PhyData[2].ControllerPhyDeviceInfo),   0},
    {"SAS_IO_UNIT1_PHY_2_MAX_TARG_PORT_CONN_TIME",  data(PhyData[2].MaxTargetPortConnectTime),  0},
    {"SAS_IO_UNIT1_PHY_2_RESERVED1",                data(PhyData[2].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_3_PORT",                     data(PhyData[3].Port),                      0},
    {"SAS_IO_UNIT1_PHY_3_PORT_FLGS",                data(PhyData[3].PortFlags),                 0},
    {"SAS_IO_UNIT1_PHY_3_PHY_FLGS",                 data(PhyData[3].PhyFlags),                  0},
    {"SAS_IO_UNIT1_PHY_3_MIN_MAX_LINK_RATE",        data(PhyData[3].MaxMinLinkRate),            0},
    {"SAS_IO_UNIT1_PHY_3_CNTLR_PHY_DEV_INFO",       data(PhyData[3].ControllerPhyDeviceInfo),   0},
    {"SAS_IO_UNIT1_PHY_3_MAX_TARG_PORT_CONN_TIME",  data(PhyData[3].MaxTargetPortConnectTime),  0},
    {"SAS_IO_UNIT1_PHY_3_RESERVED1",                data(PhyData[3].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_4_PORT",                     data(PhyData[4].Port),                      0},
    {"SAS_IO_UNIT1_PHY_4_PORT_FLGS",                data(PhyData[4].PortFlags),                 0},
    {"SAS_IO_UNIT1_PHY_4_PHY_FLGS",                 data(PhyData[4].PhyFlags),                  0},
    {"SAS_IO_UNIT1_PHY_4_MIN_MAX_LINK_RATE",        data(PhyData[4].MaxMinLinkRate),            0},
    {"SAS_IO_UNIT1_PHY_4_CNTLR_PHY_DEV_INFO",       data(PhyData[4].ControllerPhyDeviceInfo),   0},
    {"SAS_IO_UNIT1_PHY_4_MAX_TARG_PORT_CONN_TIME",  data(PhyData[4].MaxTargetPortConnectTime),  0},
    {"SAS_IO_UNIT1_PHY_4_RESERVED1",                data(PhyData[4].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_5_PORT",                     data(PhyData[5].Port),                      0},
    {"SAS_IO_UNIT1_PHY_5_PORT_FLGS",                data(PhyData[5].PortFlags),                 0},
    {"SAS_IO_UNIT1_PHY_5_PHY_FLGS",                 data(PhyData[5].PhyFlags),                  0},
    {"SAS_IO_UNIT1_PHY_5_MIN_MAX_LINK_RATE",        data(PhyData[5].MaxMinLinkRate),            0},
    {"SAS_IO_UNIT1_PHY_5_CNTLR_PHY_DEV_INFO",       data(PhyData[5].ControllerPhyDeviceInfo),   0},
    {"SAS_IO_UNIT1_PHY_5_MAX_TARG_PORT_CONN_TIME",  data(PhyData[5].MaxTargetPortConnectTime),  0},
    {"SAS_IO_UNIT1_PHY_5_RESERVED1",                data(PhyData[5].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_6_PORT",                     data(PhyData[6].Port),                      0},
    {"SAS_IO_UNIT1_PHY_6_PORT_FLGS",                data(PhyData[6].PortFlags),                 0},
    {"SAS_IO_UNIT1_PHY_6_PHY_FLGS",                 data(PhyData[6].PhyFlags),                  0},
    {"SAS_IO_UNIT1_PHY_6_MIN_MAX_LINK_RATE",        data(PhyData[6].MaxMinLinkRate),            0},
    {"SAS_IO_UNIT1_PHY_6_CNTLR_PHY_DEV_INFO",       data(PhyData[6].ControllerPhyDeviceInfo),   0},
    {"SAS_IO_UNIT1_PHY_6_MAX_TARG_PORT_CONN_TIME",  data(PhyData[6].MaxTargetPortConnectTime),  0},
    {"SAS_IO_UNIT1_PHY_6_RESERVED1",                data(PhyData[6].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_7_PORT",                     data(PhyData[7].Port),                      0},
    {"SAS_IO_UNIT1_PHY_7_PORT_FLGS",                data(PhyData[7].PortFlags),                 0},
    {"SAS_IO_UNIT1_PHY_7_PHY_FLGS",                 data(PhyData[7].PhyFlags),                  0},
    {"SAS_IO_UNIT1_PHY_7_MIN_MAX_LINK_RATE",        data(PhyData[7].MaxMinLinkRate),            0},
    {"SAS_IO_UNIT1_PHY_7_CNTLR_PHY_DEV_INFO",       data(PhyData[7].ControllerPhyDeviceInfo),   0},
    {"SAS_IO_UNIT1_PHY_7_MAX_TARG_PORT_CONN_TIME",  data(PhyData[7].MaxTargetPortConnectTime),  0},
    {"SAS_IO_UNIT1_PHY_7_RESERVED1",                data(PhyData[7].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_8_PORT",                     data(PhyData[8].Port),                      0},
    {"SAS_IO_UNIT1_PHY_8_PORT_FLGS",                data(PhyData[8].PortFlags),                 0},
    {"SAS_IO_UNIT1_PHY_8_PHY_FLGS",                 data(PhyData[8].PhyFlags),                  0},
    {"SAS_IO_UNIT1_PHY_8_MIN_MAX_LINK_RATE",        data(PhyData[8].MaxMinLinkRate),            0},
    {"SAS_IO_UNIT1_PHY_8_CNTLR_PHY_DEV_INFO",       data(PhyData[8].ControllerPhyDeviceInfo),   0},
    {"SAS_IO_UNIT1_PHY_8_MAX_TARG_PORT_CONN_TIME",  data(PhyData[8].MaxTargetPortConnectTime),  0},
    {"SAS_IO_UNIT1_PHY_8_RESERVED1",                data(PhyData[8].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_9_PORT",                     data(PhyData[9].Port),                      0},
    {"SAS_IO_UNIT1_PHY_9_PORT_FLGS",                data(PhyData[9].PortFlags),                 0},
    {"SAS_IO_UNIT1_PHY_9_PHY_FLGS",                 data(PhyData[9].PhyFlags),                  0},
    {"SAS_IO_UNIT1_PHY_9_MIN_MAX_LINK_RATE",        data(PhyData[9].MaxMinLinkRate),            0},
    {"SAS_IO_UNIT1_PHY_9_CNTLR_PHY_DEV_INFO",       data(PhyData[9].ControllerPhyDeviceInfo),   0},
    {"SAS_IO_UNIT1_PHY_9_MAX_TARG_PORT_CONN_TIME",  data(PhyData[9].MaxTargetPortConnectTime),  0},
    {"SAS_IO_UNIT1_PHY_9_RESERVED1",                data(PhyData[9].Reserved1),                 OPT},
    {"SAS_IO_UNIT1_PHY_10_PORT",                    data(PhyData[10].Port),                     0},
    {"SAS_IO_UNIT1_PHY_10_PORT_FLGS",               data(PhyData[10].PortFlags),                0},
    {"SAS_IO_UNIT1_PHY_10_PHY_FLGS",                data(PhyData[10].PhyFlags),                 0},
    {"SAS_IO_UNIT1_PHY_10_MIN_MAX_LINK_RATE",       data(PhyData[10].MaxMinLinkRate),           0},
    {"SAS_IO_UNIT1_PHY_10_CNTLR_PHY_DEV_INFO",      data(PhyData[10].ControllerPhyDeviceInfo),  0},
    {"SAS_IO_UNIT1_PHY_10_MAX_TARG_PORT_CONN_TIME", data(PhyData[10].MaxTargetPortConnectTime), 0},
    {"SAS_IO_UNIT1_PHY_10_RESERVED1",               data(PhyData[10].Reserved1),                OPT},
    {"SAS_IO_UNIT1_PHY_11_PORT",                    data(PhyData[11].Port),                     0},
    {"SAS_IO_UNIT1_PHY_11_PORT_FLGS",               data(PhyData[11].PortFlags),                0},
    {"SAS_IO_UNIT1_PHY_11_PHY_FLGS",                data(PhyData[11].PhyFlags),                 0},
    {"SAS_IO_UNIT1_PHY_11_MIN_MAX_LINK_RATE",       data(PhyData[11].MaxMinLinkRate),           0},
    {"SAS_IO_UNIT1_PHY_11_CNTLR_PHY_DEV_INFO",      data(PhyData[11].ControllerPhyDeviceInfo),  0},
    {"SAS_IO_UNIT1_PHY_11_MAX_TARG_PORT_CONN_TIME", data(PhyData[11].MaxTargetPortConnectTime), 0},
    {"SAS_IO_UNIT1_PHY_11_RESERVED1",               data(PhyData[11].Reserved1),                OPT},
    {"SAS_IO_UNIT1_PHY_12_PORT",                    data(PhyData[12].Port),                     0},
    {"SAS_IO_UNIT1_PHY_12_PORT_FLGS",               data(PhyData[12].PortFlags),                0},
    {"SAS_IO_UNIT1_PHY_12_PHY_FLGS",                data(PhyData[12].PhyFlags),                 0},
    {"SAS_IO_UNIT1_PHY_12_MIN_MAX_LINK_RATE",       data(PhyData[12].MaxMinLinkRate),           0},
    {"SAS_IO_UNIT1_PHY_12_CNTLR_PHY_DEV_INFO",      data(PhyData[12].ControllerPhyDeviceInfo),  0},
    {"SAS_IO_UNIT1_PHY_12_MAX_TARG_PORT_CONN_TIME", data(PhyData[12].MaxTargetPortConnectTime), 0},
    {"SAS_IO_UNIT1_PHY_12_RESERVED1",               data(PhyData[12].Reserved1),                OPT},
    {"SAS_IO_UNIT1_PHY_13_PORT",                    data(PhyData[13].Port),                     0},
    {"SAS_IO_UNIT1_PHY_13_PORT_FLGS",               data(PhyData[13].PortFlags),                0},
    {"SAS_IO_UNIT1_PHY_13_PHY_FLGS",                data(PhyData[13].PhyFlags),                 0},
    {"SAS_IO_UNIT1_PHY_13_MIN_MAX_LINK_RATE",       data(PhyData[13].MaxMinLinkRate),           0},
    {"SAS_IO_UNIT1_PHY_13_CNTLR_PHY_DEV_INFO",      data(PhyData[13].ControllerPhyDeviceInfo),  0},
    {"SAS_IO_UNIT1_PHY_13_MAX_TARG_PORT_CONN_TIME", data(PhyData[13].MaxTargetPortConnectTime), 0},
    {"SAS_IO_UNIT1_PHY_13_RESERVED1",               data(PhyData[13].Reserved1),                OPT},
    {"SAS_IO_UNIT1_PHY_14_PORT",                    data(PhyData[14].Port),                     0},
    {"SAS_IO_UNIT1_PHY_14_PORT_FLGS",               data(PhyData[14].PortFlags),                0},
    {"SAS_IO_UNIT1_PHY_14_PHY_FLGS",                data(PhyData[14].PhyFlags),                 0},
    {"SAS_IO_UNIT1_PHY_14_MIN_MAX_LINK_RATE",       data(PhyData[14].MaxMinLinkRate),           0},
    {"SAS_IO_UNIT1_PHY_14_CNTLR_PHY_DEV_INFO",      data(PhyData[14].ControllerPhyDeviceInfo),  0},
    {"SAS_IO_UNIT1_PHY_14_MAX_TARG_PORT_CONN_TIME", data(PhyData[14].MaxTargetPortConnectTime), 0},
    {"SAS_IO_UNIT1_PHY_14_RESERVED1",               data(PhyData[14].Reserved1),                OPT},
    {"SAS_IO_UNIT1_PHY_15_PORT",                    data(PhyData[15].Port),                     0},
    {"SAS_IO_UNIT1_PHY_15_PORT_FLGS",               data(PhyData[15].PortFlags),                0},
    {"SAS_IO_UNIT1_PHY_15_PHY_FLGS",                data(PhyData[15].PhyFlags),                 0},
    {"SAS_IO_UNIT1_PHY_15_MIN_MAX_LINK_RATE",       data(PhyData[15].MaxMinLinkRate),           0},
    {"SAS_IO_UNIT1_PHY_15_CNTLR_PHY_DEV_INFO",      data(PhyData[15].ControllerPhyDeviceInfo),  0},
    {"SAS_IO_UNIT1_PHY_15_MAX_TARG_PORT_CONN_TIME", data(PhyData[15].MaxTargetPortConnectTime), 0},
    {"SAS_IO_UNIT1_PHY_15_RESERVED1",               data(PhyData[15].Reserved1),                OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pSasIOUnitPage2_t)0)->x, sizeof(((pSasIOUnitPage2_t)0)->x)

ITEM sas_io_unit_page_2_items[] =
{
    {"SAS2_NUMDEVICESPERENCLOSURE", data(NumDevsPerEnclosure),      0},
    {"MAX_PERSIST_IDS",             data(MaxPersistentIDs),         0},
    {"MAX_PERSIST_IDS_USED",        data(NumPersistentIDsUsed),     0},
    {"STATUS",                      data(Status),                   0},
    {"FLAGS",                       data(Flags),                    0},
    {"SAS2_MAXNUMPHYMAPPEDID",      data(MaxNumPhysicalMappedIDs),  0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pSasIOUnitPage3_t)0)->x, sizeof(((pSasIOUnitPage3_t)0)->x)

ITEM sas_io_unit_page_3_items[] =
{
    {"RESERVED1",                   data(Reserved1),                        OPT},
    {"MAX_INVALID_DWRD_CNT",        data(MaxInvalidDwordCount),             0},
    {"INVALID_DWRD_CNT_TIME",       data(InvalidDwordCountTime),            0},
    {"MAX_RUNNING_DISPARE_ERR_CNT", data(MaxRunningDisparityErrorCount),    0},
    {"RUNNING_DISPARE_ERR_TIME",    data(RunningDisparityErrorTime),        0},
    {"MAX_LOSS_DWRD_SYNC_CNT",      data(MaxLossDwordSynchCount),           0},
    {"LOSS_DWRD_SYNC_CNT_TIME",     data(LossDwordSynchCountTime),          0},
    {"MAX_PHY_RESET_PROB_CNT",      data(MaxPhyResetProblemCount),          0},
    {"PHY_RESET_PROB_TIME",         data(PhyResetProblemTime),              0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pMpi2SasIOUnitPage4_t)0)->x, sizeof(((pMpi2SasIOUnitPage4_t)0)->x)

ITEM sas_io_unit_page_4_items2[] =
{
    {"GROUP0_MAX_TARGET_SPINUP",    data(SpinupGroupParameters[0].MaxTargetSpinup), 0},
    {"GROUP0_SPINUP_DELAY",         data(SpinupGroupParameters[0].SpinupDelay),     0},
    {"GROUP0_RESERVED1",            data(SpinupGroupParameters[0].Reserved1),       OPT},
    {"GROUP1_MAX_TARGET_SPINUP",    data(SpinupGroupParameters[1].MaxTargetSpinup), 0},
    {"GROUP1_SPINUP_DELAY",         data(SpinupGroupParameters[1].SpinupDelay),     0},
    {"GROUP1_RESERVED1",            data(SpinupGroupParameters[1].Reserved1),       OPT},
    {"GROUP2_MAX_TARGET_SPINUP",    data(SpinupGroupParameters[2].MaxTargetSpinup), 0},
    {"GROUP2_SPINUP_DELAY",         data(SpinupGroupParameters[2].SpinupDelay),     0},
    {"GROUP2_RESERVED1",            data(SpinupGroupParameters[2].Reserved1),       OPT},
    {"GROUP3_MAX_TARGET_SPINUP",    data(SpinupGroupParameters[3].MaxTargetSpinup), 0},
    {"GROUP3_SPINUP_DELAY",         data(SpinupGroupParameters[3].SpinupDelay),     0},
    {"GROUP3_RESERVED1",            data(SpinupGroupParameters[3].Reserved1),       OPT},
    {"RESERVED1",                   data(Reserved1),                                OPT},
    {"RESERVED2",                   data(Reserved2),                                OPT},
    {"RESERVED3",                   data(Reserved3),                                OPT},
    {"RESERVED4",                   data(Reserved4),                                OPT},
    {"NUM_PHYS",                    data(NumPhys),                                  0},
    {"PE_INIT_SPINUP_DELAY",        data(PEInitialSpinupDelay),                     0},
    {"PE_REPLY_DELAY",              data(PEReplyDelay),                             0},
    {"FLAGS",                       data(Flags),                                    0},
    {"PHY_0",                       data(PHY[0]),                                   0},
    {"PHY_1",                       data(PHY[1]),                                   0},
    {"PHY_2",                       data(PHY[2]),                                   0},
    {"PHY_3",                       data(PHY[3]),                                   0},
    {"PHY_4",                       data(PHY[4]),                                   0},
    {"PHY_5",                       data(PHY[5]),                                   0},
    {"PHY_6",                       data(PHY[6]),                                   0},
    {"PHY_7",                       data(PHY[7]),                                   0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pSasExpanderPage0_t)0)->x, sizeof(((pSasExpanderPage0_t)0)->x)

ITEM sas_expander_page_0_items[] =
{
    {"SAS_EXP0_PHYSICALPORT",       data(PhysicalPort),         0},
    {"SAS_EXP0_ENCLOSUREHANDLE",    data(EnclosureHandle),      0},
    {"SASADRSHIGH",                 data(SASAddress.High),      0},
    {"SASADRSLOW",                  data(SASAddress.Low),       0},
    {"SAS_EXP0_DISCOVERYSTATUS",    data(DiscoveryStatus),      0},
    {"DEVHNDL",                     data(DevHandle),            0},
    {"PARENTDEVHNDL",               data(ParentDevHandle),      0},
    {"EXPNDRCHGCNT",                data(ExpanderChangeCount),  0},
    {"EXPNDRROUTEINDX",             data(ExpanderRouteIndexes), 0},
    {"NUMPHYS",                     data(NumPhys),              0},
    {"SASLEVEL",                    data(SASLevel),             0},
    {"FLAGS",                       data(Flags),                0},
    {"DISCOVERYSTATUS",             data(Reserved3),            OPT},
    {"RESERVED3",                   data(Reserved3),            OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pSasExpanderPage1_t)0)->x, sizeof(((pSasExpanderPage1_t)0)->x)

ITEM sas_expander_page_1_items[] =
{
    {"SAS_EXP1_PHYSICALPORT",       data(PhysicalPort),                 0},
    {"NUMPHYS",                     data(NumPhys),                      0},
    {"PHY",                         data(Phy),                          0},
    {"SAS_EXP1_NUMTBLENTRIESPROG",  data(NumTableEntriesProgrammed),    0},
    {"PROGLINKRATE",                data(ProgrammedLinkRate),           0},
    {"HWLINKRATE",                  data(HwLinkRate),                   0},
    {"ATTCHDDEVHANDLE",             data(AttachedDevHandle),            0},
    {"PHYINFO",                     data(PhyInfo),                      0},
    {"ATTCHDDEVINFO",               data(AttachedDeviceInfo),           0},
    {"OWNERDEVHNDL",                data(OwnerDevHandle),               0},
    {"CHGCNT",                      data(ChangeCount),                  0},
    {"NEGLNKRATE",                  data(NegotiatedLinkRate),           0},
    {"PHYIDENTIFIER",               data(PhyIdentifier),                0},
    {"ATTCHDPHYIDENT",              data(AttachedPhyIdentifier),        0},
    {"RESERVED3",                   data(Reserved3),                    OPT},
    {"DISCOVERYInfo",               data(DiscoveryInfo),                DUP},
    {"DISCOVERYINFO",               data(DiscoveryInfo),                0},
    {"RESERVED4",                   data(Reserved4),                    OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pSasDevicePage0_t)0)->x, sizeof(((pSasDevicePage0_t)0)->x)

ITEM sas_device_page_0_items[] =
{
    {"SLOT",                        data(Slot),             0},
    {"ENCLOSURE_HANDLE",            data(EnclosureHandle),  0},
    {"SASADRSHIGH",                 data(SASAddress.High),  0},
    {"SASADRSLOW",                  data(SASAddress.Low),   0},
    {"SAS_DEV0_PARENTDEVHANDLE",    data(ParentDevHandle),  0},
    {"SAS_DEV0_PHYNUM",             data(PhyNum),           0},
    {"SAS_DEV0_ACCESSSTATUS",       data(AccessStatus),     0},
    {"DEVHNDL",                     data(DevHandle),        0},
    {"TARGETID",                    data(TargetID),         0},
    {"BUS",                         data(Bus),              0},
    {"DEVICEINFO",                  data(DeviceInfo),       0},
    {"FLAGS",                       data(Flags),            0},
    {"RESERVED2",                   data(Reserved2),        OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pSasDevicePage1_t)0)->x, sizeof(((pSasDevicePage1_t)0)->x)

ITEM sas_device_page_1_items[] =
{
    {"RESERVED1",           data(Reserved1),            OPT},
    {"SASADRSHIGH",         data(SASAddress.High),      0},
    {"SASADRSLOW",          data(SASAddress.Low),       0},
    {"RESERVED2",           data(Reserved2),            OPT},
    {"DEVHNDL",             data(DevHandle),            0},
    {"TARGETID",            data(TargetID),             0},
    {"BUS",                 data(Bus),                  0},
    {"INITREGDEVICEFIS",    data(InitialRegDeviceFIS),  0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pSasDevicePage2_t)0)->x, sizeof(((pSasDevicePage2_t)0)->x)

ITEM sas_device_page_2_items[] =
{
    {"SAS_DEV2_PHYSICALIDHIGH",     data(PhysicalIdentifier.High),  0},
    {"SAS_DEV2_PHYSICALIDLOW",      data(PhysicalIdentifier.Low),   0},
    {"SAS_DEV2_ENCLOSURE_MAPPING",  data(EnclosureMapping),         0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pSasPhyPage0_t)0)->x, sizeof(((pSasPhyPage0_t)0)->x)

ITEM sas_phy_page_0_items[] =
{
    {"SAS_PHY0_OWNER_DEV_HANDLE",   data(OwnerDevHandle),           0},
    {"RESERVED1",                   data(Reserved1),                OPT},
    {"SASADRSHIGH",                 data(SASAddress.High),          0},
    {"SASADRSLOW",                  data(SASAddress.Low),           0},
    {"ATTCHDDEVHNDL",               data(AttachedDevHandle),        0},
    {"ATTCHDPHYIDENTIFIER",         data(AttachedPhyIdentifier),    0},
    {"RESERVED2",                   data(Reserved2),                OPT},
    {"ATTCHDDEVINFO",               data(AttachedDeviceInfo),       0},
    {"PRGMDLINKRATE",               data(ProgrammedLinkRate),       0},
    {"HWLINKRATE",                  data(HwLinkRate),               0},
    {"CHNGCOUNT",                   data(ChangeCount),              0},
    {"SAS_PHY0_FLAGS",              data(Flags),                    0},
    {"PHYINFO",                     data(PhyInfo),                  0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pSasPhyPage1_t)0)->x, sizeof(((pSasPhyPage1_t)0)->x)

ITEM sas_phy_page_1_items[] =
{
    {"RESERVED1",               data(Reserved1),                    OPT},
    {"INVALIDDWRDCNT",          data(InvalidDwordCount),            0},
    {"RUNNGDISPARITYERRCNT",    data(RunningDisparityErrorCount),   0},
    {"LOSSDWRDSYNCCNT",         data(LossDwordSynchCount),          0},
    {"PHYRESETPROBCNT",         data(PhyResetProblemCount),         0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pSasEnclosurePage0_t)0)->x, sizeof(((pSasEnclosurePage0_t)0)->x)

ITEM sas_enclosure_page_0_items[] =
{
    {"RESERVED1",               data(Reserved1),                OPT},
    {"ENCLOSURELOGICALID_HIGH", data(EnclosureLogicalID.High),  0},
    {"ENCLOSURELOGICALID_LOW",  data(EnclosureLogicalID.Low),   0},
    {"FLAGS",                   data(Flags),                    0},
    {"ENCLOSUREHANDLE",         data(EnclosureHandle),          0},
    {"NUMSLOTS",                data(NumSlots),                 0},
    {"STARTSLOT",               data(StartSlot),                0},
    {"STARTTARGETID",           data(StartTargetID),            0},
    {"STARTBUS",                data(StartBus),                 0},
    {"SEPTARGETID",             data(SEPTargetID),              0},
    {"SEPBUS",                  data(SEPBus),                   0},
    {"RESERVED2",               data(Reserved2),                OPT},
    {"RESERVED3",               data(Reserved3),                OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pPersistentId_SAS_t)0)->x, sizeof(((pPersistentId_SAS_t)0)->x)

ITEM sas_persistent_id_items[] =
{
    {"PERSISTID_SASADDRESS_HIGH_0", data(PersistId[0].SasAddress.Word.High), 0},
    {"PERSISTID_SASADDRESS_LOW_0",  data(PersistId[0].SasAddress.Word.Low),  0},
    {"PERSISTID_RESERVED_0",        data(PersistId[0].Reserved),        OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pRaidVolumePage0_t)0)->x, sizeof(((pRaidVolumePage0_t)0)->x)

ITEM raid_volume_page_0_items[] =
{
    {"ALL", sizeof(ConfigPageHeader_t), sizeof(RaidVolumePage0_t) - sizeof(ConfigPageHeader_t), 0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pRaidVolumePage1_t)0)->x, sizeof(((pRaidVolumePage1_t)0)->x)

ITEM raid_volume_page_1_items[] =
{
    {"RAIDVOL1_VOLUMEID",   data(VolumeID),     0},
    {"RAIDVOL1_VOLUMEBUS",  data(VolumeBus),    0},
    {"RAIDVOL1_VOLUMEIOC",  data(VolumeIOC),    0},
    {"RAIDVOL1_WWID_HIGH",  data(WWID.High),    0},
    {"RAIDVOL1_WWID_LOW",   data(WWID.Low),     0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pRaidPhysDiskPage0_t)0)->x, sizeof(((pRaidPhysDiskPage0_t)0)->x)

ITEM raid_physdisk_page_0_items[] =
{
    {"ALL", sizeof(ConfigPageHeader_t), sizeof(RaidPhysDiskPage0_t) - sizeof(ConfigPageHeader_t), 0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pRaidPhysDiskPage1_t)0)->x, sizeof(((pRaidPhysDiskPage1_t)0)->x)

ITEM raid_physdisk_page_1_items[] =
{
    {"RAIDPHYDISK1_NUMPHYSDISKPATH",    data(NumPhysDiskPaths), 0},
    {"RAIDPHYDISK1_PHYSDISKNUM",        data(PhysDiskNum),      0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pBIOSPage1_t)0)->x, sizeof(((pBIOSPage1_t)0)->x)

ITEM bios_page_1_items[] =
{
    {"BIOSOPTIONS",             data(BiosOptions),                  0},
    {"IOCSETTINGS",             data(IOCSettings),                  0},
    {"BIOS_RESERVED1",          data(Reserved1),                    OPT},
    {"RESERVED1",               data(Reserved1),                    OPT},
    {"DEVSETTINGS",             data(DeviceSettings),               0},
    {"BIOS_NUMDEVS",            data(NumberOfDevices),              0},
    {"BIOS1_EXPANDERSPINUP",    data(ExpanderSpinup),               MPI1},
    {"RESERVED2",               data(Reserved2),                    OPT},
    {"IOTIMOUTBLKDEVSNONRM",    data(IOTimeoutBlockDevicesNonRM),   0},
    {"IOTIMOUTSEQUENTIAL",      data(IOTimeoutSequential),          0},
    {"IOTIMOUTOTHER",           data(IOTimeoutOther),               0},
    {"IOTIMOUTBLKDEVSRM",       data(IOTimeoutBlockDevicesRM),      0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pBIOSPage2_t)0)->x, sizeof(((pBIOSPage2_t)0)->x)

ITEM bios_page_2_items[] =
{
    {"RESERVED1",               data(Reserved1),                            OPT},
    {"RESERVED2",               data(Reserved2),                            OPT},
    {"RESERVED3",               data(Reserved3),                            OPT},
    {"RESERVED4",               data(Reserved4),                            OPT},
    {"RESERVED5",               data(Reserved5),                            OPT},
    {"RESERVED6",               data(Reserved6),                            OPT},
    {"BIOS2_BOOTDEVICEFORM",    data(BootDeviceForm),                       0},
    {"BIOS2_PREVBOOTDEVFORM",   data(PrevBootDeviceForm),                   0},
    {"RESERVED8",               data(Reserved8),                            OPT},
    {"BIOS2_SASADDRESS_HIGH",   data(BootDevice.SasWwn.SASAddress.High),    0},
    {"BIOS2_SASADDRESS_LOW",    data(BootDevice.SasWwn.SASAddress.Low),     0},
    {"BIOS2_SASADDRESS_LUN",    data(BootDevice.SasWwn.LUN[1]),             0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pMpi2BiosPage2_t)0)->x, sizeof(((pMpi2BiosPage2_t)0)->x)

ITEM bios_page_2_items2[] =
{
    {"RESERVED1",               data(Reserved1),                                                    OPT},
    {"RESERVED2",               data(Reserved2),                                                    OPT},
    {"RESERVED3",               data(Reserved3),                                                    OPT},
    {"RESERVED4",               data(Reserved4),                                                    OPT},
    {"RESERVED5",               data(Reserved5),                                                    OPT},
    {"RESERVED6",               data(Reserved6),                                                    OPT},
    {"REQ_BOOTDEVICEFORM",      data(ReqBootDeviceForm),                                            0},
    {"RESERVED7",               data(Reserved7),                                                    OPT},
    {"RESERVED8",               data(Reserved8),                                                    OPT},
    {"REQ_SASADDRESS_LOW",      data(RequestedBootDevice.SasWwid.SASAddress.Low),                   IGN},
    {"REQ_SASADDRESS_HI",       data(RequestedBootDevice.SasWwid.SASAddress.High),                  IGN},
    {"REQ_SASADDRESS_LUN",      data(RequestedBootDevice.SasWwid.LUN[1]),                           IGN},
    {"REQ_DEVICENAME_LOW",      data(RequestedBootDevice.DeviceName.DeviceName.Low),                IGN},
    {"REQ_DEVICENAME_HI",       data(RequestedBootDevice.DeviceName.DeviceName.High),               IGN},
    {"REQ_DEVICENAME_LUN",      data(RequestedBootDevice.DeviceName.LUN[1]),                        IGN},
    {"REQ_ENCLOSUREID_LOW",     data(RequestedBootDevice.EnclosureSlot.EnclosureLogicalID.Low),     IGN},
    {"REQ_ENCLOSUREID_HI",      data(RequestedBootDevice.EnclosureSlot.EnclosureLogicalID.High),    IGN},
    {"REQ_SLOTNUMBER",          data(RequestedBootDevice.EnclosureSlot.SlotNumber),                 IGN},
    {"REQALT_BOOTDEVICEFORM",   data(ReqAltBootDeviceForm),                                         0},
    {"RESERVED9",               data(Reserved9),                                                    OPT},
    {"RESERVED10",              data(Reserved10),                                                   OPT},
    {"REQALT_SASADDRESS_LOW",   data(RequestedAltBootDevice.SasWwid.SASAddress.Low),                IGN},
    {"REQALT_SASADDRESS_HI",    data(RequestedAltBootDevice.SasWwid.SASAddress.High),               IGN},
    {"REQALT_SASADDRESS_LUN",   data(RequestedAltBootDevice.SasWwid.LUN[1]),                        IGN},
    {"REQALT_DEVICENAME_LOW",   data(RequestedAltBootDevice.DeviceName.DeviceName.Low),             IGN},
    {"REQALT_DEVICENAME_HI",    data(RequestedAltBootDevice.DeviceName.DeviceName.High),            IGN},
    {"REQALT_DEVICENAME_LUN",   data(RequestedAltBootDevice.DeviceName.LUN[1]),                     IGN},
    {"REQALT_ENCLOSUREID_LOW",  data(RequestedAltBootDevice.EnclosureSlot.EnclosureLogicalID.Low),  IGN},
    {"REQALT_ENCLOSUREID_HI",   data(RequestedAltBootDevice.EnclosureSlot.EnclosureLogicalID.High), IGN},
    {"REQALT_SLOTNUMBER",       data(RequestedAltBootDevice.EnclosureSlot.SlotNumber),              IGN},
    {"CURR_BOOTDEVICEFORM",     data(CurrentBootDeviceForm),                                        0},
    {"RESERVED11",              data(Reserved11),                                                   OPT},
    {"RESERVED12",              data(Reserved12),                                                   OPT},
    {"CURR_SASADDRESS_LOW",     data(CurrentBootDevice.SasWwid.SASAddress.Low),                     IGN},
    {"CURR_SASADDRESS_HI",      data(CurrentBootDevice.SasWwid.SASAddress.High),                    IGN},
    {"CURR_SASADDRESS_LUN",     data(CurrentBootDevice.SasWwid.LUN[1]),                             IGN},
    {"CURR_DEVICENAME_LOW",     data(CurrentBootDevice.DeviceName.DeviceName.Low),                  IGN},
    {"CURR_DEVICENAME_HI",      data(CurrentBootDevice.DeviceName.DeviceName.High),                 IGN},
    {"CURR_DEVICENAME_LUN",     data(CurrentBootDevice.DeviceName.LUN[1]),                          IGN},
    {"CURR_ENCLOSUREID_LOW",    data(CurrentBootDevice.EnclosureSlot.EnclosureLogicalID.Low),       IGN},
    {"CURR_ENCLOSUREID_HI",     data(CurrentBootDevice.EnclosureSlot.EnclosureLogicalID.High),      IGN},
    {"CURR_SLOTNUMBER",         data(CurrentBootDevice.EnclosureSlot.SlotNumber),                   IGN},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pMpi2BiosPage3_t)0)->x, sizeof(((pMpi2BiosPage3_t)0)->x)

ITEM bios_page_3_items2[] =
{
    {"GLOBAL_FLAGS",            data(GlobalFlags),                                  0},
    {"BIOS_VERSION",            data(BiosVersion),                                  0},
    {"PCI_BUS_NUMBER_0",        data(AdapterOrder[0].PciBusNumber),                 0},
    {"PCI_DEVICE_FUNCTION_0",   data(AdapterOrder[0].PciDeviceAndFunctionNumber),   0},
    {"ADAPTER_FLAGS_0",         data(AdapterOrder[0].AdapterFlags),                 0},
    {"PCI_BUS_NUMBER_1",        data(AdapterOrder[1].PciBusNumber),                 OPT},
    {"PCI_DEVICE_FUNCTION_1",   data(AdapterOrder[1].PciDeviceAndFunctionNumber),   OPT},
    {"ADAPTER_FLAGS_1",         data(AdapterOrder[1].AdapterFlags),                 OPT},
    {"PCI_BUS_NUMBER_2",        data(AdapterOrder[2].PciBusNumber),                 OPT},
    {"PCI_DEVICE_FUNCTION_2",   data(AdapterOrder[2].PciDeviceAndFunctionNumber),   OPT},
    {"ADAPTER_FLAGS_2",         data(AdapterOrder[2].AdapterFlags),                 OPT},
    {"PCI_BUS_NUMBER_3",        data(AdapterOrder[3].PciBusNumber),                 OPT},
    {"PCI_DEVICE_FUNCTION_3",   data(AdapterOrder[3].PciDeviceAndFunctionNumber),   OPT},
    {"ADAPTER_FLAGS_3",         data(AdapterOrder[3].AdapterFlags),                 OPT},
    {"RESERVED1",               data(Reserved1),                                    OPT},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pBIOSPage4_t)0)->x, sizeof(((pBIOSPage4_t)0)->x)

ITEM bios_page_4_items[] =
{
    {"BIOS4_REASSIGNMENTBASEWWID_HIGH", data(ReassignmentBaseWWID.High),    0},
    {"BIOS4_REASSIGNMENTBASEWWID_LOW",  data(ReassignmentBaseWWID.Low),     0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pMpi2BiosPage4_t)0)->x, sizeof(((pMpi2BiosPage4_t)0)->x)

ITEM bios_page_4_items2[] =
{
    {"NUM_PHYS",                data(NumPhys),                              0},
    {"RESERVED1",               data(Reserved1),                            OPT},
    {"RESERVED2",               data(Reserved2),                            OPT},
    {"REASSIGN_WWID_LOW_0",     data(Phy[0].ReassignmentWWID.Low),          0},
    {"REASSIGN_WWID_HIGH_0",    data(Phy[0].ReassignmentWWID.High),         0},
    {"REASSIGN_DEVNAME_LOW_0",  data(Phy[0].ReassignmentDeviceName.Low),    0},
    {"REASSIGN_DEVNAME_HIGH_0", data(Phy[0].ReassignmentDeviceName.High),   0},
    {"REASSIGN_WWID_LOW_1",     data(Phy[1].ReassignmentWWID.Low),          0},
    {"REASSIGN_WWID_HIGH_1",    data(Phy[1].ReassignmentWWID.High),         0},
    {"REASSIGN_DEVNAME_LOW_1",  data(Phy[1].ReassignmentDeviceName.Low),    0},
    {"REASSIGN_DEVNAME_HIGH_1", data(Phy[1].ReassignmentDeviceName.High),   0},
    {"REASSIGN_WWID_LOW_2",     data(Phy[2].ReassignmentWWID.Low),          0},
    {"REASSIGN_WWID_HIGH_2",    data(Phy[2].ReassignmentWWID.High),         0},
    {"REASSIGN_DEVNAME_LOW_2",  data(Phy[2].ReassignmentDeviceName.Low),    0},
    {"REASSIGN_DEVNAME_HIGH_2", data(Phy[2].ReassignmentDeviceName.High),   0},
    {"REASSIGN_WWID_LOW_3",     data(Phy[3].ReassignmentWWID.Low),          0},
    {"REASSIGN_WWID_HIGH_3",    data(Phy[3].ReassignmentWWID.High),         0},
    {"REASSIGN_DEVNAME_LOW_3",  data(Phy[3].ReassignmentDeviceName.Low),    0},
    {"REASSIGN_DEVNAME_HIGH_3", data(Phy[3].ReassignmentDeviceName.High),   0},
    {"REASSIGN_WWID_LOW_4",     data(Phy[4].ReassignmentWWID.Low),          0},
    {"REASSIGN_WWID_HIGH_4",    data(Phy[4].ReassignmentWWID.High),         0},
    {"REASSIGN_DEVNAME_LOW_4",  data(Phy[4].ReassignmentDeviceName.Low),    0},
    {"REASSIGN_DEVNAME_HIGH_4", data(Phy[4].ReassignmentDeviceName.High),   0},
    {"REASSIGN_WWID_LOW_5",     data(Phy[5].ReassignmentWWID.Low),          0},
    {"REASSIGN_WWID_HIGH_5",    data(Phy[5].ReassignmentWWID.High),         0},
    {"REASSIGN_DEVNAME_LOW_5",  data(Phy[5].ReassignmentDeviceName.Low),    0},
    {"REASSIGN_DEVNAME_HIGH_5", data(Phy[5].ReassignmentDeviceName.High),   0},
    {"REASSIGN_WWID_LOW_6",     data(Phy[6].ReassignmentWWID.Low),          0},
    {"REASSIGN_WWID_HIGH_6",    data(Phy[6].ReassignmentWWID.High),         0},
    {"REASSIGN_DEVNAME_LOW_6",  data(Phy[6].ReassignmentDeviceName.Low),    0},
    {"REASSIGN_DEVNAME_HIGH_6", data(Phy[6].ReassignmentDeviceName.High),   0},
    {"REASSIGN_WWID_LOW_7",     data(Phy[7].ReassignmentWWID.Low),          0},
    {"REASSIGN_WWID_HIGH_7",    data(Phy[7].ReassignmentWWID.High),         0},
    {"REASSIGN_DEVNAME_LOW_7",  data(Phy[7].ReassignmentDeviceName.Low),    0},
    {"REASSIGN_DEVNAME_HIGH_7", data(Phy[7].ReassignmentDeviceName.High),   0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pLogPage0_t)0)->x, sizeof(((pLogPage0_t)0)->x)

ITEM log_page_0_items[] =
{
    {"ALL", sizeof(ConfigExtendedPageHeader_t), sizeof(LogPage0_t) - sizeof(ConfigExtendedPageHeader_t), 0},
    {0}
};

#undef data
#define data(x) (int)(size_t)&((pMpi2DriverMappingPage0_t)0)->x, sizeof(((pMpi2DriverMappingPage0_t)0)->x)

ITEM driver_mapping_page_0_items2[] =
{
    {"PHYSICAL_IDENTIFIER_LOW",     data(Entry.PhysicalIdentifier.Low),     0},
    {"PHYSICAL_IDENTIFIER_HIGH",    data(Entry.PhysicalIdentifier.High),    0},
    {"MAPPING_INFORMATION",         data(Entry.MappingInformation),         0},
    {"DEVICE_INDEX",                data(Entry.DeviceIndex),                0},
    {"PHYSICAL_BITS_MAPPING",       data(Entry.PhysicalBitsMapping),        0},
    {"RESERVED1",                   data(Entry.Reserved1),                  OPT},
    {0}
};

#undef data
#define data(x) sizeof(x)
#define plus(x,y,z) data(x) + sizeof(((p##x)0)->y) * z

SECTION sections[] =
{
    {"SECTION_GENERAL_DATA",            general_data_items,             data(GeneralData_t),            GEN},
    {"SECTION_MANUFACTURING_PAGE_0",    manufacturing_page_0_items,     data(ManufacturingPage0_t),     0},
    {"SECTION_MANUFACTURING_PAGE_1",    manufacturing_page_1_items,     data(ManufacturingPage1_t),     0},
    {"SECTION_IOC_MFG_PAGE_2",          manufacturing_page_2_items,     data(ManufacturingPage2_SAS_t), MP2},
    {"SECTION_IOC_MFG_PAGE_3",          manufacturing_page_3_items,     data(ManufacturingPage3_SAS_t), 0},
    {"SECTION_MANUFACTURING_PAGE_4",    manufacturing_page_4_items,     data(ManufacturingPage4_t),     0},
    {"SECTION_MANUFACTURING_PAGE_5",    manufacturing_page_5_items,     plus(ManufacturingPage5_t, ForceWWID, 7), 0},
    {"SECTION_MANUFACTURING_PAGE_7",    manufacturing_page_7_items,     plus(ManufacturingPage7_t, ConnectorInfo, 7), 0},
    {"SECTION_IO_UNIT_PAGE_0",          io_unit_page_0_items,           data(IOUnitPage0_t),            0},
    {"SECTION_IO_UNIT_PAGE_1",          io_unit_page_1_items,           data(IOUnitPage1_t),            0},
    {"SECTION_IO_UNIT_PAGE_2",          io_unit_page_2_items,           data(IOUnitPage2_t),            0},
    {"SECTION_IO_UNIT_PAGE_3",          io_unit_page_3_items,           plus(IOUnitPage3_t, GPIOVal, 31), 0},
    {"SECTION_IO_UNIT_PAGE_4",          io_unit_page_4_items,           data(IOUnitPage4_t),            0},
    {"SECTION_IOC_PAGE_0",              ioc_page_0_items,               data(IOCPage0_t),               0},
    {"SECTION_IOC_PAGE_1",              ioc_page_1_items,               data(IOCPage1_t),               0},
    {"SECTION_IOC_PAGE_2",              ioc_page_2_items,               data(IOCPage2_t),               0},
    {"SECTION_IOC_PAGE_3",              ioc_page_3_items,               data(IOCPage3_t),               0},
    {"SECTION_IOC_PAGE_4",              ioc_page_4_items,               data(IOCPage4_t),               0},
    {"SECTION_IOC_PAGE_5",              ioc_page_5_items,               data(IOCPage5_t),               0},
    {"SECTION_IOC_PAGE_6",              ioc_page_6_items,               data(IOCPage6_t),               0},
    {"SECTION_SAS_IO_UNIT_0",           sas_io_unit_page_0_items,       plus(SasIOUnitPage0_t, PhyData, 7), EXT},
    {"SECTION_SAS_IO_UNIT_1",           sas_io_unit_page_1_items,       plus(SasIOUnitPage1_t, PhyData, 7), EXT},
    {"SECTION_SAS_IO_UNIT_2",           sas_io_unit_page_2_items,       data(SasIOUnitPage2_t),         EXT},
    {"SECTION_SAS_IO_UNIT_3",           sas_io_unit_page_3_items,       data(SasIOUnitPage3_t),         EXT},
    {"SECTION_SAS_EXPANDER_0",          sas_expander_page_0_items,      data(SasExpanderPage0_t),       EXT},
    {"SECTION_SAS_EXPANDER_1",          sas_expander_page_1_items,      data(SasExpanderPage1_t),       EXT},
    {"SECTION_SAS_DEVICE_0",            sas_device_page_0_items,        data(SasDevicePage0_t),         EXT},
    {"SECTION_SAS_DEVICE_1",            sas_device_page_1_items,        data(SasDevicePage1_t),         EXT},
    {"SECTION_SAS_DEVICE_2",            sas_device_page_2_items,        data(SasDevicePage2_t),         EXT},
    {"SECTION_SAS_PHY_0",               sas_phy_page_0_items,           data(SasPhyPage0_t),            EXT},
    {"SECTION_SAS_PHY_1",               sas_phy_page_1_items,           data(SasPhyPage1_t),            EXT},
    {"SECTION_SAS_ENCLOSURE_0",         sas_enclosure_page_0_items,     data(SasEnclosurePage0_t),      EXT},
    {"SECTION_PERSISTENT_ID",           sas_persistent_id_items,        data(PersistentId_SAS_t),       EXT | PID},
    {"SECTION_RAID_VOL_PAGE_0",         raid_volume_page_0_items,       data(RaidVolumePage0_t),        0},
    {"SECTION_RAID_VOL_PAGE_1",         raid_volume_page_1_items,       data(RaidVolumePage1_t),        0},
    {"SECTION_RAID_PHYS_DISK_PAGE_0",   raid_physdisk_page_0_items, data(RaidPhysDiskPage0_t),      0},
    {"SECTION_RAID_PHYS_DISK_PAGE_1",   raid_physdisk_page_1_items, data(RaidPhysDiskPage1_t),      0},
    {"SECTION_BIOS_1",                  bios_page_1_items,              data(BIOSPage1_t),              0},
    {"SECTION_BIOS_2",                  bios_page_2_items,              data(BIOSPage2_t),              0},
    {"SECTION_BIOS_4",                  bios_page_4_items,              data(BIOSPage4_t),              0},
    {"SECTION_LOG_0",                   log_page_0_items,               data(LogPage0_t),               EXT},
    {0}
};

SECTION sections2[] =
{
    {"SECTION_GENERAL_DATA",            general_data_items,             data(GeneralData_t),                GEN},
    {"SECTION_MANUFACTURING_PAGE_0",    manufacturing_page_0_items,     data(ManufacturingPage0_t),         0},
    {"SECTION_MANUFACTURING_PAGE_1",    manufacturing_page_1_items,     data(ManufacturingPage1_t),         0},
    {"SECTION_IOC_MFG_PAGE_2",          manufacturing_page_2_items2,    data(ManufacturingPage2_SAS2_t),    MP2},
    {"SECTION_IOC_MFG_PAGE_3",          manufacturing_page_3_items2,    data(ManufacturingPage3_SAS2_t),    0},
    {"SECTION_MANUFACTURING_PAGE_4",    manufacturing_page_4_items2,    data(Mpi2ManufacturingPage4_t),     0},
    {"SECTION_MANUFACTURING_PAGE_5",    manufacturing_page_5_items2,    plus(Mpi2ManufacturingPage5_t, Phy, 15), 0},
    {"SECTION_IOC_MFG_PAGE_6",          manufacturing_page_6_items2,    data(ManufacturingPage6_SAS2_t),    0},
    {"SECTION_MANUFACTURING_PAGE_7",    manufacturing_page_7_items,     plus(ManufacturingPage7_t, ConnectorInfo, 15), 0},
    {"SECTION_MANUFACTURING_PAGE_8",    manufacturing_page_8_items2,    data(ManufacturingPage8_t),         0},
    {"SECTION_IOC_MFG_PAGE_9",          manufacturing_page_9_items2,    data(ManufacturingPage9_SAS2_t),    0},
    {"SECTION_MANUFACTURING_PAGE_10",   manufacturing_page_10_items2,   data(ManufacturingPage10_t),        0},
    {"SECTION_IOC_MFG_PAGE_11",         manufacturing_page_11_items2,   data(ManufacturingPage11_SAS2_t),   0},
    {"SECTION_IOC_MFG_PAGE_12",         manufacturing_page_12_items2,   data(ManufacturingPage12_SAS2_t),   0},
    {"SECTION_IOC_MFG_PAGE_13",         manufacturing_page_13_items2,   data(ManufacturingPage13_SAS2_t),   0},
    {"SECTION_IO_UNIT_PAGE_1",          io_unit_page_1_items,           data(IOUnitPage1_t),                0},
    {"SECTION_IOC_PAGE_1",              ioc_page_1_items2,              data(Mpi2IOCPage1_t),               0},
    {"SECTION_IOC_PAGE_8",              ioc_page_8_items2,              data(Mpi2IOCPage8_t),               0},
    {"SECTION_BIOS_1",                  bios_page_1_items,              data(BIOSPage1_t),                  0},
    {"SECTION_BIOS_2",                  bios_page_2_items2,             data(Mpi2BiosPage2_t),              0},
    {"SECTION_BIOS_3",                  bios_page_3_items2,             data(Mpi2BiosPage3_t),              0},
    {"SECTION_BIOS_4",                  bios_page_4_items2,             plus(Mpi2BiosPage4_t, Phy, 15),     0},
    {"SECTION_SAS_IO_UNIT_1",           sas_io_unit_page_1_items2,      plus(Mpi2SasIOUnitPage1_t, PhyData, 15), EXT},
    {"SECTION_SAS_IO_UNIT_4",           sas_io_unit_page_4_items2,      plus(Mpi2SasIOUnitPage4_t, PHY, 3), EXT},
    {"SECTION_DRIVER_MAPPING_0",        driver_mapping_page_0_items2,   data(Mpi2DriverMappingPage0_t),     EXT},
    {0}
};

#undef data
#undef plus

#define NVDATA_SIZE                 4096
#define NUM_CONFIG_PAGES            (sizeof(sections)/sizeof(sections[0]) - 2)  // ignore sentinel and GEN
#define NUM_CONFIG_PAGES2           (sizeof(sections2)/sizeof(sections2[0]) - 2)  // ignore sentinel and GEN


#endif


char *
getSasProductId(char *nvdata)
{
    CONFIG_DIR_HEADER   *cdh;
    CONFIG_PROD_ID      *cpi;

    cdh = (CONFIG_DIR_HEADER *)nvdata;

    cpi = (CONFIG_PROD_ID *)((char *)cdh + cdh->CdhSize * 4);

    return (char *)cpi->ProductId;
}


#define EXPANDER_LOG_SIGNATURE  0x74655065


#define UART_DEFAULT_BUF_SIZE  1024    // 1MB

#if DOS || EFI
#define UART_MAX_BUF_SIZE     1024     // 1MB - limited by mpt_shared_t's scratch[] buffer in mpt.c
#else
#define UART_MAX_BUF_SIZE     16384    // 16MB
#endif

int singlePortGetTargetAndSlotId(LsiDisk_t *LsiDisks)
{
    MPT_PORT         *port = NULL;
    int              numPorts = 0;
    int              portIdx = 0;
    
    int              bus;
    int              target;
    int              lun;
    unsigned char    inq[36];
    char             buf[32];
    int              i;
    int              version;
    int              max_lun;
    SCSIDevicePage0_t    SCSIDevicePage0;
    FCDevicePage0_t      FCDevicePage0;
    SasDevicePage0_t     SASDevicePage0;
    int                  b_t;
    int                  dev_handle;
    int                  address;
    int                  diskNum = 0;
    MPT_PORT            *mptPorts[NUM_PORTS];
    if(!LsiDisks) {
        syslog(LOG_INFO,"Invalid param for lsidisks is null.");
        return diskNum;
    }
    
    numPorts = findPorts(mptPorts);
    for(portIdx = 0; portIdx < numPorts; portIdx++)
    {
        port = mptPorts[portIdx];
        if(NULL != port)
        {
            for (bus = 0; bus < port->maxBuses; bus++)
            {
                for (target = port->minTargets; target < port->maxTargets; target++)
                {
                    max_lun = 1;

                    for (lun = 0; lun < max_lun; lun++)
                    {
                        if (doInquiry(port, bus, target, lun, inq, sizeof inq) != 1)
                        {
                            if (errno == EFAULT)
                                return 0;
                            if (lun == 0)
                                break;
                            else
                                continue;
                        }
                        
                        if (port->pidType == MPI_FW_HEADER_PID_TYPE_SAS)
                        {
                            if (port->mptVersion >= MPI2_VERSION_02_00)
                            {
                                if (mapBusTargetToDevHandle(port, bus, target, &dev_handle) != 1)
                                    dev_handle = 0;
                                address = MPI2_SAS_DEVICE_PGAD_FORM_HANDLE + dev_handle;
                            }
                            else
                            {
                                b_t = (bus << 8) + target;
                                address = (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID
                                           <<MPI_SAS_DEVICE_PGAD_FORM_SHIFT) + b_t;
                            }
                            if(!strcmp("Disk", deviceType[inq[0] & 0x1f]))
                            {
                                if (getConfigPage(port, MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE, 0, address,
                                                  &SASDevicePage0, sizeof SASDevicePage0) == 1)
                                {
                                    /*sprintf(LsiDisks[diskNum].sasAddr, "0x%08x%08x",
                                    get32(SASDevicePage0.SASAddress.High),
                                    get32(SASDevicePage0.SASAddress.Low)
                                    );*/
                                     LsiDisks[diskNum].target_id = target;
                                     LsiDisks[diskNum].slot = SASDevicePage0.PhyNum;
                                     diskNum++;
                                     if(diskNum >= MAX_DISK_NUM)
                                     {
                                        syslog(LOG_INFO,"The disk num is up to the limit.");
                                        closePorts(numPorts,mptPorts);
                                        return diskNum;
                                     }
                                }
                            }
                        }
                    } /* next lun */
                } /* next target */
            } /* next bus */
       }
    }
    if(0 == numPorts)
    {
        if((access(MEGA_DEVICE,F_OK))!=-1)   
        {
            diskNum = getTargetandSlotIdFromMegaraid(LsiDisks);
        }
        else
        {
            syslog(LOG_INFO,"Failed to find megaraid sas device node.");
        }
    }      
        
    closePorts(numPorts,mptPorts);
    return diskNum;
}


/* vi: set sw=4 ts=4 sts=4 et :iv */

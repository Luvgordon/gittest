/*
 *	Main program.
 *
 *	Copyright (c) 2007 by Jefferson Ogata
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; see the file COPYING.  If not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/********************************************************************

megactl/megasasctl

Program to do the things you wish dellmgr or MegaCli could do, e.g.
report device log pages, run device self tests, report disk error
counts without having to pop in and out of countless dellmgr menus,
and actually document the adapter configuration concisely (dellmgr
gives you no way to do this).

Author: Jefferson Ogata (JO317) <ogata@antibozo.net>
Date: 2006/01/23

Version 0.4.0 major changes, including SAS support: 2007/08/20

TODO:

Other log page parsers.

Cleaner log page output.

Fixes for 64-bit systems. Currently builds only with -m32.

********************************************************************/
#include    "lsiutil.h"
#include	"mega.h"
#include	<stdio.h>
#include	<stdlib.h>
#include	<stdarg.h>
#include	<unistd.h>
#include	<errno.h>
#include	<string.h>
#include	<memory.h>
#include	<malloc.h>
#include	<fcntl.h>
#include	<signal.h>
#include	<ctype.h>
#include    <syslog.h>
#include	<sys/types.h>
#include	<sys/ioctl.h>
#include	<scsi/scsi.h>


static int doIoctl (struct mega_adapter_path *adapter, void *u)
{
    switch (adapter->type)
    {
     case MEGA_ADAPTER_V2:
     case MEGA_ADAPTER_V34:
	return ioctl (adapter->fd, _IOWR(MEGAIOC_MAGIC, 0, struct uioctl_t), u);
     case MEGA_ADAPTER_V5:
	return ioctl (adapter->fd, MEGASAS_IOC_FIRMWARE, u);
    }
    return -1;
}


static int sasCommand (struct mega_adapter_path *adapter, void *data, uint32_t len, uint32_t opcode, uint16_t flags, void *mbox, uint32_t mboxlen)
{
    struct megasas_iocpacket	u;
    struct megasas_dcmd_frame	*f = (struct megasas_dcmd_frame *) &u.frame;

    memset (&u, 0, sizeof u);
    u.host_no = (u16) adapter->adapno;

    f->cmd = MFI_CMD_DCMD;
    f->flags = (u16) flags;
    f->opcode = (u32) opcode;

    if ((data != NULL) && (len > 0))
    {
	u.sgl_off = ((void *) &f->sgl) - ((void *) f);
	u.sge_count = 1;
	u.sgl[0].iov_base = data;
	u.sgl[0].iov_len = len;
	f->sge_count = 1;
	f->data_xfer_len = (u32) len;
	//f->sgl.sge32[0].phys_addr = (u32) data;
	//f->sgl.sge32[0].length = (u32) len;
    }

    if (mbox != NULL)
	memcpy (&f->mbox, mbox, mboxlen);

    if (doIoctl (adapter, &u) < 0)
    {
	    return -1;
    }
    return f->cmd_status;
}


int megaSasGetDiskInfo (struct mega_adapter_path *adapter, uint8_t target, struct mega_physical_disk_info_sas *data)
{
    uint8_t 			mbox[0xc];

    memset (&mbox, 0, sizeof mbox);
    mbox[0] = target;
    return sasCommand (adapter, data, sizeof (*data), 0x02020000, MFI_FRAME_DIR_READ, mbox, sizeof mbox);
}

int megaSasAdapterPing (int fd, uint8_t adapno)
{
    struct mega_adapter_path	adapter;
    unsigned char		data[0xc4];
    adapter.fd = fd;
    adapter.adapno = adapno;
    adapter.type = MEGA_ADAPTER_V5;
    return sasCommand (&adapter, data, sizeof data, 0x04060100, MFI_FRAME_DIR_READ, NULL, 0);
}


static int driverQuery (int fd, uint16_t adap, void *data, uint32_t len, uint8_t subop)
{
    struct uioctl_t		u;
    struct mega_adapter_path	adapter;

    memset (&u, 0, sizeof u);
    u.outlen = len;
    u.ui.fcs.opcode = M_RD_DRIVER_IOCTL_INTERFACE;
    u.ui.fcs.subopcode = subop;
    u.ui.fcs.length = len;
    u.data = data;
    if (data)
	memset (data, 0, len);

    adapter.fd = fd;
    adapter.type = MEGA_ADAPTER_V34;
    if (doIoctl (&adapter, &u) < 0)
    {
	    return -1;
    }
    return 0;
}


int megaGetNumAdapters (int fd, uint32_t *numAdapters, int sas)
{
    if (sas)
    {
    	uint8_t		k;
    	for (k = 0; k < 64; ++k)
        {
    	    if (megaSasAdapterPing (fd, k) >= 0)
            {
    		    *numAdapters = k;
                break;
            }
        }
    	return 0;
    }
    else
    {
	    return driverQuery (fd, 0, numAdapters, sizeof (*numAdapters), 'm');
    }
}




int megaSasGetDeviceList (struct mega_adapter_path *adapter, struct mega_device_list_sas **data)
{
    unsigned char		buf[0x20];
    uint32_t			len;

    if (sasCommand (adapter, buf, sizeof buf, 0x02010000, MFI_FRAME_DIR_READ, NULL, 0) < 0)
	    return -1;
    len = ((struct mega_device_list_sas *) buf)->length;
    if ((*data = (struct mega_device_list_sas *) malloc (len)) == NULL)
    {
	    return -1;
    }
    return sasCommand (adapter, *data, len, 0x02010000, MFI_FRAME_DIR_READ, NULL, 0);
}

struct adapter_config * getAdapterPhysicalDiskList (int fd, uint8_t adapno, int sas)
{
    int k;    
    struct adapter_config *a;

    if ((a = (struct adapter_config *) malloc (sizeof (*a))) == NULL)
    {
        return NULL;
    }
    memset (a, 0, sizeof (*a));

    a->target.fd = fd;
    a->target.adapno = adapno;
    a->target.type = MEGA_ADAPTER_V5;
    a->is_sas = sas;
    
    if (megaSasGetDeviceList (&a->target, &(a->q.v5.device)) < 0)
    {
        if(a->q.v5.device)
            free(a->q.v5.device);
        free(a);
        return NULL;
    }
	    
    return a;
}


int getTargetandSlotIdFromMegaraid(LsiDisk_t *LsiDisks)
{
    int				k;
    int				fd;
    
	struct adapter_config		*a;
    uint32_t			numAdapters;
    char			*device = MEGA_DEVICE;
    int				sas = 1;
    struct mega_device_list_sas		*disklist;
    int diskNum = 0;

    if ((fd = open (device, O_RDONLY)) < 0)
    {
    	syslog(LOG_INFO, "unable to open device %s: %s\n", device, strerror (errno));
    	return 0;
    }

    if (megaGetNumAdapters (fd, &numAdapters, sas) < 0)
    {
    	syslog(LOG_INFO, "unable to determine number of adapters.");
    	return 0;
    }

    if ((a = getAdapterPhysicalDiskList (fd, numAdapters, sas)) == NULL)
    {
        syslog(LOG_INFO, "cannot read adapter configuration:");
        return 0;
    }
    
    disklist = a->q.v5.device;
    if(NULL != disklist)
    {
        for(k = 0; k < disklist->num_devices; k++)
        {
            struct mega_device_entry_sas *disk = &disklist->device[k];
            if(disk->type == INQ_DASD)
            {
                LsiDisks[diskNum].target_id = disk->device_id;
                LsiDisks[diskNum].slot = disk->slot;
                diskNum++;
                if(diskNum >= MAX_DISK_NUM)
                {
                    syslog(LOG_INFO,"The disk num is up to the limit.");
                    break;
                }
            }
        }
    }

    if(NULL != disklist)
    {
        free(disklist);
        disklist = NULL;
    }
    if(NULL != a)
    {
        free(a);
        a = NULL;
    }

    close(fd);
    return diskNum;
}



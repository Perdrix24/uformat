/* 
 * uFormat v1.6 SCSIDRV routines
 *
 * Copyright (c) 2022 Claude Labelle
 *
 * based on demo of SCSIDRV by Roger Burrows for MyAtari magazine
 * thanks to Christian Zietz for help
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "scsidefs.h"

#define BYTES_PER_SECTOR 	512
#define FA_VOL 						0x08		/* volume label attribute */

typedef struct {
	char vendor[9];
	char product[17];
	char revision[5];
	char asc; /* additional sense code */
}driveinfo;

typedef struct {
	char code; /* unformatted, formatted, no disk */
	char capdesc[8]; /* current capacity descritor */
	char capdescDD[8]; /* 720K capacity descritor */
	char capdescHD[8]; /* 1.44MB capacity descritor */
	int wp; /* 1 if write protected */
}diskinfo;

/*
 *	globals
 */
tpScsiCall scsicall;
#ifdef LOGCMDS	
FILE *fp;
#endif

/*
 *	function prototypes
 */
tpScsiCall init_scsi(void);
int find_usb_bus(tBusInfo *businfo);
tHandle find_drive(tBusInfo *businfo, driveinfo *info);
long get_capacities(tHandle handle, diskinfo *info);
long format_floppy(tHandle handle, char *capdesc, void (*updatebar)(int track));
long init_floppy(tHandle handle, int disktype, char *label);
long write_boot_sector(tHandle handle, int disktype, int *spf);
long get_write_protect(tHandle handle, diskinfo *info);
long media_changed(tHandle handle);
long close_handle(tHandle handle);
long drive_ready(tHandle handle);
void scan_busses(void);
LONG scsi_inquiry(tHandle handle,char *inqdata,char *reqbuff);
LONG scsi_read_format_capacities(tHandle handle,char *capdata,char *reqbuff);
LONG scsi_format_unit(tHandle handle,int track,int side,char *desc,char *reqbuff);
LONG scsi_write10(tHandle handle,unsigned long sector,unsigned short len, char *buf,char *reqbuff);
LONG scsi_mode_sense10(tHandle handle,char pagecode,char subpagecode,unsigned short len,char *buf,char *reqbuff);
LONG scsi_test_unit_ready(tHandle handle);

int find_usb_bus(tBusInfo *businfo)
{
void *oldstack;
int found_USB_bus = 0;
LONG rc;

	/* Find USB bus */

	oldstack = (void *)Super(NULL);

	rc = scsicall->InquireSCSI(cInqFirst,businfo);
	while(rc == 0) {
		if (strcmp(businfo->BusName, "USB Mass Storage") == 0) {
			found_USB_bus = 1;
			break;
		}
		rc = scsicall->InquireSCSI(cInqNext,businfo);
	}

	Super(oldstack);
	if (found_USB_bus)
		return 1;
	else 
		return 0;
}


tHandle find_drive(tBusInfo *businfo, driveinfo *info)
{
LONG rc;

int found_USB_floppy = 0;
char inqdata[36];
tDevInfo Dev;
tHandle handle;
ULONG MaxLen; 
char reqbuff[18];
	
	memset(info, 0, sizeof(driveinfo));
	
	/* Find floppy drive among devices on USB bus */
	
	rc = scsicall->InquireBus(cInqFirst,businfo->BusNo,&Dev);

	while (rc == 0L) {
		rc = scsicall->Open(businfo->BusNo,&Dev.SCSIId,&MaxLen);
		if (rc < 0L) {
			rc = scsicall->InquireBus(cInqNext,businfo->BusNo,&Dev);
			continue;
		}
		handle = (tHandle) rc;

		memset(inqdata,0,sizeof(inqdata));
		memset(reqbuff, 0, sizeof(reqbuff));
		rc = scsi_inquiry(handle,inqdata,reqbuff);

		if (rc != 0L) {
			if (rc > 0)
				info->asc = reqbuff[12]; /* additional sense code */
			scsicall->Close(handle);
		}else {
		 	if ((inqdata[0]&0x1f) != 0)
			{
				scsicall->Close(handle);
	 			rc = scsicall->InquireBus(cInqNext,businfo->BusNo,&Dev);
		 		continue;
	 		}
		 	if ((inqdata[3] & 0x0F) != 1)
			{
				scsicall->Close(handle);
				rc = scsicall->InquireBus(cInqNext,businfo->BusNo,&Dev);
				continue;
			}	 	
			strncpy(info->vendor,inqdata+8,8);
			info->vendor[8] = '\0';
			strncpy(info->product,inqdata+16,16);
			info->product[16] = '\0';
			strncpy(info->revision,inqdata+32,4);
			info->revision[4] = '\0';

			found_USB_floppy = 1;
			break;
		}
		rc = scsicall->InquireBus(cInqNext,businfo->BusNo,&Dev);
	}	
	if (found_USB_floppy) {
#ifdef LOGCMDS
	fp = fopen("uformat.txt", "a");
	fprintf(fp, "INQUIRY rc:%ld vendor/product: %s %s\n", rc, info->vendor, info->product); 
	fclose(fp);
#endif
		return handle;
	}
	else
		return 0;
}

long get_capacities(tHandle handle, diskinfo *info)
{
char capdata[252]; /* capacity decriptors */
char caplistlen;
char capdescbuf[8];
char reqbuff[18];
long rc;
int i, retry;

	memset(info, 0, sizeof(diskinfo));
	retry = 0;
	do {
		memset(reqbuff, 0, sizeof(reqbuff));
		memset(capdata, 0, sizeof(capdata));
 		rc = scsi_read_format_capacities(handle, capdata, reqbuff);
		retry++;
	}while (rc !=0L && retry < 3);
	if (rc == 0L) {
		info->code = (capdata[8] & 0x03);
		caplistlen = capdata[3];
		memcpy(info->capdesc,capdata+4,8); /* current disk formattable capacity descriptor */
		for (i = 12; i < caplistlen; i += 8) {
			memcpy(capdescbuf,capdata+i,8); 
			if (capdescbuf[2] == 0x0B && capdescbuf[3] == 0x40 && capdescbuf[6] == 0x02  && capdescbuf[7] == 0x00)
				memcpy(info->capdescHD, capdescbuf, 8);
			else if (capdescbuf[2] == 0x05 && capdescbuf[3] == 0xA0 && capdescbuf[6] == 0x02  && capdescbuf[7] == 0x00)
				memcpy(info->capdescDD, capdescbuf, 8);				
		}
		/* if no capdescDD or capdescHD, use capdesc. */
		memset(capdescbuf,0,8);
		if (memcmp(capdescbuf, info->capdescHD, 8) == 0 && memcmp(capdescbuf, info->capdescDD, 8) == 0) {
			if (info->capdesc[2] == 0x0B && info->capdesc[3] == 0x40 && info->capdesc[6] == 0x02  && info->capdesc[7] == 0x00)
				memcpy(info->capdescHD, info->capdesc, 8);
			else if (info->capdesc[2] == 0x05 && info->capdesc[3] == 0xA0 && info->capdesc[6] == 0x02  && info->capdesc[7] == 0x00)
				memcpy(info->capdescDD, info->capdesc, 8);							
		}
	}else if (rc > 0) 
		rc = reqbuff[2]; /* sense key */
		
#ifdef LOGCMDS
	fp = fopen("uformat.txt", "a");
	fprintf(fp, "READ FORMAT CAPACITIES rc:%ld SenseKey:%02x ASC:%02x ASCQ:%02x\n", rc, reqbuff[2], reqbuff[12], reqbuff[13]); 
	fprintf(fp, "current capacity descriptor: %02x %02x %02x %02x %02x %02x %02x %02x\n", \
		info->capdesc[0], info->capdesc[1],info->capdesc[2],info->capdesc[3],info->capdesc[4],info->capdesc[5],info->capdesc[6],info->capdesc[7]);
	fprintf(fp, "HD capacity descriptor: %02x %02x %02x %02x %02x %02x %02x %02x\n", \
		info->capdescHD[0], info->capdescHD[1],info->capdescHD[2],info->capdescHD[3],info->capdescHD[4],info->capdescHD[5],info->capdescHD[6],info->capdescHD[7]);
	fprintf(fp, "DD capacity descriptor: %02x %02x %02x %02x %02x %02x %02x %02x\n", \
		info->capdescDD[0], info->capdescDD[1],info->capdescDD[2],info->capdescDD[3],info->capdescDD[4],info->capdescDD[5],info->capdescDD[6],info->capdescDD[7]);
	fclose(fp);
#endif

	return rc;
}

long format_floppy(tHandle handle, char *capdesc, void (*updatebar)(int track))
{
	long rc;
	char reqbuff[18];
	int i;
		
	for (i = 0; i < 80; i++) {
		memset(reqbuff, 0, sizeof(reqbuff));
		rc = scsi_format_unit(handle, i, 0, capdesc,reqbuff);
		if (rc != 0L) {
			if (rc > 0)
				rc = reqbuff[2]; /* sense key */
			return rc;
		}
		memset(reqbuff, 0, sizeof(reqbuff));
		rc = scsi_format_unit(handle, i, 1, capdesc,reqbuff);
		if (rc != 0L) {
			if (rc > 0)
				rc = reqbuff[2]; /* sense key */
			return rc;
		}
		updatebar(i);
	}
	
	return 0L;
}

long init_floppy(tHandle handle, int disktype, char *label)
{
	char *fat;
	char *rootdir;
	long rc;
	char reqbuff[18];
	int spf; /* sectors per fat */
	int rdlen; /* root directory length in sectors */
	char *p;
	int i;
		
	/* write boot sector */
	rc = write_boot_sector(handle, disktype, &spf);
	if (rc != 0L)
		return rc;
		
	/* write FATs */
	fat = malloc(spf * BYTES_PER_SECTOR); /* sectors per fat * sector size */
	memset(fat, 0x00, spf * BYTES_PER_SECTOR);
	fat[0] = 0xf9;
	fat[1] = 0xff;
	fat[2] = 0xff;
	memset(reqbuff, 0, sizeof(reqbuff));
	rc = scsi_write10(handle,1,spf * BYTES_PER_SECTOR,fat,reqbuff);
	if (rc != 0L) {
		if (rc > 0)
			/* return sense key, except if sense code is 0x54, return sense code */
			rc = (reqbuff[12] == 0x54)?reqbuff[12]:reqbuff[2];
		free(fat);
		return rc;
	}
	memset(reqbuff, 0, sizeof(reqbuff));
	rc = scsi_write10(handle,spf+1,spf * BYTES_PER_SECTOR,fat,reqbuff);
	if (rc != 0L) {
		if (rc > 0)
			/* return sense key, except if sense code is 0x54, return sense code */
			rc = (reqbuff[12] == 0x54)?reqbuff[12]:reqbuff[2];
		free(fat);
		return rc;
	}
	free(fat);
	
	/* write root directory */
	rdlen = (disktype == 4)? 14 : 7; /* 7 for 720K disk */
	rootdir = malloc(rdlen * BYTES_PER_SECTOR);
	memset(rootdir, 0x00, rdlen * BYTES_PER_SECTOR);
	
	/* volume label in root directory */
	memset(rootdir, ' ', 11);
	for (p = rootdir, i = 0; label[i]; i++)
		*p++ = label[i];
	rootdir[11]= FA_VOL;

	memset(reqbuff, 0, sizeof(reqbuff));
	rc = scsi_write10(handle,spf*2+1,rdlen * BYTES_PER_SECTOR,rootdir,reqbuff);
	free(rootdir);
	if (rc != 0L) {
		if (rc > 0)
			/* return sense key, except if sense code is 0x54, return sense code */
			rc = (reqbuff[12] == 0x54)?reqbuff[12]:reqbuff[2];
		return rc;
	}
	
	/* Report media change */
	scsicall->Error(handle, cErrWrite, cErrMediach);
			
	return 0L;
}

long write_boot_sector(tHandle handle, int disktype, int *spf)
{
	char bootbuf[BYTES_PER_SECTOR];
	int HDonST = 0; /* a 1.44MB on a ST when the high byte of the '_FDC' cookie lacks a value of 1 */
	char reqbuff[18];
	long rc, value;
	/* FILE *fp; */

	if (disktype == 4) {
		if (getcookie(0x5F464443L,&value)) { /* '_FDC' */
			HDonST = (value>>24)? 0 : 1;
		}else {
			HDonST = 1;
		}
	}

	/* write boot sector */
	if (HDonST)
		disktype = 3; /* 4 not available, use 720K diskette for prototype boot sector */
	
	memset(bootbuf, 0, sizeof(bootbuf));	
	Protobt(bootbuf, 0x10000001L, disktype, 0);
	bootbuf[0] = 0xe9; /* DOS compatibility */
	
	if (HDonST) {
		bootbuf[17] = 0xE0; /* NDIRS */
		bootbuf[19] = 0x40; /* NSECTS */
		bootbuf[20] = 0x0B; /* NSECTS */
		bootbuf[24] = 0x12; /* SPT */	
	}
		
	memset(reqbuff, 0, sizeof(reqbuff));
	rc = scsi_write10(handle,0,BYTES_PER_SECTOR,bootbuf,reqbuff);
	if (rc != 0L) {
		if (rc > 0)
			/* return sense key, except if sense code is 0x54, return sense code */
			rc = (reqbuff[12] == 0x54)?reqbuff[12]:reqbuff[2];
		return rc;
	}
	
	*spf = bootbuf[22];
	return 0L;
}

long get_write_protect(tHandle handle, diskinfo *info)
{
	long rc;
	char reqbuff[18];
	char buf[8];

	memset(buf, 0, sizeof(buf));
	memset(reqbuff, 0, sizeof(reqbuff));
	rc = scsi_mode_sense10(handle,0x3F,0x00,8,buf,reqbuff);
	if (rc != 0L) {
		if (rc > 0)
			rc = reqbuff[2]; /* sense key */
		return rc;
	}
	info->wp = (buf[3] & 0x80)? 1:0;
	return rc;
}

long media_changed(tHandle handle)
{
	/* Retrieve media change */
	return scsicall->Error(handle, cErrRead, cErrMediach);
}

long close_handle(tHandle handle)
{
	return scsicall->Close(handle);
}

long drive_ready(tHandle handle)
{
	return scsi_test_unit_ready(handle);
}

tpScsiCall init_scsi(void)
{
long value;

 	scsicall = NULL;
	if (getcookie(0x53435349L,&value)) {			/* 'SCSI' */
		scsicall = (tpScsiCall)value;
		if (scsicall->Version < SCSIRevision)
			scsicall = NULL;
	}
	return scsicall;
	
}

LONG scsi_inquiry(tHandle handle,char *inqdata,char *reqbuff)
{
tSCSICmd cmd;
BYTE cdb[12] = { 0x12, 0, 0, 0, 36, 0, 0, 0, 0, 0, 0, 0 };

	cmd.Handle = handle;
	cmd.Cmd = cdb;
	cmd.CmdLen = 12;
	cmd.Buffer = inqdata;
	cmd.TransferLen = 36;
	cmd.SenseBuffer = reqbuff;
	cmd.Timeout = 200;			/* i.e. 1 second - generous :-) */
	cmd.Flags = 0;

	return scsicall->In(&cmd);
}

LONG scsi_read_format_capacities(tHandle handle,char *capdata,char *reqbuff)
{
tSCSICmd cmd;
BYTE cdb[12] = { 0x23, 0, 0, 0, 0, 0, 0, 0, 252, 0, 0, 0 };

	cmd.Handle = handle;
	cmd.Cmd = cdb;
	cmd.CmdLen = 12;
	cmd.Buffer = capdata;
	cmd.TransferLen = 252;
	cmd.SenseBuffer = reqbuff;
	cmd.Timeout = 200;			/* i.e. 1 second - generous :-) */
	cmd.Flags = 0;

	return scsicall->In(&cmd);
}

LONG scsi_format_unit(tHandle handle,int track,int side,char *desc,char *reqbuff)
{
tSCSICmd cmd;
BYTE cdb[12] = { 0x04, 23, 0, 0, 0, 0, 0, 0, 12, 0, 0, 0 };
BYTE header[4] = { 0, 0, 0, 8 };
char parms[12];
long rc;

	cdb[2] = track;
	header[1] = (side == 0)?176:177;
	memcpy(parms,header,4);
	memcpy(parms+4,desc,8);
	parms[8] = 0;

	cmd.Handle = handle;
	cmd.Cmd = cdb;
	cmd.CmdLen = 12;
	cmd.Buffer = parms;
	cmd.TransferLen = 12;
	cmd.SenseBuffer = reqbuff;
	cmd.Timeout = 400;			/* i.e. 2 seconds - generous :-) */
	cmd.Flags = 0;

	rc = scsicall->Out(&cmd);
#ifdef LOGCMDS
	fp = fopen("uformat.txt", "a");
	fprintf(fp, "FORMAT UNIT rc:%ld track %d side %d SenseKey:%02x ASC:%02x ASCQ:%02x\n", rc, track, side, reqbuff[2], reqbuff[12], reqbuff[13]); 
	fclose(fp);
#endif
	return rc;
}

LONG scsi_write10(tHandle handle,unsigned long sector,unsigned short len,char *buf,char *reqbuff)
{
tSCSICmd cmd;
BYTE cdb[12] = { 0x2A, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
long rc;

	cdb[2] = ((unsigned char) (sector >> 24)) & 0xff;
	cdb[3] = ((unsigned char) (sector >> 16)) & 0xff;
	cdb[4] = ((unsigned char) (sector >> 8)) & 0xff;
	cdb[5] = (unsigned char) sector & 0xff;
	
	cdb[7] = ((unsigned char) ((len/BYTES_PER_SECTOR) >> 8)) & 0xff;
	cdb[8] = (unsigned char) (len/BYTES_PER_SECTOR) & 0xff;

	cmd.Handle = handle;
	cmd.Cmd = cdb;
	cmd.CmdLen = 12;
	cmd.Buffer = buf;
	cmd.TransferLen = len;
	cmd.SenseBuffer = reqbuff;
	cmd.Timeout = 400;			/* i.e. 2 seconds - generous :-) */
	cmd.Flags = 0;

	rc = scsicall->Out(&cmd);
#ifdef LOGCMDS
	fp = fopen("uformat.txt", "a");
	fprintf(fp, "WRITE10 number of logical blocks msb:%02x lsb:%02x\n", cdb[7], cdb[8]); 
	fprintf(fp, "WRITE10 rc:%ld SenseKey:%02x ASC:%02x ASCQ:%02x\n", rc, reqbuff[2], reqbuff[12], reqbuff[13]); 
	fclose(fp);
#endif	
	return rc;
}

LONG scsi_mode_sense10(tHandle handle,char pagecode,char subpagecode,unsigned short len,char *buf,char *reqbuff)
{
tSCSICmd cmd;
BYTE cdb[12] = { 0x5A, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
long rc;

	cdb[2] = pagecode;
	cdb[3] = subpagecode;
	cdb[7] = ((unsigned char) (len >> 8)) & 0xff;
	cdb[8] = (unsigned char) len & 0xff;

	cmd.Handle = handle;
	cmd.Cmd = cdb;
	cmd.CmdLen = 12;
	cmd.Buffer = buf;
	cmd.TransferLen = len;
	cmd.SenseBuffer = reqbuff;
	cmd.Timeout = 200;			/* i.e. 1 second - generous :-) */
	cmd.Flags = 0;

	rc = scsicall->In(&cmd);
#ifdef LOGCMDS
	fp = fopen("uformat.txt", "a");
	fprintf(fp, "MODE SENSE 10 rc:%ld SenseKey:%02x ASC:%02x ASCQ:%02x\n", rc, reqbuff[2], reqbuff[12], reqbuff[13]); 
	fprintf(fp, "buf: %02x %02x %02x %02x %02x %02x %02x %02x\n", \
		buf[0], buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7]);
	fclose(fp);
#endif	

	return rc;
}

LONG scsi_test_unit_ready(tHandle handle)
{
tSCSICmd cmd;
BYTE cdb[12] = { 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
long rc;
char buf;
char reqbuff[18];

	cmd.Handle = handle;
	cmd.Cmd = cdb;
	cmd.CmdLen = 12;
	cmd.Buffer = &buf;
	cmd.TransferLen = 0;
	cmd.SenseBuffer = reqbuff;
	cmd.Timeout = 200;			/* i.e. 1 second - generous :-) */
	cmd.Flags = 0;

	return scsicall->In(&cmd);
}

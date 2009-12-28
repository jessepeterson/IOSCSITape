#include <AssertMacros.h>
#include <sys/conf.h>
#include <miscfs/devfs/devfs.h>
#include <sys/errno.h>
#include <sys/mtio.h>
#include <sys/ioctl.h>

#include <IOKit/scsi/SCSICommandOperationCodes.h>

#include "IOSCSITape.h"

#define GROW_FACTOR 10
#define SCSI_MOTION_TIMEOUT   (kThirtySecondTimeoutInMS * 2 * 5)
#define SCSI_NOMOTION_TIMEOUT  kTenSecondTimeoutInMS

#define super IOSCSIPrimaryCommandsDevice
OSDefineMetaClassAndStructors(IOSCSITape, IOSCSIPrimaryCommandsDevice)

/* The one really global operation of this kext is to register for a
 * character device major number. The constructors and destructors
 * get called on kext load/unload making it a great candidate for
 * IOKit-instance-spaning devices. */
class CdevMajorIniter
{
public:
	int majorNumber;
	static struct cdevsw cdevsw;
	CdevMajorIniter(void);
	~CdevMajorIniter(void);
};

CdevMajorIniter::CdevMajorIniter(void)
{
	majorNumber = cdevsw_add(-1, &cdevsw);
}

CdevMajorIniter::~CdevMajorIniter(void)
{
	cdevsw_remove(majorNumber, &cdevsw);
}

/* character device system call vectors */
struct cdevsw CdevMajorIniter::cdevsw = 
{
	st_open,
	st_close,
	st_readwrite,
	st_readwrite,
	st_ioctl,
	eno_stop,
	eno_reset,
	0,
	(select_fcn_t *)enodev,
	eno_mmap,
	eno_strat,
	eno_getc,
	eno_putc,
	0
};

static CdevMajorIniter CdevMajorIniter;

IOSCSITape **IOSCSITape::devices = NULL;
int IOSCSITape::deviceCount = 0; 

bool
IOSCSITape::FindDeviceMinorNumber(void)
{
	int i;
	
	if (deviceCount == 0)
		if (!GrowDeviceMinorNumberMemory())
			return false;
	
	for (i = 0; i < deviceCount && devices[i]; i++);
	
	if (i == deviceCount)
		if (!GrowDeviceMinorNumberMemory())
			return false;
	
	tapeNumber = i;
	devices[tapeNumber] = this;
	
	return true;
}

bool
IOSCSITape::GrowDeviceMinorNumberMemory(void)
{
	IOSCSITape **newDevices;
	int cur_size = sizeof(IOSCSITape *) *  deviceCount;
	int new_size = sizeof(IOSCSITape *) * (deviceCount + GROW_FACTOR);
	
	newDevices = (IOSCSITape **)IOMalloc(new_size);
	
	if (!newDevices)
		return false;
	
	bzero(newDevices, new_size);
	
	if (deviceCount)
	{
		memcpy(newDevices, devices, cur_size);
		IOFree(devices, cur_size);
	}
	
	devices = newDevices;
	deviceCount += GROW_FACTOR;
	
	return true;
}

void
IOSCSITape::ClearDeviceMinorNumber(void)
{
	devices[tapeNumber] = NULL;
	tapeNumber = 0;
}

UInt32
IOSCSITape::GetInitialPowerState(void)
{
	return 0;
}

void
IOSCSITape::HandlePowerChange(void)
{
}

void
IOSCSITape::HandleCheckPowerState(void)
{
}

void
IOSCSITape::TicklePowerManager(void)
{
}

bool
IOSCSITape::InitializeDeviceSupport(void)
{
	if (FindDeviceMinorNumber())
	{
		cdev_node = devfs_make_node(
			makedev(CdevMajorIniter.majorNumber, tapeNumber), 
			DEVFS_CHAR,
			UID_ROOT,
			GID_OPERATOR,
			0664,
			TAPE_FORMAT, tapeNumber);
		
		if (cdev_node)
		{
			flags = 0;
			
			return true;
		}
	}

	return false;
}

void
IOSCSITape::StartDeviceSupport(void)
{
	STATUS_LOG("<%s, %s, %s> tape",
			   GetVendorString(),
			   GetProductString(),
			   GetRevisionString());
	
	GetDeviceDetails();
	GetDeviceBlockLimits();
}

void
IOSCSITape::SuspendDeviceSupport(void)
{
}

void
IOSCSITape::ResumeDeviceSupport(void)
{
}

void
IOSCSITape::StopDeviceSupport(void)
{
	devfs_remove(cdev_node);
	ClearDeviceMinorNumber();
}

void
IOSCSITape::TerminateDeviceSupport(void)
{
}

UInt32
IOSCSITape::GetNumberOfPowerStateTransitions(void)
{
	return 0;
}

bool
IOSCSITape::ClearNotReadyStatus(void)
{
	return false;
}

#if 0
#pragma mark -
#pragma mark Character device system calls
#pragma mark -
#endif /* 0 */

int st_open(dev_t dev, int flags, int devtype, struct proc *p)
{
	IOSCSITape *st = IOSCSITape::devices[minor(dev)];
	int error = ENXIO;
	
	if (st->flags & ST_DEVOPEN)
		error = EBUSY;
	else
	{
		st->flags |= ST_DEVOPEN;
		error = KERN_SUCCESS;
	}
	
	return error;
}

int st_close(dev_t dev, int flags, int devtype, struct proc *p)
{
	IOSCSITape *st = IOSCSITape::devices[minor(dev)];

	/* TODO: Assure two filemarks after writes as a pseudo-EOD. */
	st->flags &= ~ST_DEVOPEN;
	
	return KERN_SUCCESS;
}

int st_readwrite(dev_t dev, struct uio *uio, int ioflag)
{
	return (ENODEV);
}

int st_ioctl(dev_t dev, u_long cmd, caddr_t data, int fflag, struct proc *p)
{
	IOSCSITape *st = IOSCSITape::devices[minor(dev)];
	struct mtop *mt = (struct mtop *) data;
	struct mtget *g = (struct mtget *) data;
	
	switch (cmd)
	{
		case MTIOCGET:
			memset(g, 0, sizeof(struct mtget));
			g->mt_type = 0x7;	/* Ultrix compat *//*? */
			g->mt_blksiz = st->blksize;
			g->mt_density = st->density;
			// g->mt_fileno = st->fileno;
			// g->mt_blkno = st->blkno;
			g->mt_dsreg = st->flags;	/* report raw driver flags */
			/* TODO: Implement the full mtget struct */
			
			return KERN_SUCCESS;
		case MTIOCTOP:
			// int number = mt->mt_count;
			
			switch (mt->mt_op)
			{
				case MTREW:
					if (st->Rewind() == kIOReturnSuccess)
						return KERN_SUCCESS;
					else
						return ENODEV;
				default:
					return EINVAL;
			}
		default:
			return ENOTTY;
	}
}

#if 0
#pragma mark -
#pragma mark SCSI Operations
#pragma mark -
#endif /* 0 */

/*
 *  DoSCSICommand()
 *  Encapsulate super::SendCommand() to handle unexpected service and
 *  task errors as well as hand off to SCSI SENSE interpreter.
 */
SCSITaskStatus
IOSCSITape::DoSCSICommand(
	SCSITaskIdentifier	request,
	UInt32				timeoutDuration)
{
	SCSITaskStatus		taskStatus		= kSCSITaskStatus_DeliveryFailure;
	SCSIServiceResponse	serviceResponse	= kSCSIServiceResponse_SERVICE_DELIVERY_OR_TARGET_FAILURE;
	
	require((request != 0), ErrorExit);
	
	serviceResponse = SendCommand(request, timeoutDuration);
	
	if (serviceResponse != kSCSIServiceResponse_TASK_COMPLETE)
	{
		STATUS_LOG("unknown service response: 0x%x", serviceResponse);
		goto ErrorExit;
	}
	else
	{
		taskStatus = GetTaskStatus(request);
		
		if (taskStatus == kSCSITaskStatus_CHECK_CONDITION)
		{
			STATUS_LOG("unhandled CHECK CONDITION");
		}
		else if (taskStatus != kSCSITaskStatus_GOOD)
		{
			STATUS_LOG("unknown task status: 0x%x", taskStatus);
		}
	}
	
ErrorExit:
	
	return taskStatus;
}

IOReturn
IOSCSITape::TestUnitReady(void)
{
	SCSITaskIdentifier	task			= NULL;
	IOReturn			result			= kIOReturnError;
	SCSITaskStatus		taskStatus		= kSCSITaskStatus_DeviceNotResponding;
	
	task = GetSCSITask();
	
	require((task != 0), ErrorExit);
	
	if (TEST_UNIT_READY(task, 0x00) == true)
		taskStatus = DoSCSICommand(task, SCSI_NOMOTION_TIMEOUT);
	
	if (taskStatus == kSCSITaskStatus_GOOD)
		result = kIOReturnSuccess;
	
	ReleaseSCSITask(task);
	
ErrorExit:
	
	return result;
}

IOReturn
IOSCSITape::Rewind(void)
{
	IOReturn			status		= kIOReturnError;
	SCSITaskIdentifier	task		= NULL;
	SCSITaskStatus		taskStatus	= kSCSITaskStatus_DeliveryFailure;
	
	task = GetSCSITask();
	
	require ((task != 0), ErrorExit);
	
	if (REWIND(task, 0, 0) == true)
		taskStatus = DoSCSICommand(task, SCSI_MOTION_TIMEOUT);
	
	if (taskStatus == kSCSITaskStatus_GOOD)
		status = kIOReturnSuccess;
	
	ReleaseSCSITask(task);
	
ErrorExit:
	
	return status;
}

/*
 *  GetDeviceDetails()
 *  Get mode sense details and set device parameters.
 *
 *  Would have ideally liked to use super::GetModeSense() but it appears
 *  to set the DBD bit and we need the descriptor values for density,
 *  etc.
 */
IOReturn
IOSCSITape::GetDeviceDetails(void)
{
	IOReturn				status		= kIOReturnError;
	SCSITaskIdentifier		task		= NULL;
	SCSITaskStatus			taskStatus	= kSCSITaskStatus_DeviceNotResponding;
	IOMemoryDescriptor *	dataBuffer	= NULL;
	SCSI_ModeSense_Default	modeData	= { 0 };

	dataBuffer = IOMemoryDescriptor::withAddress(&modeData,
												 sizeof(modeData),
												 kIODirectionIn);
	
	require((dataBuffer != 0), ErrorExit);
	
	task = GetSCSITask();
	
	require((task != 0), ErrorExit);

	if (MODE_SENSE_6(task, 
					 dataBuffer, 
					 0x0,
					 0x0,
					 0x00,
					 sizeof(SCSI_ModeSense_Default), 
					 0x00) == true)
	{
		taskStatus = DoSCSICommand(task, SCSI_NOMOTION_TIMEOUT);
	}
	
	if (taskStatus == kSCSITaskStatus_GOOD)
	{
		blksize = 
			(modeData.descriptor.BLOCK_LENGTH[0] << 16) |
			(modeData.descriptor.BLOCK_LENGTH[1] <<  8) |
			 modeData.descriptor.BLOCK_LENGTH[2];
		
		density = modeData.descriptor.DENSITY_CODE;
		flags &= ~(ST_READONLY | ST_BUFF_MODE);
		
		if (modeData.header.DEVICE_SPECIFIC_PARAMETER & SMH_DSP_WRITE_PROT)
			flags |= ST_READONLY;
		
		if (modeData.header.DEVICE_SPECIFIC_PARAMETER & SMH_DSP_BUFF_MODE)
			flags |= ST_BUFF_MODE;

		STATUS_LOG("density code: %d, %d-byte blocks, write-%s, %sbuffered",
				   density, blksize,
				   flags & ST_READONLY ? "protected" : "enabled",
				   flags & ST_BUFF_MODE ? "" : "un");
		
		status = kIOReturnSuccess;
	}
	
	ReleaseSCSITask(task);
	dataBuffer->release();
	
ErrorExit:
	
	return status;
}

IOReturn
IOSCSITape::GetDeviceBlockLimits(void)
{
	SCSITaskIdentifier		task			= NULL;
	IOReturn				status			= kIOReturnError;
	UInt8					blockLimitsData[6]	= { 0 };
	SCSITaskStatus			taskStatus		= kSCSITaskStatus_DeliveryFailure;
	IOMemoryDescriptor *	dataBuffer		= NULL;
	
	dataBuffer = IOMemoryDescriptor::withAddress(&blockLimitsData, 
												 sizeof(blockLimitsData), 
												 kIODirectionIn);

	require ((dataBuffer != 0), ErrorExit);
	
	task = GetSCSITask();
	
	require ((task != 0), ErrorExit);
	
	if (READ_BLOCK_LIMITS(task, dataBuffer, 0x00) == true)
		taskStatus = DoSCSICommand(task, SCSI_NOMOTION_TIMEOUT);
	
	if (taskStatus == kSCSITaskStatus_GOOD)
	{
		// blkgran = blockLimitsData[0] & 0x1F;
		
		blkmin =
			(blockLimitsData[4] <<  8) |
			 blockLimitsData[5];
		
		blkmax =
			(blockLimitsData[1] << 16) |
			(blockLimitsData[2] <<  8) |
			 blockLimitsData[3];
		
		STATUS_LOG("min/max block size: %d/%d", blkmin, blkmax);
		
		status = kIOReturnSuccess;
	}

	ReleaseSCSITask(task);
	dataBuffer->release();
	
ErrorExit:
	
	return status;
}

#if 0
#pragma mark -
#pragma mark 0x01 SSC Implicit Address Commands
#pragma mark -
#endif /* 0 */

bool
IOSCSITape::ERASE_6(
	SCSITaskIdentifier	request,
	SCSICmdField1Bit	IMMED,
	SCSICmdField1Bit	LONG,
	SCSICmdField1Byte	CONTROL)
{
	bool result = false;
	
	require(IsParameterValid(IMMED, kSCSICmdFieldMask1Bit), ErrorExit);
	require(IsParameterValid(LONG, kSCSICmdFieldMask1Bit), ErrorExit);
	require(IsParameterValid(CONTROL, kSCSICmdFieldMask1Byte), ErrorExit);
	
	SetCommandDescriptorBlock(request, 
							  kSCSICmd_ERASE, 
							  (LONG << 1) |
							   IMMED, 
							  0x00, 
							  0x00, 
							  0x00, 
							  CONTROL);
	
	SetDataTransferDirection(request, kSCSIDataTransfer_NoDataTransfer);
	
	SetTimeoutDuration(request, SCSI_MOTION_TIMEOUT);
	
ErrorExit:
	
	return result;
}				   

bool
IOSCSITape::READ_6(
	SCSITaskIdentifier		request,
	IOMemoryDescriptor *	readBuffer,
	UInt32					blockSize,
	SCSICmdField1Bit		SILI,
	SCSICmdField1Bit		FIXED,
	SCSICmdField3Byte		TRANSFER_LENGTH,
	SCSICmdField1Byte		CONTROL)
{
	UInt32	requestedByteCount	= 0;
	bool	result				= false;
	
	require(IsParameterValid(SILI, kSCSICmdFieldMask1Bit), ErrorExit);
	require(IsParameterValid(FIXED, kSCSICmdFieldMask1Bit), ErrorExit);
	require(IsParameterValid(TRANSFER_LENGTH, kSCSICmdFieldMask3Byte), ErrorExit);
	require(IsParameterValid(CONTROL, kSCSICmdFieldMask1Byte), ErrorExit);
	
	if (FIXED)
		requestedByteCount = TRANSFER_LENGTH * blockSize;
	else
		requestedByteCount = TRANSFER_LENGTH;
	
	require((readBuffer != 0), ErrorExit);
	require((readBuffer->getLength() >= requestedByteCount), ErrorExit);
	
	SetCommandDescriptorBlock(request, 
							  kSCSICmd_READ_6, 
							  (SILI << 1) |
							   FIXED, 
							  (TRANSFER_LENGTH >> 16) & 0xFF, 
							  (TRANSFER_LENGTH >>  8) & 0xFF, 
							   TRANSFER_LENGTH        & 0xFF, 
							  CONTROL);
	
	SetDataBuffer(request, readBuffer);
	
	SetRequestedDataTransferCount(request, requestedByteCount);
	
	SetDataTransferDirection(request, kSCSIDataTransfer_FromTargetToInitiator);
	
	SetTimeoutDuration(request, SCSI_MOTION_TIMEOUT);
	
	result = true;
	
ErrorExit:
	
	return result;
}

bool
IOSCSITape::SPACE_6(
	SCSITaskIdentifier	request,
	SCSICmdField4Bit	CODE,
	SCSICmdField3Byte	COUNT,
	SCSICmdField1Byte	CONTROL)
{
	bool result = false;
	
	require(IsParameterValid(CODE, kSCSICmdFieldMask4Bit), ErrorExit);
	
	/* allow a two's complement negative number in the 3-byte integer
	 * space to not cause an error with IsParameterValid() */
	if ((COUNT & 0xFF800000) == 0xFF800000)
		COUNT = COUNT & 0xFFFFFF;
	
	require(IsParameterValid(COUNT, kSCSICmdFieldMask3Byte), ErrorExit);
	require(IsParameterValid(CONTROL, kSCSICmdFieldMask1Byte), ErrorExit);
	
	SetCommandDescriptorBlock(request, 
							  kSCSICmd_SPACE, 
							  CODE, 
							  (COUNT >> 16) & 0xFF, 
							  (COUNT >>  8) & 0xFF, 
							   COUNT        & 0xFF, 
							  CONTROL);
	
	SetDataTransferDirection(request, kSCSIDataTransfer_NoDataTransfer);
	
	SetTimeoutDuration(request, SCSI_MOTION_TIMEOUT);
	
	result = true;
	
ErrorExit:
	
	return result;
}

bool
IOSCSITape::WRITE_6(
	SCSITaskIdentifier		request,
	IOMemoryDescriptor *	writeBuffer,
	UInt32					blockSize,
	SCSICmdField1Bit		FIXED,
	SCSICmdField3Byte		TRANSFER_LENGTH,
	SCSICmdField1Byte		CONTROL)
{
	UInt32	requestedByteCount	= 0;
	bool	result				= false;
	
	require(IsParameterValid(FIXED, kSCSICmdFieldMask1Bit), ErrorExit);
	require(IsParameterValid(TRANSFER_LENGTH, kSCSICmdFieldMask3Byte), ErrorExit);
	require(IsParameterValid(CONTROL, kSCSICmdFieldMask1Byte), ErrorExit);
	
	if (FIXED)
		requestedByteCount = TRANSFER_LENGTH * blockSize;
	else
		requestedByteCount = TRANSFER_LENGTH;
	
	require((writeBuffer != 0), ErrorExit);
	require((writeBuffer->getLength() >= requestedByteCount), ErrorExit);
	
	SetCommandDescriptorBlock(request, 
							  kSCSICmd_WRITE_6, 
							  FIXED, 
							  (TRANSFER_LENGTH >> 16) & 0xFF, 
							  (TRANSFER_LENGTH >>  8) & 0xFF, 
							   TRANSFER_LENGTH        & 0xFF, 
							  CONTROL);
	
	SetDataBuffer(request, writeBuffer);
	
	SetRequestedDataTransferCount(request, requestedByteCount);
	
	SetDataTransferDirection(request, kSCSIDataTransfer_FromInitiatorToTarget);
	
	SetTimeoutDuration(request, SCSI_MOTION_TIMEOUT);
	
	result = true;
	
ErrorExit:
	
	return result;
}

bool
IOSCSITape::WRITE_FILEMARKS_6(
	SCSITaskIdentifier	request,
	SCSICmdField1Bit	WSMK,
	SCSICmdField1Bit	IMMED,
	SCSICmdField3Byte	TRANSFER_LENGTH,
	SCSICmdField1Byte	CONTROL)
{
	bool result = false;
	
	require(IsParameterValid(WSMK, kSCSICmdFieldMask1Bit), ErrorExit);
	require(IsParameterValid(IMMED, kSCSICmdFieldMask1Bit), ErrorExit);
	require(IsParameterValid(TRANSFER_LENGTH, kSCSICmdFieldMask3Byte), ErrorExit);
	require(IsParameterValid(CONTROL, kSCSICmdFieldMask1Byte), ErrorExit);
	
	SetCommandDescriptorBlock(request, 
							  kSCSICmd_WRITE_FILEMARKS, 
							  (WSMK << 1) |
							   IMMED, 
							  (TRANSFER_LENGTH >> 16) & 0xFF, 
							  (TRANSFER_LENGTH >>  8) & 0xFF, 
							   TRANSFER_LENGTH        & 0xFF, 
							  CONTROL);
	
	SetDataTransferDirection(request, kSCSIDataTransfer_NoDataTransfer);
	
	SetTimeoutDuration(request, SCSI_MOTION_TIMEOUT);
	
	result = true;
	
ErrorExit:
	
	return result;
}

#if 0
#pragma mark -
#pragma mark 0x01 SSC Common Commands
#pragma mark -
#endif /* 0 */

bool
IOSCSITape::LOAD_UNLOAD(
	SCSITaskIdentifier	request,
	SCSICmdField1Bit	IMMED,
	SCSICmdField1Bit	HOLD,
	SCSICmdField1Bit	EOT,
	SCSICmdField1Bit	RETEN,
	SCSICmdField1Bit	LOAD,
	SCSICmdField1Byte	CONTROL)
{
	bool result = false;
	
	require(IsParameterValid(IMMED, kSCSICmdFieldMask1Bit), ErrorExit);
	require(IsParameterValid(HOLD, kSCSICmdFieldMask1Bit), ErrorExit);
	require(IsParameterValid(EOT, kSCSICmdFieldMask1Bit), ErrorExit);
	require(IsParameterValid(RETEN, kSCSICmdFieldMask1Bit), ErrorExit);
	require(IsParameterValid(LOAD, kSCSICmdFieldMask1Bit), ErrorExit);
	require(IsParameterValid(CONTROL, kSCSICmdFieldMask1Byte), ErrorExit);
	
	SetCommandDescriptorBlock(request, 
							  kSCSICmd_LOAD_UNLOAD, 
							  IMMED, 
							  0x00, 
							  0x00, 
							  (HOLD  << 3) |
							  (EOT   << 2) |
							  (RETEN << 1) |
							   LOAD, 
							  CONTROL);
	
	SetDataTransferDirection(request, kSCSIDataTransfer_NoDataTransfer);
	
	SetTimeoutDuration(request, SCSI_MOTION_TIMEOUT);
	
	result = true;
	
ErrorExit:
	
	return result;
}

bool
IOSCSITape::READ_BLOCK_LIMITS(
	SCSITaskIdentifier		request,
	IOMemoryDescriptor *	readBuffer,
	SCSICmdField1Byte		CONTROL)
{
	bool result = false;
	
	require(IsParameterValid(CONTROL, kSCSICmdFieldMask1Byte), ErrorExit);
	
	require((readBuffer != 0), ErrorExit);
	require((readBuffer->getLength() >= 6), ErrorExit);
	
	SetCommandDescriptorBlock(request, 
							  kSCSICmd_READ_BLOCK_LIMITS, 
							  0x00, 
							  0x00, 
							  0x00, 
							  0x00, 
							  CONTROL);
	
	SetDataBuffer(request, readBuffer);
	
	SetRequestedDataTransferCount(request, 6);
	
	SetDataTransferDirection(request, kSCSIDataTransfer_FromTargetToInitiator);
	
	SetTimeoutDuration(request, SCSI_NOMOTION_TIMEOUT);
	
	result = true;
	
ErrorExit:
	
	return result;
}

bool
IOSCSITape::READ_POSITION(
	SCSITaskIdentifier		request,
	IOMemoryDescriptor *	buffer,
	SCSICmdField5Bit		SERVICE_ACTION,
	SCSICmdField2Byte		ALLOCATION_LENGTH,
	SCSICmdField1Byte		CONTROL)
{
	bool	result	= false;
	int		count	= 0;
	
	require(IsParameterValid(SERVICE_ACTION, kSCSICmdFieldMask5Bit), ErrorExit);
	require(IsParameterValid(ALLOCATION_LENGTH, kSCSICmdFieldMask2Byte), ErrorExit);
	require(IsParameterValid(CONTROL, kSCSICmdFieldMask1Byte), ErrorExit);
	
	require((buffer != 0), ErrorExit);
	
	switch (SERVICE_ACTION)
	{
		case kSCSIReadPositionServiceAction_ShortFormBlockID:
		case kSCSIReadPositionServiceAction_ShortFormVendorSpecific:
			count = 20;
			break;
		case kSCSIReadPositionServiceAction_LongForm:
			count = 32;
			break;
		case kSCSIReadPositionServiceAction_ExtendedForm:
			count = 28;
			break;
		default:
			/* unknown service action */
			count = buffer->getLength();
			break;
	}
	
	require((buffer->getLength() >= count), ErrorExit);
	
	SetCommandDescriptorBlock(request, 
							  kSCSICmd_READ_POSITION, 
							  SERVICE_ACTION, 
							  0x00, 
							  0x00, 
							  0x00, 
							  0x00,
							  0x00,
							  (ALLOCATION_LENGTH >> 8) & 0xFF,
							   ALLOCATION_LENGTH       & 0xFF,
							  CONTROL);
	
	SetDataBuffer(request, buffer);
	
	SetRequestedDataTransferCount(request, count);
	
	SetDataTransferDirection(request, kSCSIDataTransfer_FromTargetToInitiator);
	
	SetTimeoutDuration(request, SCSI_NOMOTION_TIMEOUT);
	
	result = true;
	
ErrorExit:
	
	return result;
}

bool
IOSCSITape::REWIND(
	SCSITaskIdentifier	request,
	SCSICmdField1Bit	IMMED,
	SCSICmdField1Byte	CONTROL)
{
	bool result = false;
	
	require(IsParameterValid(IMMED, kSCSICmdFieldMask1Bit), ErrorExit);
	require(IsParameterValid(CONTROL, kSCSICmdFieldMask1Byte), ErrorExit);
	
	SetCommandDescriptorBlock(request, 
							  kSCSICmd_REWIND, 
							  IMMED, 
							  0x00, 
							  0x00, 
							  0x00, 
							  CONTROL);
	
	SetDataTransferDirection(request, kSCSIDataTransfer_NoDataTransfer);
	
	SetTimeoutDuration(request, SCSI_MOTION_TIMEOUT);
	
	result = true;
	
ErrorExit:
	
	return result;
}

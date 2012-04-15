/*
 *  IOSCSITape.cpp
 *  IOSCSITape
 *
 *  This software is licensed under an MIT license. See LICENSE.txt.
 *  Created by Jesse Peterson on 12/13/09.
 *
 */

#include <AssertMacros.h>
#include <sys/conf.h>
#include <miscfs/devfs/devfs.h>
#include <sys/errno.h>
#include "mtio.h"
#include <sys/ioctl.h>
#include <sys/uio.h>

#include <IOKit/scsi/SCSICommandOperationCodes.h>

#include "IOSCSITape.h"
#include "custom_mtio.h"

#define GROW_FACTOR 10
#define SCSI_MOTION_TIMEOUT   (kThirtySecondTimeoutInMS * 2 * 5)
#define SCSI_NOMOTION_TIMEOUT  kTenSecondTimeoutInMS

#define super IOSCSIPrimaryCommandsDevice
OSDefineMetaClassAndStructors(IOSCSITape, IOSCSIPrimaryCommandsDevice)

char *kSCSISenseKeyDescriptions[] =
{
	(char *)"No Sense",
	(char *)"Recovered Error",
	(char *)"Not Ready",
	(char *)"Medium Error",
	(char *)"Hardware Error",
	(char *)"Illegal Request",
	(char *)"Unit Attention",
	(char *)"Data Protect",
	(char *)"Blank Check",
	(char *)"Vendor Specific",
	(char *)"Copy Aborted",
	(char *)"Aborted Command",
	(char *)"Equal",
	(char *)"Volume Overflow",
	(char *)"Miscompare",
	(char *)"(Unknown)"
};

#if 0
#pragma mark -
#pragma mark Initialization & support
#pragma mark -
#endif /* 0 */

/*
 *  Support for BSD-IOKit data exchange. Most of this is from
 *  IOStorageFamily-92.9 (IOMediaBSDClient.cpp).
 */
IOMemoryDescriptor *IOMemoryDescriptorFromUIO(struct uio *uio)
{
	return IOMemoryDescriptor::withOptions(
		uio,
		uio_iovcnt(uio),
		0,
		(uio_isuserspace(uio)) ? current_task() : kernel_task,
		kIOMemoryTypeUIO | kIOMemoryAsReference |
			((uio_rw(uio) == UIO_READ) ? kIODirectionIn : kIODirectionInOut)
		);
}

/* The constructors and destructors get called on kext load/unload
 * used for IOKit-instance-spaning devices. */
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
	
	fileno = -1;
	blkno = -1;
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

bool
IOSCSITape::ClearNotReadyStatus(void)
{
	return false;
}

bool
IOSCSITape::IsFixedBlockSize(void)
{
	if (blksize == 0)
		return false;
	else
		return true;
}

#if 0
#pragma mark -
#pragma mark IOKit power management
#pragma mark -
#endif /* 0 */

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

void
IOSCSITape::TerminateDeviceSupport(void)
{
}

UInt32
IOSCSITape::GetNumberOfPowerStateTransitions(void)
{
	return 0;
}

#if 0
#pragma mark -
#pragma mark Tape driver operations
#pragma mark -
#endif /* 0 */

/* The job of these tape driver operations, generally, is to adapt the
 * lower level SCSI operations and convert the results and potential
 * SENSE information into POSIX-ish return codes and results as well
 * as manage driver state like blkno and fileno. */

int st_rewind(IOSCSITape *st)
{
	if (st->Rewind() == kIOReturnSuccess)
	{
		st->fileno = 0;
		st->blkno = 0;
		return KERN_SUCCESS;
	}
	
	return ENODEV;
}

int st_space(IOSCSITape *st, SCSISpaceCode type, int number)
{
	if (st->Space(type, number) == kIOReturnSuccess)
	{
		if (st->fileno != -1)
		{
			switch (type)
			{
				case kSCSISpaceCode_Filemarks:
					st->fileno += number;
					st->blkno = 0;
					break;
				case kSCSISpaceCode_LogicalBlocks:
					st->blkno += number;
					break;
				case kSCSISpaceCode_EndOfData:
					st->fileno = -1;
					st->blkno = -1;
					break;
			}
		}
		
		return KERN_SUCCESS;
	}
	
	return ENODEV;
}

int st_write_filemarks(IOSCSITape *st, int number)
{
	if (st->WriteFilemarks(number) == kIOReturnSuccess)
	{
		if (st->fileno != -1)
		{
			st->fileno += number;
			st->blkno = 0;
		}
		
		return KERN_SUCCESS;
	}
	
	return ENODEV;
}

int st_unload(IOSCSITape *st)
{
	if (st->LoadUnload(0) == kIOReturnSuccess)
		return KERN_SUCCESS;
	
	return ENODEV;
}

int st_rdpos(IOSCSITape *st, bool vendor, unsigned int *data)
{
	SCSI_ReadPositionShortForm pos = { 0 };
	
	if (st->ReadPosition(&pos, vendor) == kIOReturnSuccess)
	{
		if (pos.flags & kSCSIReadPositionShortForm_LogicalObjectLocationUnknown)
			return (ENOTSUP);
		
		*data = pos.firstLogicalObjectLocation;
		
		return KERN_SUCCESS;
	}
	
	return ENODEV;	
}

int st_set_blocksize(IOSCSITape *st, int number)
{
	if ((number > 0) &&
		(st->blkmin || st->blkmax) &&
		(number < st->blkmin ||
		 number > st->blkmax))
	{
		return (EINVAL);
	}
	
	if (st->SetBlockSize(number) == kIOReturnSuccess)
		return KERN_SUCCESS;
	
	return (ENODEV);	
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

	/* if the last command was a write then write 2x EOF markers and
	 * backspace over 1 (for the next write) */
	if (st->flags & ST_WRITTEN)
	{
		st_write_filemarks(st, 2);
		st_space(st, kSCSISpaceCode_Filemarks, -1);
		
		st->flags &= ~ST_WRITTEN;
	}
	
	st->flags &= ~ST_DEVOPEN;
	
	return KERN_SUCCESS;
}

int st_readwrite(dev_t dev, struct uio *uio, int ioflag)
{
	IOSCSITape			*st			= IOSCSITape::devices[minor(dev)];
	IOMemoryDescriptor	*dataBuffer	= IOMemoryDescriptorFromUIO(uio);
	int					status		= ENOSYS;
	IOReturn			opStatus	= kIOReturnError;
	int					lastRealizedBytes = 0;
	
	if (dataBuffer == 0)
		return ENOMEM;
	
	dataBuffer->prepare();
	
	opStatus = st->ReadWrite(dataBuffer, &lastRealizedBytes);
	
	dataBuffer->complete();
	dataBuffer->release();
	
	if (opStatus == kIOReturnSuccess)
	{
		uio_setresid(uio, uio_resid(uio) - lastRealizedBytes);
		
		if (st->blkno != -1)
		{
			if (st->IsFixedBlockSize())
				st->blkno += (lastRealizedBytes / st->blksize);
			else
				st->blkno++;
		}

		status = KERN_SUCCESS;
	}
	else if (st->sense_flags & SENSE_FILEMARK)
	{
		if (st->fileno != -1)
		{
			st->fileno++;
			st->blkno = 0;
		}
		
		status = KERN_SUCCESS;
	}
	
	return status;
}

int st_ioctl(dev_t dev, u_long cmd, caddr_t data, int fflag, struct proc *p)
{
	IOSCSITape *st = IOSCSITape::devices[minor(dev)];
	struct mtop *mt = (struct mtop *) data;
	struct mtget *g = (struct mtget *) data;
	int number = mt->mt_count;
	int error = 0;
	
	switch (cmd)
	{
		case MTIOCGET:
			memset(g, 0, sizeof(struct mtget));
			g->mt_type = 0x7;	/* Ultrix compat *//*? */
			g->mt_blksiz = st->blksize;
			g->mt_density = st->density;
			g->mt_fileno = st->fileno;
			g->mt_blkno = st->blkno;
			g->mt_dsreg = st->flags;	/* report raw driver flags */
			g->mt_erreg = st->sense_flags;
			/* TODO: Implement the full mtget struct */
			
			break;
		case MTIOCTOP:
			switch (mt->mt_op)
			{
				case MTBSF:
					number = -number;
				case MTFSF:
					error = st_space(st, kSCSISpaceCode_Filemarks, number);
					break;
				case MTBSR:
					number = -number;
				case MTFSR:
					error = st_space(st, kSCSISpaceCode_LogicalBlocks, number);
					break;
				case MTREW:
					error = st_rewind(st);
					break;
				case MTWEOF:
					error = st_write_filemarks(st, number);
					break;
				case MTOFFL:
					error = st_unload(st);
					break;
				case MTNOP:
					break;
				case MTEOM:
					error = st_space(st, kSCSISpaceCode_EndOfData, number);
					break;
				case MTSETBSIZ:
					error = st_set_blocksize(st, number);
					break;
				default:
					error = EINVAL;
			}
			break;
		case MTIOCRDSPOS:
			error = st_rdpos(st, false, (unsigned int *)data);
			break;
		case MTIOCRDHPOS:
			error = st_rdpos(st, true, (unsigned int *)data);
			break;
		default:
			error = ENOTTY;
	}
	
	return error;
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
	sense_flags = 0;
	
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
			/* Get and interpret SCSI SENSE information */
			GetSense(request);
		}
		else if (taskStatus != kSCSITaskStatus_GOOD)
		{
			STATUS_LOG("unknown task status: 0x%x", taskStatus);
		}
		else if (taskStatus == kSCSITaskStatus_GOOD)
		{
			/* setup flags for device file closing */
			if (flags & ST_WRITTEN_TOGGLE)
				flags |= ST_WRITTEN;
			else
				flags &= ~ST_WRITTEN;
		}
	}
	
	/* clear the write toggle bit in case the next command is not a
	 * write */
	flags &= ~ST_WRITTEN_TOGGLE;
	
ErrorExit:
	
	return taskStatus;
}

/*
 *  GetSenseInformation
 *
 *  Attempt to get SCSI sense information either from the Auto sense
 *  mechanism or by querying manually.
 */
void
IOSCSITape::GetSense(SCSITaskIdentifier request)
{
	SCSI_Sense_Data		senseBuffer = { 0 };
	bool				validSense = false;
	SCSIServiceResponse	serviceResponse = kSCSIServiceResponse_SERVICE_DELIVERY_OR_TARGET_FAILURE;
	
	IOMemoryDescriptor *bufferDesc = IOMemoryDescriptor::withAddress((void *)&senseBuffer, 
																	 sizeof(senseBuffer), 
																	 kIODirectionIn);
	
	if (GetTaskStatus(request) == kSCSITaskStatus_CHECK_CONDITION)
	{
		validSense = GetAutoSenseData(request, &senseBuffer);
		
		if (validSense == false)
		{
			if (REQUEST_SENSE(request, bufferDesc, kSenseDefaultSize, 0) == true)
				serviceResponse = SendCommand(request, kTenSecondTimeoutInMS);
			
			if (serviceResponse == kSCSIServiceResponse_TASK_COMPLETE)
				validSense = true;
		}
		
		if (validSense == true)
			InterpretSense(&senseBuffer);
		else
			STATUS_LOG("invalid or unretrievable SCSI SENSE");
	}
	
	bufferDesc->release();
}

void
IOSCSITape::InterpretSense(SCSI_Sense_Data *sense)
{
	uint8_t key = sense->SENSE_KEY & kSENSE_KEY_Mask;
	uint8_t asc = sense->ADDITIONAL_SENSE_CODE;
	uint8_t ascq = sense->ADDITIONAL_SENSE_CODE_QUALIFIER;

	if ((sense->VALID_RESPONSE_CODE & kSENSE_RESPONSE_CODE_Mask) == kSENSE_RESPONSE_CODE_Current_Errors)
	{
		/* current errors, fixed format - 0x70 */
		
		// sense->VALID_RESPONSE_CODE & kSENSE_DATA_VALID

		if (key  == kSENSE_KEY_NOT_READY &&
			asc  == 0x04 &&
			ascq == 0x01)
		{
			STATUS_LOG("LOGICAL UNIT IS IN PROCESS OF BECOMING READY");
			sense_flags |= SENSE_NOTREADY;
		}
		else if (key  == kSENSE_KEY_NO_SENSE &&
				 asc  == 0x00 &&
				 ascq == 0x04)
		{
			STATUS_LOG("BEGINNING-OF-PARTITION/MEDIUM DETECTED");
			
			sense_flags |= SENSE_BOM;
		}
		else if (key  == kSENSE_KEY_BLANK_CHECK &&
				 asc  == 0x00 &&
				 ascq == 0x05)
				/* sense->SENSE_KEY & kSENSE_EOM_Mask */
		{
			STATUS_LOG("END-OF-DATA DETECTED");
			
			sense_flags |= SENSE_EOD;
		}
		else if ((sense->SENSE_KEY & kSENSE_FILEMARK_Mask) ||
				 (key  == kSENSE_KEY_NO_SENSE &&
				  asc  == 0x00 &&
				  ascq == 0x01))
		{
			STATUS_LOG("FILEMARK DETECTED");
			
			sense_flags |= SENSE_FILEMARK;
		}
		else
		{
			STATUS_LOG("SENSE: %s (Key: 0x%X, ASC: 0x%02X, ASCQ: 0x%02X)",
					   kSCSISenseKeyDescriptions[key], key, asc, ascq);

			if (sense->SENSE_KEY & kSENSE_ILI_Mask)
				STATUS_LOG("SENSE: Incorrect Length Indicator (ILI)");

			int i;
			unsigned char *bytes = (unsigned char *)sense;
			
			IOLog("SCSI SENSE DATA: ");
			for (i = 0; i < sizeof(SCSI_Sense_Data); i++)
			{
				IOLog("0x%02x ", (int)bytes[i]);
			}
			IOLog("\n");
		}
	}
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
		/* copy mode data for next MODE SELECT */
		bcopy(&modeData, &lastModeData, sizeof(SCSI_ModeSense_Default));
		
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
IOSCSITape::SetDeviceDetails(SCSI_ModeSense_Default *modeData)
{
	IOReturn				status		= kIOReturnError;
	IOMemoryDescriptor *	dataBuffer	= NULL;
	SCSITaskIdentifier		task		= NULL;
	SCSITaskStatus			taskStatus	= kSCSITaskStatus_DeviceNotResponding;
	
	dataBuffer = IOMemoryDescriptor::withAddress(modeData,
												 sizeof(SCSI_ModeSense_Default),
												 kIODirectionOut);
	
	require((dataBuffer != 0), ErrorExit);
	
	task = GetSCSITask();
	
	require((task != 0), ErrorExit);
	
	if (MODE_SELECT_6(task, 
					  dataBuffer, 
					  0x0, // PF
					  0x0, // SP
					  sizeof(SCSI_ModeSense_Default), 
					  0x00) == true)
	{
		taskStatus = DoSCSICommand(task, SCSI_NOMOTION_TIMEOUT);
	}
	
	if (taskStatus == kSCSITaskStatus_GOOD)
	{
		status = kIOReturnSuccess;
	}
	
	ReleaseSCSITask(task);
	dataBuffer->release();
	
ErrorExit:
	
	return status;
}

IOReturn
IOSCSITape::SetBlockSize(int size)
{
	IOReturn				status	= kIOReturnError;
	SCSI_ModeSense_Default	newMode;
	
	bcopy(&lastModeData, &newMode, sizeof(SCSI_ModeSense_Default));
	
	newMode.header.MODE_DATA_LENGTH = 0;
	newMode.descriptor.BLOCK_LENGTH[0] = (size >> 16) & 0xFF;
	newMode.descriptor.BLOCK_LENGTH[1] = (size >>  8) & 0xFF;
	newMode.descriptor.BLOCK_LENGTH[2] =  size        & 0xFF;
	
	if ((status = SetDeviceDetails(&newMode)) == kIOReturnSuccess)
		GetDeviceDetails();
	
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

IOReturn
IOSCSITape::WriteFilemarks(int count)
{
	SCSITaskIdentifier	task		= NULL;
	IOReturn			status		= kIOReturnError;
	SCSITaskStatus		taskStatus	= kSCSITaskStatus_No_Status;
	
	task = GetSCSITask();
	
	require((task != 0), ErrorExit);

	flags |= ST_WRITTEN_TOGGLE;

	if (WRITE_FILEMARKS_6(task, 0x0, 0x0, count, 0) == true)
		taskStatus = DoSCSICommand(task, SCSI_MOTION_TIMEOUT);
	
	if (taskStatus == kSCSITaskStatus_GOOD)
		status = kIOReturnSuccess;
	
	ReleaseSCSITask(task);
	
ErrorExit:
	
	return status;	
}

IOReturn
IOSCSITape::Space(SCSISpaceCode type, int count)
{
	SCSITaskIdentifier	task			= NULL;
	IOReturn			status			= kIOReturnError;
	SCSITaskStatus		taskStatus		= kSCSITaskStatus_No_Status;
	
	task = GetSCSITask();
	
	require((task != 0), ErrorExit);
	
	if (SPACE_6(task, type, count, 0) == true)
		taskStatus = DoSCSICommand(task, SCSI_MOTION_TIMEOUT);
	
	if (taskStatus == kSCSITaskStatus_GOOD)
		status = kIOReturnSuccess;
	
	ReleaseSCSITask(task);
	
ErrorExit:
	
	return status;	
}

IOReturn
IOSCSITape::LoadUnload(int loadUnload)
{
	SCSITaskIdentifier	task			= NULL;
	IOReturn			status			= kIOReturnError;
	SCSITaskStatus		taskStatus		= kSCSITaskStatus_DeviceNotResponding;
	
	task = GetSCSITask();
	
	require((task != 0), ErrorExit);
	
	if (LOAD_UNLOAD(task, 0, 0, 0, 0, loadUnload, 0) == true)
		taskStatus = DoSCSICommand(task, SCSI_MOTION_TIMEOUT);
	
	if (taskStatus == kSCSITaskStatus_GOOD)
		status = kIOReturnSuccess;
	
	ReleaseSCSITask(task);
	
ErrorExit:
	
	return status;
}

IOReturn
IOSCSITape::ReadPosition(SCSI_ReadPositionShortForm *readPos, bool vendor)
{
	SCSITaskIdentifier		task			= NULL;
	IOReturn				status			= kIOReturnError;
	SCSITaskStatus			taskStatus		= kSCSITaskStatus_DeviceNotResponding;
	IOMemoryDescriptor *	dataBuffer		= NULL;
	UInt8					readPosData[20] = { 0 };
	
	dataBuffer = IOMemoryDescriptor::withAddress(&readPosData, 
												 sizeof(readPosData), 
												 kIODirectionIn);
	
	require((dataBuffer != 0), ErrorExit);
	
	task = GetSCSITask();
	
	require((task != 0), ErrorExit);
	
	if (READ_POSITION(task, 
					  dataBuffer, 
					  (vendor ? kSCSIReadPositionServiceAction_ShortFormVendorSpecific : kSCSIReadPositionServiceAction_ShortFormBlockID), 
					  0x0, 
					  0x00) == true)
	{
		taskStatus = DoSCSICommand(task, SCSI_NOMOTION_TIMEOUT);
	}
	
	if (taskStatus == kSCSITaskStatus_GOOD)
	{
		readPos->flags = readPosData[0];
		readPos->partitionNumber = readPosData[1];
		
		readPos->firstLogicalObjectLocation =
			(readPosData[4]  << 24) |
			(readPosData[5]  << 16) |
			(readPosData[6]  <<  8) |
			 readPosData[7];
		
		readPos->lastLogicalObjectLocation =
			(readPosData[8]  << 24) |
			(readPosData[9]  << 16) |
			(readPosData[10] <<  8) |
			 readPosData[11];
		
		readPos->logicalObjectsInObjectBuffer =
			(readPosData[13] << 16) |
			(readPosData[14] <<  8) |
			 readPosData[15];
		
		readPos->bytesInObjectBuffer =
			(readPosData[16] << 24) |
			(readPosData[17] << 16) |
			(readPosData[18] <<  8) |
			 readPosData[19];
		
		status = kIOReturnSuccess;
	}
	
	ReleaseSCSITask(task);
	dataBuffer->release();
	
ErrorExit:
	
	return status;
}

IOReturn
IOSCSITape::ReadWrite(IOMemoryDescriptor *dataBuffer, int *realizedBytes)
{
	SCSITaskIdentifier	task			= NULL;
	IOReturn			status			= kIOReturnNoResources;
	SCSITaskStatus		taskStatus		= kSCSITaskStatus_No_Status;
	bool				cmdStatus		= false;
	int					transferSize	= 0;
	
	require((dataBuffer != 0), ErrorExit);
	
	transferSize = dataBuffer->getLength();

	if (IsFixedBlockSize())
	{
		if (transferSize % blksize)
		{
			STATUS_LOG("must be multiple of block size");
			return kIOReturnNotAligned;
		}
		
		transferSize /= blksize;
	}
	
	task = GetSCSITask();
	require((task != 0), ErrorExit);
	
	if (dataBuffer->getDirection() == kIODirectionIn)
	{
		cmdStatus = READ_6(
			task, 
			dataBuffer, 
			blksize, 
			0x0,
			IsFixedBlockSize() ? 0x1 : 0x0,
			transferSize,
			0x00);
	}
	else
	{
		cmdStatus = WRITE_6(
			task, 
			dataBuffer, 
			blksize, 
			IsFixedBlockSize() ? 0x1 : 0x0,
			transferSize, 
			0x00);

		flags |= ST_WRITTEN_TOGGLE;
	}
	
	if (cmdStatus == true)
		taskStatus = DoSCSICommand(task, SCSI_MOTION_TIMEOUT);
		
	*realizedBytes = GetRealizedDataTransferCount(task);

	if (taskStatus == kSCSITaskStatus_GOOD)
		status = kIOReturnSuccess;
	
	ReleaseSCSITask(task);
	
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

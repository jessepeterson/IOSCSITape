#include <AssertMacros.h>
#include <sys/conf.h>

#include <IOKit/scsi/SCSICommandOperationCodes.h>

#include "IOSCSITape.h"

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
private:
	static unsigned int major;
	static struct cdevsw cdevsw;
public:
	CdevMajorIniter(void);
	~CdevMajorIniter(void);
};

CdevMajorIniter::CdevMajorIniter(void)
{
	major = cdevsw_add(-1, &cdevsw);
}

CdevMajorIniter::~CdevMajorIniter(void)
{
	major = cdevsw_remove(major, &cdevsw);
}

static CdevMajorIniter CdevMajorIniter;

int gTapeCounter = -1;

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
	/* For now, just increment the counter for each device. In the
	 * future we may want to implement some sort of reclamation of
	 * device types. */
	tapeNumber = ++gTapeCounter;
	
	/* always initialize */
	return true;
}

void
IOSCSITape::StartDeviceSupport(void)
{
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

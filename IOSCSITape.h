/*
 *  IOSCSITape.h
 *  IOSCSITape
 *
 *  This software is licensed under an MIT license. See LICENSE.txt.
 *  Created by Jesse Peterson on 12/13/09.
 *
 */

#include <IOKit/scsi/IOSCSIMultimediaCommandsDevice.h>
#include <IOKit/scsi/SCSICmds_MODE_Definitions.h>

/* These were defined in the OS-supplied SCSICommandOperationCodes.h but
 * "#if 0"-ed out. May need to back these out if the official ones ever
 * get uncommented. */
/* Commands defined by the T10:997-D SCSI-3 Stream Commands (SSC) command
 * specification.  The definitions and section numbers are based on section 5
 * of the revision 22, January 1, 2000 version of the specification. 
 */
enum
{
    kSCSICmd_ERASE                          = 0x19, /* Sec. 5.3.1: Mandatory */
    kSCSICmd_FORMAT_MEDIUM                  = 0x04, /* Sec. 5.3.2: Optional */
    kSCSICmd_LOAD_UNLOAD                    = 0x1B, /* Sec. 5.3.3: Optional */
    kSCSICmd_LOCATE                         = 0x2B, /* Sec. 5.3.4: Optional */
    kSCSICmd_MOVE_MEDIUM                    = 0xA5, /* SMC: Optional */
    kSCSICmd_READ_BLOCK_LIMITS              = 0x05, /* Sec. 5.3.6: Mandatory */
    kSCSICmd_READ_ELEMENT_STATUS            = 0xB8, /* SMC: Optional */
    kSCSICmd_READ_POSITION                  = 0x34, /* Sec. 5.3.7: Mandatory */
    kSCSICmd_READ_REVERSE                   = 0x0F, /* Sec. 5.3.8: Optional */
    kSCSICmd_RECOVER_BUFFERED_DATA          = 0x14, /* Sec. 5.3.9: Optional */
    kSCSICmd_REPORT_DENSITY_SUPPORT         = 0x44, /* Sec. 5.3.10: Mandatory*/
    kSCSICmd_REWIND                         = 0x01, /* Sec. 5.3.11: Mandatory*/
    kSCSICmd_SPACE                          = 0x11, /* Sec. 5.3.12: Mandatory*/
    kSCSICmd_VERIFY_6                       = 0x13, /* Sec. 5.3.13: Optional */
    kSCSICmd_WRITE_FILEMARKS                = 0x10  /* Sec. 5.3.15: Mandatory*/
};

enum
{
	kSCSIReadPositionServiceAction_ShortFormBlockID			= 0x00,
	kSCSIReadPositionServiceAction_ShortFormVendorSpecific	= 0x01,
	kSCSIReadPositionServiceAction_LongForm					= 0x06,
	kSCSIReadPositionServiceAction_ExtendedForm				= 0x08
};

struct SCSI_ModeSense_Default
{
	SPCModeParameterHeader6			header;
	ModeParameterBlockDescriptor	descriptor;
};

typedef struct SCSI_ModeSense_Default SCSI_ModeSense_Default;

enum SCSISpaceCode
{
	kSCSISpaceCode_LogicalBlocks		= 0x0,
	kSCSISpaceCode_Filemarks			= 0x1,
	kSCSISpaceCode_SequentialFilemarks	= 0x2,
	kSCSISpaceCode_EndOfData			= 0x3,
	kSCSISpaceCode_Setmarks				= 0x4,
	kSCSISpaceCode_SequentialSetmarks	= 0x5
};

struct SCSI_ReadPositionShortForm
{
	UInt8	flags;
	UInt8	partitionNumber;
	UInt32	firstLogicalObjectLocation;
	UInt32	 lastLogicalObjectLocation;
	UInt32	logicalObjectsInObjectBuffer;
	UInt32	bytesInObjectBuffer;
};

enum ReadPositionShortFormFlags
{
	kSCSIReadPositionShortForm_BeginningOfPartition			= 0x80,
	kSCSIReadPositionShortForm_EndOfPartition				= 0x40,
	kSCSIReadPositionShortForm_LogicalObjectCountUnknown	= 0x20,
	kSCSIReadPositionShortForm_ByteCountUnknown				= 0x10,
	kSCSIReadPositionShortForm_LogicalObjectLocationUnknown	= 0x04,
	kSCSIReadPositionShortForm_PositionError				= 0x02
};

#define SMH_DSP_BUFF_MODE       0x70
#define SMH_DSP_BUFF_MODE_OFF   0x00
#define SMH_DSP_BUFF_MODE_ON    0x10
#define SMH_DSP_BUFF_MODE_MLTI  0x20
#define SMH_DSP_WRITE_PROT      0x80

#define TAPE_FORMAT "rst%d"
#define STATUS_LOG(s, ...) IOLog(TAPE_FORMAT ": " s "\n", tapeNumber, ## __VA_ARGS__)

#define ST_DEVOPEN			0x01
#define ST_READONLY			0x02
#define ST_BUFF_MODE		0x04

#define SENSE_FILEMARK		0x01
#define SENSE_EOD			0x02
#define SENSE_BOM			0x04
#define SENSE_ILI			0x08
#define SENSE_NOTREADY		0x10

class IOSCSITape : public IOSCSIPrimaryCommandsDevice {
	OSDeclareDefaultStructors(IOSCSITape)
public:
	unsigned int flags, sense_flags;
	static IOSCSITape **devices;
	
	int blksize;
	int density;
	
	int blkmin;
	int blkmax;
	
	int blkno;
	int fileno;

	/* Utilities */
	bool IsFixedBlockSize(void);

	/* SCSI Operations */
	IOReturn Rewind(void);
	IOReturn GetDeviceDetails(void);
	IOReturn GetDeviceBlockLimits(void);
	IOReturn TestUnitReady(void);
	IOReturn WriteFilemarks(int);
	IOReturn Space(SCSISpaceCode, int);
	IOReturn LoadUnload(int);
	IOReturn ReadPosition(SCSI_ReadPositionShortForm *, bool);
	IOReturn ReadWrite(IOMemoryDescriptor *, int *);
	IOReturn SetDeviceDetails(SCSI_ModeSense_Default *);
	IOReturn SetBlockSize(int);
private:
	int tapeNumber;
	
	/* SCSI Operations */
	SCSI_ModeSense_Default lastModeData;
	SCSITaskStatus DoSCSICommand(SCSITaskIdentifier, UInt32);
	void GetSense(SCSITaskIdentifier);
	void InterpretSense(SCSI_Sense_Data *);

	/* utilities for major/minor to instance tracking */
	void *cdev_node;
	static int deviceCount;

	bool FindDeviceMinorNumber(void);
	bool GrowDeviceMinorNumberMemory(void);
	void ClearDeviceMinorNumber(void);
	
	/* pure function overrides from IOSCSIPrimaryCommandsDevice */
	UInt32 GetInitialPowerState(void);
	void HandlePowerChange(void);
	void HandleCheckPowerState(void);
	void TicklePowerManager(void);
	
	bool InitializeDeviceSupport(void);
	void StartDeviceSupport(void);
	void SuspendDeviceSupport(void);
	void ResumeDeviceSupport(void);
	void StopDeviceSupport(void);
	void TerminateDeviceSupport(void);

	UInt32 GetNumberOfPowerStateTransitions(void);
	bool ClearNotReadyStatus(void);
	
	/* SSC Implicit Address Commands */
	bool ERASE_6(
		SCSITaskIdentifier,
		SCSICmdField1Bit,
		SCSICmdField1Bit,
		SCSICmdField1Byte);
	
	bool READ_6(
		SCSITaskIdentifier,
		IOMemoryDescriptor *,
		UInt32,
		SCSICmdField1Bit,
		SCSICmdField1Bit,
		SCSICmdField3Byte,
		SCSICmdField1Byte);
	
	bool SPACE_6(
		SCSITaskIdentifier,
		SCSICmdField4Bit,
		SCSICmdField3Byte,
		SCSICmdField1Byte);
	
	bool WRITE_6(
		SCSITaskIdentifier,
		IOMemoryDescriptor *,
		UInt32,
		SCSICmdField1Bit,
		SCSICmdField3Byte,
		SCSICmdField1Byte);
	
	bool WRITE_FILEMARKS_6(
		SCSITaskIdentifier,
		SCSICmdField1Bit,
		SCSICmdField1Bit,
		SCSICmdField3Byte,
		SCSICmdField1Byte);
	
	/* SSC Common Commands */
	bool LOAD_UNLOAD(
		SCSITaskIdentifier,
		SCSICmdField1Bit,
		SCSICmdField1Bit,
		SCSICmdField1Bit,
		SCSICmdField1Bit,
		SCSICmdField1Bit,
		SCSICmdField1Byte);
	
	bool READ_BLOCK_LIMITS(
		SCSITaskIdentifier,
		IOMemoryDescriptor *,
		SCSICmdField1Byte);
	
	bool READ_POSITION(
		SCSITaskIdentifier,
		IOMemoryDescriptor *,
		SCSICmdField5Bit,
		SCSICmdField2Byte,
		SCSICmdField1Byte);
	
	bool REWIND(
		SCSITaskIdentifier,
		SCSICmdField1Bit,
		SCSICmdField1Byte);
};

int st_rewind(IOSCSITape *st);
int st_space(IOSCSITape *st, SCSISpaceCode type, int number);
int st_write_filemarks(IOSCSITape *st, int number);
int st_unload(IOSCSITape *st);
int st_rdpos(IOSCSITape *st, bool vendor, unsigned int *data);

int st_open(dev_t dev, int flags, int devtype, struct proc *p);
int st_close(dev_t dev, int flags, int devtype, struct proc *p);
int st_readwrite(dev_t dev, struct uio *uio, int ioflag);
int st_ioctl(dev_t dev, u_long cmd, caddr_t data, int fflag, struct proc *p);

IOMemoryDescriptor *IOMemoryDescriptorFromUIO(struct uio *);

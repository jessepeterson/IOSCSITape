#include <IOKit/scsi/IOSCSIMultimediaCommandsDevice.h>

class IOSCSITape : public IOSCSIPrimaryCommandsDevice {
	OSDeclareDefaultStructors(IOSCSITape)
private:
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
};

#include "IOSCSITape.h"

#define super IOSCSIPrimaryCommandsDevice
OSDefineMetaClassAndStructors(IOSCSITape, IOSCSIPrimaryCommandsDevice)

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

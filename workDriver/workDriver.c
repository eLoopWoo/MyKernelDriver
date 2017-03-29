#include "stdafx.h"

/*
	workDriver - Main file
	This file contains a very simple implementation of a WDM driver. Note that it does not support all
	WDM functionality, or any functionality sufficient for practical use. The only thing this driver does
	perfectly, is loading and unloading.

	To install the driver, go to Control Panel -> Add Hardware Wizard, then select "Add a new hardware device".
	Select "manually select from list", choose device category, press "Have Disk" and enter the path to your
	INF file.
	Note that not all device types (specified as Class in INF file) can be installed that way.

	To start/stop this driver, use Windows Device Manager (enable/disable device command).

	If you want to speed up your driver development, it is recommended to see the BazisLib library, that
	contains convenient classes for standard device types, as well as a more powerful version of the driver
	wizard. To get information about BazisLib, see its website:
		http://bazislib.sysprogs.org/
*/

void workDriverUnload(IN PDRIVER_OBJECT DriverObject);
NTSTATUS workDriverCreateClose(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
NTSTATUS workDriverDefaultHandler(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
NTSTATUS workDriverAddDevice(IN PDRIVER_OBJECT  DriverObject, IN PDEVICE_OBJECT  PhysicalDeviceObject);
NTSTATUS workDriverPnP(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);

NTSTATUS RegisterDriverDeviceName(IN PDRIVER_OBJECT DriverObject);
NTSTATUS RegisterDriverDeviceLink();



typedef struct _deviceExtension
{
	PDEVICE_OBJECT DeviceObject;
	PDEVICE_OBJECT TargetDeviceObject;
	PDEVICE_OBJECT PhysicalDeviceObject;
	UNICODE_STRING DeviceInterface;
} workDriver_DEVICE_EXTENSION, *PworkDriver_DEVICE_EXTENSION;

// {22beac1a-f532-497c-9ea8-a5385f24f87e}
static const GUID GUID_workDriverInterface = {0x22BEAC1A, 0xf532, 0x497c, {0x9e, 0xa8, 0xa5, 0x38, 0x5f, 0x24, 0xf8, 0x7e } };

#ifdef __cplusplus
extern "C" NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING  RegistryPath);
#endif


void workDriverUnload(IN PDRIVER_OBJECT DriverObject)
{
	DbgPrint("Goodbye from workDriver!\n");
}

NTSTATUS workDriverCreateClose(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS workDriverDefaultHandler(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
	PworkDriver_DEVICE_EXTENSION deviceExtension = NULL;
	
	IoSkipCurrentIrpStackLocation(Irp);
	deviceExtension = (PworkDriver_DEVICE_EXTENSION) DeviceObject->DeviceExtension;
	return IoCallDriver(deviceExtension->TargetDeviceObject, Irp);
}

NTSTATUS workDriverAddDevice(IN PDRIVER_OBJECT  DriverObject, IN PDEVICE_OBJECT  PhysicalDeviceObject)
{
	PDEVICE_OBJECT DeviceObject = NULL;
	PworkDriver_DEVICE_EXTENSION pExtension = NULL;
	NTSTATUS status;
	
	status = IoCreateDevice(DriverObject,
						    sizeof(workDriver_DEVICE_EXTENSION),
							NULL,
							FILE_DEVICE_UNKNOWN,
							0,
							0,
							&DeviceObject);

	if (!NT_SUCCESS(status))
		return status;

	pExtension = (PworkDriver_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

	pExtension->DeviceObject = DeviceObject;
	pExtension->PhysicalDeviceObject = PhysicalDeviceObject;
	pExtension->TargetDeviceObject = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);

	status = IoRegisterDeviceInterface(PhysicalDeviceObject, &GUID_workDriverInterface, NULL, &pExtension->DeviceInterface);
	ASSERT(NT_SUCCESS(status));

	DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
	return STATUS_SUCCESS;
}


NTSTATUS workDriverIrpCompletion(
					  IN PDEVICE_OBJECT DeviceObject,
					  IN PIRP Irp,
					  IN PVOID Context
					  )
{
	PKEVENT Event = (PKEVENT) Context;

	UNREFERENCED_PARAMETER(DeviceObject);
	UNREFERENCED_PARAMETER(Irp);

	KeSetEvent(Event, IO_NO_INCREMENT, FALSE);

	return(STATUS_MORE_PROCESSING_REQUIRED);
}

NTSTATUS workDriverForwardIrpSynchronous(
							  IN PDEVICE_OBJECT DeviceObject,2
							  IN PIRP Irp
							  )
{
	PworkDriver_DEVICE_EXTENSION   deviceExtension;
	KEVENT event;
	NTSTATUS status;

	KeInitializeEvent(&event, NotificationEvent, FALSE);
	deviceExtension = (PworkDriver_DEVICE_EXTENSION) DeviceObject->DeviceExtension;

	IoCopyCurrentIrpStackLocationToNext(Irp);

	IoSetCompletionRoutine(Irp, workDriverIrpCompletion, &event, TRUE, TRUE, TRUE);

	status = IoCallDriver(deviceExtension->TargetDeviceObject, Irp);

	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		status = Irp->IoStatus.Status;
	}
	return status;
}

NTSTATUS workDriverPnP(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
	PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
	PworkDriver_DEVICE_EXTENSION pExt = ((PworkDriver_DEVICE_EXTENSION)DeviceObject->DeviceExtension);
	NTSTATUS status;

	ASSERT(pExt);

	switch (irpSp->MinorFunction)
	{
	case IRP_MN_START_DEVICE:
		IoSetDeviceInterfaceState(&pExt->DeviceInterface, TRUE);
		Irp->IoStatus.Status = STATUS_SUCCESS;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;

	case IRP_MN_QUERY_REMOVE_DEVICE:
		Irp->IoStatus.Status = STATUS_SUCCESS;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;

	case IRP_MN_REMOVE_DEVICE:
		IoSetDeviceInterfaceState(&pExt->DeviceInterface, FALSE);
		status = workDriverForwardIrpSynchronous(DeviceObject, Irp);
		IoDetachDevice(pExt->TargetDeviceObject);
		IoDeleteDevice(pExt->DeviceObject);
		RtlFreeUnicodeString(&pExt->DeviceInterface);
		Irp->IoStatus.Status = STATUS_SUCCESS;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;

	case IRP_MN_QUERY_PNP_DEVICE_STATE:
		status = workDriverForwardIrpSynchronous(DeviceObject, Irp);
		Irp->IoStatus.Information = 0;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}
	return workDriverDefaultHandler(DeviceObject, Irp);
}


NTSTATUS workDriverDispachioControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{

}



NTSTATUS RegisterDriverDeviceName(IN PDRIVER_OBJECT DriverObject)
{
	NTSTATUS ntStatus;
	UNICODE_STRING unicodeString;
	RtlInitUnicodeString(&unicodeString, DeviceNameBuffer);
	ntStatus = IoCreateDevice
		(
		    DriverObject,
			0,
			&unicodeString,
			FILE_DEVICE_RK,
			0,
			TRUE,&MSNetDiagDeviceObject
		);
	return (ntStatus);
}



NTSTATUS defaultDispatch(IN PDEVICE_OBJECT DeviceObject, IN PIRP IRP)
{
	((*IRP).IoStatus).Status = STATUS_SUCCESS;
	((*IRP).IoStatus).Information = 0;
	IoCompleteRequest(IRP, IO_NO_INCREMENT);
	return (STATUS_SUCCESS);
}

NTSTATUS dispatchIOControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP IRP)
{
	PIO_STACK_LOCATION irpStack;
	PVOID inputBuffer;
	PVOID outputBuffer;
	ULONG inBufferLength;
	ULONG outBufferLength;
	ULONG ioctrlcode;
	NTSTATUS ntStatus;
	ntStatus = STATUS_SUCCESS;
	((*IRP).IoStatus).Status = STATUS_SUCCESS;
	((*IRP).IoStatus).Information = 0;
	inputBuffer = (*IRP).AssociatedIrp.SystemBuffer;
	outputBuffer = (*IRP).AssociatedIrp.SystemBuffer;
	irpStack = IoGetCurrentIrpStackLocation(IRP);
	inBufferLength = (*irpStack).Parameters.DeviceIoControl.InputBufferLength;
	outBufferLength = (*irpStack).Parameters.DeviceIoControl.OutputBufferLength;
	ioctrlcode = (*irpStack).Parameters.DeviceIoControl.IoControlCode;

	DBG_TRACE("dispatchIOControl","Received a command");
	switch(ioctrlcode)
	{
	case IOCTL_TEST_CMD:
		{
			TestCommand(inputBuffer, outputBuffer, inBufferLength, outBufferLength);
			((*IRP).IoStatus).Information = outBufferLength;
		}
		break;
	default:
		{
			DBG_TRACE("dispatchIOControl","control code not recognized");
		}
		break;
	}
	IoCompleteRequest(IRP, IO_NO_INCREMENT);
	return(ntStatus);
}

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING  RegistryPath)
{
	unsigned i;
	NTSTATUS ntStatus;

	DbgPrint("Hello from workDriver!\n");
	//
	DbgPrint("Driver Entry!\n");
	ntStatus = RegisterDriverDeviceName(DriverObject);
	if(NT_SUCCESS(ntStatus))
	{	
		DbgPrint("Driver Entry, Failed to create device");
		return ntStatus;
	}
	DbgPrint("Driver Entry, Registerint drive;s dymbolic linl");
	ntStatus = RegisterDriverDeviceLink();
	if(!NT_SUCCESS(ntStatus))
	{
		DbgPrint("Driver Entry Failed to creat symbolic link");
		return ntStatus;
	}
	//
	for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
		DriverObject->MajorFunction[i] = workDriverDefaultHandler;

	DriverObject->MajorFunction[IRP_MJ_CREATE] = workDriverCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = workDriverCreateClose;
	DriverObject->MajorFunction[IRP_MJ_PNP] = workDriverPnP;
	
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = dispatchIOControl;

	DriverObject->DriverUnload = workDriverUnload;
	DriverObject->DriverStartIo = NULL;
	DriverObject->DriverExtension->AddDevice = workDriverAddDevice;

	return STATUS_SUCCESS;
}


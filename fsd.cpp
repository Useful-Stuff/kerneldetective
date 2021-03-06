/*
 * Copyright (c) 2008 Arab Team 4 Reverse Engineering. All rights reserved.
 *
 * Module Name:
 *
 *        fsd.c
 *
 * Abstract:
 *
 *        This module implements various routines used to relocate image files.
 *
 * Author:
 *
 *        GamingMasteR
 *
 */
 







#include "KeDetective.h"
#include "fsd.h"
#include "module.h"
#include <scsi.h>


FSD_OBJECT FsdFastFat, FsdNtfs;

NTSTATUS
FsCallDriver(
    PDRIVER_OBJECT DriverObject,
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
)
{
    VMProtectBegin;

    PIO_STACK_LOCATION IrpStackLocation;
    NTSTATUS Status;

    if (--Irp->CurrentLocation <= 0) 
    {
        return STATUS_UNSUCCESSFUL;
    }
    IrpStackLocation = IoGetNextIrpStackLocation(Irp);
    Irp->Tail.Overlay.CurrentStackLocation = IrpStackLocation;
    IrpStackLocation->DeviceObject = DeviceObject;
    
    if (DriverObject == NULL)
        DriverObject = DeviceObject->DriverObject;

    if (DriverObject == FsdFastFat.DriverObject && FsdFastFat.MajorFunction[IrpStackLocation->MajorFunction])
    {
        Print("Fastfat Call MJ[%d] %p", IrpStackLocation->MajorFunction, FsdFastFat.MajorFunction[IrpStackLocation->MajorFunction]);
        Status = FsdFastFat.MajorFunction[IrpStackLocation->MajorFunction](DeviceObject, Irp);
    }
    else if (DriverObject == FsdNtfs.DriverObject && FsdNtfs.MajorFunction[IrpStackLocation->MajorFunction])
    {
        Print("Ntfs Call MJ[%d] %p", IrpStackLocation->MajorFunction, FsdNtfs.MajorFunction[IrpStackLocation->MajorFunction]);
        Status = FsdNtfs.MajorFunction[IrpStackLocation->MajorFunction](DeviceObject, Irp);
    }
    else
    {
        Print("Normal Call MJ[%d] %p", IrpStackLocation->MajorFunction, DriverObject->MajorFunction[IrpStackLocation->MajorFunction]);
        Status = DriverObject->MajorFunction[IrpStackLocation->MajorFunction](DeviceObject, Irp);
    }

    VMProtectEnd;

    return Status;
};


BOOLEAN GetDriveObject(  
    IN ULONG DriveNumber,   
    OUT PDEVICE_OBJECT *DeviceObject,   
    OUT PDEVICE_OBJECT *RealDevice   
   )
{
    WCHAR driveName[] = L"\\DosDevices\\A:\\";
    UNICODE_STRING deviceName;
    HANDLE deviceHandle;
    OBJECT_ATTRIBUTES objectAttributes;
    IO_STATUS_BLOCK ioStatus;
    PFILE_OBJECT fileObject;
    NTSTATUS status;
   
    if (DriveNumber >= 'A' && DriveNumber <= 'Z')
    {
        driveName[12] = (CHAR)DriveNumber;
    }
    else if (DriveNumber >= 'a' && DriveNumber <= 'z')
    {
        driveName[12] = (CHAR)DriveNumber - 'a' + 'A';
    }
    else   
    {
        return FALSE;
    }
   
    RtlInitUnicodeString(&deviceName, driveName);
   
    InitializeObjectAttributes(&objectAttributes,   
                                &deviceName,   
                                OBJ_CASE_INSENSITIVE,   
                                NULL,   
                                NULL);
   
    status = IoCreateFile( &deviceHandle,   
                            SYNCHRONIZE | FILE_ANY_ACCESS,   
                            &objectAttributes,   
                            &ioStatus,   
                            NULL,   
                            0,   
                            FILE_SHARE_READ | FILE_SHARE_WRITE,   
                            FILE_OPEN,   
                            FILE_SYNCHRONOUS_IO_NONALERT | FILE_DIRECTORY_FILE,   
                            NULL,   
                            0,   
                            CreateFileTypeNone,   
                            NULL,   
                            0x100);
   
    if (!NT_SUCCESS(status))
    {
        Print("Could not open drive %c: %x\n", DriveNumber, status);
        return FALSE;
    }
   
    status = ObReferenceObjectByHandle(deviceHandle,   
                                        SYNCHRONIZE,   
                                        *IoFileObjectType,   
                                        KernelMode,   
                                        (PVOID *)&fileObject,   
                                        NULL);
   
    if (!NT_SUCCESS(status))
    {
        Print("Could not get fileobject from handle: %c\n", DriveNumber);
        ZwClose(deviceHandle);
        return FALSE;
    }
   
    if (fileObject->Vpb == 0 || fileObject->Vpb->RealDevice == NULL)
    {
        ObDereferenceObject(fileObject);
        ZwClose(deviceHandle);
        return FALSE;
    }
   
    *DeviceObject = fileObject->Vpb->DeviceObject;
    *RealDevice = fileObject->Vpb->RealDevice;
   
    ObDereferenceObject(fileObject);
    ZwClose(deviceHandle);
   
    return TRUE;
}


NTSTATUS   
IoCompletionRoutine(  
    IN PDEVICE_OBJECT  DeviceObject,   
    IN PIRP  Irp,   
    IN PVOID  Context   
   )
{
    *Irp->UserIosb = Irp->IoStatus;

    if (Irp->MdlAddress)
    {
        MmUnlockPages(Irp->MdlAddress);
        IoFreeMdl(Irp->MdlAddress);
        Irp->MdlAddress = NULL;
    }

    if (Irp->UserEvent)
        KeSetEvent(Irp->UserEvent, IO_NO_INCREMENT, 0);
    IoFreeIrp(Irp);
   
    return STATUS_MORE_PROCESSING_REQUIRED;
}


/*NTSTATUS
IoReadFile(
    HANDLE Handle,
    PFILE_OBJECT FileObject,
    PVOID Buffer,
    ULONG FileSize
)
{
    VMProtectBegin;

    NTSTATUS Status = STATUS_SUCCESS;
    PDEVICE_OBJECT DeviceObject;
    PIRP Irp;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER StartingOffset;

    
    if (FileObject) Print("PointerCount = %d", OBJECT_TO_OBJECT_HEADER(FileObject)->ReferenceCount);
    if (Handle)
    {
        Status = ObReferenceObjectByHandle(Handle,
            SYNCHRONIZE,
            *IoFileObjectType,
            KernelMode,
            (PVOID*)&FileObject,
            NULL);

        if (!NT_SUCCESS(Status)) return Status;
    }
    if (FileObject) Print("PointerCount = %d", OBJECT_TO_OBJECT_HEADER(FileObject)->ReferenceCount);

    

    DeviceObject = IoGetBaseFileSystemDeviceObject(FileObject);

    StartingOffset.QuadPart = 0;

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_READ,
        DeviceObject,
        Buffer,
        FileSize,
        &StartingOffset,
        &Event,
        &IoStatusBlock);

    if (Irp == NULL)
    {
        if (Handle)
            ObDereferenceObject(FileObject);
        return STATUS_UNSUCCESSFUL;
    };

    IoGetNextIrpStackLocation(Irp)->FileObject = FileObject;

    IoGetNextIrpStackLocation(Irp)->Parameters.Read.Key = 0;

    if (FileObject) Print("PointerCount = %d", OBJECT_TO_OBJECT_HEADER(FileObject)->ReferenceCount);
    Status = FsCallDriver(DeviceObject, Irp);
    if (FileObject) Print("PointerCount = %d", OBJECT_TO_OBJECT_HEADER(FileObject)->ReferenceCount);

    if (STATUS_PENDING == Status)
        KeWaitForSingleObject(&Event, Executive, KernelMode, TRUE, NULL);

    if (Handle)
        ObDereferenceObject(FileObject);

    Status = IoStatusBlock.Status;
    
    VMProtectEnd;
    return Status;
};*/


NTSTATUS
IoReadFile(
           HANDLE Handle,
           PVOID Buffer,
           ULONG FileSize,
           PLARGE_INTEGER FileOffset
           )
{
    VMProtectBegin;

    NTSTATUS Status = STATUS_SUCCESS;
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT DeviceObject;
    PIRP Irp;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER StartingOffset;



    Status = ObReferenceObjectByHandle(Handle,
        SYNCHRONIZE,
        *IoFileObjectType,
        KernelMode,
        (PVOID*)&FileObject,
        NULL);

    if (!NT_SUCCESS(Status)) return Status;

    DeviceObject = IoGetBaseFileSystemDeviceObject(FileObject);

    StartingOffset = *FileOffset;

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_READ,
        DeviceObject,
        Buffer,
        FileSize,
        &StartingOffset,
        &Event,
        &IoStatusBlock);

    if (Irp == NULL)
    {
        ObDereferenceObject(FileObject);
        return STATUS_UNSUCCESSFUL;
    };

    IoGetNextIrpStackLocation(Irp)->FileObject = FileObject;

    IoGetNextIrpStackLocation(Irp)->Parameters.Read.Key = 0;

    Status = FsCallDriver(NULL, DeviceObject, Irp);
    
    if (STATUS_PENDING == Status)
        KeWaitForSingleObject(&Event, Executive, KernelMode, TRUE, NULL);

    ObDereferenceObject(FileObject);
    
    Status = IoStatusBlock.Status;

    VMProtectEnd;
    return Status;
};


NTSTATUS
IoWriteFile(
    HANDLE Handle,
    PVOID Buffer,
    ULONG FileSize,
    PLARGE_INTEGER FileOffset
)
{
    VMProtectBegin;

    NTSTATUS Status = STATUS_SUCCESS;
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT DeviceObject;
    PIRP Irp;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER StartingOffset;

    
    Status = ObReferenceObjectByHandle(Handle,
        SYNCHRONIZE,
        *IoFileObjectType,
        KernelMode,
        (PVOID*)&FileObject,
        NULL);

    if (!NT_SUCCESS(Status)) return Status;

    DeviceObject = IoGetBaseFileSystemDeviceObject(FileObject);

    StartingOffset = *FileOffset;

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_WRITE,
        DeviceObject,
        Buffer,
        FileSize,
        &StartingOffset,
        &Event,
        &IoStatusBlock);

    if (Irp == NULL)
    {
        ObDereferenceObject(FileObject);
        return STATUS_UNSUCCESSFUL;
    };

    IoGetNextIrpStackLocation(Irp)->FileObject = FileObject;

    IoGetNextIrpStackLocation(Irp)->Parameters.Write.Key = 0;

    Status = FsCallDriver(NULL, DeviceObject, Irp);

    if (STATUS_PENDING == Status)
        KeWaitForSingleObject(&Event, Executive, KernelMode, TRUE, NULL);

    ObDereferenceObject(FileObject); 

    Status = IoStatusBlock.Status;
    
    VMProtectEnd;
    return Status;
};


NTSTATUS   
IrpFileCreate( 
    IN PUNICODE_STRING FileName,   
    IN ACCESS_MASK DesiredAccess,   
    IN ULONG FileAttributes,   
    IN ULONG ShareAccess,   
    IN ULONG CreateDisposition,   
    IN ULONG CreateOptions,   
    IN PDEVICE_OBJECT DeviceObject,   
    IN PDEVICE_OBJECT RealDevice,   
    OUT PFILE_OBJECT *Object   
   )
{
    VMProtectBegin;

    NTSTATUS status;
    KEVENT event;
    PIRP irp;
    IO_STATUS_BLOCK ioStatus;
    PIO_STACK_LOCATION irpSp;
    IO_SECURITY_CONTEXT securityContext;
    ACCESS_STATE accessState;
    OBJECT_ATTRIBUTES objectAttributes;
    PFILE_OBJECT fileObject;
    PVOID auxData;
    CSHORT fileObjectSize = sizeof(FILE_OBJECT);
   
    if (!IsXp)
    {
        fileObjectSize += 0x10;
    }

    KeInitializeEvent(&event, SynchronizationEvent, FALSE);
    irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
   
    if (irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
   
    InitializeObjectAttributes(&objectAttributes, NULL, OBJ_CASE_INSENSITIVE, 0, NULL);
   
    status = ObCreateObject(KernelMode,   
                            *IoFileObjectType,   
                            &objectAttributes,   
                            KernelMode,   
                            NULL,   
                            fileObjectSize,   
                            0,   
                            0,   
                            (PVOID *)&fileObject);
    //Print("fileObject->Count = %d", OBJECT_TO_OBJECT_HEADER(fileObject)->ReferenceCount);
   
    if (!NT_SUCCESS(status))
    {
        IoFreeIrp(irp);
        return status;
    }
   
    RtlZeroMemory(fileObject, fileObjectSize);
    fileObject->Type = IO_TYPE_FILE;
    fileObject->Size = fileObjectSize;
    fileObject->DeviceObject = RealDevice;
	fileObject->RelatedFileObject = NULL;
    fileObject->Flags = FO_SYNCHRONOUS_IO;
    fileObject->FileName.MaximumLength = FileName->MaximumLength;
    fileObject->FileName.Buffer = (PWSTR)ExAllocatePoolWithTag(NonPagedPool, FileName->MaximumLength, 'abiS');
   
    if (fileObject->FileName.Buffer == NULL)
    {
        IoFreeIrp(irp);
        ObDereferenceObject(fileObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
   
    RtlCopyUnicodeString(&fileObject->FileName, FileName);
    KeInitializeEvent(&fileObject->Lock, SynchronizationEvent, FALSE);
    KeInitializeEvent(&fileObject->Event, NotificationEvent, FALSE);
   
    irp->MdlAddress = NULL;
    irp->Flags |= IRP_CREATE_OPERATION | IRP_SYNCHRONOUS_API;
    irp->RequestorMode = KernelMode;
    irp->UserIosb = &ioStatus;
    irp->UserEvent = &event;
    irp->PendingReturned = FALSE;
    irp->Cancel = FALSE;
    irp->CancelRoutine = NULL;
    irp->Tail.Overlay.Thread = PsGetCurrentThread();
    irp->Tail.Overlay.AuxiliaryBuffer = NULL;
    irp->Tail.Overlay.OriginalFileObject = fileObject;
   
    auxData = MmAlloc(PAGE_SIZE);
    status = SeCreateAccessState(  &accessState,   
                                    auxData,   
                                    DesiredAccess,   
                                    IoGetFileObjectGenericMapping());

    MmFree(auxData);

    accessState.PreviouslyGrantedAccess |= accessState.RemainingDesiredAccess;  
    accessState.RemainingDesiredAccess = 0;
   
    if (!NT_SUCCESS(status))
    {
        IoFreeIrp(irp);
        ExFreePoolWithTag(fileObject->FileName.Buffer, 'abiS');
        ObDereferenceObject(fileObject);
        return status;
    }
   
    securityContext.SecurityQos = NULL;
    securityContext.AccessState = &accessState;
    securityContext.DesiredAccess = DesiredAccess;
    securityContext.FullCreateOptions = 0;
   
    irpSp = IoGetNextIrpStackLocation(irp);
    irpSp->MajorFunction = IRP_MJ_CREATE;
    irpSp->DeviceObject = DeviceObject;
    irpSp->FileObject = fileObject;
    irpSp->Parameters.Create.SecurityContext = &securityContext;
    irpSp->Parameters.Create.Options = (CreateDisposition << 24) | CreateOptions;
    irpSp->Parameters.Create.FileAttributes = (USHORT)FileAttributes;
    irpSp->Parameters.Create.ShareAccess = (USHORT)ShareAccess;
    irpSp->Parameters.Create.EaLength = 0;
   
    IoSetCompletionRoutine(irp, IoCompletionRoutine, NULL, TRUE, TRUE, TRUE);
    status = FsCallDriver(NULL, DeviceObject, irp);
    //Print("fileObject->Count = %d", OBJECT_TO_OBJECT_HEADER(fileObject)->ReferenceCount);

   
    if (status == STATUS_PENDING)
        KeWaitForSingleObject(&event, Executive, KernelMode, TRUE, NULL);
   
    status = ioStatus.Status;
   
    if (!NT_SUCCESS(status))
    {
		if (fileObject)
		{
			ExFreePoolWithTag(fileObject->FileName.Buffer, 'abiS');
			fileObject->FileName.Length = 0;
			fileObject->DeviceObject = NULL;
			ObDereferenceObject(fileObject);
		}
    }
    else   
    {
        InterlockedIncrement(&fileObject->DeviceObject->ReferenceCount);
   
        if (fileObject->Vpb)
        {
            InterlockedIncrement((LONG *)&fileObject->Vpb->ReferenceCount);
        }
        *Object = fileObject;
    }

    VMProtectEnd;
   
    return status;  
}


NTSTATUS
IrpFileDelete(
    IN PFILE_OBJECT FileObject,
    IN PDEVICE_OBJECT DeviceObject
   )
{
    VMProtectBegin;

    NTSTATUS status;
    KEVENT event;
    PIRP irp;
    IO_STATUS_BLOCK ioStatus;
    PIO_STACK_LOCATION irpSp;
    FILE_DISPOSITION_INFORMATION FileInformation;
   


    KeInitializeEvent(&event, SynchronizationEvent, FALSE);
    irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
   
    if (irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
   
   
   
    irp->MdlAddress = NULL;
    irp->Flags |= IRP_BUFFERED_IO | IRP_SYNCHRONOUS_API;
    irp->RequestorMode = KernelMode;
    irp->UserIosb = &ioStatus;
    irp->UserEvent = &event;
    irp->PendingReturned = FALSE;
    irp->Cancel = FALSE;
    irp->CancelRoutine = NULL;
    irp->Tail.Overlay.Thread = PsGetCurrentThread();
    irp->Tail.Overlay.AuxiliaryBuffer = NULL;
    irp->Tail.Overlay.OriginalFileObject = FileObject;
    irp->AssociatedIrp.SystemBuffer = &FileInformation;
    FileInformation.DeleteFile = TRUE;

    irpSp = IoGetNextIrpStackLocation(irp);
    irpSp->MajorFunction = IRP_MJ_SET_INFORMATION;
    irpSp->DeviceObject = DeviceObject;
    irpSp->FileObject = FileObject;
    irpSp->Parameters.SetFile.FileInformationClass = FileDispositionInformation;
    irpSp->Parameters.SetFile.Length = sizeof(FILE_DISPOSITION_INFORMATION);
    irpSp->Parameters.SetFile.FileObject = FileObject;
    
    
    
   
    IoSetCompletionRoutine(irp, IoCompletionRoutine, NULL, TRUE, TRUE, TRUE);
    status = FsCallDriver(NULL, DeviceObject, irp);
   
    if (status == STATUS_PENDING)
        KeWaitForSingleObject(&event, Executive, KernelMode, TRUE, NULL);
   
    status = ioStatus.Status;
   
    VMProtectEnd;
   
    return status;  
}


LARGE_INTEGER
IrpGetFileSize(
    IN PFILE_OBJECT FileObject,
    IN PDEVICE_OBJECT DeviceObject
   )
{
    NTSTATUS status;
    KEVENT event;
    PIRP irp;
    IO_STATUS_BLOCK ioStatus;
    PIO_STACK_LOCATION irpSp;
    FILE_STANDARD_INFORMATION FileStdInformation;
   

    RtlZeroMemory(&FileStdInformation, sizeof(FileStdInformation));

    KeInitializeEvent(&event, SynchronizationEvent, FALSE);
    irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
   
    if (irp == NULL)
        return FileStdInformation.EndOfFile;
   
   
   
    irp->MdlAddress = NULL;
    irp->Flags |= IRP_BUFFERED_IO | IRP_SYNCHRONOUS_API;
    irp->RequestorMode = KernelMode;
    irp->UserIosb = &ioStatus;
    irp->UserEvent = &event;
    irp->PendingReturned = FALSE;
    irp->Cancel = FALSE;
    irp->CancelRoutine = NULL;
    irp->Tail.Overlay.Thread = PsGetCurrentThread();
    irp->Tail.Overlay.AuxiliaryBuffer = NULL;
    irp->Tail.Overlay.OriginalFileObject = FileObject;
    irp->AssociatedIrp.SystemBuffer = &FileStdInformation;

    irpSp = IoGetNextIrpStackLocation(irp);
    irpSp->MajorFunction = IRP_MJ_QUERY_INFORMATION;
    irpSp->DeviceObject = DeviceObject;
    irpSp->FileObject = FileObject;
    irpSp->Parameters.SetFile.FileInformationClass = FileStandardInformation;
    irpSp->Parameters.SetFile.Length = sizeof(FileStdInformation);
    irpSp->Parameters.SetFile.FileObject = FileObject;
    
    
    
   
    IoSetCompletionRoutine(irp, IoCompletionRoutine, NULL, TRUE, TRUE, TRUE);
    status = FsCallDriver(NULL, DeviceObject, irp);
   
    if (status == STATUS_PENDING)
        KeWaitForSingleObject(&event, Executive, KernelMode, TRUE, NULL);
   
    status = ioStatus.Status;

    return FileStdInformation.EndOfFile;
}


NTSTATUS
IoOpenFile(
    HANDLE *Handle,
    LPWSTR FileName,
    ACCESS_MASK DesiredAccess,
    ULONG ShareAccess,
    ULONG CreateOptions,
    ULONG CreateDisposition)
{
    VMProtectBegin;

    NTSTATUS Status;
    UNICODE_STRING uniFileName;
    PDEVICE_OBJECT deviceObject, realDevice;
    WCHAR FullFilePath[MAX_PATH] = L"";
    PFILE_OBJECT fileObject;
    

    if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
    {
        return STATUS_UNSUCCESSFUL;
    }

    if (!_wcsnicmp(FileName, L"\\Systemroot\\", wcslen(L"\\Systemroot\\")))
    {
        wcscpy(FullFilePath, SystemrootPath);
        wcscat(FullFilePath, FileName + wcslen(L"\\Systemroot"));
    }
    else
    {
        wcscpy(FullFilePath, FileName);
    }

    if (!GetDriveObject(FullFilePath[0], &deviceObject, &realDevice))
    {
        return STATUS_UNSUCCESSFUL;
    }
    
    RtlInitUnicodeString(&uniFileName, FullFilePath + 2);

    Status = IrpFileCreate(&uniFileName,
                           DesiredAccess,
                           FILE_ATTRIBUTE_NORMAL,
                           ShareAccess,
                           CreateDisposition,
                           CreateOptions,
                           deviceObject,
                           realDevice,
                           &fileObject);
    
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ObOpenObjectByPointer( fileObject,
                                    0,
                                    NULL,
                                    DesiredAccess,
                                    *IoFileObjectType,
                                    KernelMode,
                                    Handle);

    VMProtectEnd;
    return Status;
};


LONGLONG GetPortableFileSize(HANDLE Handle)
{
    NTSTATUS Status;
    PFILE_OBJECT FileObject;
    LARGE_INTEGER FileSize;

    FileSize.QuadPart = 0;
    Status = ObReferenceObjectByHandle(Handle, SYNCHRONIZE, *IoFileObjectType, KernelMode, (PVOID *)&FileObject, NULL);
    if (NT_SUCCESS(Status))
    {
        FileSize = IrpGetFileSize(FileObject, IoGetBaseFileSystemDeviceObject(FileObject));
        ObDereferenceObject(FileObject);
    }
    return FileSize.QuadPart;
};


PVOID
LoadFile(
    LPWSTR FileName,
    PVOID ImageBase,
    BOOLEAN FixIAT
)
{
    VMProtectBegin;

    PVOID                rc = 0;
    HANDLE                hFile = 0;
    NTSTATUS            NtStatus = STATUS_SUCCESS;
    LONGLONG            szFile = 0;
    PVOID                FileCopy;
    PIMAGE_NT_HEADERS    nt;
    ULONG                cb;
    ULONG                i;
    PIMAGE_SECTION_HEADER    sec;
    ULONG dwRlc = FALSE;
    LARGE_INTEGER FileOffset;

    FileOffset.QuadPart = 0;
    if (ImageBase == NULL)
        return NULL;
    NtStatus = IoOpenFile(&hFile, FileName, SYNCHRONIZE, FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, FILE_OPEN);
    if (NT_SUCCESS(NtStatus)) 
    {
        szFile = GetPortableFileSize(hFile);
        FileCopy = MmAlloc((ULONG)szFile);
        if (FileCopy)
        {
            NtStatus = IoReadFile(hFile, FileCopy, (ULONG)szFile, &FileOffset);
            if (NT_SUCCESS(NtStatus)) 
            {
                nt = (PIMAGE_NT_HEADERS)RtlImageNtHeader(FileCopy);
                cb = nt->OptionalHeader.SizeOfImage;
                rc = MmAlloc(cb);
                if (rc)
                {
                    memcpy(rc,FileCopy,nt->OptionalHeader.SizeOfHeaders);
                    sec = IMAGE_FIRST_SECTION(nt);
                    for (i = 0; i < nt->FileHeader.NumberOfSections; i++)
                        memcpy((PVOID)((ULONG)rc + sec[i].VirtualAddress),(PVOID)((ULONG)FileCopy + sec[i].PointerToRawData),sec[i].SizeOfRawData);
                    
                    //Print("relocating %ls -> %p -> %p", FileName, ImageBase, cb);
                    dwRlc = LdrRelocateImage(rc, ImageBase, TRUE, -1, FALSE);
                    if (dwRlc == FALSE) 
                    {
                        Print("failed to reloc %ls", FileName);
                        MmFree(rc);
                        MmFree(FileCopy);
                        ZwClose(hFile);
                        return 0;
                    }
                    if (FixIAT)
                    {
                        UNICODE_STRING Unicode;
                        RtlInitUnicodeString(&Unicode, FileName);
                        if (!NT_SUCCESS(ResolveImageReferences(rc, &Unicode))) 
                        {
                            Print("FAIL :: IAT");
                            MmFree(rc);
                            MmFree(FileCopy);
                            ZwClose(hFile);
                            return 0;
                        };
                    };
                    MmFree(FileCopy);
                };
            };
        };
        ZwClose(hFile);
    }
    else
    {
        Print("Error = %p, LoadFile(%ws)", NtStatus, FileName);
    }

    VMProtectEnd;
    return rc;
};


PVOID
LoadSystemFile(
    LPSTR FileName,
    PVOID ImageBase,
    BOOLEAN FixIAT
)
{
    VMProtectBegin;

    PVOID rc;
    WCHAR UnicodeString[MAX_PATH] = L"", Buffer[MAX_PATH];

    if (!NT_SUCCESS(RtlMultiByteToUnicodeN(Buffer, MAX_PATH, 0, ExtractFileName(FileName), MAX_PATH)))
        return 0;
    wcsncpy(UnicodeString, SystemPath, MAX_PATH);
    wcsncat(UnicodeString, Buffer, MAX_PATH);
    rc = LoadFile(UnicodeString, ImageBase, FixIAT);
    if (!rc)
    {
        memset(UnicodeString, 0, MAX_PATH * sizeof(WCHAR));
        wcsncpy(UnicodeString, SystemPath L"drivers\\", MAX_PATH);
        wcsncat(UnicodeString, Buffer, MAX_PATH);
        rc = LoadFile(UnicodeString, ImageBase, FixIAT);
    };

    VMProtectEnd;
    return rc;
};


PIMAGE_BASE_RELOCATION
LdrProcessRelocationBlock(
    IN ULONG_PTR VA,
    IN ULONG SizeOfBlock,
    IN PUSHORT NextOffset,
    IN LONG_PTR Diff
)
{
    PUCHAR FixupVA;
    USHORT Offset;
    LONG Temp;


    while (SizeOfBlock--)
    {

        Offset = *NextOffset & (USHORT)0xfff;
        FixupVA = (PUCHAR)(VA + Offset);

        //
        // Apply the fixups.
        //

        switch ((*NextOffset) >> 12)
        {

        case IMAGE_REL_BASED_HIGHLOW :
            //
            // HighLow - (32-bits) relocate the high and low half
            //      of an address.
            //
            *(LONG UNALIGNED *)FixupVA += (ULONG) Diff;
            break;

        case IMAGE_REL_BASED_HIGH :
            //
            // High - (16-bits) relocate the high half of an address.
            //
            Temp = *(PUSHORT)FixupVA << 16;
            Temp += (ULONG) Diff;
            *(PUSHORT)FixupVA = (USHORT)(Temp >> 16);
            break;

        case IMAGE_REL_BASED_LOW :
            //
            // Low - (16-bit) relocate the low half of an address.
            //
            Temp = *(PSHORT)FixupVA;
            Temp += (ULONG) Diff;
            *(PUSHORT)FixupVA = (USHORT)Temp;
            break;

        case IMAGE_REL_BASED_DIR64:

            *(ULONG_PTR UNALIGNED *)FixupVA += Diff;

            break;

        case IMAGE_REL_BASED_ABSOLUTE :
            //
            // Absolute - no fixup required.
            //
            break;

        default :
            //
            // Illegal - illegal relocation type.
            //

            return (PIMAGE_BASE_RELOCATION)NULL;
        }
        ++NextOffset;
    }
    return (PIMAGE_BASE_RELOCATION)NextOffset;
};


ULONG
LdrRelocateImage(
    IN PVOID BaseAddress,
    IN PVOID NewBase,
    IN ULONG Success,
    IN ULONG Conflict,
    IN ULONG Invalid
)
{
    LONG_PTR Diff;
    ULONG TotalCountBytes;
    ULONG_PTR VA;
    ULONG_PTR OldBase;
    ULONG SizeOfBlock;
    PUSHORT NextOffset;
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_BASE_RELOCATION NextBlock;



    NtHeaders = (PIMAGE_NT_HEADERS)RtlImageNtHeader(BaseAddress);
    if (NtHeaders)
    {
        OldBase = NtHeaders->OptionalHeader.ImageBase;
    }
    else
    {
        return Invalid;
    };

    //
    // Locate the relocation section.
    //

    NextBlock = (PIMAGE_BASE_RELOCATION)RtlImageDirectoryEntryToData(
                    BaseAddress, TRUE, IMAGE_DIRECTORY_ENTRY_BASERELOC, &TotalCountBytes);

    if (!NextBlock || !TotalCountBytes)
    {

        //
        // The image does not contain a relocation table, and therefore
        // cannot be relocated.
        //
        return Conflict;
    };

    //
    // If the image has a relocation table, then apply the specified fixup
    // information to the image.
    //

    while (TotalCountBytes)
    {
        SizeOfBlock = NextBlock->SizeOfBlock;
        TotalCountBytes -= SizeOfBlock;
        SizeOfBlock -= sizeof(IMAGE_BASE_RELOCATION);
        SizeOfBlock /= sizeof(USHORT);
        NextOffset = (PUSHORT)((PCHAR)NextBlock + sizeof(IMAGE_BASE_RELOCATION));

        VA = (ULONG_PTR)BaseAddress + NextBlock->VirtualAddress;
        Diff = (PCHAR)NewBase - (PCHAR)OldBase;

        if (!(NextBlock = LdrProcessRelocationBlock(VA,SizeOfBlock,NextOffset,Diff)))
        {
            return Invalid;
        };
    };

    return Success;
};

RtlImageDirectoryEntryToData

typedef struct _LOAD_IMPORTS {
    SIZE_T Count;
    PKLDR_DATA_TABLE_ENTRY Entry[1];
} LOAD_IMPORTS, *PLOAD_IMPORTS;

#define NO_IMPORTS_USED ((PLOAD_IMPORTS)-2)


NTSTATUS
  RtlAppendStringToString(
    IN PSTRING  Destination,
    IN PSTRING  Source
   );


extern "C" BOOLEAN
RtlPrefixString(
    const    STRING* s1,
    const    STRING* s2,
    BOOLEAN    ignore_case
    );

#define POINTER_TO_SINGLE_ENTRY(Pointer)    ((PKLDR_DATA_TABLE_ENTRY)((ULONG_PTR)(Pointer) | 0x1))


NTSTATUS
MiSnapThunk(
    IN PVOID DllBase,
    IN PVOID ImageBase,
    IN PIMAGE_THUNK_DATA NameThunk,
    OUT PIMAGE_THUNK_DATA AddrThunk,
    IN PIMAGE_EXPORT_DIRECTORY ExportDirectory,
    IN ULONG ExportSize,
    IN BOOLEAN SnapForwarder
   )

/*++

Routine Description:

    This function snaps a thunk using the specified Export Section data.
    If the section data does not support the thunk, then the thunk is
    partially snapped (Dll field is still non-null, but snap address is
    set).

Arguments:

    DllBase - Base of DLL being snapped to.

    ImageBase - Base of image that contains the thunks to snap.

    Thunk - On input, supplies the thunk to snap.  When successfully
        snapped, the function field is set to point to the address in
        the DLL, and the DLL field is set to NULL.

    ExportDirectory - Supplies the Export Section data from a DLL.

    SnapForwarder - determine if the snap is for a forwarder, and therefore
       Address of Data is already setup.

Return Value:


    STATUS_SUCCESS or STATUS_DRIVER_ENTRYPOINT_NOT_FOUND or
        STATUS_DRIVER_ORDINAL_NOT_FOUND

--*/

{
    BOOLEAN Ordinal;
    USHORT OrdinalNumber;
    PULONG NameTableBase;
    PUSHORT NameOrdinalTableBase;
    PULONG Addr;
    USHORT HintIndex;
    ULONG High;
    ULONG Low;
    ULONG Middle = 0;
    LONG Result;
    NTSTATUS Status;

    PAGED_CODE();

    //
    // Determine if snap is by name, or by ordinal
    //

    Ordinal = (BOOLEAN)IMAGE_SNAP_BY_ORDINAL(NameThunk->u1.Ordinal);

    if (Ordinal && !SnapForwarder) {

        OrdinalNumber = (USHORT)(IMAGE_ORDINAL(NameThunk->u1.Ordinal) -
                         ExportDirectory->Base);

    } else {

        //
        // Change AddressOfData from an RVA to a VA.
        //

        if (!SnapForwarder) {
            NameThunk->u1.AddressOfData = (ULONG_PTR)ImageBase + NameThunk->u1.AddressOfData;
        }

        //
        // Lookup Name in NameTable
        //

        NameTableBase = (PULONG)((PCHAR)DllBase + (ULONG)ExportDirectory->AddressOfNames);
        NameOrdinalTableBase = (PUSHORT)((PCHAR)DllBase + (ULONG)ExportDirectory->AddressOfNameOrdinals);

        //
        // Before dropping into binary search, see if
        // the hint index results in a successful
        // match. If the hint index is zero, then
        // drop into binary search.
        //

        HintIndex = ((PIMAGE_IMPORT_BY_NAME)NameThunk->u1.AddressOfData)->Hint;
        if ((ULONG)HintIndex < ExportDirectory->NumberOfNames &&
            !strcmp((PSZ)((PIMAGE_IMPORT_BY_NAME)NameThunk->u1.AddressOfData)->Name,
             (PSZ)((PCHAR)DllBase + NameTableBase[HintIndex]))) {
            OrdinalNumber = NameOrdinalTableBase[HintIndex];

        } else {

            //
            // Lookup the import name in the name table using a binary search.
            //

            Low = 0;
            High = ExportDirectory->NumberOfNames - 1;

            while (High >= Low) {

                //
                // Compute the next probe index and compare the import name
                // with the export name entry.
                //

                Middle = (Low + High) >> 1;
                Result = strcmp((const char *)&((PIMAGE_IMPORT_BY_NAME)NameThunk->u1.AddressOfData)->Name[0],
                                (PCHAR)((PCHAR)DllBase + NameTableBase[Middle]));

                if (Result < 0) {
                    High = Middle - 1;

                } else if (Result > 0) {
                    Low = Middle + 1;

                } else {
                    break;
                }
            }

            //
            // If the high index is less than the low index, then a matching
            // table entry was not found. Otherwise, get the ordinal number
            // from the ordinal table.
            //

            if (High < Low) {
                return STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;
            } else {
                OrdinalNumber = NameOrdinalTableBase[Middle];
            }
        }
    }

    //
    // If OrdinalNumber is not within the Export Address Table,
    // then DLL does not implement function. Snap to LDRP_BAD_DLL.
    //

    if ((ULONG)OrdinalNumber >= ExportDirectory->NumberOfFunctions) {
        Status = STATUS_DRIVER_ORDINAL_NOT_FOUND;

    } else {

        Addr = (PULONG)((PCHAR)DllBase + (ULONG)ExportDirectory->AddressOfFunctions);
        *(PULONG *)&(AddrThunk->u1.Function) = (PULONG)((PCHAR)DllBase + Addr[OrdinalNumber]);

        // AddrThunk s/b used from here on.

        Status = STATUS_SUCCESS;

        if (((ULONG_PTR)AddrThunk->u1.Function > (ULONG_PTR)ExportDirectory) &&
             ((ULONG_PTR)AddrThunk->u1.Function < ((ULONG_PTR)ExportDirectory + ExportSize))) {

            UNICODE_STRING UnicodeString;
            ANSI_STRING ForwardDllName;

            PLIST_ENTRY NextEntry;
            PKLDR_DATA_TABLE_ENTRY DataTableEntry;
            ULONG ExportSize;
            PIMAGE_EXPORT_DIRECTORY ExportDirectory;

            Status = STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;

            //
            // Include the dot in the length so we can do prefix later on.
            //

            ForwardDllName.Buffer = (PCHAR)AddrThunk->u1.Function;
            ForwardDllName.Length = (USHORT)(strchr(ForwardDllName.Buffer, '.') -
                                           ForwardDllName.Buffer + 1);
            ForwardDllName.MaximumLength = ForwardDllName.Length;

            if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&UnicodeString,
                                                        &ForwardDllName,
                                                        TRUE))) {

                                                            NextEntry = PsLoadedModuleList->Flink;

                                                            while (NextEntry != PsLoadedModuleList) {

                    DataTableEntry = CONTAINING_RECORD(NextEntry,
                                                       KLDR_DATA_TABLE_ENTRY,
                                                       InLoadOrderLinks);

                    //
                    // We have to do a case INSENSITIVE comparison for
                    // forwarder because the linker just took what is in the
                    // def file, as opposed to looking in the exporting
                    // image for the name.
                    // we also use the prefix function to ignore the .exe or
                    // .sys or .dll at the end.
                    //

                    if (RtlPrefixString((PSTRING)&UnicodeString,
                                        (PSTRING)&DataTableEntry->BaseDllName,
                                        TRUE)) {

                        ExportDirectory = (PIMAGE_EXPORT_DIRECTORY)
                            RtlImageDirectoryEntryToData((PVOID)DataTableEntry->DllBase,
                                                         TRUE,
                                                         IMAGE_DIRECTORY_ENTRY_EXPORT,
                                                         &ExportSize);

                        if (ExportDirectory) {

                            IMAGE_THUNK_DATA thunkData;
                            PIMAGE_IMPORT_BY_NAME addressOfData;
                            ULONG length;

                            // one extra byte for NULL,

                            length = strlen(ForwardDllName.Buffer +
                                                ForwardDllName.Length) + 1;

                            addressOfData = (PIMAGE_IMPORT_BY_NAME)MmAlloc(length + sizeof(IMAGE_IMPORT_BY_NAME));

                            if (addressOfData) {

                                RtlCopyMemory(&(addressOfData->Name[0]),
                                              ForwardDllName.Buffer +
                                                  ForwardDllName.Length,
                                              length);

                                addressOfData->Hint = 0;

                                *(PIMAGE_IMPORT_BY_NAME *)&(thunkData.u1.AddressOfData) = addressOfData;

                                Status = MiSnapThunk((PVOID)DataTableEntry->DllBase,
                                                     ImageBase,
                                                     &thunkData,
                                                     &thunkData,
                                                     ExportDirectory,
                                                     ExportSize,
                                                     TRUE
                                                   );

                                MmFree(addressOfData);

                                AddrThunk->u1 = thunkData.u1;
                            }
                        }

                        break;
                    }

                    NextEntry = NextEntry->Flink;
                }

                RtlFreeUnicodeString(&UnicodeString);
            }

        }

    }
    return Status;
}


NTSTATUS
ResolveImageReferences (
    PVOID ImageBase,
    IN PUNICODE_STRING ImageFileDirectory
   )

/*++

Routine Description:

    This routine resolves the references from the newly loaded driver
    to the kernel, HAL and other drivers.

Arguments:

    ImageBase - Supplies the address of which the image header resides.

    ImageFileDirectory - Supplies the directory to load referenced DLLs.

Return Value:

    Status of the image reference resolution.

--*/

{
    PVOID ImportBase;
    ULONG ImportSize;
    ULONG ImportListSize;
    ULONG Count;
    ULONG i;
    PIMAGE_IMPORT_DESCRIPTOR ImportDescriptor;
    PIMAGE_IMPORT_DESCRIPTOR Imp;
    NTSTATUS st;
    ULONG ExportSize;
    PIMAGE_EXPORT_DIRECTORY ExportDirectory;
    PIMAGE_THUNK_DATA NameThunk;
    PIMAGE_THUNK_DATA AddrThunk;
    PSZ ImportName;
    PLIST_ENTRY NextEntry;
    PKLDR_DATA_TABLE_ENTRY DataTableEntry;
    PKLDR_DATA_TABLE_ENTRY SingleEntry;
    ANSI_STRING AnsiString;
    UNICODE_STRING ImportName_U;
    UNICODE_STRING ImportDescriptorName_U;
    BOOLEAN PrefixedNameAllocated;
    PLOAD_IMPORTS ImportList;
    PLOAD_IMPORTS CompactedImportList;
    BOOLEAN Loaded;

    PAGED_CODE();

    ImportDescriptor = (PIMAGE_IMPORT_DESCRIPTOR)RtlImageDirectoryEntryToData(
                        ImageBase,
                        TRUE,
                        IMAGE_DIRECTORY_ENTRY_IMPORT,
                        &ImportSize);

    if (ImportDescriptor == NULL) {
        return STATUS_SUCCESS;
    }

    // Count the number of imports so we can allocate enough room to
    // store them all chained off this module's KLDR_DATA_TABLE_ENTRY.
    //

    Count = 0;
    for (Imp = ImportDescriptor; Imp->Name && Imp->OriginalFirstThunk; Imp += 1) {
        Count += 1;
    }

    if (Count) {
        ImportListSize = Count * sizeof(PVOID) + sizeof(SIZE_T);

        ImportList = (PLOAD_IMPORTS)MmAlloc(ImportListSize);

        //
        // Zero it so we can recover gracefully if we fail in the middle.
        // If the allocation failed, just don't build the import list.
        //
    
        if (ImportList) {
            RtlZeroMemory (ImportList, ImportListSize);
            ImportList->Count = Count;
        }
    }
    else {
        ImportList = (PLOAD_IMPORTS) 0;
    }

    Count = 0;
    while (ImportDescriptor->Name && ImportDescriptor->OriginalFirstThunk) {

        ImportName = (PSZ)((PCHAR)ImageBase + ImportDescriptor->Name);

        //
        // We don't want to count coverage, win32k and irt (lego) since
        // display drivers CAN link against these.
        //


        RtlInitAnsiString(&AnsiString, ImportName);
        st = RtlAnsiStringToUnicodeString(&ImportName_U, &AnsiString, TRUE);
        if (!NT_SUCCESS(st)) {
            //MiDereferenceImports (ImportList);
            if (ImportList) {
                MmFree(ImportList);
            }
            return st;
        }

        ImportDescriptorName_U = ImportName_U;
        PrefixedNameAllocated = FALSE;

        Loaded = FALSE;


        NextEntry = PsLoadedModuleList->Flink;
        ImportBase = NULL;

        while (NextEntry != PsLoadedModuleList) {

            DataTableEntry = CONTAINING_RECORD(NextEntry,
                                               KLDR_DATA_TABLE_ENTRY,
                                               InLoadOrderLinks);

            if (RtlEqualUnicodeString (&ImportDescriptorName_U,
                                       &DataTableEntry->BaseDllName,
                                       TRUE
                                      )) {

                ImportBase = (PVOID)DataTableEntry->DllBase;

                break;
            }
            NextEntry = NextEntry->Flink;
        }

        RtlFreeUnicodeString(&ImportName_U);

        ExportDirectory = (PIMAGE_EXPORT_DIRECTORY)RtlImageDirectoryEntryToData(
                                    ImportBase,
                                    TRUE,
                                    IMAGE_DIRECTORY_ENTRY_EXPORT,
                                    &ExportSize
                                   );

        if (!ExportDirectory) {
            //MiDereferenceImports (ImportList);
            if (ImportList) {
                MmFree (ImportList);
            }
            return STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;
        }

        //
        // Walk through the IAT and snap all the thunks.
        //

        if (ImportDescriptor->OriginalFirstThunk) {

            NameThunk = (PIMAGE_THUNK_DATA)((PCHAR)ImageBase + (ULONG)ImportDescriptor->OriginalFirstThunk);
            AddrThunk = (PIMAGE_THUNK_DATA)((PCHAR)ImageBase + (ULONG)ImportDescriptor->FirstThunk);

            while (NameThunk->u1.AddressOfData) {
                st = MiSnapThunk(ImportBase,
                       ImageBase,
                       NameThunk++,
                       AddrThunk++,
                       ExportDirectory,
                       ExportSize,
                       FALSE
                      );
                if (!NT_SUCCESS(st)) {
                    //MiDereferenceImports (ImportList);
                    if (ImportList) {
                        MmFree (ImportList);
                    }
                    return st;
                }
            }
        }

        ImportDescriptor += 1;
    }

    //
    // All the imports are successfully loaded so establish and compact
    // the import unload list.
    //

    if (ImportList) {

        //
        // Blank entries occur for things like the kernel, HAL & win32k.sys
        // that we never want to unload.  Especially for things like
        // win32k.sys where the reference count can really hit 0.
        //

        Count = 0;
        for (i = 0; i < ImportList->Count; i += 1) {
            if (ImportList->Entry[i]) {
                Count += 1;
            }
        }

        if (Count == 0) {

            MmFree(ImportList);
            ImportList = NO_IMPORTS_USED;
        }
        else if (Count == 1) {
            for (i = 0; i < ImportList->Count; i += 1) {
                if (ImportList->Entry[i]) {
                    SingleEntry = POINTER_TO_SINGLE_ENTRY(ImportList->Entry[i]);
                    break;
                }
            }

            MmFree(ImportList);
            ImportList = (PLOAD_IMPORTS)SingleEntry;
        }
        else if (Count != ImportList->Count) {

            ImportListSize = Count * sizeof(PVOID) + sizeof(SIZE_T);

            CompactedImportList = (PLOAD_IMPORTS)MmAlloc(ImportListSize);
            if (CompactedImportList) {
                CompactedImportList->Count = Count;

                Count = 0;
                for (i = 0; i < ImportList->Count; i += 1) {
                    if (ImportList->Entry[i]) {
                        CompactedImportList->Entry[Count] = ImportList->Entry[i];
                        Count += 1;
                    }
                }

                MmFree(ImportList);
                MmFree(CompactedImportList);
            }
        }
    }
    return STATUS_SUCCESS;
};


BOOLEAN GetFileName(PFILE_OBJECT FileObject, PWCHAR FileName, ULONG FileNameLength)
{
    NTSTATUS Status;
    POBJECT_NAME_INFORMATION uFilePath;

    if (KdBuildNumber >= 6000)
        FileObject = (PFILE_OBJECT)((ULONG_PTR)FileObject & 0xfffffff8);

    __try
    {
        Status = IoQueryFileDosDeviceName(FileObject, &uFilePath);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = GetExceptionCode();
        Print("Exc : Status = %p", Status);
    }

    
    
    if (NT_SUCCESS(Status))
    {
        CopyUnicodeStringFile(FileName, &uFilePath->Name, FileNameLength);
        return TRUE;
    }
    else
    {
        _snwprintf(FileName, FileNameLength, L"invalid_file_name");
        Print("Status = %p", Status);
    }
    return FALSE;
};


PDRIVER_DISPATCH GetFileSystemMajorFunction(PFSD_OBJECT Fsd, ULONG MajorFunctionIndex)
{
    PIMAGE_NT_HEADERS NtHeader;
    PIMAGE_SECTION_HEADER Section;
    PDRIVER_DISPATCH MajorFunction = NULL;
    PUCHAR InitSectionStart;
    ULONG InitSectionSize;
    ULONG MajorFunctionOffset;
    ULONG Index;
    PUCHAR ptrCode;
    
    VMProtectBegin;

    if (!Fsd->DriverObject || !Fsd->DriverObject->DriverStart)
    {
        return NULL;
    }
    MajorFunctionOffset = (MajorFunctionIndex * sizeof(PDRIVER_DISPATCH)) + FIELD_OFFSET(DRIVER_OBJECT, MajorFunction);
    if (Fsd->MappedImage)
    {
        NtHeader = (PIMAGE_NT_HEADERS)RtlImageNtHeader(Fsd->MappedImage);
        Section = RtlImageRvaToSection(Fsd->MappedImage, NtHeader->OptionalHeader.AddressOfEntryPoint);
        InitSectionStart = (PUCHAR)Fsd->MappedImage + Section->VirtualAddress;
        InitSectionSize = Section->Misc.VirtualSize;

        for (Index = 0; Index < InitSectionSize - 16;)
        {
            ULONG szCode;
            ptrCode = InitSectionStart + Index;
            if (!MmIsAddressValid(ptrCode)) break;
            szCode = SizeOfCode(ptrCode, NULL);
            Index += szCode;
            if (szCode == 7)
            {
                if (ptrCode[0] == 0xc7 && ptrCode[2] == MajorFunctionOffset) // mov dword ptr [r32 + x], offset
                {
                    if (ptrCode[7] == 0xc7 && (ptrCode[8] & 0x0f) == (ptrCode[1] & 0x0f)) // check next instruction
                    {
                        if (ptrCode[1] >= 0x40 && ptrCode[1] <= 0x47) // eax (0x40) -> esi (0x47)
                        {
                            MajorFunction = *(PDRIVER_DISPATCH *)(&ptrCode[3]);
                            if (!MmIsAddressValid(MajorFunction)) MajorFunction = NULL;
                            MajorFunction = (PDRIVER_DISPATCH)((ULONG_PTR)MajorFunction - (ULONG_PTR)Fsd->DriverObject->DriverStart + (ULONG_PTR)Fsd->MappedImage);
                            if (!MmIsAddressValid(MajorFunction)) MajorFunction = NULL;
                            break;
                        }
                    }
                }
            }
        }

        if (!MajorFunction) // Special routine for [IRP_MJ_QUERY_INFORMATION]
        {
            for (Index = 0; Index < InitSectionSize - 16;)
            {
                ULONG szCode;
                ptrCode = InitSectionStart + Index;
                if (!MmIsAddressValid(ptrCode)) break;
                szCode = SizeOfCode(ptrCode, NULL);
                Index += szCode;
                if (szCode == 5)
                {
                    if (ptrCode[0] == 0xb8) // mov eax, offset
                    {
                        if (ptrCode[5] == 0x89 && ptrCode[7] == MajorFunctionOffset)
                        {
                            if (ptrCode[6] >= 0x40 && ptrCode[6] <= 0x47) // eax (0x40) -> esi (0x47)
                            {
                                MajorFunction = *(PDRIVER_DISPATCH *)(&ptrCode[1]);
                                if (!MmIsAddressValid(MajorFunction)) MajorFunction = NULL;
                                MajorFunction = (PDRIVER_DISPATCH)((ULONG_PTR)MajorFunction - (ULONG_PTR)Fsd->DriverObject->DriverStart + (ULONG_PTR)Fsd->MappedImage);
                                if (!MmIsAddressValid(MajorFunction)) MajorFunction = NULL;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    Print("MajorFunction[%u] = %p", MajorFunctionIndex, MajorFunction);

    VMProtectEnd;
    return MajorFunction;
}


NTSTATUS IrpSetFileAttributes(PFILE_OBJECT fileObject, PDEVICE_OBJECT deviceObject, ULONG Attributes)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PIRP irp;
    KEVENT SycEvent;
    FILE_BASIC_INFORMATION FileInformation;
    IO_STATUS_BLOCK ioStatus;
    PIO_STACK_LOCATION irpSp;

    
    irp = IoAllocateIrp (deviceObject->StackSize, TRUE);
    if (irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    ObReferenceObject(fileObject);

    KeInitializeEvent (&SycEvent, SynchronizationEvent, FALSE);

    RtlZeroMemory(&FileInformation, sizeof(FileInformation));
    FileInformation.FileAttributes = Attributes;

    irp->AssociatedIrp.SystemBuffer = &FileInformation;
    irp->UserEvent = &SycEvent;
    irp->UserIosb = &ioStatus;
    irp->Tail.Overlay.OriginalFileObject = fileObject;
    irp->Tail.Overlay.Thread = (PETHREAD)KeGetCurrentThread();
    irp->RequestorMode = KernelMode;

    irpSp = IoGetNextIrpStackLocation(irp);
    irpSp->MajorFunction = IRP_MJ_SET_INFORMATION;
    irpSp->DeviceObject = deviceObject;
    irpSp->FileObject = fileObject;
    irpSp->Parameters.SetFile.Length = sizeof(FILE_BASIC_INFORMATION);
    irpSp->Parameters.SetFile.FileInformationClass = FileBasicInformation;
    irpSp->Parameters.SetFile.FileObject = fileObject ;

    IoSetCompletionRoutine (irp, IoCompletionRoutine, NULL, TRUE, TRUE, TRUE);

    Status = FsCallDriver(NULL, deviceObject, irp);

    if (Status == STATUS_PENDING)
        KeWaitForSingleObject(&SycEvent, Executive, KernelMode, TRUE, NULL);

    ObDereferenceObject(fileObject);

    Status = ioStatus.Status;

    return Status;
}


NTSTATUS DeleteFile(PWCHAR FileName, LONG ForceDelete)
{
    NTSTATUS Status;
    UNICODE_STRING uniFileName;
    PDEVICE_OBJECT deviceObject, realDevice;
    PFILE_OBJECT fileObject;
    WCHAR FullFilePath[MAX_PATH] = L"";
    PVOID DataSectionObject, ImageSectionObject;
    

    if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
    {
        return STATUS_UNSUCCESSFUL;
    }

    if (FileName[1] != ':')
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!_wcsnicmp(FileName, L"\\Systemroot\\", wcslen(L"\\Systemroot\\")))
    {
        wcscpy(FullFilePath, SystemrootPath);
        wcscat(FullFilePath, FileName + wcslen(L"\\Systemroot"));
    }
    else
    {
        wcscpy(FullFilePath, FileName);
    }

    if (!GetDriveObject(FullFilePath[0], &deviceObject, &realDevice))
    {
        return STATUS_UNSUCCESSFUL;
    }
    
    RtlInitUnicodeString(&uniFileName, FullFilePath + 2);

    Status = IrpFileCreate(&uniFileName,
                           SYNCHRONIZE,
                           FILE_ATTRIBUTE_NORMAL,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           FILE_OPEN,
                           FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                           deviceObject,
                           realDevice,
                           &fileObject);

    VMProtectBegin;
    if (NT_SUCCESS(Status))
    {
        if (ForceDelete)
        {
            Print("Force Delete ...");
            DataSectionObject = fileObject->SectionObjectPointer->DataSectionObject;
            ImageSectionObject = fileObject->SectionObjectPointer->ImageSectionObject;
            fileObject->SectionObjectPointer->DataSectionObject = NULL;
            fileObject->SectionObjectPointer->ImageSectionObject = NULL;
        }

        IrpSetFileAttributes(fileObject, IoGetBaseFileSystemDeviceObject(fileObject), FILE_ATTRIBUTE_NORMAL);
        Status = IrpFileDelete(fileObject, IoGetBaseFileSystemDeviceObject(fileObject));
        
        if (ForceDelete)
        {
            fileObject->SectionObjectPointer->DataSectionObject = DataSectionObject;
            fileObject->SectionObjectPointer->ImageSectionObject = ImageSectionObject;
        }

        ObDereferenceObject(fileObject);
    }
    VMProtectEnd;
    return Status;
}


NTSTATUS CopyFile(PWCHAR lpSource, PWCHAR lpDest)
{
    NTSTATUS Status;
    HANDLE DestHandle, SrcHandle;
    PVOID Buffer;
    ULONG FileSize;
    WCHAR FullSourcePath[MAX_PATH] = L"";


    Status = IoOpenFile(&SrcHandle, lpSource, SYNCHRONIZE, FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, FILE_OPEN);

    if (NT_SUCCESS(Status))
    {
        FileSize = (ULONG)GetPortableFileSize(SrcHandle);
        if (FileSize != 0 && FileSize != -1)
        {
            Status = IoOpenFile(&DestHandle, lpDest, SYNCHRONIZE, FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, FILE_OVERWRITE_IF);
            if (NT_SUCCESS(Status))
            {
                Buffer = MmAlloc(FileSize);
                if (Buffer)
                {
                    LARGE_INTEGER FileOffset;
                    
                    FileOffset.QuadPart = 0;
                    IoReadFile(SrcHandle, Buffer, FileSize, &FileOffset);

                    FileOffset.QuadPart = 0;
                    IoWriteFile(DestHandle, Buffer, FileSize, &FileOffset);

                    MmFree(Buffer); 
                }
                ZwClose(DestHandle);
            }
        }
        ZwClose(SrcHandle);
    }

    return Status;
}


NTSTATUS
ReadWriteDisk(
    PDRIVER_OBJECT DriverObject,
    PDEVICE_OBJECT DeviceObject,
    ULONG SectorNumber,
    USHORT SectorCount,
    BOOL IsWrite,
    PVOID Buffer
    )
{
    SENSE_DATA SenseData;
    SCSI_REQUEST_BLOCK Srb;
    PCDB Cdb;
    PIRP Irp;
    PIO_STACK_LOCATION IrpSp;
    PMDL Mdl;
    KEVENT Event;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    IO_STATUS_BLOCK IoStatusBlock;


    
    VMProtectBegin;

    RtlZeroMemory(&SenseData, sizeof(SENSE_DATA));
    RtlZeroMemory(&Srb, sizeof(SCSI_REQUEST_BLOCK));

    Srb.Length = sizeof(SCSI_REQUEST_BLOCK);
    Srb.Function = SRB_FUNCTION_EXECUTE_SCSI;
    Srb.DataBuffer = Buffer;
    Srb.SenseInfoBuffer = &SenseData;
    Srb.SenseInfoBufferLength = sizeof(SENSE_DATA);
    Srb.DataTransferLength = (ULONG)Int64ShllMod32(SectorCount, 9);
    Srb.QueueAction = SRB_FLAGS_DISABLE_AUTOSENSE;
    if (IsWrite)
        Srb.SrbFlags = SRB_FLAGS_DATA_OUT;
    else
        Srb.SrbFlags = SRB_FLAGS_DATA_IN | SRB_FLAGS_ADAPTER_CACHE_ENABLE;
    Srb.SrbFlags |= SRB_FLAGS_DISABLE_AUTOSENSE;
    Srb.TimeOutValue = ((Srb.DataTransferLength + 0xFFFF) >> 16) * 10; // SCSI_DISK_TIMEOUT
    Srb.QueueSortKey = SectorNumber;
    Srb.CdbLength = sizeof(Cdb->CDB10);
    
    
    Cdb = (PCDB)&Srb.Cdb;
    Cdb->CDB10.LogicalBlockByte0 = ((PFOUR_BYTE)&SectorNumber)->Byte3;
    Cdb->CDB10.LogicalBlockByte1 = ((PFOUR_BYTE)&SectorNumber)->Byte2;
    Cdb->CDB10.LogicalBlockByte2 = ((PFOUR_BYTE)&SectorNumber)->Byte1;
    Cdb->CDB10.LogicalBlockByte3 = ((PFOUR_BYTE)&SectorNumber)->Byte0;
    Cdb->CDB10.TransferBlocksMsb = ((PTWO_BYTE)&SectorCount)->Byte1;
    Cdb->CDB10.TransferBlocksLsb = ((PTWO_BYTE)&SectorCount)->Byte0;
    Cdb->CDB10.OperationCode = IsWrite ? SCSIOP_WRITE : SCSIOP_READ;


    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (!Irp)
    {
       return STATUS_INSUFFICIENT_RESOURCES;
    }
    Mdl = IoAllocateMdl(Buffer, (ULONG)Int64ShllMod32(SectorCount, 9), FALSE, FALSE, Irp);
    if (!Mdl)
    {
       IoFreeIrp(Irp);
       return STATUS_INSUFFICIENT_RESOURCES;
    }
    MmProbeAndLockPages(Mdl, KernelMode, IoWriteAccess);

    Srb.OriginalRequest = Irp;

    Irp->UserIosb = &IoStatusBlock;
    Irp->UserEvent = &Event;
    RtlZeroMemory(&Irp->IoStatus, sizeof(IO_STATUS_BLOCK));
    Irp->MdlAddress = Mdl;
    Irp->AssociatedIrp.SystemBuffer = NULL;
    Irp->Cancel = FALSE;
    Irp->CancelRoutine = NULL;
    Irp->Flags = IRP_NOCACHE | IRP_SYNCHRONOUS_API;
    Irp->RequestorMode = KernelMode;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();

    IrpSp = IoGetNextIrpStackLocation(Irp);
    IrpSp->DeviceObject = DeviceObject;
    IrpSp->MajorFunction = IRP_MJ_SCSI;
    IrpSp->Parameters.Scsi.Srb = &Srb;

    IoSetCompletionRoutine(Irp, IoCompletionRoutine, &Srb, TRUE, TRUE, TRUE);

    Status = FsCallDriver(DriverObject, DeviceObject, Irp);

    if (Status == STATUS_PENDING)
    {
       KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
       Status = STATUS_SUCCESS;
    }

    if ((Srb.SenseInfoBuffer != &SenseData) && (Srb.SenseInfoBuffer))
    {
       ExFreePool(Srb.SenseInfoBuffer);
    }

    if (NT_SUCCESS(Status) && SRB_STATUS(Srb.SrbStatus) == SRB_STATUS_SUCCESS && Srb.ScsiStatus == SCSISTAT_GOOD)
    {
       Status = STATUS_SUCCESS;
    }
    else
    {
       Status = STATUS_UNSUCCESSFUL;
    }

    VMProtectEnd;

    return Status;
}
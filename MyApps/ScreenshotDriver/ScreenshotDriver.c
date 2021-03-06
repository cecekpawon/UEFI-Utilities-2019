//
//  Copyright (c) 2010-2019  Finnbarr P. Murphy.   All rights reserved.
//
//  Screen snapshot driver 
//
//  License: BSD 2 clause License
//
//  Portions Copyright (c) 2016-2017, Microsoft Corporation
//           Copyright (c) 2018, Intel Corporation. All rights reserved.
//           See relevant code in EDK11 for exact details
//

#include <Uefi.h>

#include <Library/DebugLib.h>
#include <Library/UefiLib.h>
#include <Library/ShellLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/PrintLib.h>
#include <Library/DevicePathLib.h>

#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleTextInEx.h>
#include <Protocol/Shell.h>
#include <Protocol/ComponentName.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/DriverDiagnostics2.h>
#include <Protocol/DriverConfiguration2.h>

#include "ScreenshotDriver.h"

#ifdef SAVE_AS_PNG
#include "../ScreenShot/lodepng.h"
#else
#include <IndustryStandard/Bmp.h>
#endif

STATIC EFI_LOADED_IMAGE                 *gModuleSelfLoadedImage     = NULL;
STATIC EFI_FILE_HANDLE                  gModuleSelfRootDir          = NULL;
STATIC EFI_DEVICE_PATH_PROTOCOL         *gDevicePath                = NULL;

EFI_GRAPHICS_OUTPUT_BLT_PIXEL EfiGraphicsColors[16] = {
    // B    G    R   reserved
    {0x00, 0x00, 0x00, 0x00},  // BLACK
    {0x98, 0x00, 0x00, 0x00},  // LIGHTBLUE
    {0x00, 0x98, 0x00, 0x00},  // LIGHTGREEN
    {0x98, 0x98, 0x00, 0x00},  // LIGHTCYAN
    {0x00, 0x00, 0x98, 0x00},  // LIGHTRED
    {0x98, 0x00, 0x98, 0x00},  // MAGENTA
    {0x00, 0x98, 0x98, 0x00},  // BROWN
    {0x98, 0x98, 0x98, 0x00},  // LIGHTGRAY
    {0x30, 0x30, 0x30, 0x00},  // DARKGRAY
    {0xff, 0x00, 0x00, 0x00},  // BLUE
    {0x00, 0xff, 0x00, 0x00},  // LIME
    {0xff, 0xff, 0x00, 0x00},  // CYAN
    {0x00, 0x00, 0xff, 0x00},  // RED
    {0xff, 0x00, 0xff, 0x00},  // FUCHSIA
    {0x00, 0xff, 0xff, 0x00},  // YELLOW
    {0xff, 0xff, 0xff, 0x00}   // WHITE
};

typedef enum {
    Black = 0,
    LightBlue,
    LightGreen,
    LightCyan,
    LightRed,
    Magenta,
    Brown,
    LightGray,
    DarkGray,
    Blue,
    Lime,
    Cyan,
    Red,
    Fuchsia,
    Yellow,
    White
} COLOR;

#define UTILITY_VERSION L"20190314"

// determines the size of status square
#define STATUS_SQUARE_SIDE 10

#define SCREENSHOTDRIVER_VERSION 0x1

EFI_DRIVER_BINDING_PROTOCOL gScreenshotDriverBinding = {
    ScreenshotDriverBindingSupported,
    ScreenshotDriverBindingStart,
    ScreenshotDriverBindingStop,
    SCREENSHOTDRIVER_VERSION,
    NULL,
    NULL
};

EFI_HANDLE SimpleTextInExHandle;

GLOBAL_REMOVE_IF_UNREFERENCED
EFI_COMPONENT_NAME2_PROTOCOL gScreenshotDriverComponentName2 = {
    (EFI_COMPONENT_NAME2_GET_DRIVER_NAME) ScreenshotDriverComponentNameGetDriverName,
    (EFI_COMPONENT_NAME2_GET_CONTROLLER_NAME) ScreenshotDriverComponentNameGetControllerName,
    "en"
};

GLOBAL_REMOVE_IF_UNREFERENCED 
EFI_UNICODE_STRING_TABLE mScreenshotDriverNameTable[] = {
    { "en", (CHAR16 *) L"ScreenshotDriver" },
    { NULL , NULL }
};

GLOBAL_REMOVE_IF_UNREFERENCED
EFI_DRIVER_DIAGNOSTICS2_PROTOCOL gScreenshotDriverDiagnostics2 = {
    ScreenshotDriverRunDiagnostics,
    "en"
}; 

GLOBAL_REMOVE_IF_UNREFERENCED
EFI_DRIVER_CONFIGURATION2_PROTOCOL gScreenshotDriverConfiguration2 = {
    ScreenshotDriverConfigurationSetOptions,
    ScreenshotDriverConfigurationOptionsValid,
    ScreenshotDriverConfigurationForceDefaults,
    "en"
};


EFI_STATUS
EFIAPI
ScreenshotDriverConfigurationSetOptions( IN  EFI_DRIVER_CONFIGURATION2_PROTOCOL       *This,
                                         IN  EFI_HANDLE                               ControllerHandle,
                                         IN  EFI_HANDLE                               ChildHandle,
                                         IN  CHAR8                                    *Language,
                                         OUT EFI_DRIVER_CONFIGURATION_ACTION_REQUIRED *ActionRequired )
{
    return EFI_UNSUPPORTED;
}


EFI_STATUS
EFIAPI
ScreenshotDriverConfigurationOptionsValid( IN EFI_DRIVER_CONFIGURATION2_PROTOCOL *This,
                                           IN EFI_HANDLE                         ControllerHandle,
                                           IN EFI_HANDLE                         ChildHandle )
{
    return EFI_UNSUPPORTED;
}


EFI_STATUS
EFIAPI
ScreenshotDriverConfigurationForceDefaults( IN  EFI_DRIVER_CONFIGURATION2_PROTOCOL       *This,
                                            IN  EFI_HANDLE                               ControllerHandle,
                                            IN  EFI_HANDLE                               ChildHandle,
                                            IN  UINT32                                   DefaultType,
                                            OUT EFI_DRIVER_CONFIGURATION_ACTION_REQUIRED *ActionRequired )
{
    return EFI_UNSUPPORTED;
}


EFI_STATUS
EFIAPI
ScreenshotDriverRunDiagnostics( IN  EFI_DRIVER_DIAGNOSTICS2_PROTOCOL *This,
                                IN  EFI_HANDLE                       ControllerHandle,
                                IN  EFI_HANDLE                       ChildHandle, 
                                IN  EFI_DRIVER_DIAGNOSTIC_TYPE       DiagnosticType, 
                                IN  CHAR8                            *Language,
                                OUT EFI_GUID                         **ErrorType, 
                                OUT UINTN                            *BufferSize,
                                OUT CHAR16                           **Buffer )
{
    return EFI_UNSUPPORTED;
}


//
//  Retrieve user readable name of the driver
//
EFI_STATUS
EFIAPI
ScreenshotDriverComponentNameGetDriverName( IN  EFI_COMPONENT_NAME2_PROTOCOL *This,
                                            IN  CHAR8                        *Language,
                                            OUT CHAR16                       **DriverName )
{
    return LookupUnicodeString2( Language,
                                 This->SupportedLanguages,
                                 mScreenshotDriverNameTable,
                                 DriverName,
                                 (BOOLEAN)(This == &gScreenshotDriverComponentName2) );
}


//
//  Retrieve user readable name of controller being managed by a driver
//
EFI_STATUS
EFIAPI
ScreenshotDriverComponentNameGetControllerName( IN  EFI_COMPONENT_NAME2_PROTOCOL *This,
                                                IN  EFI_HANDLE                   ControllerHandle,
                                                IN  EFI_HANDLE                   ChildHandle,
                                                IN  CHAR8                        *Language,
                                                OUT CHAR16                       **ControllerName )
{
    return EFI_UNSUPPORTED;
}


//
//  Start this driver on Controller
//
EFI_STATUS
EFIAPI
ScreenshotDriverBindingStart( IN EFI_DRIVER_BINDING_PROTOCOL *This,
                              IN EFI_HANDLE                  Controller,
                              IN EFI_DEVICE_PATH_PROTOCOL    *RemainingDevicePath )
{
    return EFI_UNSUPPORTED;
}


//
//  Stop this driver on ControllerHandle.
//
EFI_STATUS
EFIAPI
ScreenshotDriverBindingStop( IN EFI_DRIVER_BINDING_PROTOCOL *This,
                             IN EFI_HANDLE                  Controller,
                             IN UINTN                       NumberOfChildren,
                             IN EFI_HANDLE                  *ChildHandleBuffer )
{
    return EFI_UNSUPPORTED;
}


//
//  See if this driver supports ControllerHandle.
//
EFI_STATUS
EFIAPI
ScreenshotDriverBindingSupported( IN EFI_DRIVER_BINDING_PROTOCOL *This,
                                  IN EFI_HANDLE                  Controller,
                                  IN EFI_DEVICE_PATH_PROTOCOL    *RemainingDevicePath )
{
    return EFI_UNSUPPORTED;
}


BOOLEAN
IsUnicodeDecimalDigit( CHAR16 Char )
{
    return ((BOOLEAN)(Char >= L'0' && Char <= L'9'));
}


EFI_STATUS
UnicodeStringToInteger( CHAR16 *String,
                        UINTN  *Value )
{
    UINTN Result = 0;

    if (String == NULL || StrSize (String) == 0 || Value == NULL) {
        return (EFI_INVALID_PARAMETER);
    }

    while (IsUnicodeDecimalDigit(*String)) {
        if (!(Result <= (DivU64x32((((UINT64) ~0) - (*String - L'0')),10)))) {
            return (EFI_DEVICE_ERROR);
        }

        Result = MultU64x32(Result, 10) + (*String - L'0');
        String++;
    }

    *Value = Result;

    return (EFI_SUCCESS);
}


EFI_STATUS
ShowStatus( EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
            UINT8 Color, 
            UINTN StartX, 
            UINTN StartY,
            UINTN Width, 
            UINTN Height ) 
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Square[STATUS_SQUARE_SIDE * STATUS_SQUARE_SIDE];
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL BackupTL[STATUS_SQUARE_SIDE * STATUS_SQUARE_SIDE];
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL BackupTR[STATUS_SQUARE_SIDE * STATUS_SQUARE_SIDE];
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL BackupBL[STATUS_SQUARE_SIDE * STATUS_SQUARE_SIDE];
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL BackupBR[STATUS_SQUARE_SIDE * STATUS_SQUARE_SIDE];
    
    // set square color
    for (UINTN i = 0 ; i < STATUS_SQUARE_SIDE * STATUS_SQUARE_SIDE; i++) {
        Square[i].Blue = EfiGraphicsColors[Color].Blue;
        Square[i].Green = EfiGraphicsColors[Color].Green;
        Square[i].Red = EfiGraphicsColors[Color].Red;
        Square[i].Reserved = 0x00;
    }
    
    Width = Width - STATUS_SQUARE_SIDE -1;
    Height = Height - STATUS_SQUARE_SIDE -1;
    
    // backup current squares
    Gop->Blt(Gop, BackupTL, EfiBltVideoToBltBuffer, StartX, StartY, 0, 0, STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);
    Gop->Blt(Gop, BackupTR, EfiBltVideoToBltBuffer, StartX + Width, StartY, 0, 0, STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);
    Gop->Blt(Gop, BackupBL, EfiBltVideoToBltBuffer, StartX, StartY + Height, 0, 0, STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);
    Gop->Blt(Gop, BackupBR, EfiBltVideoToBltBuffer, StartX + Width, StartY + Height, 0, 0, STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);
        
    // draw status square
    Gop->Blt(Gop, Square, EfiBltBufferToVideo, 0, 0, StartX, StartY, STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);
    Gop->Blt(Gop, Square, EfiBltBufferToVideo, 0, 0, StartX + Width, StartY, STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);
    Gop->Blt(Gop, Square, EfiBltBufferToVideo, 0, 0, StartX, StartY + Height, STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);
    Gop->Blt(Gop, Square, EfiBltBufferToVideo, 0, 0, StartX + Width, StartY + Height, STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);
        
    // wait 500ms
    gBS->Stall(500*1000);
        
    // restore squares from backups
    Gop->Blt(Gop, BackupTL, EfiBltBufferToVideo, 0, 0, StartX, StartY, STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);
    Gop->Blt(Gop, BackupTR, EfiBltBufferToVideo, 0, 0, StartX + Width, StartY, STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);
    Gop->Blt(Gop, BackupBL, EfiBltBufferToVideo, 0, 0, StartX, StartY + Height, STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);
    Gop->Blt(Gop, BackupBR, EfiBltBufferToVideo, 0, 0, StartX + Width, StartY + Height, STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);
    
    return EFI_SUCCESS;
}


//
// Save image to file
//
EFI_STATUS 
SaveImage( CHAR16 *FileName,
           UINT8  *FileData,
           UINTN  FileDataLength )
{
    EFI_STATUS          Status;
    EFI_FILE_HANDLE     FileHandle;
    UINTN               BufferSize;

    Status = gModuleSelfRootDir->Open (
                        gModuleSelfRootDir, &FileHandle, FileName,
                        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0
                        );

    if (!EFI_ERROR (Status)) {
        BufferSize = FileDataLength;
        Status = FileHandle->Write (FileHandle, &BufferSize, FileData);
        FileHandle->Close (FileHandle);
    }

    return Status;
}

#ifdef SAVE_AS_PNG

EFI_STATUS
PreparePNGFile( EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BltBuffer,
                UINT32 Width,
                UINT32 Height )
{
    UINTN           ImageSize;
    UINTN           i;
    UINT8           *PngFile;
    UINTN           PngFileSize;
    EFI_STATUS      Status;
    EFI_TIME          Time;
    CHAR16            FileName[40]; 

    Status = EFI_SUCCESS;

    ImageSize = (Width * Height);

    // Convert BGR to RGBA with Alpha set to 0xFF
    for (i = 0; i < ImageSize; i++) {
        UINT8 Temp;

        Temp = BltBuffer[i].Blue;

        BltBuffer[i].Blue = BltBuffer[i].Red;
        BltBuffer[i].Red = Temp;
        BltBuffer[i].Reserved = 0xFF;
    }

    // Encode raw RGB image to PNG format
    PngFile = NULL;
    i = lodepng_encode32(&PngFile, &PngFileSize, (CONST UINT8*)BltBuffer, Width, Height);
    if (i == 0) {
        Status = gRT->GetTime(&Time, NULL);
        if (!EFI_ERROR(Status)) {
            UnicodeSPrint( FileName, 62, L"screenshot-%04d%02d%02d-%02d%02d%02d.png",
                           Time.Year, Time.Month, Time.Day, Time.Hour, Time.Minute, Time.Second );
        } else {
            UnicodeSPrint( FileName, 30, L"screenshot.png" );
        }

        SaveImage( FileName, PngFile, PngFileSize );
    }

    return Status;
}

#else

EFI_STATUS
PrepareBMPFile( EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BltBuffer,
                UINT32 Width, 
                UINT32 Height )
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Pixel;
    BMP_IMAGE_HEADER *BmpHeader;
    EFI_STATUS       Status = EFI_SUCCESS;
    EFI_TIME         Time;
    CHAR16           FileName[40]; 
    UINT8            *FileData;
    UINTN            FileDataLength;
    UINT8            *ImagePtr;
    UINT8            *ImagePtrBase;
    UINTN            ImageLineOffset;
    UINTN            x, y;

    ImageLineOffset = Width * 3;
    if ((ImageLineOffset % 4) != 0) {
        ImageLineOffset = ImageLineOffset + (4 - (ImageLineOffset % 4));
    }

    // allocate buffer for data
    FileDataLength = sizeof(BMP_IMAGE_HEADER) + Height * ImageLineOffset;
    FileData = AllocateZeroPool(FileDataLength);
    if (FileData == NULL) {
        FreePool(BltBuffer);
        DEBUG((DEBUG_ERROR, "AllocateZeroPool. No memory resources\n"));
        return EFI_OUT_OF_RESOURCES;
    }
    
    // fill header
    BmpHeader = (BMP_IMAGE_HEADER *)FileData;
    BmpHeader->CharB = 'B';
    BmpHeader->CharM = 'M';
    BmpHeader->Size = (UINT32)FileDataLength;
    BmpHeader->ImageOffset = sizeof(BMP_IMAGE_HEADER);
    BmpHeader->HeaderSize = 40;
    BmpHeader->PixelWidth = Width;
    BmpHeader->PixelHeight = Height;
    BmpHeader->Planes = 1;
    BmpHeader->BitPerPixel = 24;
    BmpHeader->CompressionType = 0;
    BmpHeader->XPixelsPerMeter = 0;
    BmpHeader->YPixelsPerMeter = 0;
    
    // fill pixel buffer
    ImagePtrBase = FileData + BmpHeader->ImageOffset;
    for (y = 0; y < Height; y++) {
        ImagePtr = ImagePtrBase;
        ImagePtrBase += ImageLineOffset;
        Pixel = BltBuffer + (Height - 1 - y) * Width;
        
        for (x = 0; x < Width; x++) {
            *ImagePtr++ = Pixel->Blue;
            *ImagePtr++ = Pixel->Green;
            *ImagePtr++ = Pixel->Red;
            Pixel++;
        }
    }

    FreePool(BltBuffer);

    Status = gRT->GetTime(&Time, NULL);
    if (!EFI_ERROR(Status)) {
        UnicodeSPrint( FileName, 62, L"screenshot-%04d%02d%02d-%02d%02d%02d.bmp", 
                       Time.Year, Time.Month, Time.Day, Time.Hour, Time.Minute, Time.Second );
    } else {
        UnicodeSPrint( FileName, 30, L"screenshot.bmp" );
    }
    
    SaveImage( FileName, FileData, FileDataLength );

    FreePool( FileData );

    return Status;
}

#endif


EFI_STATUS
SnapShot( EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop, 
          UINTN StartX, 
          UINTN StartY,
          UINTN Width, 
          UINTN Height ) 
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BltBuffer = NULL;
    EFI_STATUS Status = EFI_SUCCESS;
    UINTN      ImageSize;  

    ImageSize = Width * Height;
            
    // allocate memory for snapshot
    BltBuffer = AllocateZeroPool( sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) * ImageSize );
    if (BltBuffer == NULL) {
        DEBUG((DEBUG_ERROR, "BltBuffer. No memory resources\n"));
        return EFI_OUT_OF_RESOURCES;
    }

    // take screenshot
    Status = Gop->Blt( Gop, BltBuffer, EfiBltVideoToBltBuffer, StartX, StartY, 0, 0, Width, Height, 0 );
    if (EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR, "Gop->Blt [%d]\n", Status));
        FreePool( BltBuffer );
        return Status;
    }

    #ifdef SAVE_AS_PNG
    PreparePNGFile( BltBuffer, (UINT32)Width, (UINT32)Height );
    #else
    PrepareBMPFile( BltBuffer, (UINT32)Width, (UINT32)Height );
    #endif

    return Status;
}

EFI_STATUS
EFIAPI
TakeScreenShot( EFI_KEY_DATA *KeyData )
{
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
    EFI_DEVICE_PATH_PROTOCOL     *Dpp;
    EFI_HANDLE                   *Handles = NULL;
    UINTN                        HandleCount = 0;
    UINTN StartX = 0, StartY = 0, Width = 0, Height = 0; 

    // try locating GOP by handle
    Status = gBS->LocateHandleBuffer( ByProtocol,
                                      &gEfiGraphicsOutputProtocolGuid,
                                      NULL,
                                      &HandleCount,
                                      &Handles );
    if (EFI_ERROR (Status)) {
        DEBUG((DEBUG_ERROR, "No GOP handles found via LocateHandleBuffer\n"));
        return Status;
    }

    DEBUG((DEBUG_INFO, "Found %d GOP handles via LocateHandleBuffer\n", HandleCount));

    // Make sure we use the correct GOP handle
    for (UINTN Handle = 0; Handle < HandleCount; Handle++) {
        Status = gBS->HandleProtocol( Handles[Handle],
                                      &gEfiDevicePathProtocolGuid,
                                      (VOID **)&Dpp );
        if (!EFI_ERROR(Status)) {
            Status = gBS->HandleProtocol( Handles[Handle],
                                          &gEfiGraphicsOutputProtocolGuid,
                                          (VOID **)&Gop );
            if (!EFI_ERROR(Status)) {
                break;
            }
        }
    }
    FreePool( Handles );
    if (Gop == NULL) {
        DEBUG((DEBUG_ERROR, "No graphics console found.\n"));
        return Status;
    }

    Width  = Gop->Mode->Info->HorizontalResolution;
    Height = Gop->Mode->Info->VerticalResolution;

    Status = ShowStatus( Gop, Yellow, StartX, StartY, Width, Height ); 
    gBS->Stall(500*1000);
    Status = SnapShot( Gop , StartX, StartY, Width, Height );
    if (EFI_ERROR(Status)) {
        Status = ShowStatus( Gop, Red, StartX, StartY, Width, Height ); 
    } else {
        Status = ShowStatus( Gop, Lime, StartX, StartY, Width, Height ); 
    }

    return Status;
}

/*
    https://github.com/cecekpawon/UEFTW
    UEFTW Start -->
*/

STATIC
EFI_FILE_HANDLE
InternalOpenRoot (
  IN  EFI_HANDLE   DeviceHandle
  )
{
    EFI_STATUS                          Status;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL     *Volume;
    EFI_FILE_HANDLE                     File;

    File = NULL;

    Volume  = NULL;
    Status  = gBS->HandleProtocol ( DeviceHandle,
                        &gEfiSimpleFileSystemProtocolGuid,
                        (VOID **)&Volume );

    if (!EFI_ERROR (Status)) {
        Status = Volume->OpenVolume (Volume, &File);
    }

    return File;
}

STATIC
EFI_STATUS
InternalScanVolumes ( VOID )
{
    EFI_STATUS    Status;
    EFI_HANDLE    *Handles;
    UINTN         HandleCount;
    UINTN         HandleIndex;

    Handles       = NULL;
    HandleCount   = 0;
    Status        = gBS->LocateHandleBuffer (
                         ByProtocol,
                         &gEfiBlockIoProtocolGuid,
                         NULL,
                         &HandleCount,
                         &Handles
                         );

    if (!EFI_ERROR (Status)) {

        for (HandleIndex = 0; HandleIndex < HandleCount; HandleIndex++) {

            EFI_HANDLE    DeviceHandle;
            VOID          *Instance;

            DeviceHandle = Handles[HandleIndex];

            Instance  = NULL;
            Status    = gBS->HandleProtocol ( DeviceHandle,
                               &gEfiPartTypeSystemPartGuid,
                               &Instance );

            if (!EFI_ERROR (Status)) {
                gDevicePath = DevicePathFromHandle (DeviceHandle);
                gModuleSelfRootDir = InternalOpenRoot (DeviceHandle);

                if (gModuleSelfRootDir != NULL) {
                    break;
                }
            }
        }

        FreePool (Handles);
    }

    return Status;
}

EFI_STATUS
GetWritablePath ( IN EFI_HANDLE ImageHandle )
{
    EFI_STATUS  Status;

    Status = gBS->HandleProtocol ( ImageHandle,
                        &gEfiLoadedImageProtocolGuid,
                        (VOID **)&gModuleSelfLoadedImage );

    if (!EFI_ERROR (Status)) {
        gDevicePath = DevicePathFromHandle (gModuleSelfLoadedImage->DeviceHandle);
        gModuleSelfRootDir = InternalOpenRoot (gModuleSelfLoadedImage->DeviceHandle);
    }

    if (gModuleSelfRootDir == NULL) {
        DEBUG((DEBUG_ERROR, "Get self-path failed\n"));

        Status = InternalScanVolumes ();

        if (EFI_ERROR(Status)) {
            DEBUG((DEBUG_ERROR, "InternalScanVolumes %r\n", Status));
        }
    }

    return Status;
}

/*
    UEFTW End <--
*/


EFI_STATUS
EFIAPI
ScreenshotDriverEntryPoint( EFI_HANDLE ImageHandle,
                            EFI_SYSTEM_TABLE *SystemTable )
{
    //EFI_GUID gEfiSimpleTextInputExProtocolGuid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;
    EFI_STATUS                        Status;
    EFI_KEY_DATA                      SimpleTextInExKeyStroke;
    EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *SimpleTextInEx;

    // install driver model protocol(s).
    Status = EfiLibInstallAllDriverProtocols2( ImageHandle,
                                               SystemTable,
                                               &gScreenshotDriverBinding,
                                               ImageHandle,
                                               NULL,
                                               &gScreenshotDriverComponentName2,
                                               NULL,
                                               &gScreenshotDriverConfiguration2,
                                               NULL,
                                               &gScreenshotDriverDiagnostics2 );

    if (EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR, "EfiLibInstallDriverBindingComponentName2 [%d]\n", Status));
        return Status;
    }

    Status = GetWritablePath (ImageHandle);

    if (EFI_ERROR (Status) || (gModuleSelfRootDir == NULL)) {
        DEBUG((DEBUG_ERROR, "Find writable volume failed\n"));
        return EFI_UNSUPPORTED;
    }

    // set key combination to be LEFTCTRL+LEFTALT+F12
    SimpleTextInExKeyStroke.Key.ScanCode = SCAN_F12;
    SimpleTextInExKeyStroke.Key.UnicodeChar = 0;
    SimpleTextInExKeyStroke.KeyState.KeyShiftState = EFI_SHIFT_STATE_VALID | EFI_LEFT_CONTROL_PRESSED | EFI_LEFT_ALT_PRESSED;
    SimpleTextInExKeyStroke.KeyState.KeyToggleState = 0;

    Status = gBS->HandleProtocol( gST->ConsoleInHandle, 
                                  &gEfiSimpleTextInputExProtocolGuid,
                                  (VOID **) &SimpleTextInEx );
    if (EFI_ERROR (Status)) {
        DEBUG((DEBUG_ERROR, "SimpleTextInputEx handle not found via HandleProtocol\n"));
        return Status;
    }

    // register key notification function
    Status = SimpleTextInEx->RegisterKeyNotify( SimpleTextInEx,
                                                &SimpleTextInExKeyStroke,
                                                TakeScreenShot,
                                                &SimpleTextInExHandle );
    if (EFI_ERROR (Status)) {
        DEBUG((DEBUG_ERROR, "SimpleTextInEx->RegisterKeyNotify returned %d\n", Status));
        return Status;
    }

    return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
ScreenshotDriverUnload( EFI_HANDLE ImageHandle )
{
    //EFI_GUID gEfiSimpleTextInputExProtocolGuid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;
    EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *SimpleTextInEx;
    EFI_STATUS Status = EFI_SUCCESS;
    UINTN      Index;
    UINTN      HandleCount = 0;
    EFI_HANDLE *HandleBuffer = NULL;

    Status = gBS->HandleProtocol( gST->ConsoleInHandle, 
                                  &gEfiSimpleTextInputExProtocolGuid, 
                                  (VOID **) &SimpleTextInEx );
    if (EFI_ERROR (Status)) {
        DEBUG((DEBUG_ERROR, "SimpleTextInputEx handle not found via HandleProtocol\n"));
        return Status;
    }

    // unregister key notification function
    Status = SimpleTextInEx->UnregisterKeyNotify( SimpleTextInEx,
                                                  SimpleTextInExHandle );
    if (EFI_ERROR (Status)) {
        DEBUG((DEBUG_ERROR, "SimpleTextInEx->UnregisterKeyNotify returned %d\n", Status));
        return Status;
    }

    // get list of all the handles in the handle database.
    Status = gBS->LocateHandleBuffer( AllHandles,
                                      NULL,
                                      NULL,
                                      &HandleCount,
                                      &HandleBuffer );
    if (EFI_ERROR (Status)) {
        DEBUG((DEBUG_ERROR, "LocateHandleBuffer [%d]\n", Status));
        return Status;
    }

    for (Index = 0; Index < HandleCount; Index++) {
        Status = gBS->DisconnectController( HandleBuffer[Index],
                                            ImageHandle,
                                            NULL );
    }

    if (HandleBuffer != NULL) {
        FreePool( HandleBuffer );
    }

    // uninstall protocols installed in the driver entry point
    Status = gBS->UninstallMultipleProtocolInterfaces( ImageHandle,
                                                       &gEfiDriverBindingProtocolGuid, &gScreenshotDriverBinding,
                                                       &gEfiComponentName2ProtocolGuid, &gScreenshotDriverComponentName2,
                                                       &gEfiDriverConfiguration2ProtocolGuid, gScreenshotDriverConfiguration2, 
                                                       &gEfiDriverDiagnostics2ProtocolGuid, &gScreenshotDriverDiagnostics2, 
                                                       NULL );
    if (EFI_ERROR (Status)) {
        DEBUG((DEBUG_ERROR, "UninstallMultipleProtocolInterfaces returned %d\n", Status));
        return Status;
    }

    return EFI_SUCCESS;
}


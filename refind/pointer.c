/*
* refind/pointer.c
* Pointer device functions
*
* Copyright (c) 2018 CJ Vaughter
* All rights reserved.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "lib.h"
#include "global.h"
#include "screen.h"
#include "pointer.h"
#include "icns.h"
#include "Math.h"
#include "../include/refit_call_wrapper.h"
#include "../refind/lib.h"

EFI_HANDLE* APointerHandles = NULL;
EFI_ABSOLUTE_POINTER_PROTOCOL** APointerProtocol = NULL;
EFI_GUID APointerGuid = EFI_ABSOLUTE_POINTER_PROTOCOL_GUID;
UINTN NumAPointerDevices = 0;

EFI_HANDLE* SPointerHandles = NULL;
EFI_SIMPLE_POINTER_PROTOCOL** SPointerProtocol = NULL;
EFI_GUID SPointerGuid = EFI_SIMPLE_POINTER_PROTOCOL_GUID;
UINTN NumSPointerDevices = 0;

BOOLEAN PointerAvailable = FALSE;

UINTN LastXPos = 0, LastYPos = 0;
EG_IMAGE* MouseImage = NULL;
EG_IMAGE* Background = NULL;

POINTER_STATE State;

BOOLEAN gSuppressPointerDraw = FALSE;
BOOLEAN MouseTouchActive = TRUE;
// Add this global boolean declaration

////////////////////////////////////////////////////////////////////////////////
// Initialize all pointer devices
////////////////////////////////////////////////////////////////////////////////
VOID pdInitialize() {
    pdCleanup();
// just in case

    if (!(GlobalConfig.EnableMouse || GlobalConfig.EnableTouch)) {
        MouseTouchActive = FALSE;
// Set to FALSE if no pointer config is enabled 
        return;
    }

    // Get all handles that support absolute pointer protocol (usually touchscreens, but sometimes mice)
    UINTN NumPointerHandles = 0;
    EFI_STATUS handlestatus = refit_call5_wrapper(gBS->LocateHandleBuffer, ByProtocol, &APointerGuid, NULL,
                                                  &NumPointerHandles, &APointerHandles);
    if (!EFI_ERROR(handlestatus)) {
        APointerProtocol = AllocatePool(sizeof(EFI_ABSOLUTE_POINTER_PROTOCOL*) * NumPointerHandles);
        UINTN Index;
        for(Index = 0; Index < NumPointerHandles; Index++) {
            // Open the protocol on the handle
            EFI_STATUS status = refit_call6_wrapper(gBS->OpenProtocol, APointerHandles[Index], &APointerGuid,
                                                     (VOID **) &APointerProtocol[NumAPointerDevices],
   
                                                    SelfImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
            if (status == EFI_SUCCESS) {
                NumAPointerDevices++;
// NEW: Add a small delay here if needed for absolute pointers (e.g., touchscreens)
                 refit_call1_wrapper(gBS->Stall, 5 * 1000);
// 5 milliseconds (5000 microseconds)
            }
        }
    } else {
        GlobalConfig.EnableTouch = FALSE;
    }

    // Get all handles that support simple pointer protocol (mice)
    NumPointerHandles = 0;
    handlestatus = refit_call5_wrapper(gBS->LocateHandleBuffer, ByProtocol, &SPointerGuid, NULL,
                                      &NumPointerHandles, &SPointerHandles);
    if(!EFI_ERROR(handlestatus)) {
        SPointerProtocol = AllocatePool(sizeof(EFI_SIMPLE_POINTER_PROTOCOL*) * NumPointerHandles);
        UINTN Index;
        for(Index = 0; Index < NumPointerHandles; Index++) {
            // Open the protocol on the handle
            EFI_STATUS status = refit_call6_wrapper(gBS->OpenProtocol, SPointerHandles[Index], &SPointerGuid, (VOID **) &SPointerProtocol[NumSPointerDevices], SelfImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
            if (status == EFI_SUCCESS) {
                NumSPointerDevices++;
// NEW: Add a small delay after successfully opening the protocol for each simple pointer device
                refit_call1_wrapper(gBS->Stall, 5 * 1000);
// 5 milliseconds (5000 microseconds)
            } 
        }
    } else {
        GlobalConfig.EnableMouse = FALSE;
    }
    // Existing 0.5-second general delay for all pointer drivers/firmware to settle
    if (NumAPointerDevices > 0 || NumSPointerDevices > 0) { // Check if any devices were successfully opened 
        refit_call1_wrapper(gBS->Stall, 500000);
// 500,000 microseconds = 0.5 seconds 
    }
    // --- START OF SURGICAL FIX (from refindplus logic) ---
    // Set PointerAvailable: True if any pointer device was successfully opened.
    PointerAvailable = (NumAPointerDevices > 0 || NumSPointerDevices > 0);

    // Set MouseTouchActive: True if either mouse or touch config is enabled AND a pointer device is available.
    MouseTouchActive = (GlobalConfig.EnableMouse || GlobalConfig.EnableTouch) ? PointerAvailable : FALSE;
    // --- END OF SURGICAL FIX ---
// Load mouse icon - More robust loading (similar to RefindPlus logic)
    if (GlobalConfig.EnableMouse) {
        MouseImage = BuiltinIcon(BUILTIN_ICON_MOUSE);
    }
}
////////////////////////////////////////////////////////////////////////////////
// Frees allocated memory and closes pointer protocols
////////////////////////////////////////////////////////////////////////////////
VOID pdCleanup() {
PointerAvailable = FALSE;
// pdClear();
// No longer calling pdClear directly here, its logic is handled within pdDraw

// Modified pdCleanup to directly restore background and free image
if (Background) {
    egDrawImage(Background, LastXPos, LastYPos);
    egFreeImage(Background);
    Background = NULL;
}

if(APointerHandles) {
UINTN Index;
for(Index = 0; Index < NumAPointerDevices; Index++) {
refit_call4_wrapper(gBS->CloseProtocol, APointerHandles[Index], &APointerGuid, SelfImageHandle, NULL);
}
MyFreePool(APointerHandles);
APointerHandles = NULL;
}
if(APointerProtocol) {
MyFreePool(APointerProtocol);
APointerProtocol = NULL;
}
if(SPointerHandles) {
UINTN Index;
for(Index = 0; Index < NumSPointerDevices; Index++) {
refit_call4_wrapper(gBS->CloseProtocol, SPointerHandles[Index], &SPointerGuid, SelfImageHandle, NULL);
}
MyFreePool(SPointerHandles);
SPointerHandles = NULL;
}
if(SPointerProtocol) {
MyFreePool(SPointerProtocol);
SPointerProtocol = NULL;
}
if(MouseImage) {
egFreeImage(MouseImage);
// Background = NULL; // This line was problematic if Background was still in use by pdDraw for its own cleanup
MouseImage = NULL;
// Ensure MouseImage is also set to NULL after freeing
}
NumAPointerDevices = 0;
NumSPointerDevices = 0;

LastXPos = UGAWidth / 2;
LastYPos = UGAHeight / 2;

State.X = UGAWidth / 2;
State.Y = UGAHeight / 2;
State.Press = FALSE;
State.Holding = FALSE;
}
////////////////////////////////////////////////////////////////////////////////
// Returns whether or not any pointer devices are available
////////////////////////////////////////////////////////////////////////////////
BOOLEAN pdAvailable() {
return PointerAvailable;
}

////////////////////////////////////////////////////////////////////////////////
// Returns the number of pointer devices available
////////////////////////////////////////////////////////////////////////////////
UINTN pdCount() {
return NumAPointerDevices + NumSPointerDevices;
}

////////////////////////////////////////////////////////////////////////////////
// Returns a pointer device's WaitForInput event
////////////////////////////////////////////////////////////////////////////////
EFI_EVENT pdWaitEvent(UINTN Index) {
if(!PointerAvailable || Index >= NumAPointerDevices + NumSPointerDevices) {
return NULL;
}

if(Index >= NumAPointerDevices) {
return SPointerProtocol[Index - NumAPointerDevices]->WaitForInput;
}
return APointerProtocol[Index]->WaitForInput;
}

////////////////////////////////////////////////////////////////////////////////
// Gets the current state of all pointer devices and assigns State to
// the first available device's state
////////////////////////////////////////////////////////////////////////////////
static BOOLEAN LastHolding = FALSE;

EFI_STATUS pdUpdateState (VOID) {
    EFI_STATUS                 Status = EFI_NOT_READY;
    UINTN                      Index;
    INT32                      TargetX;
    INT32                      TargetY;
    INT64                      TempINT64;
    UINT64                     TempUINT64;
    EFI_SIMPLE_POINTER_STATE   SPointerState;
    EFI_ABSOLUTE_POINTER_STATE APointerState;

    #if defined (EFI32) && defined (__MAKEWITH_GNUEFI)
    return EFI_NOT_READY;
    #endif

    if (!PointerAvailable) {
        return EFI_NOT_READY;
    }

    LastHolding = State.Holding;

    do {
        for (Index = 0; Index < NumAPointerDevices; Index++) {
            Status = refit_call2_wrapper(
                APointerProtocol[Index]->GetState,
                APointerProtocol[Index], &APointerState
            );
            if (EFI_ERROR(Status)) {
                continue; // 'for' loop
            }

            TempUINT64 = DivU64x64Remainder (
                (UINT64) APointerState.CurrentX * (UINT64) UGAWidth,
                (UINT64) APointerProtocol[Index]->Mode->AbsoluteMaxX,
                NULL
            );
            State.X = (UINTN)TempUINT64;

            TempUINT64 = DivU64x64Remainder (
                (UINT64) APointerState.CurrentY * (UINT64) UGAHeight,
                (UINT64) APointerProtocol[Index]->Mode->AbsoluteMaxY,
                NULL
            );
            State.Y = (UINTN)TempUINT64;

            State.Holding = (
                APointerState.ActiveButtons & EFI_ABSP_TouchActive
            );

            break; // 'for' loop
        } // for

        if (!EFI_ERROR(Status)) {
            break; // 'do' loop
        }

        for (Index = 0; Index < NumSPointerDevices; Index++) {
            Status = refit_call2_wrapper(
                SPointerProtocol[Index]->GetState,
                SPointerProtocol[Index], &SPointerState
            );
            if (EFI_ERROR(Status)) {
                continue; // 'for' loop
            }

            TempINT64 = (INT64) State.X + DivS64x64Remainder (
                (INT64) SPointerState.RelativeMovementX * (INT64) GlobalConfig.MouseSpeed,
                (INT64) SPointerProtocol[Index]->Mode->ResolutionX,
                NULL
            );
            TargetX = (INT32)TempINT64;

            TempINT64 = (INT64) State.Y + DivS64x64Remainder (
                (INT64) SPointerState.RelativeMovementY * (INT64) GlobalConfig.MouseSpeed,
                (INT64) SPointerProtocol[Index]->Mode->ResolutionY,
                NULL
            );
            TargetY = (INT32)TempINT64;

            if (TargetX < 0)           State.X = 0;
            else if (TargetX >= UGAWidth) State.X = (UINTN)(UGAWidth - 1);
            else                          State.X = (UINTN)TargetX;

            if (TargetY < 0)           State.Y = 0;
            else if (TargetY >= UGAHeight) State.Y = (UINTN)(UGAHeight - 1);
            else                          State.Y = (UINTN)TargetY;

            State.Holding = (SPointerState.LeftButton || SPointerState.RightButton);

            break; // 'for' loop
        } // for
    } while (0); // This 'loop' only runs once

    State.Press = (!LastHolding && State.Holding);

    if (EFI_ERROR(Status)) {
        Status = EFI_NOT_READY;
    }

    return Status;
}
////////////////////////////////////////////////////////////////////////////////
// Returns the current pointer state
////////////////////////////////////////////////////////////////////////////////
POINTER_STATE pdGetState() {
return State;
}
////////////////////////////////////////////////////////////////////////////////
// Draw the mouse at the current coordinates
////////////////////////////////////////////////////////////////////////////////
VOID pdDraw() {
    // Gate the drawing if no pointer system is active
    if (!MouseTouchActive) {
        return;
    }
    if (gSuppressPointerDraw) {
        return;
    }
    // if (State.X == LastXPos && State.Y == LastYPos) {return;}
    // Restore the old background (clear the previous pointer position)
    if(Background != NULL) {
        egDrawImage(Background, LastXPos, LastYPos);
// Restore the background where the pointer previously was
        egFreeImage(Background);
// Free the old background image (it's been drawn back to screen)
        Background = NULL;
// Mark as NULL
    }

    // If MouseImage is not loaded, we can't draw anything
    if (MouseImage == NULL) {
        return;
    }

    // Capture the new background and draw the pointer at the current position
    UINTN Width  = MouseImage->Width;
    UINTN Height = MouseImage->Height;

    if(State.X + Width > UGAWidth) {
        Width = UGAWidth - State.X;
    }
    if(State.Y + Height > UGAHeight) {
        Height = UGAHeight - State.Y;
    }

    Background = egCopyScreenArea(State.X, State.Y, Width, Height);
    if(Background != NULL) { // Only attempt to draw if background was successfully captured
        BltImageCompositeBadge(Background, MouseImage, NULL, State.X, State.Y);
    }

    // Update LastXPos/LastYPos for the next frame's comparison
    LastXPos = State.X;
    LastYPos = State.Y;
}
////////////////////////////////////////////////////////////////////////////////
// Restores the background at the position the mouse was last drawn
////////////////////////////////////////////////////////////////////////////////
VOID pdClear() {
    // Gate pdClear with MouseTouchActive
    if (!MouseTouchActive) { // Add this gate
        return;
    }
    if (Background) {
        egDrawImage(Background, LastXPos, LastYPos);
        egFreeImage(Background);
        Background = NULL;
    }
}

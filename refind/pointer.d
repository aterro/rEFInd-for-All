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
#include "libeg.h"
#include "log.h"

#include "log.h"

// Replicate rEFIt's PrintPointerVars for debugging
static INTN PrintCount = 0;
VOID PrintPointerVars(
                      INT32     RelX,
                      INT32     RelY,
                      INTN      ScreenRelX,
                      INTN      ScreenRelY,
                      INTN      XPosPrev,
                      INTN      YPosPrev,
                      INTN      XPos,
                      INTN      YPos
                      )
{
  if (PrintCount < 4) {
    // NOTE: This uses DBG() which might not be active in release builds,
    // but it's the safest way to add logging without adding new dependencies.
    LOG(5, LOG_LINE_NORMAL, L"rEFInd Pointer DBG:\n");
    LOG(5, LOG_LINE_NORMAL, L"  RelX, RelY: %d, %d\n", RelX, RelY);
    LOG(5, LOG_LINE_NORMAL, L"  ScreenRelX, ScreenRelY: %d, %d\n", ScreenRelX, ScreenRelY);
    LOG(5, LOG_LINE_NORMAL, L"  XPos: %d + %d = %d -> %d\n", XPosPrev, ScreenRelX, (XPosPrev + ScreenRelX), XPos);
    LOG(5, LOG_LINE_NORMAL, L"  YPos: %d + %d = %d -> %d\n", YPosPrev, ScreenRelY, (YPosPrev + ScreenRelY), YPos);
    PrintCount++;
  }
}

// Replicate rEFIt's egRawCopy which is not in rEFInd's libeg
static VOID egRawCopy(IN EG_PIXEL *dst, IN EG_PIXEL *src, IN INTN width, IN INTN height, IN INTN dst_stride, IN INTN src_stride)
{
    INTN x, y;
    if (!dst || !src) return;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            dst[y * dst_stride + x] = src[y * src_stride + x];
        }
    }
}

BOOLEAN PointerInitialized = FALSE;

EFI_GUID gEfiSimplePointerProtocolGuid = EFI_SIMPLE_POINTER_PROTOCOL_GUID;

EFI_SIMPLE_POINTER_PROTOCOL* gSimplePointer = NULL;

typedef struct {
    UINTN XPos;
    UINTN YPos;
    UINTN Width;
    UINTN Height;
} EG_RECT;

EG_RECT PointerPlace;
EG_RECT OldPointerPlace;

EG_IMAGE* BackgroundImage = NULL;
EG_IMAGE* CompositedImage = NULL;



BOOLEAN PointerAvailable = FALSE;

UINTN LastXPos = 0, LastYPos = 0;
EG_IMAGE* MouseImage = NULL;
EG_IMAGE* PointerImage = NULL;
EG_IMAGE* Background = NULL;

POINTER_STATE State;

BOOLEAN gSuppressPointerDraw = FALSE;
BOOLEAN MouseTouchActive = TRUE;
static BOOLEAN LastHolding = FALSE;
static BOOLEAN PointerDrawnOnce = FALSE;

////////////////////////////////////////////////////////////////////////////////
// Initialize all pointer devices
////////////////////////////////////////////////////////////////////////////////
VOID pdInitialize() {
    EFI_STATUS Status;

    if (PointerInitialized || !GlobalConfig.EnableMouse) {
        MouseTouchActive = FALSE;
        return;
    }

    EFI_HANDLE  *HandleBuffer = NULL;
    UINTN       HandleCount = 0;
    UINTN       Index;

    Status = refit_call5_wrapper(gBS->LocateHandleBuffer,
                                        ByProtocol,
                                        &gEfiSimplePointerProtocolGuid,
                                        NULL,
                                        &HandleCount,
                                        &HandleBuffer);

    if (!EFI_ERROR(Status) && HandleCount > 0) {
        for (Index = 0; Index < HandleCount; Index++) {
            EFI_SIMPLE_POINTER_PROTOCOL* currentSimplePointer = NULL;
            Status = refit_call3_wrapper(gBS->HandleProtocol,
                                         HandleBuffer[Index],
                                         &gEfiSimplePointerProtocolGuid,
                                         (VOID **)&currentSimplePointer);
            if (!EFI_ERROR(Status) && currentSimplePointer != NULL) {
                // Check if this protocol reports sensible resolution values
                if (currentSimplePointer->Mode != NULL &&
                    (currentSimplePointer->Mode->ResolutionX != 0 || currentSimplePointer->Mode->ResolutionY != 0)) {
                    gSimplePointer = currentSimplePointer;
                    LOG(5, LOG_LINE_NORMAL, L"rEFInd Pointer Init DBG: Found functional SimplePointerProtocol on handle %p\n", HandleBuffer[Index]);
                    LOG(5, LOG_LINE_NORMAL, L"  ResolutionX: %ld\n", gSimplePointer->Mode->ResolutionX);
                    LOG(5, LOG_LINE_NORMAL, L"  ResolutionY: %ld\n", gSimplePointer->Mode->ResolutionY);
                    LOG(5, LOG_LINE_NORMAL, L"  LeftButton: %d\n", gSimplePointer->Mode->LeftButton);
                    LOG(5, LOG_LINE_NORMAL, L"  RightButton: %d\n", gSimplePointer->Mode->RightButton);
                    break; // Found a good one, use it
                }
            }
        }
        if (HandleBuffer) {
            MyFreePool(HandleBuffer);
        }
    }

    if (gSimplePointer == NULL) {
        LOG(5, LOG_LINE_NORMAL, L"rEFInd Pointer Init DBG: No functional SimplePointerProtocol found.\n");
        MouseTouchActive = FALSE;
        return;
    }

    PointerAvailable = TRUE;
    PointerInitialized = TRUE;
    MouseTouchActive = TRUE;

    PointerImage = BuiltinIcon(BUILTIN_ICON_MOUSE);
    if (!PointerImage) {
        PointerAvailable = FALSE;
        return;
    }

    PointerPlace.XPos = UGAWidth >> 2;
    PointerPlace.YPos = UGAHeight >> 2;
    PointerPlace.Width = PointerImage->Width;
    PointerPlace.Height = PointerImage->Height;
    CopyMem(&OldPointerPlace, &PointerPlace, sizeof(EG_RECT));


    State.X = PointerPlace.XPos;
    State.Y = PointerPlace.YPos;

    BackgroundImage = egCreateImage(PointerImage->Width, PointerImage->Height, FALSE);
    CompositedImage = egCreateImage(PointerImage->Width, PointerImage->Height, FALSE);

    if (!BackgroundImage || !CompositedImage) {
        PointerAvailable = FALSE;
        if (BackgroundImage) egFreeImage(BackgroundImage);
        if (CompositedImage) egFreeImage(CompositedImage);
        return;
    }
}

VOID pdCleanup() {
    PointerAvailable = FALSE;
    PointerInitialized = FALSE;

    if (BackgroundImage) {
        egFreeImage(BackgroundImage);
        BackgroundImage = NULL;
    }
    if (CompositedImage) {
        egFreeImage(CompositedImage);
        CompositedImage = NULL;
    }
    if (PointerImage) {
        egFreeImage(PointerImage);
        PointerImage = NULL;
    }

    gSimplePointer = NULL;
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
return (gSimplePointer != NULL) ? 1 : 0;
}

////////////////////////////////////////////////////////////////////////////////
// Returns a pointer device's WaitForInput event
////////////////////////////////////////////////////////////////////////////////
EFI_EVENT pdWaitEvent(UINTN Index) {
if(!PointerAvailable || gSimplePointer == NULL || Index != 0) {
return NULL;
}
return gSimplePointer->WaitForInput;
}

////////////////////////////////////////////////////////////////////////////////
// Gets the current state of all pointer devices and assigns State to
// the first available device's state
////////////////////////////////////////////////////////////////////////////////
EFI_STATUS pdUpdateState() {
    EFI_STATUS Status = EFI_NOT_FOUND;
    EFI_SIMPLE_POINTER_STATE SPointerState;
    INTN ScreenRelX, ScreenRelY;

    if (!PointerAvailable || !gSimplePointer) {
        return EFI_NOT_FOUND;
    }

    Status = refit_call2_wrapper(gSimplePointer->GetState, gSimplePointer, &SPointerState);
    if (!EFI_ERROR(Status)) {
        INTN MouseResX, MouseResY;

        MouseResX = (INTN)gSimplePointer->Mode->ResolutionX;
        MouseResY = (INTN)gSimplePointer->Mode->ResolutionY;

        if (MouseResX <= 0) {
            MouseResX = 65536;
        }
        if (MouseResY <= 0) {
            MouseResY = 65536;
        }

        if (SPointerState.RelativeMovementX != 0 || SPointerState.RelativeMovementY != 0) {
            INTN MouseResX = (INTN)gSimplePointer->Mode->ResolutionX;
            INTN MouseResY = (INTN)gSimplePointer->Mode->ResolutionY;

            if (MouseResX <= 0) {
                MouseResX = 65536;
            }
            if (MouseResY <= 0) {
                MouseResY = 65536;
            }

            // Apply the full scaling formula, as found in rEFIt
            ScreenRelX = (INTN)(((INTN)UGAWidth * (INTN)SPointerState.RelativeMovementX / (INTN)MouseResX) * (INTN)GlobalConfig.MouseSpeed) >> 10;
            ScreenRelY = (INTN)(((INTN)UGAHeight * (INTN)SPointerState.RelativeMovementY / (INTN)MouseResY) * (INTN)GlobalConfig.MouseSpeed) >> 10;

            State.X += ScreenRelX;
            State.Y += ScreenRelY;

            if ((State.X < 0) && PointerAvailable) State.X = 0;
            if ((State.X >= UGAWidth) && PointerAvailable) State.X = UGAWidth - 1;
            if ((State.Y < 0) && PointerAvailable) State.Y = 0;
            if ((State.Y >= UGAHeight) && PointerAvailable) State.Y = UGAHeight - 1;

            PointerPlace.XPos = State.X;
            PointerPlace.YPos = State.Y;

            PrintPointerVars(SPointerState.RelativeMovementX, SPointerState.RelativeMovementY,
                             ScreenRelX, ScreenRelY, State.X - ScreenRelX, State.Y - ScreenRelY,
                             State.X, State.Y);
        }

        State.Holding = SPointerState.LeftButton || SPointerState.RightButton;
        State.Press = State.Holding && !LastHolding;
        LastHolding = State.Holding;
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
VOID pdDraw()
{
    if (!PointerAvailable || gSuppressPointerDraw) return;
    // take background image
    egTakeImage(BackgroundImage, PointerPlace.XPos, PointerPlace.YPos, PointerPlace.Width, PointerPlace.Height);
    CopyMem(&OldPointerPlace, &PointerPlace, sizeof(EG_RECT));

    // compose pointer on background
    egRawCopy(CompositedImage->PixelData, BackgroundImage->PixelData,
              PointerPlace.Width, PointerPlace.Height,
              CompositedImage->Width,
              BackgroundImage->Width);
    egComposeImage(CompositedImage, PointerImage, 0, 0);

    // draw composed image
    egDrawImageArea(CompositedImage, 0, 0,
                    PointerPlace.Width, PointerPlace.Height,
                    PointerPlace.XPos, PointerPlace.YPos);
    PointerDrawnOnce = TRUE; // Set flag after successful draw
}
////////////////////////////////////////////////////////////////////////////////
// Restores the background at the position the mouse was last drawn
////////////////////////////////////////////////////////////////////////////////
VOID pdClear()
{
    if (!PointerAvailable || !PointerDrawnOnce) return;
    egDrawImageArea(BackgroundImage, 0, 0, OldPointerPlace.Width, OldPointerPlace.Height, OldPointerPlace.XPos, OldPointerPlace.YPos);
}

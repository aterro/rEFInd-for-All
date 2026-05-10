#include "libegint.h"
#include "libeg.h"
#include "../include/refit_call_wrapper.h"

// Console defines and variables
extern EFI_GRAPHICS_OUTPUT_PROTOCOL *GraphicsOutput;
extern UINTN egScreenWidth;
extern UINTN egScreenHeight;

VOID egTakeImage(IN EG_IMAGE *Image, INTN ScreenPosX, INTN ScreenPosY,
                 IN INTN AreaWidth, IN INTN AreaHeight)
{
  if (GraphicsOutput != NULL) {
    if (ScreenPosX + AreaWidth > egScreenWidth)
    {
      AreaWidth = egScreenWidth - ScreenPosX;
    }
    if (ScreenPosY + AreaHeight > egScreenHeight)
    {
      AreaHeight = egScreenHeight - ScreenPosY;
    }
    
    GraphicsOutput->Blt(GraphicsOutput,
                        (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)Image->PixelData,
                        EfiBltVideoToBltBuffer,
                        ScreenPosX,
                        ScreenPosY,
                        0, 0, AreaWidth, AreaHeight, (UINTN)Image->Width * 4);
  }
}
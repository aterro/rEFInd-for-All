#include "lib.h"

UINT64
DivU64x64Remainder (
  IN      UINT64                    Dividend,
  IN      UINT64                    Divisor,
  OUT     UINT64                    *Remainder  OPTIONAL
  )
{
  if (Remainder != NULL) {
    *Remainder = Dividend % Divisor;
  }
  return Dividend / Divisor;
}

INT64
DivS64x64Remainder (
  IN      INT64                    Dividend,
  IN      INT64                    Divisor,
  OUT     INT64                    *Remainder  OPTIONAL
  )
{
  if (Remainder != NULL) {
    *Remainder = Dividend % Divisor;
  }
  return Dividend / Divisor;
}

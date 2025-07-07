#ifndef _MATH_H_
#define _MATH_H_

UINT64
DivU64x64Remainder (
  IN      UINT64                    Dividend,
  IN      UINT64                    Divisor,
  OUT     UINT64                    *Remainder  OPTIONAL
  );

INT64
DivS64x64Remainder (
  IN      INT64                    Dividend,
  IN      INT64                    Divisor,
  OUT     INT64                    *Remainder  OPTIONAL
  );

#endif

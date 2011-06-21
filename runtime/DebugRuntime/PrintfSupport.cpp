//===- PrintfSupport.cpp - Secure printf() replacement --------------------===//
// 
//                            The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements a secure runtime replacement for printf() and similar
// functions.
//
//===----------------------------------------------------------------------===//

//
// This code is derived from OpenBSD's vfprintf.c; original license follows:
//
// $OpenBSD: vfprintf.c,v 1.60 2010/12/22 14:54:44 millert Exp
// 
// Copyright (c) 1990 The Regents of the University of California.
// All rights reserved.
//
// This code is derived from software contributed to Berkeley by
// Chris Torek.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the University nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.
//

//
// Actual printf innards.
//
// This code is large and complicated...
//

#include "FormatStrings.h"

#include <sys/types.h>
#include <sys/mman.h>

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include <algorithm>
#include <iostream>

using std::cerr;
using std::endl;
using std::min;

using namespace NAMESPACE_SC;

// Enable floating point number support
#define FLOATING_POINT

struct __siov
{
  char *iov_base;
  size_t iov_len;
};

struct __suio
{
  struct __siov *uio_iov;
  // Number of uio_iov buffers
  int uio_iovcnt;
  // Total number of characters to print
  int uio_resid;
};

//
// Flush out all the vectors defined by the given uio,
// then reset it so that it can be reused.
//
// Return nonzero on error, zero on success.
//
static int
do_output(output_parameter &P, struct __suio &uio)
{
  //
  // Do a write to an output file if that's the output destination.
  //
  if (P.OutputKind == output_parameter::OUTPUT_TO_FILE)
  {
    FILE *out = P.Output.File;
    for (int i = 0; i < uio.uio_iovcnt; ++i)
    {
      size_t amt;
      size_t sz = uio.uio_iov[i].iov_len;
      amt = fwrite_unlocked(&uio.uio_iov[i].iov_base[0], 1, sz, out);
      if (amt < sz)
        return 1; // Output error
    }
    uio.uio_resid = 0;
    uio.uio_iovcnt = 0;
    return 0;
  }
  return 0; 
}

//
// Get the number of bytes in the object that the pointer associated with the
// pointer_info structure points to, to the end of the object.
//
// Note: Call find_object() before calling this.
//
static inline size_t
object_len(pointer_info *p)
{
  return 1 + (size_t) ((char *) p->bounds[1] - (char *) p->ptr);
}

//
// Check if the memory object referenced by the value in P is of size at
// least n.
//
static inline bool
write_check(pointer_info *P, size_t n)
{
  size_t max;
  find_object(P);
  if (P->flags & HAVEBOUNDS)
  {
    max = object_len(P);
    if (n > max)
    {
      cerr << "Writing out of bounds!" << endl;
      write_out_of_bounds_error(P, max, n);
      return false;
    }
    else
      return true;
  }
  //
  // Assume an object without discovered boundaries has enough space.
  //
  return true;
}

//
// Check if too many arguments are accessed, if so, report an error.
//
static inline void
varg_check(int pos, int total, bool *flag)
{
  if (pos > total)
  {
    cerr << "Attempting to access argument " << pos << \
      " but there are only " << total << " arguments!" << endl;
    c_library_error("va_arg");
    *flag = true;
  }
}

//
// Determine if the given pointer parameter exists in the whitelist.
//
static inline pointer_info *
check_whitelist(void **whitelist, void *p)
{
  while (*whitelist)
  {
    if (p == *whitelist)
      return (pointer_info *) p;
    ++whitelist;
  }
  cerr << "Attempting to access a non-pointer parameter as a pointer!" << endl;
  c_library_error("va_arg");
  return NULL; 
}

//
// Get the actual pointer argument from the given parameter. If the parameter
// is whitelisted and so a wrapper, this retrieves the pointer from the
// wrapper. Otherwise it just returns the parameter because it isn't recognized
// as a wrapper.
//
static inline void *
getptrarg(void **whitelist, void *p)
{
  while (*whitelist)
  {
    if (p == *whitelist)
      return ((pointer_info *)p)->ptr;
    ++whitelist;
  }
  return p;
}

//
// Secured va_arg() which checks if too many arguments are accessed
//
// fl    - flag to set to true if too many arguments are accessed
// pos   - the argument number currently being accessed
// total - the total number of variable arguments
// ap    - the va_list
// type  - the type of the argument to be accessed
//
#define va_sarg(fl, pos, total, ap, type) \
  (varg_check(pos, total, fl),            \
   va_arg(ap, type))

union arg
{
  int     intarg;
  unsigned int    uintarg;
  long      longarg;
  unsigned long   ulongarg;
  long long   longlongarg;
  unsigned long long  ulonglongarg;
  ptrdiff_t   ptrdiffarg;
  size_t      sizearg;
  ssize_t     ssizearg;
  intmax_t    intmaxarg;
  uintmax_t   uintmaxarg;
  void        *pvoidarg;

#ifdef FLOATING_POINT
  double      doublearg;
  long double   longdoublearg;
#endif

};

static int __find_arguments(const char *fmt0, va_list ap, union arg **argtable,
    size_t *argtablesiz, unsigned vargc);
static int __grow_type_table(unsigned char **typetable, int *tablesize);

#ifdef FLOATING_POINT
#include "safecode/Runtime/FloatConversion.h"

// The default floating point precision
#define DEFPREC   6 

static int exponent(char *, int, int);
#endif // FLOATING_POINT

//
// The size of the buffer we use as scratch space for integer
// conversions, among other things.  Technically, we would need the
// most space for base 10 conversions with thousands' grouping
// characters between each pair of digits.  100 bytes is a
// conservative overestimate even for a 128-bit uintmax_t.
// 
#define BUF 100

#define STATIC_ARG_TBL_SIZE 32 // Size of static argument table.


//
// Macros for converting digits to letters and vice versa
//
#define to_digit(c) ((c) - '0')
#define is_digit(c) ((unsigned)to_digit(c) <= 9)
#define to_char(n)  ((n) + '0')

//
// Flags used during conversion.
//
#define ALT       0x0001    // alternate form
#define LADJUST   0x0004    // left adjustment
#define LONGDBL   0x0008    // long double
#define LONGINT   0x0010    // long integer
#define LLONGINT  0x0020    // long long integer
#define SHORTINT  0x0040    // short integer
#define ZEROPAD   0x0080    // zero (as opposed to blank) pad
#define FPT       0x0100    // Floating point number
#define PTRINT    0x0200    // (unsigned) ptrdiff_t
#define SIZEINT   0x0400    // (signed) size_t
#define CHARINT   0x0800    // 8 bit integer
#define MAXINT    0x1000    // largest integer size (intmax_t)


//
// The main logic for printf() style functions
//
// Inputs:
//   P         - a reference to the output_parameter structure describing
//               where to do the write
//   CInfo     - a reference to the call_info structure which contains
//               information about the va_list
//   fmt0      - the format string
//   ap        - the variable argument list
//
// Returns:
//   This function returns the number of characters that would have been
//   written had the output been unbounded on success, and a negative number on
//   failure.
//
// IMPORTANT IMPLEMENTATION LIMITATIONS
//   - No support for printing wide characters (%ls or %lc)
//   - Floating point number printing is not thread safe
//   - No support for locale defined thousands grouping (the "'" flag)
//
int
internal_printf(const options_t &options,
                output_parameter &P,
                call_info &CInfo,
                const char *fmt0,
                va_list ap)
{
  char *fmt;            // format string
  int ch;               // character from fmt
  int n, n2;            // handy integers (short term usage)
  char *cp;             // handy char pointer (short term usage)
  struct __siov *iovp;  // for PRINT macro
  int flags;            // flags as above
  int ret;              // return value accumulator
  int width;            // width from format (%8d), or 0
  int prec;             // precision from format; <0 for N/A
  char sign;            // sign prefix (' ', '+', '-', or \0)

#ifdef FLOATING_POINT
  //
  // We can decompose the printed representation of floating
  // point numbers into several parts, some of which may be empty:
  //
  // [+|-| ] [0x|0X] MMM . NNN [e|E|p|P] [+|-] ZZ
  //    A       B     ---C---      D       E   F
  //
  // A: 'sign' holds this value if present; '\0' otherwise
  // B: ox[1] holds the 'x' or 'X'; '\0' if not hexadecimal
  // C: cp points to the string MMMNNN.  Leading and trailing
  //    zeros are not in the string and must be added.
  // D: expchar holds this character; '\0' if no exponent, e.g. %f
  // F: at least two digits for decimal, at least one digit for hex
  //
  char *decimal_point = localeconv()->decimal_point;
  int signflag;     // true if float is negative
  union             // floating point arguments %[aAeEfFgG]
  {
    double dbl;
    long double ldbl;
  } fparg;
  int expt;         // integer value of exponent
  char expchar = 0; // exponent character: [eEpP\0]
  char *dtoaend;    // pointer to end of converted digits
  int expsize = 0;  // character count for expstr
  int lead = 0;     // sig figs before decimal or group sep
  int ndig = 0;     // actual number of digits returned by dtoa
  // Set this to the maximum number of digits in an exponent.
  // This is a large overestimate.
#define MAXEXPDIG 32
  char expstr[MAXEXPDIG+2]; // buffer for exponent string: e+ZZZ
  char *dtoaresult = NULL;
#endif

  uintmax_t _umax;              // integer arguments %[diouxX]
  enum { OCT, DEC, HEX } base;  // base for %[diouxX] conversion
  int dprec;         // a copy of prec if %[diouxX], 0 otherwise
  int realsz;        // field size expanded by dprec
  int size;          // size of converted field or string
  const char *xdigs; // digits for %[xX] conversion

  const int NIOV = 8;
  struct __suio uio;      // output information: summary
  struct __siov iov[NIOV];// ... and individual io vectors
  char buf[BUF];          // buffer with space for digits of uintmax_t
  char ox[2];             // space for 0x; ox[1] is either x, X, or \0
  union arg *argtable;    // args, built due to positional arg
  union arg statargtable[STATIC_ARG_TBL_SIZE];
  size_t argtablesiz;
  int nextarg;       // 1-based argument index
  va_list orgap;     // original argument pointer
  unsigned vargc = CInfo.vargc;           // number of variable arguments
  void **wl      = CInfo.whitelist;       // whitelist of pointer arguments
  pointer_info *p;   // handy pointer_info structure
  bool oob = false;  // whether too many arguments have been accessed
  char wc;

  //
  // Choose PADSIZE to trade efficiency vs. size.  If larger printf
  // fields occur frequently, increase PADSIZE and make the initialisers
  // below longer.
  //
#define PADSIZE 16    // pad chunk size
  //
  // These are declared as 8 bit integers because g++ complains about
  // unterminated strings if declared as char arrays.
  //
  static int8_t blanks[PADSIZE] =
   {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};
  static int8_t zeroes[PADSIZE] =
   {'0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0'};

  static const char xdigs_lower[] = "0123456789abcdef";
  static const char xdigs_upper[] = "0123456789ABCDEF";

  xdigs = 0;

  //
  // Printing macros
  //
  // BEWARE, these `goto error' on error, and PAD uses `n'.
  //

//
// Add the first 'len' bytes of 'ptr' to the print queue.
//
#define PRINT(ptr, len) do { \
  iovp->iov_base = (char *) (ptr); \
  iovp->iov_len = (len); \
  uio.uio_resid += (len); \
  iovp++; \
  if (++uio.uio_iovcnt >= NIOV) { \
    if (do_output(P, uio)) \
      goto error; \
    iovp = iov; \
  } \
} while (0)

//
// Output 'howmany' bytes from a pad chunk 'with'.
//
#define PAD(howmany, with) do { \
  if ((n = (howmany)) > 0) { \
    while (n > PADSIZE) { \
      PRINT(with, PADSIZE); \
      n -= PADSIZE; \
    } \
    PRINT(with, n); \
  } \
} while (0)

//
// Output a string of exactly len bytes, consisting of the string found in the
// range [p, ep), right-padded by the pad characters in 'with', if necessary.
//
#define PRINTANDPAD(p, ep, len, with) do {  \
  n2 = (ep) - (p);            \
  if (n2 > (len))             \
    n2 = (len);               \
  if (n2 > 0)                 \
    PRINT((p), n2);           \
  PAD((len) - (n2 > 0 ? n2 : 0), (with)); \
} while(0)

//
// Flush any output buffers yet to be printed.
//
#define FLUSH() do { \
  if (uio.uio_resid && do_output(P, uio)) \
    goto error; \
  uio.uio_iovcnt = 0; \
  iovp = iov; \
} while (0)


  //
  // Integer retrieval macros
  //
  // To extend shorts properly, we need both signed and unsigned
  // argument extraction methods.
  //

//
// Retrieve a signed argument.
//
#define SARG() ((intmax_t) \
  ((flags&MAXINT   ? GETARG(intmax_t)   : \
    flags&LLONGINT ? GETARG(long long)  : \
    flags&LONGINT  ? GETARG(long)       : \
    flags&PTRINT   ? GETARG(ptrdiff_t)  : \
    flags&SIZEINT  ? GETARG(ssize_t)    : \
    flags&SHORTINT ? (short)GETARG(int) : \
    flags&CHARINT  ? (signed char)GETARG(int) : \
    GETARG(int))))

//
// Retrieve an unsigned argument.
//
#define UARG() ((uintmax_t) \
  ((flags&MAXINT   ? GETARG(uintmax_t)            : \
    flags&LLONGINT ? GETARG(unsigned long long)   : \
    flags&LONGINT  ? GETARG(unsigned long)        : \
    flags&PTRINT   ? (uintptr_t)GETARG(ptrdiff_t) : /* XXX */ \
    flags&SIZEINT  ? GETARG(size_t)               : \
    flags&SHORTINT ? (unsigned short)GETARG(int)  : \
    flags&CHARINT  ? (unsigned char)GETARG(int)   : \
    GETARG(unsigned int))))

//
// Append a digit to a value and check for overflow.
//
#define APPEND_DIGIT(val, dig) do { \
  if ((val) > INT_MAX / 10) \
    goto overflow; \
  (val) *= 10; \
  if ((val) > INT_MAX - to_digit((dig))) \
    goto overflow; \
  (val) += to_digit((dig)); \
} while (0)


  //
  // Macros for getting arguments
  //

//
// Get * arguments, including the form *nn$, into val.  Preserve the nextarg
// that the argument can be gotten once the type is determined.
//
#define GETASTER(val) \
  n2 = 0; \
  cp = fmt; \
  while (is_digit(*cp)) { \
    APPEND_DIGIT(n2, *cp); \
    cp++; \
  } \
  if (*cp == '$') { \
    int hold = nextarg; \
    if (argtable == NULL) { \
      argtable = statargtable; \
      __find_arguments(fmt0, orgap, &argtable, &argtablesiz, vargc); \
    } \
    nextarg = n2; \
    val = GETARG(int); \
    nextarg = hold; \
    fmt = ++cp; \
  } else { \
    val = GETARG(int); \
  }

//
// Get the actual argument indexed by nextarg. If the argument table is
// built, use it to get the argument.  If its not, get the next
// argument (and arguments must be gotten sequentially).
//
#define GETARG(type)                                           \
  ((argtable != NULL) ?                                        \
    (varg_check(nextarg, vargc, &oob),                         \
      oob ? (type) 0 :  *((type*)(&argtable[nextarg++]))) :    \
    (nextarg++, va_sarg(&oob, nextarg - 1, vargc, ap, type)))

//
// Get the pointer_info structure indexed by nextarg.
// This will report an error if the argument is not found in the whitelist.
//
#define GETPTRINFOARG(wl) \
  (check_whitelist(wl, GETARG(void *)))

//
// Get the pointer argument indexed by nextarg.
// This is either the inner pointer value found in a pointer_info structure, or
// the value of the actual parameter if the parameter is not found in the
// whitelist.
//
#define GETPTRARG(type, wl) \
  ((type *) getptrarg(wl, GETARG(void *)))

//
// Write the current number of bytes that have been written into the next
// argument, which should be a pointer of type 'type'.
//
#define WRITECOUNTAS(type)             \
  do {                                 \
    p = GETPTRINFOARG(wl);             \
    if (p != 0) {                      \
      write_check(p, sizeof(type)) &&  \
      (*(type *)(p->ptr) = ret);       \
    } else                             \
      c_library_error("va_arg");       \
  } while (0)

  fmt = (char *)fmt0;
  argtable = NULL;
  nextarg = 1;
  va_copy(orgap, ap);
  uio.uio_iov = iovp = iov;
  uio.uio_resid = 0;
  uio.uio_iovcnt = 0;
  ret = 0;

  //
  // Scan the format for conversions (`%' character).
  //
  for (;;) {
    cp = fmt;
    while ((wc = *fmt) != 0) {
      fmt++;
      if (wc == '%') {
        fmt--;
        break;
      }
    }
    if (fmt != cp) {
      ptrdiff_t m = fmt - cp;
      if (m < 0 || m > INT_MAX - ret)
        goto overflow;
      PRINT(cp, m);
      ret += m;
    }
    if (wc == 0)
      goto done;
    fmt++;    // skip over '%'

    flags = 0;
    dprec = 0;
    width = 0;
    prec = -1;
    sign = '\0';
    ox[1] = '\0';

    //
    // This is the main % directive parsing section.
    //
rflag:    ch = *fmt++;
reswitch: switch (ch) {
    //
    // This section parses flags, precision, and field width values.
    //
    case ' ':
      //
      // ``If the space and + flags both appear, the space
      // flag will be ignored.''
      //  -- ANSI X3J11
      //
      if (!sign)
        sign = ' ';
      goto rflag;
    case '#':
      flags |= ALT;
      goto rflag;
    case '\'':
      // grouping not implemented
      goto rflag;
    //
    // Parse field width given in an argument
    //
    case '*':
      //
      // ``A negative field width argument is taken as a
      // - flag followed by a positive field width.''
      //  -- ANSI X3J11
      // They don't exclude field widths read from args.
      //
      GETASTER(width);
      if (width >= 0)
        goto rflag;
      if (width == INT_MIN)
        goto overflow;
      width = -width;
      // FALLTHROUGH
    case '-':
      flags |= LADJUST;
      goto rflag;
    case '+':
      sign = '+';
      goto rflag;
    //
    // Parse precision
    //
    case '.':
      if ((ch = *fmt++) == '*')
      {
        GETASTER(n);
        prec = n < 0 ? -1 : n;
        goto rflag;
      }
      n = 0;
      while (is_digit(ch))
      {
        APPEND_DIGIT(n, ch);
        ch = *fmt++;
      }
      //
      // If the number ends with a $, this indicates a positional argument.
      // So parse the whole format string for positional arguments.
      //
      if (ch == '$') {
        nextarg = n;
        if (argtable == NULL) {
          argtable = statargtable;
          __find_arguments(fmt0, orgap,
              &argtable, &argtablesiz, vargc);
        }
        goto rflag;
      }
      prec = n;
      goto reswitch;
    case '0':
      //
      // ``Note that 0 is taken as a flag, not as the
      // beginning of a field width.''
      //  -- ANSI X3J11
      //
      flags |= ZEROPAD;
      goto rflag;
    //
    // Reading a number here indicates either a field width or a positional
    // argument. They are distinguished since positional arguments end in $.
    //
    case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      n = 0;
      do {
        APPEND_DIGIT(n, ch);
        ch = *fmt++;
      } while (is_digit(ch));
      //
      // If the number ends with a $, this indicates a positional argument.
      // So parse the whole format string for positional arguments.
      //
      if (ch == '$') {
        nextarg = n;
        if (argtable == NULL) {
          argtable = statargtable;
          __find_arguments(fmt0, orgap,
              &argtable, &argtablesiz, vargc);
        }
        goto rflag;
      }
      width = n;
      goto reswitch;
    //
    // This section parses length modifiers.
    //

#ifdef FLOATING_POINT
    case 'L':
      flags |= LONGDBL;
      goto rflag;
#endif

    case 'h':
      if (*fmt == 'h') {
        fmt++;
        flags |= CHARINT;
      } else {
        flags |= SHORTINT;
      }
      goto rflag;
    case 'j':
      flags |= MAXINT;
      goto rflag;
    case 'l':
      if (*fmt == 'l') {
        fmt++;
        flags |= LLONGINT;
      } else {
        flags |= LONGINT;
      }
      goto rflag;
    case 'q':
      flags |= LLONGINT;
      goto rflag;
    case 't':
      flags |= PTRINT;
      goto rflag;
    case 'z':
      flags |= SIZEINT;
      goto rflag;
    case 'c':
      *(cp = buf) = GETARG(int);
      size = 1;
      sign = '\0';
      break;
    //
    // This section parses conversion specifiers and gives instructions on how
    // to print the output.
    //
    case 'D':
      flags |= LONGINT;
      // FALLTHROUGH
    case 'd':
    case 'i':
      _umax = SARG();
      if ((intmax_t)_umax < 0) {
        _umax = -_umax;
        sign = '-';
      }
      base = DEC;
      goto number;

#ifdef FLOATING_POINT
    case 'a':
    case 'A':
      if (ch == 'a') {
        ox[1] = 'x';
        xdigs = xdigs_lower;
        expchar = 'p';
      } else {
        ox[1] = 'X';
        xdigs = xdigs_upper;
        expchar = 'P';
      }
      if (prec >= 0)
        prec++;
      if (dtoaresult)
        __freedtoa(dtoaresult);
      if (flags & LONGDBL) {
        fparg.ldbl = GETARG(long double);
        dtoaresult = cp =
            __hldtoa(fparg.ldbl, xdigs, prec,
            &expt, &signflag, &dtoaend);
        if (dtoaresult == NULL) {
          errno = ENOMEM;
          goto error;
        }
      } else {
        fparg.dbl = GETARG(double);
        dtoaresult = cp =
            __hdtoa(fparg.dbl, xdigs, prec,
            &expt, &signflag, &dtoaend);
        if (dtoaresult == NULL) {
          errno = ENOMEM;
          goto error;
        }
      }
      if (prec < 0)
        prec = dtoaend - cp;
      if (expt == INT_MAX)
        ox[1] = '\0';
      goto fp_common;
    //
    // This is the form [-]d.ddde[+/-]dd.
    // The number of digits after the decimal point is the precision.
    //
    case 'e':
    case 'E':
      expchar = ch;
      if (prec < 0) // account for digit before decpt
        prec = DEFPREC + 1;
      else
        prec++;
      goto fp_begin;
    //
    // This is the form ddd.dddd. The number of digits after the decimal point
    // is the precision.
    //
    case 'f':
    case 'F':
      expchar = '\0';
      goto fp_begin;
    //
    // 'e' or 'f' style, depending on the precision and exponent.
    //
    case 'g':
    case 'G':
      expchar = ch - ('g' - 'e');
      if (prec == 0)
        prec = 1;
fp_begin:
      if (prec < 0)
        prec = DEFPREC;
      if (dtoaresult)
        __freedtoa(dtoaresult);
      if (flags & LONGDBL) {
        fparg.ldbl = GETARG(long double);
        dtoaresult = cp =
            __ldtoa(&fparg.ldbl, expchar ? 2 : 3, prec,
            &expt, &signflag, &dtoaend);
        if (dtoaresult == NULL) {
          errno = ENOMEM;
          goto error;
        }
      }
      else
      {
        fparg.dbl = GETARG(double);
        //
        // There is very sparse documentation for this function call. This
        // comment attempts to explain how it is used here.
        //
        // char *
        // dtoa(double v, int mode, int prec, int *expt, int *sign, char **end);
        //
        // The dtoa function returns a (char *) pointer of internally allocated
        // memory which is the double value converted into a decimal string.
        // The value should be free'd with freedtoa.
        //
        // In mode 2, which is needed for the form [-]d.ddde[+/-]dd, the
        // function returns a string with 'prec' significant digits.
        //
        // In this mode if XXXXX is the converted string, the number is equal to
        // 0.XXXXX * (10 ^ *expt).
        //
        // In mode 3, the function returns a string which is the decimal
        // representation going as far as 'prec' digits beyond the decimal
        // point, with trailing zeroes suppressed.
        //
        // In this mode *expt can be interpreted as the position of the
        // decimal point.
        //
        // Other arguments:
        // - signflag is set to true when the number is negative, 0 if not
        // - dtoaend is set to point to the end of the returned string.
        //
        dtoaresult = cp =
          __dtoa(fparg.dbl, expchar ? 2 : 3, prec, &expt, &signflag, &dtoaend);
        if (dtoaresult == NULL)
        {
          errno = ENOMEM;
          goto error;
        }
        //
        // If the number was bad, expt is set to 9999.
        //
        if (expt == 9999)
          expt = INT_MAX;
      }
fp_common:
      if (signflag)
        sign = '-';
      if (expt == INT_MAX)
      {  // inf or nan
        if (*cp == 'N')
        {
          cp = (char *) ((ch >= 'a') ? "nan" : "NAN");
          sign = '\0';
        }
        else
          cp = (char *) ((ch >= 'a') ? "inf" : "INF");
        size = 3;
        flags &= ~ZEROPAD;
        break;
      }
      flags |= FPT;
      ndig = dtoaend - cp;
      if (ch == 'g' || ch == 'G')
      {
        if (expt > -4 && expt <= prec)
        {
          // Make %[gG] smell like %[fF]
          expchar = '\0';
          if (flags & ALT)
            prec -= expt;
          else
            prec = ndig - expt;
          if (prec < 0)
            prec = 0;
        }
        else
        {
          // Make %[gG] smell like %[eE], but trim trailing zeroes if no # flag.
          if (!(flags & ALT))
            prec = ndig;
        }
      }
      if (expchar)
      {
        expsize = exponent(expstr, expt - 1, expchar);
        size = expsize + prec;
        if (prec > 1 || flags & ALT)
          ++size;
      }
      else
      {
        // space for digits before decimal point
        if (expt > 0)
          size = expt;
        else  // "0"
          size = 1;
        // space for decimal pt and following digits
        if (prec || flags & ALT)
          size += prec + 1;
        lead = expt;
      }
      break;
#endif // FLOATING_POINT

    case 'n':
      if (flags & LLONGINT)
        WRITECOUNTAS(long long);
      else if (flags & LONGINT)
        WRITECOUNTAS(long);
      else if (flags & SHORTINT)
        WRITECOUNTAS(short);
      else if (flags & CHARINT)
        WRITECOUNTAS(signed char);
      else if (flags & PTRINT)
        WRITECOUNTAS(ptrdiff_t);
      else if (flags & SIZEINT)
        WRITECOUNTAS(ssize_t);
      else if (flags & MAXINT)
        WRITECOUNTAS(intmax_t);
      else
        WRITECOUNTAS(int);
      continue; // no output
    case 'O':
      flags |= LONGINT;
      // FALLTHROUGH
    case 'o':
      _umax = UARG();
      base = OCT;
      goto nosign;
    case 'p':
      //
      // ``The argument shall be a pointer to void.  The
      // value of the pointer is converted to a sequence
      // of printable characters, in an implementation-
      // defined manner.''
      //  -- ANSI X3J11
      //
      _umax = (u_long) GETPTRARG(void, wl);
      base = HEX;
      xdigs = xdigs_lower;
      ox[1] = 'x';
      goto nosign;
    case 's':
      sign = '\0';
      //
      // Get the pointer_info structure associated with the current argument.
      //
      p = GETPTRINFOARG(wl);
      //
      // If the structure is NULL, then the current argument was not a pointer.
      //
      if (p == 0)
      {
        cp = (char *) "(not a string)";
        size = (int) strlen(cp);
        break;
      }
      //
      // Handle printing null pointers.
      //
      else if ((cp = (char *) p->ptr) == 0)
      {
        cp = (char *) "(null)";
        size = (int) strlen(cp);
        break;
      }
      //
      // Otherwise load the object boundaries.
      //
      find_object(p);
      //
      // A nonnegative precision means that we should print at most prec bytes
      // from this string.
      //
      if (prec >= 0)
      {
        //
        // If we have the boundaries of the object, then we should print no
        // further than min(precision, object size).
        //
        size_t maxbytes = (p->flags & HAVEBOUNDS) ? 
          min((size_t) prec, object_len(p)) :
          (size_t) prec;

        char *r = (char *) memchr(cp, 0, maxbytes);

        if (r)
          size = r - cp;
        else if ((unsigned)prec <= maxbytes)
          size = prec;
        else
        {
          size = prec;
          cerr << "Reading string out of bounds!" << endl;
          out_of_bounds_error(p, maxbytes);
        }
      }
      else
      {
        size_t len;
        //
        // If we have the boundaries of the object, then we should print
        // no further than the object size.
        //
        if (p->flags & HAVEBOUNDS)
        {
          size_t maxbytes = object_len(p);
          len = _strnlen(cp, maxbytes);
          if (len == maxbytes)
          {
            cerr << "Reading string out of bounds!" << endl;
            out_of_bounds_error(p, maxbytes);
          }
        }
        else
          len = strlen(cp);

        if (len > INT_MAX)
          goto overflow;

        size = (int)len;
      }
      break;
    case 'U':
      flags |= LONGINT;
      // FALLTHROUGH
    case 'u':
      _umax = UARG();
      base = DEC;
      goto nosign;
    case 'X':
      xdigs = xdigs_upper;
      goto hex;
    case 'x':
      xdigs = xdigs_lower;
hex:      _umax = UARG();
      base = HEX;
      // leading 0x/X only if non-zero
      if (flags & ALT && _umax != 0)
        ox[1] = ch;

      // unsigned conversions
nosign:     sign = '\0';
      //
      // ``... diouXx conversions ... if a precision is
      // specified, the 0 flag will be ignored.''
      //  -- ANSI X3J11
      //
number:     if ((dprec = prec) >= 0)
        flags &= ~ZEROPAD;

      //
      // ``The result of converting a zero value with an
      // explicit precision of zero is no characters.''
      //  -- ANSI X3J11
      //
      cp = buf + BUF;
      if (_umax != 0 || prec != 0)
      {
        //
        // Unsigned mod is hard, and unsigned mod
        // by a constant is easier than that by
        // a variable; hence this switch.
        //
        switch (base)
        {
        case OCT:
          do
          {
            *--cp = to_char(_umax & 7);
            _umax >>= 3;
          } while (_umax);
          // handle octal leading 0
          if (flags & ALT && *cp != '0')
            *--cp = '0';
          break;

        case DEC:
          // many numbers are 1 digit
          while (_umax >= 10)
          {
            *--cp = to_char(_umax % 10);
            _umax /= 10;
          }
          *--cp = to_char(_umax);
          break;

        case HEX:
          do
          {
            *--cp = xdigs[_umax & 15];
            _umax >>= 4;
          } while (_umax);
          break;

        default:
          cp = (char *) "bug in vfprintf: bad base";
          size = strlen(cp);
          goto skipsize;
        }
      }
      size = buf + BUF - cp;
      if (size > BUF) // should never happen
        abort();
    skipsize:
      break;
    default:  // "%?" prints ?, unless ? is NUL
      //
      // syslog() includes a %m flag which prints sterror(errno).
      //
      if (ch == 'm' && options & USE_M_DIRECTIVE)
      {
        cp = (char *) strerror(errno);
        size = strlen(cp);
        break;
      }
      else if (ch == '\0')
        goto done;
      // pretend it was %c with argument ch
      cp = buf;
      *cp = ch;
      size = 1;
      sign = '\0';
      break;
    }

    //
    // All reasonable formats wind up here.  At this point, `cp'
    // points to a string which (if not flags&LADJUST) should be
    // padded out to `width' places.  If flags&ZEROPAD, it should
    // first be prefixed by any sign or other prefix; otherwise,
    // it should be blank padded before the prefix is emitted.
    // After any left-hand padding and prefixing, emit zeroes
    // required by a decimal %[diouxX] precision, then print the
    // string proper, then emit zeroes required by any leftover
    // floating precision; finally, if LADJUST, pad with blanks.
    //
    // Compute actual size, so we know how much to pad.
    // size excludes decimal prec; realsz includes it.
    //

    //
    // If we've accessed an out of bounds argument, stop printing.
    //
    if (oob)
      goto error;

    realsz = dprec > size ? dprec : size;
    if (sign)
      realsz++;
    if (ox[1])
      realsz+= 2;

    // right-adjusting blank padding
    if ((flags & (LADJUST|ZEROPAD)) == 0)
      PAD(width - realsz, blanks);

    // prefix
    if (sign)
      PRINT(&sign, 1);
    if (ox[1])
    {  // ox[1] is either x, X, or \0
      ox[0] = '0';
      PRINT(ox, 2);
    }

    // right-adjusting zero padding
    if ((flags & (LADJUST|ZEROPAD)) == ZEROPAD)
      PAD(width - realsz, zeroes);

    // leading zeroes from decimal precision
    PAD(dprec - size, zeroes);

    // the string or number proper

#ifdef FLOATING_POINT
    if ((flags & FPT) == 0)
    {
      PRINT(cp, size);
    }
    else
    {  // glue together f_p fragments
      if (!expchar)
      { // %[fF] or sufficiently short %[gG]
        if (expt <= 0)
        {
          PRINT(zeroes, 1);
          if (prec || flags & ALT)
            PRINT(decimal_point, 1);
          PAD(-expt, zeroes);
          // already handled initial 0's
          prec += expt;
        }
        else
        {
          PRINTANDPAD(cp, dtoaend, lead, zeroes);
          cp += lead;
          if (prec || flags & ALT)
            PRINT(decimal_point, 1);
        }
        PRINTANDPAD(cp, dtoaend, prec, zeroes);
      }
      else
      {  // %[eE] or sufficiently long %[gG]
        if (prec > 1 || flags & ALT)
        {
          buf[0] = *cp++;
          buf[1] = *decimal_point;
          PRINT(buf, 2);
          PRINT(cp, ndig-1);
          PAD(prec - ndig, zeroes);
        } else
        { // XeYYY
          PRINT(cp, 1);
        }
        PRINT(expstr, expsize);
      }
    }
#else
    PRINT(cp, size);
#endif

    // left-adjusting padding (always blank)
    if (flags & LADJUST)
      PAD(width - realsz, blanks);

    // finally, adjust ret
    if (width < realsz)
      width = realsz;
    if (width > INT_MAX - ret)
      goto overflow;
    ret += width;

    FLUSH();  // copy out the I/O vectors
  }
done:
  FLUSH();
error:
  va_end(orgap);
  //if (__sferror(fp))
  //  ret = -1;
  goto finish;

overflow:
  errno = ENOMEM;
  ret = -1;

finish:

#ifdef FLOATING_POINT
  if (dtoaresult)
    __freedtoa(dtoaresult);
#endif

  if (argtable != NULL && argtable != statargtable)
  {
    munmap(argtable, argtablesiz);
    argtable = NULL;
  }
  return (ret);
}

//
// Type ids for argument type table.
//
#define T_UNUSED  0
#define T_SHORT   1
#define T_U_SHORT 2
#define TP_SHORT  3
#define T_INT     4
#define T_U_INT   5
#define TP_INT    6
#define T_LONG    7
#define T_U_LONG  8
#define TP_LONG   9
#define T_LLONG   10
#define T_U_LLONG 11
#define TP_LLONG  12
#define T_DOUBLE  13
#define T_LONG_DOUBLE 14
#define TP_CHAR   15
#define TP_VOID   16
#define T_PTRINT  17
#define TP_PTRINT 18
#define T_SIZEINT 19
#define T_SSIZEINT  20
#define TP_SSIZEINT 21
#define T_MAXINT  22
#define T_MAXUINT 23
#define TP_MAXINT 24
#define T_CHAR    25
#define T_U_CHAR  26

//
// Find all arguments when a positional parameter is encountered.  Returns a
// table, indexed by argument number, of pointers to each arguments.  The
// initial argument table should be an array of STATIC_ARG_TBL_SIZE entries.
// It will be replaced with a mmap-ed one if it overflows (malloc cannot be
// used since we are attempting to make snprintf thread safe, and alloca is
// problematic since we have nested functions..)
//
static int
__find_arguments(const char *fmt0, va_list ap, union arg **argtable,
    size_t *argtablesiz, unsigned vargc)
{
  char *fmt;     // format string
  int ch;        // character from fmt
  int n, n2;     // handy integer (short term usage)
  char *cp;      // handy char pointer (short term usage)
  int flags;     // flags as above
  unsigned char *typetable; // table of types
  unsigned char stattypetable[STATIC_ARG_TBL_SIZE];
  int tablesize; // current size of type table
  int tablemax;  // largest used index in table
  int nextarg;   // 1-based argument index
  int ret = 0;   // return value
  char wc;

//
// Add an argument type to the table, expanding if necessary.
//
#define ADDTYPE(type) \
  ((nextarg >= tablesize) ? \
    __grow_type_table(&typetable, &tablesize) : 0, \
  (nextarg > tablemax) ? tablemax = nextarg : 0, \
  typetable[nextarg++] = type)

#define ADDSARG() \
      ((flags&MAXINT) ? ADDTYPE(T_MAXINT) : \
      ((flags&PTRINT) ? ADDTYPE(T_PTRINT) : \
      ((flags&SIZEINT) ? ADDTYPE(T_SSIZEINT) : \
      ((flags&LLONGINT) ? ADDTYPE(T_LLONG) : \
      ((flags&LONGINT) ? ADDTYPE(T_LONG) : \
      ((flags&SHORTINT) ? ADDTYPE(T_SHORT) : \
      ((flags&CHARINT) ? ADDTYPE(T_CHAR) : ADDTYPE(T_INT))))))))

#define ADDUARG() \
      ((flags&MAXINT) ? ADDTYPE(T_MAXUINT) : \
      ((flags&PTRINT) ? ADDTYPE(T_PTRINT) : \
      ((flags&SIZEINT) ? ADDTYPE(T_SIZEINT) : \
      ((flags&LLONGINT) ? ADDTYPE(T_U_LLONG) : \
      ((flags&LONGINT) ? ADDTYPE(T_U_LONG) : \
      ((flags&SHORTINT) ? ADDTYPE(T_U_SHORT) : \
      ((flags&CHARINT) ? ADDTYPE(T_U_CHAR) : ADDTYPE(T_U_INT))))))))

//
// Add * arguments to the type array.
//
#define ADDASTER() \
  n2 = 0; \
  cp = fmt; \
  while (is_digit(*cp)) { \
    APPEND_DIGIT(n2, *cp); \
    cp++; \
  } \
  if (*cp == '$') { \
    int hold = nextarg; \
    nextarg = n2; \
    ADDTYPE(T_INT); \
    nextarg = hold; \
    fmt = ++cp; \
  } else { \
    ADDTYPE(T_INT); \
  }

  fmt = (char *)fmt0;
  typetable = stattypetable;
  tablesize = STATIC_ARG_TBL_SIZE;
  tablemax = 0;
  nextarg = 1;
  memset(typetable, T_UNUSED, STATIC_ARG_TBL_SIZE);

  //
  // Scan the format for conversions (`%' character).
  //
  for (;;) {
    cp = fmt;
    while ((wc = *fmt) != 0) {
      fmt++;
      if (wc == '%') {
        fmt--;
        break;
      }
    }
    if (wc == 0)
      goto done;
    fmt++;    // skip over '%'

    flags = 0;

rflag:    ch = *fmt++;
reswitch: switch (ch) {
    case ' ':
    case '#':
    case '\'':
      goto rflag;
    case '*':
      ADDASTER();
      goto rflag;
    case '-':
    case '+':
      goto rflag;
    case '.':
      if ((ch = *fmt++) == '*') {
        ADDASTER();
        goto rflag;
      }
      while (is_digit(ch)) {
        ch = *fmt++;
      }
      goto reswitch;
    case '0':
      goto rflag;
    case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      n = 0;
      do {
        APPEND_DIGIT(n ,ch);
        ch = *fmt++;
      } while (is_digit(ch));
      if (ch == '$') {
        nextarg = n;
        goto rflag;
      }
      goto reswitch;

#ifdef FLOATING_POINT
    case 'L':
      flags |= LONGDBL;
      goto rflag;
#endif

    case 'h':
      if (*fmt == 'h') {
        fmt++;
        flags |= CHARINT;
      } else {
        flags |= SHORTINT;
      }
      goto rflag;
    case 'l':
      if (*fmt == 'l') {
        fmt++;
        flags |= LLONGINT;
      } else {
        flags |= LONGINT;
      }
      goto rflag;
    case 'q':
      flags |= LLONGINT;
      goto rflag;
    case 't':
      flags |= PTRINT;
      goto rflag;
    case 'z':
      flags |= SIZEINT;
      goto rflag;
    case 'c':
      ADDTYPE(T_INT);
      break;
    case 'D':
      flags |= LONGINT;
      // FALLTHROUGH
    case 'd':
    case 'i':
      ADDSARG();
      break;

#ifdef FLOATING_POINT
    case 'a':
    case 'A':
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
      if (flags & LONGDBL)
        ADDTYPE(T_LONG_DOUBLE);
      else
        ADDTYPE(T_DOUBLE);
      break;
#endif // FLOATING_POINT

    case 'n':
      if (flags & LLONGINT)
        ADDTYPE(TP_LLONG);
      else if (flags & LONGINT)
        ADDTYPE(TP_LONG);
      else if (flags & SHORTINT)
        ADDTYPE(TP_SHORT);
      else if (flags & PTRINT)
        ADDTYPE(TP_PTRINT);
      else if (flags & SIZEINT)
        ADDTYPE(TP_SSIZEINT);
      else if (flags & MAXINT)
        ADDTYPE(TP_MAXINT);
      else
        ADDTYPE(TP_INT);
      continue; // no output
    case 'O':
      flags |= LONGINT;
      // FALLTHROUGH
    case 'o':
      ADDUARG();
      break;
    case 'p':
      ADDTYPE(TP_VOID);
      break;
    case 's':
      ADDTYPE(TP_CHAR);
      break;
    case 'U':
      flags |= LONGINT;
      // FALLTHROUGH
    case 'u':
    case 'X':
    case 'x':
      ADDUARG();
      break;
    default:  // "%?" prints ?, unless ? is NUL
      if (ch == '\0')
        goto done;
      break;
    }
  }
done:
  //
  // Build the argument table.
  //
  if (tablemax >= STATIC_ARG_TBL_SIZE)
  {
    *argtablesiz = sizeof(union arg) * (tablemax + 1);
    *argtable = (union arg *) mmap(NULL, *argtablesiz,
        PROT_WRITE|PROT_READ, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    if (*argtable == MAP_FAILED)
      return (-1);
  }

#if 0
  /* XXX is this required? */
  (*argtable)[0].intarg = 0;
#endif

  //
  // Build the argument table based on the entries of the type table.
  //
  for (n = 1; n <= min((int) vargc, tablemax); n++)
  {
    switch (typetable[n]) {
    case T_UNUSED:
    case T_CHAR:
    case T_U_CHAR:
    case T_SHORT:
    case T_U_SHORT:
    case T_INT:
      (*argtable)[n].intarg = va_arg(ap, int);
      break;
    case TP_SHORT:
    case TP_INT:
    case TP_LONG:
    case TP_LLONG:
    case TP_CHAR:
    case TP_VOID:
    case TP_PTRINT:
    case TP_SSIZEINT:
    case TP_MAXINT:
      (*argtable)[n].pvoidarg = va_arg(ap, void *);
      break;
    case T_U_INT:
      (*argtable)[n].uintarg = va_arg(ap, unsigned int);
      break;
    case T_LONG:
      (*argtable)[n].longarg = va_arg(ap, long);
      break;
    case T_U_LONG:
      (*argtable)[n].ulongarg = va_arg(ap, unsigned long);
      break;
    case T_LLONG:
      (*argtable)[n].longlongarg = va_arg(ap, long long);
      break;
    case T_U_LLONG:
      (*argtable)[n].ulonglongarg = va_arg(ap, unsigned long long);
      break;

#ifdef FLOATING_POINT
    case T_DOUBLE:
      (*argtable)[n].doublearg = va_arg(ap, double);
      break;
    case T_LONG_DOUBLE:
      (*argtable)[n].longdoublearg = va_arg(ap, long double);
      break;
#endif

    case T_PTRINT:
      (*argtable)[n].ptrdiffarg = va_arg(ap, ptrdiff_t);
      break;
    case T_SIZEINT:
      (*argtable)[n].sizearg = va_arg(ap, size_t);
      break;
    case T_SSIZEINT:
      (*argtable)[n].ssizearg = va_arg(ap, ssize_t);
      break;
    }
  }
  goto finish;

overflow:
  errno = ENOMEM;
  ret = -1;

finish:
  if (typetable != NULL && typetable != stattypetable) {
    munmap(typetable, *argtablesiz);
    typetable = NULL;
  }
  return (ret);
}

//
// Increase the size of the type table.
//
static int
__grow_type_table(unsigned char **typetable, int *tablesize)
{
  unsigned char *oldtable = *typetable;
  int newsize = *tablesize * 2;

  if (newsize < getpagesize())
    newsize = getpagesize();

  //
  // Allocate the new table with mmap().
  //
  if (*tablesize == STATIC_ARG_TBL_SIZE) {
    *typetable = (unsigned char *) mmap(NULL, newsize, PROT_WRITE|PROT_READ,
        MAP_ANON|MAP_PRIVATE, -1, 0);
    if (*typetable == MAP_FAILED)
      return (-1);
    bcopy(oldtable, *typetable, *tablesize);
  } else {
    unsigned char *nc = (unsigned char *) mmap(NULL, newsize,
        PROT_WRITE|PROT_READ, MAP_ANON|MAP_PRIVATE, -1, 0);
    if (nc == MAP_FAILED)
      return (-1);
    memmove(nc, *typetable, *tablesize);
    munmap(*typetable, *tablesize);
    *typetable = nc;
  }
  //
  // Fill the rest of the table with empty entries.
  //
  memset(*typetable + *tablesize, T_UNUSED, (newsize - *tablesize));

  *tablesize = newsize;
  return (0);
}

#ifdef FLOATING_POINT
//
// Convert the exponent into a string, of the form fNNN, where f is the format
// directive and NNN a digit string.
//
// Inputs:
//  p0    - the destination buffer
//  exp   - the value to convert
//  fmtch - the associated format directive
//
// Returns:
//  The length of the converted string
//
static int
exponent(char *p0, int exp, int fmtch)
{
  char *p, *t;
  char expbuf[MAXEXPDIG];

  p = p0;
  *p++ = fmtch;
  if (exp < 0)
  {
    exp = -exp;
    *p++ = '-';
  }
  else
    *p++ = '+';
  t = expbuf + MAXEXPDIG;
  if (exp > 9)
  {
    do *--t = to_char(exp % 10); while ((exp /= 10) > 9);
    *--t = to_char(exp);
    for (; t < expbuf + MAXEXPDIG; *p++ = *t++)
      /* nothing */;
  }
  else
  {
    //
    // Exponents for decimal floating point conversions
    // (%[eEgG]) must be at least two characters long,
    // whereas exponents for hexadecimal conversions can
    // be only one character long.
    //
    if (fmtch == 'e' || fmtch == 'E')
      *p++ = '0';
    *p++ = to_char(exp);
  }
  return (p - p0);
}
#endif // FLOATING_POINT


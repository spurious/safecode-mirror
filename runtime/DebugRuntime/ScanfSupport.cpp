//===- ScanfSupport.cpp -  Secure scanf() replacement          ------------===//
// 
//                       The SAFECode Compiler Project
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements a secure runtime replacement for scanf() and similar
// functions.
//
//===----------------------------------------------------------------------===//

//
// This code is derived from MINIX's doscan.c; original license follows:
//
// /cvsup/minix/src/lib/stdio/doscan.c,v 1.1.1.1 2005/04/21 14:56:35 beng Exp $
//
// Copyright (c) 1987,1997,2001 Prentice Hall
// All rights reserved.
//
// Redistribution and use of the MINIX operating system in source and
// binary forms, with or without modification, are permitted provided
// that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above
//      copyright notice, this list of conditions and the following
//      disclaimer in the documentation and/or other materials provided
//      with the distribution.
//
//    * Neither the name of Prentice Hall nor the names of the software
//      authors or contributors may be used to endorse or promote
//      products derived from this software without specific prior
//      written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS, AUTHORS, AND
// CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL PRENTICE HALL OR ANY AUTHORS OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
// BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include  <ctype.h>
#include  <inttypes.h>
#include  <limits.h>
#include  <stdio.h>
#include  <stdlib.h>
#include  <stdarg.h>
#include  <wchar.h>

#include  <set>

using std::set;

#include  "FormatStrings.h"

#ifdef __x86_64__
#define set_pointer(flags)  (flags |= FL_LONG)
#else
#define set_pointer(flags)        // nothing
#endif

// Maximum allowable size for an input number.
#define NUMLEN    512
#define NR_CHARS  256


//
// Flags describing how to process the input
//
#define FL_CHAR       0x0001    // hh length modifier
#define FL_SHORT      0x0002    // h  length modifier
#define FL_LLONG      0x0004    // ll length modifier
#define FL_LONG       0x0008    // l  length modifier
#define FL_LONGDOUBLE 0x0010    // L  length modifier
#define FL_INTMAX     0x0020    // j  length modifier
#define FL_SIZET      0x0040    // z  length modifier
#define FL_PTRDIFF    0x0080    // t  length modifier
#define FL_NOASSIGN   0x0100    // do not assign (* flag)
#define FL_WIDTHSPEC  0x0200    // field width specified

//
// _getc()
//
// Get the next character from the input.
// Returns EOF on reading error or end of file/string.
//
static inline int
_getc(input_parameter *i)
{
  //
  // Get the next character from the string.
  //
  if (i->InputKind == input_parameter::INPUT_FROM_STRING)
  {
    const char *string = i->Input.String.string;
    size_t &pos = i->Input.String.pos;
    if (string[pos] == '\0')
      return EOF;
    else
      return string[pos++];
  }
  //
  // Get the next character from the stream.
  //
  else // i->InputKind == input_parameter::INPUT_FROM_STREAM
  {
    FILE *file = i->Input.Stream.stream;
    char &lastch = i->Input.Stream.lastch;
    int ch;

    if ((ch = fgetc(file)) == EOF)
      return EOF;
    else
    {
      // Save the character we got in case it is pushed back via _ungetc().
      lastch = ch;
      return ch;
    }
  }
}

//
// _ungetc()
//
// 'Push back' the last character that was read from the input source.
// This function assumes at least one character has been read via _getc().
// This function should be called at most once between calls to _getc(),
// so that at most one character is pushed back at any given time.
//
static inline void
_ungetc(input_parameter *i)
{
  if (i->InputKind == input_parameter::INPUT_FROM_STRING)
    //
    // 'Push back' the string by just decrementing the position.
    //
    i->Input.String.pos--;
  else if (i->InputKind == input_parameter::INPUT_FROM_STREAM)
  {
    const char lastch = i->Input.Stream.lastch;
    //
    // Use ungetc() to push the last character back into the stream.
    // See the note over internal_scanf() about the portability of this
    // operation.
    //
    ungetc(lastch, i->Input.Stream.stream);
  }
}

//
// Check if the parameter has had an input failure.
// This is defined as EOF or a read error, according to the standard.
// For strings we check if the end of the string occurs.
//
// Returns: True if the parameter is said to have an input failure, false
// otherwise.
//
static inline bool
input_failure(input_parameter *i)
{
  if (i->InputKind == input_parameter::INPUT_FROM_STRING)
    return i->Input.String.string[i->Input.String.pos] == 0;
  else // i->InputKind == input_parameter::INPUT_FROM_STREAM
  {
    FILE *f = i->Input.Stream.stream;
    return ferror(f) || feof(f);
  }
}

//
// o_collect()
//
// Collect a number of characters which constitite an ordinal number.
// When the type is 'i', the base can be 8, 10, or 16, depending on the
// first 1 or 2 characters. This means that the base must be adjusted
// according to the format of the number. At the end of the function, base
// is then set to 0, so strtol() will get the right argument.
//
// Inputs:
//
//  c       - the first character to read as an input item
//  inp_buf - the buffer into which the number should be written
//  stream  - the input_parameter object which contains the rest of the
//            characters to be read as input items
//  type    - the type of the specifier associated with this conversion, one of
//            'i', 'p', 'x', 'X', 'd', 'o', or 'b'
//  width   - the maximum field width
//  basep   - A pointer to an integer. This value is written into with the
//            numerical base of the value in the buffer, suitable for a call to
//            strtol() or other function, as determined by this function.
//
// This function returns NULL if the input buffer was not filled with a valid
// integer that could be converted; and otherwise if the input buffer contains
// the digits of a valid integer, the function returns the last nonnul position
// of the buffer that was written.
//
// On success, the buffer is suited for a call to strtol() or other integer
// conversion function, with the base given in *basep.
// 
static char *
o_collect(int c,
          char *inp_buf,
          input_parameter *stream,
          char type,
          unsigned int width,
          int *basep)
{
  char *bufp = inp_buf;
  int base = 0;

  switch (type)
  {
    case 'i': // i means octal, decimal or hexadecimal
    case 'p':
    case 'x':
    case 'X': base = 16;  break;
    case 'd':
    case 'u': base = 10;  break;
    case 'o': base = 8; break;
    case 'b': base = 2; break;
  }

  //
  // Process any initial +/- sign.
  //
  if (c == '-' || c == '+')
  {
    *bufp++ = c;
    if (--width)
      c = _getc(stream);
    else
      return 0;  // An initial [+-] is not a valid number.
  }

  //
  // Determine whether an initial '0' means to process the number in
  // hexadecimal or octal, if we are given a choice between the two.
  //
  if (width && c == '0' && base == 16)
  {
    *bufp++ = c;
    if (--width)
      c = _getc(stream);
    if (c != 'x' && c != 'X')
    {
      if (type == 'i') base = 8;
    }
    else if (width)
    {
      *bufp++ = c;
      if (--width)
        c = _getc(stream);
    }
    else
      return 0; // Don't accept only an initial [+-]?0[xX] as a valid number.
  }
  else if (type == 'i')
    base = 10;

  //
  // Read as many digits as we can.
  //
  while (width)
  {
    if (    ((base == 10) && isdigit(c)             )
         || ((base == 16) && isxdigit(c)            )
         || ((base ==  8) && isdigit(c) && (c < '8'))
         || ((base ==  2) && isdigit(c) && (c < '2')))
    {
      *bufp++ = c;
      if (--width)
        c = _getc(stream);
    }
    else break;
  }

  // Push back any extra read characters that aren't part of an integer.
  if (width && c != EOF)
    _ungetc(stream);
  if (type == 'i')
    base = 0;
  *basep = base;
  *bufp = '\0';
  return bufp == inp_buf ? 0 : bufp - 1;
}

#ifdef FLOATING_POINT

#include "ScanfTables.h"

//
// f_collect()
//
// Read the longest valid floating point number prefix from the input buffer.
// Upon encountering an error, the function returns and leaves the character
// to have caused the error in the input stream.
//
// On error, the function returns NULL. On success, it returns a pointer to the
// last non-nul position in the input buffer.
//
char *
f_collect(int c, char *inp_buf, input_parameter *stream, unsigned int width)
{
  int   state = 1;   // The start state from the scanner
  int   firstiter = 1;
  char *buf = inp_buf;
  int   ch, nextch;
  int   accept;

  while (width && state > 0)
  {
    --width;
    //
    // Get the next character.
    //
    if (firstiter)
    {
      ch = c;
      firstiter = 0;
    }
    else
      ch = _getc(stream);
    //
    // Handle an 8 bit character or EOF character as if it were the end of the
    // buffer; which is denoted by 0.
    //
    if (ch == EOF || ch > 127)
      nextch = 0;
    else
      nextch = ch;
    state = yy_nxt[state][nextch];
    //
    // Advance to the next state and save the current character, if valid.
    //
    if (state > 0)
      *buf++ = nextch;
  }
  // Push back the next character if it was a valid character not part of the
  // valid input sequence.
  if (state <= 0 && ch != EOF)
    _ungetc(stream);
  //
  // Get information about the next action of the scanner.
  //
  accept = yy_accept[state < 0 ? -state : state];
  //
  // A value of 0 for accept indicates failure/that the scanner should revert
  // to the previous accepting state. Since this is not possible without
  // pushing back more than one character, we should just fail.
  //
  // A value of DEFAULT_RULE indicates to the scanner to echo the output
  // since it is unmatched. This also indicates failure.
  //
#define DEFAULT_RULE 5
  if (accept == 0 || accept == DEFAULT_RULE)
    return 0;
  else if (buf == inp_buf)
    return 0;
  else
  {
    *buf = '\0';
    return buf - 1;
  }
}
#endif

//
// Takes a pointer_info structure and attempts to verify that
//  1) the structure is not NULL
//  2) the structure exists in the given whitelist
//  3) the destination associated with the pointer_info structure has enough
//     space to hold a write of size sz.
//
// Returns p if p was not found in the whitelist, or the pointer associated
// with p otherwise.
//
static inline void *
unwrap_and_check(call_info *c, pointer_info *p, size_t sz)
{
  void **whitelist = c->whitelist;

  if (p == 0)
  {
    cerr << "Attempting to write into NULL!" << endl;
    return (void *) p;
  }

  while (*whitelist)
  {
    if (p == *whitelist)
      break;
    ++whitelist;
  }
  if (*whitelist == 0)
  {
    cerr << "Attempting to access nonexistent pointer argument "
      << (void *) p << "!" << endl;
    c_library_error(c, "va_arg");
    return (void *) p;
  }
  
  find_object(c, p);
  if (p->flags & HAVEBOUNDS)
  {
    size_t objlen = 1 + (char *) p->bounds[1] - (char *) p->ptr;
    if (sz > objlen)
    {
      cerr << "Writing out of bounds!" << endl;
      write_out_of_bounds_error(c, p, objlen, sz);
    }
  }
  return p->ptr;
}

//
// Takes a pointer_info structure and attempts to verify that:
//  1) the structure is not NULL
//  2) the structure is found in the given whitelist.
//
// Inputs:
//  c        - pointer to the call_info structure containing the whitelist to
//             search
//  p        - the pointer_info structure to lookup
//
// Returns:
//  This function returns p->ptr if the structure was found in the whitelist,
//  and p otherwise.
//
static inline void *
unwrap(call_info *c, pointer_info *p)
{
  void **whitelist = c->whitelist;

  if (p == 0)
  {
    cerr << "Attempting to write into NULL!" << endl;
    return (void *) p;
  }

  while (*whitelist)
  {
    if (p == *whitelist)
      break;
    ++whitelist;
  }
  if (*whitelist == 0)
  {
    cerr << "Attempting to access nonexistent pointer argument "
      << (void *) p << "!" << endl;
    c_library_error(c, "va_arg");
    return (void *) p;
  }

  return p->ptr;
}

//
// Returns a size which represents the maximum number of bytes that can be
// safely written starting at the address that s points to.
// 
// Inputs:
//  c       -  a pointer to the call_info structure associated with p
//  p       -  a pointer to the pointer_info structure associate with s
//  s       -  a pointer, returned via unwrap() or unwrap_and_check()
//
// The function first checks if p is a valid pointer_info structure, which
// is necessary to find the boundaries of s. If p is not a valid structure
// or the boundaries of s are unknown, the function returns SIZE_MAX.
//
static inline size_t
getsafewidth(call_info *c, pointer_info *p, void *s)
{
  //
  // The structure is not valid. Return maximum possible size.
  //
  if (p == s)
    return SIZE_MAX;
  //
  // Attempt to get the boundaries of s.
  //
  find_object(c, p);
  if (p->flags & HAVEBOUNDS)
    return (size_t) 1 + ((char *) p->bounds[1] - (char *) p->ptr);
  else
    return SIZE_MAX;
}

//
// Increment a counter that is set up to verify that a write is in boundaries.
// If the counter ever exceeds the maximum safe size, report an error.
//
// Inputs:
//  c         - the call_info structure associated with this function call
//  p         - the pointer_info structure associated with the object being
//              written into
//  curwidth  - a reference to the current number of bytes written into an
//              an object
//  safewidth - the maximum value of curwidth that won't cause a memory safety
//              error
//
// After the first time that curwidth > safewidth, the function reports a write
// error.
//
static inline void
check_and_incr_widths(call_info *c,
                      pointer_info *p,
                      size_t &curwidth,
                      const size_t safewidth)
{
  if ((++curwidth - safewidth) == 1)
  {
    size_t objlen = (size_t) 1 + ((char *) p->bounds[1] - (char *) p->ptr);
    cerr << "Writing out of bounds!" << endl;
    write_out_of_bounds_error(c, p, objlen, objlen + 1);
  }
}

//
// SAFEWRITE()
//
// A macro that attempts to securely write a value into the next parameter.
//
// Inputs:
//  ci       - pointer to the call_info structure
//  ap       - pointer to the va_list
//  arg      - the variable argument number to be accessed
//  vargc    - the total number of variable arguments
//  item     - the item to write
//  type     - the type to write the item as
//
#define SAFEWRITE(ci, ap, arg, vargc, item, type)                              \
  do                                                                           \
  {                                                                            \
    pointer_info *p;                                                           \
    void *dest;                                                                \
    if (arg++ > vargc)                                                         \
    {                                                                          \
      cerr << "Attempting to write into argument " << (arg-1) <<               \
        " but the number of arguments is " << vargc << "!" << endl;            \
      c_library_error(ci, "scanf");                                            \
    }                                                                          \
    p = va_arg(ap, pointer_info *);                                            \
    dest = unwrap_and_check(ci, p, sizeof(type));                              \
   /* if (dest != 0) */ *(type *) dest = (type) item;                          \
  } while (0)

//
// internal_scanf()
//
// This is the main logic for the scanf() routine.
//
//
// IMPLEMENTATION NOTES
//  - This function uses ungetc() to push back characters into a stream.
//    This is an error because at most one character can be pushed back
//    portably, but an ungetc() call is allowed to follow a call to a scan
//    function without any intervening I/O. Hence if this function calls
//    ungetc() and then the caller does so again on the same stream, the
//    result might fail.
//
//    However, glibc appears to have support for consecutive 2 character
//    pushback, so this doesn't fail on glibc.
//
//    Mac OS X's libc also has support for 2 character pushback.
//
//  - A nonstandard %b specifier is supported, for reading binary integers.
//  - No support for positional arguments (%n$-style directives) (yet ?).
//  - The maximum supported width of a numerical constant in the input is 512
//    bytes.
//

int
internal_scanf(input_parameter &i, call_info &c, const char *fmt, va_list ap)
{
  int   done = 0;             // number of items converted
  int   nrchars = 0;          // number of characters read
  int   base;                 // conversion base
  uintmax_t val;              // an integer value
  char *str;                  // temporary pointer
  char    *tmp_string;        // ditto
  unsigned  width = 0;        // width of field
  int   flags;                // some flags
  int   reverse;              // reverse the checking in [...]
  int   kind;
  int  ic = EOF;              // the input character
  char Xtable[NR_CHARS];      // table for %[...] scansets
  char inp_buf[NUMLEN + 1];   // buffer to hold numerical inputs

#ifdef FLOATING_POINT
  long double ld_val;
#endif

  const char *format = fmt;
  input_parameter *stream = &i;
  str = 0; // Suppress g++ complaints.
  pointer_info *p = 0;

  if (!*format) return 0;

  mbstate_t ps;
  size_t    len;
  const char *mb_pos;
  memset(&ps, 0, sizeof(ps));

  call_info *ci = &c;
  const unsigned vargc = ci->vargc;
  unsigned int arg   = 1;
  size_t safewidth = 0, curwidth = 0;

#define _SAFEWRITE(item, type) SAFEWRITE(ci, ap, arg, vargc, item, type)

  //
  // The main loop that processes the format string.
  //
  while (1)
  {
    if (isspace(*format))
    {
      while (isspace(*format))
        format++; // Skip whitespace.
      ic = _getc(stream);
      nrchars++;
      while (isspace (ic))
      {
        ic = _getc(stream);
        nrchars++;
      }
      if (ic != EOF)
        _ungetc(stream);
      nrchars--;
    }
    if (!*format) break;  // End of format
    // Match a multibyte character from the input.
    if (*format != '%')
    {
      len = mbrtowc(NULL, format, MB_CUR_MAX, &ps);
      mb_pos = format;
      format += len;
      while (mb_pos != format)
      {
        ic = _getc(stream);
        if (ic != *mb_pos)
          break;
        else
        {
          nrchars++;
          mb_pos++;
        }
      }
      if (mb_pos != format && ic != *mb_pos)
      {
        //
        // A directive that is an ordinary multibyte character is executed
        // by reading the next characters of the stream. If any of those
        // characters differ from the ones composing the directive, the
        // directive fails and the differing and subsequent characters
        // remain unread.
        //
        // - C99 Standard
        //
        if (ic != EOF)
        {
          _ungetc(stream);
          goto match_failure;
        }
        else
          goto failure;
      }
      continue;
    }
    format++; // We've read '%'; start processing a directive.
    //
    // The '%' specifier
    //
    if (*format == '%')
    {
      ic = _getc(stream);
      nrchars++;
      // Eat whitespace.
      while (isspace(ic))
      {
        ic = _getc(stream);
        nrchars++;
      }
      if (ic == '%')
      {
        format++;
        continue;
      }
      else
      {
        _ungetc(stream);
        goto failure;
      }
    }
    flags = 0;
    // '*' flag: Suppress assignment.
    if (*format == '*')
    {
      format++;
      flags |= FL_NOASSIGN;
    }
    // Get the field width, if there is any.
    if (isdigit (*format))
    {
      flags |= FL_WIDTHSPEC;
      for (width = 0; isdigit (*format);)
        width = width * 10 + *format++ - '0';
    }
    // Process length modifiers.
    switch (*format)
    {
      case 'h':
        if (*++format == 'h')
        {
          format++;
          flags |= FL_CHAR;
        }
        else
          flags |= FL_SHORT;
        break;
      case 'l':
        if (*++format == 'l')
        {
          format++;
          flags |= FL_LLONG;
        }
        else
          flags |= FL_LONG;
        break;
      case 'j':
        format++;
        flags |= FL_INTMAX;
        break;
      case 'z':
        format++;
        flags |= FL_SIZET;
        break;
      case 't':
        format++;
        flags |= FL_PTRDIFF;
        break;
      case 'L':
        format++;
        flags |= FL_LONGDOUBLE;
        break;
    }
    // Read the actual specifier.
    kind = *format;
    // Eat any initial whitespace for specifiers that allow it.
    if ((kind != 'c') && (kind != '[') && (kind != 'n'))
    {
      do
      {
        ic = _getc(stream);
        nrchars++;
      } while (isspace(ic));
      if (ic == EOF)
        goto failure;
    }
    // Get the initial character of the input, for non-%n specifiers.
    else if (kind != 'n')
    {
      ic = _getc(stream);
      if (ic == EOF)
        goto failure;
      nrchars++;
    }
    //
    // Process the format specifier.
    //
    switch (kind)
    {
      default:
        // not recognized, like %q
        goto failure;
      //
      // %n specifier
      //
      case 'n':
        if (!(flags & FL_NOASSIGN))
        {
          if (flags & FL_CHAR)
            _SAFEWRITE(nrchars, char);
          else if (flags & FL_SHORT)
            _SAFEWRITE(nrchars, short);
          else if (flags & FL_LONG)
            _SAFEWRITE(nrchars, long);
          else if (flags & FL_LLONG)
            _SAFEWRITE(nrchars, long long);
          else if (flags & FL_INTMAX)
            _SAFEWRITE(nrchars, intmax_t);
          else if (flags & FL_SIZET)
            _SAFEWRITE(nrchars, size_t);
          else if (flags & FL_PTRDIFF)
            _SAFEWRITE(nrchars, ptrdiff_t);
          else
            _SAFEWRITE(nrchars, int);
        }
        break;
      //
      // %p specifier
      //
      case 'p':
        // Set any additional flags regarding pointer representation size.
        set_pointer(flags);
        // fallthrough
      //
      // Integral specifiers: %b, %d, %i, %o, %u, %x, %X
      //
      case 'b':   // binary
      case 'd':   // decimal
      case 'i':   // general integer
      case 'o':   // octal
      case 'u':   // unsigned
      case 'x':   // hexadecimal
      case 'X':   // ditto
        // Don't read more than NUMLEN bytes.
        if (!(flags & FL_WIDTHSPEC) || width > NUMLEN)
          width = NUMLEN;
        if (!width)
          goto match_failure;

        str = o_collect(ic, inp_buf, stream, kind, width, &base);

        if (str == 0)
          goto failure;

        //
        // Although the length of the number is str-inp_buf+1
        // we don't add the 1 since we counted it already.
        // 
        nrchars += str - inp_buf;

        if (!(flags & FL_NOASSIGN))
        {
          if (kind == 'd' || kind == 'i')
            val = strtoimax(inp_buf, &tmp_string, base);
          else
            val = strtoumax(inp_buf, &tmp_string, base);
          if (flags & FL_CHAR)
            _SAFEWRITE(val, unsigned char);
          else if (flags & FL_SHORT)
            _SAFEWRITE(val, unsigned short);
          else if (flags & FL_LONG)
            _SAFEWRITE(val, unsigned long);
          else if (flags & FL_LLONG)
            _SAFEWRITE(val, unsigned long long);
          else if (flags & FL_INTMAX)
            _SAFEWRITE(val, uintmax_t);
          else if (flags & FL_SIZET)
            _SAFEWRITE(val, size_t);
          else if (flags & FL_PTRDIFF)
            _SAFEWRITE(val, ptrdiff_t);
          else
            _SAFEWRITE(val, unsigned);
        }
        break;

#define va_sarg(ap) va_arg(ap, pointer_info *)

#define incr_argcount()                                                        \
  arg++ > vargc && cerr << "Attempting to access argument " << (arg-1) <<      \
  " but the number of arguments is " << vargc << "!" << endl

      //
      // %c specifier
      //
      case 'c':
        if (!(flags & FL_WIDTHSPEC))
          width = 1;
        if (!(flags & FL_NOASSIGN))
        {
          incr_argcount();
          p         = va_sarg(ap);
          str       = (char *) unwrap(ci, p);
          safewidth = getsafewidth(ci, p, (void *) str);
          curwidth  = 0;
        }
        if (!width)
          goto match_failure;

        while (width && ic != EOF)
        {
          if (!(flags & FL_NOASSIGN))
          {
            check_and_incr_widths(ci, p, curwidth, safewidth);
            *str++ = (char) ic;
          }
          if (--width)
          {
            ic = _getc(stream);
            nrchars++;
          }
        }

        if (width)
        {
          // Is this condition ever true?
          if (ic != EOF) _ungetc(stream);
          nrchars--;
        }
        break;
      //
      // %s specifier
      //
      case 's':
        if (!(flags & FL_WIDTHSPEC))
          width = UINT_MAX;
        if (!(flags & FL_NOASSIGN))
        {
          incr_argcount();
          p         = va_sarg(ap);
          str       = (char *) unwrap(ci, p);
          safewidth = getsafewidth(ci, p, (void *) str);
          curwidth = 0;
        }
        if (!width)
          goto match_failure;

        // Read the string, with the given width, up to a space or EOF.
        while (width && ic != EOF && !isspace(ic))
        {
          if (!(flags & FL_NOASSIGN))
          {
            check_and_incr_widths(ci, p, curwidth, safewidth);
            *str++ = (char) ic;
          }
          if (--width)
          {
            ic = _getc(stream);
            nrchars++;
          }
        }
        // Terminate the string.
        if (!(flags & FL_NOASSIGN))
        {
          check_and_incr_widths(ci, p, curwidth, safewidth);
          *str = '\0';  
        }
        if (width)
        {
          // Push back any whitespace we've read.
          if (ic != EOF) _ungetc(stream);
          nrchars--;
        }
        break;
      //
      // %[...] specifier
      //
      case '[':
        if (!(flags & FL_WIDTHSPEC))
          width = UINT_MAX;
        if (!width)
          goto match_failure;
        //
        // Determine if we take the complement of the scanset.
        //
        if (*++format == '^')
        {
          reverse = 1;
          format++;
        }
        else
          reverse = 0;

        memset(&Xtable[0], 0, sizeof(Xtable));

        //
        // ']' appearing as the first character in the set does not close the
        // directive, but adds ']' to the scanset.
        //
        if (*format == ']') Xtable[(unsigned) *format++] = 1;

        while (*format && *format != ']')
        {
          Xtable[(unsigned) *format++] = 1;
          //
          // Add a character range to the scanset...
          //
          if (*format == '-')
          {
            format++;
            if (*format && *format != ']' && *(format) >= *(format -2))
            {
              int c;
              for( c = *(format -2) + 1
                  ; c <= *format ; c++)
                Xtable[(unsigned) c] = 1;
              format++;
            }
            // ...unless '-' is the last character in the set.
            else if (*format == ']')
              Xtable[(unsigned) '-'] = 1;
          }
        }
        if (!*format)
          goto match_failure;
        
        // Check for match failure.
        if (!(Xtable[(unsigned) ic] ^ reverse))
        {
          _ungetc(stream);
          goto match_failure;
        }

        if (!(flags & FL_NOASSIGN))
        {
          incr_argcount();
          p         = va_sarg(ap);
          str       = (char *) unwrap(ci, p);
          safewidth = getsafewidth(ci, p, (void *) str);
          curwidth  = 0;
        }

        // Read the rest of the string.
        do
        {
          if (!(flags & FL_NOASSIGN))
          {
            check_and_incr_widths(ci, p, curwidth, safewidth);
            *str++ = (char) ic;
          }
          if (--width)
          {
            ic = _getc(stream);
            nrchars++;
          }
        } while (width && ic != EOF && (Xtable[ic] ^ reverse));

        if (width)
        {
          if (ic != EOF)
            _ungetc(stream);
          nrchars--;
        }
        if (!(flags & FL_NOASSIGN)) // Terminate string.
        {
          check_and_incr_widths(ci, p, curwidth, safewidth);
          *str = '\0';  
        }
        break;

#ifdef FLOATING_POINT
      //
      // Floating point specifiers: %e, %E, %f, %g, %G
      //
      case 'e':
      case 'E':
      case 'f':
      case 'g':
      case 'G':
        if (!(flags & FL_WIDTHSPEC) || width > NUMLEN)
          width = NUMLEN;

        if (!width)
          goto failure;
        str = f_collect(ic, inp_buf, stream, width);

        if (str == 0)
          goto failure;

        //
        // Although the length of the number is str-inp_buf+1
        // we don't add the 1 since we counted it already
        //
        nrchars += str - inp_buf;

        if (!(flags & FL_NOASSIGN))
        {
          ld_val = strtold(inp_buf, &tmp_string);
          if (flags & FL_LONGDOUBLE)
            _SAFEWRITE(ld_val, long double);
          else if (flags & FL_LONG)
            _SAFEWRITE(ld_val, double);
          else
            _SAFEWRITE(ld_val, float);
        }
        break;
#endif

    }
    if (!(flags & FL_NOASSIGN) && kind != 'n')
      done++;
    format++;
  }

  //
  // The fscanf function returns the value of the macro EOF if an input failure
  // occurs before the first conversion (if any) has completed. Otherwise, the
  // function returns the number of input items assigned, which can be fewer
  // than provided for, or even zero, in the event of an early matching failure.
  //
  // - C99 Standard
  //
match_failure:
  return done;

failure:
  //
  // In the event of a possible input failure (=eof or read error), the
  // directive should jump to here.
  //
  if (done == 0 && input_failure(stream))
    return EOF;
  else
    goto match_failure;
}

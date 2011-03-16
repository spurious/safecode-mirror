//===------- CStdLib.cpp - CStdLib transform pass runtime functions -------===//
// 
//                          The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file provides all external functions included by the CStdLib pass.
//
//===----------------------------------------------------------------------===//

#include "DebugReport.h"
#include "PoolAllocator.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream> // Debug

#define TAG unsigned tag
#define SRC_INFO const char *SourceFile, unsigned lineNo

// Default versions of arguments to debug functions
#define DEFAULT_TAG 0
#define DEFAULT_SRC_INFO "<Unknown>", 0
#define SRC_INFO_ARGS SourceFile, lineNo

// Various violation types
#define OOB_VIOLATION(fault_ptr, handle, start, len) \
    OutOfBoundsViolation v;    \
    v.type        = ViolationInfo::FAULT_OUT_OF_BOUNDS; \
    v.faultPC     = __builtin_return_address(0); \
    v.faultPtr    = fault_ptr;  \
    v.SourceFile  = SourceFile; \
    v.lineNo      = lineNo;     \
    v.PoolHandle  = handle;     \
    v.objStart    = start;      \
    v.objLen      = len;        \
    v.dbgMetaData = NULL;       \
    ReportMemoryViolation(&v);

#define WRITE_VIOLATION(fault_ptr, handle, dst_sz, src_sz) \
    WriteOOBViolation v; \
    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,\
    v.faultPC = __builtin_return_address(0), \
    v.faultPtr = fault_ptr, \
    v.SourceFile = SourceFile, \
    v.lineNo =     lineNo, \
    v.PoolHandle = handle, \
    v.dstSize =    dst_sz, \
    v.srcSize =    src_sz, \
    v.dbgMetaData = NULL; \
    ReportMemoryViolation(&v);

#define LOAD_STORE_VIOLATION(fault_ptr, handle) \
    DebugViolationInfo v; \
    v.faultPC = __builtin_return_address(0), \
    v.faultPtr = fault_ptr, \
    v.dbgMetaData = NULL, \
    v.PoolHandle = handle, \
    v.SourceFile = SourceFile, \
    v.lineNo = lineNo; \
    ReportMemoryViolation(&v);

using namespace NAMESPACE_SC;

extern "C" {
  size_t strnlen(const char *s, size_t maxlen) {
    size_t i;
    for (i = 0; i < maxlen && s[i]; ++i)
      ;
    return i;
  }

  size_t strnlen_opt(const char *s, size_t maxlen) {
    const char *end = (const char *)memchr(s, '\0', maxlen);
    return (end ? ((size_t) (end - s)) : maxlen);
  }
}

/**
 * Optimized inline assembly implementation of strncpy that returns the number
 * of characters copied (including \0)
 *
 * @param   dst      Destination string pointer
 * @param   src      Source string pointer
 * @param   size     Number of characters to copy
 * @return  Number of characters copied (including \0)
 */
static size_t strncpy_asm(char *dst, const char *src, size_t size) {
  long copied;

#if defined(i386) || defined(__i386__) || defined(__x86__)
  __asm__ __volatile__(
    "0: xorl      %%ecx, %%ecx      \n"
    "   cmpl      %%edi, %%ecx      \n"
    "   adcl      $0, %%ecx         \n"
    "   decl      %%edi             \n"
    "   testl     %%ecx, %%ecx      \n"
    "   je        1f                \n"
    "   movsbl    (%%edx), %%ecx    \n"
    "   movb      %%cl, (%%eax)     \n"
    "   incl      %%eax             \n"
    "   incl      %%edx             \n"
    "   testl     %%ecx, %%ecx      \n"
    "   jne       0b                \n"
    "1: subl      %%esi, %%eax      \n"
    : "=a"(copied)
    : "a"(dst), "S"(dst), "d"(src), "D"(size)
    : "%ecx", "memory"
  );
#else
  strncpy(dst, src, size);
  copied = strnlen(dst, size - 1);
#endif

  return copied;
}

/**
 * Check for string termination.
 *
 * @param start  The start of the string
 * @param end    The end of the object. String is not scanned farther than here.
 * @param p      Reference to size object. Filled with the length of the string if
 *               string is terminated, otherwised filled with the size of the object.
 * @return       Whether the string is terminated within bounds.
 *
 * Note that start and end should be valid boundaries for a valid object.
 */
static bool isTerminated(void *start, void *end, size_t &p)
{
  size_t max = 1 + ((char *)end - (char *)start), len;
  len = strnlen((char *)start, max);
  p = len;
  if (len == max)
    return false;
  else
    return true;
}

/**
 * Function to search within the object and external object pools
 *
 * @param   pool       Pool handle
 * @param   poolBegin  Pointer to set to the beginning of the pool handle
 * @param   poolEnd    Pointer to set to the end of the pool handle
 * @return  Object found in pool
 */
bool pool_find(DebugPoolTy *pool, void *&poolBegin, void *&poolEnd) {
  // Retrieve memory area's bounds from pool handle.
  if (pool->Objects.find(poolBegin, poolBegin, poolEnd) || ExternalObjects.find(poolBegin, poolBegin, poolEnd))
    return true;

  return false;
}

/**
 * Secure runtime wrapper function to replace memcpy()
 *
 * @param   dstPool  Pool handle for destination memory area
 * @param   srcPool  Pool handle for source memory area
 * @param   dst      Destination memory area
 * @param   src      Source memory area
 * @param   n        Maximum number of bytes to copy
 * @return  Destination memory area
 */
void *pool_memcpy(DebugPoolTy *dstPool, DebugPoolTy *srcPool, void *dst, const void *src, size_t n) {
  size_t dstSize = 0, srcSize = 0, stop = 0;
  void *dstBegin = dst, *dstEnd = NULL, *srcBegin = (char *)src, *srcEnd = NULL;

  assert(dstPool && srcPool && dst && src && "Null pool parameters!");

  // Retrieve both the destination and source buffer's bounds from the pool handle.
  assert(pool_find(dstPool, dstBegin, dstEnd) && "Destination buffer not found in pool!");
  assert(pool_find(srcPool, srcBegin, srcEnd) && "Source string not found in pool!");

  // Check that both the destination and source pointers fall within their respective bounds.
  if (dstBegin > dstEnd) {
    std::cout << "Destination pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = dstBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = dstPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = dstBegin,
    v.objLen = (unsigned)((char *)dstEnd - (char *)dstBegin) + 1;

    ReportMemoryViolation(&v);
  }

  if (srcBegin > srcEnd) {
    std::cout << "Source pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = srcPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = srcBegin,
    v.objLen = (unsigned)((char *)srcEnd - (char *)srcBegin) + 1;

    ReportMemoryViolation(&v);
  }

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - (char *)dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;

  if (n > srcSize) {
    std::cout << "Cannot copy more bytes than the size of the source!\n";

    WriteOOBViolation v;

    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.PoolHandle = srcPool,
    v.dstSize = dstSize,
    v.srcSize = srcSize,
    v.dbgMetaData = NULL;

    ReportMemoryViolation(&v);
  }

  stop = std::min(n, srcSize);

  if (stop > dstSize) {
    std::cout << "Copy violated destination bounds!\n";

    WriteOOBViolation v;

    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.PoolHandle = srcPool,
    v.dstSize = dstSize,
    v.srcSize = srcSize,
    v.dbgMetaData = NULL;

    ReportMemoryViolation(&v);
  }

  memcpy(dst, src, stop);

  return dst;
}

/**
 * Secure runtime wrapper function to replace memmove()
 *
 * @param   dstPool  Pool handle for destination memory area
 * @param   srcPool  Pool handle for source memory area
 * @param   dst      Destination memory area
 * @param   src      Source memory area
 * @param   n        Maximum number of bytes to copy
 * @return  Destination memory area
 */
void *pool_memmove(DebugPoolTy *dstPool, DebugPoolTy *srcPool, void *dst, const void *src, size_t n) {
  size_t dstSize = 0, srcSize = 0, stop = 0;
  void *dstBegin = dst, *dstEnd = NULL, *srcBegin = (char *)src, *srcEnd = NULL;

  assert(dstPool && srcPool && dst && src && "Null pool parameters!");

  // Retrieve both the destination and source buffer's bounds from the pool handle.
  assert(pool_find(dstPool, dstBegin, dstEnd) && "Destination buffer not found in pool!");
  assert(pool_find(srcPool, srcBegin, srcEnd) && "Source string not found in pool!");

  // Check that both the destination and source pointers fall within their respective bounds.
  if (dstBegin > dstEnd) {
    std::cout << "Destination pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = dstBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = dstPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = dstBegin,
    v.objLen = (unsigned)((char *)dstEnd - (char *)dstBegin) + 1;

    ReportMemoryViolation(&v);
  }

  if (srcBegin > srcEnd) {
    std::cout << "Source pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = srcPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = srcBegin,
    v.objLen = (unsigned)((char *)srcEnd - (char *)srcBegin) + 1;

    ReportMemoryViolation(&v);
  }

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - (char *)dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;

  if (n > srcSize) {
    std::cout << "Cannot copy more bytes than the size of the source!\n";

    WriteOOBViolation v;

    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.PoolHandle = srcPool,
    v.dstSize = dstSize,
    v.srcSize = srcSize,
    v.dbgMetaData = NULL;

    ReportMemoryViolation(&v);
  }

  stop = std::min(n, srcSize);

  if (stop > dstSize) {
    std::cout << "Copy violated destination bounds!\n";

    WriteOOBViolation v;

    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.PoolHandle = srcPool,
    v.dstSize = dstSize,
    v.srcSize = srcSize,
    v.dbgMetaData = NULL;

    ReportMemoryViolation(&v);
  }

  memmove(dst, src, stop);

  return dst;
}

/**
 * Secure runtime wrapper function to replace mempcpy()
 *
 * @param   dstPool  Pool handle for destination memory area
 * @param   srcPool  Pool handle for source memory area
 * @param   dst      Destination memory area
 * @param   src      Source memory area
 * @param   n        Maximum number of bytes to copy
 * @return  Destination memory area
 */
void *pool_mempcpy(DebugPoolTy *dstPool, DebugPoolTy *srcPool, void *dst, const void *src, size_t n) {
  size_t dstSize = 0, srcSize = 0, stop = 0;
  void *dstBegin = dst, *dstEnd = NULL, *srcBegin = (char *)src, *srcEnd = NULL;

  assert(dstPool && srcPool && dst && src && "Null pool parameters!");

  // Retrieve both the destination and source buffer's bounds from the pool handle.
  assert(pool_find(dstPool, dstBegin, dstEnd) && "Destination buffer not found in pool!");
  assert(pool_find(srcPool, srcBegin, srcEnd) && "Source string not found in pool!");

  // Check that both the destination and source pointers fall within their respective bounds.
  if (dstBegin > dstEnd) {
    std::cout << "Destination pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = dstBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = dstPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = dstBegin,
    v.objLen = (unsigned)((char *)dstEnd - (char *)dstBegin) + 1;

    ReportMemoryViolation(&v);
  }

  if (srcBegin > srcEnd) {
    std::cout << "Source pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = srcPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = srcBegin,
    v.objLen = (unsigned)((char *)srcEnd - (char *)srcBegin) + 1;

    ReportMemoryViolation(&v);
  }

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - (char *)dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;

  if (n > srcSize) {
    std::cout << "Cannot copy more bytes than the size of the source!\n";

    WriteOOBViolation v;

    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.PoolHandle = srcPool,
    v.dstSize = dstSize,
    v.srcSize = srcSize,
    v.dbgMetaData = NULL;

    ReportMemoryViolation(&v);
  }

  stop = std::min(n, srcSize);

  if (stop > dstSize) {
    std::cout << "Copy violated destination bounds!\n";

    WriteOOBViolation v;

    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.PoolHandle = srcPool,
    v.dstSize = dstSize,
    v.srcSize = srcSize,
    v.dbgMetaData = NULL;

    ReportMemoryViolation(&v);
  }

#ifndef _GNU_SOURCE
  mempcpy(dst, src, stop);
#endif

  return dst;
}

/**
 * Secure runtime wrapper function to replace memset()
 *
 * @param   stringPool  Pool handle for the string
 * @param   string      String pointer
 * @return  Pointer to memory area
 */
void *pool_memset(DebugPoolTy *stringPool, void *string, int c, size_t n) {
  size_t stringSize = 0, stop = 0;
  void *stringBegin = string, *stringEnd = NULL;

  assert(stringPool && string && "Null pool parameters!");
  assert(pool_find(stringPool, stringBegin, stringEnd) && "S not found in pool!");

  if (stringBegin > stringEnd) {
    std::cout << "String pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = stringBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = stringPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = stringBegin,
    v.objLen = (unsigned)((char *)stringEnd - (char *)stringBegin) + 1;

    ReportMemoryViolation(&v);
  }

  stringSize = (char *)stringEnd - (char *)string + 1;
  if (n > stringSize) {
    std::cout << "Cannot write more bytes than the size of the destination string!\n";

    WriteOOBViolation v;

    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = stringBegin,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.PoolHandle = stringPool,
    v.dstSize = stringSize,
    v.dbgMetaData = NULL;

    ReportMemoryViolation(&v);
  }

  stop = std::min(n, stringSize);
  return memset(string, c, stop);
}

char *pool_strcpy_debug(DebugPoolTy *dstPool, DebugPoolTy *srcPool, char *dst, const char *src, const unsigned char complete, TAG, SRC_INFO) {
  size_t copied = 0, dstSize = 0, srcSize = 0, stop = 0;
  void *dstBegin = dst, *dstEnd = NULL, *srcBegin = (char *)src, *srcEnd = NULL;

  // Ensure all valid pointers.
  assert(dstPool && srcPool && dst && src && "Null pool parameters!");

  // Retrieve the destination buffer's bounds from the pool handle.
  if (!pool_find(dstPool, dstBegin, dstEnd)) {
    std::cout << "Destination buffer not found in pool!\n";

    DebugViolationInfo v;

    v.type = ViolationInfo::FAULT_LOAD_STORE,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = dstBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = dstPool,
    v.SourceFile = SourceFile,
    v.lineNo = lineNo;

    ReportMemoryViolation(&v);
  }

  // Retrieve the source buffer's bounds from the pool handle.
  if (!pool_find(srcPool, srcBegin, srcEnd)) {
    std::cout << "Source string not found in pool!\n";

    DebugViolationInfo v;

    v.type = ViolationInfo::FAULT_LOAD_STORE,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = srcPool,
    v.SourceFile = SourceFile,
    v.lineNo = lineNo;

    ReportMemoryViolation(&v);
  }

  // Check that both the destination and source pointers fall within their respective bounds.
  if (dstBegin > dstEnd) {
    std::cout << "Destination pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = dstBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = dstPool,
    v.SourceFile = SourceFile,
    v.lineNo = lineNo;
    v.objStart = dstBegin,
    v.objLen = (unsigned)((char *)dstEnd - (char *)dstBegin) + 1;

    ReportMemoryViolation(&v);
  }

  if (srcBegin > srcEnd) {
    std::cout << "Source pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = srcPool,
    v.SourceFile = SourceFile,
    v.lineNo = lineNo;
    v.objStart = srcBegin,
    v.objLen = (unsigned)((char *)srcEnd - (char *)srcBegin) + 1;

    ReportMemoryViolation(&v);
  }

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;
  stop = std::min(dstSize, srcSize);

  // Copy the source string to the destination buffer and record the number of bytes copied (including \0).
#if 0
  copied = strncpy_asm(dst, src, stop);
#endif
  strncpy(dst, src, stop);
  copied = strnlen(dst, stop - 1);

std::cout << "dst: " << dstSize << ", src: " << srcSize << ", stop: " << stop << ", copied: " << copied << "\n";

#if 0
  if (dst[copied - 1]) {
#endif
  if (dst[copied] != 0) {
    std::cout << "Copy violated destination bounds!\n";

    WriteOOBViolation v;

    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.SourceFile = SourceFile,
    v.lineNo = lineNo;
    v.PoolHandle = srcPool,
    v.dstSize = dstSize,
    v.srcSize = srcSize,
    v.copied = copied,
    v.dbgMetaData = NULL;

    ReportMemoryViolation(&v);
  }

  return dst;
}

/**
 * Secure runtime wrapper function to replace strcpy()
 *
 * @param   dstPool  Pool handle for destination buffer
 * @param   srcPool  Pool handle for source string
 * @param   dst      Destination string pointer
 * @param   src      Source string pointer
 * @return  Destination string pointer
 */
char *pool_strcpy(DebugPoolTy *dstPool, DebugPoolTy *srcPool, char *dst, const char *src, const unsigned char complete) {
  return pool_strcpy_debug(dstPool, srcPool, dst, src, complete, 0, "<Unknown>", 0);
}

/**
 * Secure runtime wrapper function to replace strlen()
 *
 * @param   stringPool  Pool handle for the string
 * @param   string      String pointer
 * @return  Length of the string
 */
size_t pool_strlen_debug(DebugPoolTy *stringPool, const char *string, const unsigned char complete, TAG, SRC_INFO) {
  size_t len = 0, maxlen = 0;
  void *stringBegin = (char *)string, *stringEnd = NULL;

  assert(stringPool && string && "Null pool parameters!");
  assert(pool_find(stringPool, stringBegin, stringEnd) && "String not found in pool!");

  if (stringBegin > stringEnd) {
    std::cout << "String pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = stringBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = stringPool,
    v.SourceFile = SourceFile,
    v.lineNo = lineNo,
    v.objStart = stringBegin,
    v.objLen = (unsigned)((char *)stringEnd - (char *)stringBegin) + 1;

    ReportMemoryViolation(&v);
  }

  maxlen = (char *)stringEnd - (char *)string;
  len = strnlen(string, maxlen + 1);

  if (len > maxlen) {
    std::cout << "String not terminated within bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = stringBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = stringPool,
    v.SourceFile = SourceFile,
    v.lineNo = lineNo,
    v.objStart = stringBegin,
    v.objLen = (unsigned)((char *)stringEnd - (char *)stringBegin) + 1;

    ReportMemoryViolation(&v);
  }

  return len;
}

/**
 * Secure runtime wrapper function to replace strlen()
 *
 * @param   stringPool  Pool handle for the string
 * @param   string      String pointer
 * @return  Length of the string
 */
size_t pool_strlen(DebugPoolTy *stringPool, const char *string, const unsigned char complete) {
  return pool_strlen_debug(stringPool, string, complete, 0, "<Unknown>", 0);
}

/**
 * Secure runtime wrapper function to replace strncpy()
 *
 * @param   dstPool  Pool handle for destination buffer
 * @param   srcPool  Pool handle for source string
 * @param   dst      Destination string pointer
 * @param   src      Source string pointer
 * @param   n        Maximum number of bytes to copy
 * @return  Destination string pointer
 */
char *pool_strncpy(DebugPoolTy *dstPool, DebugPoolTy *srcPool, char *dst, const char *src, size_t n) {
  size_t copied = 0, dstSize = 0, srcSize = 0, stop = 0;
  void *dstBegin = dst, *dstEnd = NULL, *srcBegin = (char *)src, *srcEnd = NULL;

  assert(dstPool && srcPool && dst && src && "Null pool parameters!");

  // Retrieve both the destination and source buffer's bounds from the pool handle.
  assert(pool_find(dstPool, dstBegin, dstEnd) && "Destination buffer not found in pool!");
  assert(pool_find(srcPool, srcBegin, srcEnd) && "Source string not found in pool!");

  // Check that both the destination and source pointers fall within their respective bounds.
  if (dstBegin > dstEnd) {
    std::cout << "Destination pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = dstBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = dstPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = dstBegin,
    v.objLen = (unsigned)((char *)dstEnd - (char *)dstBegin) + 1;

    ReportMemoryViolation(&v);
  }

  if (srcBegin > srcEnd) {
    std::cout << "Source pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = srcPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = srcBegin,
    v.objLen = (unsigned)((char *)srcEnd - (char *)srcBegin) + 1;

    ReportMemoryViolation(&v);
  }

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;
  stop = std::min(dstSize, srcSize);

  if (stop < n) {
    copied = strncpy_asm(dst, src, stop);

    if (copied == stop) {
      std::cout << "Copy violated destination bounds!\n";

      WriteOOBViolation v;

      v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
      v.faultPC = __builtin_return_address(0),
      v.faultPtr = srcBegin,
      v.SourceFile = __FILE__,
      v.lineNo = __LINE__,
      v.PoolHandle = srcPool,
      v.dstSize = dstSize,
      v.srcSize = srcSize,
      v.copied = copied,
      v.dbgMetaData = NULL;

      ReportMemoryViolation(&v);
    }
  } else {
    copied = strncpy_asm(dst, src, n);

    // Possibly not NULL terminated
    if (copied > dstSize) {
      std::cout << "Copy violated destination bounds!\n";

      WriteOOBViolation v;

      v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
      v.faultPC = __builtin_return_address(0),
      v.faultPtr = srcBegin,
      v.SourceFile = __FILE__,
      v.lineNo = __LINE__,
      v.PoolHandle = srcPool,
      v.dstSize = dstSize,
      v.srcSize = srcSize,
      v.copied = copied,
      v.dbgMetaData = NULL;

      ReportMemoryViolation(&v);
    }
  }

  return dst;
}

/**
 * Secure runtime wrapper function to replace strnlen()
 *
 * @param   stringPool  Pool handle for the string
 * @param   string      String pointer
 * @return  Length of the string
 */
size_t pool_strnlen(DebugPoolTy *stringPool, const char *string, size_t maxlen) {
  size_t len = 0, difflen = 0;
  void *stringBegin = (char *)string, *stringEnd = NULL;

  assert(stringPool && string && "Null pool parameters!");
  assert(pool_find(stringPool, stringBegin, stringEnd) && "String not found in pool!");

  if (stringBegin > stringEnd) {
    std::cout << "String pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = stringBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = stringPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = stringBegin,
    v.objLen = (unsigned)((char *)stringEnd - (char *)stringBegin) + 1;

    ReportMemoryViolation(&v);
  }

  difflen = (char *)stringEnd - (char *)string;

  if (difflen > maxlen) {
    std::cout << "String in pool longer than maxlen!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = stringBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = stringPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = stringBegin,
    v.objLen = (unsigned)((char *)stringEnd - (char *)stringBegin) + 1;

    ReportMemoryViolation(&v);
  }

  len = strnlen(string, difflen + 1);

  if (len > difflen) {
    std::cout << "String not terminated within bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = stringBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = stringPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = stringBegin,
    v.objLen = (unsigned)((char *)stringEnd - (char *)stringBegin) + 1;

    ReportMemoryViolation(&v);
  }

  return len;
}

/**
 * Check if object bounds are valid. Report any errors.
 *
 * @param handle The pool handle this object comes from.
 * @param start  Start of object
 * @param end    End of object
 */
static inline void doOOBCheck(DebugPoolTy *handle, const void *start, const void *end, SRC_INFO)
{
  if (end < start)
  {
    std::cout << "Pointer out of bounds!\n";
    OutOfBoundsViolation v;
    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = start,
    v.dbgMetaData = NULL,
    v.PoolHandle = handle,
    v.SourceFile = SourceFile,
    v.lineNo = lineNo,
    v.objStart = start,
    v.objLen = ((char*)end - (char *)start) + 1;
    ReportMemoryViolation(&v);
  }
}

/**
 * Secure runtime wrapper function to replace strchr()
 *
 * @param   sp     Pool handle for string
 * @param   s      String pointer
 * @param   c      Character to find
 * @return  Pointer to first instance of c in s or NULL
 */

char *pool_strchr(DebugPoolTy *sp,
                  const char *s,
                  int c,
                  unsigned char complete)
{
  return pool_strchr_debug(sp, s, c, complete,
    DEFAULT_TAG, DEFAULT_SRC_INFO);
}

/**
 * Secure runtime wrapper function to replace strchr()
 *
 * @param   sPool  Pool handle for string
 * @param   s      String pointer
 * @param   c      Character to find
 * @return  Pointer to first instance of c in s or NULL
 */
char *pool_strchr_debug(DebugPoolTy *sPool,
                        const char *s,
                        int c,
                        unsigned char complete,
                        TAG,
                        SRC_INFO)
{
  void *objStart = (void *) s, *objEnd;
  size_t len;

  // Ensure string and pool are non-null.
  assert(sPool && s && "Null pool handles!");

  // Find string in pool.
  if (!pool_find(sPool, objStart, objEnd)) {
    std::cout << "String not found in pool!\n";
    LOAD_STORE_VIOLATION(objStart, sPool)
  }

  // Check if string is out of bounds.
  doOOBCheck(sPool, objStart, objEnd, SRC_INFO_ARGS);

  // Check if string is terminated.
  if (!isTerminated(objStart, objEnd, len)) {
    std::cout << "String not terminated within bounds\n";
    OOB_VIOLATION(s, sPool, s, len);
  }

  return strchr(s, c);
}

/**
 * Secure runtime wrapper function to replace strrchr()
 *
 * @param   sPool  Pool handle for string
 * @param   s      String pointer
 * @param   c      Character to find
 * @return  Pointer to last instance of c in s or NULL
 */
char *pool_strrchr(DebugPoolTy *sPool,
                   const char *s,
                   int c,
                   const unsigned char complete)
{
  return pool_strrchr_debug(sPool, s, c, complete,
    DEFAULT_TAG, DEFAULT_SRC_INFO);
}


/**
 * Secure runtime wrapper function to replace strrchr()
 *
 * @param   sPool  Pool handle for string
 * @param   s      String pointer
 * @param   c      Character to find
 * @return  Pointer to last instance of c in s or NULL
 */
char *pool_strrchr_debug(DebugPoolTy *sPool,
                         const char *s,
                         int c,
                         const unsigned char complete,
                         TAG,
                         SRC_INFO)
{
  void *objStart = (void *) s, *objEnd;
  size_t len;

  // Ensure string and pool are non-null.
  assert(sPool && s && "Null pool handles!");

  // Find string in pool.
  if (!pool_find(sPool, objStart, objEnd)) {
    std::cout << "String not found in pool!\n";
    LOAD_STORE_VIOLATION(objStart, sPool)
  }

  // Check if string is out of bounds.
  doOOBCheck(sPool, objStart, objEnd, SRC_INFO_ARGS);

  // Check if string is terminated.
  if (!isTerminated(objStart, objEnd, len)) {
    std::cout << "String not terminated within bounds\n";
    OOB_VIOLATION(s, sPool, s, len);
  }

  return strrchr(s, c);
}

char *pool_strstr(DebugPoolTy *s1Pool,
                  DebugPoolTy *s2Pool,
                  const char *s1,
                  const char *s2,
                  unsigned char complete)
{
  return pool_strstr_debug(s1Pool, s2Pool, s1, s2, complete,
    DEFAULT_TAG, DEFAULT_SRC_INFO);
}

char *pool_strstr_debug(DebugPoolTy *s1Pool,
                        DebugPoolTy *s2Pool,
                        const char *s1,
                        const char *s2,
                        unsigned char complete,
                        TAG,
                        SRC_INFO)
{
  void *s1Begin = (void *) s1, *s1End;
  void *s2Begin = (void *) s2, *s2End;
  size_t s1Len, s2Len;

  // Ensure non-null arguments.
  assert(s1Pool && s1 && s2Pool && s2 && "Null pool parameters!");

  // Find strings in the pool.
  if (!pool_find(s1Pool, s1Begin, s1End)) {
    std::cout << "String not found in pool!\n";
    LOAD_STORE_VIOLATION(s1Begin, s1Pool)
  }
  if (!pool_find(s2Pool, s2Begin, s2End)) {
    std::cout << "String not found pool!\n";
    LOAD_STORE_VIOLATION(s2Begin, s2Pool)
  }

  // Check if strings are out of bounds.
  doOOBCheck(s1Pool, s1Begin, s1End, SRC_INFO_ARGS);
  doOOBCheck(s2Pool, s2Begin, s2End, SRC_INFO_ARGS);

  // Check if both strings are terminated.
  if (!isTerminated(s1Begin, s1End, s1Len)) {
    std::cout << "String not terminated within bounds!\n";
    OOB_VIOLATION(s1Begin, s1Pool, s1Begin, s1Len)
  }
  if (!isTerminated(s2Begin, s2End, s2Len)) {
    std::cout << "String not terminated within bounds!\n";
    OOB_VIOLATION(s2Begin, s2Pool, s2Begin, s2Len)
  }

  return strstr(s1, s2);
}

/**
 * Secure runtime wrapper function to replace strcat()
 *
 * @param  dp    Pool handle for destination string
 * @param  sp    Pool handle for source string
 * @param  d     Destination string pointer
 * @param  s     Source string pointer
 * @return       Destination string pointer
 */
char *pool_strcat(DebugPoolTy *dp,
                  DebugPoolTy *sp,
                  char *d,
                  const char *s,
                  const unsigned char c)
{
  return pool_strcat_debug(dp, sp, d, s, c,
    DEFAULT_TAG, DEFAULT_SRC_INFO);
}

/**
 * Secure runtime wrapper function to replace strcat()
 *
 * @param  dstPool  Pool handle for destination string
 * @param  srcPool  Pool handle for source string
 * @param  dst      Destination string pointer
 * @param  src      Source string pointer
 * @return          Destination string pointer
 */
char *pool_strcat_debug(DebugPoolTy *dstPool,
                        DebugPoolTy *srcPool,
                        char *dst,
                        const char *src,
                        const unsigned char complete,
                        TAG,
                        SRC_INFO)
{
  size_t srcLen, dstLen, maxLen, catLen;
  void *dstBegin = (void *) dst, *dstEnd = NULL;
  void *srcBegin = (void *) src, *srcEnd = NULL;
  char *dstNulPosition;

  // Ensure non-null pool and string arguments.
  assert(dstPool && dst && srcPool && src && "Null pool parameters!");

  // Find the strings in the pool.
  if (!pool_find(dstPool, dstBegin, dstEnd)) {
    std::cout << "Destination string not found in pool\n";
    LOAD_STORE_VIOLATION(dstBegin, dstPool)
  }
  if (!pool_find(srcPool, srcBegin, srcEnd)) {
    std::cout << "Source string not found in pool!\n";
    LOAD_STORE_VIOLATION(srcBegin, srcPool)
  }

  // Check if the strings are out of bounds.
  doOOBCheck(dstPool, dstBegin, dstEnd, SRC_INFO_ARGS);
  doOOBCheck(srcPool, srcBegin, srcEnd, SRC_INFO_ARGS);

  // Check if both src and dst are terminated.
  if (!isTerminated(dstBegin, dstEnd, dstLen)) {
    std::cout << "Destination not terminated within bounds\n";
    OOB_VIOLATION(dstBegin, dstPool, dstBegin, dstLen)
  }
  if (!isTerminated(srcBegin, srcEnd, srcLen)) {
    std::cout << "Source not terminated within bounds\n";
    OOB_VIOLATION(srcBegin, srcPool, srcBegin, srcLen)
  }

  // maxLen is the maximum length string dst can hold without going out of bounds
  maxLen  = (char *) dstEnd - (char *) dstBegin;
  // catLen is the length of the string resulting from concatenation.
  catLen  = srcLen + dstLen;

  // Check if the concatenation writes out of bounds.
  if (catLen > maxLen) {
    std::cout << "Concatenation violated destination bounds!\n";
    WRITE_VIOLATION(dstBegin, dstPool, maxLen + 1, catLen + 1)
  }

  // Append at the end of dst so concatenation doesn't have to scan dst again.
  dstNulPosition = &dst[dstLen];
  strncat(dstNulPosition, src, srcLen);

  // strcat returns the destination string.
  return dst;
}

/**
 * Secure runtime wrapper function to replace strncat()
 *
 * @param  dstPool  Pool handle for destination string
 * @param  srcPool  Pool handle for source string
 * @param  dst      Destination string pointer
 * @param  src      Source string pointer
 * @param  n        Number of characters to copy over
 * @return          Destination string pointer
 */
char *pool_strncat(DebugPoolTy *dstPool,
                   DebugPoolTy *srcPool,
                   char *dst,
                   const char *src,
                   size_t n,
                   const unsigned char complete)
{
  return pool_strncat_debug(dstPool, srcPool, dst, src, n, complete,
    DEFAULT_TAG, DEFAULT_SRC_INFO);
}

/**
 * Secure runtime wrapper function to replace strncat()
 *
 * @param  dstPool  Pool handle for destination string
 * @param  srcPool  Pool handle for source string
 * @param  dst      Destination string pointer
 * @param  src      Source string pointer
 * @param  n        Number of characters to copy over
 * @return          Destination string pointer
 */
char *pool_strncat_debug(DebugPoolTy *dstPool,
                         DebugPoolTy *srcPool,
                         char *dst,
                         const char *src,
                         size_t n,
                         const unsigned char complete,
                         TAG,
                         SRC_INFO)
{
  void *dstBegin = (void *) dst, *dstEnd;
  void *srcBegin = (void *) src, *srcEnd;
  size_t dstLen, srcLen, maxLen, catLen, srcAmt;
  char *dstNulPosition;

  // Ensure non-null arguments.
  assert(dstPool && dst && srcPool && src && "Null pool parameters!");

  // Retrieve destination and source strings from pool.
  if (!pool_find(dstPool, dstBegin, dstEnd)) {
    std::cout << "Destination string not found in pool!\n";  
    LOAD_STORE_VIOLATION(dstBegin, dstPool)
  }
  if (!pool_find(srcPool, srcBegin, srcEnd)) {
    std::cout << "Source string not found in pool!\n";  
    LOAD_STORE_VIOLATION(srcBegin, srcPool)
  }

  // Check if strings are in bounds.
  doOOBCheck(dstPool, dstBegin, dstEnd, SRC_INFO_ARGS);
  doOOBCheck(srcPool, srcBegin, srcEnd, SRC_INFO_ARGS);

  // Check if dst is nul terminated.
  if (!isTerminated(dstBegin, dstEnd, dstLen)) {
    std::cout << "String not terminated within bounds\n";
    OOB_VIOLATION(dst, dstPool, dstBegin, dstLen)
  }

  // According to POSIX, src doesn't have to be nul-terminated.
  // If it isn't, ensure strncat that doesn't read beyond the bounds of src.
  if (!isTerminated(srcBegin, srcEnd, srcLen) && srcLen < n) {
    std::cout << "Source object too small\n";
    OOB_VIOLATION(src, srcPool, srcBegin, srcLen)
  }

  // Determine the amount of characters to be copied over from src.
  // This is either n or the length of src, whichever is smaller.
  srcAmt = std::min(srcLen, n);

  // maxLen is the maximum length string dst can hold without overflowing.
  maxLen = (char *) dstEnd - (char *) dstBegin;
  // catLen is the length of the string resulting from the concatenation.
  catLen = srcAmt + dstLen;

  // Check if the copy operation would go beyong the bounds of dst.
  if (catLen > maxLen) {
    std::cout << "Concatenation violated destination bounds!\n";
    WRITE_VIOLATION(dst, dstPool, 1+maxLen, 1+catLen)
  }

  // Start concatenation the end of dst so strncat() doesn't have to scan dst
  // all over again.
  dstNulPosition = &dst[dstLen];
  strncat(dstNulPosition, src, srcAmt);

  // strncat() the returns destination string.
  return dst;
}

/**
 * Secure runtime wrapper function to replace strpbrk()
 *
 * @param   sPool  Pool handle for source string
 * @param   aPool  Pool handle for accept string
 * @param   s      String pointer
 * @param   a      Pointer to string of characters to find
 * @return  Pointer to first instance in s of some character in s, or NULL
 */
char *pool_strpbrk(DebugPoolTy *sp,
                   DebugPoolTy *ap,
                   const char *s,
                   const char *a,
                   const unsigned char complete)
{
  return pool_strpbrk_debug(sp, ap, s, a, complete,
    DEFAULT_TAG, DEFAULT_SRC_INFO);
}

/**
 * Secure runtime wrapper function to replace strpbrk()
 *
 * @param   sPool  Pool handle for source string
 * @param   aPool  Pool handle for accept string
 * @param   s      String pointer
 * @param   a      Pointer to string of characters to find
 * @return  Pointer to first instance in s of some character in s, or NULL
 */
char *pool_strpbrk_debug(DebugPoolTy *sPool,
                         DebugPoolTy *aPool,
                         const char *s,
                         const char *a,
                         const unsigned char complete,
                         TAG,
                         SRC_INFO)
{
  void *sBegin = (void *) s, *sEnd;
  void *aBegin = (void *) a, *aEnd;
  size_t sLen, aLen;

  // Ensure non-null arguments.
  assert(sPool && s && aPool && a && "Null pool parameters!");

  // Retrieve strings from pool.
  if (!pool_find(sPool, sBegin, sEnd)) {
    std::cout << "String not found in pool!\n";
    LOAD_STORE_VIOLATION(sBegin, sPool)
  }
  if (!pool_find(aPool, aBegin, aEnd)) {
    std::cout << "String not found pool!\n";
    LOAD_STORE_VIOLATION(aBegin, aPool)
  }

  // Check if strings fall in bounds.
  doOOBCheck(sPool, sBegin, sEnd, SRC_INFO_ARGS);
  doOOBCheck(aPool, aBegin, aEnd, SRC_INFO_ARGS);

  // Check if strings are terminated.
  if (!isTerminated(sBegin, sEnd, sLen)) {
    std::cout << "String not terminated within bounds!\n";
    OOB_VIOLATION(sBegin, sPool, sBegin, sLen)
  }
  if (!isTerminated(aBegin, aEnd, aLen)) {
    std::cout << "String not terminated within bounds!\n";
    OOB_VIOLATION(aBegin, aPool, aBegin, aLen)
  }

  return strpbrk(s, a);
}

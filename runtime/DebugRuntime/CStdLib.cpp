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

/*#if defined(i386) || defined(__i386__) || defined(__x86__)
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
#else*/
  strncpy(dst, src, size);
  copied = strnlen(dst, size - 1);
//#endif

  return copied;
}

/**
 * Check for string termination.
 *
 * @param start  This is a pointer to the start of the string.
 * @param end    The end of the object. String is not scanned farther than here.
 * @param p      Reference to size object. Filled with the length of the string if
 *               string is terminated, otherwised filled with the size of the object.
 * @return       Returns true if the string is terminated within bounds (ie., 
 *               if the nul terminator occurs between string and end, inclusive).
 *               Returns false if no nul terminator was found.
 *
 * Note that start and end should be valid boundaries for a valid object.
 */
static bool
isTerminated(const char *start, void *end, size_t &p)
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
 *Check for object overlap.
 *
 * @param ptr1Start  The start of the first memory object
 * @param ptr1End    The end of the first memory object or the bound that writing
 *                   operation actually touch.
 * @param ptr2Start  The start of the second memory object
 * @param ptr2End    The end of the second memory object or the bound that writing
 *                   operation actually touch.
 *
 * @return           Whether these 2 memory object overlaps
 */
static bool
isOverlapped(const void* ptr1Start, 
                         const void* ptr1End, 
                         const void* ptr2Start, 
                         const void* ptr2End){
  if( ((long int)ptr1Start>(long int)ptr2End && (long int)ptr1End>(long int)ptr2Start) || 
      ((long int)ptr1Start<(long int)ptr2End && (long int)ptr1End<(long int)ptr2Start)  )
    return false;
  return true;
}

/**
 * Searches inside the given pool for the memory object associated with the
 * the given address. If the memory object is found, it sets the poolBegin
 * and poolEnd pointers to point to the first and last valid positions of
 * the memory object, and returns true. If the memory object is not found in
 * the pool, the function returns false.
 *
 * @param   pool       The pool handle which contains the object.
 * @param   address    The address for which the object bounds are sought.
 * @param   poolBegin  Reference to a pointer to set to the beginning of
 *                     the memory object.
 * @param   poolEnd    Reference to a pointer to set to the last valid address
 *                     the memory object.
 * @return  Returns true if the object was found in the pool, false otherwise.
 */
bool
pool_find(DebugPoolTy *pool, void *address, void *&poolBegin, void *&poolEnd) {
  // Retrieve memory area's bounds from pool handle.
  if (pool->Objects.find(address, poolBegin, poolEnd) || 
      ExternalObjects.find(address, poolBegin, poolEnd))
    return true;

  return false;
}

/* Macros for determining the completeness of pointers using the completeness
   bitwise vector. */
#define ARG1_COMPLETE(c) ((bool) c & 0x1)
#define ARG2_COMPLETE(c) ((bool) c & 0x2)

/**
 * This function attempts to verify that given string pointer points to
 * a valid string that is terminated within its memory object's boundaries.
 * For strings that are marked complete, if the string is found to be
 * not in its pool, NULL, or unterminated within memory object boundaries,
 * the function reports a violation and returns false.
 * For strings not marked complete, the function attempts to do the same
 * checks as for complete pointers, except that it assumes the string was
 * valid if the string is not found in the pool and is non-NULL.
 *
 * The function returns true if no memory violations were discovered, and
 * false when there was a violation.
 *
 * @param   string     The pointer to the string to be checked.
 * @param   pool       The pool that should be searched for the memory object
 *                     that contains the string. This is required to be non-NULL
 *                     if the string pointer is non-NULL.
 * @param   complete   This is a boolean value which is true if the string
 *                     pointer was reported complete by DSA, and false if not.
 *                     If the string is incomplete, no errors are reported
 *                     if it does not exist in the pool and is non-NULL.
 * @param   function   The name of the C library function for debug reporting
 *                     purposes.
 * @param   TAG        Tag information (?)
 * @param   SRC_INFO   Source and line info debug information.
 * @return             Returns true if no violations were discoverd, and false
 *                     if the pointer does not point to a valid string and a 
 *                     memory violation was reported.
 *                     Note that if the function returns true, the pointer may
 *                     still not point to a valid string if the pointer was
 *                     incomplete.
 *
 */
static inline bool
validStringCheck(const char *string,
                 DebugPoolTy *pool,
                 bool complete,
                 const char *function,
                 SRC_INFO) {
  void *objStart, *objEnd;
  size_t len;

  // Check if the string is NULL. If it is, report this as an error.
  if (string == NULL) {
    std::cout << "String pointer is NULL!\n";
    OOB_VIOLATION(0, pool, 0, 0);
    return false;
  }

  // The pool handle is required to be non-NULL for non-NULL strings.
  assert(pool && "Null pool handle!");
  
  // Retrieve the string from the pool. If no string is found and the pointer
  // is not complete, return true. Otherwise report an error and return false.
  if (!pool_find(pool, (void *)string, objStart, objEnd)) {
    if (complete) {
      std::cout << "String not found in pool!\n";
      LOAD_STORE_VIOLATION(string, pool);
      return false;
    }
    else
      return true;
  }

  // Do a termination check.
  if (!isTerminated(string, objEnd, len)) {
    std::cout << "String is not terminated within object bounds!\n";
    OOB_VIOLATION(string, pool, objStart, 1 + (char*)objEnd - (char*)objStart);
    return false;
  }

  return true;
}


/**
 * Secure runtime wrapper function to replace strchr()
 *
 * @param   sp     Pool handle for string
 * @param   s      String pointer
 * @param   c      Character to find
 * @return  Pointer to first instance of c in s or NULL
 */

char *
pool_strchr(DebugPoolTy *sp, const char *s, int c, unsigned char complete)
{
  return pool_strchr_debug(sp, s, c, complete, DEFAULT_TAG, DEFAULT_SRC_INFO);
}

/**
 * Secure runtime wrapper function to replace strchr()
 *
 * @param   sPool  Pool handle for string
 * @param   s      String pointer
 * @param   c      Character to find
 * @return  Pointer to first instance of c in s or NULL
 */
char *
pool_strchr_debug(DebugPoolTy *sPool,
                  const char *s,
                  int c,
                  unsigned char complete,
                  TAG,
                  SRC_INFO)
{
  validStringCheck(s, sPool, ARG1_COMPLETE(complete), "strchr", SRC_INFO_ARGS);
  return strchr((char*)s, c);
}

/**
 * Secure runtime wrapper function to replace strrchr()
 *
 * @param   sPool  Pool handle for string
 * @param   s      String pointer
 * @param   c      Character to find
 * @return  Pointer to last instance of c in s or NULL
 */
char *
pool_strrchr(DebugPoolTy *sPool,
             const char *s,
             int c,
             const unsigned char complete)
{
  return pool_strrchr_debug(sPool, s, c, complete, DEFAULT_TAG,
    DEFAULT_SRC_INFO);
}


/**
 * Secure runtime wrapper function to replace strrchr()
 *
 * @param   sPool  Pool handle for string
 * @param   s      String pointer
 * @param   c      Character to find
 * @return  Pointer to last instance of c in s or NULL
 */
char *
pool_strrchr_debug(DebugPoolTy *sPool,
                   const char *s,
                   int c,
                   const unsigned char complete,
                   TAG,
                   SRC_INFO)
{
  validStringCheck(s, sPool, ARG1_COMPLETE(complete), "strrchr", SRC_INFO_ARGS);
  return strrchr((char*)s, c);
}

char *
pool_strstr(DebugPoolTy *s1Pool,
            DebugPoolTy *s2Pool,
            const char *s1,
            const char *s2,
            unsigned char complete)
{
  return pool_strstr_debug(s1Pool, s2Pool, s1, s2, complete, DEFAULT_TAG,
    DEFAULT_SRC_INFO);
}

char *
pool_strstr_debug(DebugPoolTy *s1Pool,
                  DebugPoolTy *s2Pool,
                  const char *s1,
                  const char *s2,
                  unsigned char complete,
                  TAG,
                  SRC_INFO)
{
  validStringCheck(s1, s1Pool, ARG1_COMPLETE(complete), "strstr",
    SRC_INFO_ARGS);
  validStringCheck(s2, s2Pool, ARG2_COMPLETE(complete), "strstr",
    SRC_INFO_ARGS);
  return strstr((char*)s1, s2);
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
char *
pool_strcat(DebugPoolTy *dp,
            DebugPoolTy *sp,
            char *d,
            const char *s,
            const unsigned char c)
{
  return pool_strcat_debug(dp, sp, d, s, c, DEFAULT_TAG, DEFAULT_SRC_INFO);
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
char *
pool_strcat_debug(DebugPoolTy *dstPool,
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
  bool terminated = true;

  // Ensure non-null pool and string arguments.
  assert(dstPool && dst && srcPool && src && "Null pool parameters!");

  // Find the strings in the pool.
  if (!pool_find(dstPool, (void*)dst, dstBegin, dstEnd)) {
    std::cout << "Destination string not found in pool\n";
    LOAD_STORE_VIOLATION(dstBegin, dstPool);
  }
  if (!pool_find(srcPool, (void*)src, srcBegin, srcEnd)) {
    std::cout << "Source string not found in pool!\n";
    LOAD_STORE_VIOLATION(srcBegin, srcPool);
  }

  // Check if both src and dst are terminated.
  if (!isTerminated(dst, dstEnd, dstLen)) {
    terminated = false;
    std::cout << "Destination not terminated within bounds\n";
    OOB_VIOLATION(dst, dstPool, dst, dstLen);
    //C_LIBRARY_VIOLATION(dst, dstPool, "strcat");
  }
  if (!isTerminated(src, srcEnd, srcLen)) {
    terminated = false;
    std::cout << "Source not terminated within bounds\n";
    OOB_VIOLATION(src, srcPool, src, srcLen);
    //C_LIBRARY_VIOLATION(src, srcPool, "strcat");
  }

  // If both src and dst are terminated, check for overlap.
  // Overlap occurs exactly when they share the same nul terminator in memory.
  if (terminated && &dst[dstLen] == &src[srcLen]) {
    std::cout << "Concatenating overlapping strings is undefined\n";
    OOB_VIOLATION(dst, dstPool, dst, dstLen);
    //C_LIBRARY_VIOLATION(dst, dstPool, "strcat");
  }

  // maxLen is the maximum length string dst can hold without going out of bounds
  maxLen  = (char *) dstEnd - dst;
  // catLen is the length of the string resulting from concatenation.
  catLen  = srcLen + dstLen;

  // Check if the concatenation writes out of bounds.
  if (catLen > maxLen) {
    std::cout << "Concatenation violated destination bounds!\n";
    WRITE_VIOLATION(dstBegin, dstPool, maxLen + 1, catLen + 1);
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
char *
pool_strncat(DebugPoolTy *dstPool,
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
char *
pool_strncat_debug(DebugPoolTy *dstPool,
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
  bool dst_terminated = true;

  // Ensure non-null arguments.
  assert(dstPool && dst && srcPool && src && "Null pool parameters!");

  // Retrieve destination and source strings from pool.
  if (!pool_find(dstPool, (void*)dst, dstBegin, dstEnd)) {
    std::cout << "Destination string not found in pool!\n";  
    LOAD_STORE_VIOLATION(dstBegin, dstPool)
  }
  if (!pool_find(srcPool, (void*)src, srcBegin, srcEnd)) {
    std::cout << "Source string not found in pool!\n";  
    LOAD_STORE_VIOLATION(srcBegin, srcPool)
  }

  // Check if dst is nul terminated.
  if (!isTerminated(dst, dstEnd, dstLen)) {
    dst_terminated = false;
    std::cout << "String not terminated within bounds\n";
    OOB_VIOLATION(dst, dstPool, dst, dstLen);
    //C_LIBRARY_VIOLATION(dst, dstPool, "strncat");
  }

  // According to POSIX, src doesn't have to be nul-terminated.
  // If it isn't, ensure strncat that doesn't read beyond the bounds of src.
  if (!isTerminated(src, srcEnd, srcLen) && srcLen < n) {
    std::cout << "Source object too small\n";
    OOB_VIOLATION(src, srcPool, src, srcLen);
    //C_LIBRARY_VIOLATION(dst, dstPool, "strncat");
  }

  // Determine the amount of characters to be copied over from src.
  // This is either n or the length of src, whichever is smaller.
  srcAmt = std::min(srcLen, n);

  // Check for undefined behavior due to overlapping objects.
  // Overlap occurs when dst and src are in the same object, and the
  // characters to be copied from src end inside the dst string.
  if (dst_terminated && srcBegin == dstBegin &&
        dst < &src[srcAmt] && &src[srcAmt] <= &dst[dstLen]) {
    std::cout << "Concatenating overlapping objects is undefined\n";
    OOB_VIOLATION(dst, dstPool, dst, dstLen);
    //C_LIBRARY_VIOLATION(src, srcPool, "strncat");
  }

  // maxLen is the maximum length string dst can hold without overflowing.
  maxLen = (char *) dstEnd - dst;
  // catLen is the length of the string resulting from the concatenation.
  catLen = srcAmt + dstLen;

  // Check if the copy operation would go beyong the bounds of dst.
  if (catLen > maxLen) {
    std::cout << "Concatenation violated destination bounds!\n";
    WRITE_VIOLATION(dst, dstPool, 1+maxLen, 1+catLen);
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
char *
pool_strpbrk(DebugPoolTy *sp,
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
char *
pool_strpbrk_debug(DebugPoolTy *sPool,
                   DebugPoolTy *aPool,
                   const char *s,
                   const char *a,
                   const unsigned char complete,
                   TAG,
                   SRC_INFO)
{
  validStringCheck(s, sPool, ARG1_COMPLETE(complete), "strpbrk",
    DEFAULT_SRC_INFO);
  validStringCheck(a, aPool, ARG2_COMPLETE(complete), "strpbrk",
    DEFAULT_SRC_INFO);
  return strpbrk((char*)s, a);
}

/**
 * Secure runtime wrapper function to replace strcmp()
 *
 * @param   str1Pool Pool handle for str1
 * @param   str2Pool Pool handle for str2
 * @param   str1     c string to be compared
 * @param   str2     c string to be compared
 * @return  position of first different character, or 0 it they are the same
 */
int
pool_strcmp(DebugPoolTy *s1p,
            DebugPoolTy *s2p, 
            const char *s1, 
            const char *s2,
            const unsigned char complete){
  return pool_strcmp_debug(s1p,s2p,s1,s2,complete, DEFAULT_TAG, DEFAULT_SRC_INFO);
}
/**
 * Secure runtime wrapper function to replace strcmp()
 *
 * @param   str1Pool Pool handle for str1
 * @param   str2Pool Pool handle for str2
 * @param   str1     c string to be compared
 * @param   str2     c string to be compared
 * @return  position of first different character, or 0 it they are the same
 */
int
pool_strcmp_debug(DebugPoolTy *str1Pool,
                  DebugPoolTy *str2Pool, 
                  const char *str1, 
                  const char *str2,
                  const unsigned char complete,
                  TAG,
                  SRC_INFO) {

  size_t str1Size = 0, str2Size = 0;
  void *str1Begin =(void*)str1, *str1End = NULL, *str2Begin = (void*)str2, *str2End = NULL;

  assert(str1Pool && str2Pool && str2 && str1 && "Null pool parameters!");

  if (!pool_find(str1Pool, (void*)str1, str1Begin, str1End)) {
    std::cout << "String 1 not found in pool!\n";
    LOAD_STORE_VIOLATION(str1Begin, str1Pool)
  }
  if (!pool_find(str2Pool, (void*)str2, str2Begin, str2End)) {
    std::cout << "String 2 not found in pool!\n";
    LOAD_STORE_VIOLATION(str1Begin, str1Pool)
  }

  // Check if strings are terminated.
  if (!isTerminated(str1, str1End, str1Size)) {
    std::cout << "String 1 not terminated within bounds!\n";
    OOB_VIOLATION(str1Begin, str1Pool, str1Begin, str1Size)
  }
  if (!isTerminated(str2, str2End, str2Size)) {
    std::cout << "String 2 not terminated within bounds!\n";
    OOB_VIOLATION(str2Begin, str2Pool, str2Begin, str2Size)
  }

  return strcmp(str1, str2);
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
void *
pool_memcpy(DebugPoolTy *dstPool, 
            DebugPoolTy *srcPool, 
            void *dst, 
            const void *src, 
            size_t n,
            const unsigned char complete) {
  return pool_memcpy_debug(dstPool,srcPool,dst,src, n,complete,DEFAULT_TAG, DEFAULT_SRC_INFO);
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
void *
pool_memcpy_debug(DebugPoolTy *dstPool, 
                  DebugPoolTy *srcPool, 
                  void *dst, 
                  const void *src, 
                  size_t n,
                  const unsigned char complete,
                  TAG,
                  SRC_INFO){

  size_t dstSize = 0, srcSize = 0;
  void *dstBegin = dst, *dstEnd = NULL, *srcBegin = (char *)src, *srcEnd = NULL;

  assert(dstPool && srcPool && dst && src && "Null pool parameters!");

  // Retrieve both the destination and source buffer's bounds from the pool handle.
  if(!pool_find(dstPool, (void*)dst, dstBegin, dstEnd)){
    std::cout<<"Memory object not found in pool!\n";
    LOAD_STORE_VIOLATION(dst,dstPool);
  }
  if(!pool_find(srcPool, (void*)src, srcBegin, srcEnd)){
    std::cout<<"Memory object not found in pool!\n";
    LOAD_STORE_VIOLATION(src,srcPool);
  }

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - (char *)dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;

  if (n > srcSize || n > dstSize) {
    std::cout << "Cannot copy more bytes than the size of the source!\n";
    WRITE_VIOLATION(srcBegin, srcPool, dstSize, srcSize);
  }

  if(isOverlapped(dst,(const char*)dst+n-1,src,(const char*)src+n-1)){ 
    std::cout<<"Two memory objects overlap each other!/n";
    LOAD_STORE_VIOLATION(dst,dstPool);
  }

  memcpy(dst, src, n);

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
void *
pool_memmove(DebugPoolTy *dstPool, 
             DebugPoolTy *srcPool, 
             void *dst, 
             const void *src, 
             size_t n,
             const unsigned char complete) {
  return pool_memmove_debug(dstPool,srcPool,dst,src, n,complete, DEFAULT_TAG, DEFAULT_SRC_INFO);
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
void *
pool_memmove_debug(DebugPoolTy *dstPool, 
                   DebugPoolTy *srcPool, 
                   void *dst, 
                   const void *src, 
                   size_t n,
                   const unsigned char complete,
                   TAG,
                   SRC_INFO){

  size_t dstSize = 0, srcSize = 0, stop = 0;
  void *dstBegin = dst, *dstEnd = NULL, *srcBegin = (char *)src, *srcEnd = NULL;

  assert(dstPool && srcPool && dst && src && "Null pool parameters!");

  // Retrieve both the destination and source buffer's bounds from the pool handle.
  if(!pool_find(dstPool, (void*)dst, dstBegin, dstEnd)){
    std::cout<<"Memory object not found in pool!\n";
    LOAD_STORE_VIOLATION(dst,dstPool);
  }
  if(!pool_find(srcPool, (void*)src, srcBegin, srcEnd)){
    std::cout<<"Memory object not found in pool!\n";
    LOAD_STORE_VIOLATION(src,srcPool);
  }

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - (char *)dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;
  stop = std::min(n, srcSize);
  if (n > srcSize || n > dstSize) {
    std::cout << "Cannot copy more bytes than the size of the source!\n";
    WRITE_VIOLATION(srcBegin, srcPool, dstSize, srcSize);
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
 * @return  Byte following the last written byte
 */
void *
pool_mempcpy(DebugPoolTy *dstPool, 
             DebugPoolTy *srcPool, 
             void *dst, 
             const void *src, 
             size_t n,
             const unsigned char complete) {
  return pool_mempcpy_debug(dstPool,srcPool,dst,src, n,complete,DEFAULT_TAG, DEFAULT_SRC_INFO);
}


/**
 * Secure runtime wrapper function to replace mempcpy()
 *
 * @param   dstPool  Pool handle for destination memory area
 * @param   srcPool  Pool handle for source memory area
 * @param   dst      Destination memory area
 * @param   src      Source memory area
 * @param   n        Maximum number of bytes to copy
 * @return  Byte following the last written byte
 */
#if !defined(__APPLE__)
void *
pool_mempcpy_debug(DebugPoolTy *dstPool, 
                   DebugPoolTy *srcPool, 
                   void *dst, 
                   const void *src, 
                   size_t n,
                   const unsigned char complete,
                   TAG,
                   SRC_INFO){

  size_t dstSize = 0, srcSize = 0;
  void *dstBegin = dst, *dstEnd = NULL, *srcBegin = (char *)src, *srcEnd = NULL;

  assert(dstPool && srcPool && dst && src && "Null pool parameters!");

  // Retrieve both the destination and source buffer's bounds from the pool handle.
  if(!pool_find(dstPool, (void*)dst, dstBegin, dstEnd)){
    std::cout<<"Memory object not found in pool!\n";
    LOAD_STORE_VIOLATION(dst,dstPool);
  }
  if(!pool_find(srcPool, (void*)src, srcBegin, srcEnd)){
    std::cout<<"Memory object not found in pool!\n";
    LOAD_STORE_VIOLATION(src,srcPool);
  }

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - (char *)dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;
  // Check that copy size is too big
  if (n > srcSize || n > dstSize) {
    std::cout << "Cannot copy more bytes than the size of the source!\n";
    WRITE_VIOLATION(srcBegin, srcPool, dstSize, srcSize);
  }
  // Check if two memory object overlap
  if(isOverlapped(dst,(const char*)dst+n-1,src,(const char*)src+n-1)){ 
    std::cout<<"Two memory objects overlap each other!/n";
    LOAD_STORE_VIOLATION(dst,dstPool);
  }

 
  return  mempcpy(dst, src, n);
}
#endif

/**
 * Secure runtime wrapper function to replace memset()
 *
 * @param   stringPool  Pool handle for the string
 * @param   string      String pointer
 * @param   c           an int value to be set
 * @param   n           number of bytes to be set
 * @return  Pointer to memory area
 */
void *pool_memset(DebugPoolTy *stringPool, 
             void *string, 
             int c, 
             size_t n,
             const unsigned char complete) {
  return pool_memset_debug(stringPool, string, c, n,complete, DEFAULT_TAG, DEFAULT_SRC_INFO);

}
/**
 * Secure runtime wrapper function to replace memset()
 *
 * @param   stringPool  Pool handle for the string
 * @param   string      String pointer
 * @param   c           an int value to be set
 * @param   n           number of bytes to be set
 * @return  Pointer to memory area
 */
void *
pool_memset_debug(DebugPoolTy *stringPool, 
                  void *string, 
                  int c, 
                  size_t n,
                  const unsigned char complete,
                  TAG,
                  SRC_INFO){
  size_t stringSize = 0;
  void *stringBegin = string, *stringEnd = NULL;

  assert(stringPool && string && "Null pool parameters!");

  // Retrieve both the destination and source buffer's bounds from the pool handle.
  if(!pool_find(stringPool, (void*)string, stringBegin, stringEnd)){
    std::cout<<"Memory object not found in pool!\n";
    LOAD_STORE_VIOLATION(string,stringPool);
  }

  stringSize = (char *)stringEnd - (char *)string + 1;
  if (n > stringSize) {
    std::cout << "Cannot write more bytes than the size of the destination string!\n";
    WRITE_VIOLATION(stringBegin, stringPool, stringSize, 0);
  }
  return memset(string, c, n);
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
char *
pool_strcpy(DebugPoolTy *dstPool, 
            DebugPoolTy *srcPool, 
            char *dst, 
            const char *src, 
            const unsigned char complete) {
  return pool_strcpy_debug(dstPool, srcPool, dst, src, complete, DEFAULT_TAG, DEFAULT_SRC_INFO);
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
char *
pool_strcpy_debug(DebugPoolTy *dstPool, 
                  DebugPoolTy *srcPool, 
                  char *dst, 
                  const char *src, 
                  const unsigned char complete, 
                  TAG,
                  SRC_INFO) {
  size_t dstSize = 0, srcSize = 0, len = 0;
  void *dstBegin = dst, *dstEnd = NULL, *srcBegin = (char *)src, *srcEnd = NULL;

  // Ensure all valid pointers.
  assert(dstPool && srcPool && dst && src && "Null pool parameters!");
  
  // Retrieve both the destination and source buffer's bounds from the pool handle.
  if(!pool_find(dstPool, (void*)dst, dstBegin, dstEnd)){
    std::cout<<"Memory object not found in pool!\n";
    LOAD_STORE_VIOLATION(dst,dstPool);
  }
  if(!pool_find(srcPool, (void*)src, srcBegin, srcEnd)){
    std::cout<<"Memory object not found in pool!\n";
    LOAD_STORE_VIOLATION(src,srcPool);
  }

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - (char *)dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;
  len = strnlen(src,srcSize);

  if (len == srcSize) {
    std::cout << "Source string is not NULL terminated!\n";
    OOB_VIOLATION(src,srcPool,src,len)
  }

  if (len+1 > dstSize) {
    std::cout << "Cannot copy more bytes than the size of the source!\n";
    WRITE_VIOLATION(dstBegin, dstPool, dstSize, srcSize);
  }

  if(isOverlapped(dst,(const char*)dst+len,src,(const char*)src+len)){ 
    std::cout<<"Two memory objects overlap each other!\n";
    LOAD_STORE_VIOLATION(dst,dstPool);
  }

  strncpy(dst, src, len+1);

  return dst;
}

/**
 * Secure runtime wrapper function to replace strlen()
 *
 * @param   stringPool  Pool handle for the string
 * @param   string      String pointer
 * @return  Length of the string
 */
size_t
pool_strlen(DebugPoolTy *stringPool, 
            const char *string, 
            const unsigned char complete) {
  return pool_strlen_debug(stringPool, string, 
    complete, DEFAULT_TAG, DEFAULT_SRC_INFO);
}

/**
 * Secure runtime wrapper function to replace strlen()
 *
 * @param   stringPool  Pool handle for the string
 * @param   string      String pointer
 * @return  Length of the string
 */
size_t
pool_strlen_debug(DebugPoolTy *stringPool, 
                  const char *string, 
                  const unsigned char complete, 
                  TAG, 
                  SRC_INFO) {
  size_t len = 0, maxlen = 0;
  void *stringBegin = (char *)string, *stringEnd = NULL;

  assert(stringPool && string && "Null pool parameters!");
  // Retrieve the string bound from pool
  if(!pool_find(stringPool, (void*)string, stringBegin, stringEnd)){
    std::cout<<"Memory object not found in pool!\n";
    LOAD_STORE_VIOLATION(string,stringPool);
  }
  
  // Null termination check
  maxlen = (char *)stringEnd - (char *)string+1;
  len = strnlen(string, maxlen);

  if (len  == maxlen) {
    std::cout << "String not terminated within bounds!\n";
    OOB_VIOLATION(string,stringPool,string,len)
  }

  return len;
}

//// FIXME: WHen it is tested, compiled program outputs "Illegal Instruction"
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
char *
pool_strncpy(DebugPoolTy *dstPool, 
             DebugPoolTy *srcPool, 
             char *dst, 
             const char *src, 
             size_t n,
             const unsigned char complete){
  return pool_strncpy_debug(dstPool,srcPool,dst,src, n,
    complete,DEFAULT_TAG, DEFAULT_SRC_INFO);
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
char *
pool_strncpy_debug(DebugPoolTy *dstPool, 
                   DebugPoolTy *srcPool, 
                   char *dst, 
                   const char *src, 
                   size_t n, 
                   const unsigned char complete, 
                   TAG, 
                   SRC_INFO){
  size_t dstSize = 0, srcSize = 0, stop = 0;
  void *dstBegin = dst, *dstEnd = NULL, *srcBegin = (char *)src, *srcEnd = NULL;

  assert(dstPool && srcPool && dst && src && "Null pool parameters!");

 // Retrieve both the destination and source buffer's bounds from the pool handle.
  if(!pool_find(dstPool, (void*)dst, dstBegin, dstEnd)){
    std::cout<<"Memory object not found in pool!\n";
    LOAD_STORE_VIOLATION(dst,dstPool);
  }
  if(!pool_find(srcPool, (void*)src, srcBegin, srcEnd)){
    std::cout<<"Memory object not found in pool!\n";
    LOAD_STORE_VIOLATION(src,srcPool);
  }

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - (char *)dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;
  stop = strnlen(src,srcSize);
  // If source string is not bounded and copy length is longer than the source object
  // Behavior is undefined
  if (stop==srcSize && n > srcSize) {
    std::cout << "String is not bounded and copy length is out of bound!\n";
    WRITE_VIOLATION(srcBegin, srcPool, dstSize, srcSize);
  }
  // Check if destination will be overflowed
  if (n > dstSize) {
    std::cout << "Cannot copy more bytes than the size of the source!\n";
    WRITE_VIOLATION(srcBegin, srcPool, dstSize, srcSize);
  }
  // Check if two strings are over lapped
  if(isOverlapped(dst,(const char*)dst+stop-1,src,(const char*)src+stop-1)){ 
    std::cout<<"Two memory objects overlap each other!/n";
    LOAD_STORE_VIOLATION(dst,dstPool);
  }
  // Copy string 
  strncpy_asm(dst, src, stop+1);
  // Check whether result string is NULL terminated
  if(!isTerminated(dst,dstEnd,stop)){
    std::cout<<"NULL terminator is not copied!\n";
    OOB_VIOLATION(dst,dstPool,dst,stop);
  }
  // Pad with zeros
  memset(dst+stop+1,0,n-stop-1);

  return dst;
}

/**
 * Secure runtime wrapper function to replace strnlen()
 *
 * @param   stringPool  Pool handle for the string
 * @param   string      String pointer
 * @return  Length of the string
 */
size_t
pool_strnlen(DebugPoolTy *stringPool, 
             const char *string, 
             size_t maxlen,
             const unsigned char complete) {
  return pool_strnlen_debug(stringPool, string, maxlen, 
    complete, DEFAULT_TAG, DEFAULT_SRC_INFO);
}

/**
 * Secure runtime wrapper function to replace strnlen()
 *
 * @param   stringPool  Pool handle for the string
 * @param   string      String pointer
 * @return  Length of the string
 */
size_t
pool_strnlen_debug(DebugPoolTy *stringPool, 
                   const char *string, 
                   size_t maxlen, 
                   const unsigned char complete, 
                   TAG, 
                   SRC_INFO) {
  size_t len = 0, difflen = 0;
  void *stringBegin = (char *)string, *stringEnd = NULL;

  assert(stringPool && string && "Null pool parameters!");
  // Retrieve string from the pool
  if(!pool_find(stringPool, (void*)string, stringBegin, stringEnd)){
    std::cout<<"String not found in pool!\n";
    LOAD_STORE_VIOLATION(string,stringPool);
  }

  difflen = (char *)stringEnd - (char *)string +1;
  len = strnlen(string, difflen);
  // If the string is not terminated within range and maxlen is bigger than object size
  if(maxlen > len && len==difflen){
    std::cout<<"String is not bounded!\n";
    OOB_VIOLATION(string,stringPool,string,difflen);
  }
  return len;
}

/**
 * Secure runtime wrapper function to replace strncmp()
 *
 * @param   str1Pool Pool handle for string1
 * @param   str2Pool Pool handle for string2
 * @param   num      Maximum number of chars to compare
 * @param   str1     string1 to be compared
 * @param   str2     string2 to be compared
 * @return  position of first different character, or 0 it they are the same
 */
int
pool_strncmp(DebugPoolTy *s1p,
             DebugPoolTy *s2p, 
             const char *s1, 
             const char *s2,
             size_t num,
             const unsigned char complete){
  return pool_strncmp_debug(s1p,s2p,s1,s2,num,complete, DEFAULT_TAG, DEFAULT_SRC_INFO);
}

/**
 * Secure runtime wrapper function to replace strncmp()
 *
 * @param   str1Pool Pool handle for string1
 * @param   str2Pool Pool handle for string2
 * @param   num      Maximum number of chars to compare
 * @param   str1     string1 to be compared
 * @param   str2     string2 to be compared
 * @return  position of first different character, or 0 it they are the same
 */
int
pool_strncmp_debug(DebugPoolTy *str1Pool,
                   DebugPoolTy *str2Pool, 
                   const char *str1, 
                   const char *str2,
                   size_t num,
                   const unsigned char complete,
                   TAG,
                   SRC_INFO) {

  size_t str1Size = 0, str2Size = 0;
  void *str1Begin =(void*)str1, *str1End = NULL, *str2Begin = (void*)str2, *str2End = NULL;

  assert(str1Pool && str2Pool && str2 && str1 && "Null pool parameters!");

  if (!pool_find(str1Pool, (void*)str1, str1Begin, str1End)) {
    std::cout << "String 1 not found in pool!\n";
    LOAD_STORE_VIOLATION(str1Begin, str1Pool)
  }
  if (!pool_find(str2Pool, (void*)str2, str2Begin, str2End)) {
    std::cout << "String 2 not found in pool!\n";
    LOAD_STORE_VIOLATION(str1Begin, str1Pool)
  }

  str1Size = (char*) str1End - str1+1;
  str2Size = (char*) str2End - str2+1;  

  if (str1Size<num) {
    std::cout << "Possible read out of bound in string1!\n";
    OOB_VIOLATION(str1Begin, str1Pool, str1Begin, str1Size)
  }
  if (str2Size<num) {
    std::cout << "Possible read out of bound in string2!\n";
    OOB_VIOLATION(str2Begin, str2Pool, str2Begin, str2Size)
  }

  return strncmp(str1, str2,num);
}

/**
 * Secure runtime wrapper function to replace memcmp()
 *
 * @param   str1Pool Pool handle for memory object1
 * @param   str2Pool Pool handle for memory object1
 * @param   num      Maximum number of bytes to compare
 * @param   str1     memory object1 to be compared
 * @param   str2     memory object2 to be compared
 * @return  position of first different character, or 0 it they are the same
 */
int
pool_memcmp(DebugPoolTy *s1p,
            DebugPoolTy *s2p, 
            const void *s1, 
            const void *s2,
            size_t num,
            const unsigned char complete){
  return pool_memcmp_debug(s1p,s2p,s1,s2,num,complete, DEFAULT_TAG, DEFAULT_SRC_INFO);
}

/**
 * Secure runtime wrapper function to replace memcmp()
 *
 * @param   str1Pool Pool handle for memory object1
 * @param   str2Pool Pool handle for memory object1
 * @param   num      Maximum number of bytes to compare
 * @param   str1     memory object1 to be compared
 * @param   str2     memory object2 to be compared
 * @return  position of first different character, or 0 it they are the same
 */
int
pool_memcmp_debug(DebugPoolTy *str1Pool,
                  DebugPoolTy *str2Pool, 
                  const void *str1, 
                  const void *str2,
                  size_t num,
                  const unsigned char complete,
                  TAG,
                  SRC_INFO) {

  size_t str1Size = 0, str2Size = 0;
  void *str1Begin =(void*)str1, *str1End = NULL, *str2Begin = (void*)str2, *str2End = NULL;

  assert(str1Pool && str2Pool && str2 && str1 && "Null pool parameters!");

  if (!pool_find(str1Pool, (void*)str1, str1Begin, str1End)) {
    std::cout << "String 1 not found in pool!\n";
    LOAD_STORE_VIOLATION(str1Begin, str1Pool)
  }
  if (!pool_find(str2Pool, (void*)str2, str2Begin, str2End)) {
    std::cout << "String 2 not found in pool!\n";
    LOAD_STORE_VIOLATION(str1Begin, str1Pool)
  }

  str1Size = (char*) str1End - (char*)str1+1;
  str2Size = (char*) str2End - (char*)str2+1;  

  if (str1Size<num) {
    std::cout << "Possible read out of bound in string1!\n";
    OOB_VIOLATION(str1Begin, str1Pool, str1Begin, str1Size)
  }
  if (str2Size<num) {
    std::cout << "Possible read out of bound in string2!\n";
    OOB_VIOLATION(str2Begin, str2Pool, str2Begin, str2Size)
  }

  return memcmp(str1, str2,num);
}

/**
 * Secure runtime wrapper function to replace strncasecmp()
 *
 * @param   str1Pool Pool handle for string1
 * @param   str2Pool Pool handle for string2
 * @param   num      Maximum number of chars to compare
 * @param   str1     string1 to be compared
 * @param   str2     string2 to be compared
 * @return  position of first different character, or 0 it they are the same
 */
int
pool_strncasecmp(DebugPoolTy *s1p,
                 DebugPoolTy *s2p, 
                 const char *s1, 
                 const char *s2,
                 size_t num,
                 const unsigned char complete){

  return pool_strncasecmp_debug(s1p,s2p,s1,s2,num,complete, DEFAULT_TAG, DEFAULT_SRC_INFO);
}

/**
 * Secure runtime wrapper function to replace strncasecmp()
 *
 * @param   str1Pool Pool handle for string1
 * @param   str2Pool Pool handle for string2
 * @param   num      Maximum number of chars to compare
 * @param   str1     string1 to be compared
 * @param   str2     string2 to be compared
 * @return  position of first different character, or 0 it they are the same
 */
int
pool_strncasecmp_debug(DebugPoolTy *str1Pool,
                       DebugPoolTy *str2Pool, 
                       const char *str1, 
                       const char *str2,
                       size_t num,
                       const unsigned char complete,
                       TAG,
                       SRC_INFO) {
  size_t str1Size = 0, str2Size = 0;
  void *str1Begin =(void*)str1, *str1End = NULL, *str2Begin = (void*)str2, *str2End = NULL;

  assert(str1Pool && str2Pool && str2 && str1 && "Null pool parameters!");

  if (!pool_find(str1Pool, (void*)str1, str1Begin, str1End)) {
    std::cout << "String 1 not found in pool!\n";
    LOAD_STORE_VIOLATION(str1Begin, str1Pool)
  }
  if (!pool_find(str2Pool, (void*)str2, str2Begin, str2End)) {
    std::cout << "String 2 not found in pool!\n";
    LOAD_STORE_VIOLATION(str1Begin, str1Pool)
  }

  str1Size = (char*) str1End - str1+1;
  str2Size = (char*) str2End - str2+1;  

  if (str1Size<num) {
    std::cout << "Possible read out of bound in string1!\n";
    OOB_VIOLATION(str1Begin, str1Pool, str1Begin, str1Size)
  }
  if (str2Size<num) {
    std::cout << "Possible read out of bound in string2!\n";
    OOB_VIOLATION(str2Begin, str2Pool, str2Begin, str2Size)
  }

  return strncasecmp(str1, str2,num);
}

/**
 * Secure runtime wrapper function to replace strcasecmp()
 *
 * @param   str1Pool Pool handle for str1
 * @param   str2Pool Pool handle for str2
 * @param   str1     c string to be compared
 * @param   str2     c string to be compared
 * @return  position of first different character, or 0 it they are the same
 */
int
pool_strcasecmp(DebugPoolTy *s1p,
                DebugPoolTy *s2p, 
                const char *s1, 
                const char *s2,
                const unsigned char complete){

  return pool_strcasecmp_debug(s1p,s2p,s1,s2,complete, DEFAULT_TAG, DEFAULT_SRC_INFO);
}
/**
 * Secure runtime wrapper function to replace strcasecmp()
 *
 * @param   str1Pool Pool handle for str1
 * @param   str2Pool Pool handle for str2
 * @param   str1     c string to be compared
 * @param   str2     c string to be compared
 * @return  position of first different character, or 0 it they are the same
 */
int
pool_strcasecmp_debug(DebugPoolTy *str1Pool,
                      DebugPoolTy *str2Pool, 
                      const char *str1, 
                      const char *str2,
                      const unsigned char complete,
                      TAG,
                      SRC_INFO) {

  size_t str1Size = 0, str2Size = 0;
  void *str1Begin =(void*)str1, *str1End = NULL, *str2Begin = (void*)str2, *str2End = NULL;

  assert(str1Pool && str2Pool && str2 && str1 && "Null pool parameters!");

  if (!pool_find(str1Pool, (void*)str1, str1Begin, str1End)) {
    std::cout << "String 1 not found in pool!\n";
    LOAD_STORE_VIOLATION(str1Begin, str1Pool)
  }
  if (!pool_find(str2Pool, (void*)str2, str2Begin, str2End)) {
    std::cout << "String 2 not found in pool!\n";
    LOAD_STORE_VIOLATION(str1Begin, str1Pool)
  }

  // Check if strings are terminated.
  if (!isTerminated(str1, str1End, str1Size)) {
    std::cout << "String 1 not terminated within bounds!\n";
    OOB_VIOLATION(str1Begin, str1Pool, str1Begin, str1Size)
  }
  if (!isTerminated(str2, str2End, str2Size)) {
    std::cout << "String 2 not terminated within bounds!\n";
    OOB_VIOLATION(str2Begin, str2Pool, str2Begin, str2Size)
  }

  return strcasecmp(str1, str2);
}

/**
 * Secure runtime wrapper function to replace strspn()
 *
 * @param   str1Pool Pool handle for str1
 * @param   str2Pool Pool handle for str2
 * @param   str1     c string to be scanned
 * @param   str2     c string consisted of matching characters
 * @return  length if initial portion of str1, which consists of 
 *          characters only from str2.
 */
int
pool_strspn(DebugPoolTy *s1p,
            DebugPoolTy *s2p, 
            const char *s1, 
            const char *s2,
            const unsigned char complete){

  return pool_strspn_debug(s1p,s2p,s1,s2,complete, DEFAULT_TAG, DEFAULT_SRC_INFO);
}
/**
 * Secure runtime wrapper function to replace strspn()
 *
 * @param   str1Pool Pool handle for str1
 * @param   str2Pool Pool handle for str2
 * @param   str1     c string to be compared
 * @param   str2     c string to be compared
 * @return  length if initial portion of str1, which consists of 
 *          characters only from str2.
 */
int
pool_strspn_debug(DebugPoolTy *str1Pool,
                  DebugPoolTy *str2Pool, 
                  const char *str1, 
                  const char *str2,
                  const unsigned char complete,
                  TAG,
                  SRC_INFO) {

  size_t str1Size = 0, str2Size = 0;
  void *str1Begin =(void*)str1, *str1End = NULL, *str2Begin = (void*)str2, *str2End = NULL;

  assert(str1Pool && str2Pool && str2 && str1 && "Null pool parameters!");

  if (!pool_find(str1Pool, (void*)str1, str1Begin, str1End)) {
    std::cout << "String 1 not found in pool!\n";
    LOAD_STORE_VIOLATION(str1Begin, str1Pool)
  }
  if (!pool_find(str2Pool, (void*)str2, str2Begin, str2End)) {
    std::cout << "String 2 not found in pool!\n";
    LOAD_STORE_VIOLATION(str1Begin, str1Pool)
  }

  // Check if strings are terminated.
  if (!isTerminated(str1, str1End, str1Size)) {
    std::cout << "String 1 not terminated within bounds!\n";
    OOB_VIOLATION(str1Begin, str1Pool, str1Begin, str1Size)
  }
  if (!isTerminated(str2, str2End, str2Size)) {
    std::cout << "String 2 not terminated within bounds!\n";
    OOB_VIOLATION(str2Begin, str2Pool, str2Begin, str2Size)
  }

  return strspn(str1, str2);
}

/**
 * Secure runtime wrapper function to replace strcspn()
 *
 * @param   str1Pool Pool handle for str1
 * @param   str2Pool Pool handle for str2
 * @param   str1     c string to be scanned
 * @param   str2     c string consisted of matching characters
 * @return  length if initial portion of str1, which does not 
 *          consist of any characters from str2.
 */
int pool_strcspn(DebugPoolTy *s1p,
          DebugPoolTy *s2p, 
                const char *s1, 
                const char *s2,
                const unsigned char complete){

  return pool_strcspn_debug(s1p,s2p,s1,s2,complete, DEFAULT_TAG, DEFAULT_SRC_INFO);
}
/**
 * Secure runtime wrapper function to replace strcspn()
 *
 * @param   str1Pool Pool handle for str1
 * @param   str2Pool Pool handle for str2
 * @param   str1     c string to be compared
 * @param   str2     c string to be compared
 * @return  length if initial portion of str1, which does not 
 *          consist of any characters from str2.
 */
int pool_strcspn_debug(DebugPoolTy *str1Pool,
                DebugPoolTy *str2Pool, 
                      const char *str1, 
                      const char *str2,
                      const unsigned char complete,
          TAG,
                      SRC_INFO) {

  size_t str1Size = 0, str2Size = 0;
  void *str1Begin =(void*)str1, *str1End = NULL, *str2Begin = (void*)str2, *str2End = NULL;

  assert(str1Pool && str2Pool && str2 && str1 && "Null pool parameters!");

  if (!pool_find(str1Pool, (void*)str1, str1Begin, str1End)) {
    std::cout << "String 1 not found in pool!\n";
    LOAD_STORE_VIOLATION(str1Begin, str1Pool)
  }
  if (!pool_find(str2Pool, (void*)str2, str2Begin, str2End)) {
    std::cout << "String 2 not found in pool!\n";
    LOAD_STORE_VIOLATION(str1Begin, str1Pool)
  }

  // Check if strings are terminated.
  if (!isTerminated(str1, str1End, str1Size)) {
    std::cout << "String 1 not terminated within bounds!\n";
    OOB_VIOLATION(str1Begin, str1Pool, str1Begin, str1Size)
  }
  if (!isTerminated(str2, str2End, str2Size)) {
    std::cout << "String 2 not terminated within bounds!\n";
    OOB_VIOLATION(str2Begin, str2Pool, str2Begin, str2Size)
  }

  return strcspn(str1, str2);
}

/**
 * Secure runtime wrapper function to replace memchr()
 *
 * @param   stringPool  Pool handle for the string
 * @param   string      Memory object
 * @param   c           An int value to be found
 * @param   n           Number of bytes to search in
 * @return  Pointer to position where c is first found
 */
void *
pool_memchr(DebugPoolTy *stringPool, 
            void *string, 
            int c, 
            size_t n,
            const unsigned char complete) {
  return pool_memchr_debug(stringPool, string, c, n,complete, DEFAULT_TAG, DEFAULT_SRC_INFO);

}
/**
 * Secure runtime wrapper function to replace memchr()
 *
 * @param   stringPool  Pool handle for the string
 * @param   string      Memory object
 * @param   c           An int value to be found
 * @param   n           Number of bytes to search in
 * @return  Pointer to position where c is first found
 */
void *
pool_memchr_debug(DebugPoolTy *stringPool, 
                  void *string, int c, 
                  size_t n,
                  const unsigned char complete,
                  TAG,
                  SRC_INFO){
  size_t stringSize = 0;
  void *stringBegin = string, *stringEnd = NULL, *stop= NULL;

  assert(stringPool && string && "Null pool parameters!");

  // Retrieve both the destination and source buffer's bounds from the pool handle.
  if(!pool_find(stringPool, (void*)string, stringBegin, stringEnd)){
    std::cout<<"Memory object not found in pool!\n";
    LOAD_STORE_VIOLATION(string,stringPool);
  }

  stringSize = (char *)stringEnd - (char *)string + 1;
  stop = memchr(string,c,stringSize);
  if (stop && (char*)stop<(char*)string+std::min(n,stringSize))
    return stop;
  else {
    std::cout << "Possible read out of bound in memory object!\n";
    OOB_VIOLATION(stringBegin, stringPool, stringBegin, stringSize)
    return NULL;
  }
}

/**
 * Secure runtime wrapper function to replace memccpy()
 *
 * @param   dstPool  Pool handle for destination memory area
 * @param   srcPool  Pool handle for source memory area
 * @param   dst      Destination memory area
 * @param   src      Source memory area
 * @param   c        It stops copying when it sees this char
 * @param   n        Maximum number of bytes to copy
 * @return           A pointer to the first byte after c in dst or, 
 *                   If c was not found in the first n bytes of s2, it returns a null pointer.
 */
void *
pool_memccpy(DebugPoolTy *dstPool, 
             DebugPoolTy *srcPool, 
             void *dst, 
             const void *src, 
             char c,
             size_t n,
             const unsigned char complete) {
  return pool_memccpy_debug(dstPool,srcPool,dst,src, c, n,complete,DEFAULT_TAG, DEFAULT_SRC_INFO);
}


/**
 * Secure runtime wrapper function to replace memccpy()
 *
 * @param   dstPool  Pool handle for destination memory area
 * @param   srcPool  Pool handle for source memory area
 * @param   dst      Destination memory area
 * @param   src      Source memory area
 * @param   c        It stops copying when it sees this char
 * @param   n        Maximum number of bytes to copy
 * @return           A pointer to the first byte after c in dst or, 
 *                   If c was not found in the first n bytes of s2, it returns a null pointer.
 */
void *
pool_memccpy_debug(DebugPoolTy *dstPool, 
                   DebugPoolTy *srcPool, 
                   void *dst, 
                   const void *src, 
                   char c,
                   size_t n,
                   const unsigned char complete,
                   TAG,
                   SRC_INFO){

  size_t dstSize = 0, srcSize = 0;
  void *dstBegin = dst, *dstEnd = NULL, *srcBegin = (char *)src, *srcEnd = NULL, *stop = NULL;

  assert(dstPool && srcPool && dst && src && "Null pool parameters!");

  // Retrieve both the destination and source buffer's bounds from the pool handle.
  if(!pool_find(dstPool, (void*)dst, dstBegin, dstEnd)){
    std::cout<<"Memory object not found in pool!\n";
    LOAD_STORE_VIOLATION(dst,dstPool);
  }
  if(!pool_find(srcPool, (void*)src, srcBegin, srcEnd)){
    std::cout<<"Memory object not found in pool!\n";
    LOAD_STORE_VIOLATION(src,srcPool);
  }

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - (char *)dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;
  stop= memchr((void*)src,c,srcSize);
  if(!stop){
    
    if (n > srcSize) {
      std::cout << "Cannot copy more bytes than the size of the source!\n";
      WRITE_VIOLATION(srcBegin, srcPool, dstSize, srcSize);
    }

    if (n >dstSize) {
      std::cout << "Cannot copy more bytes than the size of the destination!\n";
      WRITE_VIOLATION(dstBegin, dstPool, dstSize, srcSize);
    }

    if(isOverlapped(dst,(const char*)dst+n-1,src,(const char*)src+n-1)){ 
      std::cout<<"Two memory objects overlap each other!/n";
      LOAD_STORE_VIOLATION(dst,dstPool);
    }
  }
  
  if((size_t)((char*)stop-(char*)src+1)>dstSize){
    std::cout << "Cannot copy more bytes than the size of the destination!\n";
    WRITE_VIOLATION(dstBegin, dstPool, dstSize, srcSize);
  }

  memccpy(dst, src, c, n);

  return dst;
}


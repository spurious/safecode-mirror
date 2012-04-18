//===------- syscalls.cpp - CStdLib runtime wrappers for system calls -----===//
// 
//                          The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file provides CStdLib runtime wrapper versions of system calls.
//
//===----------------------------------------------------------------------===//

#include "CStdLib.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

//
// Check to see if the memory region between the location pointed to by Buf
// and the end of the same memory object is of at least the given minimum size.
//
// This function will look up the buffer object in the pool to determine its
// size. If the pointer is complete and not found in the pool the function
// will report an error. If the pointer points to a region of size less than
// MinSize then this function will report an error.
//
// Inputs
//   Pool     - The pool handle for the buffer
//   Buf      - The buffer
//   Complete - A boolean describing if the pointer is complete
//   MinSize  - The minimum expected size of the region pointed to by Buf
//   SRC_INFO - Source file and line number information for debugging purposes
//
static inline void
minSizeCheck (DebugPoolTy * Pool,
              void * Buf,
              bool Complete,
              size_t MinSize,
              SRC_INFO) {
  bool Found;
  void * BufStart = 0, * BufEnd = 0;

  //
  // Retrive the buffer's bound from the pool. If we cannot find the object and
  // we know everything about what the buffer should be pointing to, then
  // report an error.
  //
  if (!(Found = pool_find (Pool, Buf, BufStart, BufEnd)) && Complete) {
    LOAD_STORE_VIOLATION (Buf, Pool, SRC_INFO_ARGS);
  }

  if (Found) {
    //
    // Make sure that the region between the location pointed to by Buf and the
    // end of the same memory object is of size at least MinSize.
    //
    size_t BufSize = byte_range (Buf, BufEnd);

    if (BufSize < MinSize) {
      C_LIBRARY_VIOLATION (Buf, Pool, "", SRC_INFO_ARGS);
    }
  }
}

//
// Function: pool_read()
//
// Description:
//  This is a memory safe replacement for the read() function.
//
// Inputs:
//   Pool     - The pool handle for the input buffer
//   Buf      - The input buffer
//   FD       - The file descriptor
//   Count    - The maximum number of bytes to read
//   Complete - The Completeness bit vector
//   TAG      - The Tag information for debugging purposes
//   SRC_INFO - Source file and line number information for debugging purposes
//
ssize_t
pool_read_debug (DebugPoolTy * Pool,
                 void * Buf,
                 int FD,
                 size_t Count,
                 const uint8_t Complete,
                 TAG,
                 SRC_INFO) {
  minSizeCheck (Pool, Buf, ARG1_COMPLETE(Complete), Count, SRC_INFO_ARGS);
  return read (FD, Buf, Count);
}

ssize_t
pool_read (DebugPoolTy * Pool,
           void * Buf,
           int FD,
           size_t Count,
           const uint8_t Complete) {
  return pool_read_debug (Pool, Buf, FD, Count, Complete, DEFAULTS);
}

//
// Function: pool_recv()
//
// Description:
//  This is a memory safe replacement for the recv() function.
//
// Inputs:
//   Pool     - The pool handle for the input buffer
//   Buf      - The input buffer
//   FD       - The file descriptor
//   Count    - The maximum number of bytes to read
//   Complete - The Completeness bit vector
//   TAG      - The Tag information for debugging purposes
//   SRC_INFO - Source file and line number information for debugging purposes
//
ssize_t
pool_recv_debug (DebugPoolTy * Pool,
                 void * Buf,
                 int SockFD,
                 size_t Len,
                 int Flags,
                 const uint8_t Complete,
                 TAG,
                 SRC_INFO) {
  minSizeCheck (Pool, Buf, ARG1_COMPLETE(Complete), Len, SRC_INFO_ARGS);
  return recv (SockFD, Buf, Len, Flags);
}

ssize_t
pool_recv (DebugPoolTy * Pool,
           void * Buf,
           int SockFD,
           size_t Len,
           int Flags,
           const uint8_t Complete) {
  return pool_recv_debug (Pool, Buf, SockFD, Len, Flags, Complete, DEFAULTS);
}

//
// Function: pool_write()
//
// Description:
//  This is a memory safe replacement for the write() function.
//
// Inputs:
//   Pool     - The pool handle for the output buffer
//   Buf      - The output buffer
//   FD       - The file descriptor
//   Count    - The maximum number of bytes to write
//   Complete - The Completeness bit vector
//   TAG      - The Tag information for debugging purposes
//   SRC_INFO - Source file and line number information for debugging purposes
//
ssize_t
pool_write_debug (DebugPoolTy * Pool,
                 void * Buf,
                 int FD,
                 size_t Count,
                 const uint8_t Complete,
                 TAG,
                 SRC_INFO) {
  minSizeCheck (Pool, Buf, ARG1_COMPLETE(Complete), Count, SRC_INFO_ARGS);
  return write (FD, Buf, Count);
}

ssize_t
pool_write (DebugPoolTy * Pool,
            void * Buf,
            int FD,
            size_t Count,
            const uint8_t Complete) {
  return pool_write_debug (Pool, Buf, FD, Count, Complete, DEFAULTS);
}

//
// Function: pool_send()
//
// Description:
//  This is a memory safe replacement for the send() function.
//
// Inputs:
//   Pool     - The pool handle for the output buffer
//   FD       - The file descriptor
//   Buf      - The output buffer
//   Count    - The maximum number of bytes to write
//   Complete - The Completeness bit vector
//   TAG      - The Tag information for debugging purposes
//   SRC_INFO - Source file and line number information for debugging purposes
//
ssize_t
pool_send_debug (DebugPoolTy * Pool,
                 void * Buf,
                 int SockFD,
                 size_t Len,
                 int Flags,
                 const uint8_t Complete,
                 TAG,
                 SRC_INFO) {
  minSizeCheck (Pool, Buf, ARG1_COMPLETE(Complete), Len, SRC_INFO_ARGS);
  return send (SockFD, Buf, Len, Flags);
}

ssize_t
pool_send (DebugPoolTy * Pool,
           void * Buf,
           int SockFD,
           size_t Len,
           int Flags,
           const uint8_t Complete) {
  return pool_send_debug (Pool, Buf, SockFD, Len, Flags, Complete, DEFAULTS);
}

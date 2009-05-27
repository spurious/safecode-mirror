//===- AllocatorInfo.h ------------------------------------------*- C++ -*----//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// Define the abstraction of a pair of allocator / deallocator, including:
//
//   * The size of the object being allocated. 
//   * Whether the size may be a constant, which can be used for exactcheck
// optimization.
//
//===----------------------------------------------------------------------===//

#include "safecode/SAFECode.h"

#include <string>

/// Forward declaration
namespace llvm {
  class Value;
}

NAMESPACE_SC_BEGIN

/// AllocatorInfo - define the abstraction of a pair of allocator / deallocator.
class AllocatorInfo {
 public:
  AllocatorInfo(const std::string & allocCallName, 
                const std::string & freeCallName) : 
  allocCallName(allocCallName), freeCallName(freeCallName) {}

  virtual ~AllocatorInfo();
  /// Test whether the size of a particular allocation site may be a constant or
  /// not.
  /// This is used to determined whether SAFECode can perform an exactcheck
  /// optimization on the particular allocation site.
  /// 
  /// For simple allocators such as malloc() / poolalloc(), that is always
  /// true. However, allocators such as kmem_cache_alloc() put the size of
  /// allocation inside a struct, which needs extra instructions to get the size
  /// out. We don't want to get into this complexity right now, even running
  /// ADCE right after exactcheck optimization might fix the problem.
  ///
  virtual bool isAllocSizeMayConstant(llvm::Value * AllocSite) const { return true; }

  /// Return the size of the object being allocated
  /// Assume the caller knows it is an allocation for this allocator
  /// Return NULL when something is wrong
  virtual llvm::Value * getAllocSize(llvm::Value * AllocSite) const = 0;

  /// Return the pointer being freed
  /// Return NULL when something is wrong
  virtual llvm::Value * getFreedPointer(llvm::Value * FreeSite) const = 0;

  /// Return the function name of the allocator, say "malloc".
  const std::string & getAllocCallName() const { return allocCallName; }

  /// Return the function name of the deallocator, say "free".
  const std::string & getFreeCallName() const { return freeCallName; }
 protected:
  std::string allocCallName;
  std::string freeCallName;
};

/// SimpleAllocatorInfo - define the abstraction of simple allcators /
/// deallocators such as malloc / free
class SimpleAllocatorInfo : public AllocatorInfo {
 public:
 SimpleAllocatorInfo(const std::string & allocCallName, 
                     const std::string & freeCallName,
                     uint32_t allocSizeOperand,
                     uint32_t freePtrOperand) :
   AllocatorInfo(allocCallName, freeCallName),
    allocSizeOperand(allocSizeOperand), freePtrOperand(freePtrOperand) {}
  virtual llvm::Value * getAllocSize(llvm::Value * AllocSite) const;
  virtual llvm::Value * getFreedPointer(llvm::Value * FreeSite) const;
 private:
  uint32_t allocSizeOperand;
  uint32_t freePtrOperand;
};

NAMESPACE_SC_END

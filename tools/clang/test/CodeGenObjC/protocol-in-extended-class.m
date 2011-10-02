// REQUIRES: x86-registered-target,x86-64-registered-target
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -S %s -o %t-64.s
// RUN: FileCheck -check-prefix LP64 --input-file=%t-64.s %s
// RUN: %clang_cc1 -triple i386-apple-darwin -fobjc-fragile-abi -S %s -o %t-32.s
// RUN: FileCheck -check-prefix LP32 --input-file=%t-32.s %s

@protocol MyProtocol
@end

@protocol ExtendedProtocol
@end

@interface ItDoesntWork<MyProtocol> {
}
-(void) Meth;
@end

@interface ItDoesntWork() <MyProtocol, ExtendedProtocol>
@end

@implementation ItDoesntWork
-(void) Meth {
    ItDoesntWork <MyProtocol, ExtendedProtocol> *p = 0;
 }
@end

// CHECK-LP64: l_OBJC_PROTOCOL_$_ExtendedProtocol:

// CHECK-LP32: L_OBJC_PROTOCOL_ExtendedProtocol:

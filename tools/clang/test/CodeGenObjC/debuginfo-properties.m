// RUN: %clang_cc1 -g -emit-llvm -triple x86_64-apple-darwin -o - %s | FileCheck %s
// Check that we emit the correct method names for properties from a protocol.
// rdar://problem/13798000
@protocol NSObject
- (id)init;
@end
@interface NSObject <NSObject> {}
@end

@class Selection;

@protocol HasASelection <NSObject>
@property (nonatomic, retain) Selection* selection;
// CHECK: !DISubprogram(name: "-[MyClass selection]"
// CHECK-SAME:          line: [[@LINE-2]]
// CHECK-SAME:          isLocal: true, isDefinition: true
// CHECK: !DISubprogram(name: "-[MyClass setSelection:]"
// CHECK-SAME:          line: [[@LINE-5]]
// CHECK-SAME:          isLocal: true, isDefinition: true
// CHECK: !DISubprogram(name: "-[OtherClass selection]"
// CHECK-SAME:          line: [[@LINE-8]]
// CHECK-SAME:          isLocal: true, isDefinition: true
// CHECK: !DISubprogram(name: "-[OtherClass setSelection:]"
// CHECK-SAME:          line: [[@LINE-11]]
// CHECK-SAME:          isLocal: true, isDefinition: true

@end

@interface MyClass : NSObject <HasASelection> {
  Selection *_selection;
}
@end

@implementation MyClass
@synthesize selection = _selection;
@end

@interface OtherClass : NSObject <HasASelection> {
  Selection *_selection;
}
@end
@implementation OtherClass
@synthesize selection = _selection;
@end

//===----------------------------------------------------------------------===//
// LLVM 'embec' UTILITY : Checks codes for safety as per the EmbeC language
// rules. Targetted at embedded systems.
//
//===----------------------------------------------------------------------===//

#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Bytecode/Reader.h"
#include "llvm/Assembly/CWriter.h"
#include "UninitPointer.h"
#include "SafeDynMemAlloc.h"
#include "ArrayBoundsCheck.h"
#include "StackSafety.h"
#include "llvm/Transforms/Scalar.h"
#include "Support/CommandLine.h"
#include "Support/Signals.h"
#include <fstream>
#include <memory>
using namespace std;

static cl::opt<string>
InputFilename(cl::Positional, cl::desc("<input bytecode>"), cl::init("-"));

static cl::opt<string>
OutputFilename("o", cl::desc("Override output filename"),
               cl::value_desc("filename"));

static cl::opt<bool>
Force("f", cl::desc("Overwrite output files"));

int main(int argc, char **argv) {

  cl::ParseCommandLineOptions(argc, argv,
			      " llvm .bc -> .bc modular optimizer\n");

  // Load the input module...
  std::auto_ptr<Module> M(ParseBytecodeFile(InputFilename));


    if (M.get() == 0) {
    cerr << "bytecode didn't read correctly.\n";
    return 1;
  }

  // Figure out what stream we are supposed to write to...

  std::ostream *Out = &std::cout;  // Default to printing to stdout...


  if (OutputFilename != "") {
    Out = new std::ofstream(OutputFilename.c_str());
    

    if (!Out->good()) {
      cerr << "Error opening " << OutputFilename << "!\n";
      return 1;
    }
    
  }
    
    
  //
  PassManager Passes;
  
  //Add passes
  Passes.add(createCZeroUninitPtrPass());
  Passes.add(createABCPreProcessPass());
  Passes.add(createArrayBoundsCheckPass());
  Passes.add(createStackSafetyPass());
  Passes.add(createEmbeCFreeRemovalPass());
 
  // Now that we have all of the passes ready, run them.
  if (Passes.run(*M.get()))
    cerr << "Program modified.\n";
  (*Out) << M.get();
  //  WriteToC(M.get(), *Out, false);

  return 0;
}

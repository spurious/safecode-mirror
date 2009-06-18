//===-- sc - SAFECode Compiler Tool ---------------------------------------===//
//
//                     The SAFECode Project
//
// This file was developed by the LLVM research group and is distributed
// under the University of Illinois Open Source License. See LICENSE.TXT for
// details.
//
//===----------------------------------------------------------------------===//
//
// This program is a tool to run the SAFECode passes on a bytecode input file.
//
//===----------------------------------------------------------------------===//

#include "safecode/SAFECode.h"
#include "safecode/SAFECodeConfig.h"
#include "safecode/InsertChecks/RegisterBounds.h"
#include "safecode/InsertChecks/RegisterRuntimeInitializer.h"
#include "safecode/Support/AllocatorInfo.h"

#include "llvm/Module.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/System/Signals.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"

#include "poolalloc/PoolAllocate.h"

#include "ABCPreProcess.h"
#include "InsertPoolChecks.h"
#include "IndirectCallChecks.h"
#include "safecode/BreakConstantGEPs.h"
#include "safecode/CStdLib.h"
#include "safecode/DebugInstrumentation.h"
#include "safecode/OptimizeChecks.h"
#include "safecode/RewriteOOB.h"
#include "safecode/SpeculativeChecking.h"
#include "safecode/LowerSafecodeIntrinsic.h"
#include "safecode/FaultInjector.h"
#include "safecode/CodeDuplication.h"

#include <fstream>
#include <iostream>
#include <memory>

using namespace llvm;
using namespace NAMESPACE_SC;

// General options for sc.
static cl::opt<std::string>
InputFilename(cl::Positional, cl::desc("<input bytecode>"), cl::init("-"));

static cl::opt<std::string>
OutputFilename("o", cl::desc("Output filename"), cl::value_desc("filename"));

static cl::opt<bool>
Force("f", cl::desc("Overwrite output files"));

static cl::opt<bool>
FullPA("pa", cl::init(false), cl::desc("Use pool allocation"));

static cl::opt<bool>
EnableDebugInfo("enable-debuginfo", cl::init(false),
                cl::desc("Enable Debugging Info in Run-time Errors"));

static cl::opt<bool>
DisableCStdLib("disable-cstdlib", cl::init(true), cl::desc("Disable transformations that secure C standard library calls"));

static cl::opt<bool>
EnableFastCallChecks("enable-fastcallchecks", cl::init(false),
                     cl::desc("Enable fast indirect call checks"));

static cl::opt<bool>
DisableMonotonicLoopOpt("disable-monotonic-loop-opt", cl::init(false), cl::desc("Disable optimization for checking monotonic loops"));

enum CheckingRuntimeType {
  RUNTIME_PA, RUNTIME_DEBUG, RUNTIME_SINGLETHREAD, RUNTIME_PARALLEL, RUNTIME_QUEUE_OP, RUNTIME_SVA 
};

#ifdef SC_DEBUGTOOL
enum CheckingRuntimeType DefaultRuntime = RUNTIME_DEBUG;
#else
enum CheckingRuntimeType DefaultRuntime = RUNTIME_SINGLETHREAD;
#endif

static cl::opt<enum CheckingRuntimeType>
CheckingRuntime("runtime", cl::init(DefaultRuntime),
                  cl::desc("The runtime API used by the program"),
                  cl::values(
  clEnumVal(RUNTIME_PA,           "Pool Allocation runtime  no checks)"),
  clEnumVal(RUNTIME_DEBUG,        "Debugging Tool runtime"),
  clEnumVal(RUNTIME_SINGLETHREAD, "Single Thread runtime (Production version)"),
  clEnumVal(RUNTIME_PARALLEL,     "Parallel Checking runtime (Production version)"),
  clEnumVal(RUNTIME_QUEUE_OP,     "Parallel no-op Checking runtime (For testing queue performance)"),
  clEnumVal(RUNTIME_SVA,     "Runtime for SVA"),
  clEnumValEnd));

static cl::opt<bool>
EnableProtectingMetaData("protect-metadata", cl::init(false),
			 cl::desc("Instrument store instructions to protect the meta data"));

static cl::opt<bool>
EnableCodeDuplication("code-duplication", cl::init(false),
			 cl::desc("Enable Code Duplication for SAFECode checking"));

#define NOT_FOR_SVA(X) do { if (!SCConfig->SVAEnabled) X; } while (0);

static void addLowerIntrinsicPass(PassManager & Passes, CheckingRuntimeType type);

// GetFileNameRoot - Helper function to get the basename of a filename.
static inline std::string
GetFileNameRoot(const std::string &InputFilename) {
  std::string IFN = InputFilename;
  std::string outputFilename;
  int Len = IFN.length();
  if ((Len > 2) &&
      IFN[Len-3] == '.' && IFN[Len-2] == 'b' && IFN[Len-1] == 'c') {
    outputFilename = std::string(IFN.begin(), IFN.end()-3); // s/.bc/.s/
  } else {
    outputFilename = IFN;
  }
  return outputFilename;
}

// AllocatorInfo
namespace {
  // vmalloc
  SimpleAllocatorInfo AllocatorVMalloc("vmalloc", "vfree", 1, 1);
  // kmalloc
  SimpleAllocatorInfo AllocatorKMalloc("__kmalloc", "kfree", 1, 1);
  // __alloc_bootmem
  SimpleAllocatorInfo AllocatorBootmem("__alloc_bootmem", "", 1, 1);
  // pool allocator used by user space programs
  SimpleAllocatorInfo AllocatorPoolAlloc("poolalloc", "poolfree", 2, 2);
}


// main - Entry point for the sc compiler.
//
int main(int argc, char **argv) {
  std::string mt;
  std::string & msg = mt;
  llvm_shutdown_obj ShutdownObject;

  try {
    cl::ParseCommandLineOptions(argc, argv, "SAFECode Compiler\n");
    sys::PrintStackTraceOnErrorSignal();

    // Load the module to be compiled...
    std::auto_ptr<Module> M;
    std::string ErrorMessage;
    if (MemoryBuffer *Buffer
          = MemoryBuffer::getFileOrSTDIN(InputFilename, &ErrorMessage)) {
      M.reset(ParseBitcodeFile(Buffer, &ErrorMessage));
      delete Buffer;
    }

    if (M.get() == 0) {
      std::cerr << argv[0] << ": bytecode didn't read correctly.\n";
      return 1;
    }

    // Parse Configuration
    SCConfig = SAFECodeConfiguration::create();
    // FIXME
    // HACK: we set DSAType of SCConfig here, since we don't have a consistent
    // uses of parameters. We should sit down and discuss the parameters that
    // SAFECode should have...
    SCConfig->DSAType = 
      SCConfig->SVAEnabled ? 
      SAFECodeConfiguration::DSA_BASIC : 
      SAFECodeConfiguration::DSA_EQTD;


    if (SCConfig->SVAEnabled) {
      SCConfig->allocators.push_back(&AllocatorVMalloc);
      SCConfig->allocators.push_back(&AllocatorKMalloc);
      SCConfig->allocators.push_back(&AllocatorBootmem);
    } else {
      SCConfig->allocators.push_back(&AllocatorPoolAlloc);
    }

    // Build up all of the passes that we want to do to the module...
    PassManager Passes;
    Passes.add(new TargetData(M.get()));

    // Remove all constant GEP expressions
    NOT_FOR_SVA(Passes.add(new BreakConstantGEPs()));

    // Ensure that all malloc/free calls are changed into LLVM instructions
    NOT_FOR_SVA(Passes.add(createRaiseAllocationsPass()));

    //
    // Remove indirect calls to malloc and free functions.  This can be done
    // here because none of the SAFECode transforms will add indirect calls to
    // malloc() and free().
    //
    NOT_FOR_SVA(Passes.add(createIndMemRemPass()));

    // Ensure that all malloc/free calls are changed into LLVM instructions
    NOT_FOR_SVA(Passes.add(createRaiseAllocationsPass()));

    //
    // Ensure that all functions have only a single return instruction.  We do
    // this to make stack-to-heap promotion easier (with a single return
    // instruction, we know where to free all of the promoted alloca's).
    //
    NOT_FOR_SVA(Passes.add(createUnifyFunctionExitNodesPass()));

    //
    // Convert Unsafe alloc instructions first.  This does not rely upon
    // pool allocation and has problems dealing with cloned functions.
    //
    if (CheckingRuntime != RUNTIME_PA)
      NOT_FOR_SVA(Passes.add(new ConvertUnsafeAllocas()));

    // Schedule the Bottom-Up Call Graph analysis before pool allocation.  The
    // Bottom-Up Call Graph pass doesn't work after pool allocation has
    // been run, and PassManager schedules it after pool allocation for
    // some reason.
    NOT_FOR_SVA(Passes.add(new BottomUpCallGraph()));

    NOT_FOR_SVA(Passes.add(new ParCheckingCallAnalysis()));
 
    if (SCConfig->SVAEnabled) {
      Passes.add(new PoolAllocateSimple(true, true, false));
    } else {
      if (FullPA)
        Passes.add(new PoolAllocate(true, true));
      else
        Passes.add(new PoolAllocateSimple(true, true, true));
    }


#if 0
    // Convert Unsafe alloc instructions first.  This version relies upon
    // pool allocation,
    Passes.add(new PAConvertUnsafeAllocas());
#endif
    //    Passes.add(new EmbeCFreeRemoval());
    Passes.add(new RegisterGlobalVariables());

    if (!SCConfig->SVAEnabled) {
      Passes.add(new RegisterMainArgs());
      Passes.add(new RegisterRuntimeInitializer());
    }

    // Register all customized allocators, such as vmalloc() / kmalloc() in
    // kernel, or poolalloc() in pool allocation
    Passes.add(new RegisterCustomizedAllocation());      

    switch (SCConfig->StaticCheckType) {
    case SAFECodeConfiguration::ABC_CHECK_NONE:
      Passes.add(new ArrayBoundsCheckDummy());
      break;
    case SAFECodeConfiguration::ABC_CHECK_LOCAL:
      Passes.add(new ArrayBoundsCheckLocal());
      break;
    case SAFECodeConfiguration::ABC_CHECK_FULL:
      Passes.add(new ABCPreProcess());
      Passes.add(new ArrayBoundsCheck());
      break;
    }

    Passes.add(new InsertPoolChecks());
    Passes.add(new ExactCheckOpt());

    NOT_FOR_SVA(Passes.add(new RegisterStackObjPass()));
    NOT_FOR_SVA(Passes.add(new InitAllocas()));

    if (EnableFastCallChecks)
      Passes.add(createIndirectCallChecksPass());

    if (!DisableCStdLib) {
      NOT_FOR_SVA(Passes.add(new StringTransform()));
    }

    if (!DisableMonotonicLoopOpt)
      Passes.add(new MonotonicLoopOpt());

    if (CheckingRuntime == RUNTIME_PARALLEL) {
      Passes.add(new SpeculativeCheckingInsertSyncPoints());

      if (EnableProtectingMetaData) {
        Passes.add(new SpeculativeCheckStoreCheckPass());
      }
    }

    //
    // Do post processing required for Out of Bounds pointer rewriting.
    // Try to optimize the checks first as the rewriteOOB pass may make
    // optimization impossible.
    //
    if (CheckingRuntime == RUNTIME_DEBUG) {
      Passes.add (new OptimizeChecks());
      Passes.add(new RewriteOOB());
    }

#if 0
    //
    // Run the LICM pass to hoist checks out of loops.
    //
    Passes.add (createLICMPass());
#endif

    //
    // Remove special attributes for loop hoisting that were added by previous
    // SAFECode passes.
    //
    Passes.add (createClearCheckAttributesPass());

    if (EnableCodeDuplication)
      Passes.add(new DuplicateLoopAnalysis());

    //
    // Attempt to optimize the checks.
    //
    Passes.add (new OptimizeChecks());

#ifdef SC_DEBUGTOOL
    if (EnableDebugInfo)
      Passes.add (new DebugInstrument());
#endif

    // Lower the checking intrinsics into appropriate runtime function calls.
    // It should be the last pass
    addLowerIntrinsicPass(Passes, CheckingRuntime);

    // Verify the final result
    Passes.add(createVerifierPass());

    // Figure out where we are going to send the output...
    std::ostream *Out = 0;
    if (OutputFilename != "") {
      if (OutputFilename != "-") {
        // Specified an output filename?
        if (!Force && std::ifstream(OutputFilename.c_str())) {
          // If force is not specified, make sure not to overwrite a file!
          std::cerr << argv[0] << ": error opening '" << OutputFilename
                    << "': file exists!\n"
                    << "Use -f command line argument to force output\n";
          return 1;
        }
        Out = new std::ofstream(OutputFilename.c_str());

        // Make sure that the Out file gets unlinked from the disk if we get a
        // SIGINT
        sys::RemoveFileOnSignal(sys::Path(OutputFilename));
      } else {
        Out = &std::cout;
      }
    } else {
      if (InputFilename == "-") {
        OutputFilename = "-";
        Out = &std::cout;
      } else {
        OutputFilename = GetFileNameRoot(InputFilename);

        OutputFilename += ".sc.bc";
      }

      if (!Force && std::ifstream(OutputFilename.c_str())) {
        // If force is not specified, make sure not to overwrite a file!
          std::cerr << argv[0] << ": error opening '" << OutputFilename
                    << "': file exists!\n"
                    << "Use -f command line argument to force output\n";
          return 1;
      }
      
      Out = new std::ofstream(OutputFilename.c_str());
      if (!Out->good()) {
        std::cerr << argv[0] << ": error opening " << OutputFilename << "!\n";
        delete Out;
        return 1;
      }

      // Make sure that the Out file gets unlinked from the disk if we get a
      // SIGINT
      sys::RemoveFileOnSignal(sys::Path(OutputFilename));
    }

    // Add the writing of the output file to the list of passes
    Passes.add (CreateBitcodeWriterPass(*Out));

    // Run our queue of passes all at once now, efficiently.
    Passes.run(*M.get());

    // Delete the ostream if it's not a stdout stream
    if (Out != &std::cout) delete Out;
  
    return 0;
  } catch (msg) {
    std::cerr << argv[0] << ": " << msg << "\n";
  } catch (...) {
    std::cerr << argv[0] << ": Unexpected unknown exception occurred.\n";
  }
  llvm_shutdown();
  return 1;
}

static void addLowerIntrinsicPass(PassManager & Passes, CheckingRuntimeType type) {
  /// Mapping between check intrinsics and implementation

  typedef LowerSafecodeIntrinsic::IntrinsicMappingEntry IntrinsicMappingEntry;
  static IntrinsicMappingEntry RuntimePA[] = 
    { {"sc.lscheck",         "__sc_no_op_poolcheck" },
      {"sc.lscheckui",       "__sc_no_op_poolcheck" },
      {"sc.lscheckalign",    "__sc_no_op_poolcheckalign" },
      {"sc.lscheckalignui",    "__sc_no_op_poolcheckalign" },
      {"sc.boundscheck",       "__sc_no_op_boundscheck" },
      {"sc.boundscheckui",     "__sc_no_op_boundscheck" },
      {"sc.exactcheck",       "__sc_no_op_exactcheck" },
      {"sc.exactcheck2",     "__sc_no_op_exactcheck2" },
      {"poolregister",      "__sc_no_op_poolregister" },
      {"poolunregister",    "__sc_no_op_poolunregister" },
      {"poolalloc",         "__sc_barebone_poolalloc"},
      {"poolfree",          "__sc_barebone_poolfree"},
      {"pooldestroy",       "__sc_barebone_pooldestroy"},
      {"pool_init_runtime", "__sc_barebone_pool_init_runtime"},
      {"poolinit",          "__sc_barebone_poolinit"},
      {"poolrealloc",       "__sc_barebone_poolrealloc"},
      {"poolcalloc",        "__sc_barebone_poolcalloc"},
      {"poolstrdup",        "__sc_barebone_poolstrdup"},
      {"sc.get_actual_val",  "pchk_getActualValue" },
    };

  static IntrinsicMappingEntry RuntimeSingleThread[] = 
    { {"sc.lscheck",         "sc.lscheck" },
      {"sc.lscheckui",       "__sc_no_op_poolcheck" },
      {"sc.lscheckalign",    "poolcheckalign" },
      {"sc.lscheckalignui",    "poolcheckalignui" },
      {"sc.boundscheck",       "boundscheck" },
      {"sc.boundscheckui",     "boundscheckui" },
      {"sc.exactcheck",       "exactcheck" },
      {"sc.exactcheck2",     "exactcheck2" },
      {"sc.pool_register",      "poolregister" },
      {"sc.pool_unregister",    "poolunregister" },
      {"sc.init_pool_runtime", "__sc_bc_pool_init_runtime"},
      {"poolalloc",         "__sc_bc_poolalloc"},
      {"poolfree",          "__sc_bc_poolfree"},
      {"pooldestroy",       "__sc_bc_pooldestroy"},
      {"poolinit",          "__sc_bc_poolinit"},
      {"poolrealloc",       "__sc_bc_poolrealloc"},
      {"poolcalloc",        "__sc_bc_poolcalloc"},
      {"poolstrdup",        "__sc_bc_poolstrdup"},
      {"sc.get_actual_val",  "pchk_getActualValue" },
    };

  static IntrinsicMappingEntry RuntimeDebug[] = 
    { {"sc.lscheck",         "poolcheck" },
      {"sc.lscheckui",       "poolcheckui" },
      {"sc.lscheckalign",    "poolcheckalign" },
      {"sc.lscheckalignui",  "poolcheckalignui" },
      {"sc.boundscheck",     "boundscheck" },
      {"sc.boundscheckui",   "boundscheckui" },
      {"sc.exactcheck",      "exactcheck" },
      {"sc.exactcheck2",     "exactcheck2" },
      {"sc.get_actual_val",  "pchk_getActualValue" },
      {"sc.pool_register",   "__sc_dbg_poolregister" },
      {"sc.pool_unregister", "__sc_dbg_poolunregister" },
      {"sc.init_pool_runtime", "pool_init_runtime"},
      {"sc.pool_register_debug", "__sc_dbg_src_poolregister"},
      {"sc.poolcheck_debug","poolcheck_debug"},
      {"sc.poolcheckalign_debug","poolcheckalign_debug"},
      {"sc.boundscheck_debug","boundscheck_debug"},
      {"sc.boundscheckui_debug","boundscheckui_debug"},
      {"sc.exactcheck2_debug","exactcheck2_debug"},
      {"sc.pool_argvregister", "__sc_dbg_poolargvregister"},
    };


  static IntrinsicMappingEntry RuntimeParallel[] = 
    { {"sc.lscheck",         "__sc_par_poolcheck" },
      {"sc.lscheckui",       "__sc_no_op_poolcheck" },
      {"sc.lscheckalign",    "__sc_par_poolcheckalign" },
      {"sc.lscheckalignui",    "__sc_par_poolcheckalignui" },
      {"sc.boundscheck",       "__sc_par_boundscheck" },
      {"sc.boundscheckui",     "__sc_par_boundscheckui" },
      {"sc.exactcheck",       "exactcheck" },
      {"sc.exactcheck2",     "exactcheck2" },
      {"sc.lscheck.serial",     "__sc_bc_poolcheck" },
      {"sc.lscheckui.serial",   "__sc_no_op_poolcheck" },
      {"sc.lscheckalign.serial","poolcheckalign" },
      {"sc.lscheckalignui.serial","poolcheckalignui" },
      {"sc.boundscheck.serial",   "__sc_bc_boundscheck" },
      {"sc.boundscheckui.serial", "__sc_bc_boundscheckui" },
      {"sc.exactcheck.serial",       "exactcheck" },
      {"sc.exactcheck2.serial",     "exactcheck2" },
      {"poolargvregister",      "__sc_par_poolargvregister" },
      {"poolregister",      "__sc_par_poolregister" },
      {"poolunregister",    "__sc_par_poolunregister" },
      {"poolalloc",         "__sc_par_poolalloc"},
      {"poolfree",          "__sc_par_poolfree"},
      {"pooldestroy",       "__sc_par_pooldestroy"},
      {"pool_init_runtime", "__sc_par_pool_init_runtime"},
      {"poolinit",          "__sc_par_poolinit"},
      {"poolrealloc",       "__sc_par_poolrealloc"},
      {"poolcalloc",        "__sc_par_poolcalloc"},
      {"poolstrdup",        "__sc_par_poolstrdup"},
    };

  const char * queueOpFunction = "__sc_par_enqueue_1";

  static IntrinsicMappingEntry RuntimeQueuePerformance[] = 
    { {"sc.lscheck",        queueOpFunction}, 
      {"sc.lscheckui",      queueOpFunction},
      {"sc.lscheckalign",   queueOpFunction}, 
      {"sc.lscheckalignui", queueOpFunction},
      {"sc.boundscheck",    queueOpFunction}, 
      {"sc.boundscheckui",  queueOpFunction},
      {"sc.exactcheck",     "exactcheck" },
      {"sc.exactcheck2",    "exactcheck2" },
      {"poolregister",      queueOpFunction}, 
      {"poolunregister",    queueOpFunction},
      {"poolalloc",         "__sc_barebone_poolalloc"},
      {"poolfree",          "__sc_barebone_poolfree"},
      {"pooldestroy",       "__sc_barebone_pooldestroy"},
      {"pool_init_runtime", "__sc_par_pool_init_runtime"},
      {"poolinit",          "__sc_barebone_poolinit"},
      {"poolrealloc",       "__sc_barebone_poolrealloc"},
      {"poolcalloc",        "__sc_barebone_poolcalloc"},
      {"poolstrdup",        "__sc_barebone_poolstrdup"},
    };

  static IntrinsicMappingEntry RuntimeSVA[] = 
    { {"sc.lscheck",         "poolcheck" },
      {"sc.lscheckui",       "poolcheck_i" },
      {"sc.lscheckalign",    "poolcheckalign" },
      {"sc.lscheckalignui",  "poolcheckalign_i" },
      {"sc.boundscheck",     "pchk_bounds" },
      {"sc.boundscheckui",   "pchk_bounds_i" },
      {"sc.exactcheck",      "exactcheck" },
      {"sc.exactcheck2",     "exactcheck2" },
      {"sc.pool_register",   "pchk_reg_obj" },
      {"sc.pool_unregister", "pchk_drop_obj" },
      {"poolinit",           "__sva_pool_init" },
    };

  switch (type) {
  case RUNTIME_PA:
    Passes.add(new LowerSafecodeIntrinsic(RuntimePA, RuntimePA + sizeof(RuntimePA) / sizeof(IntrinsicMappingEntry)));
    break;
    
  case RUNTIME_DEBUG:
    Passes.add(new LowerSafecodeIntrinsic(RuntimeDebug, RuntimeDebug + sizeof(RuntimeDebug) / sizeof(IntrinsicMappingEntry)));
    break;

  case RUNTIME_SINGLETHREAD:
    Passes.add(new LowerSafecodeIntrinsic(RuntimeSingleThread, RuntimeSingleThread + sizeof(RuntimeSingleThread) / sizeof(IntrinsicMappingEntry)));
    break;

  case RUNTIME_PARALLEL:
    Passes.add(new LowerSafecodeIntrinsic(RuntimeParallel, RuntimeParallel + sizeof(RuntimeParallel) / sizeof(IntrinsicMappingEntry)));
    break;

  case RUNTIME_QUEUE_OP:
    Passes.add(new LowerSafecodeIntrinsic(RuntimeQueuePerformance, RuntimeQueuePerformance+ sizeof(RuntimeQueuePerformance) / sizeof(IntrinsicMappingEntry)));
    break;

  case RUNTIME_SVA:
    Passes.add(new LowerSafecodeIntrinsic(RuntimeSVA, RuntimeSVA + sizeof(RuntimeSVA) / sizeof(IntrinsicMappingEntry)));
    break;

  default:
    assert (0 && "Invalid Runtime!");
  }
}

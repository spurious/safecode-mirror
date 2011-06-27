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
#include "safecode/CompleteChecks.h"
#include "safecode/BaggyBoundsChecks.h"
#include "safecode/SafeLoadStoreOpts.h"
#include "safecode/InsertChecks/RegisterBounds.h"
#include "safecode/InsertChecks/RegisterRuntimeInitializer.h"
#include "safecode/Support/AllocatorInfo.h"
#include "safecode/SCPoolHeuristic.h"

#include "llvm/Module.h"
#include "llvm/LLVMContext.h"
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
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"

#include "poolalloc/PoolAllocate.h"
#include "safecode/InsertChecks.h"
#include "safecode/LoadStoreChecks.h"
#if 0
#include "IndirectCallChecks.h"
#endif
#include "safecode/BreakConstantGEPs.h"
#include "safecode/BreakConstantStrings.h"
#include "safecode/CStdLib.h"
#include "safecode/DebugInstrumentation.h"
#include "safecode/DetectDanglingPointers.h"
#include "safecode/DummyUse.h"
#include "safecode/FormatStrings.h"
#include "safecode/OptimizeChecks.h"
#include "safecode/RewriteOOB.h"
#include "safecode/SpeculativeChecking.h"
#include "safecode/LowerSafecodeIntrinsic.h"
#include "safecode/FaultInjector.h"
#include "safecode/CodeDuplication.h"

#include <fstream>
#include <iostream>
#include <memory>

namespace llvm {
  extern ModulePass * createSteensgaardPass();
}

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
DisableLSChecks  ("disable-lschecks",
                  cl::init(false),
                  cl::desc("Disable Load/Store Checks"));

static cl::opt<bool>
DisableGEPChecks ("disable-gepchecks", cl::Hidden,
                  cl::init(false),
                  cl::desc("Disable GetElementPtr(GEP) Checks"));

static cl::opt<bool>
DisableDebugInfo("disable-debuginfo",
                 cl::init(false),
                 cl::desc("Disable Debugging Info in Run-time Errors"));

static cl::opt<bool>
DisableCStdLib("disable-cstdlib",
               cl::init(true),
               cl::desc("Disable transformations that secure C standard library calls"));

static cl::opt<bool>
DisableFSChecks("disable-printfchecks",
                cl::init(true),
                cl::desc("Disable securing of printf() style functions"));

static cl::opt<bool>
EnableFastCallChecks("enable-fastcallchecks",
                     cl::init(false),
                     cl::desc("Enable fast indirect call checks"));

static cl::opt<bool>
DisableMonotonicLoopOpt("disable-monotonic-loop-opt", 
                        cl::init(false),
                        cl::desc("Disable optimization for checking monotonic loops"));

static cl::opt<bool>
DisableExactChecks("disable-exactchecks",
                   cl::init(false),
                   cl::desc("Disable exactcheck optimization"));

static cl::opt<bool>
DisableTypeSafetyOpts("disable-typesafety",
                      cl::init(false),
                      cl::desc("Disable type-safety optimizations"));

enum CheckingRuntimeType {
  RUNTIME_PA,
  RUNTIME_DEBUG,
  RUNTIME_SINGLETHREAD,
  RUNTIME_PARALLEL,
  RUNTIME_QUEUE_OP,
  RUNTIME_SVA,
  RUNTIME_BB
};

enum CheckingRuntimeType DefaultRuntime = RUNTIME_DEBUG;

static cl::opt<enum CheckingRuntimeType>
CheckingRuntime("runtime", cl::init(DefaultRuntime),
                  cl::desc("The runtime API used by the program"),
                  cl::values(
  clEnumVal(RUNTIME_PA,           "Pool Allocation runtime  no checks)"),
  clEnumVal(RUNTIME_DEBUG,        "Debugging Tool runtime"),
  clEnumVal(RUNTIME_SINGLETHREAD, "Single Thread runtime (Production version)"),
  clEnumVal(RUNTIME_PARALLEL,     "Parallel Checking runtime (Production version)"),
  clEnumVal(RUNTIME_QUEUE_OP,     "Parallel no-op Checking runtime (For testing queue performance)"),
  clEnumVal(RUNTIME_SVA,          "Runtime for SVA"),
  clEnumVal(RUNTIME_BB,           "Runtime for BaggyBounds"),
  clEnumValEnd));

static cl::opt<bool>
EnableProtectingMetaData("protect-metadata", cl::init(false),
			 cl::desc("Instrument store instructions to protect the meta data"));

static cl::opt<bool>
EnableCodeDuplication("code-duplication", cl::init(false),
			 cl::desc("Enable Code Duplication for SAFECode checking"));

#define NOT_FOR_SVA(X) do { if (!SCConfig.svaEnabled()) X; } while (0);

static void addLowerIntrinsicPass(PassManager & Passes, CheckingRuntimeType type);
static void addStaticGEPCheckingPass(PassManager & Passes);
static void addPoolAllocationPass(PassManager & Passes);

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
  SimpleAllocatorInfo AllocatorPoolAlloc("malloc", "free", 1, 1);
}


// main - Entry point for the sc compiler.
//
int main(int argc, char **argv) {
  LLVMContext &Context = getGlobalContext();
  llvm_shutdown_obj ShutdownObject;

  try {
    cl::ParseCommandLineOptions(argc, argv, "SAFECode Compiler\n");
    sys::PrintStackTraceOnErrorSignal();

    // Load the module to be compiled...
    std::auto_ptr<Module> M;
    std::string ErrorMessage;
    if (MemoryBuffer *Buffer
          = MemoryBuffer::getFileOrSTDIN(InputFilename, &ErrorMessage)) {
      M.reset(ParseBitcodeFile(Buffer, Context, &ErrorMessage));
      delete Buffer;
    }

    if (M.get() == 0) {
      std::cerr << argv[0] << ": bytecode didn't read correctly.\n";
      return 1;
    }

    // The type of DSA depends on which pool allocation pass is used.
    if (SCConfig.svaEnabled()) {
      SCConfig.allocators.push_back(&AllocatorVMalloc);
      SCConfig.allocators.push_back(&AllocatorKMalloc);
      SCConfig.allocators.push_back(&AllocatorBootmem);
    } else {
      SCConfig.allocators.push_back(&AllocatorPoolAlloc);
    }

    // Build up all of the passes that we want to do to the module...
    PassManager Passes;
    Passes.add(new TargetData(M.get()));

    //
    // Create a new allocator information pass and schedule it for execution.
    //
    AllocatorInfoPass * AllocInfo = new AllocatorInfoPass ();
    if (SCConfig.svaEnabled()) {
      AllocInfo->addAllocator (&AllocatorVMalloc);
      AllocInfo->addAllocator (&AllocatorKMalloc);
      AllocInfo->addAllocator (&AllocatorBootmem);
    }

    //
    // Merge constants.  We do this here because merging constants *after*
    // running SAFECode may cause incorrect registration of global objects
    // (e.g., two global object registrations may register the same object
    // because the globals are identical constant strings).
    //
    Passes.add (createConstantMergePass());

    // Remove all constant GEP expressions
    NOT_FOR_SVA(Passes.add(new BreakConstantGEPs()));

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
    if (CheckingRuntime != RUNTIME_PA) {
      if (!DisableTypeSafetyOpts) {
        Passes.add(new ArrayBoundsCheckLocal());
        NOT_FOR_SVA(Passes.add(new ConvertUnsafeAllocas()));
      }
    }

    //
    // Transform C Standard Library function calls.
    //
    if (!DisableCStdLib) {
      if (CheckingRuntime == RUNTIME_DEBUG) {
        NOT_FOR_SVA(Passes.add(new StringTransform()));
      }
    }

    //
    // Transform format string functions.
    //
    if (!DisableFSChecks && CheckingRuntime == RUNTIME_DEBUG)
      NOT_FOR_SVA(Passes.add(new FormatStringTransform()));

    //
    // Ensure that all type-safe stack allocations are initialized.
    //
    NOT_FOR_SVA(Passes.add(new InitAllocas()));

    //
    // Disable this pass for now.  We don't really use it, and it generates
    // lots of compiler warnings.
    //
#if 0
    Passes.add(new EmbeCFreeRemoval());
#endif

    //
    // Use static analysis to determine which indexing operations (GEPs) do not
    // require run-time checks.  This is scheduled right before the check
    // insertion pass because it seems that the PassManager will invalidate the
    // results if they are not consumed immediently.
    //
    // Note that we must schedule DSA to run before the static GEP checking
    // pass manually.  If we don't, then PassManager just throws the static
    // GEP checking pass away.
    //
    Passes.add (new EQTDDataStructures());
    Passes.add(new InsertPoolChecks());

    if (!DisableLSChecks)  Passes.add(new InsertLSChecks());
    if (!DisableGEPChecks) {
      addStaticGEPCheckingPass(Passes);
      Passes.add(new InsertGEPChecks());
    }

    //
    // Go ahead and make all of the run-time checks complete.  This tool can
    // use DSA (which makes this transform possible).
    //
    Passes.add(new CompleteChecks());

    //
    // Optimize away type-safe load/store checks if we're using automatic pool
    // allocation.  If we optimize away type-safe loads and stores, insert
    // alignment checks.
    //
    if (CheckingRuntime != RUNTIME_BB){
      if (SCConfig.getPAType() == SAFECodeConfiguration::PA_APA) {
        if (!DisableTypeSafetyOpts) {
          Passes.add(new OptimizeSafeLoadStore());
          Passes.add(new AlignmentChecks());
        }
      }
    }

    //
    // Instrument the code so that memory objects are registered into the
    // correct pools.  Note that user-space SAFECode requires a few additional
    // transforms to do this.
    //
    Passes.add(new RegisterGlobalVariables());

    if (!SCConfig.svaEnabled()) {
      Passes.add(new RegisterMainArgs());
      Passes.add(new RegisterRuntimeInitializer());
    }

    Passes.add(new RegisterFunctionByvalArguments());

    // Register all customized allocators, such as vmalloc() / kmalloc() in
    // kernel, or poolalloc() in pool allocation
    Passes.add(new RegisterCustomizedAllocation());      

    if (!DisableExactChecks) Passes.add(new ExactCheckOpt());

    NOT_FOR_SVA(Passes.add(new RegisterStackObjPass()));

#if 0
    if (EnableFastCallChecks)
      Passes.add(createIndirectCallChecksPass());
#endif

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
    // Note that the RewriteOOB pass is always required for user-space
    // SAFECode because it is how we handle the C standard allowing pointers to
    // move one beyond the end of an object as long as the pointer is not
    // dereferenced.
    //
    // Try to optimize the checks first as the RewriteOOB pass may make
    // optimization impossible.
    //
    if (CheckingRuntime == RUNTIME_DEBUG) {
      Passes.add(new OptimizeChecks());
      Passes.add(new RewriteOOB());
    }
    if (CheckingRuntime == RUNTIME_BB) {
      Passes.add(new InsertBaggyBoundsChecks());
      Passes.add(new OptimizeChecks());
      Passes.add(new RewriteOOB());
    }

    //
    // Run pool allocation.
    //
    addPoolAllocationPass(Passes);

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
    // Attempt to optimize the checks.  Do not optimize object registration
    // in debug mode because we need to use pool_unregister to detect invalid
    // frees.
    //
    Passes.add (new OptimizeChecks());
    if (CheckingRuntime != RUNTIME_BB) {
      if (DisableDebugInfo) {
        if (SCConfig.getPAType() == SAFECodeConfiguration::PA_APA) {
          Passes.add (new PoolRegisterElimination());
        }
      } else {
#if 0
        Passes.add (new DebugPoolRegisterElimination());
#endif
      }
    }

    Passes.add(new UnusedCheckElimination());

    //
    // Instrument the code so that dangling pointers are detected.
    //
    Passes.add(new DetectDanglingPointers());

    if (!DisableDebugInfo)
      Passes.add (new DebugInstrument());

    // Lower the checking intrinsics into appropriate runtime function calls.
    // It should be the last pass
    addLowerIntrinsicPass(Passes, CheckingRuntime);

    // Make all strings non-constant so that the linker doesn't try to merge
    // them together.
    Passes.add(new BreakConstantStrings());

    //
    // Remove pool metadata.
    //
    Passes.add(new RemovePoolMDPass());

    // Verify the final result
    Passes.add(createVerifierPass());

    // Figure out where we are going to send the output...
    raw_fd_ostream *Out = 0;
    std::string error;
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
        Out = new raw_fd_ostream (OutputFilename.c_str(), error);

        // Make sure that the Out file gets unlinked from the disk if we get a
        // SIGINT
        sys::RemoveFileOnSignal(sys::Path(OutputFilename));
      } else {
        Out = new raw_stdout_ostream();
      }
    } else {
      if (InputFilename == "-") {
        OutputFilename = "-";
        Out = new raw_stdout_ostream();
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
      
      Out = new raw_fd_ostream(OutputFilename.c_str(), error);
      if (error.length()) {
        std::cerr << argv[0] << ": error opening " << OutputFilename << "!\n";
        delete Out;
        return 1;
      }

      // Make sure that the Out file gets unlinked from the disk if we get a
      // SIGINT
      sys::RemoveFileOnSignal(sys::Path(OutputFilename));
    }

    // Add the writing of the output file to the list of passes
    Passes.add (createBitcodeWriterPass(*Out));

    // Run our queue of passes all at once now, efficiently.
    Passes.run(*M.get());

    // Delete the ostream
    delete Out;
  
    return 0;
  } catch (const std::string & msg) {
    std::cerr << argv[0] << ": " << msg << "\n";
  } catch (...) {
    std::cerr << argv[0] << ": Unexpected unknown exception occurred.\n";
  }
  llvm_shutdown();
  return 1;
}

static void addStaticGEPCheckingPass(PassManager & Passes) {
	switch (SCConfig.staticCheckType()) {
		case SAFECodeConfiguration::ABC_CHECK_NONE:
			Passes.add(new ArrayBoundsCheckDummy());
			break;
		case SAFECodeConfiguration::ABC_CHECK_LOCAL:
#if 0
      if (SCConfig.getPAType() == SAFECodeConfiguration::PA_APA) {
        Passes.add(new ArrayBoundsCheckStruct());
      }
#endif
			Passes.add(new ArrayBoundsCheckLocal());
			break;
		case SAFECodeConfiguration::ABC_CHECK_FULL:
      if (SCConfig.getPAType() == SAFECodeConfiguration::PA_APA) {
        Passes.add(new ArrayBoundsCheckStruct());
      }
#if 0
			Passes.add(new ArrayBoundsCheck());
#else
			assert (0 && "Omega pass is not working right now!");
#endif
			break;
	}
}

static inline void
addLowerIntrinsicPass(PassManager & Passes, CheckingRuntimeType type) {
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
      {"sc.funccheck",       "__sc_dbg_funccheck" },
      {"sc.get_actual_val",  "pchk_getActualValue" },
      {"sc.pool_register",   "__sc_dbg_poolregister" },
      {"sc.pool_unregister", "__sc_dbg_poolunregister" },
      {"sc.pool_unregister_stack", "__sc_dbg_poolunregister_stack" },
      {"sc.pool_unregister_debug", "__sc_dbg_poolunregister_debug" },
      {"sc.pool_unregister_stack_debug", "__sc_dbg_poolunregister_stack_debug" },
      {"poolalloc",         "__pa_bitmap_poolalloc"},
      {"poolfree",          "__pa_bitmap_poolfree"},

      {"sc.init_pool_runtime", "pool_init_runtime"},
      {"sc.pool_register_debug", "__sc_dbg_src_poolregister"},
      {"sc.pool_register_stack_debug", "__sc_dbg_src_poolregister_stack"},
      {"sc.pool_register_stack", "__sc_dbg_poolregister_stack"},
      {"sc.pool_register_global", "__sc_dbg_poolregister_global"},
      {"sc.pool_register_global_debug", "__sc_dbg_poolregister_global_debug"},
      {"sc.pool_reregister", "__sc_dbg_poolreregister"},
      {"sc.pool_reregister_debug", "__sc_dbg_src_poolreregister"},

      {"sc.lscheck_debug",      "poolcheck_debug"},
      {"sc.lscheckui_debug",    "poolcheckui_debug"},
      {"sc.lscheckalign_debug", "poolcheckalign_debug"},
      {"sc.boundscheck_debug",  "boundscheck_debug"},
      {"sc.boundscheckui_debug","boundscheckui_debug"},
      {"sc.exactcheck2_debug",  "exactcheck2_debug"},
      {"sc.pool_argvregister",  "__sc_dbg_poolargvregister"},

      {"poolinit",              "__sc_dbg_poolinit"},
      {"pooldestroy",           "__sc_dbg_pooldestroy"},
      {"poolalloc_debug",       "__sc_dbg_src_poolalloc"},
      {"poolfree_debug",        "__sc_dbg_src_poolfree"},

      // CStdLib
      {"pool_strcat_debug",     "pool_strcat_debug"},
      {"pool_strcpy_debug",     "pool_strcpy_debug"},
      {"pool_stpcpy_debug",     "pool_stpcpy_debug"},
      {"pool_strchr_debug",     "pool_strchr_debug"},
      {"pool_strlen_debug",     "pool_strlen_debug"},
      {"pool_strncat_debug",    "pool_strncat_debug"},
      {"pool_strpbrk_debug",    "pool_strpbrk_debug"},
      {"pool_strrchr_debug",    "pool_strrchr_debug"},
      {"pool_strstr_debug",     "pool_strstr_debug"},
      {"pool_strcmp_debug",     "pool_strcmp_debug"},
      {"pool_strncmp_debug",    "pool_strncmp_debug"},
      {"pool_memcmp_debug",     "pool_memcmp_debug"},
      {"pool_strcasecmp_debug", "pool_strcasecmp_debug"},
      {"pool_strncasecmp_debug","pool_strncasecmp_debug"},
      {"pool_strspn_debug",     "pool_strspn_debug"},
      {"pool_strcspn_debug",    "pool_strcspn_debug"},
      {"pool_strncpy_debug",    "pool_strncpy_debug"},
      {"pool_memccpy_debug",    "pool_memccpy_debug"},
      {"pool_memchr_debug",     "pool_memchr_debug"},
      {"pool_bcmp_debug",       "pool_bcmp_debug"},
      {"pool_bcopy_debug",      "pool_bcopy_debug"},
      {"pool_index_debug",      "pool_index_debug"},
      {"pool_rindex_debug",     "pool_rindex_debug"},
      {"pool_strcasestr_debug", "pool_strcasestr_debug"},

      // Format string functions
      {"sc.fsparameter",        "__sc_fsparameter"},
      {"sc.fscallinfo",         "__sc_fscallinfo"},
      {"sc.fscallinfo_debug",   "__sc_fscallinfo_debug"},
      {"pool_printf",           "pool_printf"},
      {"pool_fprintf",          "pool_fprintf"},
      {"pool_sprintf",          "pool_sprintf"},
      {"pool_snprintf",         "pool_snprintf"},
      {"pool_err",              "pool_err"},
      {"pool_errx",             "pool_errx"},
      {"pool_warn",             "pool_warn"},
      {"pool_warnx",            "pool_warnx"},
      {"pool_syslog",           "pool_syslog"},
      {"pool_scanf",            "pool_scanf"},
      {"pool_fscanf",           "pool_fscanf"},
      {"pool_sscanf",           "pool_sscanf"},

      // These functions register objects in the splay trees
      {"poolcalloc_debug",      "__sc_dbg_src_poolcalloc"},
      {"poolcalloc",            "__sc_dbg_poolcalloc"},
      {"poolstrdup",            "__sc_dbg_poolstrdup"},
      {"poolstrdup_debug",      "__sc_dbg_poolstrdup_debug"},
      {"poolrealloc",           "__sc_dbg_poolrealloc"},
      {"poolrealloc_debug",     "__sc_dbg_poolrealloc_debug"},
      {"poolmemalign",          "__sc_dbg_poolmemalign"},
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
  
  static IntrinsicMappingEntry RuntimeBB[] = 
    { {"sc.lscheck",         "bb_poolcheck" },
      {"sc.lscheckui",       "bb_poolcheckui" },
      {"sc.lscheckalign",    "bb_poolcheckalign" },
      {"sc.lscheckalignui",  "bb_poolcheckalignui" },
      {"sc.boundscheck",     "bb_boundscheck" },
      {"sc.boundscheckui",   "bb_boundscheckui" },
      {"sc.exactcheck",      "bb_exactcheck" },
      {"sc.exactcheck2",     "bb_exactcheck2" },
      {"sc.funccheck",       "__sc_bb_funccheck" },
      {"sc.get_actual_val",  "pchk_getActualValue" },
      {"sc.pool_register",   "__sc_bb_poolregister" },
      {"sc.pool_unregister", "__sc_bb_poolunregister" },
      {"sc.pool_unregister_stack", "__sc_bb_poolunregister_stack" },
      {"sc.pool_unregister_debug", "__sc_bb_poolunregister_debug" },
      {"sc.pool_unregister_stack_debug", "__sc_bb_poolunregister_stack_debug" },
      {"poolalloc",         "__sc_bb_poolalloc"},
      {"poolfree",          "__sc_bb_poolfree"},

      {"sc.init_pool_runtime", "pool_init_runtime"},
      {"sc.pool_register_debug", "__sc_bb_src_poolregister"},
      {"sc.pool_register_stack_debug", "__sc_bb_src_poolregister_stack"},
      {"sc.pool_register_stack", "__sc_bb_poolregister_stack"},
      {"sc.pool_register_global", "__sc_bb_poolregister_global"},
      {"sc.pool_register_global_debug", "__sc_bb_poolregister_global_debug"},
      {"sc.lscheck_debug",      "bb_poolcheck_debug"},
      {"sc.lscheckui_debug",    "bb_poolcheck_debug"},
      {"sc.lscheckalign_debug", "bb_poolcheckalign_debug"},
      {"sc.boundscheck_debug",  "bb_boundscheck_debug"},
      {"sc.boundscheckui_debug","bb_boundscheckui_debug"},
      {"sc.exactcheck2_debug",  "bb_exactcheck2_debug"},
      {"sc.pool_argvregister",  "__sc_bb_poolargvregister"},

      {"poolinit",              "__sc_bb_poolinit"},
      {"pooldestroy",           "__sc_bb_pooldestroy"},
      {"poolalloc_debug",       "__sc_bb_src_poolalloc"},
      {"poolfree_debug",        "__sc_bb_src_poolfree"},

      // These functions register objects in the splay trees
      {"poolcalloc_debug",      "__sc_bb_src_poolcalloc"},
      {"poolcalloc",            "__sc_bb_poolcalloc"},
      {"poolstrdup",            "__sc_bb_poolstrdup"},
      {"poolstrdup_debug",      "__sc_bb_poolstrdup_debug"},
      {"poolrealloc",           "__sc_bb_poolrealloc"},
      {"poolrealloc_debug",     "__sc_bb_poolrealloc_debug"},
      {"poolmemalign",          "__sc_bb_poolmemalign"},
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
  
  case RUNTIME_BB:
    Passes.add(new LowerSafecodeIntrinsic(RuntimeBB, RuntimeBB + sizeof(RuntimeBB) / sizeof(IntrinsicMappingEntry)));
    break;

  default:
    assert (0 && "Invalid Runtime!");
  }
}

static inline void addPoolAllocationPass(PassManager & Passes) {
  if (CheckingRuntime == RUNTIME_BB) {
    Passes.add(new PoolAllocateSimple(true, true, false));
    return;
  }
  switch (SCConfig.getPAType()) {
  case SAFECodeConfiguration::PA_SINGLE:
    Passes.add(new PoolAllocateSimple(true, true, false));
    break;
  case SAFECodeConfiguration::PA_SIMPLE:
    Passes.add(new PoolAllocateSimple(true, true, true));
    break;
  case SAFECodeConfiguration::PA_MULTI:
    Passes.add(new PoolAllocateMultipleGlobalPool());
    break;
  case SAFECodeConfiguration::PA_APA:
    Passes.add(new PA::SCHeuristic());
    Passes.add(new PoolAllocate(true, true));
    break;
  } 
}

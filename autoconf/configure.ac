dnl **************************************************************************
dnl * Initialize
dnl **************************************************************************
AC_INIT([[[SAFECode]]],[[[1.00]]],[dhurjati@cs.uiuc.edu])

dnl Tell autoconf that the auxilliary files are actually located in
dnl the LLVM autoconf directory, not here.
AC_CONFIG_AUX_DIR(../../autoconf)

dnl Tell autoconf that this is an LLVM project being configured
dnl This provides the --with-llvmsrc and --with-llvmobj options
LLVM_CONFIG_PROJECT

dnl Verify that the source directory is valid
AC_CONFIG_SRCDIR(["Makefile.common.in"])

dnl Configure a common Makefile
AC_CONFIG_FILES(Makefile.common)

dnl Configure project makefiles
dnl List every Makefile that exists within your source tree
AC_CONFIG_MAKEFILE(Makefile)
AC_CONFIG_MAKEFILE(lib/Makefile)
AC_CONFIG_MAKEFILE(lib/ArrayBoundChecks/Makefile)
AC_CONFIG_MAKEFILE(lib/ConvertUnsafeAllocas/Makefile)
AC_CONFIG_MAKEFILE(lib/InsertPoolChecks/Makefile)
AC_CONFIG_MAKEFILE(lib/PointerChecks/Makefile)
AC_CONFIG_MAKEFILE(lib/StackSafety/Makefile)

dnl **************************************************************************
dnl * Determine which system we are building on
dnl **************************************************************************

dnl **************************************************************************
dnl * Check for programs.
dnl **************************************************************************

AC_PATH_PROG(OMEGA, [omega], [echo omega])
if test "$OMEGA" != "echo omega" ; then
  AC_DEFINE([HAVE_OMEGA],[1],[Define if the Omega program is available])
  AC_DEFINE_UNQUOTED([LLVM_PATH_OMEGA],"$OMEGA",
   [Define to path of Omega program if found or 'echo omega' otherwise])
fi

dnl **************************************************************************
dnl * Check for libraries.
dnl **************************************************************************

dnl **************************************************************************
dnl * Checks for header files.
dnl **************************************************************************

dnl **************************************************************************
dnl * Checks for typedefs, structures, and compiler characteristics.
dnl **************************************************************************

dnl **************************************************************************
dnl * Checks for library functions.
dnl **************************************************************************

dnl **************************************************************************
dnl * Enable various compile-time options
dnl **************************************************************************

dnl **************************************************************************
dnl * Set the location of various third-party software packages
dnl **************************************************************************

dnl Specify the location of the llvm-poolalloc project
AC_ARG_WITH(poolalloc-srcdir,
  AS_HELP_STRING([--with-poolalloc-srcdir],
    [Specify location of Pool Allocation source code]),
    AC_SUBST(poolallocsrcdir,"$withval"),
    AC_SUBST(poolallocsrcdir,"$llvm_src/projects/llvm-poolalloc"
    ))

AC_ARG_WITH(poolalloc-objdir,
  AS_HELP_STRING([--with-poolalloc-objdir],
    [Specify location of Pool Allocation object code]),
    AC_SUBST(poolallocobjdir,"$withval"),
    AC_SUBST(poolallocobjdir,"$llvm_obj/projects/llvm-poolalloc"
    ))

dnl **************************************************************************
dnl * Create the output files
dnl **************************************************************************

dnl This must be last
AC_OUTPUT

/*===- UserPoolSystem.cpp - Implementation of callbacks needed by runtimes ===*/
/*                                                                            */
/*                            SAFECode Compiler                               */
/*                                                                            */
/* This file was developed by the LLVM research group and is distributed      */
/* under the University of Illinois Open Source License. See LICENSE.TXT for  */
/* details.                                                                   */
/*                                                                            */
/*===----------------------------------------------------------------------===*/
/*                                                                            */
/* This file implements the callbacks for userspace code that are required by */
/* the various SAFECode runtime libraries.                                    */
/*                                                                            */
/*===----------------------------------------------------------------------===*/

#include <stdio.h>
#include <unistd.h>

void
poolcheckfail (const char * msg, int i, void* p)
{
  fprintf (stderr, "poolcheckfail: %s: %x : %x\n", msg, i, p);
  fflush (stderr);
}

void
poolcheckfatal (const char * msg, int i)
{
  fprintf (stderr, "poolcheckfatal: %s: %x\n", msg, i);
  fflush (stderr);
  exit (1);
}

void
poolcheckinfo (const char * msg, int i)
{
  printf ("poolcheckinfo: %s %x\n", msg, i);
  fflush (stdout);
  return;
}

void
poolcheckinfo2 (const char * msg, int a, int b)
{
  printf ("poolcheckinfo: %s %x %x\n", msg, a, b);
  fflush (stdout);
  return;
}

void *
poolcheckmalloc (unsigned int power)
{
  return malloc (4096 * (1U << power));
}

void *
sp_malloc (unsigned int size)
{
  return malloc (size);
}

void
printpoolinfo (void *Pool)
{
  return;
}

int
llva_load_lif (int i)
{
  return 0;
}

int
llva_save_lif ()
{
  return 0;
}

int
llva_save_tsc ()
{
  return 0;
}


/*===- Stats.cpp - Statistics held by the runtime -------------------------===*/
/*                                                                            */
/*                       The LLVM Compiler Infrastructure                     */
/*                                                                            */
/* This file was developed by the LLVM research group and is distributed      */
/* under the University of Illinois Open Source License. See LICENSE.TXT for  */
/* details.                                                                   */
/*                                                                            */
/*===----------------------------------------------------------------------===*/
/*                                                                            */
/* This file implements functions that can be used to hold statistics         */
/* information                                                                */
/*                                                                            */
/*===----------------------------------------------------------------------===*/

/* The number of stack to heap promotions executed dynamically */
static int stack_promotes = 0;

int stat_exactcheck  = 0;
int stat_exactcheck2 = 0;
int stat_exactcheck3 = 0;

extern int stat_poolcheck;
extern int stat_poolcheckarray;
extern int stat_poolcheckarray_i;
extern int stat_boundscheck;
extern int stat_boundscheck_i;
extern unsigned int externallocs;
extern unsigned int allallocs;

void
stackpromote()
{
  ++stack_promotes;
  return;
}

int
getstackpromotes()
{
  poolcheckinfo ("LLVA: getstackpromotes", stack_promotes);
  poolcheckinfo ("LLVA: stat_exactcheck", stat_exactcheck);
  poolcheckinfo ("LLVA: stat_exactcheck2", stat_exactcheck2);
  poolcheckinfo ("LLVA: stat_exactcheck3", stat_exactcheck3);
  poolcheckinfo ("LLVA: stat_poolcheck", stat_poolcheck);
  poolcheckinfo ("LLVA: stat_poolcheckarray", stat_poolcheckarray);
  poolcheckinfo ("LLVA: stat_poolcheckarray_i", stat_poolcheckarray_i);
  poolcheckinfo ("LLVA: stat_boundscheck", stat_boundscheck);
  poolcheckinfo ("LLVA: stat_boundscheck_i", stat_boundscheck_i);
  poolcheckinfo ("LLVA: external allocs", externallocs);
  poolcheckinfo ("LLVA: all      allocs", allallocs);
  return stack_promotes;
}


#!/bin/sh

TOTAL=`ls -1 *result-*|wc -l`
TMPFN=`tempfile`
grep "FAILED" *result-* > $TMPFN
FAILED=`cat $TMPFN|wc -l`

TMPFN2=`tempfile`
grep "MISMATCH" *result-* > $TMPFN2
MISMATCH=`cat $TMPFN2|wc -l`

printf "Total Cases: %d\tFailed:%d\tMismatched Debug Info:%d\n" $TOTAL $FAILED $MISMATCH

echo "\n"
echo "Details"
echo "================================================================="

cat $TMPFN $TMPFN2|sort|sed - "s/:/		| /g"

rm $TMPFN $TMPFN2
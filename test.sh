#!/bin/bash

declare -r TESTDIR=$(dirname $(readlink -f "${BASH_SOURCE}"))

. ${TESTDIR}/assert.sh.inc

assertExitValue "18|9" "${TESTDIR}/query_int_parser '18|9'  2>/dev/null | grep -xq 'H = 000000020000000900000012E000'" $TRUE
assertExitValue "9|18" "${TESTDIR}/query_int_parser '9|18'  2>/dev/null | grep -xq 'H = 000000020000000900000012E000'" $TRUE
assertExitValue "45|53|21" "${TESTDIR}/query_int_parser '45|53|21' 2>/dev/null | grep -xq 'H = 00000003000000150000002D00000035EF00'" $TRUE
assertExitValue "45|53|21" "${TESTDIR}/query_int_parser '123|28|456|7' 2>/dev/null | grep -xq 'H = 00000004000000070000001C0000007B000001C8EFFF00'" $TRUE

exit $?

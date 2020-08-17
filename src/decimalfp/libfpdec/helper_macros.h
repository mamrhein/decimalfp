/*
------------------------------------------------------------------------------
Name:        helper_macros.h

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application or library.
             For license details please read the file LICENSE provided
             together with the application or library.
------------------------------------------------------------------------------
$Source$
$Revision$
*/


#ifndef FPDEC_HELPER_MACROS_H
#define FPDEC_HELPER_MACROS_H

#include <stddef.h>

#include "common.h"


/*****************************************************************************
*  Macros
*****************************************************************************/

// error return
#define ERROR(err) {errno = err; return err;}
#define ERROR_RETVAL(err, retval) {errno = err; return retval;}
#define MEMERROR ERROR(ENOMEM)
#define MEMERROR_RETVAL(retval) ERROR_RETVAL(ENOMEM, retval)

// int compare
#define CMP(a, b) (((a) > (b)) - ((a) < (b)))

// max / min
#define MAX(a, b) ((a) >= (b) ? (a) : (b))
#define MIN(a, b) ((a) <= (b) ? (a) : (b))

// modulo arith
#define CEIL(a, b) (((a) % (b)) <= 0 ? (a) / (b) : (a) / (b) + 1)
#define FLOOR(a, b) (((a) % (b)) < 0 ? (a) / (b) - 1 : (a) / (b))
#define MOD(a, b) ((a) - FLOOR((a), (b)) * (b))

// powers of 10
static uint64_t _10_pows[20] = {
        1UL,
        10UL,
        100UL,
        1000UL,
        10000UL,
        100000UL,
        1000000UL,
        10000000UL,
        100000000UL,
        1000000000UL,
        10000000000UL,
        100000000000UL,
        1000000000000UL,
        10000000000000UL,
        100000000000000UL,
        1000000000000000UL,
        10000000000000000UL,
        100000000000000000UL,
        1000000000000000000UL,
        10000000000000000000UL
};
#define _10_POW_N(exp) (_10_pows[exp])

static uint128_t _DW_10_POWS[10] = {
        {7766279631452241920UL,  5UL},
        {3875820019684212736UL,  54UL},
        {1864712049423024128UL,  542UL},
        {200376420520689664UL,   5421UL},
        {2003764205206896640UL,  54210UL},
        {1590897978359414784UL,  542101UL},
        {15908979783594147840UL, 5421010UL},
        {11515845246265065472UL, 54210108UL},
        {4477988020393345024UL,  542101086UL},
        {7886392056514347008UL,  5421010862UL},
};
#define _DW_10_POW_N(exp) (_DW_10_POWS[exp - 20])

#endif //FPDEC_HELPER_MACROS_H

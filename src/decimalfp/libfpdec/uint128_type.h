/* ---------------------------------------------------------------------------
Name:        uint128_type.h

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application. For license
             details please read the file LICENSE.TXT provided together
             with the application.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#ifndef FPDEC_UINT128_TYPE_H
#define FPDEC_UINT128_TYPE_H

#include <stdint.h>

/*****************************************************************************
*  Types
*****************************************************************************/

// large unsigned int
typedef struct uint128 {
    uint64_t lo;
    uint64_t hi;
} uint128_t;

static const uint128_t UINT128_ZERO = {0, 0};
static const uint128_t UINT128_MAX = {UINT64_MAX, UINT64_MAX};

#endif //FPDEC_UINT128_TYPE_H

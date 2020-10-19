/* ---------------------------------------------------------------------------
Name:        uint128_type_native.h

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application. For license
             details please read the file LICENSE.TXT provided together
             with the application.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#ifndef FPDEC_UINT128_TYPE_NATIVE_H
#define FPDEC_UINT128_TYPE_NATIVE_H

#include <stdint.h>

/*****************************************************************************
*  Types
*****************************************************************************/

// large unsigned int
typedef unsigned __int128 uint128_t;

static const uint128_t UINT128_ZERO = 0ULL;
static const uint128_t UINT128_MAX =
    ((uint128_t)UINT64_MAX << 64U) + UINT64_MAX;

#endif //FPDEC_UINT128_TYPE_NATIVE_H

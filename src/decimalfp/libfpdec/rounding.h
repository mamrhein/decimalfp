/*
------------------------------------------------------------------------------
Name:        rounding.h

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application or library.
             For license details please read the file LICENSE provided
             together with the application or library.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#ifndef FPDEC_ROUNDING_H
#define FPDEC_ROUNDING_H

enum FPDEC_ROUNDING_MODE {
    // Use default rounding mode
    FPDEC_ROUND_DEFAULT = 0,
    // Round away from zero if last digit after rounding towards
    // zero would have been 0 or 5; otherwise round towards zero.
    FPDEC_ROUND_05UP = 1,
    // Round towards Infinity.
    FPDEC_ROUND_CEILING = 2,
    // Round towards zero.
    FPDEC_ROUND_DOWN = 3,
    // Round towards -Infinity.
    FPDEC_ROUND_FLOOR = 4,
    // Round to nearest with ties going towards zero.
    FPDEC_ROUND_HALF_DOWN = 5,
    // Round to nearest with ties going to nearest even integer.
    FPDEC_ROUND_HALF_EVEN = 6,
    // Round to nearest with ties going away from zero.
    FPDEC_ROUND_HALF_UP = 7,
    // Round away from zero.
    FPDEC_ROUND_UP = 8,
};

#define FPDEC_MAX_ROUNDING_MODE 8

enum FPDEC_ROUNDING_MODE
fpdec_get_default_rounding_mode();

enum FPDEC_ROUNDING_MODE
fpdec_set_default_rounding_mode(enum FPDEC_ROUNDING_MODE);

#endif //FPDEC_ROUNDING_H

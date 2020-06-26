/*
------------------------------------------------------------------------------
Name:        rounding.c

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application or library.
             For license details please read the file LICENSE provided
             together with the application or library.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#include <assert.h>

#include "rounding_.h"


static enum FPDEC_ROUNDING_MODE dflt_rounding_mode = FPDEC_ROUND_HALF_EVEN;


enum FPDEC_ROUNDING_MODE
fpdec_get_default_rounding_mode() {
    return dflt_rounding_mode;
}


enum FPDEC_ROUNDING_MODE
fpdec_set_default_rounding_mode(enum FPDEC_ROUNDING_MODE rnd) {
    assert(rnd > FPDEC_ROUND_DEFAULT);
    assert(rnd <= FPDEC_MAX_ROUNDING_MODE);

    dflt_rounding_mode = rnd;
    return rnd;
}


fpdec_digit_t
round_to_multiple(fpdec_sign_t sign, fpdec_digit_t num, bool delta,
                  fpdec_digit_t quant, enum FPDEC_ROUNDING_MODE rounding) {
    fpdec_digit_t rem;

    rem = num % quant;
    if (rem == 0 && !delta)
        return num;
    else
        return num - rem + quant *
                round_qr(sign, num / quant, rem, delta, quant, rounding);
}

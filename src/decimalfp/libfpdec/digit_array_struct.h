/*
------------------------------------------------------------------------------
Name:        digit_array_struct.h

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application or library.
             For license details please read the file LICENSE provided
             together with the application or library.
------------------------------------------------------------------------------
$Source$
$Revision$
*/


#ifndef FPDEC_DIGIT_ARRAY_STRUCT_H
#define FPDEC_DIGIT_ARRAY_STRUCT_H

/*****************************************************************************
*  Types
*****************************************************************************/

struct fpdec_digit_array {
    fpdec_n_digits_t n_alloc;
    fpdec_n_digits_t n_signif;
    fpdec_digit_t digits[1];
};

#endif //FPDEC_DIGIT_ARRAY_STRUCT_H

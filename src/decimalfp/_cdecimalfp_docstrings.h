/* ---------------------------------------------------------------------------
Name:        _cdecimalfp_docstrings.h

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application. For license
             details please read the file LICENSE.TXT provided together
             with the application.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#ifndef DECIMALFP__CDECIMALFP_DOCSTRINGS_H
#define DECIMALFP__CDECIMALFP_DOCSTRINGS_H

#include <pymacro.h>

// Decimal class methods

PyDoc_STRVAR(
    DecimalType_from_float_doc,
    "Convert a finite float (or int) to a :class:`Decimal`.\n\n"
    "Args:\n"
    "    f (float or int): number to be converted to a `Decimal`\n\n"
    "Returns:\n"
    "    :class:`Decimal` instance derived from `f`\n\n"
    "Raises:\n"
    "    TypeError: `f` is neither a `float` nor an `int`.\n"
    "    ValueError: `f` can not be converted to a :class:`Decimal` with\n"
    "        a precision <= the maximal precision.\n\n"
    "Beware that Decimal.from_float(0.3) != Decimal('0.3').\n"
);

PyDoc_STRVAR(

    DecimalType_from_decimal_doc,
    "Convert a finite decimal number to a :class:`Decimal`.\n\n"
    "Args:\n"
    "    d (see below): decimal number to be converted to a :class:`Decimal`\n\n"
    "Returns:\n"
    "    :class:`Decimal` instance derived from `d`\n\n"
    "Raises:\n"
    "    TypeError: `d` is not an instance of the types listed above.\n"
    "    ValueError: `d` can not be converted to a :class:`Decimal`.\n\n"
);

PyDoc_STRVAR(
    DecimalType_from_real_doc,
    "TODO");

PyDoc_STRVAR(
    Decimal_adjusted_doc,
    "TODO");

PyDoc_STRVAR(
    Decimal_quantize_doc,
    "TODO");

PyDoc_STRVAR(
    Decimal_as_tuple_doc,
    "TODO");

PyDoc_STRVAR(
    Decimal_as_fraction_doc,
    "TODO");

PyDoc_STRVAR(
    Decimal_as_integer_ratio_doc,
    "TODO");

PyDoc_STRVAR(
    Decimal_copy_doc,
    "TODO");

PyDoc_STRVAR(
    Decimal_reduce_doc,
    "TODO");

PyDoc_STRVAR(
    Decimal_setstate_doc,
    "TODO");

PyDoc_STRVAR(
    Decimal_format_doc,
    "TODO");

PyDoc_STRVAR(
    Decimal_floor_doc,
    "TODO");

PyDoc_STRVAR(
    Decimal_ceil_doc,
    "TODO");

PyDoc_STRVAR(
    Decimal_round_doc,
    "TODO");


// _cdecimalfp module level functions

PyDoc_STRVAR(
    get_dflt_rounding_mode_doc,
    "Return default rounding mode."
);

PyDoc_STRVAR(
    set_dflt_rounding_mode_doc,
    "Set default rounding mode.\n\n"
    "Args:\n"
    "    rounding (ROUNDING): rounding mode to be set as default\n\n"
    "Raises:\n"
    "    TypeError: given 'rounding' is not a valid rounding mode\n\n"
);

#endif //DECIMALFP__CDECIMALFP_DOCSTRINGS_H

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

// Decimal type

PyDoc_STRVAR(
    DecimalType_doc,
    "Decimal number with a given number of fractional digits.\n\n"
    "Args:\n"
    "   value (see below): numerical value (default: None)\n"
    "   precision (numbers.Integral): number of fractional digits (default:\n"
    "       None)\n\n"
    "If `value` is given, it must either be a string, an instance of\n"
    "`numbers.Integral`, `number.Rational` (for example `fractions.Fraction`),\n"
    "`decimal.Decimal`, a finite instance of `numbers.Real` (for example `float`)\n"
    "or be convertable to a `float` or an `int`.\n\n"
    "If a string is given as value, it must be a string in one of two formats:\n\n"
    "    * [+|-]<int>[.<frac>][<e|E>[+|-]<exp>] or\n"
    "    * [+|-].<frac>[<e|E>[+|-]<exp>].\n\n"
    "If given value is `None`, Decimal(0) is returned.\n\n"
    "Returns:\n"
    "    :class:`Decimal` instance derived from `value` according\n"
    "    to `precision`\n\n"
    "The value is always adjusted to the given precision or the precision is\n"
    "calculated from the given value, if no precision is given.\n\n"
    "Raises:\n"
    "    TypeError: `precision` is given, but not of type `Integral`.\n"
    "    TypeError: `value` is not an instance of the types listed above and\n"
    "        not convertable to `float` or `int`.\n"
    "    ValueError: `precision` is given, but not >= 0.\n"
    "    ValueError: `precision` is given, but not <= `MAX_DEC_PRECISION`.\n"
    "    ValueError: `value` can not be converted to a `Decimal` (with a number\n"
    "        of fractional digits <= `MAX_DEC_PRECISION`).\n\n"
    ":class:`Decimal` instances are immutable.\n\n"
);

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
    "Convert a finite Real number to a :class:`Decimal`.\n\n"
    "Args:\n"
    "    r (`numbers.Real`): number to be converted to a :class:`Decimal`\n"
    "    exact (`bool`): `True` if `r` shall exactly be represented by\n"
    "        the resulting :class:`Decimal`\n\n"
    "Returns:\n"
    "    :class:`Decimal` instance derived from `r`\n\n"
    "Raises:\n"
    "    TypeError: `r` is not an instance of `numbers.Real`.\n"
    "    ValueError: `exact` is `True` and `r` can not exactly be converted\n"
    "        to a :class:`Decimal` with a precision <= `MAX_DEC_PRECISION`.\n\n"
    "If `exact` is `False` and `r` can not exactly be represented by a\n"
    "`Decimal` with a precision <= `MAX_DEC_PRECISION`, the result is\n"
    "rounded to a precision = `MAX_DEC_PRECISION`.\n\n"
);

// Decimal properties

PyDoc_STRVAR(
    Decimal_precision_doc,
    "Return precision of `self`."
);

PyDoc_STRVAR(
    Decimal_magnitude_doc,
    "Return magnitude of `self` in terms of power to 10.\n\n"
    "I.e. the largest integer exp so that 10 ** exp <= self.\n\n"
);

PyDoc_STRVAR(
    Decimal_numerator_doc,
    "Return the normalized numerator of `self`.\n\n"
    "I. e. the numerator from the pair of integers with the smallest\n"
    "positive denominator, whose ratio is equal to `self`.\n\n"
);

PyDoc_STRVAR(
    Decimal_denominator_doc,
    "Return the normalized denominator of 'self'.\n\n"
    "I. e. the smallest positive denominator from the pairs of integers,\n"
    "whose ratio is equal to `self`.\n\n"
);

PyDoc_STRVAR(
    Decimal_real_doc,
    "Return real part of `self`.\n\n"
    "Returns `self` (Real numbers are their real component).\n\n"
);

PyDoc_STRVAR(
    Decimal_imag_doc,
    "Return imaginary part of `self`.\n\n"
    "Returns 0 (Real numbers have no imaginary component).\n\n"
);

// Decimal instance methods

PyDoc_STRVAR(
    Decimal_adjusted_doc,
    "Return adjusted copy of `self`.\n\n"
    "Args:\n"
    "    precision (numbers.Integral): number of fractional digits (default: None)\n"
    "    rounding (ROUNDING): rounding mode (default: None)\n\n"
    "Returns:\n"
    "    :class:`Decimal` instance derived from `self`, adjusted to the given\n"
    "        `precision`, using the given `rounding` mode\n\n"
    "If no `precision` is given, the result is adjusted to the minimum\n"
    "precision preserving x == x.adjusted().\n\n"
    "If no `rounding` mode is given, the current default rounding mode is used.\n\n"
    "If the given `precision` is less than the precision of `self`, the\n"
    "result is rounded and thus information may be lost.\n\n"
);

PyDoc_STRVAR(
    Decimal_quantize_doc,
    "Return integer multiple of `quant` closest to `self`.\n\n"
    "Args:\n"
    "    quant (Number): quantum to get a multiple from; must be a `Rational`\n"
    "        or a number which is convertable to a `Rational`\n"
    "        (i. e. must support 'as_integer_ratio')\n"
    "    rounding (ROUNDING): rounding mode (default: None)\n\n"
    "If no `rounding` mode is given, the current default rounding mode is\n"
    "used.\n\n"
    "Returns:\n"
    "    :class:`Decimal` instance that is the integer multiple of `quant`\n"
    "        closest to `self` (according to `rounding` mode); if result\n"
    "        can not be represented as :class:`Decimal`, an instance of\n"
    "        `Fraction` is returned\n\n"
    "Raises:\n"
    "    TypeError: `quant` is not a number or does not support\n"
    "        `as_integer_ratio`\n"
    "    ValueError: `quant` is not convertable to a `Rational`\n\n"
);

PyDoc_STRVAR(
    Decimal_as_tuple_doc,
    "Return a tuple (sign, coeff, exp) equivalent to `self`.\n\n"
    "self == sign * coeff * 10 ** exp.\n\n"
    "sign in (-1, 0, 1), for self < 0, = 0, > 0.\n"
    "coeff = 0 only if self = 0, without trailing zeros otherwise.\n\n"
);

PyDoc_STRVAR(
    Decimal_as_fraction_doc,
    "Return an instance of `Fraction` equal to `self`.\n\n"
    "Returns the `Fraction` with the smallest positive denominator, whose\n"
    "ratio is equal to `self`.\n\n");

PyDoc_STRVAR(
    Decimal_as_integer_ratio_doc,
    "Return a pair of integers whose ratio is equal to `self`.\n\n"
    "Returns the pair of numerator and denominator with the smallest\n"
    "positive denominator, whose ratio is equal to `self`.\n\n");

// Decimal special methods

PyDoc_STRVAR(
    Decimal_copy_doc,
    "Return self (Decimal instances are immutable).");

PyDoc_STRVAR(
    Decimal_getstate_doc,
    "Return state representation of `self`.");

PyDoc_STRVAR(
    Decimal_setstate_doc,
    "Set state of `self` from `state`.");

PyDoc_STRVAR(
    Decimal_bytes_doc,
    "bytes(self)");

PyDoc_STRVAR(
    Decimal_format_doc,
    "Return `self` converted to a string according to `fmt_spec`.\n\n"
    "Args:\n"
    "    fmt_spec (str): a standard format specifier for a number\n\n"
    "Returns:\n"
    "    str: `self` converted to a string according to `fmt_spec`\n\n");

PyDoc_STRVAR(
    Decimal_trunc_doc,
    "math.trunc(self)");

PyDoc_STRVAR(
    Decimal_floor_doc,
    "math.floor(self)");

PyDoc_STRVAR(
    Decimal_ceil_doc,
    "math.ceil(self)");

PyDoc_STRVAR(
    Decimal_round_doc,
    "round(self [, n_digits])\n\n"
    "Round `self` to a given precision in decimal digits (default 0).\n"
    "`n_digits` may be negative.\n\n"
    "This method is called by the built-in `round` function. It returns an\n"
    "`int` when called with one argument, otherwise a :class:`Decimal`.\n\n");


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

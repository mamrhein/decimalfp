The module `decimalfp` provides a `Decimal` number type which can represent
decimal numbers of arbitrary magnitude and arbitrary precision, i.e. any
number of fractional digits.

Usage
-----

`decimalfp.Decimal` instances are created by giving a `value` (default: 0) and
a `precision` (i.e the number of fractional digits, default: None).

If `precision` is given, it must be of type `int` and >= 0.

If `value` is given, it must either be a string (type `str` or `unicode` in
Python 2.x, `bytes` or `str` in Python 3.x), an instance of `number.Integral`
(for example `int` or `long` in Python 2.x, `int` in Python 3.x),
`number.Rational` (for example `fractions.Fraction`), `decimal.Decimal` or
`float` or be convertable to a `float` or an `int`.

If a string is given as value, it must be a string in one of two formats:

* [+|-]<int>[.<frac>][<e|E>[+|-]<exp>] or
* [+|-].<frac>[<e|E>[+|-]<exp>].

The value is always adjusted to the given precision or the precision is
calculated from the given value, if no precision is given.

`Decimal` instances are immutable.

Computations
------------

When importing `decimalfp`, its `Decimal` type is registered in Pythons
numerical stack as `number.Rational`. It supports all operations defined for
that base class and its instances can be mixed in computations with instances
of all numeric types mentioned above.

All numerical operations give an exact result, i.e. they are not automatically
constraint to the precision of the operands or to a number of significant
digits (like the floating-point `Decimal` type from the standard module
`decimal`). Adding and subtracting give a result with a precision equal to the
maximum of the precisions of the operands. Multiplying results have a
precision equal to the sum of the precisions of the operands. For performance
reasons, division tries to give an exact result as Decimal only up to a fixed
limit of fractional digits. This limit defaults to 32 and is accessible as
`decimalfp.LIMIT_PREC`. If the result can not be represented as a Decimal
within this limit, a `fractions.Fraction` is returned.

`Decimal` supports rounding via the built-in function `round` using the same
rounding mode as the `float` type by default (i.e. ROUND_HALF_UP in Pyhton 2.x
and ROUND_HALF_EVEN in Python 3.x). In addition, via the method `adjusted` a
`Decimal` with a different precision can be derived, supporting all rounding
modes defined by the standard module `decimal`.

For more details see the documentation provided with the source distribution
or `here <http://pythonhosted.org/decimalfp>`_.

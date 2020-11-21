The module _decimalfp_ provides a _Decimal_ number type which can represent
decimal numbers of (nearly) arbitrary magnitude and very large precision, i.e. 
with a very large number of fractional digits.

### Usage

_decimalfp.Decimal_ instances are created by giving a _value_ (default: 0) and
a _precision_ (i.e the number of fractional digits, default: None).

If _precision_ is given, it must be of type _int_ and >= 0.

If _value_ is given, it must either be a string, an instance of 
_number.Integral_, _number.Rational_ (for example _fractions.Fraction_), 
_decimal.Decimal_ or _float_ or be convertable to a _float_ or an _int_.

If a string is given as value, it must be a string in one of two formats:

    [+|-]<int>[.<frac>][<e|E>[+|-]<exp>] or
    [+|-].<frac>[<e|E>[+|-]<exp>].

The value is always adjusted to the given precision or the precision is
calculated from the given value, if no precision is given.

When the given _precision_ is lower than the precision of the given _value_,
the result is rounded, according to the current default rounding mode (which 
itself defaults to ROUND_HALF_EVEN).

When no _precision_ is given and the given _value_ is a _float_ or a
_numbers.Rational_ (but no _Decimal_), the _Decimal_ constructor tries to
convert _value_ exactly. But this is done only up a fixed limit of fractional 
digits (imposed by the implementation, currently 65535). If _value_ can not be 
represented as a _Decimal_ within this limit, an exception is raised.

_Decimal_ does not deal with infinity, division by 0 always raises a
_ZeroDivisionError_. Likewise, infinite instances of type _float_ or
_decimal.Decimal_ can not be converted to _Decimal_ instances. The same is
true for the 'not a number' instances of these types.

### Computations

When importing _decimalfp_, its _Decimal_ type is registered in Pythons
numerical stack as subclass of _number.Rational_. It supports all operations 
defined for that base class and its instances can be mixed in computations 
with instances of all numeric types mentioned above.

All numerical operations give an exact result, i.e. they are not automatically
constraint to the precision of the operands or to a number of significant
digits (like the floating-point _Decimal_ type from the standard module
_decimal_). When the result can not exactly be represented by a _Decimal_
instance within the limit of fractional digits, an instance of
_fractions.Fraction_ is returned.

_Decimal_ supports rounding via the built-in function _round_ using the same
rounding mode as the _float_ type by default (ROUND_HALF_EVEN in Python 3). In 
addition, via the method _adjusted_, a _Decimal_ with a different precision 
can be derived, supporting all rounding modes defined by the standard library 
module _decimal_.

For more details see the documentation provided with the source distribution
or [here](https://decimalfp.readthedocs.io/en/latest).

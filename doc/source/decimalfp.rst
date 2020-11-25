*******************************************
Decimal numbers with fixed-point arithmetic
*******************************************

.. automodule:: decimalfp

Class `Decimal`
---------------

.. autoclass:: decimalfp.Decimal
    :members: from_float, from_decimal, from_real,
        precision, magnitude, numerator, denominator, real, imag,
        adjusted, quantize, as_fraction, as_integer_ratio, as_tuple, __hash__,
        __eq__, __lt__, __le__, __gt__, __ge__,
        __abs__, __neg__, __pos__,
        __add__, __radd__, __sub__, __rsub__,
        __mul__, __rmul__, __pow__,
        __div__, __rdiv__, __truediv__, __rtruediv__,
        __trunc__, __floor__, __ceil__, __round__,
        __repr__, __str__, __bytes__, __format__

Rounding modes
--------------

`Decimal` supports rounding modes equivalent to those defined by the standard
library module `decimal`: ROUND_DOWN, ROUND_UP, ROUND_HALF_DOWN,
ROUND_HALF_UP, ROUND_HALF_EVEN, ROUND_CEILING, ROUND_FLOOR and ROUND_05UP.

The rounding modes are wrapped into the Enum :class:`ROUNDING`.

.. autoclass:: ROUNDING
    :members: ROUND_05UP, ROUND_CEILING, ROUND_DOWN, ROUND_FLOOR,
        ROUND_HALF_DOWN, ROUND_HALF_EVEN, ROUND_HALF_UP, ROUND_UP

Unless a rounding mode is explicitely given, the rounding mode set as current
default is used. To get or set the default rounding mode, the package
`decimalfp` provides the following two functions:

.. autofunction:: get_dflt_rounding_mode

.. autofunction:: set_dflt_rounding_mode

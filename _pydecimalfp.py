# -*- coding: utf-8 -*-
##----------------------------------------------------------------------------
## Name:        _pydecimalfp
## Purpose:     Decimal fixed-point arithmetic (Python implementation)
##
## Author:      Michael Amrhein (mamrhein@users.sourceforge.net)
##
## Copyright:   (c) 2001-2014 Michael Amrhein
##              Portions adopted from FixedPoint.py written by Tim Peters
## License:     This program is free software. You can redistribute it, use it
##              and/or modify it under the terms of the 2-clause BSD license.
##              For license details please read the file LICENSE.TXT provided
##              together with the source code.
##----------------------------------------------------------------------------
## $Source$
## $Revision$

"""Decimal fixed-point arithmetic."""

from __future__ import absolute_import, division
import sys
import math
import numbers
import operator
from decimalfp import _get_limit_prec, get_rounding
from decimal import Decimal as _StdLibDecimal
from fractions import gcd, Fraction
from functools import reduce
import locale
# rounding modes
from decimal import ROUND_DOWN, ROUND_UP, ROUND_HALF_DOWN, ROUND_HALF_UP,\
    ROUND_HALF_EVEN, ROUND_CEILING, ROUND_FLOOR, ROUND_05UP


__version__ = 0, 9, 6


# Python 2 / Python 3
if sys.version_info[0] < 3:
    # rounding mode of builtin round function
    DFLT_ROUNDING = ROUND_HALF_UP
    # Compatible testing for strings
    str = type(u'')
    bytes = type(b'')
else:
    # In 3.0 round changed from half-up to half-even !
    DFLT_ROUNDING = ROUND_HALF_EVEN
# Compatible testing for strings
str_types = (bytes, str)


class Decimal(numbers.Rational):

    """Decimal number with a given number of fractional digits.

    Args:
        value (see below): numerical value (default: 0)
        precision (int): number of fractional digits (default: None)

    If `value` is given, it must either be a string (type `str` or `unicode`
    in Python 2.x, `bytes` or `str` in Python 3.x), an instance of
    `number.Integral` (for example `int` or `long` in Python 2.x, `int` in
    Python 3.x), `number.Rational` (for example `fractions.Fraction`),
    `decimal.Decimal` or `float` or be convertable to a `float` or an `int`.

    If a string is given as value, it must be a string in one of two formats:

    * [+|-]<int>[.<frac>][<e|E>[+|-]<exp>] or

    * [+|-].<frac>[<e|E>[+|-]<exp>].

    Returns:
        :class:`Decimal` instance derived from `value` according
            to `precision`

    The value is always adjusted to the given precision or the precision is
    calculated from the given value, if no precision is given. For performance
    reasons, in the latter case the conversion of a `numbers.Rational` (like
    `fractions.Fraction`) or a `float` tries to give an exact result as a
    :class:`Decimal` only up to a fixed limit of fractional digits. This limit
    defaults to 32 and is accessible as `decimalfp.LIMIT_PREC`.

    Raises:
        TypeError: `precision` is given, but not of type `int`.
        TypeError: `value` is not an instance of the types listed above and
            not convertable to `float` or `int`.
        ValueError: `precision` is given, but not >= 0.
        ValueError: `value` can not be converted to a `Decimal` (with a number
            of fractional digits <= `LIMIT_PREC` if no `precision` is given).

    :class:`Decimal` instances are immutable.
    """

    __slots__ = ('_value', '_precision',
                 # used for caching values only:
                 '_hash', '_numerator', '_denominator')

    def __new__(cls, value=0, precision=None):
        if precision is not None:
            if not isinstance(precision, int):
                raise TypeError("Precision must be of <type 'int'>.")
            if precision < 0:
                raise ValueError("Precision must be >= 0.")
        self = super(Decimal, cls).__new__(cls)
        # only used as cache:
        self._hash = self._numerator = self._denominator = None

        # Decimal
        if isinstance(value, Decimal):
            vp = value._precision
            if precision is None or precision == vp:
                self._precision = vp
                self._value = value._value
            else:
                self._precision = precision
                self._value = _adjust(value._value, vp, precision)
            return self

        # String
        if isinstance(value, str_types):
            try:
                s = value.decode()
            except AttributeError:
                s = value
            i, e = _string2exact(s)
            if i is None:
                raise ValueError("Can't convert %s to Decimal." % repr(value))
            self._value, self._precision = _convert_exp(i, e, precision)
            return self

        # Integral
        if isinstance(value, numbers.Integral):
            if precision is None:
                self._precision = 0
                self._value = int(value)
            else:
                self._precision = precision
                self._value = int(value * 10 ** precision)
            return self

        # Decimal (from standard library)
        if isinstance(value, _StdLibDecimal):
            if value.is_finite():
                sign, digits, exp = value.as_tuple()
                i = (-1) ** sign * reduce(lambda x, y: x * 10 + y, digits)
                self._value, self._precision = _convert_exp(i, exp, precision)
                return self
            else:
                raise ValueError("Can't convert %s to Decimal." % repr(value))

        # Rational
        if isinstance(value, numbers.Rational):
            n, d = value.numerator, value.denominator
            if precision is None:
                self._value, self._precision, r = _approx_rational(n, d)
                if r != 0:
                    raise ValueError("Can't convert %s exactly to Decimal."
                                     % repr(value))
            else:
                self._value = _convert_rational(n, d, precision)
                self._precision = precision
            return self

        # Float
        if isinstance(value, float):
            try:
                n, d = value.as_integer_ratio()
            except (ValueError, OverflowError):
                raise ValueError("Can't convert %s to Decimal." % repr(value))
            if precision is None:
                self._value, self._precision, r = _approx_rational(n, d)
                if r != 0:
                    raise ValueError("Can't convert %s exactly to Decimal."
                                     % repr(value))
            else:
                self._value = _convert_rational(n, d, precision)
                self._precision = precision
            return self

        # Others
        # If there's a float or int equivalent to value, use it
        ev = None
        try:
            ev = float(value)
        except:
            try:
                ev = int(value)
            except:
                pass
        if ev == value:     # do we really have the same value?
            return cls(ev, precision)

        # unable to create Decimal
        raise TypeError("Can't convert %s to Decimal." % repr(value))

    # to be compatible to fractions.Fraction
    @classmethod
    def from_float(cls, f):
        """Convert a finite float (or int) to a :class:`Decimal`.

        Args:
            f (float or int): number to be converted to a `Decimal`

        Returns:
            :class:`Decimal` instance derived from `f`

        Raises:
            TypeError: `f` is neither a `float` nor an `int`.
            ValueError: `f` can not be converted to a :class:`Decimal` with
                a precision <= `LIMIT_PREC`.

        Beware that Decimal.from_float(0.3) != Decimal('0.3').
        """
        if not isinstance(f, (float, numbers.Integral)):
            raise TypeError("%s is not a float." % repr(f))
        return cls(f)

    # to be compatible to fractions.Fraction
    @classmethod
    def from_decimal(cls, d):
        """Convert a finite decimal number to a :class:`Decimal`.

        Args:
            d (see below): decimal number to be converted to a
                :class:`Decimal`

        `d` can be of type :class:`Decimal`, `numbers.Integral` or
        `decimal.Decimal`.

        Returns:
            :class:`Decimal` instance derived from `d`

        Raises:
            TypeError: `d` is not an instance of the types listed above.
            ValueError: `d` can not be converted to a :class:`Decimal`.
        """
        if not isinstance(d, (Decimal, numbers.Integral, _StdLibDecimal)):
            raise TypeError("%s is not a Decimal." % repr(d))
        return cls(d)

    @classmethod
    def from_real(cls, r, exact=True):
        """Convert a Real number to a :class:`Decimal`.

        Args:
            r (`numbers.Real`): number to be converted to a :class:`Decimal`
            exact (`bool`): `True` if `r` shall exactly be represented by
                the resulting :class:`Decimal`

        Returns:
            :class:`Decimal` instance derived from `r`

        Raises:
            TypeError: `r` is not an instance of `numbers.Real`.
            ValueError: `exact` is `True` and `r` can not exactly be converted
                to a :class:`Decimal` with a precision <= `LIMIT_PREC`.

        If `exact` is `False` and `r` can not exactly be represented by a
        `Decimal` with a precision <= `LIMIT_PREC`, the result is rounded to a
        precision = `LIMIT_PREC`.
        """
        if not isinstance(r, numbers.Real):
            raise TypeError("%s is not a Real." % repr(r))
        try:
            return cls(r)
        except ValueError:
            if exact:
                raise
            else:
                return cls(r, _get_limit_prec())

    @property
    def precision(self):
        """Return precision of `self`."""
        return self._precision

    @property
    def magnitude(self):
        """Return magnitude of `self` in terms of power to 10, i.e. the
        smallest integer exp so that 10 ** exp >= self."""
        return len(str(self._value)) - self._precision

    @property
    def numerator(self):
        """Return the numerator from the pair of integers with the smallest
        positive denominator, whose ratio is equal to `self`."""
        if self._numerator is not None:
            return self._numerator
        n, d = self.as_integer_ratio()
        return n

    @property
    def denominator(self):
        """Return the smallest positive denominator from the pairs of
        integers, whose ratio is equal to `self`."""
        if self._denominator is not None:
            return self._denominator
        n, d = self.as_integer_ratio()
        return d

    @property
    def real(self):
        """The real part of `self`.

        Returns `self` (Real numbers are their real component)."""
        return self

    @property
    def imag(self):
        """The imaginary part of `self`.

        Returns 0 (Real numbers have no imaginary component)."""
        return 0

    def adjusted(self, precision=None, rounding=None):
        """Return copy of `self`, adjusted to the given `precision`, using the
        given `rounding` mode.

        Args:
            precision (int): number of fractional digits (default: None)
            rounding(str): rounding mode (default: None)

        Returns:
            :class:`Decimal` instance derived from `self`, adjusted
                to the given `precision`, using the given `rounding` mode

        If no `precision` is given, the result is adjusted to the minimum
        precision preserving x == x.adjusted().

        If no `rounding` mode is given, the default mode from the current
        context (from module `decimal`) is used.

        If the given `precision` is less than the precision of `self`, the
        result is rounded and thus information may be lost.
        """
        if precision is None:
            result = Decimal(self)
            result._value, result._precision = _reduce(self._value,
                                                       self._precision)
        else:
            if not isinstance(precision, int):
                raise TypeError("Precision must be of <type 'int'>.")
            #if precision < 0:
            #    raise ValueError("Precision must be >= 0.")
            result = Decimal(self)
            result._value = _adjust(self._value, self._precision, precision,
                                    rounding)
            result._precision = max(precision, 0)
        return result

    def as_tuple(self):
        """Return a tuple (sign, coeff, exp) so that
        self == (-1) ** sign * coeff * 10 ** exp."""
        v = self._value
        sign = int(v < 0)
        coeff = abs(v)
        exp = - self.precision
        return sign, coeff, exp

    # return lowest fraction equal to self
    def as_integer_ratio(self):
        """Return the pair of numerator and denominator with the smallest
        positive denominator, whose ratio is equal to `self`."""
        n, d = self._value, 10 ** self._precision
        g = gcd(n, d)
        n, d = self._numerator, self._denominator = n // g, d // g
        return n, d

    def __copy__(self):
        """Return self (Decimal instances are immutable)."""
        return self

    def __deepcopy__(self, memo):
        return self.__copy__()

    def __reduce__(self):
        """Helper for pickle"""
        return (_r, (self._value, self._precision))

    # string representation
    def __repr__(self):
        """repr(self)"""
        sp = self._precision
        rv, rp = _reduce(self._value, sp)
        if rp == 0:
            s = str(rv)
        else:
            s = str(abs(rv))
            n = len(s)
            if n > rp:
                s = "'%s%s.%s'" % ((rv < 0)*'-', s[0:-rp], s[-rp:])
            else:
                s = "'%s0.%s%s'" % ((rv < 0)*'-', (rp-n)*'0', s)
        if sp == rp:
            return "Decimal(%s)" % (s)
        else:
            return "Decimal(%s, %s)" % (s, sp)

    def __str__(self):
        """str(self)"""
        sp = self._precision
        if sp == 0:
            return "%i" % self._value
        else:
            sv = self._value
            i = _int(sv, sp)
            f = sv - i * 10 ** sp
            s = (i == 0 and f < 0)*'-'  # -1 < self < 0 => i = 0 and f < 0 !!!
            return '%s%i.%0*i' % (s, i, sp, abs(f))

    def __lstr__(self):
        """locale.str(self)"""
        return self.__format__('n')

    def __format__(self, fmtSpec):
        """Return `self` converted to a string according to given format
        specifier.

        Args:
            fmtSpec (str): a standard format specifier for a number

        Returns:
            str: `self` converted to a string according to `fmtSpec`
        """
        (fmtFill, fmtAlign, fmtSign, fmtMinWidth, fmtThousandsSep,
            fmtGrouping, fmtDecimalPoint, fmtPrecision,
            fmtType) = _getFormatParams(fmtSpec)
        nToFill = fmtMinWidth
        prec = self._precision
        if fmtPrecision is None:
            fmtPrecision = prec
        if fmtType == '%':
            percentSign = '%'
            nToFill -= 1
            xtraShift = 2
        else:
            percentSign = ''
            xtraShift = 0
        val = _adjust(self._value, self._precision, fmtPrecision + xtraShift)
        if val < 0:
            sign = '-'
            nToFill -= 1
            val = abs(val)
        elif fmtSign == '-':
            sign = ''
        else:
            sign = fmtSign
            nToFill -= 1
        rawDigits = "%i" % val
        if fmtPrecision:
            decimalPoint = fmtDecimalPoint
            rawDigits, fracPart = (rawDigits[:-fmtPrecision],
                                   rawDigits[-fmtPrecision:])
            nToFill -= fmtPrecision + 1
        else:
            decimalPoint = ''
            fracPart = ''
        if fmtAlign == '=':
            intPart = _padDigits(rawDigits, max(0, nToFill), fmtFill,
                                 fmtThousandsSep, fmtGrouping)
            return sign + intPart + decimalPoint + fracPart + percentSign
        else:
            intPart = _padDigits(rawDigits, 0, fmtFill,
                                 fmtThousandsSep, fmtGrouping)
            raw = sign + intPart + decimalPoint + fracPart + percentSign
            if nToFill > len(intPart):
                fmt = "%s%s%i" % (fmtFill, fmtAlign, fmtMinWidth)
                return format(raw, fmt)
            else:
                return raw

    # compare to Decimal or any type that can be converted to a Decimal
    def _make_comparable(self, other):
        if isinstance(other, Decimal):
            sp, op = self._precision, other._precision
            if sp == op:
                return self._value, other._value
            elif sp < op:
                return _adjust(self._value, sp, op), other._value
            else:
                return self._value, _adjust(other._value, op, sp)
        if isinstance(other, numbers.Integral):
            return self._value, other * 10 ** self._precision
        if isinstance(other, numbers.Rational):
            return (self.numerator * other.denominator,
                    other.numerator * self.denominator)
        if isinstance(other, float):
            n, d = other.as_integer_ratio()
            return (self.numerator * d, n * self.denominator)
        if isinstance(other, _StdLibDecimal):
            return (self, Decimal(other))
        if isinstance(other, numbers.Complex) and other.imag == 0:
            return self._make_comparable(other.real)
        else:
            raise TypeError

    def _compare(self, other, op):
        """Compare self and other using operator op."""
        try:
            sv, ov = self._make_comparable(other)
        except TypeError:
            return NotImplemented
        return op(sv, ov)

    def __eq__(self, other):
        """self == other"""
        return self._compare(other, operator.eq)

    def __lt__(self, other):
        """self < other"""
        return self._compare(other, operator.lt)

    def __le__(self, other):
        """self <= other"""
        return self._compare(other, operator.le)

    def __gt__(self, other):
        """self > other"""
        return self._compare(other, operator.gt)

    def __ge__(self, other):
        """self >= other"""
        return self._compare(other, operator.ge)

    # compute hash index
    def __hash__(self):
        """hash(self)"""
        if self._hash is not None:
            return self._hash
        sv, sp = self._value, self._precision
        if sp == 0:                         # if self == int(self),
            h = self._hash = hash(sv)       # same hash as int
        else:                               # otherwise same hash as
                                            # equivalent fraction
            h = self._hash = hash(Fraction(sv, 10 ** sp))
        return h

    # return 0 or 1 for truth-value testing
    def __bool__(self):
        """bool(self)"""
        return self._value != 0
    __nonzero__ = __bool__

    # return integer portion as int
    def __trunc__(self):
        """math.trunc(self)"""
        return _int(self._value, self._precision)
    __int__ = __trunc__

    # convert to float (may loose precision!)
    def __float__(self):
        """float(self)"""
        return self._value / 10 ** self._precision

    def __pos__(self):
        """+self"""
        return self

    def __neg__(self):
        """-self"""
        result = Decimal(self)
        result._value = -result._value
        return result

    def __abs__(self):
        """abs(self)"""
        result = Decimal(self)
        result._value = abs(result._value)
        return result

    def __add__(self, other):
        """self + other"""
        if isinstance(other, Decimal):
            p = self._precision - other._precision
            if p == 0:
                result = Decimal(self)
                result._value += other._value
            elif p > 0:
                result = Decimal(self)
                result._value += other._value * 10 ** p
            else:
                result = Decimal(other)
                result._value += self._value * 10 ** -p
            return result
        elif isinstance(other, numbers.Integral):
            result = Decimal(self)
            result._value += other * 10 ** self._precision
            return result
        elif isinstance(other, numbers.Rational):
            other_numerator, other_denominator = (other.numerator,
                                                  other.denominator)
        elif isinstance(other, float):
            other_numerator, other_denominator = other.as_integer_ratio()
        elif isinstance(other, _StdLibDecimal):
            return self.__add__(Decimal(other))
        else:
            return NotImplemented
        # handle Rational and float
        self_denominator = 10 ** self._precision
        num = self._value * other_denominator + (self_denominator *
                                                 other_numerator)
        den = other_denominator * self_denominator
        minPrec = self._precision
        # return num / den as Decimal or as Fraction
        return _div(num, den, minPrec)

    # other + self
    __radd__ = __add__

    def __sub__(self, other):
        """self - other"""
        if isinstance(other, Decimal):
            p = self._precision - other._precision
            if p == 0:
                result = Decimal(self)
                result._value -= other._value
            elif p > 0:
                result = Decimal(self)
                result._value -= other._value * 10 ** p
            else:
                result = Decimal(other)
                result._value = self._value * 10 ** -p - other._value
            return result
        elif isinstance(other, numbers.Integral):
            result = Decimal(self)
            result._value -= other * 10 ** self._precision
            return result
        elif isinstance(other, numbers.Rational):
            other_numerator, other_denominator = (other.numerator,
                                                  other.denominator)
        elif isinstance(other, float):
            other_numerator, other_denominator = other.as_integer_ratio()
        elif isinstance(other, _StdLibDecimal):
            return self.__sub__(Decimal(other))
        else:
            return NotImplemented
        # handle Rational and float
        self_denominator = 10 ** self._precision
        num = self._value * other_denominator - (self_denominator *
                                                 other_numerator)
        den = other_denominator * self_denominator
        minPrec = self._precision
        # return num / den as Decimal or as Fraction
        return _div(num, den, minPrec)

    def __rsub__(self, other):
        """other - self"""
        return self.__neg__().__add__(other)

    def __mul__(self, other):
        """self * other"""
        if isinstance(other, Decimal):
            result = Decimal(self)
            result._value *= other._value
            result._precision += other._precision
            return result
        elif isinstance(other, numbers.Integral):
            result = Decimal(self)
            result._value *= other
            return result
        elif isinstance(other, numbers.Rational):
            other_numerator, other_denominator = (other.numerator,
                                                  other.denominator)
        elif isinstance(other, float):
            other_numerator, other_denominator = other.as_integer_ratio()
        elif isinstance(other, _StdLibDecimal):
            return self.__mul__(Decimal(other))
        else:
            return NotImplemented
        # handle Rational and float
        num = self._value * other_numerator
        den = other_denominator * 10 ** self._precision
        minPrec = self._precision
        # return num / den as Decimal or as Fraction
        return _div(num, den, minPrec)

    # other * self
    __rmul__ = __mul__

    def __div__(self, other):
        """self / other"""
        if isinstance(other, Decimal):
            sp, op = self._precision, other._precision
            num = self._value * 10 ** op
            den = other._value * 10 ** sp
            minPrec = max(sp, op)
            # return num / den as Decimal or as Fraction
            return _div(num, den, minPrec)
        elif isinstance(other, numbers.Rational):       # includes Integral
            other_numerator, other_denominator = (other.numerator,
                                                  other.denominator)
        elif isinstance(other, float):
            other_numerator, other_denominator = other.as_integer_ratio()
        elif isinstance(other, _StdLibDecimal):
            return self.__div__(Decimal(other))
        else:
            return NotImplemented
        # handle Rational and float
        num = self._value * other_denominator
        den = other_numerator * 10 ** self._precision
        minPrec = self._precision
        # return num / den as Decimal or as Fraction
        return _div(num, den, minPrec)

    def __rdiv__(self, other):
        """other / self"""
        if isinstance(other, numbers.Rational):
            other_numerator, other_denominator = (other.numerator,
                                                  other.denominator)
        elif isinstance(other, float):
            other_numerator, other_denominator = other.as_integer_ratio()
        elif isinstance(other, _StdLibDecimal):
            return Decimal(other).__div__(self)
        else:
            return NotImplemented
        # handle Rational and float
        num = other_numerator * 10 ** self._precision
        den = self._value * other_denominator
        minPrec = self._precision
        # return num / den as Decimal or as Fraction
        return _div(num, den, minPrec)

    # Decimal division is true division
    __truediv__ = __div__
    __rtruediv__ = __rdiv__

    def __divmod__(self, other):
        """self // other, self % other"""
        if isinstance(other, Decimal):
            sp, op = self._precision, other._precision
            if sp >= op:
                r = Decimal(self)
                sv = self._value
                ov = _adjust(other._value, op, sp)
            else:
                r = Decimal(other)
                sv = _adjust(self._value, sp, op)
                ov = other._value
            q = sv // ov
            r._value = sv - q * ov
            return Decimal(q, r._precision), r
        elif isinstance(other, numbers.Integral):
            r = Decimal(self)
            sv = self._value
            sp = self._precision
            ov = other * 10 ** sp
            q = sv // ov
            r._value = sv - q * ov
            return Decimal(q, sp), r
        elif isinstance(other, _StdLibDecimal):
            return self.__divmod__(Decimal(other))
        else:
            return self // other, self % other

    def __rdivmod__(self, other):
        """other // self, other % self"""
        if isinstance(other, numbers.Integral):
            sp = self._precision
            r = Decimal(other, sp)
            sv = self._value
            ov = other * 10 ** sp
            q = ov // sv
            r._value = ov - q * sv
            return Decimal(q, sp), r
        elif isinstance(other, _StdLibDecimal):
            return Decimal(other).__divmod__(self)
        else:
            return other // self, other % self

    def __floordiv__(self, other):
        """self // other"""
        if isinstance(other, (Decimal, numbers.Integral, _StdLibDecimal)):
            return self.__divmod__(other)[0]
        else:
            return Decimal(math.floor(self / other), self._precision)

    def __rfloordiv__(self, other):
        """other // self"""
        if isinstance(other, (Decimal, numbers.Integral, _StdLibDecimal)):
            return self.__rdivmod__(other)[0]
        else:
            return Decimal(math.floor(other / self), self._precision)

    def __mod__(self, other):
        """self % other"""
        if isinstance(other, (Decimal, numbers.Integral, _StdLibDecimal)):
            return self.__divmod__(other)[1]
        else:
            return self - other * (self // other)

    def __rmod__(self, other):
        """other % self"""
        if isinstance(other, (Decimal, numbers.Integral, _StdLibDecimal)):
            return self.__rdivmod__(other)[1]
        else:
            return other - self * (other // self)

    def __pow__(self, other, mod=None):
        """self ** other

        If other is an integer (or a Rational with denominator = 1), the
        result will be a Decimal. Otherwise, the result will be a float or
        complex since roots are generally irrational.

        `mod` must always be None (otherwise a `TypeError` is raised).
        """
        if mod is not None:
            raise TypeError("3rd argument not allowed unless all arguments "
                            "are integers")
        if isinstance(other, numbers.Integral):
            other = int(other)
            if other >= 0:
                result = Decimal()
                result._value = self._value ** other
                result._precision = self._precision * other
                return result
            else:
                return 1 / self.__pow__(-other, None)
        elif isinstance(other, numbers.Rational):
            if other.denominator == 1:
                return self ** other.numerator
            else:
                return float(self) ** float(other)
        else:
            return float(self) ** other

    def __rpow__(self, other):
        """other ** self"""
        if self.denominator == 1:
            return other ** self.numerator
        return other ** float(self)

    def __floor__(self):
        """math.floor(self)"""
        n, d = self._value, 10 ** self._precision
        return n // d

    def __ceil__(self):
        """math.ceil(self)"""
        n, d = self._value, 10 ** self._precision
        return -(-n // d)

    def __round__(self, ndigits=None):
        """round(self [, ndigits])

        Round `self` to a given precision in decimal digits (default 0).
        `ndigits` may be negative.

        Note: This method is called by the built-in `round` function only in
        Python 3.x! It returns an `int` when called with one argument,
        otherwise a :class:`Decimal`.
        """
        if ndigits is None:
            # return integer
            return int(self.adjusted(0, DFLT_ROUNDING))
        # otherwise return Decimal
        return self.adjusted(ndigits, DFLT_ROUNDING)


# helper functions:


# reconstruct Decimal from pickle
def _r(v, p):
    d = Decimal()
    d._value = v
    d._precision = p
    return d

# parse string
import re
_pattern = r"""
            \s*
            (?P<sign>[+|-])?
            (
                (?P<int>\d+)(\.(?P<frac>\d*))?
                |
                \.(?P<onlyfrac>\d+)
            )
            ([eE](?P<exp>[+|-]?\d+))?
            \s*$
            """
_parseString = re.compile(_pattern, re.VERBOSE).match


def _string2exact(s):
    m = _parseString(s)
    if m is None:
        return None, None
    ep = m.group('exp')
    if ep:
        e = int(ep)
    else:
        e = 0
    ip = m.group('int')
    if ip:
        fp = m.group('frac')
        i = int(ip)
    else:
        fp = m.group('onlyfrac')
        i = 0
    if fp:
        f = int(fp)
        n = len(fp)
    else:
        f = 0
        n = 0
    i = i * 10 ** n + f
    e -= n
    if m.group('sign') == '-':
        i = -i
    return i, e

# parse a format specifier
# [[fill]align][sign][0][minimumwidth][,][.precision][type]

_pattern = r"""
            \A
            (?:
                (?P<fill>.)?
                (?P<align>[<>=^])
            )?
            (?P<sign>[-+ ])?
            (?P<zeropad>0)?
            (?P<minimumwidth>(?!0)\d+)?
            (?P<thousands_sep>,)?
            (?:\.(?P<precision>0|(?!0)\d+))?
            (?P<type>[fFn%])?
            \Z
            """
_parseFormatSpec = re.compile(_pattern, re.VERBOSE).match
del re, _pattern

_dfltFormatParams = {'fill': ' ',
                     'align': '<',
                     'sign': '-',
                     #'zeropad': '',
                     'minimumwidth': 0,
                     'thousands_sep': '',
                     'grouping': [3, 0],
                     'decimal_point': '.',
                     'precision': None,
                     'type': 'f'}


def _getFormatParams(formatSpec):
    m = _parseFormatSpec(formatSpec)
    if m is None:
        raise ValueError("Invalid format specifier: " + formatSpec)
    fill = m.group('fill')
    zeropad = m.group('zeropad')
    if fill:                            # fill overrules zeropad
        fmtFill = fill
        fmtAlign = m.group('align')
    elif zeropad:                       # zeropad overrules align
        fmtFill = '0'
        fmtAlign = "="
    else:
        fmtFill = _dfltFormatParams['fill']
        fmtAlign = m.group('align') or _dfltFormatParams['align']
    fmtSign = m.group('sign') or _dfltFormatParams['sign']
    minimumwidth = m.group('minimumwidth')
    if minimumwidth:
        fmtMinWidth = int(minimumwidth)
    else:
        fmtMinWidth = _dfltFormatParams['minimumwidth']
    fmtType = m.group('type') or _dfltFormatParams['type']
    if fmtType == 'n':
        lconv = locale.localeconv()
        fmtThousandsSep = m.group('thousands_sep') and lconv['thousands_sep']
        fmtGrouping = lconv['grouping']
        fmtDecimalPoint = lconv['decimal_point']
    else:
        fmtThousandsSep = (m.group('thousands_sep') or
                           _dfltFormatParams['thousands_sep'])
        fmtGrouping = _dfltFormatParams['grouping']
        fmtDecimalPoint = _dfltFormatParams['decimal_point']
    precision = m.group('precision')
    if precision:
        fmtPrecision = int(precision)
    else:
        fmtPrecision = None
    return (fmtFill, fmtAlign, fmtSign, fmtMinWidth, fmtThousandsSep,
            fmtGrouping, fmtDecimalPoint, fmtPrecision, fmtType)


def _padDigits(digits, minWidth, fill, sep=None, grouping=None):
    nDigits = len(digits)
    if sep and grouping:
        slices = []
        i = j = 0
        limit = max(minWidth, nDigits) if fill == '0' else nDigits
        for l in _iterGrouping(grouping):
            j = min(i + l, limit)
            #print(i,j,limit)
            slices.append((i, j))
            if j >= limit:
                break
            i = j
            limit = max(limit - 1, nDigits, i + 1)
        if j < limit:
            slices.append((j, limit))
        #print(limit,slices)
        digits = (limit - nDigits) * fill + digits
        raw = sep.join([digits[limit - j: limit - i]
                       for i, j in reversed(slices)])
        return (minWidth - len(raw)) * fill + raw
    else:
        return (minWidth - nDigits) * fill + digits


def _iterGrouping(grouping):
    l = None
    for i in grouping[:-1]:
        yield i
        l = i
    i = grouping[-1]
    if i == 0:
        while l:
            yield l
    elif i != locale.CHAR_MAX:
        yield i


# Helper functions for decimal arithmetic


def _convert_exp(i, e, p):
    """Return v, p so that v / 10 ** p = i * 10 ** e, rounded to precision p
    """
    if p is None:
        if e > 0:
            return i * 10 ** e, 0
        else:
            return i, abs(e)
    else:
        n = e + p
        if n >= 0:
            return i * 10 ** n, p
        else:
            return _div_rounded(i, 10 ** -n), p


def _reduce(v, p):
    """Return rv, rp so that rv // 10 ** rp = v // 10 ** p and rv % 10 != 0
    """
    if v == 0:
        return 0, 0
    while p > 0 and v % 10 == 0:
        p -= 1
        v = v // 10
    return v, p


def _adjust(v, p, rp, rounding=None):
    """Return rv so that rv // 10 ** max(rp, 0) = v // 10 ** p,
    rounded to precision rp using given rounding mode (or default mode if none
    is given)."""
    dp = rp - p
    if dp == 0:
        return v
    elif dp > 0:
        return v * 10 ** dp
    elif rp >= 0:
        return _div_rounded(v, 10 ** -dp, rounding)
    else:
        return _div_rounded(v, 10 ** -dp, rounding) * 10 ** -rp


# divide x by y, return rounded result
def _div_rounded(x, y, rounding=None):
    """Return x // y, rounded using given rounding mode (or default mode
    if none is given)."""
    q, r = divmod(x, y)
    if r == 0:              # no need for rounding
        return q
    return q + _round(q, r, y, rounding)


def _convert_rational(num, den, p):
    return _div_rounded(num * 10 ** p, den)


def _approx_rational(num, den, minPrec=0, maxPrec=None):
    """Approximate x = num / den as Decimal.

    Return q, p, r, so that
        q * 10 ** -p + r = x
    and p <= max(minPrec, LIMIT_PREC) and r -> 0.
    """
    if maxPrec is None:
        maxPrec = max(minPrec, _get_limit_prec())
    while True:
        p = (minPrec + maxPrec) // 2
        q, r = divmod(num * 10 ** p, den)
        if p == maxPrec:
            break
        if r == 0:
            maxPrec = p
        elif minPrec >= maxPrec - 2:
            minPrec = maxPrec
        else:
            minPrec = p
    return q, p, r


def _int(v, p):
    """Return integral part of shifted decimal"""
    if p == 0:
        return v
    if v == 0:
        return v
    if p > 0:
        if v > 0:
            return v // 10 ** p
        else:
            return -(-v // 10 ** p)
    else:
        return v * 10 ** -p


def _div(num, den, minPrec):
    """Return num / den as Decimal, if possible with precision <=
    max(minPrec, LIMIT_PREC), otherwise as Fraction"""
    q, p, r = _approx_rational(num, den, minPrec)
    if r == 0:
        result = Decimal()
        result._value = q
        result._precision = p
        return result
    else:
        return Fraction(num, den)


# helper for different rounding modes


# Round towards 0 (aka truncate)
# quotient negativ => add 1
def _round_down(q, r, y):
    if q < 0:
        return 1
    else:
        return 0


# Round away from 0
# quotient not negativ => add 1
def _round_up(q, r, y):
    if q >= 0:
        return 1
    else:
        return 0


# Round 5 down
# |remainder| > |divisor|/2 or
# |remainder| = |divisor|/2 and quotient < 0
# => add 1
def _round_half_down(q, r, y):
    ar, ay = abs(2*r), abs(y)
    if ar > ay or (ar == ay and q < 0):
        return 1
    else:
        return 0


# Round 5 up (away from 0)
# |remainder| > |divisor|/2 or
# |remainder| = |divisor|/2 and quotient >= 0
# => add 1
def _round_half_up(q, r, y):
    ar, ay = abs(2*r), abs(y)
    if ar > ay or (ar == ay and q >= 0):
        return 1
    else:
        return 0


# Round 5 to even, rest to nearest
# |remainder| > |divisor|/2 or
# |remainder| = |divisor|/2 and quotient not even
# => add 1
def _round_half_even(q, r, y):
    ar, ay = abs(2*r), abs(y)
    if ar > ay or (ar == ay and q % 2 != 0):
        return 1
    else:
        return 0


# Round up (not away from 0 if negative)
# => always add 1
def _round_ceiling(q, r, y):
    return 1


# Round down (not towards 0 if negative)
# => never add 1
def _round_floor(q, r, y):
    return 0


# Round down unless last digit is 0 or 5
# quotient not negativ and quotient divisible by 5 without remainder or
# quotient negativ and (quotient + 1) not divisible by 5 without remainder
# => add 1
def _round_05up(q, r, y):
    if q >= 0 and q % 5 == 0 or q < 0 and (q + 1) % 5 != 0:
        return 1
    else:
        return 0


_rounding_map = {
    ROUND_DOWN: _round_down,
    ROUND_UP: _round_up,
    ROUND_HALF_DOWN: _round_half_down,
    ROUND_HALF_UP: _round_half_up,
    ROUND_HALF_EVEN: _round_half_even,
    ROUND_CEILING: _round_ceiling,
    ROUND_FLOOR: _round_floor,
    ROUND_05UP: _round_05up}


def _round(q, r, y, rounding=None):
    if rounding is None:
        rounding = get_rounding()
    f = _rounding_map[rounding]
    return f(q, r, y)

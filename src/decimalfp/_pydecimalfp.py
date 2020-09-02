# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Name:        _pydecimalfp
# Purpose:     Decimal fixed-point arithmetic (Python implementation)
#
# Author:      Michael Amrhein (michael@adrhinum.de)
#
# Copyright:   (c) 2001 ff. Michael Amrhein
#              Portions adopted from FixedPoint.py written by Tim Peters
# License:     This program is part of a larger application. For license
#              details please read the file LICENSE.TXT provided together
#              with the application.
# ----------------------------------------------------------------------------
# $Source$
# $Revision$


"""Decimal fixed-point arithmetic."""


# standard lib imports

from decimal import Decimal as _StdLibDecimal
from fractions import Fraction
from functools import reduce
import locale
from math import floor, gcd, log10
from numbers import Complex, Integral, Rational, Real
import operator
from typing import Any, Callable, Optional, Sequence, Tuple, Union

# local imports

from .rounding import ROUNDING


MAX_DEC_PRECISION = 9 * 255

# 10 ** exp (mit cache)

_base10_pow_cache = list(range(128))
for i in _base10_pow_cache:
    _base10_pow_cache[i] = 10 ** i


def base10pow(exp: int) -> int:
    """Return 10 ** `exp`."""
    assert exp >= 0, 'base10pow called with exponent < 0: %i' % exp
    try:
        return _base10_pow_cache[exp]
    except IndexError:
        return 10 ** exp


# parse functions
import re                                                   # noqa: I100, I202

# parse for a Decimal
# [+|-]<int>[.<frac>][<e|E>[+|-]<exp>] or
# [+|-].<frac>[<e|E>[+|-]<exp>].
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
_parse_dec_string = re.compile(_pattern, re.VERBOSE).match

# parse for a format specifier
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
_parse_format_spec = re.compile(_pattern, re.VERBOSE).match
del re, _pattern


class Decimal:

    """Decimal number with a given number of fractional digits.

    Args:
        value (see below): numerical value (default: None)
        precision (numbers.Integral): number of fractional digits (default:
            None)

    If `value` is given, it must either be a string, an instance of
    `numbers.Integral`, `number.Rational` (for example `fractions.Fraction`),
    `decimal.Decimal`, a finite instance of `numbers.Real` (for example
    `float`) or be convertable to a `float` or an `int`.

    If a string is given as value, it must be a string in one of two formats:

    * [+|-]<int>[.<frac>][<e|E>[+|-]<exp>] or
    * [+|-].<frac>[<e|E>[+|-]<exp>].

    If given value is `None`, Decimal(0) is returned.

    Returns:
        :class:`Decimal` instance derived from `value` according
            to `precision`

    The value is always adjusted to the given precision or the precision is
    calculated from the given value, if no precision is given.

    Raises:
        TypeError: `precision` is given, but not of type `Integral`.
        TypeError: `value` is not an instance of the types listed above and
            not convertable to `float` or `int`.
        ValueError: `precision` is given, but not >= 0.
        ValueError: `value` can not be converted to a `Decimal` (with a number
            of fractional digits <= `MAX_DEC_PRECISION`).

    :class:`Decimal` instances are immutable.

    """

    __slots__ = ('_value', '_precision',
                 # used for caching values only:
                 '_hash', '_numerator', '_denominator'
                 )

    def __new__(cls, value=None, precision=None):
        """Create and return new `Decimal` instance."""
        self = object.__new__(cls)

        if precision is None:
            if value is None:
                self._value = 0
                self._precision = 0
                return self
        else:
            if not isinstance(precision, Integral):
                raise TypeError(
                    "Precision must be of type 'numbers.Integral'.")
            else:
                precision = int(precision)
            if precision < 0:
                raise ValueError("Precision must be >= 0.")
            if precision > MAX_DEC_PRECISION:
                raise ValueError("Precision limit exceeded.")
            if value is None:
                self._value = 0
                self._precision = precision
                return self

        # Decimal
        if isinstance(value, Decimal):
            v, p = value._value, value._precision
            if precision is None or precision == p:
                self._value = v
                self._precision = p
            else:
                self._value = _vp_adjust_to_prec(v, p, precision)
                self._precision = precision
            return self

        # String
        if isinstance(value, str):
            parsed = _parse_dec_string(value)
            if parsed is None:
                raise ValueError("Can't convert %s to Decimal." % repr(value))
            sign_n_digits = parsed.group('sign') or ''
            s_exp = parsed.group('exp')
            if s_exp:
                exp = int(s_exp)
            else:
                exp = 0
            s_int = parsed.group('int')
            if s_int:
                s_frac = parsed.group('frac')
                if s_frac:
                    sign_n_digits += s_int + s_frac
                    n_frac = len(s_frac)
                else:
                    sign_n_digits += s_int
                    n_frac = 0
            else:
                s_frac = parsed.group('onlyfrac')
                n_frac = len(s_frac)
                sign_n_digits += s_frac
            if precision is None:
                precision = max(0, n_frac - exp)
                if precision > MAX_DEC_PRECISION:
                    raise ValueError("Precision limit exceeded.")
            self._precision = precision
            shift10 = precision - n_frac + exp
            if shift10 == 0:
                self._value = int(sign_n_digits)
            elif shift10 > 0:
                self._value = int(sign_n_digits) * base10pow(shift10)
            else:
                self._value = _floordiv_rounded(int(sign_n_digits),
                                                base10pow(-shift10))
            return self

        # Integral
        if isinstance(value, Integral):
            value = int(value)
            if precision is None:
                self._precision = 0
                self._value = value
            else:
                self._precision = precision
                self._value = value * base10pow(precision)
            return self

        # Decimal (from standard library)
        if isinstance(value, _StdLibDecimal):
            if value.is_finite():
                sign, digits, exp = value.as_tuple()
                coeff = (-1) ** sign * reduce(lambda x, y: x * 10 + y, digits)
                if precision is None:
                    if exp > 0:
                        self._value = coeff * base10pow(exp)
                        self._precision = 0
                    else:
                        self._value = coeff
                        self._precision = abs(exp)
                        if self._precision > MAX_DEC_PRECISION:
                            raise ValueError("Precision limit exceeded.")
                else:
                    self._precision = precision
                    shift10 = exp + precision
                    if shift10 == 0:
                        self._value = coeff
                    elif shift10 > 0:
                        self._value = coeff * base10pow(shift10)
                    else:
                        self._value = _floordiv_rounded(coeff,
                                                        base10pow(-shift10))
                return self
            else:
                raise ValueError("Can't convert %s to Decimal." % repr(value))

        # Real (incl. Rational)
        if isinstance(value, Real):
            try:
                num, den = value.numerator, value.denominator
            except AttributeError:
                try:
                    num, den = value.as_integer_ratio()
                except (ValueError, OverflowError, AttributeError):
                    raise ValueError("Can't convert %s to Decimal."
                                     % repr(value))
            if precision is None:
                v, p, rem = _approx_rational(num, den)
                if rem:
                    raise ValueError("Can't convert %s exactly to Decimal."
                                     % repr(value))
                if p > MAX_DEC_PRECISION:
                    raise ValueError("Precision limit exceeded.")
                self._value = v
                self._precision = p
            else:
                self._value = _floordiv_rounded(num * base10pow(precision),
                                                den)
                self._precision = precision
            return self

        # Others
        # If there's a float or int equivalent to value, use it
        ev = None
        try:
            ev = float(value)
        except (TypeError, ValueError):
            try:
                ev = int(value)
            except (TypeError, ValueError):
                pass
        if ev == value:     # do we really have the same value?
            return Decimal(ev, precision)

        # unable to create Decimal
        raise TypeError("Can't convert %s to Decimal." % repr(value))

    # to be compatible to fractions.Fraction
    @classmethod
    def from_float(cls, f: Union[float, Integral]) -> "Decimal":
        """Convert a finite float (or int) to a :class:`Decimal`.

        Args:
            f (float or int): number to be converted to a `Decimal`

        Returns:
            :class:`Decimal` instance derived from `f`

        Raises:
            TypeError: `f` is neither a `float` nor an `int`.
            ValueError: `f` can not be converted to a :class:`Decimal` with
                a precision <= `MAX_DEC_PRECISION`.

        Beware that Decimal.from_float(0.3) != Decimal('0.3').

        """
        if not isinstance(f, (float, Integral)):
            raise TypeError("%s is not a float." % repr(f))
        return cls(f)

    # to be compatible to fractions.Fraction
    @classmethod
    def from_decimal(cls, d: Union["Decimal", Integral, _StdLibDecimal]) \
            -> "Decimal":
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
        if not isinstance(d, (Decimal, Integral, _StdLibDecimal)):
            raise TypeError("%s is not a Decimal." % repr(d))
        return cls(d)

    @classmethod
    def from_real(cls, r: Real, exact: bool = True) -> "Decimal":
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
                to a :class:`Decimal` with a precision <= `MAX_DEC_PRECISION`.

        If `exact` is `False` and `r` can not exactly be represented by a
        `Decimal` with a precision <= `MAX_DEC_PRECISION`, the result is rounded to a
        precision = `MAX_DEC_PRECISION`.

        """
        if not isinstance(r, Real):
            raise TypeError("%s is not a Real." % repr(r))
        try:
            return cls(r)
        except ValueError:
            if exact:
                raise
            else:
                return cls(r, MAX_DEC_PRECISION)

    @property
    def precision(self) -> int:
        """Return precision of `self`."""
        return self._precision

    @property
    def magnitude(self) -> int:
        """Return magnitude of `self` in terms of power to 10.

        I.e. the largest integer exp so that 10 ** exp <= self.

        """
        sv = self._value
        if sv == 0:
            raise OverflowError("Result would be '-Infinity'.")
        return floor(log10(abs(sv))) - self._precision

    @property
    def numerator(self) -> int:
        """Return the normalized numerator of `self`.

        I. e. the numerator from the pair of integers with the smallest
        positive denominator, whose ratio is equal to `self`.

        """
        try:
            return self._numerator
        except AttributeError:
            self._numerator, self._denominator = self.as_integer_ratio()
            return self._numerator

    @property
    def denominator(self) -> int:
        """Return the normalized denominator of 'self'.

        I. e. the smallest positive denominator from the pairs of integers,
        whose ratio is equal to `self`.

        """
        try:
            return self._denominator
        except AttributeError:
            self._numerator, self._denominator = self.as_integer_ratio()
            return self._denominator

    @property
    def real(self) -> "Decimal":
        """Return real part of `self`.

        Returns `self` (Real numbers are their real component).

        """
        return self

    @property
    def imag(self) -> int:
        """Return imaginary part of `self`.

        Returns 0 (Real numbers have no imaginary component).

        """
        return 0

    def adjusted(self, precision: Optional[int] = None,
                 rounding: Optional[ROUNDING] = None) -> "Decimal":
        """Return adjusted copy of `self`.

        Args:
            precision (numbers.Integral): number of fractional digits
                (default: None)
            rounding (ROUNDING): rounding mode (default: None)

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
            adj = object.__new__(Decimal)
            adj._value, adj._precision = _vp_normalize(self._value,
                                                       self._precision)
        else:
            if not isinstance(precision, Integral):
                raise TypeError("Precision must be of type 'Integral'.")
            to_prec = int(precision)
            if to_prec > MAX_DEC_PRECISION:
                raise ValueError("Precision limit exceeded.")
            p = self._precision
            if to_prec == p:
                return self
            adj = object.__new__(Decimal)
            adj._value = _vp_adjust_to_prec(self._value, p, to_prec, rounding)
            adj._precision = max(0, to_prec)
        return adj

    def quantize(self, quant: Any, rounding: Optional[ROUNDING] = None) \
            -> Union["Decimal", Fraction]:
        """Return integer multiple of `quant` closest to `self`.

        Args:
            quant (Any): quantum to get a multiple from; must be a `Rational`
                or convertable to :class:`Decimal`
            rounding (ROUNDING): rounding mode (default: None)

        If no `rounding` mode is given, the default mode from the current
        context (from module `decimal`) is used.

        Returns:
            :class:`Decimal` instance that is the integer multiple of `quant`
                closest to `self` (according to `rounding` mode); if result
                can not be represented as :class:`Decimal`, an instance of
                `Fraction` is returned

        Raises:
            TypeError: `quant` is not a Rational number or can not be
                converted to a :class:`Decimal`

        """
        try:
            num, den = quant.numerator, quant.denominator
        except AttributeError:
            try:
                num, den = quant.as_integer_ratio()
            except AttributeError:
                try:
                    quant = Decimal(quant)
                except (TypeError, ValueError):
                    raise TypeError("Can't quantize to a '%s': %s."
                                    % (quant.__class__.__name__, quant))
                num, den = quant.as_integer_ratio()
        mult = _floordiv_rounded(self._value * den,
                                 base10pow(self._precision) * num,
                                 rounding)
        return Decimal(mult) * quant

    def as_tuple(self) -> Tuple[int, int, int]:
        """Return a tuple (sign, coeff, exp) equivalent to `self`.

        self == (-1) ** sign * coeff * 10 ** exp.

        """
        v = self._value
        sign = int(v < 0)
        coeff = abs(v)
        exp = -self._precision
        return sign, coeff, exp

    def as_fraction(self) -> Fraction:
        """Return an instance of `Fraction` equal to `self`.

        Returns the `Fraction` with the smallest positive denominator, whose
        ratio is equal to `self`.

        """
        return Fraction(self._value, base10pow(self._precision))

    def as_integer_ratio(self) -> Tuple[int, int]:
        """Return a pair of integers whose ratio is equal to `self`.

        Returns the pair of numerator and denominator with the smallest
        positive denominator, whose ratio is equal to `self`.

        """
        n, d = self._value, base10pow(self._precision)
        g = gcd(n, d)
        return n // g, d // g

    def __copy__(self) -> "Decimal":
        """Return self (Decimal instances are immutable)."""
        return self

    def __deepcopy__(self, memo) -> "Decimal":
        """Return self (Decimal instances are immutable)."""
        return self.__copy__()

    def __reduce__(self) -> Tuple[type, Tuple, Tuple[int, int]]:
        """Return pickle helper tuple."""
        return (Decimal, (), (self._value, self._precision))

    def __setstate__(self, state: Tuple[int, int]):
        """Set state of `self` from `state`."""
        self._value, self._precision = state

    # string representation
    def __repr__(self) -> str:
        """repr(self)"""                                        # noqa: D400
        sv = self._value
        sp = self._precision
        rv, rp = _vp_normalize(sv, sp)
        if rp == 0:
            s = str(rv)
        else:
            s = str(abs(rv))
            n = len(s)
            if n > rp:
                s = "'%s%s.%s'" % ((rv < 0) * '-', s[0:-rp], s[-rp:])
            else:
                s = "'%s0.%s%s'" % ((rv < 0) * '-', (rp - n) * '0', s)
        if sp == rp:
            return "Decimal(%s)" % (s)
        else:
            return "Decimal(%s, %s)" % (s, sp)

    def __str__(self) -> str:
        """str(self)"""                                         # noqa: D400
        sp = self._precision
        if sp == 0:
            return "%i" % self._value
        else:
            sv = self._value
            i = _vp_to_int(sv, sp)
            f = sv - i * base10pow(sp)
            s = (i == 0 and f < 0) * '-'  # -1 < self < 0 => i = 0 and f < 0 !
            return '%s%i.%0*i' % (s, i, sp, abs(f))

    def __format__(self, fmt_spec: str) -> str:
        """Return `self` converted to a string according to `fmt_spec`.

        Args:
            fmt_spec (str): a standard format specifier for a number

        Returns:
            str: `self` converted to a string according to `fmt_spec`

        """
        (fmt_fill, fmt_align, fmt_sign, fmt_min_width, fmt_thousands_sep,
            fmt_grouping, fmt_decimal_point, fmt_precision,
            fmt_type) = _get_format_params(fmt_spec)
        n_to_fill = fmt_min_width
        sv = self._value
        sp = self._precision
        if fmt_precision is None:
            fmt_precision = sp
        if fmt_type == '%':
            percent_sign = '%'
            n_to_fill -= 1
            xtra_shift = 2
        else:
            percent_sign = ''
            xtra_shift = 0
        v = _vp_adjust_to_prec(sv, sp, fmt_precision + xtra_shift)
        if v < 0:
            sign = '-'
            n_to_fill -= 1
            v = abs(v)
        elif fmt_sign == '-':
            sign = ''
        else:
            sign = fmt_sign
            n_to_fill -= 1
        raw_digits = format(v, '>0%i' % (fmt_precision + 1))
        if fmt_precision:
            decimal_point = fmt_decimal_point
            raw_digits, frac_part = (raw_digits[:-fmt_precision],
                                     raw_digits[-fmt_precision:])
            n_to_fill -= fmt_precision + 1
        else:
            decimal_point = ''
            frac_part = ''
        if fmt_align == '=':
            int_part = _pad_digits(raw_digits, max(0, n_to_fill), fmt_fill,
                                   fmt_thousands_sep, fmt_grouping)
            return sign + int_part + decimal_point + frac_part + percent_sign
        else:
            int_part = _pad_digits(raw_digits, 0, fmt_fill,
                                   fmt_thousands_sep, fmt_grouping)
            raw = sign + int_part + decimal_point + frac_part + percent_sign
            if n_to_fill > len(int_part):
                fmt = "%s%s%i" % (fmt_fill, fmt_align, fmt_min_width)
                return format(raw, fmt)
            else:
                return raw

    def _compare(self, other: Any, cmp: Callable) -> bool:
        """Compare `self` and `other` using operator `cmp`."""
        sv = self._value
        sp = self._precision
        if isinstance(other, Decimal):
            ov = other._value
            op = other._precision
            # if sp == op, we are done, otherwise we adjust the value with the
            # lesser precision
            if sp < op:
                sv *= base10pow(op - sp)
            elif sp > op:
                ov *= base10pow(sp - op)
            return cmp(sv, ov)
        elif isinstance(other, Integral):
            ov = int(other) * base10pow(sp)
            return cmp(sv, ov)
        elif isinstance(other, Rational):
            # cross-wise product of numerator and denominator
            sv *= other.denominator
            ov = other.numerator * base10pow(sp)
            return cmp(sv, ov)
        elif isinstance(other, Real):
            try:
                num, den = other.as_integer_ratio()             # type: ignore
            except AttributeError:
                return NotImplemented
            except (ValueError, OverflowError):
                # 'nan' and 'inf'
                return cmp(sv, other)
            # cross-wise product of numerator and denominator
            sv *= den
            ov = num * base10pow(sp)
            return cmp(sv, ov)
        elif isinstance(other, _StdLibDecimal):
            if other.is_finite():
                sign, digits, exp = other.as_tuple()
                ov = (-1) ** sign * reduce(lambda x, y: x * 10 + y, digits)
                op = abs(exp)
                # if sp == op, we are done, otherwise we adjust the value with
                # the lesser precision
                if sp < op:
                    sv *= base10pow(op - sp)
                elif sp > op:
                    ov *= base10pow(sp - op)
                return cmp(sv, ov)
            else:
                # 'nan' and 'inf'
                return cmp(sv, other)
        elif isinstance(other, Complex):
            if cmp in (operator.eq, operator.ne):
                if other.imag == 0:
                    return self._compare(other.real, cmp)
                else:
                    return False if cmp is operator.eq else True
        # don't know how to compare
        return NotImplemented

    def __eq__(self, other: Any) -> bool:
        """self == other"""                                 # noqa: D400, D403
        return self._compare(other, operator.eq)

    def __lt__(self, other: Any) -> bool:
        """self < other"""                                  # noqa: D400, D403
        return self._compare(other, operator.lt)

    def __le__(self, other: Any) -> bool:
        """self <= other"""                                 # noqa: D400, D403
        return self._compare(other, operator.le)

    def __gt__(self, other: Any) -> bool:
        """self > other"""                                  # noqa: D400, D403
        return self._compare(other, operator.gt)

    def __ge__(self, other: Any) -> bool:
        """self >= other"""                                 # noqa: D400, D403
        return self._compare(other, operator.ge)

    def __hash__(self) -> int:
        """hash(self)"""                                        # noqa: D400
        try:
            return self._hash
        except AttributeError:
            sv, sp = self._value, self._precision
            if sp == 0:           # if self == int(self),
                return hash(sv)   # same hash as int
            else:                 # otherwise same hash as equivalent fraction
                return hash(Fraction(sv, base10pow(sp)))

    # return 0 or 1 for truth-value testing
    def __nonzero__(self) -> bool:
        """bool(self)"""                                        # noqa: D400
        return self._value != 0
    __bool__ = __nonzero__

    # return integer portion as int
    def __int__(self) -> int:
        """math.trunc(self)"""                                  # noqa: D400
        return _vp_to_int(self._value, self._precision)
    __trunc__ = __int__

    # convert to float (may loose precision!)
    def __float__(self) -> float:
        """float(self)"""                                       # noqa: D400
        return self._value / base10pow(self._precision)

    def __pos__(self) -> "Decimal":
        """+self"""                                             # noqa: D400
        return self

    def __neg__(self) -> "Decimal":
        """-self"""                                             # noqa: D400
        result = object.__new__(Decimal)
        result._value = -self._value
        result._precision = self._precision
        return result

    def __abs__(self) -> "Decimal":
        """abs(self)"""                                         # noqa: D400
        result = object.__new__(Decimal)
        result._value = abs(self._value)
        result._precision = self._precision
        return result

    def __add__(self, other: Any) -> Union["Decimal", Fraction]:
        """self + other"""                                  # noqa: D400, D403
        return add(self, other)

    # other + self
    __radd__ = __add__

    def __sub__(self, other: Any) -> Union["Decimal", Fraction]:
        """self - other"""                                  # noqa: D400, D403
        return sub(self, other)

    def __rsub__(self, other: Any) -> Union["Decimal", Fraction]:
        """other - self"""                                  # noqa: D400, D403
        return add(-self, other)

    def __mul__(self, other: Any) -> Union["Decimal", Fraction]:
        """self * other"""                                  # noqa: D400, D403
        return mul(self, other)

    # other * self
    __rmul__ = __mul__

    def __div__(self, other: Any) -> Union["Decimal", Fraction]:
        """self / other"""                                  # noqa: D400, D403
        return div1(self, other)

    def __rdiv__(self, other: Any) -> Union["Decimal", Fraction]:
        """other / self"""                                  # noqa: D400, D403
        return div2(other, self)

    # Decimal division is true division
    __truediv__ = __div__
    __rtruediv__ = __rdiv__

    def __divmod__(self, other: Any) \
            -> Tuple[int, Union["Decimal", Fraction]]:
        """self // other, self % other"""                   # noqa: D400, D403
        return divmod1(self, other)

    def __rdivmod__(self, other: Any) \
            -> Tuple[int, Union["Decimal", Fraction]]:
        """other // self, other % self"""                   # noqa: D400, D403
        return divmod2(other, self)

    def __floordiv__(self, other: Any) -> int:
        """self // other"""                                 # noqa: D400, D403
        return floordiv1(self, other)

    def __rfloordiv__(self, other: Any) -> int:
        """other // self"""                                 # noqa: D400, D403
        return floordiv2(other, self)

    def __mod__(self, other: Any) -> Union["Decimal", Fraction]:
        """self % other"""                                  # noqa: D400, D403
        return mod1(self, other)

    def __rmod__(self, other: Any) -> Union["Decimal", Fraction]:
        """other % self"""                                  # noqa: D400, D403
        return mod2(other, self)

    def __pow__(self, other: Any, mod: Any = None) \
            -> Union["Decimal", float, complex]:
        """self ** other

        If other is an integer (or a Rational with denominator = 1), the
        result will be a Decimal. Otherwise, the result will be a float or
        complex since roots are generally irrational.

        `mod` must always be None (otherwise a `TypeError` is raised).

        """
        if mod is not None:
            raise TypeError("3rd argument not allowed unless all arguments "
                            "are integers")
        if isinstance(other, (Real, _StdLibDecimal)):
            return pow1(self, other)
        return NotImplemented

    def __rpow__(self, other: Any, mod: Any = None) \
            -> Union["Decimal", float, complex]:
        """other ** self

        `mod` must always be None (otherwise a `TypeError` is raised).
        """
        if mod is not None:                                 # pragma: no cover
            raise TypeError("3rd argument not allowed unless all arguments "
                            "are integers")
        return pow2(other, self)

    def __floor__(self) -> int:
        """math.floor(self)"""                                  # noqa: D400
        n, d = self._value, base10pow(self._precision)
        return n // d

    def __ceil__(self) -> int:
        """math.ceil(self)"""                                   # noqa: D400
        n, d = self._value, base10pow(self._precision)
        return -(-n // d)

    def __round__(self, precision: Optional[int] = None) \
            -> Union[int, "Decimal"]:
        """round(self [, n_digits])

        Round `self` to a given precision in decimal digits (default 0).
        `n_digits` may be negative.

        This method is called by the built-in `round` function. It returns an
        `int` when called with one argument, otherwise a :class:`Decimal`.
        """
        if precision is None:
            # return integer
            return int(self.adjusted(0))
        # otherwise return Decimal
        return self.adjusted(precision)


# register Decimal as Rational
Rational.register(Decimal)


# helper functions for formatting:


_dflt_format_params = {'fill': ' ',
                       'align': '>',
                       'sign': '-',
                       #'zeropad': '',
                       'minimumwidth': 0,
                       'thousands_sep': '',
                       'grouping': [3, 0],
                       'decimal_point': '.',
                       'precision': None,
                       'type': 'f'}


def _get_format_params(format_spec: str) \
        -> Tuple[str, str, str, int, str, Sequence[int], str, Optional[int],
                 str]:
    m = _parse_format_spec(format_spec)
    if m is None:
        raise ValueError("Invalid format specifier: " + format_spec)
    fill = m.group('fill')
    zeropad = m.group('zeropad')
    if fill:                            # fill overrules zeropad
        fmt_fill = fill
        fmt_align = m.group('align')
    elif zeropad:                       # zeropad overrules align
        fmt_fill = '0'
        fmt_align = "="
    else:
        fmt_fill = _dflt_format_params['fill']
        fmt_align = m.group('align') or _dflt_format_params['align']
    fmt_sign = m.group('sign') or _dflt_format_params['sign']
    minimumwidth = m.group('minimumwidth')
    if minimumwidth:
        fmt_min_width = int(minimumwidth)
    else:
        fmt_min_width = _dflt_format_params['minimumwidth']
    fmt_type = m.group('type') or _dflt_format_params['type']
    if fmt_type == 'n':
        lconv = locale.localeconv()
        fmt_thousands_sep = (m.group('thousands_sep') and
                             lconv['thousands_sep'])
        fmt_grouping = lconv['grouping']
        fmt_decimal_point = lconv['decimal_point']
    else:
        fmt_thousands_sep = (m.group('thousands_sep') or
                             _dflt_format_params['thousands_sep'])
        fmt_grouping = _dflt_format_params['grouping']
        fmt_decimal_point = _dflt_format_params['decimal_point']
    precision = m.group('precision')
    if precision:
        fmt_precision = int(precision)
    else:
        fmt_precision = None
    return (fmt_fill, fmt_align, fmt_sign, fmt_min_width, fmt_thousands_sep,
            fmt_grouping, fmt_decimal_point, fmt_precision, fmt_type)


def _pad_digits(digits: str, min_width: int, fill: str,
                sep: Optional[str] = None,
                grouping: Optional[Sequence[int]] = None) -> str:
    n_digits = len(digits)
    if sep and grouping:
        slices = []
        i = j = 0
        limit = max(min_width, n_digits) if fill == '0' else n_digits
        for k in _iter_grouping(grouping):
            j = min(i + k, limit)
            slices.append((i, j))
            if j >= limit:
                break
            i = j
            limit = max(limit - 1, n_digits, i + 1)
        if j < limit:
            slices.append((j, limit))
        digits = (limit - n_digits) * fill + digits
        raw = sep.join([digits[limit - j: limit - i]
                        for i, j in reversed(slices)])
        return (min_width - len(raw)) * fill + raw
    else:
        return (min_width - n_digits) * fill + digits


def _iter_grouping(grouping):
    # From Python docs: 'grouping' is a sequence of numbers specifying which
    # relative positions the 'thousands_sep' is expected. If the sequence is
    # terminated with CHAR_MAX, no further grouping is performed. If the
    # sequence terminates with a 0, the last group size is repeatedly used.
    k = None
    for i in grouping[:-1]:
        yield i
        k = i
    i = grouping[-1]
    if i == 0:
        while k:
            yield k
    elif i != locale.CHAR_MAX:                              # pragma: no cover
        yield i


# helper functions for decimal arithmetic


def _vp_adjust_to_prec(v: int, p: int, to_prec: int,
                       rounding: Optional[ROUNDING] = None) -> int:
    # Return internal tuple (v, p) adjusted to precision `to_prec` using given
    # rounding mode (or default mode if none is given).
    # Assumes p != to_prec.
    dp = to_prec - p
    if dp >= 0:
        # increase precision -> increase internal value
        return v * base10pow(dp)
    # decrease precision -> decrease internal value -> rounding
    elif to_prec >= 0:
        # resulting precision >= 0 -> just return adjusted internal value
        return _floordiv_rounded(v, base10pow(-dp), rounding)
    else:
        # result to be rounded to a power of 10 -> two steps needed:
        # 1) round internal value to requested precision
        # 2) adjust internal value to precison 0 (because internal precision
        # must be >= 0)
        return (_floordiv_rounded(v, base10pow(-dp), rounding) *
                base10pow(-to_prec))


def _vp_normalize(v: int, p: int) -> Tuple[int, int]:
    # Reduce v, p to the smallest precision >= 0 without loosing value.
    # I. e. return rv, rp so that rv // 10 ** rp == v // 10 ** p and
    # (rv % 10 != 0 or rp == 0)
    if v == 0:
        return 0, 0
    while p > 0 and v % 10 == 0:
        p -= 1
        v = v // 10
    return v, p


def _floordiv_rounded(x: int, y: int,
                      rounding: Optional[ROUNDING] = None) -> int:
    # Return x // y, rounded using given rounding mode (or default mode
    # if none is given)
    quot, rem = divmod(x, y)
    if rem == 0:              # no need for rounding
        return quot
    else:
        if rounding is None:
            rounding = get_dflt_rounding_mode()
        if rounding == ROUNDING.ROUND_HALF_UP:
            # Round 5 up (away from 0)
            # |remainder| > |divisor|/2 or
            # |remainder| = |divisor|/2 and quotient >= 0
            # => add 1
            ar, ay = abs(2 * rem), abs(y)
            if ar > ay or (ar == ay and quot >= 0):
                return quot + 1
            else:
                return quot
        elif rounding == ROUNDING.ROUND_HALF_EVEN:
            # Round 5 to even, rest to nearest
            # |remainder| > |divisor|/2 or
            # |remainder| = |divisor|/2 and quotient not even
            # => add 1
            ar, ay = abs(2 * rem), abs(y)
            if ar > ay or (ar == ay and quot % 2 != 0):
                return quot + 1
            else:
                return quot
        elif rounding == ROUNDING.ROUND_HALF_DOWN:
            # Round 5 down
            # |remainder| > |divisor|/2 or
            # |remainder| = |divisor|/2 and quotient < 0
            # => add 1
            ar, ay = abs(2 * rem), abs(y)
            if ar > ay or (ar == ay and quot < 0):
                return quot + 1
            else:
                return quot
        elif rounding == ROUNDING.ROUND_DOWN:
            # Round towards 0 (aka truncate)
            # quotient negativ
            # => add 1
            if quot < 0:
                return quot + 1
            else:
                return quot
        elif rounding == ROUNDING.ROUND_UP:
            # Round away from 0
            # quotient not negativ
            # => add 1
            if quot >= 0:
                return quot + 1
            else:
                return quot
        elif rounding == ROUNDING.ROUND_CEILING:
            # Round up (not away from 0 if negative)
            # => always add 1
            return quot + 1
        elif rounding == ROUNDING.ROUND_FLOOR:
            # Round down (not towards 0 if negative)
            # => never add 1
            return quot
        elif rounding == ROUNDING.ROUND_05UP:
            # Round down unless last digit is 0 or 5
            # quotient not negativ and
            # quotient divisible by 5 without remainder or
            # quotient negativ and
            # (quotient + 1) not divisible by 5 without remainder
            # => add 1
            if (quot >= 0 and quot % 5 == 0 or
                    quot < 0 and (quot + 1) % 5 != 0):
                return quot + 1
            else:
                return quot
    raise ValueError(f"Invalid rounding mode: {rounding!r}.")


def _vp_to_int(v: int, p: int) -> int:
    # Return integral part of shifted decimal.
    if p == 0:
        return v
    if v == 0:
        return v
    if p > 0:
        if v > 0:
            return v // base10pow(p)
        else:
            return -(-v // base10pow(p))
    else:   # shouldn't happen!
        return v * base10pow(-p)                            # pragma: no cover


def _approx_rational(num: int, den: int, min_prec: int = 0) \
        -> Tuple[int, int, int]:
    # Approximate num / den as internal Decimal representation.
    # Returns v, p, r, so that
    # v * 10 ** -p + r == num / den
    # and p <= max(min_prec, MAX_DEC_PRECISION) and r -> 0.
    max_prec = max(min_prec, MAX_DEC_PRECISION)
    while True:
        p = (min_prec + max_prec) // 2
        v, r = divmod(num * base10pow(p), den)
        if p == max_prec:
            break
        if r == 0:
            max_prec = p
        elif min_prec >= max_prec - 2:
            min_prec = max_prec
        else:
            min_prec = p
    return v, p, r


def _div(num: int, den: int, min_prec: int) -> Union[Decimal, Fraction]:
    # Return num / den as Decimal,
    # if possible with precision <= max(minPrec, MAX_DEC_PRECISION),
    # otherwise as Fraction
    v, p, r = _approx_rational(num, den, min_prec)
    if r == 0:
        dec = object.__new__(Decimal)
        dec._value = v
        dec._precision = p
        return dec
    else:
        return Fraction(num, den)


def add(x: Decimal, y: Any) -> Union[Decimal, Fraction]:
    """x + y                                                # noqa: D400, D403

    x must be a Decimal.

    """
    if isinstance(y, Decimal):
        p = x._precision - y._precision
        if p == 0:
            result = Decimal(x)
            result._value += y._value
        elif p > 0:
            result = Decimal(x)
            result._value += y._value * base10pow(p)
        else:
            result = Decimal(y)
            result._value += x._value * base10pow(-p)
        return result
    elif isinstance(y, Integral):
        p = x._precision
        result = Decimal(x)
        result._value += int(y) * base10pow(p)
        return result
    elif isinstance(y, Rational):
        y_numerator, y_denominator = (y.numerator, y.denominator)
    elif isinstance(y, Real):
        try:
            y_numerator, y_denominator = y.as_integer_ratio()
        except (ValueError, OverflowError, AttributeError):
            raise ValueError("Unsupported operand: %s" % repr(y))
    elif isinstance(y, _StdLibDecimal):
        return add(x, Decimal(y))
    else:
        return NotImplemented
    # handle Rational and Real
    x_denominator = base10pow(x._precision)
    num = x._value * y_denominator + x_denominator * y_numerator
    den = y_denominator * x_denominator
    min_prec = x._precision
    # return num / den as Decimal or as Fraction
    return _div(num, den, min_prec)


def sub(x: Decimal, y: Any) -> Union[Decimal, Fraction]:
    """x - y                                                # noqa: D400, D403

    x must be a Decimal.

    """
    if isinstance(y, Decimal):
        p = x._precision - y._precision
        if p == 0:
            result = Decimal(x)
            result._value -= y._value
        elif p > 0:
            result = Decimal(x)
            result._value -= y._value * base10pow(p)
        else:
            result = Decimal(y)
            result._value = x._value * base10pow(-p) - y._value
        return result
    elif isinstance(y, Integral):
        p = x._precision
        result = Decimal(x)
        result._value -= int(y) * base10pow(p)
        return result
    elif isinstance(y, Rational):
        y_numerator, y_denominator = (y.numerator, y.denominator)
    elif isinstance(y, Real):
        try:
            y_numerator, y_denominator = y.as_integer_ratio()
        except (ValueError, OverflowError, AttributeError):
            raise ValueError("Unsupported operand: %s" % repr(y))
    elif isinstance(y, _StdLibDecimal):
        return sub(x, Decimal(y))
    else:
        return NotImplemented
    # handle Rational and Real
    x_denominator = base10pow(x._precision)
    num = x._value * y_denominator - x_denominator * y_numerator
    den = y_denominator * x_denominator
    min_prec = x._precision
    # return num / den as Decimal or as Fraction
    return _div(num, den, min_prec)


def mul(x: Decimal, y: Any) -> Union[Decimal, Fraction]:
    """x * y                                                # noqa: D400, D403

    x must be a Decimal.

    """
    if isinstance(y, Decimal):
        result = Decimal(x)
        result._value *= y._value
        result._precision += y._precision
        if result._precision > MAX_DEC_PRECISION:
            return Fraction(result._value, 10 ** result._precision)
        return result
    elif isinstance(y, Integral):
        result = Decimal(x)
        result._value *= y
        return result
    elif isinstance(y, Rational):
        y_numerator, y_denominator = (y.numerator, y.denominator)
    elif isinstance(y, Real):
        try:
            y_numerator, y_denominator = y.as_integer_ratio()
        except (ValueError, OverflowError, AttributeError):
            raise ValueError("Unsupported operand: %s" % repr(y))
    elif isinstance(y, _StdLibDecimal):
        return x.__mul__(Decimal(y))
    else:
        return NotImplemented
    # handle Rational and Real
    num = x._value * y_numerator
    den = y_denominator * base10pow(x._precision)
    min_prec = x._precision
    # return num / den as Decimal or as Fraction
    return _div(num, den, min_prec)


def div1(x: Decimal, y: Any) -> Union[Decimal, Fraction]:
    """x / y                                                # noqa: D400, D403

    x must be a Decimal.

    """
    if isinstance(y, Decimal):
        xp, yp = x._precision, y._precision
        num = x._value * base10pow(yp)
        den = y._value * base10pow(xp)
        min_prec = max(0, xp - yp)
        # return num / den as Decimal or as Fraction
        return _div(num, den, min_prec)
    elif isinstance(y, Rational):       # includes Integral
        y_numerator, y_denominator = (y.numerator, y.denominator)
    elif isinstance(y, Real):
        try:
            y_numerator, y_denominator = y.as_integer_ratio()
        except (ValueError, OverflowError, AttributeError):
            raise ValueError("Unsupported operand: %s" % repr(y))
    elif isinstance(y, _StdLibDecimal):
        return div1(x, Decimal(y))
    else:
        return NotImplemented
    # handle Rational and Real
    num = x._value * y_denominator
    den = y_numerator * base10pow(x._precision)
    min_prec = x._precision
    # return num / den as Decimal or as Fraction
    return _div(num, den, min_prec)


def div2(x: Any, y: Decimal) -> Union[Decimal, Fraction]:
    """x / y                                                # noqa: D400, D403

    y must be a Decimal.

    """
    if isinstance(x, Rational):
        x_numerator, x_denominator = (x.numerator, x.denominator)
    elif isinstance(x, Real):
        try:
            x_numerator, x_denominator = x.as_integer_ratio()
        except (ValueError, OverflowError, AttributeError):
            raise ValueError("Unsupported operand: %s" % repr(x))
    elif isinstance(x, _StdLibDecimal):
        return div1(Decimal(x), y)
    else:
        return NotImplemented
    # handle Rational and Real
    num = x_numerator * base10pow(y._precision)
    den = y._value * x_denominator
    min_prec = y._precision
    # return num / den as Decimal or as Fraction
    return _div(num, den, min_prec)


def divmod1(x: Decimal, y: Any) -> Tuple[int, Union[Decimal, Fraction]]:
    """x // y, x % y                                        # noqa: D400, D403

    x must be a Decimal.

    """
    if isinstance(y, Decimal):
        xp, yp = x._precision, y._precision
        if xp >= yp:
            r = Decimal(x)
            xv = x._value
            yv = y._value * base10pow(xp - yp)
        else:
            r = Decimal(y)
            xv = x._value * base10pow(yp - xp)
            yv = y._value
        q = xv // yv
        r._value = xv - q * yv
        return q, r
    elif isinstance(y, Integral):
        r = Decimal(x)
        xv = x._value
        xp = x._precision
        yv = y * base10pow(xp)
        q = xv // yv
        r._value = xv - q * yv
        return q, r
    elif isinstance(y, _StdLibDecimal):
        return divmod1(x, Decimal(y))
    else:
        return x // y, x % y


def divmod2(x: Any, y: Decimal) -> Tuple[int, Union[Decimal, Fraction]]:
    """x // y, x % y                                        # noqa: D400, D403

    y must be a Decimal.

    """
    if isinstance(x, Integral):
        r = Decimal(y)
        yv = y._value
        yp = y._precision
        xv = x * base10pow(yp)
        q = xv // yv
        r._value = xv - q * yv
        return q, r
    elif isinstance(x, _StdLibDecimal):
        return divmod1(Decimal(x), y)
    else:
        return x // y, x % y


def floordiv1(x: Decimal, y: Any) -> int:
    """x // y                                               # noqa: D400, D403

    x must be a Decimal.

    """
    if isinstance(y, (Decimal, Integral, _StdLibDecimal)):
        return divmod1(x, y)[0]
    else:
        return floor(x / y)


def floordiv2(x: Any, y: Decimal) -> int:
    """x // y                                               # noqa: D400, D403

    y must be a Decimal.

    """
    if isinstance(x, (Integral, _StdLibDecimal)):
        return divmod2(x, y)[0]
    else:
        return floor(x / y)


def mod1(x: Decimal, y: Any) -> Union[Decimal, Fraction]:
    """x % y                                                # noqa: D400, D403

    x must be a Decimal.

    """
    if isinstance(y, (Decimal, Integral, _StdLibDecimal)):
        return divmod1(x, y)[1]
    else:
        return x - y * Decimal(x // y)


def mod2(x: Any, y: Decimal) -> Union[Decimal, Fraction]:
    """x % y                                                # noqa: D400, D403

    y must be a Decimal.

    """
    if isinstance(x, (Integral, _StdLibDecimal)):
        return divmod2(x, y)[1]
    else:
        return x - y * Decimal(x // y)


def pow1(x: Decimal, y: Any) \
        -> Union["Decimal", float, complex]:
    """x ** y                                               # noqa: D400, D403

    x must be a Decimal.

    """
    try:
        exp = int(y)
    except (ValueError, OverflowError):
        raise ValueError("Unsupported operand: %s" % repr(y)) from None
    else:
        if exp != y:
            # fractional power -> irrational result
            return float(x) ** float(y)
        if exp >= 0:
            result = Decimal()
            result._value = x._value ** exp
            result._precision = x._precision * exp
            if result._precision > MAX_DEC_PRECISION:
                raise ValueError("Precision limit exceeded.")
            return result
        else:
            # 1 / x ** -y)
            exp = -exp
            prec = x._precision
            return _div(base10pow(prec * exp), x._value ** exp, prec)


def pow2(x: Any, y: Decimal) \
        -> Union["Decimal", float, complex]:
    """x ** y                                               # noqa: D400, D403

    y must be a Decimal.

    """
    if y.denominator == 1:
        return x ** y.numerator
    return x ** float(y)


# get / set default rounding mode

_dflt_rounding_mode = ROUNDING.ROUND_HALF_EVEN


def get_dflt_rounding_mode() -> ROUNDING:
    """Return default rounding mode."""
    global _dflt_rounding_mode
    return _dflt_rounding_mode


def set_dflt_rounding_mode(rounding: ROUNDING):
    """Set default rounding mode.

    Args:
        rounding (ROUNDING): rounding mode to be set as default

    """
    global _dflt_rounding_mode
    _dflt_rounding_mode = rounding


__all__ = [
    'Decimal',
    'ROUNDING',
    'get_dflt_rounding_mode',
    'set_dflt_rounding_mode',
]

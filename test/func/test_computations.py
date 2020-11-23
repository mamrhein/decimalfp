#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Name:        test_adjustments
# Purpose:     Test driver for package 'decimalfp' (computations)
#
# Author:      Michael Amrhein (michael@adrhinum.de)
#
# Copyright:   (c) 2019 Michael Amrhein
# ----------------------------------------------------------------------------
# $Source$
# $Revision$


"""Test driver for package 'decimalfp' (computations)."""


from decimal import Decimal as StdLibDecimal
from decimal import getcontext
from fractions import Fraction
import numbers
import operator


import pytest

from decimalfp._pydecimalfp import MAX_DEC_PRECISION


ADD_SUB = (operator.add, operator.sub)
DIV_OPS = (operator.floordiv, operator.truediv)
MOD_OPS = (divmod, operator.mod)
DIV_MOD_OPS = DIV_OPS + MOD_OPS
BIN_OPS = ADD_SUB + (operator.mul,) + DIV_MOD_OPS


ctx = getcontext()
ctx.prec = 3350


class FakeReal:

    def __init__(self, value):
        self.f = float(value)

    def __int__(self):
        """math.trunc(self)"""                                  # noqa: D400
        raise ValueError


numbers.Real.register(FakeReal)


@pytest.mark.parametrize("operand2",
                         ("17.800",
                          ".".join(("1" * 825, "47" * 13 + "0" * 29)),
                          "-0.00014",
                          "999999999999999999.9",
                          "1",
                          "-1"),
                         ids=("compact",
                              "large",
                              "fraction",
                              "999999999999999999.9",
                              "one",
                              "minus-one"))
@pytest.mark.parametrize("operand1",
                         ("17.800",
                          ".".join(("1" * 1121, "4" * 33 + "0" * 19)),
                          "-0.00014",
                          "1",
                          "-1"),
                         ids=("compact",
                              "large",
                              "fraction",
                              "one",
                              "minus-one"))
@pytest.mark.parametrize("op",
                         [op for op in BIN_OPS],
                         ids=[op.__name__ for op in BIN_OPS])
def test_bin_ops_with_decimal_operands(impl, op, operand1, operand2):
    dec1 = impl.Decimal(operand1)
    dec2 = impl.Decimal(operand2)
    equiv1 = Fraction(operand1)
    equiv2 = Fraction(operand2)
    assert op(dec1, dec2) == op(equiv1, equiv2)


@pytest.mark.parametrize("operand2",
                         (1080, 6 / 7, Fraction(-34, 5),
                          StdLibDecimal("192.38463")),
                         ids=("int", "float", "Fraction", "StdLibDecimal"))
@pytest.mark.parametrize("operand1",
                         ("17.800",
                          ".".join(("1" * 2059, "4" * 33 + "0" * 19)),
                          "-0.00014"),
                         ids=("compact", "large", "fraction"))
@pytest.mark.parametrize("op",
                         [op for op in BIN_OPS],
                         ids=[op.__name__ for op in BIN_OPS])
def test_bin_ops_with_mixed_typed_operands(impl, op, operand1, operand2):
    dec = impl.Decimal(operand1)
    equiv1 = Fraction(operand1)
    equiv2 = Fraction(operand2)
    assert op(dec, operand2) == op(equiv1, equiv2)
    assert op(operand2, dec) == op(equiv2, equiv1)


@pytest.mark.parametrize("zero",
                         (None, 0, 0.0, Fraction(0, 1), StdLibDecimal(0)),
                         ids=("Decimal", "int", "float", "Fraction",
                              "StdLibDecimal"))
@pytest.mark.parametrize("operand",
                         ("17.800",
                          ".".join(("1" * 2259, "4" * 33 + "0" * 19)),
                          "-0.00014"),
                         ids=("compact", "large", "fraction"))
@pytest.mark.parametrize("op",
                         [op for op in ADD_SUB],
                         ids=[op.__name__ for op in ADD_SUB])
def test_decimal_add_sub_zero(impl, op, operand, zero):
    dec = impl.Decimal(operand)
    if zero is None:
        zero = impl.Decimal()
    res = op(dec, zero)
    assert isinstance(res, impl.Decimal)
    assert res == dec


@pytest.mark.parametrize("zero",
                         (None, 0, 0.0, Fraction(0, 1), StdLibDecimal(0)),
                         ids=("Decimal", "int", "float", "Fraction",
                              "StdLibDecimal"))
@pytest.mark.parametrize("operand",
                         ("17.800",
                          ".".join(("1" * 2259, "4" * 33 + "0" * 19)),
                          "-0.00014"),
                         ids=("compact", "large", "fraction"))
def test_decimal_mul_zero(impl, operand, zero):
    dec = impl.Decimal(operand)
    if zero is None:
        zero = impl.Decimal()
    res = dec * zero
    assert isinstance(res, impl.Decimal)
    assert res == zero


def test_mul_prec_limit_exceeded(impl):
    e = (MAX_DEC_PRECISION + 1) // 2
    f = Fraction(1, 10 ** e)
    d = impl.Decimal(f)
    p = d * d
    assert(isinstance(p, Fraction))
    assert(p == f * f)


@pytest.mark.parametrize("zero",
                         (None, 0, 0.0, Fraction(0, 1), StdLibDecimal(0)),
                         ids=("Decimal", "int", "float", "Fraction",
                              "StdLibDecimal"))
@pytest.mark.parametrize("operand",
                         ("17.800",
                          ".".join(("1" * 2259, "4" * 33 + "0" * 19)),
                          "-0.00014"),
                         ids=("compact", "large", "fraction"))
def test_zero_truediv_decimal(impl, operand, zero):
    dec = impl.Decimal(operand)
    if zero is None:
        zero = impl.Decimal()
    res = zero / dec
    assert isinstance(res, impl.Decimal)
    assert res == zero


@pytest.mark.parametrize("zero",
                         (None, 0, 0.0, Fraction(0, 1), StdLibDecimal(0)),
                         ids=("Decimal", "int", "float", "Fraction",
                              "StdLibDecimal"))
@pytest.mark.parametrize("operand",
                         ("17.800",
                          ".".join(("1" * 2259, "4" * 33 + "0" * 19)),
                          "-0.00014"),
                         ids=("compact", "large", "fraction"))
def test_zero_floordiv_decimal(impl, operand, zero):
    dec = impl.Decimal(operand)
    if zero is None:
        zero = impl.Decimal()
    res = zero // dec
    assert isinstance(res, int)
    assert res == zero


@pytest.mark.parametrize("zero",
                         (None, 0, 0.0, Fraction(0, 1), StdLibDecimal(0)),
                         ids=("Decimal", "int", "float", "Fraction",
                              "StdLibDecimal"))
@pytest.mark.parametrize("operand",
                         ("17.800",
                          ".".join(("1" * 2259, "4" * 33 + "0" * 19)),
                          "-0.00014"),
                         ids=("compact", "large", "fraction"))
@pytest.mark.parametrize("op",
                         [op for op in DIV_MOD_OPS],
                         ids=[op.__name__ for op in DIV_MOD_OPS])
def test_decimal_div_zero(impl, op, operand, zero):
    dec = impl.Decimal(operand)
    if zero is None:
        zero = impl.Decimal()
    with pytest.raises(ZeroDivisionError):
        op(dec, zero)

@pytest.mark.parametrize("operand",
                         (17.8, Fraction(3, 5)),
                         ids=("float", "fraction"))
@pytest.mark.parametrize("op",
                         [op for op in DIV_MOD_OPS],
                         ids=[op.__name__ for op in DIV_MOD_OPS])
def test_number_div_decimal_zero(impl, op, operand):
    zero = impl.Decimal()
    with pytest.raises(ZeroDivisionError):
        op(operand, zero)


@pytest.mark.parametrize("operand2",
                         (44, 2.0, Fraction(-34, 1),
                          StdLibDecimal("19.000"), None),
                         ids=("int", "float", "Fraction", "StdLibDecimal",
                              "Decimal"))
@pytest.mark.parametrize("operand1",
                         ("17.800",
                          ".".join(("1" * 2259, "4" * 33 + "0" * 19)),
                          "-0.00014"),
                         ids=("compact", "large", "fraction"))
def test_decimal_pow_integral(impl, operand1, operand2):
    dec = impl.Decimal(operand1)
    if operand2 is None:
        operand2 = impl.Decimal("23.0")
    exp = int(operand2)
    if exp >= 0:
        res = Fraction(dec.numerator ** exp, dec.denominator ** exp)
    else:
        res = Fraction(dec.denominator ** -exp, dec.numerator ** -exp)
    assert dec ** operand2 == res


@pytest.mark.parametrize("operand2",
                         (2.5, Fraction(-34, 7), StdLibDecimal("19.050"),
                          None),
                         ids=("float", "Fraction", "StdLibDecimal",
                              "Decimal"))
@pytest.mark.parametrize("operand1",
                         ("17.800",
                          "-0.00014"),
                         ids=("compact", "fraction"))
def test_decimal_pow_fraction(impl, operand1, operand2):
    dec = impl.Decimal(operand1)
    if operand2 is None:
        operand2 = impl.Decimal("23.8")
    assert dec ** operand2 == float(operand1) ** float(operand2)


@pytest.mark.parametrize("zero",
                         (None, 0, 0.0, Fraction(0, 1), StdLibDecimal(0)),
                         ids=("Decimal", "int", "float", "Fraction",
                              "StdLibDecimal"))
@pytest.mark.parametrize("operand",
                         ("17.800",
                          ".".join(("1" * 2259, "4" * 33 + "0" * 19)),
                          "-0.00014"),
                         ids=("compact", "large", "fraction"))
def test_decimal_pow_zero(impl, operand, zero):
    dec = impl.Decimal(operand)
    if zero is None:
        zero = impl.Decimal()
    assert dec ** zero == 1


@pytest.mark.parametrize("operand2",
                         ("17.800", "-0.00014", "3.00"),
                         ids=("compact", "fraction", "integral"))
@pytest.mark.parametrize("operand1",
                         (2.5, Fraction(-34, 7)),
                         ids=("float", "Fraction"))
def test_num_pow_decimal(impl, operand1, operand2):
    dec = impl.Decimal(operand2)
    if dec.denominator == 1:
        assert operand1 ** dec == operand1 ** int(dec)
    else:
        assert operand1 ** dec == operand1 ** float(dec)


def test_pow_incompat_param(impl):
    dec = impl.Decimal("3.12")
    with pytest.raises(TypeError):
        dec.__pow__(4, mod=3)


@pytest.mark.parametrize("other",
                         (FakeReal("0.5"), float('Inf'), StdLibDecimal('Inf'),
                          float('Nan'), StdLibDecimal('Nan')),
                         ids=("FakeReal", "'inf' (float)", "'inf' (Decimal)",
                              "'nan' (float)", "'nan' (Decimal)")
                         )
@pytest.mark.parametrize("op",
                         [op for op in BIN_OPS],
                         ids=[op.__name__ for op in BIN_OPS])
def test_bin_ops_incompat_number(impl, op, other):
    dec = impl.Decimal()
    with pytest.raises(ValueError):
        op(dec, other)
    with pytest.raises(ValueError):
        op(other, dec)


@pytest.mark.parametrize("other",
                         ("0.5", min, int, 5 + 3j),
                         ids=("str", "function", "class", "complex")
                         )
@pytest.mark.parametrize("op",
                         [op for op in BIN_OPS],
                         ids=[op.__name__ for op in BIN_OPS])
def test_bin_ops_incompat_operand_type(impl, op, other):
    dec = impl.Decimal()
    with pytest.raises(TypeError):
        op(dec, other)
    with pytest.raises(TypeError):
        op(other, dec)


@pytest.mark.parametrize("other",
                         (FakeReal("0.5"), float('Inf'), StdLibDecimal('Inf'),
                          float('Nan'), StdLibDecimal('Nan')),
                         ids=("FakeReal", "'inf' (float)", "'inf' (Decimal)",
                              "'nan' (float)", "'nan' (Decimal)")
                         )
def test_pow_incompat_number(impl, other):
    dec = impl.Decimal()
    with pytest.raises(ValueError):
        pow(dec, other)


@pytest.mark.parametrize("other",
                         ("0.5", min, int, 5 + 3j),
                         ids=("str", "function", "class", "complex")
                         )
def test_pow_incompat_operand_type(impl, other):
    dec = impl.Decimal()
    with pytest.raises(TypeError):
        pow(dec, other)

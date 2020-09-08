#!/usr/bin/env python
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Name:        test_constructors
# Purpose:     Test driver for package 'decimalfp' (constructors)
#
# Author:      Michael Amrhein (michael@adrhinum.de)
#
# Copyright:   (c) 2018 ff. Michael Amrhein
# License:     This program is part of a larger application. For license
#              details please read the file LICENSE.TXT provided together
#              with the application.
# ----------------------------------------------------------------------------
# $Source$
# $Revision$


"""Test driver for package 'decimalfp' (constructors)."""

import copy
from decimal import Decimal as StdLibDecimal  # , InvalidOperation
from fractions import Fraction
from numbers import Integral
import sys

import pytest

# ???: where to export global constants?
from decimalfp._pydecimalfp import MAX_DEC_PRECISION


@pytest.fixture(scope="module")
def dflt_rounding(impl):
    rnd = impl.get_dflt_rounding_mode()
    impl.set_dflt_rounding_mode(impl.ROUNDING.ROUND_HALF_UP);
    yield
    impl.set_dflt_rounding_mode(rnd)


def test_dflt_rounding(dflt_rounding):
    """Activate fixture to set default rounding"""
    pass


class IntWrapper:

    def __init__(self, i):
        assert isinstance(i, int)
        self.i = i

    def __int__(self):
        """int(self)"""
        return self.i

    def __eq__(self, i):
        """self == i"""
        return self.i == i


Integral.register(IntWrapper)


class FloatWrapper:

    def __init__(self, f):
        assert isinstance(f, float)
        self.f = f

    def __float__(self):
        """float(self)"""
        return self.f

    def __eq__(self, f):
        """self == f"""
        return self.f == f


@pytest.mark.parametrize("prec", [None, 0, 7],
                         ids=("prec=None", "prec=0", "prec=7"))
def test_decimal_no_value(impl, prec):
    dec = impl.Decimal(precision=prec)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == (prec if prec else 0)


@pytest.mark.parametrize("value", [float, 3 + 2j],
                         ids=("value=float", "value=3+2j"))
def test_decimal_wrong_value_type(impl, value):
    with pytest.raises(TypeError):
        impl.Decimal(value=value)


@pytest.mark.parametrize("prec", ["5", 7.5],
                         ids=("prec='5'", "prec=7.5"))
def test_decimal_wrong_precision_type(impl, prec):
    with pytest.raises(TypeError):
        impl.Decimal(precision=prec)


def test_decimal_wrong_precision_value(impl):
    with pytest.raises(ValueError):
        impl.Decimal(precision=-7)


compact_coeff = 174
compact_prec = 1
compact_ratio = Fraction(compact_coeff, 10 ** compact_prec)
compact_str = ".174e2"
compact_adj = 0
compact_adj_ratio = round(compact_ratio)
small_coeff = 123456789012345678901234567890
small_prec = 20
small_ratio = Fraction(-small_coeff, 10 ** small_prec)
small_str = "-12345678901234567890.1234567890E-10"
small_adj = 15
small_adj_ratio = Fraction(round(-small_coeff, small_adj - small_prec),
                           10 ** small_prec)
large_coeff = 294898 * 10 ** 2453 + 1498953
large_prec = 459
large_ratio = Fraction(large_coeff, 10 ** large_prec)
large_str = f"{large_coeff}e-{large_prec}"
large_adj = large_prec - 30
large_adj_ratio = Fraction(round(large_coeff, large_adj - large_prec),
                           10 ** large_prec)


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((compact_str, compact_prec, compact_ratio),
                          (small_str, small_prec, small_ratio),
                          (large_str, large_prec, large_ratio),
                          (" .829  ", 3, Fraction(829, 1000)),
                          ("\t -00000000 ", 0, 0),
                          ("\t -000.00000", 5, 0)),
                         ids=("compact", "small", "large", "frac-only",
                              "zeros", "zeros-with-point"))
def test_decimal_from_str_dflt_prec(impl, value, prec, ratio):
    dec = impl.Decimal(value)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((compact_str, compact_adj, compact_adj_ratio),
                          (small_str, small_adj, small_adj_ratio),
                          (large_str, large_adj, large_adj_ratio),
                          ("27.81029", IntWrapper(3), Fraction(2781, 100)),
                          (".829", 2, Fraction(83, 100)),
                          (".726", 0, 1)),
                         ids=("compact", "small", "large", "Integral as prec",
                              "frac-only", "carry-over"))
def test_decimal_from_str_adj(impl, value, prec, ratio):
    dec = impl.Decimal(value, prec)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((compact_str, compact_prec, compact_ratio),
                          (small_str, small_prec, small_ratio),
                          (large_str, large_prec, large_ratio),
                          (" .829  ", 3, Fraction(829, 1000)),
                          ("\t -00000000 ", 0, 0),
                          ("\t -000.00000", 5, 0)),
                         ids=("compact", "small", "large", "frac-only",
                              "zeros", "zeros-with-point"))
def test_decimal_from_str_no_adj(impl, value, prec, ratio):
    prec *= 3
    dec = impl.Decimal(value, prec)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize("value", ["\u1811\u1817.\u1814", "\u0f20.\u0f24"],
                         ids=["mongolian", "tibetian"])
def test_decimal_from_non_ascii_digits(impl, value):
    dec = impl.Decimal(value)
    assert isinstance(dec, impl.Decimal)


@pytest.mark.parametrize("value",
                         (" 1.23.5", "1.24e", "--4.92", "", "   ", "3,49E-3",
                          "\t+   \r\n"),
                         ids=("two-points", "missing-exp", "double-sign",
                              "empty-string", "blanks", "invalid-char",
                              "sign-only"))
def test_decimal_from_str_wrong_format(impl, value):
    with pytest.raises(ValueError):
        impl.Decimal(value)


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((compact_str, compact_prec, compact_ratio),
                          (small_str, small_prec, small_ratio),
                          (large_str, large_prec, large_ratio)),
                         ids=("compact", "small", "large"))
def test_decimal_from_decimal_dflt_prec(impl, value, prec, ratio):
    dec = impl.Decimal(value)
    dec = impl.Decimal(dec)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((compact_str, compact_adj, compact_adj_ratio),
                          (small_str, small_adj, small_adj_ratio),
                          (large_str, large_adj, large_adj_ratio)),
                         ids=("compact", "small", "large"))
def test_decimal_from_decimal_adj(impl, value, prec, ratio):
    dec = impl.Decimal(value)
    assert isinstance(dec, impl.Decimal)
    dec = impl.Decimal(dec, prec)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((compact_str, compact_prec, compact_ratio),
                          (small_str, small_prec, small_ratio),
                          (large_str, large_prec, large_ratio)),
                         ids=("compact", "small", "large"))
def test_decimal_from_decimal_no_adj(impl, value, prec, ratio):
    prec += 17
    dec = impl.Decimal(value)
    dec = impl.Decimal(dec, prec)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((StdLibDecimal("123.4567"), 4,
                           Fraction(1234567, 10000)),
                          (5, 0, Fraction(5, 1))),
                         ids=("StdLibDecimal", "int"))
def test_decimal_from_decimal_cls_meth(impl, value, prec, ratio):
    dec = impl.Decimal.from_decimal(value)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize("value",
                         (Fraction(12346, 100), FloatWrapper(328.5), 5.31),
                         ids=("Fraction", "FloatWrapper", "float"))
def test_decimal_from_decimal_cls_meth_wrong_type(impl, value):
    with pytest.raises(TypeError):
        impl.Decimal.from_decimal(value)


@pytest.mark.parametrize(("value", "ratio"),
                         ((compact_coeff, Fraction(compact_coeff, 1)),
                          (small_coeff, Fraction(small_coeff, 1)),
                          (large_coeff, Fraction(large_coeff, 1)),
                          (IntWrapper(328), Fraction(328, 1))),
                         ids=("compact", "small", "large", "IntWrapper"))
def test_decimal_from_integral(impl, value, ratio):
    dec = impl.Decimal(value)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == 0
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((compact_coeff, compact_adj, compact_coeff),
                          (IntWrapper(328), 7, Fraction(328, 1)),
                          (19, 12, 19),
                          (small_coeff, small_adj, Fraction(small_coeff, 1)),
                          (large_coeff, large_adj, Fraction(large_coeff, 1)),
                          ),
                         ids=("compact", "IntWrapper", "prec > 9", "small",
                              "large",))
def test_decimal_from_integral_adj(impl, value, prec, ratio):
    dec = impl.Decimal(value, prec)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((StdLibDecimal(compact_str), compact_prec,
                           compact_ratio),
                          (StdLibDecimal(small_str), small_prec, small_ratio),
                          (StdLibDecimal(large_str), large_prec, large_ratio),
                          (StdLibDecimal("5.4e6"), 0, Fraction(5400000, 1))),
                         ids=("compact", "small", "large", "pos-exp"))
def test_decimal_from_stdlib_decimal_dflt_prec(impl, value, prec, ratio):
    dec = impl.Decimal(value)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((StdLibDecimal(compact_str), compact_adj,
                           compact_adj_ratio),
                          (StdLibDecimal(small_str), small_adj,
                           small_adj_ratio),
                          (StdLibDecimal(large_str), large_adj,
                           large_adj_ratio),
                          (StdLibDecimal("54e-3"), 3, Fraction(54, 1000)),
                          (StdLibDecimal("5.4e4"), 2, Fraction(54000, 1))),
                         ids=("compact", "small", "large", "exp+prec=0",
                              "pos-exp"))
def test_decimal_from_stdlib_decimal_adj(impl, value, prec, ratio):
    dec = impl.Decimal(value, prec)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((StdLibDecimal(compact_str), compact_prec,
                           compact_ratio),
                          (StdLibDecimal(small_str), small_prec, small_ratio),
                          (StdLibDecimal(large_str), large_prec, large_ratio),
                          (StdLibDecimal("5.4e6"), 0, Fraction(5400000, 1))),
                         ids=("compact", "small", "large", "pos-exp"))
def test_decimal_from_stdlib_decimal_no_adj(impl, value, prec, ratio):
    prec *= 5
    dec = impl.Decimal(value, prec)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec"),
                         ((StdLibDecimal('inf'), compact_prec),
                          (StdLibDecimal('-inf'), None),
                          (StdLibDecimal('nan'), large_prec)),
                         ids=("inf", "-inf", "nan"))
def test_decimal_from_incompat_stdlib_decimal(impl, value, prec):
    with pytest.raises(ValueError):
        impl.Decimal(value, prec)


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((17.5, 1, Fraction(175, 10)),
                          (sys.float_info.max, 0,
                           Fraction(int(sys.float_info.max), 1)),
                          (FloatWrapper(328.5), 1, Fraction(3285, 10))),
                         ids=("compact", "float.max", "FloatWrapper"))
def test_decimal_from_float_dflt_prec(impl, value, prec, ratio):
    dec = impl.Decimal(value)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((17.5, 3, Fraction(175, 10)),
                          (sys.float_info.min, 17, Fraction(0, 1)),
                          (FloatWrapper(328.5), 0, Fraction(329, 1))),
                         ids=("compact", "float.min", "FloatWrapper"))
def test_decimal_from_float_adj(impl, value, prec, ratio):
    dec = impl.Decimal(value, prec)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((17.5, 1, Fraction(175, 10)),
                          (sys.float_info.max, 0,
                           Fraction(int(sys.float_info.max), 1)),
                          (FloatWrapper(328.5), 1, Fraction(3285, 10))),
                         ids=("compact", "float.max", "FloatWrapper"))
def test_decimal_from_float_no_adj(impl, value, prec, ratio):
    prec += 7
    dec = impl.Decimal(value, prec)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec"),
                         ((float('inf'), compact_prec),
                          (float('-inf'), None),
                          (float('nan'), large_prec),
                                 # (0.3, None),
                                 # (sys.float_info.min, None)
                          ),
                         ids=("inf", "-inf", "nan",
                                 # "0.3", "float.min"
                              ))
def test_decimal_from_incompat_float(impl, value, prec):
    with pytest.raises(ValueError):
        impl.Decimal(value, prec)


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((1.5, 1, Fraction(15, 10)),
                          (sys.float_info.max, 0,
                           Fraction(int(sys.float_info.max), 1)),
                          (5, 0, Fraction(5, 1))),
                         ids=("compact", "float.max", "int"))
def test_decimal_from_float_cls_meth(impl, value, prec, ratio):
    dec = impl.Decimal.from_float(value)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize("value",
                         (Fraction(12346, 100),
                          FloatWrapper(328.5),
                          StdLibDecimal("5.31")),
                         ids=("Fraction", "FloatWrapper", "StdLibDecimal"))
def test_decimal_from_float_cls_meth_wrong_type(impl, value):
    with pytest.raises(TypeError):
        impl.Decimal.from_float(value)


@pytest.mark.parametrize(("prec", "ratio"),
                         ((1, Fraction(175, 10)),
                          (0, Fraction(int(sys.float_info.max), 1)),
                          (15, Fraction(sys.maxsize, 10 ** 15)),
                          (63, Fraction(1, sys.maxsize + 1))),
                         ids=("compact", "float.max", "maxsize", "frac_only"))
def test_decimal_from_fraction_dflt_prec(impl, prec, ratio):
    dec = impl.Decimal(ratio)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((Fraction(175, 10), 0, Fraction(18, 1)),
                          (Fraction(int(sys.float_info.max), 1), 7,
                           Fraction(int(sys.float_info.max), 1)),
                          (Fraction(sys.maxsize, 10 ** 15), 10,
                           Fraction(round(sys.maxsize, -5), 10 ** 15)),
                          (Fraction(1, 333333333333333333333333333333), 30,
                           round(Fraction(1, 333333333333333333333333333333),
                                 30))),
                         ids=("compact", "float.max", "maxsize", "fraction"))
def test_decimal_from_fraction_adj(impl, value, prec, ratio):
    dec = impl.Decimal(value, prec)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("prec", "ratio"),
                         ((1, Fraction(175, 10)),
                          (0, Fraction(int(sys.float_info.max), 1)),
                          (15, Fraction(sys.maxsize, 10 ** 15)),
                          (63, Fraction(1, sys.maxsize + 1))),
                         ids=("compact", "float.max", "maxsize", "frac_only"))
def test_decimal_from_fraction_no_adj(impl, prec, ratio):
    prec = 2 * prec + 5
    dec = impl.Decimal(ratio, prec)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec"),
                         ((Fraction(1, 3), None),
                                 # (Fraction.from_float(sys.float_info.min), None)
                          ),
                         ids=("1/3",
                                 # "float.min"
                              ))
def test_decimal_from_incompat_fraction(impl, value, prec):
    with pytest.raises(ValueError):
        impl.Decimal(value, prec)


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((17.5, 1, Fraction(175, 10)),
                          (Fraction(1, 3), MAX_DEC_PRECISION,
                           Fraction(int("3" * MAX_DEC_PRECISION),
                                    10 ** MAX_DEC_PRECISION)),
                          (Fraction(328, 100000), 5, Fraction(328, 100000))),
                         ids=("float", "1/3", "Fraction"))
def test_decimal_from_real_cls_meth_non_exact(impl, value, prec, ratio):
    dec = impl.Decimal.from_real(value, exact=False)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((17.5, 1, Fraction(175, 10)),
                          (sys.float_info.max, 0,
                           Fraction(int(sys.float_info.max), 1)),
                          (Fraction(3285, 10), 1, Fraction(3285, 10))),
                         ids=("float", "float.max", "Fraction"))
def test_decimal_from_real_cls_meth_exact(impl, value, prec, ratio):
    dec = impl.Decimal.from_real(value, exact=True)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize("value",
                         (float("inf"),
                          Fraction(1, 3)),
                         ids=("inf", "1/3"))
def test_decimal_from_real_cls_meth_exact_fail(impl, value):
    with pytest.raises(ValueError):
        impl.Decimal.from_real(value, exact=True)


@pytest.mark.parametrize("value",
                         (3 + 2j, "31.209", FloatWrapper(328.5)),
                         ids=("complex", "str", "FloatWrapper"))
def test_decimal_from_real_cls_meth_wrong_type(impl, value):
    with pytest.raises(TypeError):
        impl.Decimal.from_real(value)


@pytest.mark.parametrize("value",
                         ("17.800",
                          ".".join(("1" * 3097, "4" * 33 + "0" * 19)),
                          "-0.00014"),
                         ids=("compact", "large", "fraction"))
def test_copy(impl, value):
    dec = impl.Decimal(value)
    assert copy.copy(dec) is dec
    assert copy.deepcopy(dec) is dec


@pytest.mark.parametrize("value",
                         (# "17.800e2305",
                          # ".".join(("1" * 2260, "4" * 33 + "0" * 19)),
                          "-0." + "7" * (MAX_DEC_PRECISION + 1),),
                         ids=(# "exp > 127",
                              # "coeff too large",
                              f"prec > {MAX_DEC_PRECISION}",))
def test_limits_exceeded(impl, value):
    with pytest.raises(ValueError):
        impl.Decimal(value)

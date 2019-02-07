#!/usr/bin/env python
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Name:        test_constructors
# Purpose:     Test driver for both implementations of decimalfp
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


"""Test driver for both implementations of decimalfp."""


import sys
from decimal import Decimal as StdLibDecimal  # , InvalidOperation
from fractions import Fraction

import pytest

from decimalfp import (
    Decimal,
    ROUNDING,
    set_rounding,
)


# set default rounding to ROUND_HALF_UP
set_rounding(ROUNDING.ROUND_HALF_UP)


class IntWrapper():

    def __init__(self, i):
        assert isinstance(i, int)
        self.i = i

    def __int__(self):
        """int(self)"""
        return self.i

    def __eq__(self, i):
        """self == i"""
        return self.i == i


class FloatWrapper():

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


@pytest.mark.parametrize("value", [Decimal, 3+2j],
                         ids=("value=Decimal", "value=3+2j"))
def test_decimal_wrong_value_type(impl, value):
    with pytest.raises(TypeError):
        impl.Decimal(value=value)


@pytest.mark.parametrize("prec", ["5", 7.5, IntWrapper(5)],
                         ids=("prec='5'", "prec=7.5", "prec=IntWrapper(5)"))
def test_decimal_wrong_precision_type(impl, prec):
    with pytest.raises(TypeError):
        impl.Decimal(precision=prec)


def test_decimal_wrong_precision_value(impl):
    with pytest.raises(ValueError):
        impl.Decimal(precision=-7)


compact_coeff = 174
compact_prec = 1
compact_ratio = Fraction(compact_coeff, 10 ** compact_prec)
compact_str = "17.4"
compact_adj = 2
compact_adj_ratio = compact_ratio
small_coeff = 123456789012345678901234567890
small_prec = 20
small_ratio = Fraction(-small_coeff, 10 ** small_prec)
small_str = "-12345678901234567890.1234567890E-10"
small_adj = 15
small_adj_ratio = Fraction(round(-small_coeff, small_adj - small_prec),
                           10 ** small_prec)
large_coeff = 294898 * 10 ** 24573 + 1498953
large_prec = 24573
large_ratio = Fraction(large_coeff, 10 ** large_prec)
large_str = f"{large_coeff}e-{large_prec}"
large_adj = large_prec - 30
large_adj_ratio = Fraction(round(large_coeff, large_adj - large_prec),
                           10 ** large_prec)


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((compact_str, compact_prec, compact_ratio),
                          (small_str, small_prec, small_ratio),
                          (large_str, large_prec, large_ratio)),
                         ids=("compact", "small", "large"))
def test_decimal_from_str(impl, value, prec, ratio):
    dec = impl.Decimal(value)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((compact_str, compact_adj, compact_adj_ratio),
                          (small_str, small_adj, small_adj_ratio),
                          (large_str, large_adj, large_adj_ratio)),
                         ids=("compact", "small", "large"))
def test_decimal_from_str_adj(impl, value, prec, ratio):
    dec = impl.Decimal(value, prec)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


# @pytest.mark.parametrize("value", ["\u1811\u1817.\u1814", "\u0f20.\u0f24"],
#                          ids=["mongolian", "tibetian"])
# def test_decimal_from_non_ascii_digits(impl, value):
#     dec = impl.Decimal(value)
#     assert isinstance(dec, impl.Decimal)


@pytest.mark.parametrize(("value"),
                         (" 1.23.5", "1.24e", "--4.92", "", "3,49E-3"),
                         ids=("two-points", "missing-exp", "double-sign",
                              "empty-string", "invalid-char"))
def test_decimal_from_str_wrong_format(impl, value):
    with pytest.raises(ValueError):
        impl.Decimal(value)


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((compact_str, compact_prec, compact_ratio),
                          (small_str, small_prec, small_ratio),
                          (large_str, large_prec, large_ratio)),
                         ids=("compact", "small", "large"))
def test_decimal_from_decimal(impl, value, prec, ratio):
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
                         ((StdLibDecimal("123.4567"), 4,
                           Fraction(1234567, 10000)),
                          (5, 0, Fraction(5, 1))),
                         ids=("StdLibDecimal", "int"))
def test_decimal_from_decimal_cls_meth(impl, value, prec, ratio):
    dec = impl.Decimal.from_decimal(value)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value"),
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
                         ((compact_coeff, compact_adj,
                           Fraction(compact_coeff, 1)),
                          (small_coeff, small_adj, Fraction(small_coeff, 1)),
                          (large_coeff, large_adj, Fraction(large_coeff, 1)),
                          (IntWrapper(328), 7, Fraction(328, 1))),
                         ids=("compact", "small", "large", "IntWrapper"))
def test_decimal_from_integral_adj(impl, value, prec, ratio):
    dec = impl.Decimal(value, prec)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((StdLibDecimal(compact_str), compact_prec,
                           compact_ratio),
                          (StdLibDecimal(small_str), small_prec, small_ratio),
                          (StdLibDecimal(large_str), large_prec, large_ratio)
                          ),
                         ids=("compact", "small", "large"))
def test_decimal_from_stdlib_decimal(impl, value, prec, ratio):
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
                           large_adj_ratio)),
                         ids=("compact", "small", "large"))
def test_decimal_from_stdlib_decimal_adj(impl, value, prec, ratio):
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
def test_decimal_from_float(impl, value, prec, ratio):
    dec = impl.Decimal(value)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((17.5, 3, Fraction(175, 10)),
                          (sys.float_info.min, 17, Fraction(0, 1)),
                          (FloatWrapper(328.5), 0, Fraction(329, 1))),
                         ids=("compact", "float.max", "FloatWrapper"))
def test_decimal_from_float_adj(impl, value, prec, ratio):
    dec = impl.Decimal(value, prec)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec"),
                         ((float('inf'), compact_prec),
                          (float('-inf'), None),
                          (float('nan'), large_prec),
                          (0.3, None),
                          (sys.float_info.min, None)),
                         ids=("inf", "-inf", "nan", "0.3", "float.min"))
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


@pytest.mark.parametrize(("value"),
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
                          (15, Fraction(sys.maxsize, 10 ** 15))),
                         ids=("compact", "float.max", "maxsize"))
def test_decimal_from_fraction(impl, prec, ratio):
    dec = impl.Decimal(ratio)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((Fraction(175, 10), 0, Fraction(18, 1)),
                          (Fraction(int(sys.float_info.max), 1), 7,
                           Fraction(int(sys.float_info.max), 1)),
                          (Fraction(sys.maxsize, 10 ** 15), 10,
                           Fraction(round(sys.maxsize, -5), 10 ** 15))),
                         ids=("compact", "float.max", "maxsize"))
def test_decimal_from_fraction_adj(impl, value, prec, ratio):
    dec = impl.Decimal(value, prec)
    assert isinstance(dec, impl.Decimal)
    assert dec.precision == prec
    assert dec.as_fraction() == ratio


@pytest.mark.parametrize(("value", "prec"),
                         ((Fraction(1, 3), None),
                          (Fraction.from_float(sys.float_info.min), None)),
                         ids=("1/3", "float.min"))
def test_decimal_from_incompat_fraction(impl, value, prec):
    with pytest.raises(ValueError):
        impl.Decimal(value, prec)


@pytest.mark.parametrize(("value", "prec", "ratio"),
                         ((17.5, 1, Fraction(175, 10)),
                          (Fraction(1, 3), 32,
                           Fraction(int("3" * 32), 10 ** 32)),
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


@pytest.mark.parametrize(("value"),
                         (float("inf"),
                          Fraction(1, 3),
                          0.1),
                         ids=("inf", "1/3", "0.1"))
def test_decimal_from_real_cls_meth_exact_fail(impl, value):
    with pytest.raises(ValueError):
        impl.Decimal.from_real(value, exact=True)


@pytest.mark.parametrize(("value"),
                         (3 + 2j, "31.209", FloatWrapper(328.5)),
                         ids=("complex", "str", "FloatWrapper"))
def test_decimal_from_real_cls_meth_wrong_type(impl, value):
    with pytest.raises(TypeError):
        impl.Decimal.from_real(value)

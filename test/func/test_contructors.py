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


from importlib import import_module
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
        self.i = i

    def __int__(self):
        """int(self)"""
        return self.i

    def __eq__(self, i):
        """self == i"""
        return self.i == i


@pytest.fixture(scope="session",
                params=("decimalfp._pydecimalfp",
                        "decimalfp._cdecimalfp"),
                ids=("pydec", "cydec"))
def impl(request):
    mod = import_module(request.param)
    return mod


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


# @pytest.mark.parametrize("value", ["\u1811\u1817.\u1814", "\u0f20.\u0f24"],
#                          ids=["mongolian", "tibetian"])
# def test_decimal_from_non_ascii_digits(impl, value):
#     dec = impl.Decimal(value)
#     assert isinstance(dec, impl.Decimal)


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

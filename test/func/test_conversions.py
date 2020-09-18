#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Name:        test_properties
# Purpose:     Test driver for package 'decimalfp' (conversions)
#
# Author:      Michael Amrhein (michael@adrhinum.de)
#
# Copyright:   (c) 2019 Michael Amrhein
# ----------------------------------------------------------------------------
# $Source$
# $Revision$


"""Test driver for package 'decimalfp' (conversions)."""


from fractions import Fraction
import math

import pytest


@pytest.mark.parametrize("value",
                         ("17.8",
                          ".".join(("1" * 3297, "4" * 33)),
                          "0.00014"),
                         ids=("compact", "large", "fraction"))
def test_true(impl, value):
    dec = impl.Decimal(value)
    assert dec


@pytest.mark.parametrize("value", (None, "0.0000"),
                         ids=("None", "0"))
def test_false(impl, value):
    dec = impl.Decimal(value)
    assert not dec


@pytest.mark.parametrize("value",
                         ("0.000",
                          "-17.03",
                          Fraction(9 ** 394, 10 ** 247),
                          Fraction(-19, 4000)),
                         ids=("zero", "compact", "large", "fraction"))
def test_int(impl, value):
    f = Fraction(value)
    dec = impl.Decimal(value)
    assert int(f) == int(dec)


@pytest.mark.parametrize(("num", "den"),
                         ((170, 10),
                          (9 ** 394, 10 ** 247),
                          (-19, 4000)),
                         ids=("compact", "large", "fraction"))
def test_trunc(impl, num, den):
    f = Fraction(num, den)
    dec = impl.Decimal(f)
    assert math.trunc(f) == math.trunc(dec)


@pytest.mark.parametrize(("num", "den"),
                         ((170, 10),
                          (9 ** 394, 10 ** 247),
                          (-19, 4000)),
                         ids=("compact", "large", "fraction"))
def test_floor(impl, num, den):
    f = Fraction(num, den)
    dec = impl.Decimal(f, 250)
    assert math.floor(f) == math.floor(dec)


@pytest.mark.parametrize(("num", "den"),
                         ((170, 10),
                          (9 ** 394, 10 ** 247),
                          (-19, 4000)),
                         ids=("compact", "large", "fraction"))
def test_ceil(impl, num, den):
    f = Fraction(num, den)
    dec = impl.Decimal(f, 250)
    assert math.ceil(f) == math.ceil(dec)


@pytest.mark.parametrize(("num", "den"),
                         ((17, 1),
                          (9 ** 394, 10 ** 247),
                          (-190, 400000)),
                         ids=("compact", "large", "fraction"))
def test_to_float(impl, num, den):
    f = Fraction(num, den)
    dec = impl.Decimal(f, 250)
    assert float(f) == float(dec)


@pytest.mark.parametrize("exp", (0, -54, 43), ids=("e0", "e-", "e+"))
@pytest.mark.parametrize("coeff", (0, 178, 100000, 9 ** 378 * 10),
                         ids=("zero", "small", "10pow", "large"))
@pytest.mark.parametrize("sign", (1, -1), ids=("pos", "neg"))
def test_as_tuple(impl, sign, coeff, exp):
    s = f"{'-' if sign < 0 else ''}{coeff}e{exp}"
    dec = impl.Decimal(s)
    if coeff == 0:
        sign = 0
        exp = 0
    else:
        # normalize coeff
        while coeff % 10 == 0:
            coeff //= 10
            exp += 1
    assert dec.as_tuple() == (sign, coeff, exp)


@pytest.mark.parametrize("value",
                         ("0.00000",
                          17,
                          "-33000.17",
                          Fraction(9 ** 394, 10 ** 247),
                          Fraction(-19, 400000)),
                         ids=("zero", "int", "compact", "large", "fraction"))
def test_as_integer_ratio(impl, value):
    f = Fraction(value)
    dec = impl.Decimal(value)
    assert dec.as_integer_ratio() == (f.numerator, f.denominator)


@pytest.mark.parametrize("value",
                         ("0.00000",
                          17,
                          "-33000.17",
                          Fraction(9 ** 394, 10 ** 247),
                          Fraction(-19, 400000)),
                         ids=("zero", "int", "compact", "large", "fraction"))
def test_as_fraction(impl, value):
    f = Fraction(value)
    dec = impl.Decimal(value)
    assert dec.as_fraction() == f


@pytest.mark.parametrize(("value", "prec", "str_"),
                         ((None, None, "0"),
                          (None, 2, "0.00"),
                          ("-20.7e-3", 5, "-0.02070"),
                          ("0.0000000000207", None, "0.0000000000207"),
                          (887 * 10 ** 14, 0, "887" + "0" * 14)))
def test_str(impl, value, prec, str_):
    dec = impl.Decimal(value, prec)
    assert str(dec) == str_


@pytest.mark.parametrize(("value", "prec", "repr_"),
                         ((None, None, "Decimal(0)"),
                          (None, 2, "Decimal(0, 2)"),
                          ("15", 2, "Decimal(15, 2)"),
                          ("15.4", 2, "Decimal('15.4', 2)"),
                          ("-20.7e-3", 5, "Decimal('-0.0207', 5)"),
                          ("0.0000000000207", None,
                           "Decimal('0.0000000000207')"),
                          (887 * 10 ** 14, 0,
                           "Decimal(887" + "0" * 14 + ")")))
def test_repr(impl, value, prec, repr_):
    dec = impl.Decimal(value, prec)
    assert repr(dec) == repr_

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


@pytest.mark.parametrize(("num", "den"),
                         ((170, 10),
                          (9 ** 394, 10 ** 247),
                          (-19, 4000)),
                         ids=("compact", "large", "fraction"))
def test_trunc(impl, num, den):
    f = Fraction(num, den)
    dec = impl.Decimal(f, 250)
    assert int(f) == int(dec)
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

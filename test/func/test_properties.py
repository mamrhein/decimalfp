#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Name:        test_properties
# Purpose:     Test driver for package 'decimalfp' (properties)
#
# Author:      Michael Amrhein (michael@adrhinum.de)
#
# Copyright:   (c) 2019 Michael Amrhein
# ----------------------------------------------------------------------------
# $Source$
# $Revision$


"""Test driver for package 'decimalfp' (properties)."""


from fractions import Fraction

import pytest


@pytest.mark.parametrize(("value", "magn"),
                         (("17.8", 1),
                          (".".join(("1" * 3297, "4" * 33)), 3296),
                          ("-0.00014", -4)),
                         ids=("compact", "large", "fraction"))
def test_magnitude(impl, value, magn):
    dec = impl.Decimal(value)
    assert dec.magnitude == magn


def test_magnitude_fail_on_zero(impl):
    dec = impl.Decimal()
    with pytest.raises(OverflowError):
        m = dec.magnitude


@pytest.mark.parametrize(("num", "den"),
                         ((170, 10),
                          (9 ** 394, 10 ** 247),
                          (-19, 4000)),
                         ids=("compact", "large", "fraction"))
def test_numerator(impl, num, den):
    f = Fraction(num, den)
    dec = impl.Decimal(f, 250)
    assert dec.numerator == f.numerator


@pytest.mark.parametrize(("num", "den"),
                         ((-17, 1),
                          (9 ** 394, 10 ** 247),
                          (190, 400000)),
                         ids=("compact", "large", "fraction"))
def test_denominator(impl, num, den):
    f = Fraction(num, den)
    dec = impl.Decimal(f, 250)
    assert dec.denominator == f.denominator


@pytest.mark.parametrize("value",
                         ("17.8",
                          ".".join(("1" * 3297, "4" * 33)),
                          "-0.00014"),
                         ids=("compact", "large", "fraction"))
def test_real_imag(impl, value):
    dec = impl.Decimal(value)
    assert dec.real is dec
    assert dec.imag == 0

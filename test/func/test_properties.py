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
                          ("0.00014", -4)),
                         ids=("compact", "large", "fraction"))
def test_magnitude(impl, value, magn):
    dec = impl.Decimal(value)
    assert dec.magnitude == magn

@pytest.mark.parametrize(("num", "den"),
                         ((17, 1),
                          (9 ** 394, 10 ** 247),
                          (19, 4000)),
                         ids=("compact", "large", "fraction"))
def test_num_den(impl, num, den):
    dec = impl.Decimal(Fraction(num, den), 250)
    assert dec.numerator == num
    assert dec.denominator == den

@pytest.mark.parametrize("value",
                         ("17.8",
                          ".".join(("1" * 3297, "4" * 33)),
                          "0.00014"),
                         ids=("compact", "large", "fraction"))
def test_real_imag(impl, value):
    dec = impl.Decimal(value)
    assert dec.real is dec
    assert dec.imag == 0

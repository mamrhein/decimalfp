#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Name:        test_properties
# Purpose:     Test driver for package 'decimalfp' (format)
#
# Author:      Michael Amrhein (michael@adrhinum.de)
#
# Copyright:   (c) 2019 Michael Amrhein
# ----------------------------------------------------------------------------
# $Source$
# $Revision$


"""Test driver for package 'decimalfp' (format)."""


import locale
import os
import sys

import pytest


@pytest.fixture
def set_locale_de():
    loc = locale.getlocale()
    if sys.platform == 'win32':
        yield locale.setlocale(locale.LC_ALL, "German_Germany.1252")
    else:
        yield locale.setlocale(locale.LC_ALL, "de_DE.UTF-8")
    locale.setlocale(locale.LC_ALL, loc)


def test_testdata(impl, set_locale_de):
    dec = impl.Decimal("1234567890.12345678901234567890")
    pth = os.path.dirname(__file__)
    fn = os.path.join(pth, "format.tests")
    with open(fn) as tests:
        for line in tests:
            format_spec, formatted = [s.strip("'")
                                      for s in line.strip().split("\t")]
            assert format(dec, format_spec) == formatted


@pytest.mark.parametrize("value",
                         ("17.800",
                          ".".join(("1" * 3097, "4" * 33 + "0" * 19)),
                          "-0.00014"),
                         ids=("compact", "large", "fraction"))
def test_format_std(impl, value):
    dec = impl.Decimal(value)
    assert format(dec) == str(dec)


@pytest.mark.parametrize("value",
                         ("17.800",
                          ".".join(("1" * 3097, "4" * 33 + "0" * 19)),
                          "-0.00014"),
                         ids=("compact", "large", "fraction"))
@pytest.mark.parametrize("prec", (0, 3, 4, 5, 13))
def test_format_adjusted(impl, value, prec):
    dec = impl.Decimal(value)
    assert format(dec, f"<.{prec}") == str(dec.adjusted(prec))


@pytest.mark.parametrize("format_spec",
                         ("<.",
                          " +012.5F",
                          "_+012.5F",
                          "+012.5e",
                          "+012.5E",
                          "+012.5g",
                          "+012.5G"))
def test_invalid_format_spec(impl, format_spec):
    dec = impl.Decimal("23.7")
    with pytest.raises(ValueError):
        format(dec, format_spec)

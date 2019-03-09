#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Name:        test_adjustments
# Purpose:     Test driver for package 'decimalfp' (adjustments)
#
# Author:      Michael Amrhein (michael@adrhinum.de)
#
# Copyright:   (c) 2019 Michael Amrhein
# ----------------------------------------------------------------------------
# $Source$
# $Revision$


"""Test driver for package 'decimalfp' (adjustments)."""


from decimal import Decimal as StdLibDecimal
from decimal import getcontext
from fractions import Fraction
import pytest

from decimalfp import set_rounding, ROUNDING


set_rounding(ROUNDING.ROUND_HALF_UP)
ctx = getcontext()
ctx.prec = 3350


@pytest.mark.parametrize(("value", "prec"),
                         (("17.800", 1),
                          (".".join(("1" * 3297, "4" * 33 + "0" * 19)), 33),
                          ("0.00014", 5)),
                         ids=("compact", "large", "fraction"))
def test_normalize(impl, value, prec):
    dec = impl.Decimal(value)
    adj = dec.adjusted()
    assert adj.precision == prec
    assert dec.as_fraction() == adj.as_fraction()


@pytest.mark.parametrize(("value", "prec", "numerator"),
                         (("17.849", 1, 178),
                          (".".join(("1" * 3297, "4" * 33)), -3,
                           int("1" * 3294 + "000")),
                          ("0.00015", 4, 2)),
                         ids=("compact", "large", "fraction"))
def test_adjust_dflt_round(impl, value, prec, numerator):
    dec = impl.Decimal(value)
    adj = dec.adjusted(prec)
    res_prec = max(prec, 0)
    assert adj.precision == res_prec
    assert adj.as_fraction() == Fraction(numerator, 10 ** res_prec)


@pytest.mark.parametrize("rnd",
                         [rnd for rnd in ROUNDING],
                         ids=[rnd.name for rnd in ROUNDING])
@pytest.mark.parametrize("value",
                         ("17.849",
                          ".".join(("1" * 3297, "4" * 33)),
                          "0.00015"),
                         ids=("compact",
                              "large",
                              "fraction"))
@pytest.mark.parametrize("prec", (1,
                                  -3,
                                  4),
                         ids=("1",
                              "-3",
                              "4"))
def test_adjust_round(impl, rnd, value, prec):
    dec = impl.Decimal(value)
    adj = dec.adjusted(prec, rnd)
    res_prec = max(prec, 0)
    assert adj.precision == res_prec
    quant = StdLibDecimal("1e%i" % -prec)
    eq_dec = StdLibDecimal(value).quantize(quant, rnd.name)
    assert adj.as_fraction() == Fraction(*eq_dec.as_integer_ratio())

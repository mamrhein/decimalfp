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

from decimalfp import ROUNDING, set_rounding


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
                         ids=("compact", "large", "fraction"))
@pytest.mark.parametrize("prec", (1, -3, 4), ids=("1", "-3", "4"))
def test_adjust_round(impl, rnd, value, prec):
    dec = impl.Decimal(value)
    adj = dec.adjusted(prec, rounding=rnd)
    res_prec = max(prec, 0)
    assert adj.precision == res_prec
    quant = StdLibDecimal("1e%i" % -prec)
    # compute equivalent StdLibDecimal
    eq_dec = StdLibDecimal(value).quantize(quant, rnd.name)
    assert adj.as_fraction() == Fraction(eq_dec)


@pytest.mark.parametrize("prec", ["5", 7.5, Fraction(5, 1)],
                         ids=("prec='5'", "prec=7.5", "prec=Fraction(5, 1)"))
def test_adjust_wrong_precision_type(impl, prec):
    dec = impl.Decimal('3.12')
    with pytest.raises(TypeError):
        dec.adjusted(precision=prec)


@pytest.mark.parametrize("value",
                         ("17.849",
                          ".".join(("1" * 3297, "4" * 33)),
                          "0.0025"),
                         ids=("compact", "large", "fraction"))
@pytest.mark.parametrize("quant", (Fraction(1, 40),
                                   StdLibDecimal("-0.3"),
                                   "0.4",
                                   3),
                         ids=("Fraction 1/4",
                              "StdLibDecimal -0.3",
                              "str 0.4",
                              "3"))
def test_quantize_dflt_round(impl, value, quant):
    dec = impl.Decimal(value)
    adj = dec.quantize(quant)
    # compute equivalent StdLibDecimal
    if isinstance(quant, Fraction):
        q = StdLibDecimal(quant.numerator) / StdLibDecimal(quant.denominator)
    else:
        q = StdLibDecimal(quant)
    eq_dec = (StdLibDecimal(value) / q).quantize(1) * q
    assert adj.as_fraction() == Fraction(eq_dec)


@pytest.mark.parametrize("rnd",
                         [rnd for rnd in ROUNDING],
                         ids=[rnd.name for rnd in ROUNDING])
@pytest.mark.parametrize("value",
                         ("17.849",
                          ".".join(("1" * 3297, "4" * 33)),
                          "0.0025"),
                         ids=("compact", "large", "fraction"))
@pytest.mark.parametrize("quant", (Fraction(1, 40),
                                   StdLibDecimal("-0.3"),
                                   "0.4",
                                   3),
                         ids=("Fraction 1/4",
                              "StdLibDecimal -0.3",
                              "str 0.4",
                              "3"))
def test_quantize_round(impl, rnd, value, quant):
    dec = impl.Decimal(value)
    adj = dec.quantize(quant, rounding=rnd)
    # compute equivalent StdLibDecimal
    if isinstance(quant, Fraction):
        q = StdLibDecimal(quant.numerator) / StdLibDecimal(quant.denominator)
    else:
        q = StdLibDecimal(quant)
    eq_dec = (StdLibDecimal(value) / q).quantize(1, rnd.name) * q
    assert adj.as_fraction() == Fraction(eq_dec)


@pytest.mark.parametrize("quant", ["1/5", 7.5 + 3j, Fraction],
                         ids=("quant='1/5'", "quant=7.5+3j",
                              "quant=Fraction"))
def test_quantize_wrong_quant_type(impl, quant):
    dec = impl.Decimal('3.12')
    with pytest.raises(TypeError):
        dec.quantize(quant)


@pytest.mark.parametrize("value",
                         ("17.849",
                          ".".join(("1" * 3297, "4" * 33)),
                          "0.00015"),
                         ids=("compact", "large", "fraction"))
@pytest.mark.parametrize("prec", (0, -3, 4), ids=("0", "-3", "4"))
def test_round(impl, value, prec):
    dec = impl.Decimal(value)
    adj = round(dec, prec)
    assert isinstance(adj, impl.Decimal)
    res_prec = max(prec, 0)
    assert adj.precision == res_prec
    assert adj.as_fraction() == round(dec.as_fraction(), prec)


@pytest.mark.parametrize("value",
                         ("17.849",
                          ".".join(("1" * 3297, "4" * 33)),
                          "0.00015"),
                         ids=("compact", "large", "fraction"))
def test_round_to_int(impl, value):
    dec = impl.Decimal(value)
    adj = round(dec)
    assert isinstance(adj, int)
    assert adj == round(dec.as_fraction())

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


from fractions import Fraction
import pytest

from decimalfp import set_rounding, ROUNDING


set_rounding(ROUNDING.ROUND_HALF_UP)


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
                          (".".join(("1" * 3297, "4" * 33)), -3, int("1" * 3294 + "000")),
                          ("0.00015", 4, 2)),
                         ids=("compact", "large", "fraction"))
def test_adjust_dflt_round(impl, value, prec, numerator):
    dec = impl.Decimal(value)
    adj = dec.adjusted(prec)
    res_prec = max(prec, 0)
    assert adj.precision == res_prec
    assert adj.as_fraction() == Fraction(numerator, 10 ** res_prec)

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


import pytest


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

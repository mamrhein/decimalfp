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

from decimalfp._pydecimalfp import MAX_DEC_PRECISION


ctx = getcontext()
ctx.prec = 3350


@pytest.fixture(scope="module")
def dflt_rounding(impl):
    rnd = impl.get_dflt_rounding_mode()
    impl.set_dflt_rounding_mode(impl.ROUNDING.ROUND_HALF_UP);
    yield
    impl.set_dflt_rounding_mode(rnd);


def test_dflt_rounding(dflt_rounding):
    """Activate fixture to set default rounding"""
    pass


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


@pytest.mark.parametrize("value",
                         ("17.849",
                          ".".join(("1" * 3297, "4" * 33)),
                          "0.00015"),
                         ids=("compact", "large", "fraction"))
@pytest.mark.parametrize("prec", (1, -3, 5), ids=("1", "-3", "5"))
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


@pytest.mark.parametrize("prec",
                         (MAX_DEC_PRECISION + 1, -MAX_DEC_PRECISION - 1),
                         ids=("max+1", "-max-1"))
def test_adjust_limits_exceeded(impl, prec):
    dec = impl.Decimal("4.83")
    with pytest.raises(ValueError):
        dec.adjusted(prec)


@pytest.mark.parametrize("quant", (Fraction(1, 40),
                                   StdLibDecimal("-0.3"),
                                   0.4,
                                   3,
                                   1),
                         ids=("Fraction 1/40",
                              "StdLibDecimal -0.3",
                              "0.4",
                              "3",
                              "1"))
@pytest.mark.parametrize("value",
                         ("17.849",
                          ".".join(("1" * 2259, "4" * 33)),
                          "0.0025",
                          "12345678901234567e12"),
                         ids=("compact", "large", "fraction", "int"))
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


@pytest.mark.parametrize("quant", (Fraction(1, 40),
                                   StdLibDecimal("-0.3"),
                                   0.4,
                                   3,
                                   1),
                         ids=("Fraction 1/40",
                              "StdLibDecimal -0.3",
                              "0.4",
                              "3",
                              "1"))
@pytest.mark.parametrize("value",
                         ("17.849",
                          ".".join(("1" * 2259, "4" * 33)),
                          "0.0025",
                          "12345678901234567e12"),
                         ids=("compact", "large", "fraction", "int"))
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


@pytest.mark.parametrize("quant", (Fraction(1, 3),
                                   Fraction(5, 7)),
                         ids=("Fraction 1/3",
                              "Fraction 5/7"))
@pytest.mark.parametrize("value",
                         ("17.5",
                          "15"),
                         ids=("17.5", "15"))
def test_quantize_to_non_decimal(impl, value, quant):
    dec = impl.Decimal(value)
    adj = dec.quantize(quant)
    # compute equivalent Fraction
    equiv = round(Fraction(value) / quant) * quant
    assert adj == equiv


@pytest.mark.parametrize("quant", ["0.5", 7.5 + 3j, Fraction],
                         ids=("quant='0.5'", "quant=7.5+3j",
                              "quant=Fraction"))
def test_quantize_wrong_quant_type(impl, quant):
    dec = impl.Decimal('3.12')
    with pytest.raises(TypeError):
        dec.quantize(quant)


@pytest.mark.parametrize("quant", [float('inf'), StdLibDecimal('-inf'),
                                   float('nan'), StdLibDecimal('NaN')],
                         ids=("quant='inf'", "quant='-inf'",
                              "quant='nan'", "quant='NaN'"))
def test_quantize_incompat_quant_value(impl, quant):
    dec = impl.Decimal('3.12')
    with pytest.raises(ValueError):
        dec.quantize(quant)


@pytest.mark.parametrize("value",
                         ("-17.849",
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
                         ("-17.849",
                          ".".join(("1" * 3297, "4" * 33)),
                          "0.00015",
                          "999999999999999999.67",),
                         ids=("compact", "large", "fraction", "carry",))
def test_round_to_int(impl, value):
    dec = impl.Decimal(value)
    adj = round(dec)
    assert isinstance(adj, int)
    assert adj == round(dec.as_fraction())


@pytest.mark.parametrize("value",
                         ("17.8",
                          ".".join(("1" * 3297, "4" * 33)),
                          "-0.00014"),
                         ids=("compact", "large", "fraction"))
def test_pos(impl, value):
    dec = impl.Decimal(value)
    assert +dec is dec


@pytest.mark.parametrize("value",
                         ("17.8",
                          "-" + ".".join(("1" * 3297, "4" * 33)),
                          "-0.00014",
                          "0.00"),
                         ids=("compact", "large", "fraction", "0"))
def test_neg(impl, value):
    dec = impl.Decimal(value)
    assert -(-dec) == dec
    assert -(-(-dec)) == -dec
    if dec <= 0:
        assert -dec >= 0
    else:
        assert -dec <= 0


@pytest.mark.parametrize("value",
                         ("17.8",
                          "-" + ".".join(("1" * 3297, "4" * 33)),
                          "-0.00014",
                          "0.00"),
                         ids=("compact", "large", "fraction", "0"))
def test_abs(impl, value):
    dec = impl.Decimal(value)
    assert abs(dec) >= 0
    if dec < 0:
        assert abs(dec) == -dec
    else:
        assert abs(dec) == dec

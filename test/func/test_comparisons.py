#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Name:        test_adjustments
# Purpose:     Test driver for package 'decimalfp' (comparisons)
#
# Author:      Michael Amrhein (michael@adrhinum.de)
#
# Copyright:   (c) 2019 Michael Amrhein
# ----------------------------------------------------------------------------
# $Source$
# $Revision$


"""Test driver for package 'decimalfp' (comparisons)."""


from decimal import Decimal as StdLibDecimal
from decimal import getcontext
from fractions import Fraction
import operator
import sys

import pytest


EQUALITY_OPS = (operator.eq, operator.ne)
ORDERING_OPS = (operator.le, operator.lt, operator.ge, operator.gt)


ctx = getcontext()
ctx.prec = 3350


def chk_eq(dec, equiv):
    assert dec == equiv
    assert dec >= equiv
    assert dec <= equiv
    assert not(dec != equiv)
    assert not(dec > equiv)
    assert not(dec < equiv)
    # x == y  <=> hash(x) == hash (y)
    assert hash(dec) == hash(equiv)


@pytest.mark.parametrize("value",
                         ("-17",
                          "".join(("1" * 3097, "4" * 33, "0" * 19)),
                          "-0"),
                         ids=("compact", "large", "zero"))
@pytest.mark.parametrize("trail", (".000", ".", ""),
                         ids=("trail='000'", "trail='.'", "trail=''"))
def test_eq_integral(impl, value, trail):
    dec = impl.Decimal(value + trail)
    equiv = int(value)
    chk_eq(dec, equiv)


@pytest.mark.parametrize("rational",
                         (None, StdLibDecimal, Fraction),
                         ids=("Decimal", "StdLibDecimal", "Fraction"))
@pytest.mark.parametrize("value",
                         ("17.800",
                          ".".join(("1" * 3097, "4" * 33 + "0" * 19)),
                          "-0.00014"),
                         ids=("compact", "large", "fraction"))
@pytest.mark.parametrize("trail1", ("000", ""),
                         ids=("trail1='000'", "trail1=''"))
@pytest.mark.parametrize("trail2", ("0000", ""),
                         ids=("trail2='0000'", "trail2=''"))
def test_eq_rational(impl, rational, value, trail1, trail2):
    if rational is None:
        rational = impl.Decimal
    dec = impl.Decimal(value + trail1)
    equiv = rational(value + trail2)
    chk_eq(dec, equiv)


@pytest.mark.parametrize("value",
                         ("17.500",
                          sys.float_info.max * 0.9,
                          "%1.63f" % (1 / sys.maxsize)),
                         ids=("compact", "large", "fraction"))
def test_eq_real(impl, value):
    dec = impl.Decimal(value, 400)
    equiv = float(value)
    chk_eq(dec, equiv)


@pytest.mark.parametrize("value",
                         ("17.500",
                          sys.float_info.max,
                          "%1.63f" % (1 / sys.maxsize)),
                         ids=("compact", "large", "fraction"))
def test_eq_complex(impl, value):
    dec = impl.Decimal(value, 400)
    equiv = complex(value)
    non_equiv = complex(float(value), 1)
    assert dec == equiv
    assert not (dec != equiv)
    assert dec != non_equiv
    assert not (dec == non_equiv)


def chk_gt(dec, non_equiv_gt):
    assert dec != non_equiv_gt
    assert dec < non_equiv_gt
    assert dec <= non_equiv_gt
    assert not(dec == non_equiv_gt)
    assert not(dec > non_equiv_gt)
    assert not(dec >= non_equiv_gt)
    # x != y  <=> hash(x) != hash (y)
    assert hash(dec) != hash(non_equiv_gt)


def chk_lt(dec, non_equiv_lt):
    assert dec != non_equiv_lt
    assert dec > non_equiv_lt
    assert dec >= non_equiv_lt
    assert not(dec == non_equiv_lt)
    assert not(dec < non_equiv_lt)
    assert not(dec <= non_equiv_lt)
    # x != y  <=> hash(x) != hash (y)
    assert hash(dec) != hash(non_equiv_lt)


@pytest.mark.parametrize("value",
                         ("-17",
                          "".join(("1" * 3097, "4" * 33, "0" * 19)),
                          "-0"),
                         ids=("compact", "large", "zero"))
def test_ne_integral(impl, value):
    non_equiv = int(value)
    prec = len(value)
    delta = Fraction(1, 10 ** prec)
    dec_gt = impl.Decimal(non_equiv + delta, prec)
    chk_lt(dec_gt, non_equiv)
    dec_lt = impl.Decimal(non_equiv - delta, prec)
    chk_gt(dec_lt, non_equiv)


@pytest.mark.parametrize("rational",
                         (None, StdLibDecimal, Fraction),
                         ids=("Decimal", "StdLibDecimal", "Fraction"))
@pytest.mark.parametrize("value",
                         ("17.800",
                          ".".join(("1" * 3097, "4" * 33 + "0" * 19)),
                          "-0.00014"),
                         ids=("compact", "large", "fraction"))
def test_ne_rational(impl, rational, value):
    if rational is None:
        rational = impl.Decimal
    dec = impl.Decimal(value)
    non_equiv_gt = rational(value) + rational(1) / 10 ** 180
    chk_gt(dec, non_equiv_gt)
    non_equiv_lt = rational(value) - rational(1) / 10 ** 180
    chk_lt(dec, non_equiv_lt)


@pytest.mark.parametrize("value",
                         ("17.500",
                          sys.float_info.max * 0.9,
                          "%1.63f" % (1 / sys.maxsize)),
                         ids=("compact", "large", "fraction"))
def test_ne_real(impl, value):
    dec = impl.Decimal(value, 400)
    non_equiv_gt = float(value) * (1. + 1. / 10 **14)
    chk_gt(dec, non_equiv_gt)
    non_equiv_lt = float(value) * (1. - 1. / 10 **14)
    chk_lt(dec, non_equiv_lt)


@pytest.mark.parametrize("op",
                         [op for op in ORDERING_OPS],
                         ids=[op.__name__ for op in ORDERING_OPS])
@pytest.mark.parametrize("value", ["0.4", "1.75+3j"],
                         ids=("other='0.4'", "1.75+3j"))
def test_ord_ops_complex(impl, op, value):
    dec = impl.Decimal('3.12')
    cmplx = complex(value)
    with pytest.raises(TypeError):
        op(dec, cmplx)


@pytest.mark.parametrize("other", ["1/5", operator.ne],
                         ids=("other='1/5'", "other=operator.ne"))
def test_eq_ops_non_number(impl, other):
    dec = impl.Decimal('3.12')
    assert not(dec == other)
    assert dec != other


@pytest.mark.parametrize("op",
                         [op for op in ORDERING_OPS],
                         ids=[op.__name__ for op in ORDERING_OPS])
@pytest.mark.parametrize("other", ["1/5", operator.ne],
                         ids=("other='1/5'", "other=operator.ne"))
def test_ord_ops_non_number(impl, op, other):
    dec = impl.Decimal('3.12')
    with pytest.raises(TypeError):
        op(dec, other)

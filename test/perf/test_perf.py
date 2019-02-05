#!/usr/bin/env python
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Name:        test_perf
# Purpose:     Compare performance of different implementations of Decimal
#
# Author:      Michael Amrhein michael@adrhinum.de)
#
# Copyright:   (c) 2014 ff. Michael Amrhein
# License:     This program is free software. You can redistribute it, use it
#              and/or modify it under the terms of the 2-clause BSD license.
#              For license details please read the file LICENSE.TXT provided
#              together with the source code.
# ----------------------------------------------------------------------------
# $Source$
# $Revision$


"""Compare performance of different implementations of Decimal."""


# import math
# import os
# import sys
from collections import namedtuple
from importlib import import_module
# from decimal import Decimal as StdLibDecimal

import pytest

# from decimalfp import ROUNDING
# from decimalfp._pydecimalfp import Decimal as PyDecimal             # noqa
# from decimalfp._cdecimalfp import Decimal as CDecimal               # noqa


@pytest.fixture(scope="session",
                params=("decimal", "decimalfp._pydecimalfp",
                        "decimalfp._cdecimalfp"),
                ids=("stdlib", "pydec", "cydec"))
def impl(request):
    """Return Decimal implementation."""
    mod = import_module(request.param)
    return mod


StrVals = namedtuple('StrVals', "compact, small, large")
str_vals = StrVals("+17.4",
                   "-1234567890.12345678901234567890",
                   "9" * 294 + "." + "183" * 81)


@pytest.mark.parametrize("value", str_vals, ids=str_vals._fields)
def test_decimal_from_str(impl, benchmark, value):
    """Decimal from string."""
    Decimal = impl.Decimal
    dec = benchmark(Decimal, value)
    assert isinstance(dec, Decimal)

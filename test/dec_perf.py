#!/usr/bin/env python
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Name:        dec_perf
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


from __future__ import absolute_import, division, print_function

import math
import os
import platform
import sys
from timeit import Timer

from decimalfp import ROUNDING
from decimalfp._pydecimalfp import Decimal as PyDecimal             # noqa
from decimalfp._cdecimalfp import Decimal as CDecimal               # noqa

PY_IMPL = platform.python_implementation()
PY_VERSION = platform.python_version()

dec_impls = ("PyDecimal", "CDecimal")


def testComputation(cls):
    """Execute several computations for performance testing."""
    f = cls('23.25')
    g = cls('-23.2562398')
    h = cls(sys.maxsize ** 10) + cls(1 / f, 128)
    b = (--f == +f)
    b = (abs(g) == abs(-g))
    r = g - g
    r = f + g - h
    r = f - 23
    r = 23 - f
    b = -(3 * f)
    b = (-3) * h
    b = ((2 * f) * f == f * (2 * f) == f * (f * 2))
    b = 3 * h / f
    f2 = -2 * f
    b = ((-f2) / f == f2 / (-f) == -(f2 / f) == 2)
    b = g / f
    b = g // f
    b = h / -g
    b = (g % -f == h)
    b = divmod(24, f)
    b = divmod(-g, h)
    b = f ** 2
    b = 1 / g ** 2
    b = 2 ** f
    b = math.floor(f)
    b = math.floor(g)
    b = math.ceil(f)
    b = math.ceil(g)
    b = round(f)
    b = round(g)
    for mode in ROUNDING:
        b = h.adjusted(14, mode)
    if b:
        return r
    else:
        return -r


if __name__ == '__main__':
    print(os.path.realpath(os.path.curdir))
    print('---', PY_IMPL, PY_VERSION, '---')
    for impl in dec_impls:
        timer = Timer("testComputation(%s)" % impl,
                      "from dec_perf import testComputation, %s" % impl)
        results = timer.repeat(10, 1000)
        print("%s: %s" % (impl, min(results)))
    print('---')

# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Name:        conftest
# Purpose:     Shared pytest fixtures (for performance tests)
#
# Author:      Michael Amrhein (michael@adrhinum.de)
#
# Copyright:   (c) 2019 Michael Amrhein
# License:     This program is part of a larger application. For license
#              details please read the file LICENSE.TXT provided together
#              with the application.
# ----------------------------------------------------------------------------
# $Source$
# $Revision$


"""Shared pytest fixtures (for performance tests)."""


# standard library imports

from collections import namedtuple
from importlib import import_module

# third-party imports

import pytest


@pytest.fixture(scope="session",
                params=(("decimal", None),
                        ("decimalfp._pydecimalfp", "decimalfp"),
                        ("decimalfp._cdecimalfp", "decimalfp"),),
                ids=("stdlib", "pydec", "cdec"))
def impl(request):
    """Return Decimal implementation."""
    mod = import_module(*request.param)
    return mod


StrVals = namedtuple('StrVals', "compact, small, large")
str_vals = StrVals("+17.4",
                   "1378570027748199474127664735846.4",
                   "23736879701971506826510228743275818303082405966963"
                   "10266102448847546256985139661624907044831705726200"
                   "26685864126453500524062407462382244055484510207919"
                   "76477494461052153151151671404578771319069576820096"
                   "22675846630147142057752885245146367885627742299476"
                   "14681983358617778148564596922111292238977187282815"
                   "5326000516131275861459807299866165299818918.084320"
                   "75746062699164383497897468539877308669886464")


@pytest.fixture(scope="session",
                params=str_vals,
                ids=str_vals._fields)
def str_value(request):
    return request.param


@pytest.fixture(scope="session",
                params=str_vals,
                ids=str_vals._fields)
def dec_value_1(impl, request):
    return impl.Decimal(request.param) * impl.Decimal("2.5")


@pytest.fixture(scope="session",
                params=str_vals,
                ids=str_vals._fields)
def dec_value_2(impl, request):
    return impl.Decimal(request.param)

# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Name:        conftest
# Purpose:     Shared pytest fixtures
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


"""Shared pytest fixtures."""


# standard library imports

from importlib import import_module

# third-party imports

import pytest

# local imports


@pytest.fixture(scope="session",
                params=("decimalfp._pydecimalfp",
                        "decimalfp._cdecimalfp"),
                ids=("pydec", "cydec"))
def impl(request):
    mod = import_module(request.param)
    return mod

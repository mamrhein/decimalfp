# -*- coding: utf-8 -*-
#-----------------------------------------------------------------------------
# Name:        rounding
# Purpose:     Rounding parameters for decimal fixed-point arithmetic
#
# Author:      Michael Amrhein (michael@adrhinum.de)
#
# Copyright:   (c) 2018 Michael Amrhein
# License:     This program is part of a larger application. For license
#              details please read the file LICENSE.TXT provided together
#              with the application.
#-----------------------------------------------------------------------------
# $Source$
# $Revision$


"""Rounding parameters for decimal fixed-point arithmetic."""


# standard library imports
from decimal import getcontext as _getcontext
from enum import Enum

# third-party imports


# local imports


# precision limit for division or conversion without explicitly given
# precision
LIMIT_PREC = 32


# rounding modes equivalent to those defined in standard lib module 'decimal'
class ROUNDING(Enum):

    """Enumeration of rounding modes."""

    # Implementation of __index__ depends on values in ROUNDING being ints
    # starting with 1 !!!
    __next_value__ = 1

    def __new__(cls, doc):
        """Return new member of the Enum."""
        member = object.__new__(cls)
        member._value_ = cls.__next_value__
        cls.__next_value__ += 1
        member.__doc__ = doc
        return member

    def __index__(self):
        """Return `self` converted to an `int`."""
        return self.value - 1

    __int__ = __index__

    #: Round away from zero if last digit after rounding towards
    #: zero would have been 0 or 5; otherwise round towards zero.
    ROUND_05UP = 'Round away from zero if last digit after rounding towards '\
        'zero would have been 0 or 5; otherwise round towards zero.'
    #: Round towards Infinity.
    ROUND_CEILING = 'Round towards Infinity.'
    #: Round towards zero.
    ROUND_DOWN = 'Round towards zero.'
    #: Round towards -Infinity.
    ROUND_FLOOR = 'Round towards -Infinity.'
    #: Round to nearest with ties going towards zero.
    ROUND_HALF_DOWN = 'Round to nearest with ties going towards zero.'
    #: Round to nearest with ties going to nearest even integer.
    ROUND_HALF_EVEN = \
        'Round to nearest with ties going to nearest even integer.'
    #: Round to nearest with ties going away from zero.
    ROUND_HALF_UP = 'Round to nearest with ties going away from zero.'
    #: Round away from zero.
    ROUND_UP = 'Round away from zero.'


# Python 2 / Python 3
import sys                                                  # noqa: I100, I202
if sys.version_info[0] < 3:
    # rounding mode of builtin round function
    ROUNDING.default = ROUNDING.ROUND_HALF_UP
else:
    # In 3.0 round changed from half-up to half-even !
    ROUNDING.default = ROUNDING.ROUND_HALF_EVEN
del sys


# functions to get / set rounding mode
def get_rounding():
    """Return rounding mode from current context."""
    ctx = _getcontext()
    return ROUNDING[ctx.rounding]


def set_rounding(rounding):
    """Set rounding mode in current context.

    Args:
        rounding (ROUNDING): rounding mode to be set

    """
    ctx = _getcontext()
    ctx.rounding = rounding.name


__all__ = [
    'LIMIT_PREC',
    'ROUNDING',
    'get_rounding',
    'set_rounding',
]

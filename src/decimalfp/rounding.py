# -*- coding: utf-8 -*-
##----------------------------------------------------------------------------
## Name:        rounding
## Purpose:     Rounding parameters for decimal fixed-point arithmetic
##
## Author:      Michael Amrhein (mamrhein@users.sourceforge.net)
##
## Copyright:   (c) 2018 Michael Amrhein
## License:     This program is free software. You can redistribute it, use it
##              and/or modify it under the terms of the 2-clause BSD license.
##              For license details please read the file LICENSE.TXT provided
##              together with the source code.
##----------------------------------------------------------------------------
## $Source$
## $Revision$


"""Rounding parameters for decimal fixed-point arithmetic."""

#
# rounding modes
from decimal import (                                           # noqa: F401
    ROUND_05UP,
    ROUND_CEILING,
    ROUND_DOWN,
    ROUND_FLOOR,
    ROUND_HALF_DOWN,
    ROUND_HALF_EVEN,
    ROUND_HALF_UP,
    ROUND_UP,
)
# function to get context from decimal
from decimal import getcontext as _getcontext


# precision limit for division or conversion without explicitly given
# precision
LIMIT_PREC = 32


def get_limit_prec():
    """Return precision limit."""
    return LIMIT_PREC


# functions to get / set rounding mode
def get_rounding():
    """Return rounding mode from current context."""
    ctx = _getcontext()
    return ctx.rounding


def set_rounding(rounding):
    """Set rounding mode in current context."""
    ctx = _getcontext()
    ctx.rounding = rounding

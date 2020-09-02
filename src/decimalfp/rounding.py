# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Name:        rounding
# Purpose:     Rounding parameters for decimal fixed-point arithmetic
#
# Author:      Michael Amrhein (michael@adrhinum.de)
#
# Copyright:   (c) 2018 Michael Amrhein
# License:     This program is part of a larger application. For license
#              details please read the file LICENSE.TXT provided together
#              with the application.
# ----------------------------------------------------------------------------
# $Source$
# $Revision$


"""Rounding modes for decimal fixed-point arithmetic."""


# standard library imports
from enum import Enum


# rounding modes equivalent to those defined in standard lib module 'decimal'
class ROUNDING(Enum):

    """Enumeration of rounding modes."""

    __next_value__ = 1

    def __new__(cls, doc: str) -> 'ROUNDING':
        """Return new member of the Enum."""
        member = object.__new__(cls)
        member._value_ = cls.__next_value__
        cls.__next_value__ += 1
        member.__doc__ = doc
        return member

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


__all__ = [
    'ROUNDING',
]

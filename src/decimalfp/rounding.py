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
from enum import Enum, EnumMeta


class _EnumMetaWithDefault(EnumMeta):

    def __init__(cls, cls_name, bases, classdict):
        super().__init__(cls_name, bases, classdict)
        cls._default = None

    @property
    def default(cls) -> Enum:
        """Return default value."""
        return cls._default

    @default.setter
    def default(cls, dflt: Enum):
        assert(isinstance(dflt, cls))
        cls._default = dflt


# rounding modes equivalent to those defined in standard lib module 'decimal'
class ROUNDING(metaclass=_EnumMetaWithDefault):

    """Enumeration of rounding modes."""

    __next_value__ = 1

    def __new__(cls, doc: str) -> 'ROUNDING':
        """Return new member of the Enum."""
        member = object.__new__(cls)
        member._value_ = cls.__next_value__
        cls.__next_value__ += 1
        member.__doc__ = doc
        return member

    @property
    def name(self) -> str:
        """Name of the element."""
        return self._name_

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


# In 3.0 round changed from half-up to half-even !
ROUNDING.default = ROUNDING.ROUND_HALF_EVEN


# functions to get / set rounding mode
def get_rounding() -> ROUNDING:
    """Return default rounding mode."""
    return ROUNDING.default


def set_rounding(rounding: ROUNDING):
    """Set default rounding mode.

    Args:
        rounding (ROUNDING): rounding mode to be set as default

    """
    ROUNDING.default = rounding


__all__ = [
    'ROUNDING',
    'get_rounding',
    'set_rounding',
]

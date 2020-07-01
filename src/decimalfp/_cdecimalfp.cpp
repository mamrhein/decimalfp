/* ---------------------------------------------------------------------------
Name:        _cdecimalfp.cpp

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application. For license
             details please read the file LICENSE.TXT provided together
             with the application.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#include <pybind11/pybind11.h>
#include <pybind11/operators.h>
#include <pybind11/iostream.h>
#include "libfpdec/fpdecimal.hpp"

using namespace fpdec;
namespace py = pybind11;

PYBIND11_MODULE(_cdecimalfp, m) {
    py::class_<Decimal>(m, "Decimal")
        .def(py::init<>())
        .def(py::init<const std::string>())
        .def(py::init<const long long int>())
        .def(py::init<const Decimal &>())
        .def(py::init<const Decimal &, const fpdec_dec_prec_t,
            const Rounding>())
            // properties
        .def_property_readonly("precision", &Decimal::precision)
        .def_property_readonly("magnitude", &Decimal::magnitude)
            // operators
        .def("__pos__", (Decimal (Decimal::*)() const) &Decimal::operator+)
        .def("__neg__", (Decimal (Decimal::*)() const) &Decimal::operator-)
        .def(py::self == py::self)
        .def(py::self != py::self)
        .def(py::self <= py::self)
        .def(py::self < py::self)
        .def(py::self >= py::self)
        .def(py::self > py::self)
        .def(py::self + py::self)
        .def(py::self - py::self)
        .def(py::self * py::self)
        .def(py::self / py::self)
        // member functions
        .def("_dump", &Decimal::dump)
        ;
    py::enum_<Rounding>(m, "ROUNDING")
        .value("ROUND_05UP", Rounding::round_05up)
        .value("ROUND_CEILING", Rounding::round_ceiling)
        .value("ROUND_DOWN", Rounding::round_down)
        .value("ROUND_FLOOR", Rounding::round_floor)
        .value("ROUND_HALF_DOWN", Rounding::round_half_down)
        .value("ROUND_HALF_EVEN", Rounding::round_half_even)
        .value("ROUND_HALF_UP", Rounding::round_half_up)
        .value("ROUND_UP", Rounding::round_up);
}


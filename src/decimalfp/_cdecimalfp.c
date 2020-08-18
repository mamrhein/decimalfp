/* ---------------------------------------------------------------------------
Name:        _cdecimalfp.c

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application. For license
             details please read the file LICENSE.TXT provided together
             with the application.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#define PY_SSIZE_T_CLEAN
#define Py_LIMITED_API 0x03060000

#include <Python.h>
#include "_cdecimalfp_docstrings.h"
#include "libfpdec/fpdec.h"
#include "libfpdec/fpdec_struct.h"
#include "libfpdec/digit_array_struct.h"

#define ASSIGN_AND_CHECK_NULL(result, expr) \
    do { result = (expr); if (result == NULL) goto ERROR; } while (0)

// Abstract number types

static PyObject *Number = NULL;
static PyObject *Complex = NULL;
static PyObject *Real = NULL;
static PyObject *Rational = NULL;
static PyObject *Integral = NULL;

// Concrete number types

static PyObject *Fraction = NULL;
static PyObject *StdLibDecimal = NULL;

// Python math functions

static PyCFunction PyNumber_gcd;

// *** error handling ***

static int
value_error_int(const char *msg) {
    PyErr_SetString(PyExc_ValueError, msg);
    return -1;
}

static PyObject *
value_error_ptr(const char *msg) {
    PyErr_SetString(PyExc_ValueError, msg);
    return NULL;
}

static int
type_error_int(const char *msg) {
    PyErr_SetString(PyExc_TypeError, msg);
    return -1;
}

static PyObject *
type_error_ptr(const char *msg) {
    PyErr_SetString(PyExc_TypeError, msg);
    return NULL;
}

static int
runtime_error_int(const char *msg) {
    PyErr_SetString(PyExc_RuntimeError, msg);
    return -1;
}

static PyObject *
runtime_error_ptr(const char *msg) {
    PyErr_SetString(PyExc_RuntimeError, msg);
    return NULL;
}

#define CHECK_FPDEC_ERROR(rc)                                               \
    switch (rc) {                                                           \
        case FPDEC_OK:                                                      \
            break;                                                          \
        case ENOMEM:                                                        \
            return PyErr_NoMemory();                                        \
        case FPDEC_PREC_LIMIT_EXCEEDED:                                     \
            return value_error_ptr("Precision limit exceeded.");            \
        case FPDEC_EXP_LIMIT_EXCEEDED:                                      \
        case FPDEC_N_DIGITS_LIMIT_EXCEEDED:                                 \
            PyErr_SetString(PyExc_OverflowError,                            \
                            "Internal limit exceeded.");                    \
            return NULL;                                                    \
        case FPDEC_INVALID_DECIMAL_LITERAL:                                 \
            return value_error_ptr("Invalid Decimal literal.");             \
        case FPDEC_DIVIDE_BY_ZERO:                                          \
            PyErr_SetString(PyExc_ZeroDivisionError, "Division by zero.");  \
            return NULL;                                                    \
        default:                                                            \
            PyErr_SetString(PyExc_SystemError, "Unknown error code.");      \
            return NULL;                                                    \
    }

// *** Python number constants ***

static PyObject *PyZERO;
static PyObject *PyONE;
static PyObject *PyTEN;
static PyObject *PyRADIX;

// *** Helper prototypes ***

static long
fpdec_dec_coeff_exp(PyObject **coeff, const fpdec_t *fpdec);

static void
fpdec_as_integer_ratio(PyObject **numerator, PyObject **denominator,
                       const fpdec_t *fpdec);

// *** Decimal type ***

typedef struct {
    PyObject_HEAD
    Py_hash_t hash;
    PyObject *numerator;
    PyObject *denominator;
    fpdec_t fpdec;
} DecimalObject;

static PyTypeObject *DecimalType;

// type checks

static inline int
Decimal_Check_Exact(PyObject *obj) {
    return (Py_TYPE(obj) == DecimalType);
}

static inline int
Decimal_Check(PyObject *obj) {
    return PyObject_TypeCheck(obj, DecimalType);
}

// constructors / destructors

static DecimalObject *
DecimalType_alloc(PyTypeObject *type) {
    DecimalObject *dec;

    if (type == DecimalType)
        dec = PyObject_New(DecimalObject, type);
    else {
        allocfunc tp_alloc = (allocfunc)PyType_GetSlot(type, Py_tp_alloc);
        dec = (DecimalObject *)tp_alloc(type, 0);
    }
    if (dec == NULL) {
        return NULL;
    }

    dec->hash = -1;
    dec->numerator = NULL;
    dec->denominator = NULL;
    dec->fpdec = FPDEC_ZERO;
    return dec;
}

static void
Decimal_dealloc(DecimalObject *self) {
    PyTypeObject *tp = Py_TYPE(self);
    fpdec_reset_to_zero(&self->fpdec, 0);
    Py_XDECREF(self->numerator);
    Py_XDECREF(self->denominator);
    Py_XDECREF(self->hash);
    PyObject_Free(self);
    Py_DECREF(tp);
}

#define DECIMAL_ALLOC(type) \
    DecimalObject *self; \
    do {self = DecimalType_alloc(type); \
        if (self == NULL) \
            return NULL;  \
        } while (0)

static PyObject *
DecimalType_from_fpdec(PyTypeObject *type, fpdec_t *fpdec,
                       long adjust_to_prec) {
    error_t rc;
    DECIMAL_ALLOC(type);

    if (adjust_to_prec == -1 || adjust_to_prec == FPDEC_DEC_PREC(fpdec))
        rc = fpdec_copy(&self->fpdec, fpdec);
    else
        rc = fpdec_adjusted(&self->fpdec, fpdec, adjust_to_prec,
                            FPDEC_ROUND_DEFAULT);
    CHECK_FPDEC_ERROR(rc)
}

static PyObject *
DecimalType_from_float(PyTypeObject *type, PyObject *f) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
DecimalType_from_decimal(PyTypeObject *type, PyObject *d) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
DecimalType_from_real(PyTypeObject *type, PyObject *r, PyObject *exact) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
DecimalType_from_obj(PyTypeObject *type, PyObject *obj, long adjust_to_prec) {

    if (obj == Py_None) {
        DECIMAL_ALLOC(type);
        self->fpdec.dec_prec = Py_MAX(0, adjust_to_prec);        \
        return (PyObject *)self;
    }

    if (PyObject_IsInstance(obj, (PyObject *)DecimalType)) {
        if (type == DecimalType && (adjust_to_prec == -1 ||
                                    ((DecimalObject *)obj)->fpdec.dec_prec ==
                                    adjust_to_prec)) {
            Py_IncRef(obj);
            return obj;
        }
        return DecimalType_from_fpdec(type, &((DecimalObject *)obj)->fpdec,
                                      adjust_to_prec);
    }

    // unable to create Decimal
    return PyErr_Format(PyExc_TypeError, "Can't convert %r to Decimal.", obj);
}

static PyObject *
DecimalType_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    static char *kw_names[] = {"value", "precision", NULL};
    PyObject *value = Py_None;
    PyObject *precision = Py_None;
    long adjust_to_prec;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OO", kw_names,
                                     &value, &precision))
        return NULL;

    if (precision == Py_None) {
        return (PyObject *)DecimalType_from_obj(type, value, -1);
    }
    else {
        if (!PyObject_IsInstance(precision, Integral))
            return type_error_ptr("Precision must be of type 'numbers"
                                  ".Integral'.");
        adjust_to_prec = PyLong_AsLong(precision);
        if (adjust_to_prec < 0)
            return value_error_ptr("Precision must be >= 0.");
        if (adjust_to_prec > FPDEC_MAX_DEC_PREC)
            return value_error_ptr("Precision limit exceeded.");

        return (PyObject *)DecimalType_from_obj(type, value, adjust_to_prec);
    }
}

// properties

static PyObject *
Decimal_precision_get(DecimalObject *self) {
    long prec = FPDEC_DEC_PREC(&self->fpdec);
    return PyLong_FromLong(prec);
}

static PyObject *
Decimal_magnitude_get(DecimalObject *self) {
    long magn = fpdec_magnitude(&self->fpdec);
    if (magn == -1 && errno != 0) {
        PyErr_SetString(PyExc_OverflowError, "Result would be '-Infinity'.");
        return NULL;
    }
    return PyLong_FromLong(magn);
}

static PyObject *
Decimal_numerator_get(DecimalObject *self) {
    if (self->numerator == NULL)
        fpdec_as_integer_ratio(&self->numerator, &self->denominator,
                               &self->fpdec);
    return self->numerator;
}

static PyObject *
Decimal_denominator_get(DecimalObject *self) {
    if (self->denominator == NULL)
        fpdec_as_integer_ratio(&self->numerator, &self->denominator,
                               &self->fpdec);
    return self->denominator;
}

static PyObject *
Decimal_real_get(DecimalObject *self) {
    return (PyObject *)self;
}

static PyObject *
Decimal_imag_get(DecimalObject *self) {
    return PyZERO;
}

// converting methods

static PyObject *
Decimal_adjusted(DecimalObject *self, PyObject *precision,
                 PyObject *rounding) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_quantize(DecimalObject *self, PyObject *quant, PyObject *rounding) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_as_tuple(DecimalObject *self) {
    fpdec_t *fpdec = &self->fpdec;
    PyObject *sign = NULL;
    PyObject *coeff = NULL;
    PyObject *adj_coeff = NULL;
    PyObject *dec_prec = NULL;
    PyObject *res = NULL;
    long exp;
    long dec_shift;

    sign = FPDEC_SIGN(fpdec) == FPDEC_SIGN_NEG ? PyONE : PyZERO;
    exp = fpdec_dec_coeff_exp(&coeff, fpdec);
    if (coeff == NULL)
        return NULL;
    dec_shift = exp + fpdec->dec_prec;
    if (dec_shift < 0) {
        PyObject *divisor = PyLong_FromLong(-dec_shift);
        if (divisor == NULL) {
            Py_DECREF(coeff);
            return NULL;
        }
        adj_coeff = PyNumber_FloorDivide(coeff, divisor);
        if (adj_coeff == NULL) {
            Py_DECREF(coeff);
            Py_DECREF(divisor);
            return NULL;
        }
        coeff = adj_coeff;      // stealing reference
    }
    else if (dec_shift > 0) {
        PyObject *factor = PyLong_FromLong(dec_shift);
        if (factor == NULL) {
            Py_DECREF(coeff);
            return NULL;
        }
        adj_coeff = PyNumber_Multiply(coeff, factor);
        if (adj_coeff == NULL) {
            Py_DECREF(coeff);
            Py_DECREF(factor);
            return NULL;
        }
        coeff = adj_coeff;      // stealing reference
    }
    dec_prec = PyLong_FromLong(-fpdec->dec_prec);
    if (dec_prec != NULL) {
        res = PyTuple_Pack(3, sign, coeff, dec_prec);
        Py_DECREF(dec_prec);
    }
    Py_DECREF(coeff);
    return res;
}

static PyObject *
Decimal_as_fraction(DecimalObject *self) {
    return PyObject_CallFunctionObjArgs(Fraction, Decimal_numerator_get(self),
                                        Decimal_denominator_get(self), NULL);
}

static PyObject *
Decimal_as_integer_ratio(DecimalObject *self) {
    return PyTuple_Pack(2, Decimal_numerator_get(self),
                        Decimal_denominator_get(self));
}

static PyObject *
Decimal_floor(DecimalObject *self) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_ceil(DecimalObject *self) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_round(DecimalObject *self, PyObject *precision) {
    Py_RETURN_NOTIMPLEMENTED;
}

// pickle helper

static PyObject *
Decimal_reduce(DecimalObject *self) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_setstate(DecimalObject *self, PyObject *state) {
    Py_RETURN_NOTIMPLEMENTED;
}

// string representation

static PyObject *
Decimal_repr(DecimalObject *self) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_str(DecimalObject *self) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_format(DecimalObject *self, PyObject *fmt_spec) {
    Py_RETURN_NOTIMPLEMENTED;
}

// special methods

static Py_hash_t
Decimal_hash(DecimalObject *self) {
    if (self->hash == -1) {
        if (PyObject_RichCompareBool(Decimal_denominator_get(self), PyONE,
                                     Py_EQ))
            self->hash = PyObject_Hash(Decimal_numerator_get(self));
        else
            self->hash = PyObject_Hash(Decimal_as_fraction(self));
    }
    return self->hash;
}

static PyObject *
Decimal_copy(DecimalObject *self) {
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *
Decimal_deepcopy(DecimalObject *self, PyObject *memo) {
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *
Decimal_richcompare(PyObject *self, PyObject *other, int op) {
    int r;

    assert(Decimal_Check(self));

    if (Decimal_Check(other)) {
        r = fpdec_compare(&((DecimalObject *)self)->fpdec,
                          &((DecimalObject *)other)->fpdec,
                          false);
    }
    else {
        // TODO
        Py_RETURN_NOTIMPLEMENTED;
    }

    switch (op) {
        case Py_EQ:
            r = (r == 0);
            break;
        case Py_NE:
            r = (r != 0);
            break;
        case Py_LE:
            r = (r <= 0);
            break;
        case Py_GE:
            r = (r >= 0);
            break;
        case Py_LT:
            r = (r < 0);
            break;
        case Py_GT:
            r = (r > 0);
            break;
        default:
            Py_UNREACHABLE();
    }

    return PyBool_FromLong(r);
}

// unary number methods

static PyObject *
Decimal_neg(PyObject *x) {
    DecimalObject *dec;
    fpdec_t *x_fpdec = &((DecimalObject *)x)->fpdec;
    error_t rc;

    if (FPDEC_EQ_ZERO(x_fpdec))
        return x;

    dec = DecimalType_alloc(Py_TYPE(x));
    rc = fpdec_copy(&dec->fpdec, x_fpdec);
    CHECK_FPDEC_ERROR(rc);
    dec->fpdec.sign *= -1;
    return (PyObject *)dec;
}

static PyObject *
Decimal_pos(PyObject *x) {
    return x;
}

static PyObject *
Decimal_abs(PyObject *x) {
    DecimalObject *dec;
    fpdec_t *x_fpdec = &((DecimalObject *)x)->fpdec;
    error_t rc;

    if (FPDEC_SIGN(x_fpdec) != FPDEC_SIGN_NEG)
        return x;

    dec = DecimalType_alloc(Py_TYPE(x));
    rc = fpdec_copy(&dec->fpdec, x_fpdec);
    CHECK_FPDEC_ERROR(rc);
    dec->fpdec.sign = 1;
    return (PyObject *)dec;
}

static PyObject *
Decimal_int(PyObject *x) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_float(PyObject *x) {
    Py_RETURN_NOTIMPLEMENTED;
}

// binary number methods

static PyObject *
Decimal_add(PyObject *x, PyObject *y) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_sub(PyObject *x, PyObject *y) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_mul(PyObject *x, PyObject *y) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_mod(PyObject *x, PyObject *y) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_divmod(PyObject *x, PyObject *y) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_floordiv(PyObject *x, PyObject *y) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_truediv(PyObject *x, PyObject *y) {
    Py_RETURN_NOTIMPLEMENTED;
}

// ternary number methods

static PyObject *
Decimal_pow(PyObject *x, PyObject *y, PyObject *mod) {
    Py_RETURN_NOTIMPLEMENTED;
}

// Decimal type spec

static PyGetSetDef Decimal_properties[] = {
    {"precision", (getter)Decimal_precision_get, 0,
     "Return precision of `self`.", 0},
    {"magnitude", (getter)Decimal_magnitude_get, 0,
     "Return magnitude of `self` in terms of power to 10.\n\n"
     "I.e. the largest integer exp so that 10 ** exp <= self.\n\n", 0},
    {"numerator", (getter)Decimal_numerator_get, 0,
     "Return the normalized numerator of `self`.\n\n"
     "I. e. the numerator from the pair of integers with the smallest\n"
     "positive denominator, whose ratio is equal to `self`.\n\n", 0},
    {"denominator", (getter)Decimal_denominator_get, 0,
     "Return the normalized denominator of 'self'.\n\n"
     "I. e. the smallest positive denominator from the pairs of integers,\n"
     "whose ratio is equal to `self`.\n\n", 0},
    {"real", (getter)Decimal_real_get, 0,
     "Return real part of `self`.\n\n"
     "Returns `self` (Real numbers are their real component).\n\n", 0},
    {"imag", (getter)Decimal_imag_get, 0,
     "Return imaginary part of `self`.\n\n"
     "Returns 0 (Real numbers have no imaginary component).\n\n", 0},
    {0, 0, 0, 0, 0}
};

static PyMethodDef Decimal_methods[] = {
    /* class methods */
    {"from_float",
     (PyCFunction)DecimalType_from_float,
     METH_O | METH_CLASS,
     DecimalType_from_float_doc},
    {"from_decimal",
     (PyCFunction)DecimalType_from_decimal,
     METH_O | METH_CLASS,
     DecimalType_from_decimal_doc},
    {"from_real",
     (PyCFunction)(void *)(PyCFunctionWithKeywords)DecimalType_from_real,
     METH_CLASS | METH_VARARGS | METH_KEYWORDS,
     DecimalType_from_real_doc},
    // other methods
    {"adjusted",
     (PyCFunction)(void *)(PyCFunctionWithKeywords)Decimal_adjusted,
     METH_VARARGS | METH_KEYWORDS,
     Decimal_adjusted_doc},
    {"quantize",
     (PyCFunction)(void *)(PyCFunctionWithKeywords)Decimal_quantize,
     METH_VARARGS | METH_KEYWORDS,
     Decimal_quantize_doc},
    {"as_tuple",
     (PyCFunction)Decimal_as_tuple,
     METH_NOARGS,
     Decimal_as_tuple_doc},
    {"as_fraction",
     (PyCFunction)Decimal_as_fraction,
     METH_NOARGS,
     Decimal_as_fraction_doc},
    {"as_integer_ratio",
     (PyCFunction)Decimal_as_integer_ratio,
     METH_NOARGS,
     Decimal_as_integer_ratio_doc},
    // special methods
    {"__copy__",
     (PyCFunction)Decimal_copy,
     METH_NOARGS,
     Decimal_copy_doc},
    {"__deepcopy__",
     (PyCFunction)Decimal_deepcopy,
     METH_O,
     Decimal_copy_doc},
    {"__reduce__",
     (PyCFunction)Decimal_reduce,
     METH_NOARGS,
     Decimal_reduce_doc},
    {"__setstate__",
     (PyCFunction)Decimal_setstate,
     METH_O,
     Decimal_setstate_doc},
    {"__format__",
     (PyCFunction)Decimal_format,
     METH_O,
     Decimal_format_doc},
    {"__floor__",
     (PyCFunction)Decimal_floor,
     METH_NOARGS,
     Decimal_floor_doc},
    {"__ceil__",
     (PyCFunction)Decimal_ceil,
     METH_NOARGS,
     Decimal_ceil_doc},
    {"__round__",
     (PyCFunction)(void *)(PyCFunctionWithKeywords)Decimal_round,
     METH_VARARGS | METH_KEYWORDS,
     Decimal_round_doc},
    {0, 0, 0, 0}
};

static PyType_Slot decimal_type_slots[] = {
    //{Py_tp_doc, DecimalType_doc},
    {Py_tp_new, DecimalType_new},
    {Py_tp_dealloc, Decimal_dealloc},
    {Py_tp_richcompare, Decimal_richcompare},
    {Py_tp_hash, Decimal_hash},
    //{Py_tp_str, Decimal_str},
    //{Py_tp_repr, Decimal_repr},
    /* properties */
    {Py_tp_getset, Decimal_properties},
    /* number methods */
    {Py_nb_add, Decimal_add},
    {Py_nb_subtract, Decimal_sub},
    {Py_nb_multiply, Decimal_mul},
    {Py_nb_remainder, Decimal_mod},
    {Py_nb_divmod, Decimal_divmod},
    {Py_nb_power, Decimal_pow},
    {Py_nb_negative, Decimal_neg},
    {Py_nb_positive, Decimal_pos},
    {Py_nb_absolute, Decimal_abs},
    {Py_nb_int, Decimal_int},
    {Py_nb_float, Decimal_float},
    {Py_nb_floor_divide, Decimal_floordiv},
    {Py_nb_true_divide, Decimal_truediv},
    /* other methods */
    {Py_tp_methods, Decimal_methods},
    {0, NULL}
};

static PyType_Spec DecimalType_spec = {
    "Decimal",                              /* name */
    sizeof(DecimalObject),                  /* basicsize */
    0,                                      /* itemsize */
    0,                                      /* flags */
    decimal_type_slots                      /* slots */
};

// *** Helper functions ***

static PyObject *
digits_as_int(fpdec_digit_array_t *digit_array) {
    PyObject *res;
    PyObject *mult;
    PyObject *digit;
    ssize_t idx = digit_array->n_signif - 1;

    assert(digit_array->n_signif > 0);

    ASSIGN_AND_CHECK_NULL(res,
                          PyLong_FromUnsignedLong(digit_array->digits[idx]));
    while (--idx >= 0) {
        ASSIGN_AND_CHECK_NULL(digit,
                              PyLong_FromUnsignedLong(
                                  digit_array->digits[idx]));
        ASSIGN_AND_CHECK_NULL(mult, PyNumber_Multiply(res, PyRADIX));
        Py_DECREF(res);
        ASSIGN_AND_CHECK_NULL(res, PyNumber_Add(mult, digit));
        Py_DECREF(digit);
        Py_DECREF(mult);
    }
    return res;

ERROR:
    Py_XDECREF(res);
    Py_XDECREF(mult);
    Py_XDECREF(digit);
    return NULL;
}

static long
fpdec_dec_coeff_exp(PyObject **coeff, const fpdec_t *fpdec) {
    assert(*coeff == NULL);

    if (FPDEC_EQ_ZERO(fpdec)) {
        *coeff = PyZERO;
        Py_INCREF(*coeff);
        return 0;
    }
    else if (FPDEC_IS_DYN_ALLOC(fpdec)) {
        *coeff = digits_as_int(fpdec->digit_array);
        if (*coeff == NULL)
            return 0;
        return FPDEC_DYN_EXP(fpdec) * DEC_DIGITS_PER_DIGIT;
    }
    else {
        if (fpdec->hi == 0)
            *coeff = PyLong_FromUnsignedLong(fpdec->lo);
        else {
            PyObject *hi = PyLong_FromUnsignedLong(fpdec->hi);
            if (hi == NULL)
                return 0;
            PyObject *lo = PyLong_FromUnsignedLong(fpdec->lo);
            if (lo == NULL) {
                Py_DECREF(hi);
                return 0;
            }
            PyObject *sh = PyNumber_Lshift(hi, PyLong_FromSize_t(64));
            if (sh == NULL) {
                Py_DECREF(hi);
                Py_DECREF(lo);
                return 0;
            }
            *coeff = PyNumber_Add(sh, lo);
        }
        if (*coeff == NULL)
            return 0;
        return -fpdec->dec_prec;
    }
}

static void
fpdec_as_integer_ratio(PyObject **numerator, PyObject **denominator,
                       const fpdec_t *fpdec) {
    PyObject *coeff = NULL;
    PyObject *neg_coeff = NULL;
    PyObject *py_exp = NULL;
    PyObject *ten_pow_exp = NULL;
    PyObject *gcd = NULL;
    long exp;

    assert(*numerator == NULL);
    assert(*denominator == NULL);

    exp = fpdec_dec_coeff_exp(&coeff, fpdec);
    if (coeff == NULL)
        return;

    if (FPDEC_SIGN(fpdec) == FPDEC_SIGN_NEG) {
        ASSIGN_AND_CHECK_NULL(neg_coeff, PyNumber_Negative(coeff));
        Py_DECREF(coeff);
        coeff = neg_coeff;      // stealing reference
    }

    if (exp == 0) {
        // *numerator = coeff, *denominator = 1
        *numerator = coeff;      // stealing reference
        *denominator = PyONE;
        Py_INCREF(*denominator);
        return;
    }
    if (exp > 0) {
        // *numerator = coeff * 10 ^ exp, *denominator = 1
        ASSIGN_AND_CHECK_NULL(py_exp, PyLong_FromLong(exp));
        ASSIGN_AND_CHECK_NULL(ten_pow_exp,
                              PyNumber_Power(PyTEN, py_exp, Py_None));
        ASSIGN_AND_CHECK_NULL(*numerator,
                              PyNumber_Multiply(coeff, ten_pow_exp));
        Py_DECREF(py_exp);
        Py_DECREF(ten_pow_exp);
        *denominator = PyONE;
        Py_INCREF(*denominator);
    }
    else {
        // *numerator = coeff, *denominator = 10 ^ -exp, but they may need
        // to be normalized!
        ASSIGN_AND_CHECK_NULL(py_exp, PyLong_FromLong(-exp));
        ASSIGN_AND_CHECK_NULL(ten_pow_exp,
                              PyNumber_Power(PyTEN, py_exp, Py_None));
        ASSIGN_AND_CHECK_NULL(gcd, PyNumber_gcd(coeff, ten_pow_exp));
        ASSIGN_AND_CHECK_NULL(*numerator, PyNumber_FloorDivide(coeff, gcd));
        *denominator = PyNumber_FloorDivide(ten_pow_exp, gcd);
        if (*denominator == NULL) {
            Py_DECREF(*numerator);
            *numerator = NULL;
        }
    }
ERROR:
    // clean-up (not only in case of an error!)
    Py_XDECREF(coeff);
    Py_XDECREF(py_exp);
    Py_XDECREF(ten_pow_exp);
    Py_XDECREF(gcd);
}

// *** _cdecimalfp module ***

static int
PyModule_AddType(PyObject *module, const char *name, PyObject *type) {
    Py_INCREF(type);
    if (PyModule_AddObject(module, name, type) < 0) {
        Py_DECREF(type);
        return -1;
    }
    return 0;
}

PyDoc_STRVAR(cdecimalfp_doc, "Decimal fixed-point arithmetic.");

static int
cdecimalfp_exec(PyObject *module) {
    /* Import from numbers */
    PyObject *numbers = NULL;
    ASSIGN_AND_CHECK_NULL(numbers, PyImport_ImportModule("numbers"));
    ASSIGN_AND_CHECK_NULL(Number, PyObject_GetAttrString(numbers, "Number"));
    ASSIGN_AND_CHECK_NULL(Complex, PyObject_GetAttrString(numbers,
                                                          "Complex"));
    ASSIGN_AND_CHECK_NULL(Real, PyObject_GetAttrString(numbers, "Real"));
    ASSIGN_AND_CHECK_NULL(Rational, PyObject_GetAttrString(numbers,
                                                           "Rational"));
    ASSIGN_AND_CHECK_NULL(Integral, PyObject_GetAttrString(numbers,
                                                           "Integral"));
    Py_CLEAR(numbers);
    /* Import from fractions */
    PyObject *fractions = NULL;
    ASSIGN_AND_CHECK_NULL(fractions, PyImport_ImportModule("fractions"));
    ASSIGN_AND_CHECK_NULL(Fraction, PyObject_GetAttrString(fractions,
                                                           "Fraction"));
    Py_CLEAR(fractions);
    /* Import from decimal */
    PyObject *decimal = NULL;
    ASSIGN_AND_CHECK_NULL(decimal, PyImport_ImportModule("decimal"));
    ASSIGN_AND_CHECK_NULL(StdLibDecimal, PyObject_GetAttrString(decimal,
                                                                "Decimal"));
    Py_CLEAR(decimal);
    /* Import from math */
    PyObject *math = NULL;
    ASSIGN_AND_CHECK_NULL(math, PyImport_ImportModule("math"));
    ASSIGN_AND_CHECK_NULL(PyNumber_gcd,
                          (PyCFunction)PyObject_GetAttrString(math, "gcd"));
    Py_CLEAR(math);

    /* Init libfpdec memory handlers */
    fpdec_mem_alloc = PyMem_Calloc;
    fpdec_mem_free = PyMem_Free;

    /* Init global constants */
    PyZERO = PyLong_FromLong(0L);
    PyONE = PyLong_FromLong(1L);
    PyTEN = PyLong_FromLong(10L);
    PyRADIX = PyLong_FromUnsignedLong(RADIX);

    /* Init global vars */
    PyModule_AddIntConstant(module, "MAX_DEC_PRECISION", FPDEC_MAX_DEC_PREC);

    /* Add types */
    ASSIGN_AND_CHECK_NULL(DecimalType,
                          (PyTypeObject *)PyType_FromSpec(&DecimalType_spec));
    if (PyModule_AddType(module, "Decimal", (PyObject *)DecimalType) < 0)
        goto ERROR;

    return 0;
ERROR:
    Py_CLEAR(Number);
    Py_CLEAR(Complex);
    Py_CLEAR(Real);
    Py_CLEAR(Rational);
    Py_CLEAR(Integral);
    Py_CLEAR(Fraction);
    Py_CLEAR(StdLibDecimal);
    Py_CLEAR(DecimalType);
    return -1;
}

static PyModuleDef_Slot cdecimalfp_slots[] = {
    {Py_mod_exec, cdecimalfp_exec},
    {0, NULL}
};

static struct PyModuleDef cdecimalfp_module = {
    PyModuleDef_HEAD_INIT,              /* m_base */
    "_cdecimalfp",                      /* m_name */
    cdecimalfp_doc,                     /* m_doc */
    0,                                  /* m_size */
    NULL,                               /* m_methods */
    cdecimalfp_slots,                   /* m_slots */
    NULL,                               /* m_traverse */
    NULL,                               /* m_clear */
    NULL                                /* m_free */
};

PyMODINIT_FUNC
PyInit__cdecimalfp(void) {
    return PyModuleDef_Init(&cdecimalfp_module);
}

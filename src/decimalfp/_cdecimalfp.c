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
#include <math.h>
#include "_cdecimalfp_docstrings.h"
#include "libfpdec/fpdec.h"
#include "libfpdec/fpdec_struct.h"
#include "libfpdec/digit_array_struct.h"
#include "libfpdec/basemath.h"

#define ASSIGN_AND_CHECK_NULL(result, expr) \
    do { result = (expr); if (result == NULL) goto ERROR; } while (0)

#define ASSIGN_AND_CHECK_OK(result, expr) \
    do { result = (expr); if (result != NULL) goto CLEAN_UP; } while (0)

#define CHECK_TYPE(obj, type) \
    if (!PyObject_TypeCheck(obj, (PyTypeObject *)type)) goto ERROR

#define CHECK_ERROR(rc) if ((rc) != 0) goto ERROR

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

static PyObject *PyNumber_gcd = NULL;

// PyLong methods

static PyObject *PyLong_bit_length = NULL;

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
    do {                                                                    \
    switch (rc) {                                                           \
        case FPDEC_OK:                                                      \
            break;                                                          \
        case ENOMEM:                                                        \
            PyErr_SetNone(PyExc_MemoryError);                               \
            goto ERROR;                                                     \
        case FPDEC_PREC_LIMIT_EXCEEDED:                                     \
            PyErr_SetString(PyExc_ValueError, "Precision limit exceeded."); \
            goto ERROR;                                                     \
        case FPDEC_EXP_LIMIT_EXCEEDED:                                      \
        case FPDEC_N_DIGITS_LIMIT_EXCEEDED:                                 \
            PyErr_SetString(PyExc_OverflowError,                            \
                            "Internal limit exceeded.");                    \
            goto ERROR;                                                     \
        case FPDEC_INVALID_DECIMAL_LITERAL:                                 \
            PyErr_SetString(PyExc_ValueError, "Invalid Decimal literal.");  \
            goto ERROR;                                                     \
        case FPDEC_DIVIDE_BY_ZERO:                                          \
            PyErr_SetString(PyExc_ZeroDivisionError, "Division by zero.");  \
            goto ERROR;                                                     \
        default:                                                            \
            PyErr_SetString(PyExc_SystemError, "Unknown error code.");      \
            goto ERROR;                                                     \
    }} while (0)


// *** Python number constants ***

static PyObject *PyZERO = NULL;
static PyObject *PyONE = NULL;
static PyObject *PyTEN = NULL;
static PyObject *PyRADIX = NULL;
static PyObject *Py2pow64 = NULL;
static PyObject *MAX_DEC_PRECISION = NULL;

// *** Helper prototypes ***

static inline PyObject *
PyLong_from_u128(uint128_t *ui);

static PyObject *
PyLong_from_digits(const fpdec_digit_t *digits,
                   fpdec_n_digits_t n_digits,
                   uint8_t n_dec_adjust);

static PyObject *
PyLong_from_fpdec(const fpdec_t *fpdec);

static long
fpdec_dec_coeff_exp(PyObject **coeff, const fpdec_t *fpdec);

static void
fpdec_as_integer_ratio(PyObject **numerator, PyObject **denominator,
                       const fpdec_t *fpdec);

static inline size_t
n_digits_needed(size_t n, uint64_t fb, uint64_t tb);

static error_t
fpdec_from_pylong(fpdec_t *fpdec, PyObject *val);

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
    freefunc tp_free = (freefunc)PyType_GetSlot(Py_TYPE(self), Py_tp_free);
    fpdec_reset_to_zero(&self->fpdec, 0);
    Py_CLEAR(self->numerator);
    Py_CLEAR(self->denominator);
    tp_free(self);
}

#define DECIMAL_ALLOC(type, name) \
    do {name = DecimalType_alloc(type); \
        if (name == NULL) \
            return NULL;  \
        } while (0)

#define DECIMAL_ALLOC_SELF(type) \
    DecimalObject *self; \
    DECIMAL_ALLOC(type, self)

static PyObject *
DecimalType_from_fpdec(PyTypeObject *type, fpdec_t *fpdec,
                       long adjust_to_prec) {
    error_t rc;
    DECIMAL_ALLOC_SELF(type);

    if (adjust_to_prec == -1 || adjust_to_prec == FPDEC_DEC_PREC(fpdec))
        rc = fpdec_copy(&self->fpdec, fpdec);
    else
        rc = fpdec_adjusted(&self->fpdec, fpdec, adjust_to_prec,
                            FPDEC_ROUND_DEFAULT);
    CHECK_FPDEC_ERROR(rc);
    return (PyObject *)self;

ERROR:
    Decimal_dealloc(self);
    return NULL;
}

static PyObject *
DecimalType_from_decimal(PyTypeObject *type, PyObject *val,
                         long adjust_to_prec) {
    if (type == DecimalType && (adjust_to_prec == -1 ||
                                ((DecimalObject *)val)->fpdec.dec_prec ==
                                adjust_to_prec)) {
        // obj is a direct instance of DecimalType, a direct instance of
        // DecinalType is wanted and there's no need to adjust the result,
        // so just return the given instance (ref count increased)
        Py_INCREF(val);
        return val;
    }
    return DecimalType_from_fpdec(type, &((DecimalObject *)val)->fpdec,
                                  adjust_to_prec);
}

static PyObject *
DecimalType_from_str(PyTypeObject *type, PyObject *val, long adjust_to_prec) {
    wchar_t *buf;
    error_t rc;
    fpdec_t *fpdec;
    DECIMAL_ALLOC_SELF(type);

    fpdec = &self->fpdec;
    ASSIGN_AND_CHECK_NULL(buf, PyUnicode_AsWideCharString(val, NULL));
    rc = fpdec_from_unicode_literal(fpdec, buf);
    PyMem_Free(buf);
    CHECK_FPDEC_ERROR(rc);

    if (adjust_to_prec != -1 && adjust_to_prec != FPDEC_DEC_PREC(fpdec)) {
        rc = fpdec_adjust(fpdec, adjust_to_prec, FPDEC_ROUND_DEFAULT);
        CHECK_FPDEC_ERROR(rc);
    }
    return (PyObject *)self;

ERROR:
    Decimal_dealloc(self);
    return NULL;
}

static PyObject *
DecimalType_from_pylong(PyTypeObject *type, PyObject *val,
                        long adjust_to_prec) {
    error_t rc;
    fpdec_t *fpdec;
    DECIMAL_ALLOC_SELF(type);

    fpdec = &self->fpdec;
    rc = fpdec_from_pylong(fpdec, val);
    CHECK_FPDEC_ERROR(rc);

    if (adjust_to_prec != -1 && adjust_to_prec != FPDEC_DEC_PREC(fpdec)) {
        rc = fpdec_adjust(fpdec, adjust_to_prec, FPDEC_ROUND_DEFAULT);
        CHECK_FPDEC_ERROR(rc);
    }
    Py_INCREF(val);
    self->numerator = val;
    Py_INCREF(PyONE);
    self->denominator = PyONE;
    return (PyObject *)self;

ERROR:
    assert(PyErr_Occurred());
    Decimal_dealloc(self);
    return NULL;
}

static PyObject *
DecimalType_from_integral(PyTypeObject *type, PyObject *val,
                          long adjust_to_prec) {
    PyObject *d;
    PyObject *i = PyNumber_Long(val);
    if (i == NULL)
        return NULL;
    d = DecimalType_from_pylong(type, i, adjust_to_prec);
    Py_DECREF(i);
    return d;
}

static error_t
fpdec_from_num_den(fpdec_t *fpdec, PyObject *numerator,
                   PyObject *denominator, long adjust_to_prec) {
    error_t rc;
    fpdec_t num = FPDEC_ZERO;
    fpdec_t den = FPDEC_ZERO;

    rc = fpdec_from_pylong(&num, numerator);
    CHECK_FPDEC_ERROR(rc);
    rc = fpdec_from_pylong(&den, denominator);
    CHECK_FPDEC_ERROR(rc);
    rc = fpdec_div(fpdec, &num, &den, (int)adjust_to_prec,
                   FPDEC_ROUND_DEFAULT);

ERROR:
    fpdec_reset_to_zero(&num, 0);
    fpdec_reset_to_zero(&den, 0);
    return rc;
}

static PyObject *
DecimalType_from_num_den(PyTypeObject *type, PyObject *numerator,
                         PyObject *denominator, long adjust_to_prec) {
    DECIMAL_ALLOC_SELF(type);
    error_t rc;

    rc = fpdec_from_num_den(&self->fpdec, numerator, denominator,
                            adjust_to_prec);
    CHECK_FPDEC_ERROR(rc);
    if (adjust_to_prec == -1) {
        // The quotient has not been adjusted, so we can safely cache
        // numerator and denominator
        Py_INCREF(numerator);
        self->numerator = numerator;
        Py_INCREF(denominator);
        self->denominator = denominator;
    }
    return (PyObject *)self;

ERROR:
    Decimal_dealloc(self);
    return NULL;
}

static PyObject *
DecimalType_from_rational(PyTypeObject *type, PyObject *val,
                          long adjust_to_prec) {
    PyObject *numerator = NULL;
    PyObject *denominator = NULL;
    PyObject *dec = NULL;

    ASSIGN_AND_CHECK_NULL(numerator,
                          PyObject_GetAttrString(val, "numerator"));
    ASSIGN_AND_CHECK_NULL(denominator,
                          PyObject_GetAttrString(val, "denominator"));
    dec = DecimalType_from_num_den(type, numerator, denominator,
                                   adjust_to_prec);
    Py_DECREF(numerator);
    Py_DECREF(denominator);
    return dec;

ERROR:
    Py_XDECREF(numerator);
    Py_XDECREF(denominator);
    return NULL;
}

static PyObject *
DecimalType_from_float(PyTypeObject *type, PyObject *val,
                       long adjust_to_prec) {
    PyObject *dec = NULL;
    PyObject *ratio = NULL;
    PyObject *numerator = NULL;
    PyObject *denominator = NULL;

    ASSIGN_AND_CHECK_NULL(ratio,
                          PyObject_CallMethod(val, "as_integer_ratio", NULL));
    ASSIGN_AND_CHECK_NULL(numerator, PySequence_GetItem(ratio, 0));
    ASSIGN_AND_CHECK_NULL(denominator, PySequence_GetItem(ratio, 1));
    ASSIGN_AND_CHECK_NULL(dec, DecimalType_from_num_den(type, numerator,
                                                        denominator,
                                                        adjust_to_prec));
    goto CLEAN_UP;

ERROR:
    {
        PyObject *err = PyErr_Occurred();
        assert(err);
        if (err == PyExc_ValueError || err == PyExc_OverflowError ||
            err == PyExc_AttributeError) {
            PyErr_Clear();
            PyErr_Format(PyExc_ValueError, "Can't convert %R to Decimal.",
                         val);
        }
    }

CLEAN_UP:
    Py_XDECREF(ratio);
    Py_XDECREF(numerator);
    Py_XDECREF(denominator);
    return dec;
}

static PyObject *
DecimalType_from_stdlib_decimal(PyTypeObject *type, PyObject *val,
                                long adjust_to_prec) {
    PyObject *dec = NULL;
    PyObject *is_finite = NULL;
    PyObject *tup = NULL;
    PyObject *exp = NULL;
    long prec = adjust_to_prec;

    ASSIGN_AND_CHECK_NULL(is_finite,
                          PyObject_CallMethod(val, "is_finite", NULL));
    if (!PyObject_IsTrue(is_finite)) {
        PyErr_Format(PyExc_ValueError, "Can't convert %R to Decimal.", val);
        goto ERROR;
    }

    if (adjust_to_prec == -1) {
        // get number of fractional digits from given value
        ASSIGN_AND_CHECK_NULL(tup,
                              PyObject_CallMethod(val, "as_tuple", NULL));
        ASSIGN_AND_CHECK_NULL(exp, PySequence_GetItem(tup, 2));
        prec = MAX(0L, -PyLong_AsLong(exp));
        if (PyErr_Occurred())
            goto ERROR;
    }
    ASSIGN_AND_CHECK_NULL(dec, DecimalType_from_float(type, val, prec));
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(is_finite);
    Py_XDECREF(tup);
    Py_XDECREF(exp);
    return dec;
}

static PyObject *
DecimalType_from_float_or_int(PyTypeObject *type, PyObject *val) {
    if (PyFloat_Check(val))
        return DecimalType_from_float(type, val, -1);
    if (PyLong_Check(val))
        return DecimalType_from_pylong(type, val, -1);
    return PyErr_Format(PyExc_TypeError, "%R is not a float or int.", val);
}

static PyObject *
DecimalType_from_decimal_or_int(PyTypeObject *type, PyObject *val) {
    if (Decimal_Check(val))
        return DecimalType_from_decimal(type, val, -1);
    if (PyObject_IsInstance(val, StdLibDecimal))
        return DecimalType_from_stdlib_decimal(type, val, -1);
    if (PyLong_Check(val))
        return DecimalType_from_pylong(type, val, -1);
    if (PyObject_IsInstance(val, Integral))
        return DecimalType_from_integral(type, val, -1);
    return PyErr_Format(PyExc_TypeError, "%R is not a Decimal.", val);
}

static PyObject *
DecimalType_from_obj(PyTypeObject *type, PyObject *obj, long adjust_to_prec) {

    if (obj == Py_None) {
        DECIMAL_ALLOC_SELF(type);
        self->fpdec.dec_prec = Py_MAX(0, adjust_to_prec);
        Py_INCREF(PyZERO);
        self->numerator = PyZERO;
        Py_INCREF(PyONE);
        self->denominator = PyONE;
        return (PyObject *)self;
    }

    // Decimal
    if (Decimal_Check(obj))
        return DecimalType_from_decimal(type, obj, adjust_to_prec);

    // String
    if (PyUnicode_Check(obj))
        return DecimalType_from_str(type, obj, adjust_to_prec);

    // Python <int>
    if (PyLong_Check(obj))
        return DecimalType_from_pylong(type, obj, adjust_to_prec);

    // Integral
    if (PyObject_IsInstance(obj, Integral))
        return DecimalType_from_integral(type, obj, adjust_to_prec);

    // Rational
    if (PyObject_IsInstance(obj, Rational))
        return DecimalType_from_rational(type, obj, adjust_to_prec);

    // Python standard lib Decimal
    if (PyObject_IsInstance(obj, StdLibDecimal))
        return DecimalType_from_stdlib_decimal(type, obj, adjust_to_prec);

    // Python <float>, Real
    if (PyFloat_Check(obj) || PyObject_IsInstance(obj, Real))
        return DecimalType_from_float(type, obj, adjust_to_prec);

    // If there's a float or int equivalent to value, use it
    {
        PyObject *num = PyNumber_Float(obj);
        if (num != NULL && PyObject_RichCompareBool(num, obj, Py_EQ) == 1) {
            PyObject *dec = DecimalType_from_float(type, num, adjust_to_prec);
            Py_DECREF(num);
            return dec;
        }
        num = PyNumber_Long(obj);
        if (num != NULL && PyObject_RichCompareBool(num, obj, Py_EQ) == 1) {
            PyObject *dec = DecimalType_from_pylong(type, num,
                                                    adjust_to_prec);
            Py_DECREF(num);
            return dec;
        }
    }

    // unable to create Decimal
    return PyErr_Format(PyExc_TypeError, "Can't convert %R to Decimal.", obj);
}

static PyObject *
DecimalType_from_real(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    static char *kw_names[] = {"r", "exact", NULL};
    PyObject *r = Py_None;
    PyObject *exact = Py_True;
    PyObject *dec;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|O", kw_names, &r,
                                     &exact))
        return NULL;

    if (!PyObject_IsInstance(r, Real))
        return PyErr_Format(PyExc_TypeError, "%R is not a Real.", r);

    dec = DecimalType_from_obj(type, r, -1);
    if (dec == NULL && PyErr_ExceptionMatches(PyExc_ValueError)) {
        if (!PyObject_IsTrue(exact)) {
            PyErr_Clear();
            dec = DecimalType_from_obj(type, r, FPDEC_MAX_DEC_PREC);
        }
    }
    return dec;
}

static PyObject *
DecimalType_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    static char *kw_names[] = {"value", "precision", NULL};
    PyObject *value = Py_None;
    PyObject *precision = Py_None;
    long adjust_to_prec;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OO", kw_names, &value,
                                     &precision))
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
    if (self->numerator == NULL) {
        fpdec_as_integer_ratio(&self->numerator, &self->denominator,
                               &self->fpdec);
        if (self->numerator == NULL)
            return NULL;
    }
    Py_INCREF(self->numerator);
    return self->numerator;
}

static PyObject *
Decimal_denominator_get(DecimalObject *self) {
    if (self->denominator == NULL) {
        fpdec_as_integer_ratio(&self->numerator, &self->denominator,
                               &self->fpdec);
        if (self->denominator == NULL)
            return NULL;
    }
    Py_INCREF(self->denominator);
    return self->denominator;
}

static PyObject *
Decimal_real_get(DecimalObject *self) {
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *
Decimal_imag_get(DecimalObject *self) {
    Py_INCREF(PyZERO);
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
    PyObject *dec_prec = NULL;
    PyObject *res = NULL;
    int64_t exp;

    exp = fpdec_dec_coeff_exp(&coeff, fpdec);
    if (coeff == NULL) {
        goto ERROR;
    }
    ASSIGN_AND_CHECK_NULL(sign, PyLong_FromLong(FPDEC_SIGN(fpdec)));
    ASSIGN_AND_CHECK_NULL(dec_prec, PyLong_FromLong(exp));
    ASSIGN_AND_CHECK_NULL(res, PyTuple_Pack(3, sign, coeff, dec_prec));
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_DECREF(sign);
    Py_DECREF(coeff);
    Py_DECREF(dec_prec);
    return res;
}

static PyObject *
Decimal_as_fraction(DecimalObject *self) {
    PyObject *res = NULL;
    PyObject *num = NULL;
    PyObject *den = NULL;

    ASSIGN_AND_CHECK_NULL(num, Decimal_numerator_get(self));
    ASSIGN_AND_CHECK_NULL(den, Decimal_denominator_get(self));
    ASSIGN_AND_CHECK_NULL(res, PyObject_CallFunctionObjArgs(Fraction,
                                                            num, den, NULL));
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(num);
    Py_XDECREF(den);
    return res;
}

static PyObject *
Decimal_as_integer_ratio(DecimalObject *self) {
    PyObject *res = NULL;
    PyObject *num = NULL;
    PyObject *den = NULL;

    ASSIGN_AND_CHECK_NULL(num, Decimal_numerator_get(self));
    ASSIGN_AND_CHECK_NULL(den, Decimal_denominator_get(self));
    ASSIGN_AND_CHECK_NULL(res, PyTuple_Pack(2, num, den));
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(num);
    Py_XDECREF(den);
    return res;
}

static PyObject *
Decimal_floor(DecimalObject *self) {
    PyObject *res = NULL;
    PyObject *num = NULL;
    PyObject *den = NULL;

    ASSIGN_AND_CHECK_NULL(num, Decimal_numerator_get(self));
    ASSIGN_AND_CHECK_NULL(den, Decimal_denominator_get(self));
    ASSIGN_AND_CHECK_NULL(res, PyNumber_FloorDivide(num, den));
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(num);
    Py_XDECREF(den);
    return res;
}

static PyObject *
Decimal_ceil(DecimalObject *self) {
    PyObject *res = NULL;
    PyObject *num = NULL;
    PyObject *den = NULL;
    PyObject *t = NULL;

    ASSIGN_AND_CHECK_NULL(num, Decimal_numerator_get(self));
    ASSIGN_AND_CHECK_NULL(den, Decimal_denominator_get(self));
    ASSIGN_AND_CHECK_NULL(t, PyNumber_Negative(num));
    ASSIGN_AND_CHECK_NULL(t, PyNumber_InPlaceFloorDivide(t, den));
    ASSIGN_AND_CHECK_NULL(res, PyNumber_Negative(t));
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(num);
    Py_XDECREF(den);
    Py_XDECREF(t);
    return res;
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
Decimal_str(DecimalObject *self) {
    PyObject *res = NULL;
    char *lit = fpdec_as_ascii_literal(&self->fpdec, false);

    if (lit == NULL) {
        PyErr_SetNone(PyExc_MemoryError);
        return NULL;
    }
    res = PyUnicode_InternFromString(lit);
    fpdec_mem_free(lit);
    return res;
}

static PyObject *
Decimal_repr(DecimalObject *self) {
    PyObject *res = NULL;
    char *lit = fpdec_as_ascii_literal(&self->fpdec, true);
    char *radix_point;
    size_t n_frac_digits;

    if (lit == NULL) {
        PyErr_SetNone(PyExc_MemoryError);
        return NULL;
    }
    radix_point = strrchr(lit, '.');
    if (radix_point == NULL)
        n_frac_digits = 0;
    else
        n_frac_digits = strlen(radix_point) - 1;
    if (n_frac_digits == FPDEC_DEC_PREC(&self->fpdec))
        if (n_frac_digits == 0)
            res = PyUnicode_FromFormat("Decimal(%s)", lit);
        else
            res = PyUnicode_FromFormat("Decimal('%s')", lit);
    else {
        if (n_frac_digits == 0)
            res = PyUnicode_FromFormat("Decimal(%s, %u)",
                                       lit, FPDEC_DEC_PREC(&self->fpdec));
        else
            res = PyUnicode_FromFormat("Decimal('%s', %u)",
                                       lit, FPDEC_DEC_PREC(&self->fpdec));
    }
    fpdec_mem_free(lit);
    return res;
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
Decimal_cmp_to_int(DecimalObject *x, PyObject *y, int op) {
    PyObject *res = NULL;
    PyObject *num = NULL;
    PyObject *den = NULL;
    PyObject *t = NULL;

    ASSIGN_AND_CHECK_NULL(num, Decimal_numerator_get(x));
    ASSIGN_AND_CHECK_NULL(den, Decimal_denominator_get(x));
    ASSIGN_AND_CHECK_NULL(t, PyNumber_Multiply(y, den));
    ASSIGN_AND_CHECK_NULL(res, PyObject_RichCompare(num, t, op));
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());
    Py_CLEAR(res);

CLEAN_UP:
    Py_CLEAR(num);
    Py_CLEAR(den);
    Py_CLEAR(t);
    return res;
}

static PyObject *
Decimal_cmp_to_ratio(DecimalObject *x, PyObject *y, int op) {
    PyObject *res = NULL;
    PyObject *x_num = NULL;
    PyObject *x_den = NULL;
    PyObject *y_num = NULL;
    PyObject *y_den = NULL;
    PyObject *lhs = NULL;
    PyObject *rhs = NULL;

    ASSIGN_AND_CHECK_NULL(x_num, Decimal_numerator_get(x));
    ASSIGN_AND_CHECK_NULL(x_den, Decimal_denominator_get(x));
    ASSIGN_AND_CHECK_NULL(y_num, PySequence_GetItem(y, 0));
    ASSIGN_AND_CHECK_NULL(y_den, PySequence_GetItem(y, 1));
    ASSIGN_AND_CHECK_NULL(lhs, PyNumber_Multiply(x_num, y_den));
    ASSIGN_AND_CHECK_NULL(rhs, PyNumber_Multiply(y_num, x_den));
    ASSIGN_AND_CHECK_NULL(res, PyObject_RichCompare(lhs, rhs, op));
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());
    Py_CLEAR(res);

CLEAN_UP:
    Py_CLEAR(x_num);
    Py_CLEAR(x_den);
    Py_CLEAR(y_num);
    Py_CLEAR(y_den);
    Py_CLEAR(lhs);
    Py_CLEAR(rhs);
    return res;
}

static PyObject *
Decimal_richcompare(DecimalObject *self, PyObject *other, int op) {
    PyObject *res = NULL;
    PyObject *t = NULL;

    // Decimal
    if (Decimal_Check(other)) {
        int r = fpdec_compare(&self->fpdec,
                              &((DecimalObject *)other)->fpdec, false);
        Py_RETURN_RICHCOMPARE(r, 0, op);
    }

    // Python <int>
    if (PyLong_Check(other))
        return Decimal_cmp_to_int(self, other, op);

    // Integral
    if (PyObject_IsInstance(other, Integral)) {
        ASSIGN_AND_CHECK_NULL(t, PyNumber_Long(other));
        ASSIGN_AND_CHECK_NULL(res, Decimal_cmp_to_int(self, t, op));
        goto CLEAN_UP;
    }

    // Rational
    if (PyObject_IsInstance(other, Rational)) {
        ASSIGN_AND_CHECK_NULL(t, Decimal_as_fraction(self));
        ASSIGN_AND_CHECK_NULL(res, PyObject_RichCompare(t, other, op));
        goto CLEAN_UP;
    }

    // Python <float>, standard lib Decimal, Real
    // Test if convertable to a Rational
    t = PyObject_CallMethod(other, "as_integer_ratio", NULL);
    if (t != NULL) {
        res = Decimal_cmp_to_ratio(self, t, op);
        goto CLEAN_UP;
    }
    else {
        PyObject *exc = PyErr_Occurred();
        if (exc == PyExc_ValueError || exc == PyExc_OverflowError) {
            // 'nan' or 'inf'
            PyErr_Clear();
            res = PyObject_RichCompare(PyZERO, other, op);
            goto CLEAN_UP;
        }
        else if (exc == PyExc_AttributeError)
            // fall through
            PyErr_Clear();
        else
            goto ERROR;
    }

    // Complex
    if (PyObject_IsInstance(other, Complex)) {
        if (op == Py_EQ || op == Py_NE) {
            ASSIGN_AND_CHECK_NULL(t, PyObject_GetAttrString(other, "imag"));
            if (PyObject_RichCompareBool(t, PyZERO, Py_EQ)) {
                Py_DECREF(t);
                ASSIGN_AND_CHECK_NULL(t, PyObject_GetAttrString(other,
                                                                "real"));
                ASSIGN_AND_CHECK_NULL(res, Decimal_richcompare(self, t, op));
            }
            else {
                res = op == Py_EQ ? Py_False : Py_True;
                Py_INCREF(res);
            }
            goto CLEAN_UP;
        }
    }

    // don't know how to compare
    Py_INCREF(Py_NotImplemented);
    res = Py_NotImplemented;
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());
    Py_CLEAR(res);

CLEAN_UP:
    Py_CLEAR(t);
    return res;
}

// unary number methods

static PyObject *
Decimal_neg(PyObject *x) {
    DecimalObject *dec;
    fpdec_t *x_fpdec = &((DecimalObject *)x)->fpdec;
    error_t rc;

    if (FPDEC_EQ_ZERO(x_fpdec)) {
        Py_INCREF(x);
        return x;
    }

    DECIMAL_ALLOC(Py_TYPE(x), dec);
    rc = fpdec_copy(&dec->fpdec, x_fpdec);
    CHECK_FPDEC_ERROR(rc);
    dec->fpdec.sign *= -1;
    return (PyObject *)dec;

ERROR:
    Py_XDECREF(dec);
    return NULL;
}

static PyObject *
Decimal_pos(PyObject *x) {
    Py_INCREF(x);
    return x;
}

static PyObject *
Decimal_abs(PyObject *x) {
    DecimalObject *dec;
    fpdec_t *x_fpdec = &((DecimalObject *)x)->fpdec;
    error_t rc;

    if (FPDEC_SIGN(x_fpdec) != FPDEC_SIGN_NEG) {
        Py_INCREF(x);
        return x;
    }

    DECIMAL_ALLOC(Py_TYPE(x), dec);
    rc = fpdec_copy(&dec->fpdec, x_fpdec);
    CHECK_FPDEC_ERROR(rc);
    dec->fpdec.sign = 1;
    return (PyObject *)dec;

ERROR:
    Py_XDECREF(dec);
    return NULL;
}

static PyObject *
Decimal_int(DecimalObject *x) {
    fpdec_t *fpdec = &x->fpdec;
    return PyLong_from_fpdec(fpdec);
}

static PyObject *
Decimal_float(DecimalObject *x) {
    PyObject *res = NULL;
    PyObject *num = NULL;
    PyObject *den = NULL;

    ASSIGN_AND_CHECK_NULL(num, Decimal_numerator_get(x));
    ASSIGN_AND_CHECK_NULL(den, Decimal_denominator_get(x));
    ASSIGN_AND_CHECK_NULL(res, PyNumber_TrueDivide(num, den));
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(num);
    Py_XDECREF(den);
    return res;
}

static int
Decimal_bool(DecimalObject *x) {
    return !FPDEC_EQ_ZERO(&x->fpdec);
}

// binary number methods

static fpdec_t *
fpdec_from_obj(fpdec_t *tmp, PyObject *obj) {
    PyObject *ratio = NULL;
    PyObject *num = NULL;
    PyObject *den = NULL;
    error_t rc;
    fpdec_t *fpdec = NULL;

    if (Decimal_Check(obj)) {
        fpdec = &((DecimalObject *)obj)->fpdec;
    }
    else if (PyLong_Check(obj)) {
        rc = fpdec_from_pylong(tmp, obj);
        if (rc == FPDEC_OK)
            fpdec = tmp;
    }
    else if (PyObject_IsInstance(obj, Integral)) {
        ASSIGN_AND_CHECK_NULL(num, PyNumber_Long(obj));
        rc = fpdec_from_pylong(tmp, num);
        if (rc == FPDEC_OK)
            fpdec = tmp;
    }
    else if (PyObject_IsInstance(obj, Rational)) {
        ASSIGN_AND_CHECK_NULL(num, PyObject_GetAttrString(obj, "numerator"));
        ASSIGN_AND_CHECK_NULL(den,
                              PyObject_GetAttrString(obj, "denominator"));
        rc = fpdec_from_num_den(tmp, num, den, -1);
        if (rc == FPDEC_OK)
            fpdec = tmp;
    }
    else if (PyObject_IsInstance(obj, Real) ||
             PyObject_IsInstance(obj, StdLibDecimal)) {
        ASSIGN_AND_CHECK_NULL(ratio,
                              PyObject_CallMethod(obj, "as_integer_ratio",
                                                  NULL));
        ASSIGN_AND_CHECK_NULL(num, PySequence_GetItem(ratio, 0));
        ASSIGN_AND_CHECK_NULL(den, PySequence_GetItem(ratio, 1));
        rc = fpdec_from_num_den(tmp, num, den, -1);
        if (rc == FPDEC_OK)
            fpdec = tmp;
    }
    goto CLEAN_UP;

ERROR:
    {
        PyObject *err = PyErr_Occurred();
        assert(err);
        if (err == PyExc_ValueError || err == PyExc_OverflowError ||
            err == PyExc_AttributeError) {
            PyErr_Clear();
            PyErr_Format(PyExc_ValueError, "Can't convert %R to Decimal.",
                         obj);
        }
    }

CLEAN_UP:
    Py_XDECREF(ratio);
    Py_XDECREF(num);
    Py_XDECREF(den);
    return fpdec;
}

static PyObject *
fallback_op(PyObject *x, PyObject *y, binaryfunc op) {
    PyObject *res = NULL;
    PyObject *fx = NULL;
    PyObject *fy = NULL;

    ASSIGN_AND_CHECK_NULL(fx, PyObject_CallFunctionObjArgs(Fraction, x,
                                                           Py_None, NULL));
    ASSIGN_AND_CHECK_NULL(fy, PyObject_CallFunctionObjArgs(Fraction, y,
                                                           Py_None, NULL));
    ASSIGN_AND_CHECK_NULL(res, op(fx, fy));
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(fx);
    Py_XDECREF(fy);
    return res;
}

static PyObject *
fallback_n_convert_op(PyTypeObject *dec_type, PyObject *x, PyObject *y,
                      binaryfunc op) {
    PyObject *res = NULL;
    PyObject *dec = NULL;

    ASSIGN_AND_CHECK_NULL(res, fallback_op(x, y, op));
    // try to convert result back to Decimal
    dec = DecimalType_from_rational(dec_type, res, -1);
    if (dec == NULL) {
        // result is not convertable to a Decimal, so return Fraction
        PyErr_Clear();
    }
    else {
        Py_CLEAR(res);
        res = dec;
    }
    return res;

ERROR:
    assert(PyErr_Occurred());
    return NULL;
}

#define BINOP_DEC_TYPE(x, y)            \
    PyTypeObject *dec_type =            \
        Decimal_Check(x) ? Py_TYPE(x) : \
        (Decimal_Check(y) ? Py_TYPE(y) : NULL)

#define BINOP_ALLOC_RESULT(type)      \
    DecimalObject *dec;               \
    DECIMAL_ALLOC(type, dec);         \
    PyObject *res = (PyObject *) dec

#define CONVERT_AND_CHECK(fpdec, tmp, obj)         \
    do {                                           \
    fpdec = fpdec_from_obj(tmp, obj);              \
    if (fpdec == NULL) {                           \
        if (PyErr_Occurred())                      \
            goto ERROR;                            \
        else if (PyObject_IsInstance(obj, Number)) \
            goto FALLBACK;                         \
        else  {                                    \
            res = Py_NotImplemented;               \
            Py_INCREF(res);                        \
            goto CLEAN_UP;                         \
        }                                          \
    }} while (0)

static PyObject *
Decimal_add(PyObject *x, PyObject *y) {
    BINOP_DEC_TYPE(x, y);
    BINOP_ALLOC_RESULT(dec_type);
    fpdec_t *fpz = &dec->fpdec;
    error_t rc;
    fpdec_t tmp_x = FPDEC_ZERO;
    fpdec_t tmp_y = FPDEC_ZERO;
    fpdec_t *fpx, *fpy;

    CONVERT_AND_CHECK(fpx, &tmp_x, x);
    CONVERT_AND_CHECK(fpy, &tmp_y, y);

    rc = fpdec_add(fpz, fpx, fpy);
    if (rc == FPDEC_OK)
        goto CLEAN_UP;

FALLBACK:
    Py_CLEAR(dec);
    ASSIGN_AND_CHECK_OK(res, fallback_n_convert_op(dec_type, x, y,
                                                   PyNumber_Add));

ERROR:
    assert(PyErr_Occurred());
    Py_CLEAR(res);

CLEAN_UP:
    fpdec_reset_to_zero(&tmp_x, 0);
    fpdec_reset_to_zero(&tmp_y, 0);
    return res;
}

static PyObject *
Decimal_sub(PyObject *x, PyObject *y) {
    BINOP_DEC_TYPE(x, y);
    BINOP_ALLOC_RESULT(dec_type);
    fpdec_t *fpz = &dec->fpdec;
    error_t rc;
    fpdec_t tmp_x = FPDEC_ZERO;
    fpdec_t tmp_y = FPDEC_ZERO;
    fpdec_t *fpx, *fpy;

    CONVERT_AND_CHECK(fpx, &tmp_x, x);
    CONVERT_AND_CHECK(fpy, &tmp_y, y);

    rc = fpdec_sub(fpz, fpx, fpy);
    if (rc == FPDEC_OK)
        goto CLEAN_UP;

FALLBACK:
    Py_CLEAR(dec);
    ASSIGN_AND_CHECK_OK(res, fallback_n_convert_op(dec_type, x, y,
                                                   PyNumber_Subtract));

ERROR:
    assert(PyErr_Occurred());
    Py_CLEAR(res);

CLEAN_UP:
    fpdec_reset_to_zero(&tmp_x, 0);
    fpdec_reset_to_zero(&tmp_y, 0);
    return res;
}

static PyObject *
Decimal_mul(PyObject *x, PyObject *y) {
    BINOP_DEC_TYPE(x, y);
    BINOP_ALLOC_RESULT(dec_type);
    fpdec_t *fpz = &dec->fpdec;
    error_t rc;
    fpdec_t tmp_x = FPDEC_ZERO;
    fpdec_t tmp_y = FPDEC_ZERO;
    fpdec_t *fpx, *fpy;

    CONVERT_AND_CHECK(fpx, &tmp_x, x);
    CONVERT_AND_CHECK(fpy, &tmp_y, y);

    rc = fpdec_mul(fpz, fpx, fpy);
    if (rc == FPDEC_OK)
        goto CLEAN_UP;

FALLBACK:
    Py_CLEAR(dec);
    ASSIGN_AND_CHECK_OK(res, fallback_n_convert_op(dec_type, x, y,
                                                   PyNumber_Multiply));

ERROR:
    assert(PyErr_Occurred());
    Py_CLEAR(res);

CLEAN_UP:
    fpdec_reset_to_zero(&tmp_x, 0);
    fpdec_reset_to_zero(&tmp_y, 0);
    return res;
}

static PyObject *
Decimal_remainder(PyObject *x, PyObject *y) {
    BINOP_DEC_TYPE(x, y);
    BINOP_ALLOC_RESULT(dec_type);
    fpdec_t *fpz = &dec->fpdec;
    error_t rc;
    fpdec_t tmp_x = FPDEC_ZERO;
    fpdec_t tmp_y = FPDEC_ZERO;
    fpdec_t q = FPDEC_ZERO;
    fpdec_t *fpx, *fpy;

    CONVERT_AND_CHECK(fpx, &tmp_x, x);
    CONVERT_AND_CHECK(fpy, &tmp_y, y);

    rc = fpdec_divmod(&q, fpz, fpx, fpy);
    if (rc == FPDEC_OK)
        goto CLEAN_UP;

FALLBACK:
    Py_CLEAR(dec);
    ASSIGN_AND_CHECK_OK(res, fallback_n_convert_op(dec_type, x, y,
                                                   PyNumber_Remainder));

ERROR:
    assert(PyErr_Occurred());
    Py_CLEAR(res);

CLEAN_UP:
    fpdec_reset_to_zero(&tmp_x, 0);
    fpdec_reset_to_zero(&tmp_y, 0);
    fpdec_reset_to_zero(&q, 0);
    return res;
}

static PyObject *
Decimal_divmod(PyObject *x, PyObject *y) {
    BINOP_DEC_TYPE(x, y);
    PyObject *res = NULL;
    PyObject *quot = NULL;
    PyObject *rem = NULL;
    error_t rc;
    fpdec_t tmp_x = FPDEC_ZERO;
    fpdec_t tmp_y = FPDEC_ZERO;
    fpdec_t q = FPDEC_ZERO;
    fpdec_t r = FPDEC_ZERO;
    fpdec_t *fpx, *fpy;

    CONVERT_AND_CHECK(fpx, &tmp_x, x);
    CONVERT_AND_CHECK(fpy, &tmp_y, y);

    rc = fpdec_divmod(&q, &r, fpx, fpy);
    if (rc != FPDEC_OK)
        goto FALLBACK;
    ASSIGN_AND_CHECK_NULL(quot, PyLong_from_fpdec(&q));
    ASSIGN_AND_CHECK_NULL(rem, DecimalType_from_fpdec(dec_type, &r, -1));
    ASSIGN_AND_CHECK_NULL(res, PyTuple_Pack(2, quot, rem));
    goto CLEAN_UP;

FALLBACK:
    ASSIGN_AND_CHECK_OK(res, fallback_op(x, y, PyNumber_Divmod));

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(quot);
    Py_XDECREF(rem);
    fpdec_reset_to_zero(&tmp_x, 0);
    fpdec_reset_to_zero(&tmp_y, 0);
    fpdec_reset_to_zero(&q, 0);
    fpdec_reset_to_zero(&r, 0);
    return res;
}

static PyObject *
Decimal_floordiv(PyObject *x, PyObject *y) {
    BINOP_DEC_TYPE(x, y);
    PyObject *res = NULL;
    error_t rc;
    fpdec_t tmp_x = FPDEC_ZERO;
    fpdec_t tmp_y = FPDEC_ZERO;
    fpdec_t q = FPDEC_ZERO;
    fpdec_t r = FPDEC_ZERO;
    fpdec_t *fpx, *fpy;

    CONVERT_AND_CHECK(fpx, &tmp_x, x);
    CONVERT_AND_CHECK(fpy, &tmp_y, y);

    rc = fpdec_divmod(&q, &r, fpx, fpy);
    if (rc == FPDEC_OK) {
        ASSIGN_AND_CHECK_NULL(res, PyLong_from_fpdec(&q));
        goto CLEAN_UP;
    }

FALLBACK:
    ASSIGN_AND_CHECK_OK(res, fallback_op(x, y, PyNumber_FloorDivide));

ERROR:
    assert(PyErr_Occurred());
    Py_CLEAR(res);

CLEAN_UP:
    fpdec_reset_to_zero(&tmp_x, 0);
    fpdec_reset_to_zero(&tmp_y, 0);
    fpdec_reset_to_zero(&r, 0);
    return res;
}

static PyObject *
Decimal_truediv(PyObject *x, PyObject *y) {
    BINOP_DEC_TYPE(x, y);
    BINOP_ALLOC_RESULT(dec_type);
    fpdec_t *fpz = &dec->fpdec;
    error_t rc;
    fpdec_t tmp_x = FPDEC_ZERO;
    fpdec_t tmp_y = FPDEC_ZERO;
    fpdec_t *fpx, *fpy;

    CONVERT_AND_CHECK(fpx, &tmp_x, x);
    CONVERT_AND_CHECK(fpy, &tmp_y, y);

    rc = fpdec_div(fpz, fpx, fpy, -1, FPDEC_ROUND_DEFAULT);
    if (rc == FPDEC_OK)
        goto CLEAN_UP;

FALLBACK:
    Py_CLEAR(dec);
    ASSIGN_AND_CHECK_OK(res, fallback_n_convert_op(dec_type, x, y,
                                                   PyNumber_TrueDivide));

ERROR:
    assert(PyErr_Occurred());
    Py_CLEAR(res);

CLEAN_UP:
    fpdec_reset_to_zero(&tmp_x, 0);
    fpdec_reset_to_zero(&tmp_y, 0);
    return res;
}

// ternary number methods

static PyObject *
dec_pow_pylong(DecimalObject *x, PyObject *exp) {
    PyObject *res = NULL;
    PyObject *f = NULL;
    PyObject *dec = NULL;

    if (PyObject_RichCompareBool(exp, PyZERO, Py_EQ) == 1) {
        fpdec_t *one = (fpdec_t *)&FPDEC_ONE;
        ASSIGN_AND_CHECK_NULL(res,
                              (PyObject *)DecimalType_from_fpdec(Py_TYPE(x),
                                                                 one, -1));
    }
    else {
        ASSIGN_AND_CHECK_NULL(f, Decimal_as_fraction(x));
        ASSIGN_AND_CHECK_NULL(res, PyNumber_Power(f, exp, Py_None));
        // try to convert result back to Decimal
        dec = DecimalType_from_rational(Py_TYPE(x), res, -1);
        if (dec == NULL) {
            // result is not convertable to a Decimal, so return Fraction
            PyErr_Clear();
        }
        else {
            Py_CLEAR(res);
            res = dec;
        }
    }
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(f);
    return res;
}

static PyObject *
dec_pow_obj(DecimalObject *x, PyObject *y) {
    PyObject *res = NULL;
    PyObject *exp = NULL;
    PyObject *fx = NULL;
    PyObject *fy = NULL;

    exp = PyNumber_Long(y);
    if (exp == NULL) {
        PyObject *exc = PyErr_Occurred();
        if (exc == PyExc_ValueError || exc == PyExc_OverflowError) {
            PyErr_Clear();
            PyErr_Format(PyExc_ValueError, "Unsupported operand: %R", y);
        }
        goto ERROR;
    }
    if (PyObject_RichCompareBool(exp, y, Py_NE) == 1) {
        // fractional exponent => fallback to float
        ASSIGN_AND_CHECK_NULL(fx, PyNumber_Float((PyObject *)x));
        ASSIGN_AND_CHECK_NULL(fy, PyNumber_Float(y));
        ASSIGN_AND_CHECK_NULL(res, PyNumber_Power(fx, fy, Py_None));
    }
    else
        ASSIGN_AND_CHECK_NULL(res, dec_pow_pylong(x, exp));
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(exp);
    Py_XDECREF(fx);
    Py_XDECREF(fy);
    return res;
}

static PyObject *
obj_pow_dec(PyObject *x, DecimalObject *y) {
    PyObject *res = NULL;
    PyObject *num = NULL;
    PyObject *den = NULL;
    PyObject *f = NULL;

    ASSIGN_AND_CHECK_NULL(den, Decimal_denominator_get(y));
    if (PyObject_RichCompareBool(den, PyONE, Py_EQ) == 1) {
        ASSIGN_AND_CHECK_NULL(num, Decimal_numerator_get(y));
        ASSIGN_AND_CHECK_NULL(res, PyNumber_Power(x, num, Py_None));
    }
    else {
        ASSIGN_AND_CHECK_NULL(f, PyNumber_Float(y));
        ASSIGN_AND_CHECK_NULL(res, PyNumber_Power(x, f, Py_None));
    }
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(num);
    Py_XDECREF(den);
    Py_XDECREF(f);
    return res;
}

static PyObject *
Decimal_pow(PyObject *x, PyObject *y, PyObject *mod) {
    if (mod != Py_None) {
        PyErr_SetString(PyExc_TypeError,
                        "3rd argument not allowed unless all arguments "
                        "are integers.");
        return NULL;
    }

    if (Decimal_Check(x)) {
        if (PyObject_IsInstance(y, Real) ||
            PyObject_IsInstance(y, StdLibDecimal)) {
            return dec_pow_obj((DecimalObject *)x, y);
        }
        Py_RETURN_NOTIMPLEMENTED;
    }
    else {          // y is a Decimal
        return obj_pow_dec(x, (DecimalObject *)y);
    }
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
     "I. e. the smallest positive denominator from the pairs of integers,"
     "\nwhose ratio is equal to `self`.\n\n", 0},
    {"real", (getter)Decimal_real_get, 0,
     "Return real part of `self`.\n\n"
     "Returns `self` (Real numbers are their real component).\n\n", 0},
    {"imag", (getter)Decimal_imag_get, 0,
     "Return imaginary part of `self`.\n\n"
     "Returns 0 (Real numbers have no imaginary component).\n\n", 0},
    {0, 0, 0, 0, 0}};

static PyMethodDef Decimal_methods[] = {
    /* class methods */
    {"from_float",
     (PyCFunction)DecimalType_from_float_or_int,
     METH_O | METH_CLASS,
     DecimalType_from_float_doc},
    {"from_decimal",
     (PyCFunction)DecimalType_from_decimal_or_int,
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
    {"__trunc__",
     (PyCFunction)Decimal_int,
     METH_NOARGS,
     Decimal_trunc_doc},
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
    {Py_tp_free, PyObject_Del},
    {Py_tp_richcompare, Decimal_richcompare},
    {Py_tp_hash, Decimal_hash},
    {Py_tp_str, Decimal_str},
    {Py_tp_repr, Decimal_repr},
    /* properties */
    {Py_tp_getset, Decimal_properties},
    /* number methods */
    {Py_nb_bool, Decimal_bool},
    {Py_nb_add, Decimal_add},
    {Py_nb_subtract, Decimal_sub},
    {Py_nb_multiply, Decimal_mul},
    {Py_nb_remainder, Decimal_remainder},
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
    "_cdecimalfp.Decimal",                  /* name */
    sizeof(DecimalObject),                  /* basicsize */
    0,                                      /* itemsize */
    0,                                      /* flags */
    decimal_type_slots                      /* slots */
};

// *** Helper functions ***

static inline PyObject *
PyLong_10_pow_exp(const uint8_t exp) {
    assert(exp <= DEC_DIGITS_PER_DIGIT);
    return PyLong_FromUnsignedLong(_10_POW_N(exp));
}

static PyObject *
PyLong_from_digits(const fpdec_digit_t *digits,
                   const fpdec_n_digits_t n_digits,
                   const uint8_t n_dec_adjust) {
    PyObject *res = NULL;
    PyObject *t = NULL;
    PyObject *digit = NULL;
    PyObject *adj_digit = NULL;
    PyObject *adj_base = NULL;
    uint8_t adj_base_exp;
    ssize_t idx = n_digits - 1;

    assert(n_digits > 1);
    assert(n_dec_adjust < DEC_DIGITS_PER_DIGIT);

    ASSIGN_AND_CHECK_NULL(res, PyLong_FromUnsignedLongLong(digits[idx]));
    while (--idx > 0) {
        ASSIGN_AND_CHECK_NULL(digit,
                              PyLong_FromUnsignedLongLong(digits[idx]));
        ASSIGN_AND_CHECK_NULL(t, PyNumber_Multiply(res, PyRADIX));
        Py_DECREF(res);
        ASSIGN_AND_CHECK_NULL(res, PyNumber_Add(t, digit));
        Py_CLEAR(digit);
        Py_CLEAR(t);
    }
    if (n_dec_adjust == 0) {
        ASSIGN_AND_CHECK_NULL(digit,
                              PyLong_FromUnsignedLongLong(digits[idx]));
        ASSIGN_AND_CHECK_NULL(t, PyNumber_Multiply(res, PyRADIX));
        Py_CLEAR(res);
        ASSIGN_AND_CHECK_NULL(res, PyNumber_Add(t, digit));
    }
    else {
        ASSIGN_AND_CHECK_NULL(digit,
                              PyLong_FromUnsignedLongLong(digits[idx]));
        ASSIGN_AND_CHECK_NULL(t, PyLong_10_pow_exp(n_dec_adjust));
        ASSIGN_AND_CHECK_NULL(adj_digit, PyNumber_FloorDivide(digit, t));
        Py_CLEAR(t);
        adj_base_exp = DEC_DIGITS_PER_DIGIT - n_dec_adjust;
        ASSIGN_AND_CHECK_NULL(adj_base, PyLong_10_pow_exp(adj_base_exp));
        ASSIGN_AND_CHECK_NULL(t, PyNumber_Multiply(res, adj_base));
        Py_CLEAR(res);
        ASSIGN_AND_CHECK_NULL(res, PyNumber_Add(t, adj_digit));
    }
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());
    Py_CLEAR(res);

CLEAN_UP:
    Py_XDECREF(t);
    Py_XDECREF(digit);
    Py_XDECREF(adj_digit);
    Py_XDECREF(adj_base);
    return res;
}

static inline PyObject *
PyLong_from_u128_lo_hi(uint64_t lo, uint64_t hi) {
    PyObject *res = NULL;
    PyObject *res_hi = NULL;
    PyObject *res_lo = NULL;
    PyObject *sh = NULL;
    PyObject *t = NULL;

    ASSIGN_AND_CHECK_NULL(res_hi, PyLong_FromUnsignedLongLong(hi));
    ASSIGN_AND_CHECK_NULL(res_lo, PyLong_FromUnsignedLongLong(lo));
    ASSIGN_AND_CHECK_NULL(sh, PyLong_FromSize_t(64));
    ASSIGN_AND_CHECK_NULL(t, PyNumber_Lshift(res_hi, sh));
    ASSIGN_AND_CHECK_NULL(res, PyNumber_Add(t, res_lo));
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(res_hi);
    Py_XDECREF(res_lo);
    Py_XDECREF(sh);
    Py_XDECREF(t);
    return res;
}

static inline PyObject *
PyLong_from_u128(uint128_t *ui) {
    if (ui->hi == 0)
        return PyLong_FromUnsignedLongLong(ui->lo);
    else
        return PyLong_from_u128_lo_hi(ui->lo, ui->hi);
}

static PyObject *
PyLong_from_fpdec(const fpdec_t *fpdec) {
    PyObject *res = NULL;
    PyObject *py_exp = NULL;
    PyObject *ten_pow_exp = NULL;
    PyObject *t = NULL;

    if (FPDEC_EQ_ZERO(fpdec)) {
        Py_INCREF(PyZERO);
        return PyZERO;
    }

    if (FPDEC_IS_DYN_ALLOC(fpdec)) {
        fpdec_digit_t *digits = FPDEC_DYN_DIGITS(fpdec);
        int64_t n_digits = FPDEC_DYN_N_DIGITS(fpdec);
        int64_t exp = FPDEC_DYN_EXP(fpdec);
        if (-exp >= n_digits) {
            // there is no integral part
            Py_INCREF(PyZERO);
            return PyZERO;
        }
        if (exp < 0) {
            // exclude fractional digits
            n_digits += exp;
            digits += -exp;
            exp = 0;
        }
        if (n_digits == 1)
            ASSIGN_AND_CHECK_NULL(res,
                                  PyLong_FromUnsignedLongLong(*digits));
        else
            ASSIGN_AND_CHECK_NULL(res,
                                  PyLong_from_digits(digits, n_digits, 0));
        if (exp > 0) {
            ASSIGN_AND_CHECK_NULL(py_exp, PyLong_FromLongLong(exp));
            ASSIGN_AND_CHECK_NULL(ten_pow_exp,
                                  PyNumber_Power(PyTEN, py_exp, Py_None));
            ASSIGN_AND_CHECK_NULL(res,
                                  PyNumber_InPlaceMultiply(res, ten_pow_exp));
        }
    }
    else {
        uint128_t shint = {fpdec->lo, fpdec->hi};
        fpdec_dec_prec_t prec = FPDEC_DEC_PREC(fpdec);
        if (prec > 0)
            u128_idiv_u64(&shint, _10_POW_N(prec));
        ASSIGN_AND_CHECK_NULL(res, PyLong_from_u128(&shint));
    }
    if (FPDEC_LT_ZERO(fpdec)) {
        t = res;                    // stealing reference
        ASSIGN_AND_CHECK_NULL(res, PyNumber_Negative(t));
    }
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(py_exp);
    Py_XDECREF(ten_pow_exp);
    Py_XDECREF(t);
    return res;
}

static int64_t
fpdec_dec_coeff_exp(PyObject **coeff, const fpdec_t *fpdec) {
    fpdec_sign_t sign;
    uint128_t coeff128;
    int64_t exp;

    assert(*coeff == NULL);

    if (FPDEC_EQ_ZERO(fpdec)) {
        *coeff = PyZERO;
        Py_INCREF(*coeff);
        return 0ULL;
    }
    else if (fpdec_as_sign_coeff128_exp(&sign, &coeff128, &exp, fpdec) == 0) {
        *coeff = PyLong_from_u128(&coeff128);
        return exp;
    }
    else {
        fpdec_digit_t *digits = FPDEC_DYN_DIGITS(fpdec);
        fpdec_digit_t least_signif_digit = digits[0];
        int8_t n_trailing_zeros = 0;
        while (least_signif_digit % 10 == 0) {
            least_signif_digit /= 10;
            n_trailing_zeros++;
        }
        exp = FPDEC_DYN_EXP(fpdec) * DEC_DIGITS_PER_DIGIT + n_trailing_zeros;
        *coeff = PyLong_from_digits(FPDEC_DYN_DIGITS(fpdec),
                                    FPDEC_DYN_N_DIGITS(fpdec),
                                    n_trailing_zeros);
        return exp;
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
        coeff = neg_coeff;          // stealing reference
    }

    if (exp == 0) {
        // *numerator = coeff, *denominator = 1
        Py_INCREF(coeff);
        *numerator = coeff;
        Py_INCREF(PyONE);
        *denominator = PyONE;
        return;
    }
    if (exp > 0) {
        // *numerator = coeff * 10 ^ exp, *denominator = 1
        ASSIGN_AND_CHECK_NULL(py_exp, PyLong_FromLong(exp));
        ASSIGN_AND_CHECK_NULL(ten_pow_exp,
                              PyNumber_Power(PyTEN, py_exp, Py_None));
        ASSIGN_AND_CHECK_NULL(*numerator,
                              PyNumber_Multiply(coeff, ten_pow_exp));
        Py_INCREF(PyONE);
        *denominator = PyONE;
    }
    else {
        // *numerator = coeff, *denominator = 10 ^ -exp, but they may need
        // to be normalized!
        ASSIGN_AND_CHECK_NULL(py_exp, PyLong_FromLong(-exp));
        ASSIGN_AND_CHECK_NULL(ten_pow_exp,
                              PyNumber_Power(PyTEN, py_exp, Py_None));
        ASSIGN_AND_CHECK_NULL(gcd,
                              PyObject_CallFunctionObjArgs(PyNumber_gcd,
                                                           coeff, ten_pow_exp,
                                                           NULL));
        ASSIGN_AND_CHECK_NULL(*numerator, PyNumber_FloorDivide(coeff, gcd));
        ASSIGN_AND_CHECK_NULL(*denominator, PyNumber_FloorDivide(ten_pow_exp,
                                                                 gcd));
    }
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());
    Py_CLEAR(*numerator);
    Py_CLEAR(*denominator);

CLEAN_UP:
    Py_XDECREF(coeff);
    Py_XDECREF(py_exp);
    Py_XDECREF(ten_pow_exp);
    Py_XDECREF(gcd);
}

/* No. of digits base tb needed to represent an n-digit number base fb */
static inline size_t
n_digits_needed(size_t n, uint64_t fb, uint64_t tb) {
    const double log10_2pow64 = log10(pow(2, 64));
    double log10_fb = (fb == 0) ? log10_2pow64 : log10(fb);
    double log10_tb = (tb == 0) ? log10_2pow64 : log10(tb);
    return (size_t)ceil(log10_fb * n / log10_tb);
}

static inline uint128_t
PyLong_as_u128(PyObject *val) {
    // val must be a PyLong and must be >= 0 and < 2 ^ 96 !!!
    uint128_t res;
    PyObject *t, *q, *r;

    t = PyNumber_Divmod(val, Py2pow64);
    q = PySequence_GetItem(t, 0);
    r = PySequence_GetItem(t, 1);
    Py_DECREF(t);
    res.lo = PyLong_AsUnsignedLongLong(r);
    Py_DECREF(r);
    res.hi = PyLong_AsUnsignedLongLong(q);
    Py_DECREF(q);
    return res;
}

static inline error_t
PyLong_as_digit_array(fpdec_digit_t *res, const size_t n_digits,
                      PyObject *val) {
    // val must be a PyLong and must be > 0 !!!
    fpdec_digit_t *digit = res;
    PyObject *t, *q, *r;

    q = val;
    Py_INCREF(q);
    while (PyObject_RichCompareBool(q, PyZERO, Py_GT)) {
        t = PyNumber_Divmod(q, PyRADIX);
        Py_DECREF(q);
        q = PySequence_GetItem(t, 0);
        r = PySequence_GetItem(t, 1);
        Py_DECREF(t);
        *digit = PyLong_AsUnsignedLongLong(r);
        digit++;
        Py_DECREF(r);
    }
    Py_DECREF(q);
    return FPDEC_OK;
}

static error_t
fpdec_from_pylong(fpdec_t *fpdec, PyObject *val) {
    error_t rc;
    long long lval;
    PyObject *n_bits = NULL;
    size_t size_base_2;
    PyObject *abs_val = NULL;
    fpdec_sign_t sign;

    assert(PyLong_Check(val));

    lval = PyLong_AsLongLong(val);
    if (PyErr_Occurred() == NULL)
        return fpdec_from_long_long(fpdec, lval);
    // fall through
    PyErr_Clear();

    // handle PyLong out of range of long long
    if (PyObject_RichCompareBool(val, PyZERO, Py_LT)) {
        abs_val = PyNumber_Absolute(val);
        if (abs_val == NULL)
            return ENOMEM;
        sign = FPDEC_SIGN_NEG;
    }
    else {
        abs_val = val;
        Py_INCREF(abs_val);
        sign = FPDEC_SIGN_POS;
    }
    n_bits = PyObject_CallFunctionObjArgs(PyLong_bit_length, abs_val, NULL);
    size_base_2 = PyLong_AsSize_t(n_bits);
    Py_DECREF(n_bits);
    if (size_base_2 <= 96) {
        uint128_t i = PyLong_as_u128(abs_val);
        Py_DECREF(abs_val);
        fpdec->lo = i.lo;
        fpdec->hi = i.hi;
        FPDEC_SIGN(fpdec) = sign;
        return FPDEC_OK;
    }
    else {
        size_t n_digits = n_digits_needed(size_base_2, 2, RADIX);
        fpdec_digit_t *digits =
            (fpdec_digit_t *)fpdec_mem_alloc(n_digits, sizeof(fpdec_digit_t));
        if (digits == NULL) {
            Py_DECREF(abs_val);
            return ENOMEM;
        }
        rc = PyLong_as_digit_array(digits, n_digits, abs_val);
        Py_DECREF(abs_val);
        if (rc == FPDEC_OK)
            rc = fpdec_from_sign_digits_exp(fpdec, sign, n_digits, digits, 0);
        fpdec_mem_free(digits);
        return rc;
    }
}

// *** Enum ROUNDING type ***

static const char EnumRounding_name[] = "ROUNDING";
static PyObject *EnumRounding;  // will be imported from rounding.py

// ???: check optimization via static mapping (initialized in module exec)
static PyObject *
fpdec_rnd_2_py_rnd(enum FPDEC_ROUNDING_MODE fpdec_rnd) {
    PyObject *val = NULL;
    PyObject *py_rnd = NULL;

    ASSIGN_AND_CHECK_NULL(val, PyLong_FromLong((long)fpdec_rnd));
    ASSIGN_AND_CHECK_NULL(py_rnd,
                          PyObject_CallFunctionObjArgs(EnumRounding, val,
                                                       NULL));
    Py_INCREF(py_rnd);
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(val);
    return py_rnd;
}

static enum FPDEC_ROUNDING_MODE
py_rnd_2_fpdec_rnd(PyObject *py_rnd) {
    long fpdec_rnd = -1;
    PyObject *val = NULL;

    CHECK_TYPE(py_rnd, EnumRounding);
    ASSIGN_AND_CHECK_NULL(val, PyObject_GetAttrString(py_rnd, "value"));
    fpdec_rnd = PyLong_AsLong(val);
    if (fpdec_rnd < 1 || fpdec_rnd > FPDEC_MAX_ROUNDING_MODE) {
        goto ERROR;
    }
    goto CLEAN_UP;

ERROR:
    PyErr_Format(PyExc_TypeError, "Illegal rounding mode: %R", py_rnd);
    fpdec_rnd = -1;

CLEAN_UP:
    Py_XDECREF(val);
    return (enum FPDEC_ROUNDING_MODE)fpdec_rnd;
}

// *** _cdecimalfp module ***

static PyObject *
get_dflt_rounding_mode(PyObject *mod) {
    enum FPDEC_ROUNDING_MODE dflt = fpdec_get_default_rounding_mode();
    return fpdec_rnd_2_py_rnd(dflt);
}

static PyObject *
set_dflt_rounding_mode(PyObject *mod, PyObject *py_rnd) {
    enum FPDEC_ROUNDING_MODE new_dflt = py_rnd_2_fpdec_rnd(py_rnd);
    if (new_dflt < 0)
        return NULL;
    fpdec_set_default_rounding_mode(new_dflt);
    return Py_None;
}

static PyMethodDef cdecimalfp_methods[] = {
    {"get_dflt_rounding_mode",
     (PyCFunction)get_dflt_rounding_mode,
     METH_NOARGS,
     get_dflt_rounding_mode_doc},
    {"set_dflt_rounding_mode",
     (PyCFunction)set_dflt_rounding_mode,
     METH_O,
     set_dflt_rounding_mode_doc},
    {0, 0, 0, 0}
};

#define PYMOD_ADD_OBJ(module, name, obj)                    \
    do {                                                    \
        Py_INCREF(obj);                                     \
        if (PyModule_AddObject(module, name, obj) < 0) {    \
            Py_DECREF(obj);                                 \
            goto ERROR;                                     \
        }                                                   \
    } while (0)

PyDoc_STRVAR(cdecimalfp_doc, "Decimal fixed-point arithmetic.");

static int
cdecimalfp_exec(PyObject *module) {
    /* Import from numbers */
    PyObject *numbers = NULL;
    ASSIGN_AND_CHECK_NULL(numbers, PyImport_ImportModule("numbers"));
    ASSIGN_AND_CHECK_NULL(Number, PyObject_GetAttrString(numbers, "Number"));
    ASSIGN_AND_CHECK_NULL(Complex,
                          PyObject_GetAttrString(numbers, "Complex"));
    ASSIGN_AND_CHECK_NULL(Real, PyObject_GetAttrString(numbers, "Real"));
    ASSIGN_AND_CHECK_NULL(Rational,
                          PyObject_GetAttrString(numbers, "Rational"));
    ASSIGN_AND_CHECK_NULL(Integral,
                          PyObject_GetAttrString(numbers, "Integral"));
    Py_CLEAR(numbers);

    /* Import from fractions */
    PyObject *fractions = NULL;
    ASSIGN_AND_CHECK_NULL(fractions, PyImport_ImportModule("fractions"));
    ASSIGN_AND_CHECK_NULL(Fraction,
                          PyObject_GetAttrString(fractions, "Fraction"));
    Py_CLEAR(fractions);

    /* Import from decimal */
    PyObject *decimal = NULL;
    ASSIGN_AND_CHECK_NULL(decimal, PyImport_ImportModule("decimal"));
    ASSIGN_AND_CHECK_NULL(StdLibDecimal,
                          PyObject_GetAttrString(decimal, "Decimal"));
    Py_CLEAR(decimal);

    /* Import from math */
    PyObject *math = NULL;
    ASSIGN_AND_CHECK_NULL(math, PyImport_ImportModule("math"));
    ASSIGN_AND_CHECK_NULL(PyNumber_gcd, PyObject_GetAttrString(math, "gcd"));
    Py_CLEAR(math);

    /* PyLong methods */
    ASSIGN_AND_CHECK_NULL(PyLong_bit_length,
                          PyObject_GetAttrString((PyObject *)&PyLong_Type,
                                                 "bit_length"));
    /* Import from rounding */
    PyObject *rounding = NULL;
    ASSIGN_AND_CHECK_NULL(rounding,
                          PyImport_ImportModule("decimalfp.rounding"));
    ASSIGN_AND_CHECK_NULL(EnumRounding,
                          PyObject_GetAttrString(rounding,
                                                 EnumRounding_name));
    Py_CLEAR(rounding);

    /* Init libfpdec memory handlers */
    fpdec_mem_alloc = PyMem_Calloc;
    fpdec_mem_free = PyMem_Free;

    /* Init global Python constants */
    PyZERO = PyLong_FromLong(0L);
    PyONE = PyLong_FromLong(1L);
    PyTEN = PyLong_FromLong(10L);
    PyRADIX = PyLong_FromUnsignedLong(RADIX);
    PyObject *Py2pow32 = PyLong_FromLong(4294967296L);
    Py2pow64 = PyNumber_Multiply(Py2pow32, Py2pow32);
    Py_CLEAR(Py2pow32);

    /* Init global vars */
    MAX_DEC_PRECISION = PyLong_FromLong(FPDEC_MAX_DEC_PREC);
    PYMOD_ADD_OBJ(module, "MAX_DEC_PRECISION", MAX_DEC_PRECISION);

    /* Add types */
    ASSIGN_AND_CHECK_NULL(DecimalType,
                          (PyTypeObject *)PyType_FromSpec(&DecimalType_spec));
    PYMOD_ADD_OBJ(module, "Decimal", (PyObject *)DecimalType);
    PYMOD_ADD_OBJ(module, EnumRounding_name, EnumRounding);

    /* Register Decimal as Rational */
    ASSIGN_AND_CHECK_NULL(DecimalType,
                          (PyTypeObject *)PyObject_CallMethod(Rational,
                                                              "register", "O",
                                                              DecimalType));

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
    Py_CLEAR(EnumRounding);
    Py_CLEAR(PyNumber_gcd);
    Py_CLEAR(PyLong_bit_length);
    Py_CLEAR(PyZERO);
    Py_CLEAR(PyONE);
    Py_CLEAR(PyTEN);
    Py_CLEAR(PyRADIX);
    Py_CLEAR(Py2pow64);
    Py_CLEAR(MAX_DEC_PRECISION);
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
    cdecimalfp_methods,                /* m_methods */
    cdecimalfp_slots,                   /* m_slots */
    NULL,                               /* m_traverse */
    NULL,                               /* m_clear */
    NULL                                /* m_free */
};

PyMODINIT_FUNC
PyInit__cdecimalfp(void) {
    return PyModuleDef_Init(&cdecimalfp_module);
}

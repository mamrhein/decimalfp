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
#include "libfpdec/compiler_macros.h"

// Macro defs to be compatible with Python 3.6 (incl PyPy3):

#ifndef Py_UNREACHABLE
#define Py_UNREACHABLE() abort()
#endif

#ifndef Py_RETURN_RICHCOMPARE
#define Py_RETURN_RICHCOMPARE(val1, val2, op)                               \
    do {                                                                    \
        switch (op) {                                                       \
        case Py_EQ: if ((val1) == (val2)) Py_RETURN_TRUE; Py_RETURN_FALSE;  \
        case Py_NE: if ((val1) != (val2)) Py_RETURN_TRUE; Py_RETURN_FALSE;  \
        case Py_LT: if ((val1) < (val2)) Py_RETURN_TRUE; Py_RETURN_FALSE;   \
        case Py_GT: if ((val1) > (val2)) Py_RETURN_TRUE; Py_RETURN_FALSE;   \
        case Py_LE: if ((val1) <= (val2)) Py_RETURN_TRUE; Py_RETURN_FALSE;  \
        case Py_GE: if ((val1) >= (val2)) Py_RETURN_TRUE; Py_RETURN_FALSE;  \
        default:                                                            \
            Py_UNREACHABLE();                                               \
        }                                                                   \
    } while (0)
#endif

// In Python 3.8 PyMem_Calloc has been removed from the stable C-API and
// is no longer included in Python.h

#ifndef PyMem_Calloc

void *
PyMem_Calloc(size_t nelem, size_t elsize) {
    if (elsize != 0 && nelem > UINT64_MAX / elsize)
        return NULL;
    return PyMem_Malloc(nelem * elsize);
}

#endif

// Macros to simplify error checking

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

static PyObject *
value_error_ptr(const char *msg) {
    PyErr_SetString(PyExc_ValueError, msg);
    return NULL;
}

static PyObject *
type_error_ptr(const char *msg) {
    PyErr_SetString(PyExc_TypeError, msg);
    return NULL;
}

#define CHECK_FPDEC_ERROR(rc)                                               \
    do {                                                                    \
    switch (rc) {                                                           \
        case FPDEC_OK:                                                      \
            break;                                                          \
        case ENOMEM:                                                        \
            PyErr_NoMemory();                               \
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
        case FPDEC_INVALID_FORMAT:                                          \
            PyErr_SetString(PyExc_ValueError, "Invalid format spec.");      \
            goto ERROR;                                                     \
        case FPDEC_INCOMPAT_LOCALE:                                         \
            PyErr_SetString(PyExc_ValueError, "Incompatible locale.");      \
            goto ERROR;                                                     \
        default:                                                            \
            PyErr_SetString(PyExc_SystemError, "Unknown error code.");      \
            goto ERROR;                                                     \
    }} while (0)


// *** Python number constants ***

static PyObject *PyZERO = NULL;
static PyObject *PyONE = NULL;
static PyObject *PyTEN = NULL;
static PyObject *Py64 = NULL;
static PyObject *PyRADIX = NULL;
static PyObject *PyUInt64Max = NULL;
static PyObject *Py2pow64 = NULL;
static PyObject *MAX_DEC_PRECISION = NULL;

static PyObject *ZERO = NULL;
static PyObject *ONE = NULL;

// *** Helper prototypes ***

static inline PyObject *
PyLong_from_u128(const uint128_t *ui);

static PyObject *
PyLong_from_digits(const fpdec_digit_t *digits,
                   fpdec_n_digits_t n_digits,
                   uint8_t n_dec_adjust);

static PyObject *
PyLong_from_fpdec(const fpdec_t *fpdec);

static int64_t
fpdec_dec_coeff_exp(PyObject **coeff, const fpdec_t *fpdec);

static void
fpdec_as_integer_ratio(PyObject **numerator, PyObject **denominator,
                       const fpdec_t *fpdec);

static inline size_t
n_digits_needed(size_t n, uint64_t fb, uint64_t tb);

static error_t
fpdec_from_pylong(fpdec_t *fpdec, PyObject *val);

static enum FPDEC_ROUNDING_MODE
py_rnd_2_fpdec_rnd(PyObject *py_rnd);

/*============================================================================
* Decimal type
* ==========================================================================*/

typedef struct fpdec_object {
    PyObject_HEAD
    Py_hash_t hash;
    PyObject *numerator;
    PyObject *denominator;
    fpdec_t fpdec;
} DecimalObject;

static PyTypeObject *DecimalType;

// Method prototypes

static PyObject *
Decimal_as_fraction(DecimalObject *self, PyObject *args UNUSED);

// Type checks

static inline int
Decimal_Check_Exact(PyObject *obj) {
    return (Py_TYPE(obj) == DecimalType);
}

static inline int
Decimal_Check(PyObject *obj) {
    return PyObject_TypeCheck(obj, DecimalType);
}

// Constructors / destructors

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
    DecimalObject *name = DecimalType_alloc(type); \
    do {if (name == NULL) return NULL; } while (0)

#define DECIMAL_ALLOC_SELF(type) \
    DECIMAL_ALLOC(type, self)

static PyObject *
DecimalType_from_decimal(PyTypeObject *type, PyObject *val,
                         long adjust_to_prec) {
    error_t rc;

    if (type == DecimalType && (adjust_to_prec == -1 ||
                                ((DecimalObject *)val)->fpdec.dec_prec ==
                                adjust_to_prec)) {
        // val is a direct instance of DecimalType, a direct instance of
        // DecinalType is wanted and there's no need to adjust the result,
        // so just return the given instance (ref count increased)
        Py_INCREF(val);
        return val;
    }

    DECIMAL_ALLOC_SELF(type);
    rc = fpdec_adjusted(&self->fpdec, &((DecimalObject *)val)->fpdec,
                        adjust_to_prec, FPDEC_ROUND_DEFAULT);
    CHECK_FPDEC_ERROR(rc);
    return (PyObject *)self;

ERROR:
    Decimal_dealloc(self);
    return NULL;
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
    ASSIGN_AND_CHECK_NULL(dec, DecimalType_from_num_den(type, numerator,
                                                        denominator,
                                                        adjust_to_prec));
    goto CLEAN_UP;

ERROR:
    {
        PyObject *err = PyErr_Occurred();
        assert(err);
        if (err == PyExc_ValueError) {
            PyErr_Clear();
            PyErr_Format(PyExc_ValueError,
                         "Can't convert %R exactly to Decimal.",
                         val);
        }
    }

CLEAN_UP:
    Py_XDECREF(numerator);
    Py_XDECREF(denominator);
    return dec;
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
    if (PyLong_Check(val)) // NOLINT(hicpp-signed-bitwise)
        return DecimalType_from_pylong(type, val, -1);
    return PyErr_Format(PyExc_TypeError, "%R is not a float or int.", val);
}

static PyObject *
DecimalType_from_decimal_or_int(PyTypeObject *type, PyObject *val) {
    if (Decimal_Check(val))
        return DecimalType_from_decimal(type, val, -1);
    if (PyObject_IsInstance(val, StdLibDecimal))
        return DecimalType_from_stdlib_decimal(type, val, -1);
    if (PyLong_Check(val)) // NOLINT(hicpp-signed-bitwise)
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
    if (PyUnicode_Check(obj)) // NOLINT(hicpp-signed-bitwise)
        return DecimalType_from_str(type, obj, adjust_to_prec);

    // Python <int>
    if (PyLong_Check(obj)) // NOLINT(hicpp-signed-bitwise)
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

// Helper macros

#define DECIMAL_ALLOC_RESULT(type) \
    DECIMAL_ALLOC(type, res)

#define BINOP_DEC_TYPE(x, y)            \
    PyTypeObject *dec_type =            \
        Decimal_Check(x) ? Py_TYPE(x) : \
        (Decimal_Check(y) ? Py_TYPE(y) : NULL)

#define BINOP_ALLOC_RESULT(type)      \
    DECIMAL_ALLOC(type, dec);         \
    PyObject *res = (PyObject *) dec

#define CONVERT_AND_CHECK(fpdec, tmp, num)         \
    do {                                           \
    fpdec = fpdec_from_number(tmp, num);           \
    if (fpdec == NULL) {                           \
        if (PyErr_Occurred())                      \
            goto ERROR;                            \
        else if (PyObject_IsInstance(num, Number)) \
            goto FALLBACK;                         \
        else  {                                    \
            res = Py_NotImplemented;               \
            Py_INCREF(res);                        \
            goto CLEAN_UP;                         \
        }                                          \
    }} while (0)

// Properties

static PyObject *
Decimal_precision_get(DecimalObject *self, void *closure UNUSED) {
    long prec = FPDEC_DEC_PREC(&self->fpdec);
    return PyLong_FromLong(prec);
}

static PyObject *
Decimal_magnitude_get(DecimalObject *self, void *closure UNUSED) {
    long magn = fpdec_magnitude(&self->fpdec);
    if (magn == -1 && errno != 0) {
        PyErr_SetString(PyExc_OverflowError, "Result would be '-Infinity'.");
        errno = 0;
        return NULL;
    }
    return PyLong_FromLong(magn);
}

static PyObject *
Decimal_numerator_get(DecimalObject *self, void *closure UNUSED) {
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
Decimal_denominator_get(DecimalObject *self, void *closure UNUSED) {
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
Decimal_real_get(DecimalObject *self, void *closure UNUSED) {
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *
Decimal_imag_get(DecimalObject *self UNUSED, void *closure UNUSED) {
    Py_INCREF(PyZERO);
    return PyZERO;
}

// String representation

static PyObject *
Decimal_bytes(DecimalObject *self, PyObject *args UNUSED) {
    PyObject *res = NULL;
    char *lit = fpdec_as_ascii_literal(&self->fpdec, false);

    if (lit == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    res = PyBytes_FromString(lit);
    fpdec_mem_free(lit);
    return res;
}

static PyObject *
Decimal_str(DecimalObject *self) {
    PyObject *res = NULL;
    char *lit = fpdec_as_ascii_literal(&self->fpdec, false);

    if (lit == NULL) {
        PyErr_NoMemory();
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
        PyErr_NoMemory();
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
    PyObject *res = NULL;
    PyObject *utf8_fmt_spec = NULL;
    uint8_t *formatted = NULL;

    ASSIGN_AND_CHECK_NULL(utf8_fmt_spec, PyUnicode_AsUTF8String(fmt_spec));
    formatted = fpdec_formatted(&self->fpdec,
                                (uint8_t *)PyBytes_AsString(utf8_fmt_spec));
    if (formatted == NULL)
        CHECK_FPDEC_ERROR(errno);
    ASSIGN_AND_CHECK_NULL(res, PyUnicode_FromString((char *)formatted));
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(utf8_fmt_spec);
    fpdec_mem_free(formatted);
    return res;
}

// Special methods

static Py_hash_t
Decimal_hash(DecimalObject *self) {
    if (self->hash == -1) {
        if (PyObject_RichCompareBool(Decimal_denominator_get(self, NULL),
                                     PyONE, Py_EQ))
            self->hash = PyObject_Hash(Decimal_numerator_get(self, NULL));
        else
            self->hash = PyObject_Hash(Decimal_as_fraction(self, NULL));
    }
    return self->hash;
}

static PyObject *
Decimal_copy(DecimalObject *self, PyObject *args UNUSED) {
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *
Decimal_deepcopy(DecimalObject *self, PyObject *memo UNUSED) {
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *
Decimal_cmp_to_int(DecimalObject *x, PyObject *y, int op) {
    PyObject *res = NULL;
    PyObject *num = NULL;
    PyObject *den = NULL;
    PyObject *t = NULL;

    ASSIGN_AND_CHECK_NULL(num, Decimal_numerator_get(x, NULL));
    ASSIGN_AND_CHECK_NULL(den, Decimal_denominator_get(x, NULL));
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

    ASSIGN_AND_CHECK_NULL(x_num, Decimal_numerator_get(x, NULL));
    ASSIGN_AND_CHECK_NULL(x_den, Decimal_denominator_get(x, NULL));
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
    if (PyLong_Check(other)) // NOLINT(hicpp-signed-bitwise)
        return Decimal_cmp_to_int(self, other, op);

    // Integral
    if (PyObject_IsInstance(other, Integral)) {
        ASSIGN_AND_CHECK_NULL(t, PyNumber_Long(other));
        ASSIGN_AND_CHECK_NULL(res, Decimal_cmp_to_int(self, t, op));
        goto CLEAN_UP;
    }

    // Rational
    if (PyObject_IsInstance(other, Rational)) {
        ASSIGN_AND_CHECK_NULL(t, Decimal_as_fraction(self, NULL));
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

// Unary number methods

static PyObject *
Decimal_neg(PyObject *x) {
    DECIMAL_ALLOC(Py_TYPE(x), dec);
    fpdec_t *x_fpdec = &((DecimalObject *)x)->fpdec;
    error_t rc;

    if (FPDEC_EQ_ZERO(x_fpdec)) {
        Py_INCREF(x);
        return x;
    }

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
    DECIMAL_ALLOC(Py_TYPE(x), dec);
    fpdec_t *x_fpdec = &((DecimalObject *)x)->fpdec;
    error_t rc;

    if (FPDEC_SIGN(x_fpdec) != FPDEC_SIGN_NEG) {
        Py_INCREF(x);
        return x;
    }

    rc = fpdec_copy(&dec->fpdec, x_fpdec);
    CHECK_FPDEC_ERROR(rc);
    dec->fpdec.sign = 1;
    return (PyObject *)dec;

ERROR:
    Py_XDECREF(dec);
    return NULL;
}

static PyObject *
Decimal_int(DecimalObject *x, PyObject *args UNUSED) {
    fpdec_t *fpdec = &x->fpdec;
    return PyLong_from_fpdec(fpdec);
}

static PyObject *
Decimal_float(DecimalObject *x) {
    PyObject *res = NULL;
    PyObject *num = NULL;
    PyObject *den = NULL;

    ASSIGN_AND_CHECK_NULL(num, Decimal_numerator_get(x, NULL));
    ASSIGN_AND_CHECK_NULL(den, Decimal_denominator_get(x, NULL));
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

// Binary number methods

static fpdec_t *
fpdec_from_number(fpdec_t *tmp, PyObject *obj) {
    PyObject *ratio = NULL;
    PyObject *num = NULL;
    PyObject *den = NULL;
    error_t rc;
    fpdec_t *fpdec = NULL;

    if (Decimal_Check(obj)) {
        fpdec = &((DecimalObject *)obj)->fpdec;
    }
    else if (PyLong_Check(obj)) { // NOLINT(hicpp-signed-bitwise)
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
    else if (PyObject_HasAttrString((PyObject *)Py_TYPE(obj), "as_integer_ratio")) {
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
            PyErr_Format(PyExc_ValueError, "Unsupported operand: %R.", obj);
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
    error_t rc;
    fpdec_t tmp_x = FPDEC_ZERO;
    fpdec_t tmp_y = FPDEC_ZERO;
    fpdec_t q = FPDEC_ZERO;
    fpdec_t *r = &dec->fpdec;
    fpdec_t *fpx, *fpy;

    CONVERT_AND_CHECK(fpx, &tmp_x, x);
    CONVERT_AND_CHECK(fpy, &tmp_y, y);

    rc = fpdec_divmod(&q, r, fpx, fpy);
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
    DecimalObject *rem = NULL;
    error_t rc;
    fpdec_t tmp_x = FPDEC_ZERO;
    fpdec_t tmp_y = FPDEC_ZERO;
    fpdec_t q = FPDEC_ZERO;
    fpdec_t *fpx, *fpy;

    CONVERT_AND_CHECK(fpx, &tmp_x, x);
    CONVERT_AND_CHECK(fpy, &tmp_y, y);
    ASSIGN_AND_CHECK_NULL(rem, DecimalType_alloc(dec_type));

    rc = fpdec_divmod(&q, &rem->fpdec, fpx, fpy);
    if (rc != FPDEC_OK)
        goto FALLBACK;
    ASSIGN_AND_CHECK_NULL(quot, PyLong_from_fpdec(&q));
    ASSIGN_AND_CHECK_NULL(res, PyTuple_Pack(2, quot, rem));
    goto CLEAN_UP;

FALLBACK:
    ASSIGN_AND_CHECK_OK(res, fallback_op(x, y, PyNumber_Divmod));

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    fpdec_reset_to_zero(&tmp_x, 0);
    fpdec_reset_to_zero(&tmp_y, 0);
    fpdec_reset_to_zero(&q, 0);
    Py_XDECREF(quot);
    Py_XDECREF(rem);
    return res;
}

static PyObject *
Decimal_floordiv(PyObject *x, PyObject *y) {
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
    fpdec_reset_to_zero(&q, 0);
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

// Ternary number methods

static PyObject *
dec_pow_pylong(DecimalObject *x, PyObject *exp) {
    PyObject *res = NULL;
    PyObject *f = NULL;
    DecimalObject *dec = NULL;

    if (PyObject_RichCompareBool(exp, PyZERO, Py_EQ) == 1) {
        ASSIGN_AND_CHECK_NULL(dec, DecimalType_alloc(Py_TYPE(x)));
        fpdec_copy(&dec->fpdec, &FPDEC_ONE);
        res = (PyObject *)dec;
    }
    else {
        ASSIGN_AND_CHECK_NULL(f, Decimal_as_fraction(x, NULL));
        ASSIGN_AND_CHECK_NULL(res, PyNumber_Power(f, exp, Py_None));
        // try to convert result back to Decimal
        dec = (DecimalObject *)DecimalType_from_rational(Py_TYPE(x), res, -1);
        if (dec == NULL) {
            // result is not convertable to a Decimal, so return Fraction
            PyErr_Clear();
        }
        else {
            Py_CLEAR(res);
            res = (PyObject *)dec;
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

    ASSIGN_AND_CHECK_NULL(den, Decimal_denominator_get(y, NULL));
    if (PyObject_RichCompareBool(den, PyONE, Py_EQ) == 1) {
        ASSIGN_AND_CHECK_NULL(num, Decimal_numerator_get(y, NULL));
        ASSIGN_AND_CHECK_NULL(res, PyNumber_Power(x, num, Py_None));
    }
    else {
        ASSIGN_AND_CHECK_NULL(f, PyNumber_Float((PyObject *)y));
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

// Converting methods

#define DEF_N_CONV_RND_MODE(rounding)                            \
    enum FPDEC_ROUNDING_MODE rnd = py_rnd_2_fpdec_rnd(rounding); \
    if (rnd > FPDEC_MAX_ROUNDING_MODE)                                               \
        goto ERROR

static PyObject *
Decimal_adj_to_prec(DecimalObject *self, PyObject *precision,
                    PyObject *rounding) {
    PyTypeObject *dec_type = Py_TYPE(self);
    DECIMAL_ALLOC_RESULT(dec_type);
    error_t rc;
    PyObject *pylong_prec = NULL;
    long prec;

    if (precision == Py_None) {
        rc = fpdec_copy(&res->fpdec, &self->fpdec);
        CHECK_FPDEC_ERROR(rc);
        rc = fpdec_normalize_prec(&res->fpdec);
        CHECK_FPDEC_ERROR(rc);
        goto CLEAN_UP;
    }

    if (PyLong_Check(precision)) { // NOLINT(hicpp-signed-bitwise)
        Py_INCREF(precision);
        pylong_prec = precision;
    }
    else if (PyObject_IsInstance(precision, Integral))
        ASSIGN_AND_CHECK_NULL(pylong_prec, PyNumber_Long(precision));
    else {
        PyErr_SetString(PyExc_TypeError,
                        "Precision must be of type 'Integral'.");
        goto ERROR;
    }
    prec = PyLong_AsLong(pylong_prec);
    if (PyErr_Occurred())
        goto ERROR;

    if (prec < -FPDEC_MAX_DEC_PREC || prec > FPDEC_MAX_DEC_PREC) {
        PyErr_Format(PyExc_ValueError, "Precision limit exceed: %ld", prec);
        goto ERROR;
    }

    DEF_N_CONV_RND_MODE(rounding);
    rc = fpdec_adjusted(&res->fpdec, &self->fpdec, prec, rnd);
    CHECK_FPDEC_ERROR(rc);

    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());
    Py_CLEAR(res);

CLEAN_UP:
    Py_XDECREF(pylong_prec);
    return (PyObject *)res;
}

static PyObject *
Decimal_adjusted(DecimalObject *self, PyObject *args, PyObject *kwds) {
    static char *kw_names[] = {"precision", "rounding", NULL};
    PyObject *precision = Py_None;
    PyObject *rounding = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OO", kw_names,
                                     &precision, &rounding))
        return NULL;

    return Decimal_adj_to_prec(self, precision, rounding);
}

static PyObject *
Decimal_quantize(DecimalObject *self, PyObject *args, PyObject *kwds) {
    static char *kw_names[] = {"quant", "rounding", NULL};
    PyObject *quant = Py_None;
    PyObject *rounding = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|O", kw_names, &quant,
                                     &rounding))
        return NULL;

    BINOP_ALLOC_RESULT(Py_TYPE(self));
    PyObject *frac_quant = NULL;
    PyObject *t = NULL;
    PyObject *num = NULL;
    PyObject *den = NULL;
    fpdec_t *fpz = &dec->fpdec;
    error_t rc;
    fpdec_t tmp_q = FPDEC_ZERO;
    fpdec_t *fp_quant;
    DEF_N_CONV_RND_MODE(rounding);

    fp_quant = fpdec_from_number(&tmp_q, quant);
    if (fp_quant == NULL) {
        if (PyErr_Occurred()) {
            PyErr_Format(PyExc_ValueError, "Can't quantize to '%R'.",
                         quant);
            goto ERROR;
        }
        else if (PyObject_IsInstance(quant, Real) ||
                 PyObject_IsInstance(quant, StdLibDecimal))
            goto FALLBACK;
        else {
            PyErr_Format(PyExc_TypeError, "Can't quantize to a '%S': %S.",
                         Py_TYPE(quant), quant);
            goto ERROR;
        }
    }
    rc = fpdec_quantized(fpz, &self->fpdec, fp_quant, rnd);
    if (rc == FPDEC_OK)
        goto CLEAN_UP;

FALLBACK:
    fpdec_reset_to_zero(&dec->fpdec, 0);
    res = NULL;
    ASSIGN_AND_CHECK_NULL(frac_quant,
                          PyObject_CallFunctionObjArgs(Fraction, quant, NULL));
    ASSIGN_AND_CHECK_NULL(num, PyObject_GetAttrString(frac_quant,
                                                      "numerator"));
    ASSIGN_AND_CHECK_NULL(den, PyObject_GetAttrString(frac_quant,
                                                      "denominator"));
    ASSIGN_AND_CHECK_NULL(t, Decimal_mul((PyObject *)self, den));
    tmp_q = FPDEC_ZERO;
    rc = fpdec_from_pylong(&tmp_q, num);
    CHECK_FPDEC_ERROR(rc);
    rc = fpdec_div(&dec->fpdec, &((DecimalObject *)t)->fpdec, &tmp_q, 0, rnd);
    CHECK_FPDEC_ERROR(rc);
    ASSIGN_AND_CHECK_NULL(res, Decimal_mul((PyObject *)dec, frac_quant));
    Py_CLEAR(dec);
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());
    Py_CLEAR(dec);
    res = NULL;

CLEAN_UP:
    fpdec_reset_to_zero(&tmp_q, 0);
    Py_XDECREF(frac_quant);
    Py_XDECREF(t);
    Py_XDECREF(num);
    Py_XDECREF(den);
    return res;
}

static PyObject *
Decimal_as_tuple(DecimalObject *self, PyObject *args UNUSED) {
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
Decimal_as_fraction(DecimalObject *self, PyObject *args UNUSED) {
    PyObject *res = NULL;
    PyObject *num = NULL;
    PyObject *den = NULL;

    ASSIGN_AND_CHECK_NULL(num, Decimal_numerator_get(self, NULL));
    ASSIGN_AND_CHECK_NULL(den, Decimal_denominator_get(self, NULL));
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
Decimal_as_integer_ratio(DecimalObject *self, PyObject *args UNUSED) {
    PyObject *res = NULL;
    PyObject *num = NULL;
    PyObject *den = NULL;

    ASSIGN_AND_CHECK_NULL(num, Decimal_numerator_get(self, NULL));
    ASSIGN_AND_CHECK_NULL(den, Decimal_denominator_get(self, NULL));
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
Decimal_floor(DecimalObject *self, PyObject *args UNUSED) {
    PyObject *res = NULL;
    PyObject *num = NULL;
    PyObject *den = NULL;

    ASSIGN_AND_CHECK_NULL(num, Decimal_numerator_get(self, NULL));
    ASSIGN_AND_CHECK_NULL(den, Decimal_denominator_get(self, NULL));
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
Decimal_ceil(DecimalObject *self, PyObject *args UNUSED) {
    PyObject *res = NULL;
    PyObject *num = NULL;
    PyObject *den = NULL;
    PyObject *t = NULL;

    ASSIGN_AND_CHECK_NULL(num, Decimal_numerator_get(self, NULL));
    ASSIGN_AND_CHECK_NULL(den, Decimal_denominator_get(self, NULL));
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
Decimal_round(DecimalObject *self, PyObject *args, PyObject *kwds) {
    static char *kw_names[] = {"precision", NULL};
    PyObject *precision = Py_None;
    PyObject *adj = NULL;
    PyObject *res = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kw_names,
                                     &precision))
        return NULL;

    if (precision == Py_None) {
        ASSIGN_AND_CHECK_NULL(adj, Decimal_adj_to_prec(self, PyZERO,
                                                       Py_None));
        ASSIGN_AND_CHECK_NULL(res, Decimal_int((DecimalObject *)adj, NULL));
    }
    else
        ASSIGN_AND_CHECK_NULL(res, Decimal_adj_to_prec(self, precision,
                                                       Py_None));
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(adj);
    return res;
}

// Pickle helper

static PyObject *
Decimal_setstate(DecimalObject *self, PyObject *state) {
    char *buf = NULL;
    error_t rc;

    ASSIGN_AND_CHECK_NULL(buf, PyBytes_AsString(state));
    rc = fpdec_from_ascii_literal(&self->fpdec, buf);
    CHECK_FPDEC_ERROR(rc);
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_INCREF(Py_None);
    return Py_None;
}

// Decimal type spec

static PyGetSetDef Decimal_properties[] = {
    {"precision", (getter)Decimal_precision_get, 0,
     Decimal_precision_doc, 0},
    {"magnitude", (getter)Decimal_magnitude_get, 0,
     Decimal_magnitude_doc, 0},
    {"numerator", (getter)Decimal_numerator_get, 0,
     Decimal_numerator_doc, 0},
    {"denominator", (getter)Decimal_denominator_get, 0,
     Decimal_denominator_doc, 0},
    {"real", (getter)Decimal_real_get, 0,
     Decimal_real_doc, 0},
    {"imag", (getter)Decimal_imag_get, 0,
     Decimal_imag_doc, 0},
    {"__module__", (getter)Decimal_imag_get, 0,
     "Hack to avoid deprecation warning. Will be overwritten.\n\n", 0},
    {0, 0, 0, 0, 0}};

static PyMethodDef Decimal_methods[] = {
    /* class methods */
    {"from_float",
     (PyCFunction)DecimalType_from_float_or_int,
     METH_O | METH_CLASS, // NOLINT(hicpp-signed-bitwise)
     DecimalType_from_float_doc},
    {"from_decimal",
     (PyCFunction)DecimalType_from_decimal_or_int,
     METH_O | METH_CLASS, // NOLINT(hicpp-signed-bitwise)
     DecimalType_from_decimal_doc},
    {"from_real",
     (PyCFunction)(void *)(PyCFunctionWithKeywords)DecimalType_from_real,
     METH_CLASS | METH_VARARGS | METH_KEYWORDS, // NOLINT(hicpp-signed-bitwise)
     DecimalType_from_real_doc},
    // instance methods
    {"adjusted",
     (PyCFunction)(void *)(PyCFunctionWithKeywords)Decimal_adjusted,
     METH_VARARGS | METH_KEYWORDS, // NOLINT(hicpp-signed-bitwise)
     Decimal_adjusted_doc},
    {"quantize",
     (PyCFunction)(void *)(PyCFunctionWithKeywords)Decimal_quantize,
     METH_VARARGS | METH_KEYWORDS, // NOLINT(hicpp-signed-bitwise)
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
    {"__getstate__",
     (PyCFunction)Decimal_bytes,
     METH_NOARGS,
     Decimal_getstate_doc},
    {"__setstate__",
     (PyCFunction)Decimal_setstate,
     METH_O,
     Decimal_setstate_doc},
    {"__bytes__",
     (PyCFunction)Decimal_bytes,
     METH_NOARGS,
     Decimal_bytes_doc},
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
     METH_VARARGS | METH_KEYWORDS, // NOLINT(hicpp-signed-bitwise)
     Decimal_round_doc},
    {0, 0, 0, 0}
};

static PyType_Slot decimal_type_slots[] = {
    {Py_tp_doc, DecimalType_doc},
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
    "Decimal",                              /* name */
    sizeof(DecimalObject),                  /* basicsize */
    0,                                      /* itemsize */
    0,                                      /* flags */
    decimal_type_slots                      /* slots */
};

// *** Helper functions ***

static inline PyObject *
PyLong_10_pow_exp(const uint8_t exp) {
    assert(exp <= DEC_DIGITS_PER_DIGIT);
    return PyLong_FromUnsignedLong(u64_10_pow_n(exp));
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
PyLong_from_u128(const uint128_t *ui) {
    if (U128P_HI(ui) == 0)
        return PyLong_FromUnsignedLongLong(U128P_LO(ui));
    else
        return PyLong_from_u128_lo_hi(U128P_LO(ui), U128P_HI(ui));
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
        uint128_t shint = U128_RHS(fpdec->lo, fpdec->hi);
        fpdec_dec_prec_t prec = FPDEC_DEC_PREC(fpdec);
        if (prec > 0)
            u128_idiv_u64(&shint, u64_10_pow_n(prec));
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
    PyObject *hi, *lo;

    hi = PyNumber_Rshift(val, Py64);
    lo = PyNumber_And(val, PyUInt64Max);
    U128_FROM_LO_HI(&res, PyLong_AsUnsignedLongLong(lo),
                    PyLong_AsUnsignedLongLong(hi));
    Py_DECREF(hi);
    Py_DECREF(lo);
    return res;
}

static inline error_t
PyLong_as_digit_array(fpdec_digit_t *res, PyObject *val) {
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

    assert(PyLong_Check(val)); // NOLINT(hicpp-signed-bitwise)

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
        fpdec->lo = U128_LO(i);
        fpdec->hi = U128_HI(i);
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
        rc = PyLong_as_digit_array(digits, abs_val);
        Py_DECREF(abs_val);
        if (rc == FPDEC_OK)
            rc = fpdec_from_sign_digits_exp(fpdec, sign, n_digits, digits, 0);
        fpdec_mem_free(digits);
        return rc;
    }
}

/*============================================================================
* Enum ROUNDING type
* ==========================================================================*/

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
    long fpdec_rnd;
    PyObject *val = NULL;

    if (py_rnd == Py_None)
        return FPDEC_ROUND_DEFAULT;

    CHECK_TYPE(py_rnd, EnumRounding);
    ASSIGN_AND_CHECK_NULL(val, PyObject_GetAttrString(py_rnd, "value"));
    fpdec_rnd = PyLong_AsLong(val);
    if (fpdec_rnd < 1 || fpdec_rnd > FPDEC_MAX_ROUNDING_MODE) {
        goto ERROR;
    }
    goto CLEAN_UP;

ERROR:
    PyErr_Format(PyExc_TypeError, "Illegal rounding mode: %R", py_rnd);
    fpdec_rnd = FPDEC_MAX_ROUNDING_MODE + 1;

CLEAN_UP:
    Py_XDECREF(val);
    return (enum FPDEC_ROUNDING_MODE)fpdec_rnd;
}

/*============================================================================
* _cdecimalfp module
* ==========================================================================*/

static PyObject *
get_dflt_rounding_mode(PyObject *mod UNUSED, PyObject *args UNUSED) {
    enum FPDEC_ROUNDING_MODE dflt = fpdec_get_default_rounding_mode();
    return fpdec_rnd_2_py_rnd(dflt);
}

static PyObject *
set_dflt_rounding_mode(PyObject *mod UNUSED, PyObject *py_rnd) {
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

static PyTypeObject *
PyType_FromModuleAndSpecH(PyObject *module, PyType_Spec *spec) {
    // This is a hack to set the __module__ attribute of the created type!!!
    PyTypeObject *res = NULL;
    PyObject *mod_name = NULL;
    int rc;

    ASSIGN_AND_CHECK_NULL(mod_name, PyObject_GetAttrString(module,
                                                           "__name__"));
    ASSIGN_AND_CHECK_NULL(res, (PyTypeObject *)PyType_FromSpec(spec));
    rc = PyObject_SetAttrString((PyObject *)res, "__module__", mod_name);
    if (rc != 0) {
        Py_DECREF(res);
        goto ERROR;
    }
    goto CLEAN_UP;

ERROR:
    assert(PyErr_Occurred());

CLEAN_UP:
    Py_XDECREF(mod_name);
    return res;
}

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
    Py64 = PyLong_FromLong(64L);
    PyRADIX = PyLong_FromUnsignedLongLong(RADIX);
    PyUInt64Max = PyLong_FromUnsignedLongLong(UINT64_MAX);
    Py2pow64 = PyNumber_Lshift(PyONE, Py64);

    /* Init global vars */
    MAX_DEC_PRECISION = PyLong_FromLong(FPDEC_MAX_DEC_PREC);
    PYMOD_ADD_OBJ(module, "MAX_DEC_PRECISION", MAX_DEC_PRECISION);

    /* Add types */
    ASSIGN_AND_CHECK_NULL(DecimalType,
                          PyType_FromModuleAndSpecH(module,
                                                    &DecimalType_spec));
    PYMOD_ADD_OBJ(module, "Decimal", (PyObject *)DecimalType);
    PYMOD_ADD_OBJ(module, EnumRounding_name, EnumRounding);

    /* Register Decimal as Rational */
    ASSIGN_AND_CHECK_NULL(DecimalType,
                          (PyTypeObject *)PyObject_CallMethod(Rational,
                                                              "register", "O",
                                                              DecimalType));

    /* Add some Decimal constants */
    ZERO = DecimalType_from_pylong(DecimalType, PyZERO, -1);
    PYMOD_ADD_OBJ(module, "ZERO", ZERO);
    ONE = DecimalType_from_pylong(DecimalType, PyONE, -1);
    PYMOD_ADD_OBJ(module, "ONE", ONE);

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
    Py_CLEAR(Py64);
    Py_CLEAR(PyRADIX);
    Py_CLEAR(PyUInt64Max);
    Py_CLEAR(Py2pow64);
    Py_CLEAR(MAX_DEC_PRECISION);
    Py_CLEAR(ZERO);
    Py_CLEAR(ONE);
    return -1;
}

static PyModuleDef_Slot cdecimalfp_slots[] = {
    {Py_mod_exec, cdecimalfp_exec},
    {0, NULL}
};

static struct PyModuleDef cdecimalfp_module = {
    PyModuleDef_HEAD_INIT,              /* m_base */
    "decimalfp",                        /* m_name */
    cdecimalfp_doc,                     /* m_doc */
    0,                                  /* m_size */
    cdecimalfp_methods,                 /* m_methods */
    cdecimalfp_slots,                   /* m_slots */
    NULL,                               /* m_traverse */
    NULL,                               /* m_clear */
    NULL                                /* m_free */
};

PyMODINIT_FUNC
PyInit__cdecimalfp(void) {
    return PyModuleDef_Init(&cdecimalfp_module);
}

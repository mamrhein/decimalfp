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

// Abstract number types

static PyObject *Number = NULL;
static PyObject *Complex = NULL;
static PyObject *Real = NULL;
static PyObject *Rational = NULL;
static PyObject *Integral = NULL;

// Concrete number types

static PyObject *Fraction = NULL;
static PyObject *StdLibDecimal = NULL;

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

// *** Helper prototypes ***

static void *
fpdec_as_dec_coeff_exp(PyObject *coeff, PyObject *exp, const fpdec_t *fpdec);

static void *
fpdec_as_integer_ratio(PyObject *numerator, PyObject *denominator,
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

//static PyObject *
//DecimalType_FromCString(PyTypeObject *type, const char *s,
//                      PyObject *context)
//{
//    PyObject *dec;
//    uint32_t status = 0;
//
//    dec = DecimalType_new(type);
//    if (dec == NULL) {
//        return NULL;
//    }
//
//    mpd_qset_string(MPD(dec), s, CTX(context), &status);
//    if (Decimal_addstatus(context, status)) {
//        Py_DECREF(dec);
//        return NULL;
//    }
//    return dec;
//}
//
//
///* Return a new DecimalObject or a subtype from a PyUnicodeObject. */
//static PyObject *
//DecimalType_FromUnicode(PyTypeObject *type, const PyObject *u,
//                      PyObject *context)
//{
//    PyObject *dec;
//    char *s;
//
//    s = numeric_as_ascii(u, 0, 0);
//    if (s == NULL) {
//        return NULL;
//    }
//
//    dec = DecimalType_FromCString(type, s, context);
//    PyMem_Free(s);
//    return dec;
//}
//
//
///* Return a DecimalObject or a subtype from a PyFloatObject.
//   Conversion is exact. */
//static PyObject *
//DecimalType_FromFloatExact(PyTypeObject *type, PyObject *v,
//                         PyObject *context)
//{
//    PyObject *dec, *tmp;
//    PyObject *n, *d, *n_d;
//    mpd_ssize_t k;
//    double x;
//    int sign;
//    mpd_t *d1, *d2;
//    uint32_t status = 0;
//    mpd_context_t maxctx;
//
//
//    assert(PyType_IsSubtype(type, &DecimalType));
//
//    if (PyLong_Check(v)) {
//        return DecimalType_FromLongExact(type, v, context);
//    }
//    if (!PyFloat_Check(v)) {
//        PyErr_SetString(PyExc_TypeError,
//            "argument must be int or float");
//        return NULL;
//    }
//
//    x = PyFloat_AsDouble(v);
//    if (x == -1.0 && PyErr_Occurred()) {
//        return NULL;
//    }
//    sign = (copysign(1.0, x) == 1.0) ? 0 : 1;
//
//    if (Py_IS_NAN(x) || Py_IS_INFINITY(x)) {
//        dec = DecimalType_new(type);
//        if (dec == NULL) {
//            return NULL;
//        }
//        if (Py_IS_NAN(x)) {
//            /* decimal.py calls repr(float(+-nan)),
//             * which always gives a positive result. */
//            mpd_setspecial(MPD(dec), MPD_POS, MPD_NAN);
//        }
//        else {
//            mpd_setspecial(MPD(dec), sign, MPD_INF);
//        }
//        return dec;
//    }
//
//    /* absolute value of the float */
//    tmp = _py_float_abs(v);
//    if (tmp == NULL) {
//        return NULL;
//    }
//
//    /* float as integer ratio: numerator/denominator */
//    n_d = _py_float_as_integer_ratio(tmp, NULL);
//    Py_DECREF(tmp);
//    if (n_d == NULL) {
//        return NULL;
//    }
//    n = PyTuple_GET_ITEM(n_d, 0);
//    d = PyTuple_GET_ITEM(n_d, 1);
//
//    tmp = _py_long_bit_length(d, NULL);
//    if (tmp == NULL) {
//        Py_DECREF(n_d);
//        return NULL;
//    }
//    k = PyLong_AsSsize_t(tmp);
//    Py_DECREF(tmp);
//    if (k == -1 && PyErr_Occurred()) {
//        Py_DECREF(n_d);
//        return NULL;
//    }
//    k--;
//
//    dec = DecimalType_FromLongExact(type, n, context);
//    Py_DECREF(n_d);
//    if (dec == NULL) {
//        return NULL;
//    }
//
//    d1 = mpd_qnew();
//    if (d1 == NULL) {
//        Py_DECREF(dec);
//        PyErr_NoMemory();
//        return NULL;
//    }
//    d2 = mpd_qnew();
//    if (d2 == NULL) {
//        mpd_del(d1);
//        Py_DECREF(dec);
//        PyErr_NoMemory();
//        return NULL;
//    }
//
//    mpd_maxcontext(&maxctx);
//    mpd_qset_uint(d1, 5, &maxctx, &status);
//    mpd_qset_ssize(d2, k, &maxctx, &status);
//    mpd_qpow(d1, d1, d2, &maxctx, &status);
//    if (Decimal_addstatus(context, status)) {
//        mpd_del(d1);
//        mpd_del(d2);
//        Py_DECREF(dec);
//        return NULL;
//    }
//
//    /* result = n * 5**k */
//    mpd_qmul(MPD(dec), MPD(dec), d1, &maxctx, &status);
//    mpd_del(d1);
//    mpd_del(d2);
//    if (Decimal_addstatus(context, status)) {
//        Py_DECREF(dec);
//        return NULL;
//    }
//    /* result = +- n * 5**k * 10**-k */
//    mpd_set_sign(MPD(dec), sign);
//    MPD(dec)->exp = -k;
//
//    return dec;
//}
//
///* Return a new DecimalObject or a subtype from a Decimal. */
//static PyObject *
//DecimalType_FromDecimalExact(PyTypeObject *type, PyObject *v, PyObject
//*context)
//{
//    PyObject *dec;
//    uint32_t status = 0;
//
//    if (type == &DecimalType && Decimal_CheckExact(v)) {
//        Py_INCREF(v);
//        return v;
//    }
//
//    dec = DecimalType_new(type);
//    if (dec == NULL) {
//        return NULL;
//    }
//
//    mpd_qcopy(MPD(dec), MPD(v), &status);
//    if (Decimal_addstatus(context, status)) {
//        Py_DECREF(dec);
//        return NULL;
//    }
//
//    return dec;
//}
//
///* Special methods */

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
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_denominator_get(DecimalObject *self) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_real_get(DecimalObject *self) {
    return (PyObject *)self;
}

static PyObject *
Decimal_imag_get(DecimalObject *self) {
    return PyLong_FromLong(0L);
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
    PyObject *exp = NULL;
    PyObject *res = NULL;

    sign = PyLong_FromLong(FPDEC_SIGN(fpdec) = FPDEC_SIGN_NEG ? 1 : 0);


    res = PyTuple_Pack(3, sign, coeff, exp);
    Py_DECREF(sign);
    Py_DECREF(coeff);
    Py_DECREF(exp);
    return res;
}

static PyObject *
Decimal_as_fraction(DecimalObject *self) {
    Py_RETURN_NOTIMPLEMENTED;
}

static PyObject *
Decimal_as_integer_ratio(DecimalObject *self) {
    Py_RETURN_NOTIMPLEMENTED;
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
    if (PyObject_RichCompareBool(Decimal_numerator_get(self),
                                 PyLong_FromLong(1), Py_EQ))
        return PyObject_Hash(Decimal_numerator_get(self));
    else
        return PyObject_Hash(Decimal_as_fraction(self));
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


//
//static PyMethodDef Decimal_methods [] =
//{
//  /* Unary arithmetic functions, optional context arg */
//  { "exp", (PyCFunction)(void(*)(void))Decimal_mpd_qexp,
//    METH_VARARGS|METH_KEYWORDS, doc_exp },
//  { "ln", (PyCFunction)(void(*)(void))Decimal_mpd_qln,
//    METH_VARARGS|METH_KEYWORDS, doc_ln },
//  { "log10", (PyCFunction)(void(*)(void))Decimal_mpd_qlog10,
//    METH_VARARGS|METH_KEYWORDS, doc_log10 },
//  { "next_minus", (PyCFunction)(void(*)(void))Decimal_mpd_qnext_minus,
//    METH_VARARGS|METH_KEYWORDS, doc_next_minus },
//  { "next_plus", (PyCFunction)(void(*)(void))Decimal_mpd_qnext_plus,
//    METH_VARARGS|METH_KEYWORDS, doc_next_plus },
//  { "normalize", (PyCFunction)(void(*)(void))Decimal_mpd_qreduce,
//    METH_VARARGS|METH_KEYWORDS, doc_normalize },
//  { "to_integral", (PyCFunction)(void(*)(void))Decimal_ToIntegralValue,
//    METH_VARARGS|METH_KEYWORDS, doc_to_integral },
//  { "to_integral_exact", (PyCFunction)(void(*)(void))Decimal_ToIntegralExact,
//    METH_VARARGS|METH_KEYWORDS, doc_to_integral_exact },
//  { "to_integral_value", (PyCFunction)(void(*)(void))Decimal_ToIntegralValue,
//    METH_VARARGS|METH_KEYWORDS, doc_to_integral_value },
//  { "sqrt", (PyCFunction)(void(*)(void))Decimal_mpd_qsqrt,
//    METH_VARARGS|METH_KEYWORDS, doc_sqrt },
//
//  /* Binary arithmetic functions, optional context arg */
//  { "compare", (PyCFunction)(void(*)(void))Decimal_mpd_qcompare,
//    METH_VARARGS|METH_KEYWORDS, doc_compare },
//  { "compare_signal", (PyCFunction)(void(*)(void))
//  Decimal_mpd_qcompare_signal,
//    METH_VARARGS|METH_KEYWORDS, doc_compare_signal },
//  { "max", (PyCFunction)(void(*)(void))Decimal_mpd_qmax,
//    METH_VARARGS|METH_KEYWORDS, doc_max },
//  { "max_mag", (PyCFunction)(void(*)(void))Decimal_mpd_qmax_mag,
//    METH_VARARGS|METH_KEYWORDS, doc_max_mag },
//  { "min", (PyCFunction)(void(*)(void))Decimal_mpd_qmin,
//    METH_VARARGS|METH_KEYWORDS, doc_min },
//  { "min_mag", (PyCFunction)(void(*)(void))Decimal_mpd_qmin_mag,
//    METH_VARARGS|METH_KEYWORDS, doc_min_mag },
//  { "next_toward", (PyCFunction)(void(*)(void))Decimal_mpd_qnext_toward,
//    METH_VARARGS|METH_KEYWORDS, doc_next_toward },
//  { "quantize", (PyCFunction)(void(*)(void))Decimal_mpd_qquantize,
//    METH_VARARGS|METH_KEYWORDS, doc_quantize },
//  { "remainder_near", (PyCFunction)(void(*)(void))Decimal_mpd_qrem_near,
//    METH_VARARGS|METH_KEYWORDS, doc_remainder_near },
//
//  /* Ternary arithmetic functions, optional context arg */
//  { "fma", (PyCFunction)(void(*)(void))Decimal_mpd_qfma,
//    METH_VARARGS|METH_KEYWORDS, doc_fma },
//
//  /* Boolean functions, no context arg */
//  { "is_canonical", Decimal_mpd_iscanonical, METH_NOARGS, doc_is_canonical },
//  { "is_finite", Decimal_mpd_isfinite, METH_NOARGS, doc_is_finite },
//  { "is_infinite", Decimal_mpd_isinfinite, METH_NOARGS, doc_is_infinite },
//  { "is_nan", Decimal_mpd_isnan, METH_NOARGS, doc_is_nan },
//  { "is_qnan", Decimal_mpd_isqnan, METH_NOARGS, doc_is_qnan },
//  { "is_snan", Decimal_mpd_issnan, METH_NOARGS, doc_is_snan },
//  { "is_signed", Decimal_mpd_issigned, METH_NOARGS, doc_is_signed },
//  { "is_zero", Decimal_mpd_iszero, METH_NOARGS, doc_is_zero },
//
//  /* Boolean functions, optional context arg */
//  { "is_normal", (PyCFunction)(void(*)(void))Decimal_mpd_isnormal,
//    METH_VARARGS|METH_KEYWORDS, doc_is_normal },
//  { "is_subnormal", (PyCFunction)(void(*)(void))Decimal_mpd_issubnormal,
//    METH_VARARGS|METH_KEYWORDS, doc_is_subnormal },
//
//  /* Unary functions, no context arg */
//  { "adjusted", Decimal_mpd_adjexp, METH_NOARGS, doc_adjusted },
//  { "canonical", Decimal_canonical, METH_NOARGS, doc_canonical },
//  { "conjugate", Decimal_conjugate, METH_NOARGS, doc_conjugate },
//  { "radix", Decimal_mpd_radix, METH_NOARGS, doc_radix },
//
//  /* Unary functions, optional context arg for conversion errors */
//  { "copy_abs", Decimal_mpd_qcopy_abs, METH_NOARGS, doc_copy_abs },
//  { "copy_negate", Decimal_mpd_qcopy_negate, METH_NOARGS, doc_copy_negate },
//
//  /* Unary functions, optional context arg */
//  { "logb", (PyCFunction)(void(*)(void))Decimal_mpd_qlogb,
//    METH_VARARGS|METH_KEYWORDS, doc_logb },
//  { "logical_invert", (PyCFunction)(void(*)(void))Decimal_mpd_qinvert,
//    METH_VARARGS|METH_KEYWORDS, doc_logical_invert },
//  { "number_class", (PyCFunction)(void(*)(void))Decimal_mpd_class,
//    METH_VARARGS|METH_KEYWORDS, doc_number_class },
//  { "to_eng_string", (PyCFunction)(void(*)(void))Decimal_mpd_to_eng,
//    METH_VARARGS|METH_KEYWORDS, doc_to_eng_string },
//
//  /* Binary functions, optional context arg for conversion errors */
//  { "compare_total", (PyCFunction)(void(*)(void))Decimal_mpd_compare_total,
//    METH_VARARGS|METH_KEYWORDS, doc_compare_total },
//  { "compare_total_mag", (PyCFunction)(void(*)(void))
//  Decimal_mpd_compare_total_mag, METH_VARARGS|METH_KEYWORDS,
//  doc_compare_total_mag },
//  { "copy_sign", (PyCFunction)(void(*)(void))Decimal_mpd_qcopy_sign,
//    METH_VARARGS|METH_KEYWORDS, doc_copy_sign },
//  { "same_quantum", (PyCFunction)(void(*)(void))Decimal_mpd_same_quantum,
//    METH_VARARGS|METH_KEYWORDS, doc_same_quantum },
//
//  /* Binary functions, optional context arg */
//  { "logical_and", (PyCFunction)(void(*)(void))Decimal_mpd_qand,
//    METH_VARARGS|METH_KEYWORDS, doc_logical_and },
//  { "logical_or", (PyCFunction)(void(*)(void))Decimal_mpd_qor,
//    METH_VARARGS|METH_KEYWORDS, doc_logical_or },
//  { "logical_xor", (PyCFunction)(void(*)(void))Decimal_mpd_qxor,
//    METH_VARARGS|METH_KEYWORDS, doc_logical_xor },
//  { "rotate", (PyCFunction)(void(*)(void))Decimal_mpd_qrotate,
//    METH_VARARGS|METH_KEYWORDS, doc_rotate },
//  { "scaleb", (PyCFunction)(void(*)(void))Decimal_mpd_qscaleb,
//    METH_VARARGS|METH_KEYWORDS, doc_scaleb },
//  { "shift", (PyCFunction)(void(*)(void))Decimal_mpd_qshift,
//    METH_VARARGS|METH_KEYWORDS, doc_shift },
//
//  /* Miscellaneous */
//  { "from_float", DecimalType_from_float, METH_O|METH_CLASS, doc_from_float },
//  { "as_tuple", Decimal_AsTuple, METH_NOARGS, doc_as_tuple },
//  { "as_integer_ratio", Decimal_as_integer_ratio, METH_NOARGS,
//    doc_as_integer_ratio },
//
//  /* Special methods */
//  { "__copy__", Decimal_copy, METH_NOARGS, NULL },
//  { "__deepcopy__", Decimal_copy, METH_O, NULL },
//  { "__format__", Decimal_format, METH_VARARGS, NULL },
//  { "__reduce__", Decimal_reduce, METH_NOARGS, NULL },
//  { "__round__", Decimal_Round, METH_VARARGS, NULL },
//  { "__ceil__", Decimal_ceil, METH_NOARGS, NULL },
//  { "__floor__", Decimal_floor, METH_NOARGS, NULL },
//  { "__trunc__", Decimal_trunc, METH_NOARGS, NULL },
//  { "__complex__", Decimal_complex, METH_NOARGS, NULL },
//  { "__sizeof__", Decimal_sizeof, METH_NOARGS, NULL },
//
//  { NULL, NULL, 1 }
//};
//
//static PyTypeObject DecimalType =
//{
//    PyVarObject_HEAD_INIT(NULL, 0)
//    "decimal.Decimal",                      /* tp_name */
//    sizeof(DecimalObject),                    /* tp_basicsize */
//    0,                                      /* tp_itemsize */
//    (destructor) Decimal_dealloc,               /* tp_dealloc */
//    0,                                      /* tp_vectorcall_offset */
//    (getattrfunc) 0,                        /* tp_getattr */
//    (setattrfunc) 0,                        /* tp_setattr */
//    0,                                      /* tp_as_async */
//    (reprfunc) Decimal_repr,                    /* tp_repr */
//    &Decimal_number_methods,                    /* tp_as_number */
//    0,                                      /* tp_as_sequence */
//    0,                                      /* tp_as_mapping */
//    (hashfunc) Decimal_hash,                    /* tp_hash */
//    0,                                      /* tp_call */
//    (reprfunc) Decimal_str,                     /* tp_str */
//    (getattrofunc) PyObject_GenericGetAttr, /* tp_getattro */
//    (setattrofunc) 0,                       /* tp_setattro */
//    (PyBufferProcs *) 0,                    /* tp_as_buffer */
//    (Py_TPFLAGS_DEFAULT|
//     Py_TPFLAGS_BASETYPE),                  /* tp_flags */
//    doc_decimal,                            /* tp_doc */
//    0,                                      /* tp_traverse */
//    0,                                      /* tp_clear */
//    Decimal_richcompare,                        /* tp_richcompare */
//    0,                                      /* tp_weaklistoffset */
//    0,                                      /* tp_iter */
//    0,                                      /* tp_iternext */
//    Decimal_methods,                            /* tp_methods */
//    0,                                      /* tp_members */
//    Decimal_getsets,                            /* tp_getset */
//    0,                                      /* tp_base */
//    0,                                      /* tp_dict */
//    0,                                      /* tp_descr_get */
//    0,                                      /* tp_descr_set */
//    0,                                      /* tp_dictoffset */
//    0,                                      /* tp_init */
//    0,                                      /* tp_alloc */
//    Decimal_new,                                /* tp_new */
//    PyObject_Del,                           /* tp_free */
//};

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
    {Py_tp_new, DecimalType_new},
    {Py_tp_dealloc, Decimal_dealloc},
    {Py_tp_richcompare, Decimal_richcompare},
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

static PyLongObject *
digits_as_int(fpdec_digit_array_t *digits) {

}

static void *
fpdec_as_dec_coeff_exp(PyObject *coeff, PyObject *exp, const fpdec_t *fpdec) {

}

static void *
fpdec_as_integer_ratio(PyObject *numerator, PyObject *denominator,
                       const fpdec_t *fpdec) {

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

#define ASSIGN_AND_CHECK_NULL(result, expr) \
    do { result = (expr); if (result == NULL) goto ERROR; } while (0)

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

    /* Init libfpdec memory handlers */
    fpdec_mem_alloc = PyMem_Calloc;
    fpdec_mem_free = PyMem_Free;

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

/* ---------------------------------------------------------------------------
Name:        fpdec.hpp

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application. For license
             details please read the file LICENSE.TXT provided together
             with the application.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#ifndef FPDEC_FPDEC_HPP
#define FPDEC_FPDEC_HPP

#include <stdexcept>
#include <string>
#include "common.h"

namespace fpdec {

    class InternalLimitExceeded : public std::range_error {
    public:
        error_t error;

        InternalLimitExceeded(error_t err) :
            std::range_error("Internal limit exceeded."),
            error(err) {
        };
    };

    class InvalidDecimalLiteral : public std::invalid_argument {
    public:
        std::string invalid_literal;

        InvalidDecimalLiteral(const std::string lit) :
            std::invalid_argument("Invalid Decimal literal"),
            invalid_literal(lit) {
        };
    };

    class DivisionByZero : public std::range_error {
    public:
        DivisionByZero() :
            std::range_error("Division by zero.") {
        };
    };

    // The members of 'Rounding' must be kept in sync with FPDEC_ROUNDING
    // in rounding.h !!!

    enum class Rounding : int {
        // Use the rounding mode set as default ('round_half_even' if not set
        // explicitly)
        round_default = 0,
        // Round away from zero if last digit after rounding towards
        // zero would have been 0 or 5; otherwise round towards zero.
        round_05up,
        // Round towards Infinity.
        round_ceiling,
        // Round towards zero.
        round_down,
        // Round towards -Infinity.
        round_floor,
        // Round to nearest with ties going towards zero.
        round_half_down,
        // Round to nearest with ties going to nearest even integer.
        round_half_even,
        // Round to nearest with ties going away from zero.
        round_half_up,
        // Round away from zero.
        round_up,
    };

    class Decimal {
    public:
        Decimal() noexcept;
        Decimal(const Decimal &);
        Decimal(const Decimal &, const fpdec_dec_prec_t,
                const Rounding = Rounding::round_default);
        Decimal(Decimal &&) = default;
        Decimal(const std::string &);
        Decimal(const long long int) noexcept;
        ~Decimal();
        // properties
        fpdec_sign_t sign() const noexcept;
        fpdec_dec_prec_t precision() const noexcept;
        int magnitude() const;
        // operators
        Decimal &operator=(const Decimal &) = default;
        Decimal &operator=(Decimal &&) = default;
        Decimal operator+() const noexcept;
        Decimal operator-() const;
        bool operator==(const Decimal &) const noexcept;
        bool operator!=(const Decimal &) const noexcept;
        bool operator<=(const Decimal &) const noexcept;
        bool operator<(const Decimal &) const noexcept;
        bool operator>=(const Decimal &) const noexcept;
        bool operator>(const Decimal &) const noexcept;
        Decimal operator+(Decimal &);
        Decimal operator-(Decimal &);
        Decimal operator*(Decimal &);
        Decimal operator/(Decimal &);

    private:
        fpdec_t fpdec;
        Decimal(const fpdec_t *);
    };

    // interacting with integers
    bool operator==(const long long int, const Decimal &) noexcept;
    bool operator!=(const long long int, const Decimal &) noexcept;
    bool operator<=(const long long int, const Decimal &) noexcept;
    bool operator<(const long long int, const Decimal &) noexcept;
    bool operator>=(const long long int, const Decimal &) noexcept;
    bool operator>(const long long int, const Decimal &) noexcept;

}; // namespace fpdec

#endif //FPDEC_FPDEC_HPP

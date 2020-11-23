/* ---------------------------------------------------------------------------
Name:        compiler_macros.h

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application. For license
             details please read the file LICENSE.TXT provided together
             with the application.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#ifndef FPDEC_COMPILER_MACROS_H
#define FPDEC_COMPILER_MACROS_H

#if defined(__GNUC__) || defined(__clang__)
#define UNUSED __attribute__((unused))
#define FALLTHROUGH __attribute__((fallthrough))
#else
#define UNUSED
#define FALLTHROUGH
#endif

#endif //FPDEC_COMPILER_MACROS_H

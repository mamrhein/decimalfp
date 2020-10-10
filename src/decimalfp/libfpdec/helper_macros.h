/*
------------------------------------------------------------------------------
Name:        helper_macros.h

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application or library.
             For license details please read the file LICENSE provided
             together with the application or library.
------------------------------------------------------------------------------
$Source$
$Revision$
*/


#ifndef FPDEC_HELPER_MACROS_H
#define FPDEC_HELPER_MACROS_H

/*****************************************************************************
*  Macros
*****************************************************************************/

// error return
#define ERROR(err) {errno = err; return err;}
#define ERROR_RETVAL(err, retval) {errno = err; return retval;}
#define MEMERROR ERROR(ENOMEM)
#define MEMERROR_RETVAL(retval) ERROR_RETVAL(ENOMEM, retval)

#endif //FPDEC_HELPER_MACROS_H

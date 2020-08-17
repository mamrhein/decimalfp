/* ---------------------------------------------------------------------------
Name:        unicode_digits.h

Author:      Michael Amrhein (michael@adrhinum.de)

Copyright:   (c) 2020 ff. Michael Amrhein
License:     This program is part of a larger application. For license
             details please read the file LICENSE.TXT provided together
             with the application.
------------------------------------------------------------------------------
$Source$
$Revision$
*/

#ifndef FPDEC_UNICODE_DIGITS_H
#define FPDEC_UNICODE_DIGITS_H

#include <stddef.h>


struct digit_range {
    int digit0;
    int digit9;
};

static const struct digit_range lookup[] = {
    {0x00000660, 0x00000669}, // ARABIC-INDIC DIGITS
    {0x000006F0, 0x000006F9}, // EXTENDED ARABIC-INDIC DIGITS
    {0x000007C0, 0x000007C9}, // NKO DIGITS
    {0x00000966, 0x0000096F}, // DEVANAGARI DIGITS
    {0x000009E6, 0x000009EF}, // BENGALI DIGITS
    {0x00000A66, 0x00000A6F}, // GURMUKHI DIGITS
    {0x00000AE6, 0x00000AEF}, // GUJARATI DIGITS
    {0x00000B66, 0x00000B6F}, // ORIYA DIGITS
    {0x00000BE6, 0x00000BEF}, // TAMIL DIGITS
    {0x00000C66, 0x00000C6F}, // TELUGU DIGITS
    {0x00000CE6, 0x00000CEF}, // KANNADA DIGITS
    {0x00000D66, 0x00000D6F}, // MALAYALAM DIGITS
    {0x00000DE6, 0x00000DEF}, // SINHALA LITH DIGITS
    {0x00000E50, 0x00000E59}, // THAI DIGITS
    {0x00000ED0, 0x00000ED9}, // LAO DIGITS
    {0x00000F20, 0x00000F29}, // TIBETAN DIGITS
    {0x00001040, 0x00001049}, // MYANMAR DIGITS
    {0x00001090, 0x00001099}, // MYANMAR SHAN DIGITS
    {0x000017E0, 0x000017E9}, // KHMER DIGITS
    {0x00001810, 0x00001819}, // MONGOLIAN DIGITS
    {0x00001946, 0x0000194F}, // LIMBU DIGITS
    {0x000019D0, 0x000019D9}, // NEW TAI LUE DIGITS
    {0x00001A80, 0x00001A89}, // TAI THAM HORA DIGITS
    {0x00001A90, 0x00001A99}, // TAI THAM THAM DIGITS
    {0x00001B50, 0x00001B59}, // BALINESE DIGITS
    {0x00001BB0, 0x00001BB9}, // SUNDANESE DIGITS
    {0x00001C40, 0x00001C49}, // LEPCHA DIGITS
    {0x00001C50, 0x00001C59}, // OL CHIKI DIGITS
    {0x0000A620, 0x0000A629}, // VAI DIGITS
    {0x0000A8D0, 0x0000A8D9}, // SAURASHTRA DIGITS
    {0x0000A900, 0x0000A909}, // KAYAH LI DIGITS
    {0x0000A9D0, 0x0000A9D9}, // JAVANESE DIGITS
    {0x0000A9F0, 0x0000A9F9}, // MYANMAR TAI LAING DIGITS
    {0x0000AA50, 0x0000AA59}, // CHAM DIGITS
    {0x0000ABF0, 0x0000ABF9}, // MEETEI MAYEK DIGITS
    {0x0000FF10, 0x0000FF19}, // FULLWIDTH DIGITS
    {0x000104A0, 0x000104A9}, // OSMANYA DIGITS
    {0x00010D30, 0x00010D39}, // HANIFI ROHINGYA DIGITS
    {0x00011066, 0x0001106F}, // BRAHMI DIGITS
    {0x000110F0, 0x000110F9}, // SORA SOMPENG DIGITS
    {0x00011136, 0x0001113F}, // CHAKMA DIGITS
    {0x000111D0, 0x000111D9}, // SHARADA DIGITS
    {0x000112F0, 0x000112F9}, // KHUDAWADI DIGITS
    {0x00011450, 0x00011459}, // NEWA DIGITS
    {0x000114D0, 0x000114D9}, // TIRHUTA DIGITS
    {0x00011650, 0x00011659}, // MODI DIGITS
    {0x000116C0, 0x000116C9}, // TAKRI DIGITS
    {0x00011730, 0x00011739}, // AHOM DIGITS
    {0x000118E0, 0x000118E9}, // WARANG CITI DIGITS
    {0x00011950, 0x00011959}, // DIVES AKURU DIGITS
    {0x00011C50, 0x00011C59}, // BHAIKSUKI DIGITS
    {0x00011D50, 0x00011D59}, // MASARAM GONDI DIGITS
    {0x00011DA0, 0x00011DA9}, // GUNJALA GONDI DIGITS
    {0x00016A60, 0x00016A69}, // MRO DIGITS
    {0x00016B50, 0x00016B59}, // PAHAWH HMONG DIGITS
    {0x0001E140, 0x0001E149}, // NYIAKENG PUACHUE HMONG DIGITS
    {0x0001E2F0, 0x0001E2F9}, // WANCHO DIGITS
    {0x0001E950, 0x0001E959}, // ADLAM DIGITS
    {0,0}
};

static inline char
lookup_unicode_digit(wchar_t wch) {
    struct digit_range *range = (struct digit_range *)&lookup;

    for (; range->digit0 != 0; ++range) {
        if ((int)wch < range->digit0)
            return -1;
        if ((int)wch <= range->digit9)
            return '0' + (int)wch - range->digit0;
    }
    return -1;
}

#endif //FPDEC_UNICODE_DIGITS_H

/*    pp_pack.c
 *
 *    Copyright (c) 1991-2002, Larry Wall
 *
 *    You may distribute under the terms of either the GNU General Public
 *    License or the Artistic License, as specified in the README file.
 *
 */

#include "EXTERN.h"
#define PERL_IN_PP_PACK_C
#include "perl.h"

/*
 * The compiler on Concurrent CX/UX systems has a subtle bug which only
 * seems to show up when compiling pp.c - it generates the wrong double
 * precision constant value for (double)UV_MAX when used inline in the body
 * of the code below, so this makes a static variable up front (which the
 * compiler seems to get correct) and uses it in place of UV_MAX below.
 */
#ifdef CXUX_BROKEN_CONSTANT_CONVERT
static double UV_MAX_cxux = ((double)UV_MAX);
#endif

/*
 * Offset for integer pack/unpack.
 *
 * On architectures where I16 and I32 aren't really 16 and 32 bits,
 * which for now are all Crays, pack and unpack have to play games.
 */

/*
 * These values are required for portability of pack() output.
 * If they're not right on your machine, then pack() and unpack()
 * wouldn't work right anyway; you'll need to apply the Cray hack.
 * (I'd like to check them with #if, but you can't use sizeof() in
 * the preprocessor.)  --???
 */
/*
    The appropriate SHORTSIZE, INTSIZE, LONGSIZE, and LONGLONGSIZE
    defines are now in config.h.  --Andy Dougherty  April 1998
 */
#define SIZE16 2
#define SIZE32 4

/* CROSSCOMPILE and MULTIARCH are going to affect pp_pack() and pp_unpack().
   --jhi Feb 1999 */

#if SHORTSIZE != SIZE16 || LONGSIZE != SIZE32
#   define PERL_NATINT_PACK
#endif

#if LONGSIZE > 4 && defined(_CRAY)
#  if BYTEORDER == 0x12345678
#    define OFF16(p)	(char*)(p)
#    define OFF32(p)	(char*)(p)
#  else
#    if BYTEORDER == 0x87654321
#      define OFF16(p)	((char*)(p) + (sizeof(U16) - SIZE16))
#      define OFF32(p)	((char*)(p) + (sizeof(U32) - SIZE32))
#    else
       }}}} bad cray byte order
#    endif
#  endif
#  define COPY16(s,p)  (*(p) = 0, Copy(s, OFF16(p), SIZE16, char))
#  define COPY32(s,p)  (*(p) = 0, Copy(s, OFF32(p), SIZE32, char))
#  define COPYNN(s,p,n) (*(p) = 0, Copy(s, (char *)(p), n, char))
#  define CAT16(sv,p)  sv_catpvn(sv, OFF16(p), SIZE16)
#  define CAT32(sv,p)  sv_catpvn(sv, OFF32(p), SIZE32)
#else
#  define COPY16(s,p)  Copy(s, p, SIZE16, char)
#  define COPY32(s,p)  Copy(s, p, SIZE32, char)
#  define COPYNN(s,p,n) Copy(s, (char *)(p), n, char)
#  define CAT16(sv,p)  sv_catpvn(sv, (char*)(p), SIZE16)
#  define CAT32(sv,p)  sv_catpvn(sv, (char*)(p), SIZE32)
#endif

STATIC SV *
S_mul128(pTHX_ SV *sv, U8 m)
{
  STRLEN          len;
  char           *s = SvPV(sv, len);
  char           *t;
  U32             i = 0;

  if (!strnEQ(s, "0000", 4)) {  /* need to grow sv */
    SV             *tmpNew = newSVpvn("0000000000", 10);

    sv_catsv(tmpNew, sv);
    SvREFCNT_dec(sv);		/* free old sv */
    sv = tmpNew;
    s = SvPV(sv, len);
  }
  t = s + len - 1;
  while (!*t)                   /* trailing '\0'? */
    t--;
  while (t > s) {
    i = ((*t - '0') << 7) + m;
    *(t--) = '0' + (i % 10);
    m = i / 10;
  }
  return (sv);
}

/* Explosives and implosives. */

#if 'I' == 73 && 'J' == 74
/* On an ASCII/ISO kind of system */
#define ISUUCHAR(ch)    ((ch) >= ' ' && (ch) < 'a')
#else
/*
  Some other sort of character set - use memchr() so we don't match
  the null byte.
 */
#define ISUUCHAR(ch)    (memchr(PL_uuemap, (ch), sizeof(PL_uuemap)-1) || (ch) == ' ')
#endif


PP(pp_unpack)
{
    dSP;
    dPOPPOPssrl;
    I32 start_sp_offset = SP - PL_stack_base;
    I32 gimme = GIMME_V;
    SV *sv;
    STRLEN llen;
    STRLEN rlen;
    register char *pat = SvPV(left, llen);
#ifdef PACKED_IS_OCTETS
    /* Packed side is assumed to be octets - so force downgrade if it
       has been UTF-8 encoded by accident
     */
    register char *s = SvPVbyte(right, rlen);
#else
    register char *s = SvPV(right, rlen);
#endif
    char *strend = s + rlen;
    char *strbeg = s;
    register char *patend = pat + llen;
    I32 datumtype;
    register I32 len;
    register I32 bits = 0;
    register char *str;

    /* These must not be in registers: */
    short ashort;
    int aint;
    long along;
#ifdef HAS_QUAD
    Quad_t aquad;
#endif
    U16 aushort;
    unsigned int auint;
    U32 aulong;
#ifdef HAS_QUAD
    Uquad_t auquad;
#endif
    char *aptr;
    float afloat;
    double adouble;
    I32 checksum = 0;
    UV culong = 0;
    NV cdouble = 0.0;
    const int bits_in_uv = 8 * sizeof(culong);
    int commas = 0;
    int star;
#ifdef PERL_NATINT_PACK
    int natint;		/* native integer */
    int unatint;	/* unsigned native integer */
#endif
    bool do_utf8 = DO_UTF8(right);

    while (pat < patend) {
      reparse:
	datumtype = *pat++ & 0xFF;
#ifdef PERL_NATINT_PACK
	natint = 0;
#endif
	if (isSPACE(datumtype))
	    continue;
	if (datumtype == '#') {
	    while (pat < patend && *pat != '\n')
		pat++;
	    continue;
	}
	if (*pat == '!') {
	    char *natstr = "sSiIlL";

	    if (strchr(natstr, datumtype)) {
#ifdef PERL_NATINT_PACK
		natint = 1;
#endif
		pat++;
	    }
	    else
		DIE(aTHX_ "'!' allowed only after types %s", natstr);
	}
	star = 0;
	if (pat >= patend)
	    len = 1;
	else if (*pat == '*') {
	    len = strend - strbeg;	/* long enough */
	    pat++;
	    star = 1;
	}
	else if (isDIGIT(*pat)) {
	    len = *pat++ - '0';
	    while (isDIGIT(*pat)) {
		len = (len * 10) + (*pat++ - '0');
		if (len < 0)
		    DIE(aTHX_ "Repeat count in unpack overflows");
	    }
	}
	else
	    len = (datumtype != '@');
      redo_switch:
	switch(datumtype) {
	default:
	    DIE(aTHX_ "Invalid type in unpack: '%c'", (int)datumtype);
	case ',': /* grandfather in commas but with a warning */
	    if (commas++ == 0 && ckWARN(WARN_UNPACK))
		Perl_warner(aTHX_ WARN_UNPACK,
			    "Invalid type in unpack: '%c'", (int)datumtype);
	    break;
	case '%':
	    if (len == 1 && pat[-1] != '1')
		len = 16;
	    checksum = len;
	    culong = 0;
	    cdouble = 0;
	    if (pat < patend)
		goto reparse;
	    break;
	case '@':
	    if (len > strend - strbeg)
		DIE(aTHX_ "@ outside of string");
	    s = strbeg + len;
	    break;
	case 'X':
	    if (len > s - strbeg)
		DIE(aTHX_ "X outside of string");
	    s -= len;
	    break;
	case 'x':
	    if (len > strend - s)
		DIE(aTHX_ "x outside of string");
	    s += len;
	    break;
	case '/':
	    if (start_sp_offset >= SP - PL_stack_base)
		DIE(aTHX_ "/ must follow a numeric type");
	    datumtype = *pat++;
	    if (*pat == '*')
		pat++;		/* ignore '*' for compatibility with pack */
	    if (isDIGIT(*pat))
		DIE(aTHX_ "/ cannot take a count" );
	    len = POPi;
	    star = 0;
	    goto redo_switch;
	case 'A':
	case 'Z':
	case 'a':
	    if (len > strend - s)
		len = strend - s;
	    if (checksum)
		goto uchar_checksum;
	    sv = NEWSV(35, len);
	    sv_setpvn(sv, s, len);
	    if (datumtype == 'A' || datumtype == 'Z') {
		aptr = s;	/* borrow register */
		if (datumtype == 'Z') {	/* 'Z' strips stuff after first null */
		    s = SvPVX(sv);
		    while (*s)
			s++;
		    if (star) /* exact for 'Z*' */
		        len = s - SvPVX(sv) + 1;
		}
		else {		/* 'A' strips both nulls and spaces */
		    s = SvPVX(sv) + len - 1;
		    while (s >= SvPVX(sv) && (!*s || isSPACE(*s)))
			s--;
		    *++s = '\0';
		}
		SvCUR_set(sv, s - SvPVX(sv));
		s = aptr;	/* unborrow register */
	    }
	    s += len;
	    XPUSHs(sv_2mortal(sv));
	    break;
	case 'B':
	case 'b':
	    if (star || len > (strend - s) * 8)
		len = (strend - s) * 8;
	    if (checksum) {
		if (!PL_bitcount) {
		    Newz(601, PL_bitcount, 256, char);
		    for (bits = 1; bits < 256; bits++) {
			if (bits & 1)	PL_bitcount[bits]++;
			if (bits & 2)	PL_bitcount[bits]++;
			if (bits & 4)	PL_bitcount[bits]++;
			if (bits & 8)	PL_bitcount[bits]++;
			if (bits & 16)	PL_bitcount[bits]++;
			if (bits & 32)	PL_bitcount[bits]++;
			if (bits & 64)	PL_bitcount[bits]++;
			if (bits & 128)	PL_bitcount[bits]++;
		    }
		}
		while (len >= 8) {
		    culong += PL_bitcount[*(unsigned char*)s++];
		    len -= 8;
		}
		if (len) {
		    bits = *s;
		    if (datumtype == 'b') {
			while (len-- > 0) {
			    if (bits & 1) culong++;
			    bits >>= 1;
			}
		    }
		    else {
			while (len-- > 0) {
			    if (bits & 128) culong++;
			    bits <<= 1;
			}
		    }
		}
		break;
	    }
	    sv = NEWSV(35, len + 1);
	    SvCUR_set(sv, len);
	    SvPOK_on(sv);
	    str = SvPVX(sv);
	    if (datumtype == 'b') {
		aint = len;
		for (len = 0; len < aint; len++) {
		    if (len & 7)		/*SUPPRESS 595*/
			bits >>= 1;
		    else
			bits = *s++;
		    *str++ = '0' + (bits & 1);
		}
	    }
	    else {
		aint = len;
		for (len = 0; len < aint; len++) {
		    if (len & 7)
			bits <<= 1;
		    else
			bits = *s++;
		    *str++ = '0' + ((bits & 128) != 0);
		}
	    }
	    *str = '\0';
	    XPUSHs(sv_2mortal(sv));
	    break;
	case 'H':
	case 'h':
	    if (star || len > (strend - s) * 2)
		len = (strend - s) * 2;
	    sv = NEWSV(35, len + 1);
	    SvCUR_set(sv, len);
	    SvPOK_on(sv);
	    str = SvPVX(sv);
	    if (datumtype == 'h') {
		aint = len;
		for (len = 0; len < aint; len++) {
		    if (len & 1)
			bits >>= 4;
		    else
			bits = *s++;
		    *str++ = PL_hexdigit[bits & 15];
		}
	    }
	    else {
		aint = len;
		for (len = 0; len < aint; len++) {
		    if (len & 1)
			bits <<= 4;
		    else
			bits = *s++;
		    *str++ = PL_hexdigit[(bits >> 4) & 15];
		}
	    }
	    *str = '\0';
	    XPUSHs(sv_2mortal(sv));
	    break;
	case 'c':
	    if (len > strend - s)
		len = strend - s;
	    if (checksum) {
		while (len-- > 0) {
		    aint = *s++;
		    if (aint >= 128)	/* fake up signed chars */
			aint -= 256;
		    if (checksum > bits_in_uv)
			cdouble += (NV)aint;
		    else
			culong += aint;
		}
	    }
	    else {
		EXTEND(SP, len);
		EXTEND_MORTAL(len);
		while (len-- > 0) {
		    aint = *s++;
		    if (aint >= 128)	/* fake up signed chars */
			aint -= 256;
		    sv = NEWSV(36, 0);
		    sv_setiv(sv, (IV)aint);
		    PUSHs(sv_2mortal(sv));
		}
	    }
	    break;
	case 'C':
	unpack_C: /* unpack U will jump here if not UTF-8 */
            if (len == 0) {
		do_utf8 = FALSE;
		break;
	    }
	    if (len > strend - s)
		len = strend - s;
	    if (checksum) {
	      uchar_checksum:
		while (len-- > 0) {
		    auint = *s++ & 255;
		    culong += auint;
		}
	    }
	    else {
		EXTEND(SP, len);
		EXTEND_MORTAL(len);
		while (len-- > 0) {
		    auint = *s++ & 255;
		    sv = NEWSV(37, 0);
		    sv_setiv(sv, (IV)auint);
		    PUSHs(sv_2mortal(sv));
		}
	    }
	    break;
	case 'U':
	    if (len == 0) {
		do_utf8 = TRUE;
		break;
	    }
	    if (!do_utf8)
		 goto unpack_C;
	    if (len > strend - s)
		len = strend - s;
	    if (checksum) {
		while (len-- > 0 && s < strend) {
		    STRLEN alen;
		    auint = NATIVE_TO_UNI(utf8n_to_uvchr((U8*)s, strend - s, &alen, 0));
		    along = alen;
		    s += along;
		    if (checksum > bits_in_uv)
			cdouble += (NV)auint;
		    else
			culong += auint;
		}
	    }
	    else {
		EXTEND(SP, len);
		EXTEND_MORTAL(len);
		while (len-- > 0 && s < strend) {
		    STRLEN alen;
		    auint = NATIVE_TO_UNI(utf8n_to_uvchr((U8*)s, strend - s, &alen, 0));
		    along = alen;
		    s += along;
		    sv = NEWSV(37, 0);
		    sv_setuv(sv, (UV)auint);
		    PUSHs(sv_2mortal(sv));
		}
	    }
	    break;
	case 's':
#if SHORTSIZE == SIZE16
	    along = (strend - s) / SIZE16;
#else
	    along = (strend - s) / (natint ? sizeof(short) : SIZE16);
#endif
	    if (len > along)
		len = along;
	    if (checksum) {
#if SHORTSIZE != SIZE16
		if (natint) {
		    short ashort;
		    while (len-- > 0) {
			COPYNN(s, &ashort, sizeof(short));
			s += sizeof(short);
			if (checksum > bits_in_uv)
			    cdouble += (NV)ashort;
			else
			    culong += ashort;

		    }
		}
		else
#endif
                {
		    while (len-- > 0) {
			COPY16(s, &ashort);
#if SHORTSIZE > SIZE16
			if (ashort > 32767)
			  ashort -= 65536;
#endif
			s += SIZE16;
			if (checksum > bits_in_uv)
			    cdouble += (NV)ashort;
			else
			    culong += ashort;
		    }
		}
	    }
	    else {
		EXTEND(SP, len);
		EXTEND_MORTAL(len);
#if SHORTSIZE != SIZE16
		if (natint) {
		    short ashort;
		    while (len-- > 0) {
			COPYNN(s, &ashort, sizeof(short));
			s += sizeof(short);
			sv = NEWSV(38, 0);
			sv_setiv(sv, (IV)ashort);
			PUSHs(sv_2mortal(sv));
		    }
		}
		else
#endif
                {
		    while (len-- > 0) {
			COPY16(s, &ashort);
#if SHORTSIZE > SIZE16
			if (ashort > 32767)
			  ashort -= 65536;
#endif
			s += SIZE16;
			sv = NEWSV(38, 0);
			sv_setiv(sv, (IV)ashort);
			PUSHs(sv_2mortal(sv));
		    }
		}
	    }
	    break;
	case 'v':
	case 'n':
	case 'S':
#if SHORTSIZE == SIZE16
	    along = (strend - s) / SIZE16;
#else
	    unatint = natint && datumtype == 'S';
	    along = (strend - s) / (unatint ? sizeof(unsigned short) : SIZE16);
#endif
	    if (len > along)
		len = along;
	    if (checksum) {
#if SHORTSIZE != SIZE16
		if (unatint) {
		    unsigned short aushort;
		    while (len-- > 0) {
			COPYNN(s, &aushort, sizeof(unsigned short));
			s += sizeof(unsigned short);
			if (checksum > bits_in_uv)
			    cdouble += (NV)aushort;
			else
			    culong += aushort;
		    }
		}
		else
#endif
                {
		    while (len-- > 0) {
			COPY16(s, &aushort);
			s += SIZE16;
#ifdef HAS_NTOHS
			if (datumtype == 'n')
			    aushort = PerlSock_ntohs(aushort);
#endif
#ifdef HAS_VTOHS
			if (datumtype == 'v')
			    aushort = vtohs(aushort);
#endif
			if (checksum > bits_in_uv)
			    cdouble += (NV)aushort;
			else
			    culong += aushort;
		    }
		}
	    }
	    else {
		EXTEND(SP, len);
		EXTEND_MORTAL(len);
#if SHORTSIZE != SIZE16
		if (unatint) {
		    unsigned short aushort;
		    while (len-- > 0) {
			COPYNN(s, &aushort, sizeof(unsigned short));
			s += sizeof(unsigned short);
			sv = NEWSV(39, 0);
			sv_setiv(sv, (UV)aushort);
			PUSHs(sv_2mortal(sv));
		    }
		}
		else
#endif
                {
		    while (len-- > 0) {
			COPY16(s, &aushort);
			s += SIZE16;
			sv = NEWSV(39, 0);
#ifdef HAS_NTOHS
			if (datumtype == 'n')
			    aushort = PerlSock_ntohs(aushort);
#endif
#ifdef HAS_VTOHS
			if (datumtype == 'v')
			    aushort = vtohs(aushort);
#endif
			sv_setiv(sv, (UV)aushort);
			PUSHs(sv_2mortal(sv));
		    }
		}
	    }
	    break;
	case 'i':
	    along = (strend - s) / sizeof(int);
	    if (len > along)
		len = along;
	    if (checksum) {
		while (len-- > 0) {
		    Copy(s, &aint, 1, int);
		    s += sizeof(int);
		    if (checksum > bits_in_uv)
			cdouble += (NV)aint;
		    else
			culong += aint;
		}
	    }
	    else {
		EXTEND(SP, len);
		EXTEND_MORTAL(len);
		while (len-- > 0) {
		    Copy(s, &aint, 1, int);
		    s += sizeof(int);
		    sv = NEWSV(40, 0);
#ifdef __osf__
                    /* Without the dummy below unpack("i", pack("i",-1))
                     * return 0xFFffFFff instead of -1 for Digital Unix V4.0
                     * cc with optimization turned on.
		     *
		     * The bug was detected in
		     * DEC C V5.8-009 on Digital UNIX V4.0 (Rev. 1091) (V4.0E)
		     * with optimization (-O4) turned on.
		     * DEC C V5.2-040 on Digital UNIX V4.0 (Rev. 564) (V4.0B)
		     * does not have this problem even with -O4.
		     *
		     * This bug was reported as DECC_BUGS 1431
		     * and tracked internally as GEM_BUGS 7775.
		     *
		     * The bug is fixed in
		     * Tru64 UNIX V5.0:      Compaq C V6.1-006 or later
		     * UNIX V4.0F support:   DEC C V5.9-006 or later
		     * UNIX V4.0E support:   DEC C V5.8-011 or later
		     * and also in DTK.
		     *
		     * See also few lines later for the same bug.
		     */
                    (aint) ?
		    	sv_setiv(sv, (IV)aint) :
#endif
		    sv_setiv(sv, (IV)aint);
		    PUSHs(sv_2mortal(sv));
		}
	    }
	    break;
	case 'I':
	    along = (strend - s) / sizeof(unsigned int);
	    if (len > along)
		len = along;
	    if (checksum) {
		while (len-- > 0) {
		    Copy(s, &auint, 1, unsigned int);
		    s += sizeof(unsigned int);
		    if (checksum > bits_in_uv)
			cdouble += (NV)auint;
		    else
			culong += auint;
		}
	    }
	    else {
		EXTEND(SP, len);
		EXTEND_MORTAL(len);
		while (len-- > 0) {
		    Copy(s, &auint, 1, unsigned int);
		    s += sizeof(unsigned int);
		    sv = NEWSV(41, 0);
#ifdef __osf__
                    /* Without the dummy below unpack("I", pack("I",0xFFFFFFFF))
                     * returns 1.84467440737096e+19 instead of 0xFFFFFFFF.
		     * See details few lines earlier. */
                    (auint) ?
		        sv_setuv(sv, (UV)auint) :
#endif
		    sv_setuv(sv, (UV)auint);
		    PUSHs(sv_2mortal(sv));
		}
	    }
	    break;
	case 'l':
#if LONGSIZE == SIZE32
	    along = (strend - s) / SIZE32;
#else
	    along = (strend - s) / (natint ? sizeof(long) : SIZE32);
#endif
	    if (len > along)
		len = along;
	    if (checksum) {
#if LONGSIZE != SIZE32
		if (natint) {
		    while (len-- > 0) {
			COPYNN(s, &along, sizeof(long));
			s += sizeof(long);
			if (checksum > bits_in_uv)
			    cdouble += (NV)along;
			else
			    culong += along;
		    }
		}
		else
#endif
                {
		    while (len-- > 0) {
#if LONGSIZE > SIZE32 && INTSIZE == SIZE32
			I32 along;
#endif
			COPY32(s, &along);
#if LONGSIZE > SIZE32
			if (along > 2147483647)
			  along -= 4294967296;
#endif
			s += SIZE32;
			if (checksum > bits_in_uv)
			    cdouble += (NV)along;
			else
			    culong += along;
		    }
		}
	    }
	    else {
		EXTEND(SP, len);
		EXTEND_MORTAL(len);
#if LONGSIZE != SIZE32
		if (natint) {
		    while (len-- > 0) {
			COPYNN(s, &along, sizeof(long));
			s += sizeof(long);
			sv = NEWSV(42, 0);
			sv_setiv(sv, (IV)along);
			PUSHs(sv_2mortal(sv));
		    }
		}
		else
#endif
                {
		    while (len-- > 0) {
#if LONGSIZE > SIZE32 && INTSIZE == SIZE32
			I32 along;
#endif
			COPY32(s, &along);
#if LONGSIZE > SIZE32
			if (along > 2147483647)
			  along -= 4294967296;
#endif
			s += SIZE32;
			sv = NEWSV(42, 0);
			sv_setiv(sv, (IV)along);
			PUSHs(sv_2mortal(sv));
		    }
		}
	    }
	    break;
	case 'V':
	case 'N':
	case 'L':
#if LONGSIZE == SIZE32
	    along = (strend - s) / SIZE32;
#else
	    unatint = natint && datumtype == 'L';
	    along = (strend - s) / (unatint ? sizeof(unsigned long) : SIZE32);
#endif
	    if (len > along)
		len = along;
	    if (checksum) {
#if LONGSIZE != SIZE32
		if (unatint) {
		    unsigned long aulong;
		    while (len-- > 0) {
			COPYNN(s, &aulong, sizeof(unsigned long));
			s += sizeof(unsigned long);
			if (checksum > bits_in_uv)
			    cdouble += (NV)aulong;
			else
			    culong += aulong;
		    }
		}
		else
#endif
                {
		    while (len-- > 0) {
			COPY32(s, &aulong);
			s += SIZE32;
#ifdef HAS_NTOHL
			if (datumtype == 'N')
			    aulong = PerlSock_ntohl(aulong);
#endif
#ifdef HAS_VTOHL
			if (datumtype == 'V')
			    aulong = vtohl(aulong);
#endif
			if (checksum > bits_in_uv)
			    cdouble += (NV)aulong;
			else
			    culong += aulong;
		    }
		}
	    }
	    else {
		EXTEND(SP, len);
		EXTEND_MORTAL(len);
#if LONGSIZE != SIZE32
		if (unatint) {
		    unsigned long aulong;
		    while (len-- > 0) {
			COPYNN(s, &aulong, sizeof(unsigned long));
			s += sizeof(unsigned long);
			sv = NEWSV(43, 0);
			sv_setuv(sv, (UV)aulong);
			PUSHs(sv_2mortal(sv));
		    }
		}
		else
#endif
                {
		    while (len-- > 0) {
			COPY32(s, &aulong);
			s += SIZE32;
#ifdef HAS_NTOHL
			if (datumtype == 'N')
			    aulong = PerlSock_ntohl(aulong);
#endif
#ifdef HAS_VTOHL
			if (datumtype == 'V')
			    aulong = vtohl(aulong);
#endif
			sv = NEWSV(43, 0);
			sv_setuv(sv, (UV)aulong);
			PUSHs(sv_2mortal(sv));
		    }
		}
	    }
	    break;
	case 'p':
	    along = (strend - s) / sizeof(char*);
	    if (len > along)
		len = along;
	    EXTEND(SP, len);
	    EXTEND_MORTAL(len);
	    while (len-- > 0) {
		if (sizeof(char*) > strend - s)
		    break;
		else {
		    Copy(s, &aptr, 1, char*);
		    s += sizeof(char*);
		}
		sv = NEWSV(44, 0);
		if (aptr)
		    sv_setpv(sv, aptr);
		PUSHs(sv_2mortal(sv));
	    }
	    break;
	case 'w':
	    EXTEND(SP, len);
	    EXTEND_MORTAL(len);
	    {
		UV auv = 0;
		U32 bytes = 0;
		
		while ((len > 0) && (s < strend)) {
		    auv = (auv << 7) | (*s & 0x7f);
		    /* UTF8_IS_XXXXX not right here - using constant 0x80 */
		    if ((U8)(*s++) < 0x80) {
			bytes = 0;
			sv = NEWSV(40, 0);
			sv_setuv(sv, auv);
			PUSHs(sv_2mortal(sv));
			len--;
			auv = 0;
		    }
		    else if (++bytes >= sizeof(UV)) {	/* promote to string */
			char *t;
			STRLEN n_a;

			sv = Perl_newSVpvf(aTHX_ "%.*"UVf, (int)TYPE_DIGITS(UV), auv);
			while (s < strend) {
			    sv = mul128(sv, *s & 0x7f);
			    if (!(*s++ & 0x80)) {
				bytes = 0;
				break;
			    }
			}
			t = SvPV(sv, n_a);
			while (*t == '0')
			    t++;
			sv_chop(sv, t);
			PUSHs(sv_2mortal(sv));
			len--;
			auv = 0;
		    }
		}
		if ((s >= strend) && bytes)
		    DIE(aTHX_ "Unterminated compressed integer");
	    }
	    break;
	case 'P':
	    if (star)
	        DIE(aTHX_ "P must have an explicit size");
	    EXTEND(SP, 1);
	    if (sizeof(char*) > strend - s)
		break;
	    else {
		Copy(s, &aptr, 1, char*);
		s += sizeof(char*);
	    }
	    sv = NEWSV(44, 0);
	    if (aptr)
		sv_setpvn(sv, aptr, len);
	    PUSHs(sv_2mortal(sv));
	    break;
#ifdef HAS_QUAD
	case 'q':
	    along = (strend - s) / sizeof(Quad_t);
	    if (len > along)
		len = along;
	    if (checksum) {
		while (len-- > 0) {
		    Copy(s, &aquad, 1, Quad_t);
		    s += sizeof(Quad_t);
		    if (checksum > bits_in_uv)
			cdouble += (NV)aquad;
		    else
			culong += aquad;
		}
	    }
            else {
                EXTEND(SP, len);
                EXTEND_MORTAL(len);
                while (len-- > 0) {
                    if (s + sizeof(Quad_t) > strend)
                        aquad = 0;
                    else {
		    Copy(s, &aquad, 1, Quad_t);
		    s += sizeof(Quad_t);
                    }
                    sv = NEWSV(42, 0);
                    if (aquad >= IV_MIN && aquad <= IV_MAX)
		    sv_setiv(sv, (IV)aquad);
                    else
                        sv_setnv(sv, (NV)aquad);
                    PUSHs(sv_2mortal(sv));
                }
            }
	    break;
	case 'Q':
	    along = (strend - s) / sizeof(Quad_t);
	    if (len > along)
		len = along;
	    if (checksum) {
		while (len-- > 0) {
		    Copy(s, &auquad, 1, Uquad_t);
		    s += sizeof(Uquad_t);
		    if (checksum > bits_in_uv)
			cdouble += (NV)auquad;
		    else
			culong += auquad;
		}
	    }
            else {
                EXTEND(SP, len);
                EXTEND_MORTAL(len);
                while (len-- > 0) {
                    if (s + sizeof(Uquad_t) > strend)
                        auquad = 0;
                    else {
                        Copy(s, &auquad, 1, Uquad_t);
                        s += sizeof(Uquad_t);
                    }
                    sv = NEWSV(43, 0);
                    if (auquad <= UV_MAX)
                        sv_setuv(sv, (UV)auquad);
                    else
		    sv_setnv(sv, (NV)auquad);
                    PUSHs(sv_2mortal(sv));
                }
            }
	    break;
#endif
	/* float and double added gnb@melba.bby.oz.au 22/11/89 */
	case 'f':
	case 'F':
	    along = (strend - s) / sizeof(float);
	    if (len > along)
		len = along;
	    if (checksum) {
		while (len-- > 0) {
		    Copy(s, &afloat, 1, float);
		    s += sizeof(float);
		    cdouble += afloat;
		}
	    }
	    else {
		EXTEND(SP, len);
		EXTEND_MORTAL(len);
		while (len-- > 0) {
		    Copy(s, &afloat, 1, float);
		    s += sizeof(float);
		    sv = NEWSV(47, 0);
		    sv_setnv(sv, (NV)afloat);
		    PUSHs(sv_2mortal(sv));
		}
	    }
	    break;
	case 'd':
	case 'D':
	    along = (strend - s) / sizeof(double);
	    if (len > along)
		len = along;
	    if (checksum) {
		while (len-- > 0) {
		    Copy(s, &adouble, 1, double);
		    s += sizeof(double);
		    cdouble += adouble;
		}
	    }
	    else {
		EXTEND(SP, len);
		EXTEND_MORTAL(len);
		while (len-- > 0) {
		    Copy(s, &adouble, 1, double);
		    s += sizeof(double);
		    sv = NEWSV(48, 0);
		    sv_setnv(sv, (NV)adouble);
		    PUSHs(sv_2mortal(sv));
		}
	    }
	    break;
	case 'u':
	    /* MKS:
	     * Initialise the decode mapping.  By using a table driven
             * algorithm, the code will be character-set independent
             * (and just as fast as doing character arithmetic)
             */
            if (PL_uudmap['M'] == 0) {
                int i;

                for (i = 0; i < sizeof(PL_uuemap); i += 1)
                    PL_uudmap[(U8)PL_uuemap[i]] = i;
                /*
                 * Because ' ' and '`' map to the same value,
                 * we need to decode them both the same.
                 */
                PL_uudmap[' '] = 0;
            }

	    along = (strend - s) * 3 / 4;
	    sv = NEWSV(42, along);
	    if (along)
		SvPOK_on(sv);
	    while (s < strend && *s > ' ' && ISUUCHAR(*s)) {
		I32 a, b, c, d;
		char hunk[4];

		hunk[3] = '\0';
		len = PL_uudmap[*(U8*)s++] & 077;
		while (len > 0) {
		    if (s < strend && ISUUCHAR(*s))
			a = PL_uudmap[*(U8*)s++] & 077;
 		    else
 			a = 0;
		    if (s < strend && ISUUCHAR(*s))
			b = PL_uudmap[*(U8*)s++] & 077;
 		    else
 			b = 0;
		    if (s < strend && ISUUCHAR(*s))
			c = PL_uudmap[*(U8*)s++] & 077;
 		    else
 			c = 0;
		    if (s < strend && ISUUCHAR(*s))
			d = PL_uudmap[*(U8*)s++] & 077;
		    else
			d = 0;
		    hunk[0] = (a << 2) | (b >> 4);
		    hunk[1] = (b << 4) | (c >> 2);
		    hunk[2] = (c << 6) | d;
		    sv_catpvn(sv, hunk, (len > 3) ? 3 : len);
		    len -= 3;
		}
		if (*s == '\n')
		    s++;
		else if (s[1] == '\n')		/* possible checksum byte */
		    s += 2;
	    }
	    XPUSHs(sv_2mortal(sv));
	    break;
	}
	if (checksum) {
	    sv = NEWSV(42, 0);
	    if (strchr("fFdD", datumtype) ||
	      (checksum > bits_in_uv && strchr("csSiIlLnNUvVqQ", datumtype)) ) {
		NV trouble;

                adouble = (NV) (1 << (checksum & 15));
		while (checksum >= 16) {
		    checksum -= 16;
		    adouble *= 65536.0;
		}
		while (cdouble < 0.0)
		    cdouble += adouble;
		cdouble = Perl_modf(cdouble / adouble, &trouble) * adouble;
		sv_setnv(sv, cdouble);
	    }
	    else {
		if (checksum < bits_in_uv) {
		    UV mask = ((UV)1 << checksum) - 1;
		    culong &= mask;
		}
		sv_setuv(sv, (UV)culong);
	    }
	    XPUSHs(sv_2mortal(sv));
	    checksum = 0;
	}
        if (gimme != G_ARRAY &&
            SP - PL_stack_base == start_sp_offset + 1) {
          /* do first one only unless in list context
             / is implmented by unpacking the count, then poping it from the
             stack, so must check that we're not in the middle of a /  */
          if ((pat >= patend) || *pat != '/')
            RETURN;
        }
    }
    if (SP - PL_stack_base == start_sp_offset && gimme == G_SCALAR)
	PUSHs(&PL_sv_undef);
    RETURN;
}

STATIC void
S_doencodes(pTHX_ register SV *sv, register char *s, register I32 len)
{
    char hunk[5];

    *hunk = PL_uuemap[len];
    sv_catpvn(sv, hunk, 1);
    hunk[4] = '\0';
    while (len > 2) {
	hunk[0] = PL_uuemap[(077 & (*s >> 2))];
	hunk[1] = PL_uuemap[(077 & (((*s << 4) & 060) | ((s[1] >> 4) & 017)))];
	hunk[2] = PL_uuemap[(077 & (((s[1] << 2) & 074) | ((s[2] >> 6) & 03)))];
	hunk[3] = PL_uuemap[(077 & (s[2] & 077))];
	sv_catpvn(sv, hunk, 4);
	s += 3;
	len -= 3;
    }
    if (len > 0) {
	char r = (len > 1 ? s[1] : '\0');
	hunk[0] = PL_uuemap[(077 & (*s >> 2))];
	hunk[1] = PL_uuemap[(077 & (((*s << 4) & 060) | ((r >> 4) & 017)))];
	hunk[2] = PL_uuemap[(077 & ((r << 2) & 074))];
	hunk[3] = PL_uuemap[0];
	sv_catpvn(sv, hunk, 4);
    }
    sv_catpvn(sv, "\n", 1);
}

STATIC SV *
S_is_an_int(pTHX_ char *s, STRLEN l)
{
  STRLEN	 n_a;
  SV             *result = newSVpvn(s, l);
  char           *result_c = SvPV(result, n_a);	/* convenience */
  char           *out = result_c;
  bool            skip = 1;
  bool            ignore = 0;

  while (*s) {
    switch (*s) {
    case ' ':
      break;
    case '+':
      if (!skip) {
	SvREFCNT_dec(result);
	return (NULL);
      }
      break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      skip = 0;
      if (!ignore) {
	*(out++) = *s;
      }
      break;
    case '.':
      ignore = 1;
      break;
    default:
      SvREFCNT_dec(result);
      return (NULL);
    }
    s++;
  }
  *(out++) = '\0';
  SvCUR_set(result, out - result_c);
  return (result);
}

/* pnum must be '\0' terminated */
STATIC int
S_div128(pTHX_ SV *pnum, bool *done)
{
  STRLEN          len;
  char           *s = SvPV(pnum, len);
  int             m = 0;
  int             r = 0;
  char           *t = s;

  *done = 1;
  while (*t) {
    int             i;

    i = m * 10 + (*t - '0');
    m = i & 0x7F;
    r = (i >> 7);		/* r < 10 */
    if (r) {
      *done = 0;
    }
    *(t++) = '0' + r;
  }
  *(t++) = '\0';
  SvCUR_set(pnum, (STRLEN) (t - s));
  return (m);
}


PP(pp_pack)
{
    dSP; dMARK; dORIGMARK; dTARGET;
    register SV *cat = TARG;
    register I32 items;
    STRLEN fromlen;
    register char *pat = SvPVx(*++MARK, fromlen);
    char *patcopy;
    register char *patend = pat + fromlen;
    register I32 len;
    I32 datumtype;
    SV *fromstr;
    /*SUPPRESS 442*/
    static char null10[] = {0,0,0,0,0,0,0,0,0,0};
    static char *space10 = "          ";

    /* These must not be in registers: */
    char achar;
    I16 ashort;
    int aint;
    unsigned int auint;
    I32 along;
    U32 aulong;
#ifdef HAS_QUAD
    Quad_t aquad;
    Uquad_t auquad;
#endif
    char *aptr;
    float afloat;
    double adouble;
    int commas = 0;
#ifdef PERL_NATINT_PACK
    int natint;		/* native integer */
#endif

    items = SP - MARK;
    MARK++;
    sv_setpvn(cat, "", 0);
    patcopy = pat;
    while (pat < patend) {
	SV *lengthcode = Nullsv;
#define NEXTFROM ( lengthcode ? lengthcode : items-- > 0 ? *MARK++ : &PL_sv_no)
	datumtype = *pat++ & 0xFF;
#ifdef PERL_NATINT_PACK
	natint = 0;
#endif
	if (isSPACE(datumtype)) {
	    patcopy++;
	    continue;
        }
#ifndef PACKED_IS_OCTETS
	if (datumtype == 'U' && pat == patcopy+1)
	    SvUTF8_on(cat);
#endif
	if (datumtype == '#') {
	    while (pat < patend && *pat != '\n')
		pat++;
	    continue;
	}
        if (*pat == '!') {
	    char *natstr = "sSiIlL";

	    if (strchr(natstr, datumtype)) {
#ifdef PERL_NATINT_PACK
		natint = 1;
#endif
		pat++;
	    }
	    else
		DIE(aTHX_ "'!' allowed only after types %s", natstr);
	}
	if (*pat == '*') {
	    len = strchr("@Xxu", datumtype) ? 0 : items;
	    pat++;
	}
	else if (isDIGIT(*pat)) {
	    len = *pat++ - '0';
	    while (isDIGIT(*pat)) {
		len = (len * 10) + (*pat++ - '0');
		if (len < 0)
		    DIE(aTHX_ "Repeat count in pack overflows");
	    }
	}
	else
	    len = 1;
	if (*pat == '/') {
	    ++pat;
	    if ((*pat != 'a' && *pat != 'A' && *pat != 'Z') || pat[1] != '*')
		DIE(aTHX_ "/ must be followed by a*, A* or Z*");
	    lengthcode = sv_2mortal(newSViv(sv_len(items > 0
						   ? *MARK : &PL_sv_no)
                                            + (*pat == 'Z' ? 1 : 0)));
	}
	switch(datumtype) {
	default:
	    DIE(aTHX_ "Invalid type in pack: '%c'", (int)datumtype);
	case ',': /* grandfather in commas but with a warning */
	    if (commas++ == 0 && ckWARN(WARN_PACK))
		Perl_warner(aTHX_ WARN_PACK,
			    "Invalid type in pack: '%c'", (int)datumtype);
	    break;
	case '%':
	    DIE(aTHX_ "%% may only be used in unpack");
	case '@':
	    len -= SvCUR(cat);
	    if (len > 0)
		goto grow;
	    len = -len;
	    if (len > 0)
		goto shrink;
	    break;
	case 'X':
	  shrink:
	    if (SvCUR(cat) < len)
		DIE(aTHX_ "X outside of string");
	    SvCUR(cat) -= len;
	    *SvEND(cat) = '\0';
	    break;
	case 'x':
	  grow:
	    while (len >= 10) {
		sv_catpvn(cat, null10, 10);
		len -= 10;
	    }
	    sv_catpvn(cat, null10, len);
	    break;
	case 'A':
	case 'Z':
	case 'a':
	    fromstr = NEXTFROM;
	    aptr = SvPV(fromstr, fromlen);
	    if (pat[lengthcode ? -2 : -1] == '*') { /* -2 after '/' */  
		len = fromlen;
		if (datumtype == 'Z')
		    ++len;
	    }
	    if (fromlen >= len) {
		sv_catpvn(cat, aptr, len);
		if (datumtype == 'Z')
		    *(SvEND(cat)-1) = '\0';
	    }
	    else {
		sv_catpvn(cat, aptr, fromlen);
		len -= fromlen;
		if (datumtype == 'A') {
		    while (len >= 10) {
			sv_catpvn(cat, space10, 10);
			len -= 10;
		    }
		    sv_catpvn(cat, space10, len);
		}
		else {
		    while (len >= 10) {
			sv_catpvn(cat, null10, 10);
			len -= 10;
		    }
		    sv_catpvn(cat, null10, len);
		}
	    }
	    break;
	case 'B':
	case 'b':
	    {
		register char *str;
		I32 saveitems;

		fromstr = NEXTFROM;
		saveitems = items;
		str = SvPV(fromstr, fromlen);
		if (pat[-1] == '*')
		    len = fromlen;
		aint = SvCUR(cat);
		SvCUR(cat) += (len+7)/8;
		SvGROW(cat, SvCUR(cat) + 1);
		aptr = SvPVX(cat) + aint;
		if (len > fromlen)
		    len = fromlen;
		aint = len;
		items = 0;
		if (datumtype == 'B') {
		    for (len = 0; len++ < aint;) {
			items |= *str++ & 1;
			if (len & 7)
			    items <<= 1;
			else {
			    *aptr++ = items & 0xff;
			    items = 0;
			}
		    }
		}
		else {
		    for (len = 0; len++ < aint;) {
			if (*str++ & 1)
			    items |= 128;
			if (len & 7)
			    items >>= 1;
			else {
			    *aptr++ = items & 0xff;
			    items = 0;
			}
		    }
		}
		if (aint & 7) {
		    if (datumtype == 'B')
			items <<= 7 - (aint & 7);
		    else
			items >>= 7 - (aint & 7);
		    *aptr++ = items & 0xff;
		}
		str = SvPVX(cat) + SvCUR(cat);
		while (aptr <= str)
		    *aptr++ = '\0';

		items = saveitems;
	    }
	    break;
	case 'H':
	case 'h':
	    {
		register char *str;
		I32 saveitems;

		fromstr = NEXTFROM;
		saveitems = items;
		str = SvPV(fromstr, fromlen);
		if (pat[-1] == '*')
		    len = fromlen;
		aint = SvCUR(cat);
		SvCUR(cat) += (len+1)/2;
		SvGROW(cat, SvCUR(cat) + 1);
		aptr = SvPVX(cat) + aint;
		if (len > fromlen)
		    len = fromlen;
		aint = len;
		items = 0;
		if (datumtype == 'H') {
		    for (len = 0; len++ < aint;) {
			if (isALPHA(*str))
			    items |= ((*str++ & 15) + 9) & 15;
			else
			    items |= *str++ & 15;
			if (len & 1)
			    items <<= 4;
			else {
			    *aptr++ = items & 0xff;
			    items = 0;
			}
		    }
		}
		else {
		    for (len = 0; len++ < aint;) {
			if (isALPHA(*str))
			    items |= (((*str++ & 15) + 9) & 15) << 4;
			else
			    items |= (*str++ & 15) << 4;
			if (len & 1)
			    items >>= 4;
			else {
			    *aptr++ = items & 0xff;
			    items = 0;
			}
		    }
		}
		if (aint & 1)
		    *aptr++ = items & 0xff;
		str = SvPVX(cat) + SvCUR(cat);
		while (aptr <= str)
		    *aptr++ = '\0';

		items = saveitems;
	    }
	    break;
	case 'C':
	case 'c':
	    while (len-- > 0) {
		fromstr = NEXTFROM;
		switch (datumtype) {
		case 'C':
		    aint = SvIV(fromstr);
		    if ((aint < 0 || aint > 255) &&
			ckWARN(WARN_PACK))
		        Perl_warner(aTHX_ WARN_PACK,
				    "Character in \"C\" format wrapped");
		    achar = aint & 255;
		    sv_catpvn(cat, &achar, sizeof(char));
		    break;
		case 'c':
		    aint = SvIV(fromstr);
		    if ((aint < -128 || aint > 127) &&
			ckWARN(WARN_PACK))
		        Perl_warner(aTHX_ WARN_PACK,
				    "Character in \"c\" format wrapped");
		    achar = aint & 255;
		    sv_catpvn(cat, &achar, sizeof(char));
		    break;
		}
	    }
	    break;
	case 'U':
	    while (len-- > 0) {
		fromstr = NEXTFROM;
		auint = UNI_TO_NATIVE(SvUV(fromstr));
		SvGROW(cat, SvCUR(cat) + UTF8_MAXLEN + 1);
		SvCUR_set(cat, (char*)uvchr_to_utf8((U8*)SvEND(cat),auint)
			       - SvPVX(cat));
	    }
	    *SvEND(cat) = '\0';
	    break;
	/* Float and double added by gnb@melba.bby.oz.au  22/11/89 */
	case 'f':
	case 'F':
	    while (len-- > 0) {
		fromstr = NEXTFROM;
		afloat = (float)SvNV(fromstr);
		sv_catpvn(cat, (char *)&afloat, sizeof (float));
	    }
	    break;
	case 'd':
	case 'D':
	    while (len-- > 0) {
		fromstr = NEXTFROM;
		adouble = (double)SvNV(fromstr);
		sv_catpvn(cat, (char *)&adouble, sizeof (double));
	    }
	    break;
	case 'n':
	    while (len-- > 0) {
		fromstr = NEXTFROM;
		ashort = (I16)SvIV(fromstr);
#ifdef HAS_HTONS
		ashort = PerlSock_htons(ashort);
#endif
		CAT16(cat, &ashort);
	    }
	    break;
	case 'v':
	    while (len-- > 0) {
		fromstr = NEXTFROM;
		ashort = (I16)SvIV(fromstr);
#ifdef HAS_HTOVS
		ashort = htovs(ashort);
#endif
		CAT16(cat, &ashort);
	    }
	    break;
	case 'S':
#if SHORTSIZE != SIZE16
	    if (natint) {
		unsigned short aushort;

		while (len-- > 0) {
		    fromstr = NEXTFROM;
		    aushort = SvUV(fromstr);
		    sv_catpvn(cat, (char *)&aushort, sizeof(unsigned short));
		}
	    }
	    else
#endif
            {
		U16 aushort;

		while (len-- > 0) {
		    fromstr = NEXTFROM;
		    aushort = (U16)SvUV(fromstr);
		    CAT16(cat, &aushort);
		}

	    }
	    break;
	case 's':
#if SHORTSIZE != SIZE16
	    if (natint) {
		short ashort;

		while (len-- > 0) {
		    fromstr = NEXTFROM;
		    ashort = SvIV(fromstr);
		    sv_catpvn(cat, (char *)&ashort, sizeof(short));
		}
	    }
	    else
#endif
            {
		while (len-- > 0) {
		    fromstr = NEXTFROM;
		    ashort = (I16)SvIV(fromstr);
		    CAT16(cat, &ashort);
		}
	    }
	    break;
	case 'I':
	    while (len-- > 0) {
		fromstr = NEXTFROM;
		auint = SvUV(fromstr);
		sv_catpvn(cat, (char*)&auint, sizeof(unsigned int));
	    }
	    break;
	case 'w':
            while (len-- > 0) {
		fromstr = NEXTFROM;
		adouble = Perl_floor(SvNV(fromstr));

		if (adouble < 0)
		    DIE(aTHX_ "Cannot compress negative numbers");

		if (
#if UVSIZE > 4 && UVSIZE >= NVSIZE
		    adouble <= 0xffffffff
#else
#   ifdef CXUX_BROKEN_CONSTANT_CONVERT
		    adouble <= UV_MAX_cxux
#   else
		    adouble <= UV_MAX
#   endif
#endif
		    )
		{
		    char   buf[1 + sizeof(UV)];
		    char  *in = buf + sizeof(buf);
		    UV     auv = U_V(adouble);

		    do {
			*--in = (auv & 0x7f) | 0x80;
			auv >>= 7;
		    } while (auv);
		    buf[sizeof(buf) - 1] &= 0x7f; /* clear continue bit */
		    sv_catpvn(cat, in, (buf + sizeof(buf)) - in);
		}
		else if (SvPOKp(fromstr)) {  /* decimal string arithmetics */
		    char           *from, *result, *in;
		    SV             *norm;
		    STRLEN          len;
		    bool            done;

		    /* Copy string and check for compliance */
		    from = SvPV(fromstr, len);
		    if ((norm = is_an_int(from, len)) == NULL)
			DIE(aTHX_ "can compress only unsigned integer");

		    New('w', result, len, char);
		    in = result + len;
		    done = FALSE;
		    while (!done)
			*--in = div128(norm, &done) | 0x80;
		    result[len - 1] &= 0x7F; /* clear continue bit */
		    sv_catpvn(cat, in, (result + len) - in);
		    Safefree(result);
		    SvREFCNT_dec(norm);	/* free norm */
                }
		else if (SvNOKp(fromstr)) {
		    char   buf[sizeof(double) * 2];	/* 8/7 <= 2 */
		    char  *in = buf + sizeof(buf);

		    do {
			double next = floor(adouble / 128);
			*--in = (unsigned char)(adouble - (next * 128)) | 0x80;
			if (in <= buf)  /* this cannot happen ;-) */
			    DIE(aTHX_ "Cannot compress integer");
			adouble = next;
		    } while (adouble > 0);
		    buf[sizeof(buf) - 1] &= 0x7f; /* clear continue bit */
		    sv_catpvn(cat, in, (buf + sizeof(buf)) - in);
		}
		else
		    DIE(aTHX_ "Cannot compress non integer");
	    }
            break;
	case 'i':
	    while (len-- > 0) {
		fromstr = NEXTFROM;
		aint = SvIV(fromstr);
		sv_catpvn(cat, (char*)&aint, sizeof(int));
	    }
	    break;
	case 'N':
	    while (len-- > 0) {
		fromstr = NEXTFROM;
		aulong = SvUV(fromstr);
#ifdef HAS_HTONL
		aulong = PerlSock_htonl(aulong);
#endif
		CAT32(cat, &aulong);
	    }
	    break;
	case 'V':
	    while (len-- > 0) {
		fromstr = NEXTFROM;
		aulong = SvUV(fromstr);
#ifdef HAS_HTOVL
		aulong = htovl(aulong);
#endif
		CAT32(cat, &aulong);
	    }
	    break;
	case 'L':
#if LONGSIZE != SIZE32
	    if (natint) {
		unsigned long aulong;

		while (len-- > 0) {
		    fromstr = NEXTFROM;
		    aulong = SvUV(fromstr);
		    sv_catpvn(cat, (char *)&aulong, sizeof(unsigned long));
		}
	    }
	    else
#endif
            {
		while (len-- > 0) {
		    fromstr = NEXTFROM;
		    aulong = SvUV(fromstr);
		    CAT32(cat, &aulong);
		}
	    }
	    break;
	case 'l':
#if LONGSIZE != SIZE32
	    if (natint) {
		long along;

		while (len-- > 0) {
		    fromstr = NEXTFROM;
		    along = SvIV(fromstr);
		    sv_catpvn(cat, (char *)&along, sizeof(long));
		}
	    }
	    else
#endif
            {
		while (len-- > 0) {
		    fromstr = NEXTFROM;
		    along = SvIV(fromstr);
		    CAT32(cat, &along);
		}
	    }
	    break;
#ifdef HAS_QUAD
	case 'Q':
	    while (len-- > 0) {
		fromstr = NEXTFROM;
		auquad = (Uquad_t)SvUV(fromstr);
		sv_catpvn(cat, (char*)&auquad, sizeof(Uquad_t));
	    }
	    break;
	case 'q':
	    while (len-- > 0) {
		fromstr = NEXTFROM;
		aquad = (Quad_t)SvIV(fromstr);
		sv_catpvn(cat, (char*)&aquad, sizeof(Quad_t));
	    }
	    break;
#endif
	case 'P':
	    len = 1;		/* assume SV is correct length */
	    /* FALL THROUGH */
	case 'p':
	    while (len-- > 0) {
		fromstr = NEXTFROM;
		if (fromstr == &PL_sv_undef)
		    aptr = NULL;
		else {
		    STRLEN n_a;
		    /* XXX better yet, could spirit away the string to
		     * a safe spot and hang on to it until the result
		     * of pack() (and all copies of the result) are
		     * gone.
		     */
		    if (ckWARN(WARN_PACK) && (SvTEMP(fromstr)
						|| (SvPADTMP(fromstr)
						    && !SvREADONLY(fromstr))))
		    {
			Perl_warner(aTHX_ WARN_PACK,
				"Attempt to pack pointer to temporary value");
		    }
		    if (SvPOK(fromstr) || SvNIOK(fromstr))
			aptr = SvPV(fromstr,n_a);
		    else
			aptr = SvPV_force(fromstr,n_a);
		}
		sv_catpvn(cat, (char*)&aptr, sizeof(char*));
	    }
	    break;
	case 'u':
	    fromstr = NEXTFROM;
	    aptr = SvPV(fromstr, fromlen);
	    SvGROW(cat, fromlen * 4 / 3);
	    if (len <= 2)
		len = 45;
	    else
		len = len / 3 * 3;
	    while (fromlen > 0) {
		I32 todo;

		if (fromlen > len)
		    todo = len;
		else
		    todo = fromlen;
		doencodes(cat, aptr, todo);
		fromlen -= todo;
		aptr += todo;
	    }
	    break;
	}
    }
    SvSETMAGIC(cat);
    SP = ORIGMARK;
    PUSHs(cat);
    RETURN;
}
#undef NEXTFROM


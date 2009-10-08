/*
  Copyright (c) 2006-2009 by Jakob Schroeter <js@camaya.net>
  This file is part of the gloox library. http://camaya.net/gloox

  This software is distributed under a license. The full license
  agreement can be found in the file LICENSE in this distribution.
  This software may not be copied, modified, sold or distributed
  other than expressed in the named license agreement.

  This software is distributed without any warranty.
*/

/*
  This class is based on a C implementation of the MD5 algorithm written by
  L. Peter Deutsch.
  The full notice as shipped with the original verson is included below.
*/

/*
  Copyright (C) 1999, 2000, 2002 Aladdin Enterprises.  All rights reserved.

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  L. Peter Deutsch
  ghost@aladdin.com

 */
/* $Id: md5.c,v 1.6 2002/04/13 19:20:28 lpd Exp $ */
/*
  Independent implementation of MD5 (RFC 1321).

  This code implements the MD5 Algorithm defined in RFC 1321, whose
  text is available at
        http://www.ietf.org/rfc/rfc1321.txt
  The code is derived from the text of the RFC, including the test suite
  (section A.5) but excluding the rest of Appendix A.  It does not include
  any code or documentation that is identified in the RFC as being
  copyrighted.

  The original and principal author of md5.c is L. Peter Deutsch
  <ghost@aladdin.com>.  Other authors are noted in the change history
  that follows (in reverse chronological order):

  2002-04-13 lpd Clarified derivation from RFC 1321; now handles byte order
        either statically or dynamically; added missing #include <string.h>
        in library.
  2002-03-11 lpd Corrected argument list for main(), and added int return
        type, in test program and T value program.
  2002-02-21 lpd Added missing #include <stdio.h> in test program.
  2000-07-03 lpd Patched to eliminate warnings about "constant is
        unsigned in ANSI C, signed in traditional"; made test program
        self-checking.
  1999-11-04 lpd Edited comments slightly for automatic TOC extraction.
  1999-10-18 lpd Fixed typo in header comment (ansi2knr rather than md5).
  1999-05-03 lpd Original version.
 */

#ifdef _WIN32 // to disable warning C4996 about sprintf being deprecated
# include "../config.h.win"
#elif defined( _WIN32_WCE )
# include "../config.h.win"
// #else
// # include "config.h"
#endif

#include "md4.h"

#include <cstdio>
#include <string.h>

#include <cstdio> // [s]print[f]

namespace gloox
{
// #undef BYTE_ORDER    /* 1 = big-endian, -1 = little-endian, 0 = unknown */
// #ifdef ARCH_IS_BIG_ENDIAN
// #  define BYTE_ORDER (ARCH_IS_BIG_ENDIAN ? 1 : -1)
// #else
// #  define BYTE_ORDER 0
// #endif

#undef BYTE_ORDER
#define BYTE_ORDER 0

#define T_MASK ((unsigned int)~0)
#define T1 /* 0xd76aa478 */ (T_MASK ^ 0x28955b87)
#define T2 /* 0xe8c7b756 */ (T_MASK ^ 0x173848a9)
#define T3    0x242070db
#define T4 /* 0xc1bdceee */ (T_MASK ^ 0x3e423111)
#define T5 /* 0xf57c0faf */ (T_MASK ^ 0x0a83f050)
#define T6    0x4787c62a
#define T7 /* 0xa8304613 */ (T_MASK ^ 0x57cfb9ec)
#define T8 /* 0xfd469501 */ (T_MASK ^ 0x02b96afe)
#define T9    0x698098d8
#define T10 /* 0x8b44f7af */ (T_MASK ^ 0x74bb0850)
#define T11 /* 0xffff5bb1 */ (T_MASK ^ 0x0000a44e)
#define T12 /* 0x895cd7be */ (T_MASK ^ 0x76a32841)
#define T13    0x6b901122
#define T14 /* 0xfd987193 */ (T_MASK ^ 0x02678e6c)
#define T15 /* 0xa679438e */ (T_MASK ^ 0x5986bc71)
#define T16    0x49b40821
#define T17 /* 0xf61e2562 */ (T_MASK ^ 0x09e1da9d)
#define T18 /* 0xc040b340 */ (T_MASK ^ 0x3fbf4cbf)
#define T19    0x265e5a51
#define T20 /* 0xe9b6c7aa */ (T_MASK ^ 0x16493855)
#define T21 /* 0xd62f105d */ (T_MASK ^ 0x29d0efa2)
#define T22    0x02441453
#define T23 /* 0xd8a1e681 */ (T_MASK ^ 0x275e197e)
#define T24 /* 0xe7d3fbc8 */ (T_MASK ^ 0x182c0437)
#define T25    0x21e1cde6
#define T26 /* 0xc33707d6 */ (T_MASK ^ 0x3cc8f829)
#define T27 /* 0xf4d50d87 */ (T_MASK ^ 0x0b2af278)
#define T28    0x455a14ed
#define T29 /* 0xa9e3e905 */ (T_MASK ^ 0x561c16fa)
#define T30 /* 0xfcefa3f8 */ (T_MASK ^ 0x03105c07)
#define T31    0x676f02d9
#define T32 /* 0x8d2a4c8a */ (T_MASK ^ 0x72d5b375)
#define T33 /* 0xfffa3942 */ (T_MASK ^ 0x0005c6bd)
#define T34 /* 0x8771f681 */ (T_MASK ^ 0x788e097e)
#define T35    0x6d9d6122
#define T36 /* 0xfde5380c */ (T_MASK ^ 0x021ac7f3)
#define T37 /* 0xa4beea44 */ (T_MASK ^ 0x5b4115bb)
#define T38    0x4bdecfa9
#define T39 /* 0xf6bb4b60 */ (T_MASK ^ 0x0944b49f)
#define T40 /* 0xbebfbc70 */ (T_MASK ^ 0x4140438f)
#define T41    0x289b7ec6
#define T42 /* 0xeaa127fa */ (T_MASK ^ 0x155ed805)
#define T43 /* 0xd4ef3085 */ (T_MASK ^ 0x2b10cf7a)
#define T44    0x04881d05
#define T45 /* 0xd9d4d039 */ (T_MASK ^ 0x262b2fc6)
#define T46 /* 0xe6db99e5 */ (T_MASK ^ 0x1924661a)
#define T47    0x1fa27cf8
#define T48 /* 0xc4ac5665 */ (T_MASK ^ 0x3b53a99a)
#define T49 /* 0xf4292244 */ (T_MASK ^ 0x0bd6ddbb)
#define T50    0x432aff97
#define T51 /* 0xab9423a7 */ (T_MASK ^ 0x546bdc58)
#define T52 /* 0xfc93a039 */ (T_MASK ^ 0x036c5fc6)
#define T53    0x655b59c3
#define T54 /* 0x8f0ccc92 */ (T_MASK ^ 0x70f3336d)
#define T55 /* 0xffeff47d */ (T_MASK ^ 0x00100b82)
#define T56 /* 0x85845dd1 */ (T_MASK ^ 0x7a7ba22e)
#define T57    0x6fa87e4f
#define T58 /* 0xfe2ce6e0 */ (T_MASK ^ 0x01d3191f)
#define T59 /* 0xa3014314 */ (T_MASK ^ 0x5cfebceb)
#define T60    0x4e0811a1
#define T61 /* 0xf7537e82 */ (T_MASK ^ 0x08ac817d)
#define T62 /* 0xbd3af235 */ (T_MASK ^ 0x42c50dca)
#define T63    0x2ad7d2bb
#define T64 /* 0xeb86d391 */ (T_MASK ^ 0x14792c6e)


  const unsigned char MD4::pad[64] =
  {
    0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  };

  const std::string MD4::md4( const std::string& data )
  {
    MD4 md4;
    md4.feed( data );
    return md4.hex();
  }

  MD4::MD4()
    : m_finished( false )
  {
    init();
  }

  MD4::~MD4()
  {
  }

  void MD4::process( const unsigned char* data /*[64]*/ )
  {
    unsigned int a = m_state.abcd[0];
    unsigned int b = m_state.abcd[1];
    unsigned int c = m_state.abcd[2];
    unsigned int d = m_state.abcd[3];
    unsigned int t;
#if BYTE_ORDER > 0
    /* Define storage only for big-endian CPUs. */
    unsigned int X[16];
#else
    /* Define storage for little-endian or both types of CPUs. */
    unsigned int xbuf[16];
    const unsigned int *X;
#endif

    {
#if BYTE_ORDER == 0
      /*
      * Determine dynamically whether this is a big-endian or
      * little-endian machine, since we can use a more efficient
      * algorithm on the latter.
      */
      static const int w = 1;

      if( *((const unsigned char *)&w) ) /* dynamic little-endian */
#endif
#if BYTE_ORDER <= 0             /* little-endian */
      {
        /*
        * On little-endian machines, we can process properly aligned
        * data without copying it.
        */
        if( !((data - (const unsigned char*)0) & 3) )
        {
          /* data are properly aligned */
          X = (const unsigned int*)data;
        }
        else
        {
          /* not aligned */
          memcpy( xbuf, data, 64 );
          X = xbuf;
        }
      }
#endif
#if BYTE_ORDER == 0
      else // dynamic big-endian
#endif
#if BYTE_ORDER >= 0 // big-endian
      {
        /*
        * On big-endian machines, we must arrange the bytes in the
        * right order.
        */
        const unsigned char* xp = data;
        int i;

#  if BYTE_ORDER == 0
        X = xbuf; // (dynamic only)
#  else
#    define xbuf X  /* (static only) */
#  endif
        for( i = 0; i < 16; ++i, xp += 4 )
          xbuf[i] = xp[0] + ( xp[1] << 8 ) + ( xp[2] << 16 ) + ( xp[3] << 24 );
      }
#endif
    }

#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

    /* Round 1. */
    /* Let [abcd k s] denote the operation
       a = (a + F(b,c,d) + X[k] <<< s). */
#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define SET(a, b, c, d, k, s)\
  t = a + F(b,c,d) + X[k];\
  a = ROTATE_LEFT(t, s)
    /* Do the following 16 operations. */
    SET(a, b, c, d,  0,  3);
    SET(d, a, b, c,  1,  7);
    SET(c, d, a, b,  2, 11);
    SET(b, c, d, a,  3, 19);
    SET(a, b, c, d,  4,  3);
    SET(d, a, b, c,  5,  7);
    SET(c, d, a, b,  6, 11);
    SET(b, c, d, a,  7, 19);
    SET(a, b, c, d,  8,  3);
    SET(d, a, b, c,  9,  7);
    SET(c, d, a, b, 10, 11);
    SET(b, c, d, a, 11, 19);
    SET(a, b, c, d, 12,  3);
    SET(d, a, b, c, 13,  7);
    SET(c, d, a, b, 14, 11);
    SET(b, c, d, a, 15, 19);
#undef SET

     /* Round 2. */
     /* Let [abcd k s ] denote the operation
          a = (a + G(b,c,d) + X[k] + 0x5A827999) <<< s. */
#define G(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define SET(a, b, c, d, k, s)\
  t = a + G(b,c,d) + X[k] + 0x5A827999;\
  a = ROTATE_LEFT(t, s)
     /* Do the following 16 operations. */
    SET(a, b, c, d,  0,  3);
    SET(d, a, b, c,  4,  5);
    SET(c, d, a, b,  8,  9);
    SET(b, c, d, a, 12, 13);
    SET(a, b, c, d,  1,  3);
    SET(d, a, b, c,  5,  5);
    SET(c, d, a, b,  9,  9);
    SET(b, c, d, a, 13, 13);
    SET(a, b, c, d,  2,  3);
    SET(d, a, b, c,  6,  5);
    SET(c, d, a, b, 10,  9);
    SET(b, c, d, a, 14, 13);
    SET(a, b, c, d,  3,  3);
    SET(d, a, b, c,  7,  5);
    SET(c, d, a, b, 11,  9);
    SET(b, c, d, a, 15, 13);
#undef SET

     /* Round 3. */
     /* Let [abcd k s t] denote the operation
          a = b + ((a + H(b,c,d) + X[k] + T[i]) <<< s). */
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define SET(a, b, c, d, k, s)\
  t = a + H(b,c,d) + X[k] + 0x6ED9EBA1;\
  a = ROTATE_LEFT(t, s)
     /* Do the following 16 operations. */
    SET(a, b, c, d,  0,  3);
    SET(d, a, b, c,  8,  9);
    SET(c, d, a, b,  4, 11);
    SET(b, c, d, a, 12, 15);
    SET(a, b, c, d,  2,  3);
    SET(d, a, b, c, 10,  9);
    SET(c, d, a, b,  6, 11);
    SET(b, c, d, a, 14, 15);
    SET(a, b, c, d,  1,  3);
    SET(d, a, b, c,  9,  9);
    SET(c, d, a, b,  5, 11);
    SET(b, c, d, a, 13, 15);
    SET(a, b, c, d,  3,  3);
    SET(d, a, b, c, 11,  9);
    SET(c, d, a, b,  7, 11);
    SET(b, c, d, a, 15, 15);
#undef SET


     /* Then perform the following additions. (That is increment each
        of the four registers by the value it had before this block
        was started.) */
    m_state.abcd[0] += a;
    m_state.abcd[1] += b;
    m_state.abcd[2] += c;
    m_state.abcd[3] += d;
  }

  void MD4::init()
  {
    m_finished = false;
    m_state.count[0] = 0;
    m_state.count[1] = 0;
    m_state.abcd[0] = 0x67452301;
    m_state.abcd[1] = /*0xefcdab89*/ T_MASK ^ 0x10325476;
    m_state.abcd[2] = /*0x98badcfe*/ T_MASK ^ 0x67452301;
    m_state.abcd[3] = 0x10325476;
  }

  void MD4::feed( const std::string& data )
  {
    feed( (const unsigned char*)data.c_str(), (int)data.length() );
  }

  void MD4::feed( const unsigned char* data, int bytes )
  {
    const unsigned char* p = data;
    int left = bytes;
    int offset = ( m_state.count[0] >> 3 ) & 63;
    unsigned int nbits = (unsigned int)( bytes << 3 );

    if( bytes <= 0 )
      return;

    /* Update the message length. */
    m_state.count[1] += bytes >> 29;
    m_state.count[0] += nbits;
    if( m_state.count[0] < nbits )
      m_state.count[1]++;

    /* Process an initial partial block. */
    if( offset )
    {
      int copy = ( offset + bytes > 64 ? 64 - offset : bytes );

      memcpy( m_state.buf + offset, p, copy );
      if( offset + copy < 64 )
        return;
      p += copy;
      left -= copy;
      process( m_state.buf );
    }

    /* Process full blocks. */
    for( ; left >= 64; p += 64, left -= 64 )
      process( p );

    /* Process a final partial block. */
    if( left )
      memcpy( m_state.buf, p, left );
  }

  void MD4::finalize()
  {
    if( m_finished )
      return;

    unsigned char data[8];

    /* Save the length before padding. */
    for( int i = 0; i < 8; ++i )
      data[i] = (unsigned char)( m_state.count[i >> 2] >> ( ( i & 3 ) << 3 ) );

    /* Pad to 56 bytes mod 64. */
    feed( pad, ( ( 55 - ( m_state.count[0] >> 3 ) ) & 63 ) + 1 );

    /* Append the length. */
    feed( data, 8 );

    m_finished = true;
  }

  const std::string MD4::hex()
  {
    if( !m_finished )
      finalize();

    char buf[33];

    for( int i = 0; i < 16; ++i )
      sprintf( buf + i * 2, "%02x", (unsigned char)( m_state.abcd[i >> 2] >> ( ( i & 3 ) << 3 ) ) );

    return std::string( buf, 32 );
  }

  const std::string MD4::binary()
  {
    if( !m_finished )
      finalize();

    unsigned char digest[16];
    for( int i = 0; i < 16; ++i )
      digest[i] = (unsigned char)( m_state.abcd[i >> 2] >> ( ( i & 3 ) << 3 ) );

    return std::string( (char*)digest, 16 );
  }

  void MD4::reset()
  {
    init();
  }

}

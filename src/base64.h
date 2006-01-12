/*
  Copyright (c) 2005 by Jakob Schroeter <js@camaya.net>
  This file is part of the gloox library. http://camaya.net/gloox

  This software is distributed under a license. The full license
  agreement can be found in the file LICENSE in this distribution.
  This software may not be copied, modified, sold or distributed
  other than expressed in the named license agreement.

  This software is distributed without any warranty.
*/


#ifndef BASE64_H__
#define BASE64_H__

#include <string>

namespace gloox
{

  /**
   * @brief An implementation of the Base64 data encoding (RFC 3548)
   *
   * @author Jakob Schroeter <js@camaya.net>
   * @since 0.8
   */
  class Base64
  {

    public:
      /**
       *
       */
      static const std::string encode64( const std::string& input );

      /**
       *
       */
      static const std::string decode64( const std::string& input );

      /**
       *
       */
    private:
      static const std::string alphabet64;

  };

//   const std::string Base64::alphabet32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
//   const std::string Base64::alphabet16 = "0123456789ABCDEF";

}

#endif // BASE64_H__

/*
  Copyright (c) 2004-2005 by Jakob Schroeter <js@camaya.net>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
*/



#ifndef IQHANDLER_H__
#define IQHANDLER_H__

#include <iksemel.h>
#include <string>

using namespace std;

/**
 * A virtual interface.
 * Derived classes can be registered as IqHandlers with the JClient.
 * Upon an incoming IQ packet @ref handleIq() will be called.
 * @author Jakob Schroeter <js@camaya.net>
 */
class IqHandler
{
  public:
    /**
     * Reimplement this function if you want to be notified about
     * incoming IQs.
     * @param xmlns The XML namespace of the IQ packet
     * @param pak The complete packet for convenience
     */
    virtual void handleIq( const char* xmlns, ikspak* pak ) {};

    /**
     * Reimplement this function if you want to be notified about
     * incoming IQs with a specific child tag.
     * @param tag The sub tag's name of the IQ packet
     * @param pak The complete packet for convenience
     */
    virtual void handleIqTag( const char* tag, ikspak* pak ) {};

    /**
     * Reimplement this function if you want to be notified about
     * incoming IQs with a specific value of the @c id attribute. You
     * have to enable tracking of those IDs using @c JClient::trackID().
     * This is usually useful for IDs that generate a positive reply, i.e.
     * &lt;iq type='result' id='reg2'/&gt; where a namespace filter wouldn't
     * work.
     * @param id The ID that was trackked.
     * @param pak 0 if the stanza was of type 'result', the complete packet
     * for convenience if not.
     */
    virtual void handleIqID( const char* id, ikspak* pak ) {};
};

#endif // IQHANDLER_H__

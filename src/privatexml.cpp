/*
  Copyright (c) 2004-2007 by Jakob Schroeter <js@camaya.net>
  This file is part of the gloox library. http://camaya.net/gloox

  This software is distributed under a license. The full license
  agreement can be found in the file LICENSE in this distribution.
  This software may not be copied, modified, sold or distributed
  other than expressed in the named license agreement.

  This software is distributed without any warranty.
*/


#include "privatexml.h"
#include "clientbase.h"
#include "stanza.h"

namespace gloox
{

  PrivateXML::PrivateXML( ClientBase* parent )
    : m_parent( parent )
  {
    if( m_parent )
      m_parent->registerIqHandler( this, XMLNS_PRIVATE_XML );
  }

  PrivateXML::~PrivateXML()
  {
    if( m_parent )
      m_parent->removeIqHandler( this, XMLNS_PRIVATE_XML );
  }

  std::string PrivateXML::requestXML( const std::string& tag, const std::string& xmlns,
                                      PrivateXMLHandler* pxh )
  {
    const std::string& id = m_parent->getID();

    IQ iq( IQ::Get, JID(), id, XMLNS_PRIVATE_XML );
    new Tag( iq.query(), tag, XMLNS, xmlns );

    m_track[id] = pxh;
    m_parent->send( iq, this, RequestXml );

    return id;
  }

  std::string PrivateXML::storeXML( Tag* tag, PrivateXMLHandler* pxh )
  {
    const std::string& id = m_parent->getID();

    IQ iq( IQ::Set, JID(), id, XMLNS_PRIVATE_XML );
    iq.query()->addChild( tag );

    m_track[id] = pxh;
    m_parent->send( iq, this, StoreXml );

    return id;
  }

  void PrivateXML::handleIqID( const IQ& iq, int context )
  {
    TrackMap::iterator t = m_track.find( iq.id() );
    if( t != m_track.end() )
    {
      switch( iq.subtype() )
      {
        case IQ::Result:
        {
          switch( context )
          {
            case RequestXml:
            {
              Tag* q = iq.query();
              if( q )
              {
                const TagList& l = q->children();
                TagList::const_iterator it = l.begin();
                if( it != l.end() )
                {
                  (*t).second->handlePrivateXML( (*it)->name(), (*it) );
                }
              }
              break;
            }

            case StoreXml:
            {
              (*t).second->handlePrivateXMLResult( iq.id(), PrivateXMLHandler::PxmlStoreOk );
              break;
            }
          }
          m_track.erase( t );
          return;
          break;
        }
        case IQ::Error:
        {
          switch( context )
          {
            case RequestXml:
            {
              (*t).second->handlePrivateXMLResult( iq.id(), PrivateXMLHandler::PxmlRequestError );
              break;
            }

            case StoreXml:
            {
              (*t).second->handlePrivateXMLResult( iq.id(), PrivateXMLHandler::PxmlStoreError );
              break;
            }
          }
          break;
        }
        default:
          break;
      }

      m_track.erase( t );
    }
  }

}

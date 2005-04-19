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
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
*/


#include "jclient.h"
#include "jthread.h"
#include "roster.h"


#include <iostream>

#define JLIB_VERSION "0.1"


JClient::JClient()
  : m_port( XMPP_PORT ), m_thread( 0 ),
  m_tls( true ), m_sasl( true ),
  m_autoPresence( false ), m_manageRoster( true ),
  m_handleDiscoInfo( true ), m_handleDiscoItems( true ),
  m_idCount( 0 ), m_roster( 0 )
{
  init();
}

JClient::JClient( const std::string& id, const std::string& password, int port )
  : m_port( port ), m_password( password ), m_thread( 0 ),
  m_tls( true ), m_sasl( true ),
  m_autoPresence( false ), m_manageRoster( true ),
  m_handleDiscoInfo( true ), m_handleDiscoItems( true ),
  m_idCount( 0 ), m_roster( 0 )
{
  m_self = iks_id_new( get_stack(), id.c_str() );
  m_username = m_self->user;
  m_server = m_self->server;
  m_resource = m_self->resource;
  printf("u: %s, s: %s, r: %s, p: %s\n", m_username.c_str(),m_server.c_str(),m_resource.c_str(),m_password.c_str());
  init();
}

JClient::JClient( const std::string& username, const std::string& password, const std::string& server,
                  const std::string& resource, int port )
  : m_username( username ), m_resource( resource ), m_password( password ),
  m_server( server ), m_port( port ), m_thread( 0 ),
  m_tls( true ), m_sasl( true ),
  m_autoPresence( false ), m_manageRoster( true ),
  m_handleDiscoInfo( true ), m_handleDiscoItems( true ),
  m_idCount( 0 ), m_roster( 0 )
{
  init();
}

JClient::~JClient()
{
}

void JClient::init()
{
  setFeature( XMLNS_VERSION );
  setFeature( XMLNS_DISCO_INFO );
  setFeature( XMLNS_DISCO_ITEMS );
  setVersion( "JLib", JLIB_VERSION );
  setIdentity( "client", "bot" );
  m_roster = new Roster( this );
}

void JClient::on_stream( int type, iks* node )
{
  if( m_debug ) printf("in on_stream\n");
  ikspak* pak = iks_packet( node );

  switch (type)
  {
    case IKS_NODE_START:      // <stream:stream>
      if ( m_tls && !is_secure() )
      {
        start_tls();
        if( m_debug ) printf("after starttls\n");
        break;
      }
      if ( !m_sasl )
        login( iks_find_attrib ( node, "id" ) );
      break;
      case IKS_NODE_NORMAL:     // first level child of stream
        if ( strncmp( "stream:features", iks_name( node ), 15 ) == 0 ) {
          m_streamFeatures = iks_stream_features( node );
          if ( m_sasl ) {
            if ( m_tls && !is_secure() )
              break;
            if ( m_authorized ) {
              iks* t;
              if ( m_streamFeatures & IKS_STREAM_BIND ) {
                send( iks_make_resource_bind( m_self ) );
              }
              if ( m_streamFeatures & IKS_STREAM_SESSION ) {
                iks* x = iks_make_session();
                iks_insert_attrib( x, "id", "auth" );
                send( x );
              }
            } else {
              if ( m_streamFeatures & IKS_STREAM_SASL_MD5 )
                start_sasl( IKS_SASL_DIGEST_MD5, (char *) username().c_str(), (char *) password().c_str() );
              else if ( m_streamFeatures & IKS_STREAM_SASL_PLAIN )
                start_sasl( IKS_SASL_PLAIN, (char *) username().c_str(), (char *) password().c_str() );
            }
          }
        } else if ( strcmp ( "failure", iks_name ( node ) ) == 0 ) {
          if( m_debug ) printf("sasl authentication failed...\n");
          m_state = STATE_AUTHENTICATION_FAILED;
        } else if ( strcmp ( "success", iks_name ( node ) ) == 0 ) {
          if( m_debug ) printf( "sasl initialisation successful...\n" );
          m_state = STATE_AUTHENTICATED;
          m_authorized = true;
          header( server() );
        } else {
          ikspak* pak;
          pak = iks_packet ( node );
          iks_filter_packet ( m_filter, pak );
        }
        break;
    case IKS_NODE_ERROR:      // <stream:error>
//       syslog(LOG_ERR, "stream error. quitting\n");
//       logout();
      break;
    case IKS_NODE_STOP:       // </stream:stream>
      break;
  }
//   if( m_debug ) printf("at the end of on_stream\n");
}


void JClient::on_log( const char* data, size_t size, int is_incoming ) {
  if( m_debug )
  {
    if ( is_secure() )
      cerr << "Sec";

    if (is_incoming)
      cerr << "RECV ";
    else
      cerr << "SEND ";

    cerr << "[" << data << "]" << endl;
  }
}

void JClient::setVersion( const char* name, const char* version )
{
  m_versionName = strdup( name );
  m_versionVersion = strdup( version );
}

void JClient::setIdentity( const char* category, const char* type )
{
  m_identityCategory = strdup( category );
  m_identityType = strdup( type );
}

void JClient::setFeature( const char* feature )
{
  m_discoCapabilities.push_back( strdup( feature ) );
}

void JClient::disableDiscoInfo()
{
  m_handleDiscoInfo = false;
  m_discoCapabilities.clear();
}

void JClient::disableDiscoItems()
{
  m_handleDiscoItems = false;
}

void JClient::disableRosterManagement()
{
  m_manageRoster = false;
  delete m_roster;
  m_roster = 0;
}

void JClient::getDiscoInfo( const char* to )
{
  std::string id = getID();
  iks* x = iks_make_iq( IKS_TYPE_GET, XMLNS_DISCO_INFO );
  iks_insert_attrib( x, "from", jid().c_str() );
  iks_insert_attrib( x, "to", to );
  iks_insert_attrib( x, "id", id.c_str() );
  send( x );
  addQueryID( to, id );
}

void JClient::getDiscoItems( const char* to )
{
  std::string id = getID();
  iks* x = iks_make_iq( IKS_TYPE_GET, XMLNS_DISCO_ITEMS );
  iks_insert_attrib( x, "from", jid().c_str() );
  iks_insert_attrib( x, "to", to );
  iks_insert_attrib( x, "id", id.c_str() );
  send( x );
  addQueryID( to, id );
}

void JClient::addQueryID( std::string jid, std::string id )
{
  m_queryIDs[id] = jid;
}

std::string JClient::getID()
{
  char* tmp = (char*)malloc( strlen( "id" ) + sizeof( int ) );
  tmp = strdup( "id%d" );
  sprintf( tmp, tmp, ++m_idCount );
  std::string str( tmp );
  free( tmp );
  return str;
}

void JClient::login( char* sid )
{
  if( m_debug ) printf("in login()\n");

  iks* x = iks_make_auth( m_self, password().c_str(), sid );
  iks_insert_attrib( x, "id", "auth" );
  send( x );
}

void JClient::setupFilter()
{
  m_filter = iks_filter_new();

  iks_filter_add_rule( m_filter, (iksFilterHook*) authHook, this,
                      IKS_RULE_TYPE, IKS_PAK_IQ,
                      IKS_RULE_SUBTYPE, IKS_TYPE_RESULT,
                      IKS_RULE_ID, "auth",
                      IKS_RULE_DONE );
  iks_filter_add_rule( m_filter, (iksFilterHook*) registerHook, this,
                      IKS_RULE_TYPE, IKS_PAK_IQ,
                      IKS_RULE_SUBTYPE, IKS_TYPE_ERROR,
                      IKS_RULE_ID, "auth",
                      IKS_RULE_DONE );
  iks_filter_add_rule( m_filter, (iksFilterHook*) registeredHook, this,
                      IKS_RULE_TYPE, IKS_PAK_IQ,
                      IKS_RULE_SUBTYPE, IKS_TYPE_RESULT,
                      IKS_RULE_ID, "reg",
                      IKS_RULE_DONE );
  iks_filter_add_rule( m_filter, (iksFilterHook*) errorHook, this,
                      IKS_RULE_TYPE, IKS_PAK_IQ,
                      IKS_RULE_SUBTYPE, IKS_TYPE_ERROR,
                      IKS_RULE_DONE );
  iks_filter_add_rule( m_filter, (iksFilterHook*) msgHook, this,
                      IKS_RULE_TYPE, IKS_PAK_MESSAGE,
                      IKS_RULE_DONE );
  iks_filter_add_rule( m_filter, (iksFilterHook*) presenceHook, this,
                      IKS_RULE_TYPE, IKS_PAK_PRESENCE,
                      IKS_RULE_DONE );
  iks_filter_add_rule( m_filter, (iksFilterHook*) subscriptionHook, this,
                      IKS_RULE_TYPE, IKS_PAK_S10N,
                      IKS_RULE_DONE );
  iks_filter_add_rule( m_filter, (iksFilterHook*) iqHook, this,
                      IKS_RULE_TYPE, IKS_PAK_IQ,
                      IKS_RULE_DONE );
}

void JClient::connect()
{
  if( jid().empty() )
    return;

  m_self = iks_id_new( get_stack(), jid().c_str() );
  setupFilter();

  m_state = STATE_CONNECTING;
  int ret;
  if(ret = Stream::connect( m_server, m_port ) )
  {
    switch( ret )
    {
      case IKS_NET_NODNS:
        printf( "host name lookup failure: %s\n", m_server.c_str() );
        break;
      case IKS_NET_NOSOCK:
        printf( "cannot create socket\n" );
        break;
      case IKS_NET_NOCONN:
        printf( "connection refused or no xml stream: %s:%d\n", m_server.c_str(), m_port );
        break;
      case IKS_NET_RWERR:
        printf( "read/write error: %s\n", m_server.c_str() );
        break;
    }
    return;
  }

  m_state = STATE_CONNECTED;

  m_thread = new JThread( this );
  m_thread->start();

  while( m_state >= STATE_CONNECTED &&
         m_state != STATE_AUTHENTICATED &&
         m_state != STATE_AUTHENTICATION_FAILED )
  {
    JThread::sleep( 1000 );
  }

  if ( m_state == STATE_AUTHENTICATION_FAILED )
  {
    if( m_createAccount )
    {
//       createAccount();
    }
  }

  m_thread->join();
}

void JClient::disconnect()
{
  m_thread->cancel();
  Stream::disconnect();
  m_state = STATE_DISCONNECTED;
  delete( m_thread );
  m_thread = 0;
}

void JClient::send( iks* x )
{
  Stream::send( this->P, x );
  iks_free( x );
}

void JClient::sendPresence()
{
  iks* x = iks_make_pres( IKS_SHOW_AVAILABLE, "online" );
  send( x );
}

void JClient::send( const char* jid, const char* data )
{
  iks* x = iks_make_msg( IKS_TYPE_NONE, jid, data );
  send( x );
}

JClient::state JClient::clientState()
{
  return m_state;
}

void JClient::setClientState( state s )
{
  m_state = s;
}

iksparser* JClient::parser()
{
  return this->P;
}

Roster* JClient::roster()
{
  return m_roster;
}

void JClient::registerPresenceHandler( PresenceHandler* ph )
{
  m_presenceHandlers.push_back( ph );
}

void JClient::removePresenceHandler( PresenceHandler* ph )
{
  m_presenceHandlers.remove( ph );
}

void JClient::registerIqHandler( IqHandler* ih, const char* xmlns )
{
  m_iqNSHandlers[strdup( xmlns )] = ih;
}

void JClient::registerIqHandler( IqHandler* ih )
{
  m_iqHandlers.push_back( ih );
}

void JClient::removeIqHandler( const char* xmlns )
{
  m_iqNSHandlers.erase( xmlns );
}

void JClient::removeIqHandler( IqHandler* ih )
{
  m_iqHandlers.remove( ih );
}

void JClient::registerMessageHandler( MessageHandler* mh )
{
  m_messageHandlers.push_back( mh );
}

void JClient::removeMessageHandler( MessageHandler* mh )
{
  m_messageHandlers.remove( mh );
}

void JClient::registerSubscriptionHandler( SubscriptionHandler* sh )
{
  m_subscriptionHandlers.push_back( sh );
}

void JClient::removeSubscriptionHandler( SubscriptionHandler* sh )
{
  m_subscriptionHandlers.remove( sh );
}

void JClient::registerConnectionListener( ConnectionListener* cl )
{
  m_connectionListeners.push_back( cl );
}

void JClient::removeConnectionListener( ConnectionListener* cl )
{
  m_connectionListeners.remove( cl );
}

void JClient::notifyOnConnect()
{
  if( m_manageRoster )
    m_roster->fill();

  if( m_autoPresence )
    sendPresence();

  ConnectionListenerList::const_iterator it = m_connectionListeners.begin();
  for( it; it != m_connectionListeners.end(); it++ ) {
    (*it)->onConnect();
  }
}

void JClient::notifyOnDisconnect()
{
  ConnectionListenerList::const_iterator it = m_connectionListeners.begin();
  for( it; it != m_connectionListeners.end(); it++ ) {
    (*it)->onDisconnect();
  }
}

void JClient::notifyPresenceHandlers( iksid* from, iksubtype type, ikshowtype show, const char* msg )
{
  PresenceHandlerList::const_iterator it = m_presenceHandlers.begin();
  for( it; it != m_presenceHandlers.end(); it++ ) {
    (*it)->handlePresence( from, type, show, msg );
  }
}

void JClient::notifySubscriptionHandlers( iksid* from, iksubtype type, const char* msg )
{
  SubscriptionHandlerList::const_iterator it = m_subscriptionHandlers.begin();
  for( it; it != m_subscriptionHandlers.end(); it++ ) {
    (*it)->handleSubscription( from, type, msg );
  }
}

void JClient::notifyIqHandlers( const char* xmlns, ikspak* pak )
{
  if( iks_strncmp( xmlns, XMLNS_VERSION, iks_strlen( XMLNS_VERSION ) ) == 0 )
  {
    iks* x = iks_new( "iq" );
    iks_insert_attrib( x, "type", "result" );
    iks_insert_attrib( x, "to", pak->from->full );
    iks_insert_attrib( x, "from", jid().c_str() );
    iks_insert_attrib( x, "id", pak->id );
    iks* y = iks_insert( x, "query" );
    iks_insert_attrib( y, "xmlns", XMLNS_VERSION );
    iks* z = iks_insert( y, "name" );
    iks_insert_cdata( z, m_versionName.c_str(), m_versionName.length() );
    z = iks_insert( y, "version" );
    iks_insert_cdata( z, m_versionVersion.c_str(), m_versionVersion.length() );
    send( x );
  }
  else if( ( iks_strncmp( xmlns, XMLNS_DISCO_INFO, iks_strlen( XMLNS_DISCO_INFO ) ) == 0 )
             && ( m_handleDiscoInfo ) )
  {
    iks* x = iks_new( "iq" );
    iks_insert_attrib( x, "type", "result" );
    iks_insert_attrib( x, "id", pak->id );
    iks_insert_attrib( x, "to", pak->from->full );
    iks_insert_attrib( x, "from", jid().c_str() );
    iks* y = iks_insert( x, "query" );
    iks_insert_attrib( y, "xmlns", XMLNS_DISCO_INFO );
    iks* i = iks_insert( y, "identity" );
    iks_insert_attrib( i, "category", m_identityCategory.c_str() );
    iks_insert_attrib( i, "type", m_identityType.c_str() );
    iks_insert_attrib( i, "name", m_versionName.c_str() );

    CharList::const_iterator it = m_discoCapabilities.begin();
    for( it; it != m_discoCapabilities.end(); ++it )
    {
      iks* z = iks_insert( y, "feature" );
      iks_insert_attrib( z, "var", (*it) );
    }
    send( x );
  }
  else if( ( iks_strncmp( xmlns, XMLNS_DISCO_ITEMS, iks_strlen( XMLNS_DISCO_ITEMS ) ) == 0 )
             && ( m_handleDiscoItems ) )
  {
    iks* x = iks_new( "iq" );
    iks_insert_attrib( x, "type", "result" );
    iks_insert_attrib( x, "id", pak->id );
    iks_insert_attrib( x, "to", pak->from->full );
    iks_insert_attrib( x, "from", jid().c_str() );
    iks* y = iks_insert( x, "query" );
    iks_insert_attrib( y, "xmlns", XMLNS_DISCO_ITEMS );
    send( x );
  }
  else
  {
    IqHandlerList::const_iterator it = m_iqHandlers.begin();
    for( it; it != m_iqHandlers.end(); it++ ) {
      (*it)->handleIq( xmlns, pak );
    }
    IqHandlerMap::const_iterator it_ns = m_iqNSHandlers.begin();
    for( it_ns; it_ns != m_iqNSHandlers.end(); it_ns++ ) {
      if( iks_strncmp( (*it_ns).first, xmlns, iks_strlen( xmlns ) ) == 0 )
        (*it_ns).second->handleIq( xmlns, pak );
    }
  }
}

void JClient::notifyMessageHandlers( iksid* from, iksubtype type, const char* msg )
{
  MessageHandlerList::const_iterator it = m_messageHandlers.begin();
  for( it; it != m_messageHandlers.end(); it++ ) {
    (*it)->handleMessage( from, type, msg );
  }
}

void authHook( JClient* stream, ikspak* pak )
{
  if( stream->debug() ) printf("authHook\n");
  stream->notifyOnConnect();
}

void registerHook( JClient* stream, ikspak* pak )
{
  stream->setClientState( JClient::STATE_AUTHENTICATION_FAILED );
//   if (config.autoreg)
//   {
//     iks *x, *y;
//     iksid* id;
//     id = iks_id_new(m_stack, config.jabberID);
// 
//     x = iks_new("iq");
//     iks_insert_attrib(x, "type", "set");
//     iks_insert_attrib(x, "id", "reg");
//     y = iks_insert(x, "query");
//     iks_insert_attrib(y, "xmlns", XMLNS_REGISTER );
//     iks_insert_cdata(iks_insert(y, "username"), id->user, strlen(id->user));
//     iks_insert_cdata(iks_insert(y, "password"), config.jabberPwd, strlen(config.jabberPwd));
//     iks_send(m_prs, x);
// //    iks_delete(y);
//     iks_delete(x);
//   }
//   else
//   {
//     if( stream->debug() ) printf("jabber account does not exist and autoreg is false. exiting.\n");
//     m_continue = 0;
//   }
}

void registeredHook( JClient* stream, ikspak* pak )
{
  if( stream->debug() ) printf("registeredHook\n");
  stream->setClientState( JClient::STATE_AUTHENTICATED );
}

void msgHook( JClient* stream, ikspak* pak )
{
  if( stream->debug() ) printf("msgHook\n");
  stream->notifyMessageHandlers( pak->from, pak->subtype, iks_find_cdata( pak->x, "body" ) );
}

void iqHook( JClient* stream, ikspak* pak )
{
  if( stream->debug() ) printf("iqHook\n");
  stream->notifyIqHandlers( pak->ns, pak );
}

void presenceHook( JClient* stream, ikspak* pak )
{
  if( stream->debug() ) printf("presenceHook\n");
  stream->notifyPresenceHandlers( pak->from, pak->subtype, pak->show, iks_find_cdata( pak->x, "status" ) );
}

void subscriptionHook( JClient* stream, ikspak* pak )
{
  if( stream->debug() ) printf("subscriptionHook\n");
  stream->notifySubscriptionHandlers( pak->from, pak->subtype, iks_find_cdata( pak->x, "status" ) );
}

void errorHook( JClient* stream, ikspak* pak )
{
  if( stream->debug() ) printf("errorHook\n");
}

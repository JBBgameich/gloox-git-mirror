/*
  Copyright (c) 2004-2006 by Jakob Schroeter <js@camaya.net>
  This file is part of the gloox library. http://camaya.net/gloox

  This software is distributed under a license. The full license
  agreement can be found in the file LICENSE in this distribution.
  This software may not be copied, modified, sold or distributed
  other than expressed in the named license agreement.

  This software is distributed without any warranty.
*/



#include "gloox.h"

#include "compression.h"
#include "connection.h"
#include "dns.h"
#include "logsink.h"
#include "prep.h"
#include "parser.h"

#ifdef __MINGW32__
#include <winsock.h>
#endif

#ifndef WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#else
#include <winsock.h>
#endif

#ifdef USE_WINTLS
# include <schannel.h>
#endif

#include <time.h>

#include <string>
#include <sstream>

namespace gloox
{

  Connection::Connection( Parser *parser, const LogSink& logInstance, const std::string& server,
                          unsigned short port )
    : m_parser( parser ), m_state ( StateDisconnected ), m_disconnect ( ConnNoError ),
      m_logInstance( logInstance ), m_compression( 0 ), m_buf( 0 ),
      m_server( Prep::idna( server ) ), m_port( port ), m_socket( -1 ), m_bufsize( 17000 ),
      m_cancel( true ), m_secure( false ), m_fdRequested( false ), m_enableCompression( false )
  {
    m_buf = (char*)calloc( m_bufsize + 1, sizeof( char ) );
#ifdef USE_OPENSSL
    m_ssl = 0;
#endif
  }

  Connection::~Connection()
  {
    cleanup();
    free( m_buf );
    m_buf = 0;
    m_parser = 0;
  }

#ifdef HAVE_TLS
  void Connection::setClientCert( const std::string& clientKey, const std::string& clientCerts )
  {
    m_clientKey = clientKey;
    m_clientCerts = clientCerts;
  }
#endif

#if defined( USE_OPENSSL )
  bool Connection::tlsHandshake()
  {
    SSL_library_init();
    SSL_CTX *sslCTX = SSL_CTX_new( TLSv1_client_method() );
    if( !sslCTX )
      return false;

    if( !SSL_CTX_set_cipher_list( sslCTX, "HIGH:MEDIUM:AES:@STRENGTH" ) )
      return false;

    StringList::const_iterator it = m_cacerts.begin();
    for( ; it != m_cacerts.end(); ++it )
      SSL_CTX_load_verify_locations( sslCTX, (*it).c_str(), NULL );

    if( !m_clientKey.empty() && !m_clientCerts.empty() )
    {
      SSL_CTX_use_certificate_chain_file( sslCTX, m_clientCerts.c_str() );
      SSL_CTX_use_PrivateKey_file( sslCTX, m_clientKey.c_str(), SSL_FILETYPE_PEM );
    }

    m_ssl = SSL_new( sslCTX );
    SSL_set_connect_state( m_ssl );

    BIO *socketBio = BIO_new_socket( m_socket, BIO_NOCLOSE );
    if( !socketBio )
      return false;

    SSL_set_bio( m_ssl, socketBio, socketBio );
    SSL_set_mode( m_ssl, SSL_MODE_AUTO_RETRY );

    if( !SSL_connect( m_ssl ) )
      return false;

    m_secure = true;

    int res = SSL_get_verify_result( m_ssl );
    if( res != X509_V_OK )
      m_certInfo.status = CertInvalid;
    else
      m_certInfo.status = CertOk;

    X509 *peer;
    peer = SSL_get_peer_certificate( m_ssl );
    if( peer )
    {
      char peer_CN[256];
      X509_NAME_get_text_by_NID( X509_get_issuer_name( peer ), NID_commonName, peer_CN, sizeof( peer_CN ) );
      m_certInfo.issuer = peer_CN;
      X509_NAME_get_text_by_NID( X509_get_subject_name( peer ), NID_commonName, peer_CN, sizeof( peer_CN ) );
      m_certInfo.server = peer_CN;
      std::string p;
      p.assign( peer_CN );
      int (*pf)( int ) = tolower;
      transform( p.begin(), p.end(), p.begin(), pf );
      if( p != m_server )
        m_certInfo.status |= CertWrongPeer;
    }
    else
    {
      m_certInfo.status = CertInvalid;
    }

    const char *tmp;
    tmp = SSL_get_cipher_name( m_ssl );
    if( tmp )
      m_certInfo.cipher = tmp;

    tmp = SSL_get_cipher_version( m_ssl );
    if( tmp )
      m_certInfo.protocol = tmp;

    return true;
  }

  inline bool Connection::tls_send( const void *data, size_t len )
  {
    int ret;
    ret = SSL_write( m_ssl, data, len );
    return true;
  }

  inline int Connection::tls_recv( void *data, size_t len )
  {
    return SSL_read( m_ssl, data, len );
  }

  inline bool Connection::tls_dataAvailable()
  {
    return false; // SSL_pending( m_ssl ); // FIXME: crashes
  }

  inline void Connection::tls_cleanup()
  {
    SSL_shutdown( m_ssl );
    SSL_free( m_ssl );
  }

#elif defined( USE_GNUTLS )
  bool Connection::tlsHandshake()
  {
    const int protocolPriority[] = { GNUTLS_TLS1, GNUTLS_SSL3, 0 };
    const int kxPriority[]       = { GNUTLS_KX_RSA, 0 };
    const int cipherPriority[]   = { GNUTLS_CIPHER_AES_256_CBC, GNUTLS_CIPHER_AES_128_CBC,
                                             GNUTLS_CIPHER_3DES_CBC, GNUTLS_CIPHER_ARCFOUR, 0 };
    const int compPriority[]     = { GNUTLS_COMP_ZLIB, GNUTLS_COMP_NULL, 0 };
    const int macPriority[]      = { GNUTLS_MAC_SHA, GNUTLS_MAC_MD5, 0 };

    if( gnutls_global_init() != 0 )
      return false;

    if( gnutls_certificate_allocate_credentials( &m_credentials ) < 0 )
      return false;

    StringList::const_iterator it = m_cacerts.begin();
    for( ; it != m_cacerts.end(); ++it )
      gnutls_certificate_set_x509_trust_file( m_credentials, (*it).c_str(), GNUTLS_X509_FMT_PEM );

    if( !m_clientKey.empty() && !m_clientCerts.empty() )
    {
      gnutls_certificate_set_x509_key_file( m_credentials, m_clientKey.c_str(),
                                            m_clientCerts.c_str(), GNUTLS_X509_FMT_PEM );
    }

    if( gnutls_init( &m_session, GNUTLS_CLIENT ) != 0 )
    {
      gnutls_certificate_free_credentials( m_credentials );
      return false;
    }

    gnutls_protocol_set_priority( m_session, protocolPriority );
    gnutls_cipher_set_priority( m_session, cipherPriority );
    gnutls_compression_set_priority( m_session, compPriority );
    gnutls_kx_set_priority( m_session, kxPriority );
    gnutls_mac_set_priority( m_session, macPriority );
    gnutls_credentials_set( m_session, GNUTLS_CRD_CERTIFICATE, m_credentials );

    gnutls_transport_set_ptr( m_session, (gnutls_transport_ptr_t)m_socket );
    if( gnutls_handshake( m_session ) != 0 )
    {
      gnutls_deinit( m_session );
      gnutls_certificate_free_credentials( m_credentials );
      return false;
    }
    gnutls_certificate_free_ca_names( m_credentials );

    m_secure = true;

    unsigned int status;
    bool error = false;

    if( gnutls_certificate_verify_peers2( m_session, &status ) < 0 )
      error = true;

    m_certInfo.status = 0;
    if( status & GNUTLS_CERT_INVALID )
      m_certInfo.status |= CertInvalid;
    if( status & GNUTLS_CERT_SIGNER_NOT_FOUND )
      m_certInfo.status |= CertSignerUnknown;
    if( status & GNUTLS_CERT_REVOKED )
      m_certInfo.status |= CertRevoked;
    if( status & GNUTLS_CERT_SIGNER_NOT_CA )
      m_certInfo.status |= CertSignerNotCa;
    const gnutls_datum_t* certList = 0;
    unsigned int certListSize;
    if( !error && ( ( certList = gnutls_certificate_get_peers( m_session, &certListSize ) ) == 0 ) )
      error = true;

    gnutls_x509_crt_t *cert = new gnutls_x509_crt_t[certListSize+1];
    for( unsigned int i=0; !error && ( i<certListSize ); ++i )
    {
      if( !error && ( gnutls_x509_crt_init( &cert[i] ) < 0 ) )
        error = true;
      if( !error && ( gnutls_x509_crt_import( cert[i], &certList[i], GNUTLS_X509_FMT_DER ) < 0 ) )
        error = true;
    }

    if( ( gnutls_x509_crt_check_issuer( cert[certListSize-1], cert[certListSize-1] ) > 0 )
         && certListSize > 0 )
      certListSize--;

    bool chain = true;
    for( unsigned int i=1; !error && ( i<certListSize ); ++i )
    {
      chain = error = !verifyAgainst( cert[i-1], cert[i] );
    }
    if( !chain )
      m_certInfo.status |= CertInvalid;
    m_certInfo.chain = chain;

    m_certInfo.chain = verifyAgainstCAs( cert[certListSize], 0 /*CAList*/, 0 /*CAListSize*/ );

    int t = (int)gnutls_x509_crt_get_expiration_time( cert[0] );
    if( t == -1 )
      error = true;
    else if( t < time( 0 ) )
      m_certInfo.status |= CertExpired;
    m_certInfo.date_from = t;

    t = (int)gnutls_x509_crt_get_activation_time( cert[0] );
    if( t == -1 )
      error = true;
    else if( t > time( 0 ) )
      m_certInfo.status |= CertNotActive;
    m_certInfo.date_to = t;

    char name[64];
    size_t nameSize = sizeof( name );
    gnutls_x509_crt_get_issuer_dn( cert[0], name, &nameSize );
    m_certInfo.issuer = name;

    nameSize = sizeof( name );
    gnutls_x509_crt_get_dn( cert[0], name, &nameSize );
    m_certInfo.server = name;

    const char* info;
    info = gnutls_compression_get_name( gnutls_compression_get( m_session ) );
    if( info )
      m_certInfo.compression = info;

    info = gnutls_mac_get_name( gnutls_mac_get( m_session ) );
    if( info )
      m_certInfo.mac = info;

    info = gnutls_cipher_get_name( gnutls_cipher_get( m_session ) );
    if( info )
      m_certInfo.cipher = info;

    info = gnutls_protocol_get_name( gnutls_protocol_get_version( m_session ) );
    if( info )
      m_certInfo.protocol = info;

    if( !gnutls_x509_crt_check_hostname( cert[0], m_server.c_str() ) )
      m_certInfo.status |= CertWrongPeer;

    for( unsigned int i=0; i<certListSize; ++i )
      gnutls_x509_crt_deinit( cert[i] );

    delete[] cert;

    return true;
  }

  bool Connection::verifyAgainst( gnutls_x509_crt_t cert, gnutls_x509_crt_t issuer )
  {
    unsigned int result;
    gnutls_x509_crt_verify( cert, &issuer, 1, 0, &result );
    if( result & GNUTLS_CERT_INVALID )
      return false;

    if( gnutls_x509_crt_get_expiration_time( cert ) < time( 0 ) )
      return false;

    if( gnutls_x509_crt_get_activation_time( cert ) > time( 0 ) )
      return false;

    return true;
  }

  bool Connection::verifyAgainstCAs( gnutls_x509_crt_t cert, gnutls_x509_crt_t *CAList, int CAListSize )
  {
    unsigned int result;
    gnutls_x509_crt_verify( cert, CAList, CAListSize, GNUTLS_VERIFY_ALLOW_X509_V1_CA_CRT, &result );
    if( result & GNUTLS_CERT_INVALID )
      return false;

    if( gnutls_x509_crt_get_expiration_time( cert ) < time( 0 ) )
      return false;

    if( gnutls_x509_crt_get_activation_time( cert ) > time( 0 ) )
      return false;

    return true;
  }

  inline bool Connection::tls_send( const void *data, size_t len )
  {
    int ret;
    do
    {
      ret = gnutls_record_send( m_session, data, len );
    }
    while( ( ret == GNUTLS_E_AGAIN ) || ( ret == GNUTLS_E_INTERRUPTED ) );
    return true;
  }

  inline int Connection::tls_recv( void *data, size_t len )
  {
    return gnutls_record_recv( m_session, data, len );
  }

  inline bool Connection::tls_dataAvailable()
  {
    return false; // gnutls_check_pending( m_session ); // FIXME: crashes
  }

  inline void Connection::tls_cleanup()
  {
    gnutls_bye( m_session, GNUTLS_SHUT_RDWR );
    gnutls_deinit( m_session );
    gnutls_certificate_free_credentials( m_credentials );
    gnutls_global_deinit();
  }

#elif defined( USE_WINTLS )
  bool Connection::tlsHandshake()
  {
    INIT_SECURITY_INTERFACE pInitSecurityInterface;

    m_lib = LoadLibrary( "secur32.dll" );
    if( m_lib == NULL )
      return false;

    pInitSecurityInterface = (INIT_SECURITY_INTERFACE)GetProcAddress( m_lib, "InitSecurityInterfaceA" );
    if( pInitSecurityInterface == NULL )
    {
      FreeLibrary( m_lib );
      m_lib = 0;
      return false;
    }

    m_securityFunc = pInitSecurityInterface();
    if( !m_securityFunc )
    {
      FreeLibrary( m_lib );
      m_lib = 0;
      return false;
    }

    SCHANNEL_CRED schannelCred;
    memset( &schannelCred, 0, sizeof( schannelCred ) );
    memset( &m_credentials, 0, sizeof( m_credentials ) );
    memset( &m_context, 0, sizeof( m_context ) );

    schannelCred.dwVersion = SCHANNEL_CRED_VERSION;
    schannelCred.grbitEnabledProtocols = SP_PROT_TLS1_CLIENT;
    schannelCred.cSupportedAlgs = 0; // FIXME
#ifdef MSVC
    schannelCred.dwMinimumCipherStrength = 0; // FIXME
    schannelCred.dwMaximumCipherStrength = 0; // FIXME
#else
    schannelCred.dwMinimumCypherStrength = 0; // FIXME
    schannelCred.dwMaximumCypherStrength = 0; // FIXME
#endif
    schannelCred.dwSessionLifespan = 0;
    schannelCred.dwFlags = SCH_CRED_NO_SERVERNAME_CHECK | SCH_CRED_NO_DEFAULT_CREDS |
                           SCH_CRED_MANUAL_CRED_VALIDATION; // FIXME check

    TimeStamp timeStamp;
    SECURITY_STATUS ret;
    ret = m_securityFunc->AcquireCredentialsHandleA( NULL, UNISP_NAME_A, SECPKG_CRED_OUTBOUND,
                                     NULL, &schannelCred, NULL,
                                     NULL, &m_credentials, &timeStamp );
    if( ret != SEC_E_OK )
    {
      printf( "AcquireCredentialsHandleA failed\n" );
      return false;
    }

    m_sspiFlags = ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR
                      | ISC_REQ_MUTUAL_AUTH | ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT
                      | ISC_REQ_STREAM;

    SecBufferDesc outBufferDesc;
    SecBuffer outBuffers[1];

    outBuffers[0].BufferType = SECBUFFER_TOKEN;
    outBuffers[0].pvBuffer = NULL;
    outBuffers[0].cbBuffer = 0;

    outBufferDesc.ulVersion = SECBUFFER_VERSION;
    outBufferDesc.cBuffers = 1;
    outBufferDesc.pBuffers = outBuffers;

    long unsigned int sspiFlagsOut;
    ret = m_securityFunc->InitializeSecurityContextA( &m_credentials, NULL, NULL, m_sspiFlags, 0,
        SECURITY_NATIVE_DREP, NULL, 0, &m_context,
        &outBufferDesc, &sspiFlagsOut, &timeStamp );
    if( ret == SEC_I_CONTINUE_NEEDED && outBuffers[0].cbBuffer != 0 && outBuffers[0].pvBuffer != NULL )
    {
      printf( "OK: Continue needed: " );

      int ret = ::send( m_socket, (const char*)outBuffers[0].pvBuffer, outBuffers[0].cbBuffer, 0 );
      if( ret == SOCKET_ERROR || ret == 0 )
      {
        m_securityFunc->FreeContextBuffer( outBuffers[0].pvBuffer );
        m_securityFunc->DeleteSecurityContext( &m_context );
        return false;
      }

      m_securityFunc->FreeContextBuffer( outBuffers[0].pvBuffer );
      outBuffers[0].pvBuffer = NULL;
    }

    if( !handshakeLoop() )
    {
      printf( "handshakeLoop failed\n" );
      return false;
    }

    ret = m_securityFunc->QueryContextAttributes( &m_context, SECPKG_ATTR_STREAM_SIZES, &m_streamSizes );
    if( ret != SEC_E_OK )
    {
      printf( "could not read stream attribs (sizes)\n" );
      return false;
    }

    int maxSize = m_streamSizes.cbHeader + m_streamSizes.cbMaximumMessage + m_streamSizes.cbTrailer;
    m_iBuffer = (char*)malloc( maxSize );
    if( !m_iBuffer )
      return false;

    m_oBuffer = (char*)malloc( maxSize );
    if( !m_oBuffer )
      return false;

    m_bufferOffset = m_iBuffer;
    m_messageOffset = m_oBuffer + m_streamSizes.cbHeader;

    SecPkgContext_Authority streamAuthority;
    ret = m_securityFunc->QueryContextAttributes( &m_context, SECPKG_ATTR_AUTHORITY, &streamAuthority );
    if( ret != SEC_E_OK )
    {
      printf( "could not read stream attribs (sizes)\n" );
      return false;
    }
    else
    {
      m_certInfo.issuer.assign( streamAuthority.sAuthorityName );
    }

    SecPkgContext_ConnectionInfo streamInfo;
    ret = m_securityFunc->QueryContextAttributes( &m_context, SECPKG_ATTR_CONNECTION_INFO, &streamInfo );
    if( ret != SEC_E_OK )
    {
      printf( "could not read stream attribs (sizes)\n" );
      return false;
    }
    else
    {
      if( streamInfo.dwProtocol == SP_PROT_TLS1_CLIENT )
        m_certInfo.protocol = "TLS 1.0";
      else
        m_certInfo.protocol = "unknown";

      std::ostringstream oss;
      switch( streamInfo.aiCipher )
      {
        case CALG_3DES:
          oss << "3DES";
          break;
        case CALG_AES_128:
          oss << "AES";
          break;
        case CALG_AES_256:
          oss << "AES";
          break;
        case CALG_DES:
          oss << "DES";
          break;
        case CALG_RC2:
          oss << "RC2";
          break;
        case CALG_RC4:
          oss << "RC4";
          break;
        default:
          oss << "unknown";
      }

      oss << " " << streamInfo.dwCipherStrength;
      m_certInfo.cipher = oss.str();
      oss.str( "" );

      switch( streamInfo.aiHash  )
      {
        case CALG_MD5:
          oss << "MD5";
          break;
        case CALG_SHA:
          oss << "SHA";
          break;
        default:
          oss << "unknown";
      }

      oss << " " << streamInfo.dwHashStrength;
      m_certInfo.mac = oss.str();

      m_certInfo.compression = "unknown";
    }

    m_secure = true;

    return true;
  }

  bool Connection::handshakeLoop()
  {
    const int bufsize = 65536;
    char *buf = (char*)malloc( bufsize );
    if( !buf )
      return false;

    int bufFilled = 0;
    int dataRecv = 0;
    bool doRead = true;

    SecBufferDesc outBufferDesc, inBufferDesc;
    SecBuffer outBuffers[1], inBuffers[2];

    SECURITY_STATUS ret = SEC_I_CONTINUE_NEEDED;

    while( ret == SEC_I_CONTINUE_NEEDED ||
           ret == SEC_E_INCOMPLETE_MESSAGE ||
           ret == SEC_I_INCOMPLETE_CREDENTIALS )
    {

      if( doRead )
      {
        dataRecv = ::recv( m_socket, buf + bufFilled, bufsize - bufFilled, 0 );

        if( dataRecv == SOCKET_ERROR || dataRecv == 0 )
        {
          break;
        }

        printf( "%d bytes handshake data received\n", dataRecv );

        bufFilled += dataRecv;
      }
      else
      {
        doRead = true;
      }

      outBuffers[0].BufferType = SECBUFFER_TOKEN;
      outBuffers[0].pvBuffer = NULL;
      outBuffers[0].cbBuffer = 0;

      outBufferDesc.ulVersion = SECBUFFER_VERSION;
      outBufferDesc.cBuffers = 1;
      outBufferDesc.pBuffers = outBuffers;

      inBuffers[0].BufferType = SECBUFFER_TOKEN;
      inBuffers[0].pvBuffer = buf;
      inBuffers[0].cbBuffer = bufFilled;

      inBuffers[1].BufferType = SECBUFFER_EMPTY;
      inBuffers[1].pvBuffer = NULL;
      inBuffers[1].cbBuffer = 0;

      inBufferDesc.ulVersion = SECBUFFER_VERSION;
      inBufferDesc.cBuffers = 2;
      inBufferDesc.pBuffers = inBuffers;

      printf( "buffers inited, calling InitializeSecurityContextA\n" );
      long unsigned int sspiFlagsOut;
      TimeStamp timeStamp;
      ret = m_securityFunc->InitializeSecurityContextA( &m_credentials, &m_context, NULL,
                                                        m_sspiFlags, 0,
                                                        SECURITY_NATIVE_DREP, &inBufferDesc, 0, NULL,
                                                        &outBufferDesc, &sspiFlagsOut, &timeStamp );
      if( ret == SEC_E_OK || ret == SEC_I_CONTINUE_NEEDED ||
          ( FAILED( ret ) && sspiFlagsOut & ISC_RET_EXTENDED_ERROR ) )
      {
        if( outBuffers[0].cbBuffer != 0 && outBuffers[0].pvBuffer != NULL )
        {
          printf( "ISCA returned, buffers not empty\n" );
          dataRecv = ::send( m_socket, (const char*)outBuffers[0].pvBuffer, outBuffers[0].cbBuffer, 0  );
          if( dataRecv == SOCKET_ERROR || dataRecv == 0 )
          {
            m_securityFunc->FreeContextBuffer( &outBuffers[0].pvBuffer );
            m_securityFunc->DeleteSecurityContext( &m_context );
            free( buf );
            printf( "coudl not send bufer to server, exiting\n" );
            return false;
          }

          m_securityFunc->FreeContextBuffer( outBuffers[0].pvBuffer );
          outBuffers[0].pvBuffer = NULL;
        }
      }

      if( ret == SEC_E_INCOMPLETE_MESSAGE )
        continue;

      if( ret == SEC_E_OK )
      {
        printf( "handshake successful\n" );
        break;
      }

      if( FAILED( ret ) )
        break;

      if( ret == SEC_I_INCOMPLETE_CREDENTIALS )
      {
        printf( "server requested client credentials\n" );
        ret = SEC_I_CONTINUE_NEEDED;
        continue;
      }

      if( inBuffers[1].BufferType == SECBUFFER_EXTRA )
      {
        printf("some xtra mem in inbuf\n" );
        MoveMemory( buf, buf + ( bufFilled - inBuffers[1].cbBuffer ),
                   inBuffers[1].cbBuffer );

        bufFilled = inBuffers[1].cbBuffer;
      }
      else
      {
        bufFilled = 0;
      }
    }

    if( FAILED( ret ) )
      m_securityFunc->DeleteSecurityContext( &m_context );

    free( buf );

    if( ret == SEC_E_OK )
      return true;

    return false;
  }

  inline bool Connection::tls_send( const void *data, size_t len )
  {
    if( len <= 0 )
      return false;

    SECURITY_STATUS ret;
    SecBuffer *dataBuffer;
    SecBuffer *extraBuffer;
    SecBuffer extraBuffer;

    m_buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
    m_buffers[0].pvBuffer = m_oBuffer;
    m_buffers[0].cbBuffer = m_streamSizes.cbHeader;

    m_buffers[1].BufferType = SECBUFFER_DATA;
    m_buffers[1].pvBuffer = m_messageOffset;

    m_buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
    m_buffers[2].cbBuffer = m_streamSizes.cbTrailer;

    m_buffers[3].BufferType = SECBUFFER_EMPTY;
    m_buffers[3].pvBuffer = NULL;
    m_buffers[3].cbBuffer = 0;

    m_message.ulVersion = SECBUFFER_VERSION;
    m_message.cBuffers = 4;
    m_message.pBuffers = m_buffers;

    while( len > 0 )
    {
      if( m_streamSizes.cbMaximumMessage < len )
      {
        memcpy( m_messageOffset, data, m_streamSizes.cbMaximumMessage );
        len -= m_streamSizes.cbMaximumMessage;
        m_buffers[1].cbBuffer = m_streamSizes.cbMaximumMessage;
        m_buffers[2].pvBuffer = m_messageOffset + m_streamSizes.cbMaximumMessage;
      }
      else
      {
        memcpy( m_messageOffset, data, len );
        m_buffers[1].cbBuffer = len;
        m_buffers[2].pvBuffer = m_messageOffset + len;
        len = 0;
      }

      ret = m_securityFunc->EncryptMessage( &m_context, 0, &m_message, 0 );
      if( ret != SEC_E_OK )
        return false;

      int t = ::send( m_socket, m_oBuffer,
                      m_buffers[0].cbBuffer + m_buffers[1].cbBuffer + m_buffers[2].cbBuffer, 0 );
      if( t == SOCKET_ERROR || cbData == 0 )
      {
        return false;
      }
    }

    return true;
  }

  inline int Connection::tls_recv( void *data, size_t len )
  {
    SECURITY_STATUS ret;
    SecBuffer *dataBuffer = 0;
    SecBuffer *extraBuffer = 0;
    m_iBuffer = 0;

    int maxLength = m_streamSizes.cbHeader  m_streamSizes.cbMaximumMessage + m_streamSizes.cbTrailer;

    int t = ::recv( m_socket, m_iBuffer + m_bufferOffset, maxLength - m_bufferOffset, 0 );
    if( t == SOCKET_ERROR )
      return 0;
    else if( t == 0 )
      return 0;
    else
      m_bufferOffset += t;

    m_buffers[0].BufferType = SECBUFFER_DATA;
    m_buffers[0].pvBuffer = m_iBuffer;
    m_buffers[0].cbBuffer = m_bufferOffset;

    m_buffers[1].BufferType = SECBUFFER_EMPTY;
    m_buffers[2].BufferType = SECBUFFER_EMPTY;
    m_buffers[3].BufferType = SECBUFFER_EMPTY;

    m_message.ulVersion = SECBUFFER_VERSION;
    m_message.cBuffers = 4;
    m_message.pBuffers = m_uffers;

    ret = m_securityFunc->DecryptMessage( &m_context, &m_message, 0, NULL );

    if( ret == SEC_E_INCOMPLETE_MESSAGE )
      return 0;

    if( ret == SEC_I_CONTEXT_EXPIRED )
      return 0;

    if( ret != SEC_E_OK && ret != SEC_I_RENEGOTIATE && ret != SEC_I_CONTEXT_EXPIRED )
      return false;

    for( int i = 1; i < 4; ++i )
    {
      if( dataBuffer == 0 && m_buffers[i].BufferType == SECBUFFER_DATA )
      {
        dataBuffer = &m_buffers[i];
      }
      if( extraBuffer == 0 && m_buffers[i].BufferType == SECBUFFER_EXTRA )
      {
        extraBuffer = &m_buffers[i];
      }
    }

    if( dataBuffer )
    {
      if( dataBuffer.cbBuffer > len )
      {
        printf( "uhoh! buffer too small! FIXME!!!!\n" );
        memcpy( data, dataBuffer.pvBuffer, len );
      }
      else
      {
        memcpy( data, dataBuffer.pvBuffer, dataBuffer.cbBuffer );
        return dataBuffer.cbBuffer;
      }
    }

    return 0;
  }

  inline bool Connection::tls_dataAvailable()
  {
    return false;
  }

  inline void Connection::tls_cleanup()
  {
    m_securityFunc->DeleteSecurityContext( &m_context );
  }
#endif

#ifdef HAVE_ZLIB
  bool Connection::initCompression( StreamFeature method )
  {
    delete m_compression;
    m_compression = 0;
    m_compression = new Compression( method );
    return true;
  }

  void Connection::enableCompression()
  {
    if( !m_compression )
      return;

    m_enableCompression = true;
  }
#endif

  ConnectionState Connection::connect()
  {
    if( m_socket != -1 && m_state >= StateConnecting )
    {
      return m_state;
    }

    m_state = StateConnecting;

    if( m_port == ( unsigned short ) -1 )
      m_socket = DNS::connect( m_server, m_logInstance );
    else
      m_socket = DNS::connect( m_server, m_port, m_logInstance );

    if( m_socket < 0 )
    {
      switch( m_socket )
      {
        case -DNS::DNS_COULD_NOT_CONNECT:
          m_logInstance.log( LogLevelError, LogAreaClassConnection, "connection error: could not connect" );
          break;
        case -DNS::DNS_NO_HOSTS_FOUND:
          m_logInstance.log( LogLevelError, LogAreaClassConnection, "connection error: no hosts found" );
          break;
        case -DNS::DNS_COULD_NOT_RESOLVE:
          m_logInstance.log( LogLevelError, LogAreaClassConnection, "connection error: could not resolve" );
          break;
      }
      cleanup();
    }
    else
      m_state = StateConnected;

    m_cancel = false;
    return m_state;
  }

  void Connection::disconnect( ConnectionError e )
  {
    m_disconnect = e;
    m_cancel = true;

    if( m_fdRequested )
      cleanup();
  }

  int Connection::fileDescriptor()
  {
    m_fdRequested = true;
    return m_socket;
  }

  bool Connection::dataAvailable( int timeout )
  {
#ifdef HAVE_TLS
    if( tls_dataAvailable() )
    {
        return true;
    }
#endif

    fd_set fds;
    struct timeval tv;

    FD_ZERO( &fds );
    FD_SET( m_socket, &fds );

    tv.tv_sec = timeout / 1000;
    tv.tv_usec = timeout % 1000;

    if( select( m_socket + 1, &fds, 0, 0, timeout == -1 ? 0 : &tv ) >= 0 )
    {
      return FD_ISSET( m_socket, &fds ) ? true : false;
    }
    return false;
  }

  ConnectionError Connection::recv( int timeout )
  {
    if( m_cancel )
    {
      ConnectionError e = m_disconnect;
      cleanup();
      return e;
    }

    if( m_socket == -1 )
      return ConnNotConnected;

    if( !m_fdRequested && !dataAvailable( timeout ) )
    {
        return ConnNoError;
    }

    // optimize(?): recv returns the size. set size+1 = \0
    memset( m_buf, '\0', m_bufsize + 1 );
    int size = 0;
#ifdef HAVE_TLS
    if( m_secure )
    {
      size = tls_recv( m_buf, m_bufsize );
    }
    else
#endif
    {
#ifdef SKYOS
      size = ::recv( m_socket, (unsigned char*)m_buf, m_bufsize, 0 );
#else
      size = ::recv( m_socket, m_buf, m_bufsize, 0 );
#endif
    }

    if( size < 0 )
    {
      // error
      return ConnIoError;
    }
    else if( size == 0 )
    {
      // connection closed
      return ConnUserDisconnected;
    }

    std::string buf;
    if( m_compression && m_enableCompression )
    {
      buf.assign( m_buf, size );
      buf = m_compression->decompress( buf );
    }
    else
      buf.assign( m_buf, strlen( m_buf ) );

    Parser::ParserState ret = m_parser->feed( buf );
    if( ret != Parser::PARSER_OK )
    {
      cleanup();
      switch( ret )
      {
        case Parser::PARSER_BADXML:
          m_logInstance.log( LogLevelError, LogAreaClassConnection, "XML parse error" );
          break;
        case Parser::PARSER_NOMEM:
          m_logInstance.log( LogLevelError, LogAreaClassConnection, "memory allocation error" );
          break;
        default:
          m_logInstance.log( LogLevelError, LogAreaClassConnection, "unexpected error" );
          break;
      }
      return ConnIoError;
    }

    return ConnNoError;
  }

  ConnectionError Connection::receive()
  {
    if( m_socket == -1 || !m_parser )
      return ConnNotConnected;

    while( !m_cancel )
    {
      ConnectionError r = recv( 1 );
      if( r != ConnNoError )
        return r;
    }
    cleanup();

    return m_disconnect;
  }

  bool Connection::send( const std::string& data )
  {
    if( data.empty() || ( m_socket == -1 ) )
      return false;

    std::string xml;
    if( m_compression && m_enableCompression )
      xml = m_compression->compress( data );
    else
      xml = data;

#ifdef HAVE_TLS
    if( m_secure )
    {
      size_t len = xml.length();
      if( tls_send( xml.c_str (), len ) == false )
        return false;
    }
    else
#endif
    {
      size_t num = 0;
      size_t len = xml.length();
      while( num < len )
      {
#ifdef SKYOS
        int sent = ::send( m_socket, (unsigned char*)(xml.c_str()+num), len - num, 0 );
#else
        int sent = ::send( m_socket, (xml.c_str()+num), len - num, 0 );
#endif
        if ( sent == -1 )
          return false;

        num += sent;
      }
    }

    return true;
  }

  void Connection::cleanup()
  {
#ifdef HAVE_TLS
    if( m_secure )
    {
        tls_cleanup();
    }
#endif

    if( m_socket != -1 )
    {
#ifdef WIN32
      closesocket( m_socket );
#else
      close( m_socket );
#endif
      m_socket = -1;
    }
    m_state = StateDisconnected;
    m_disconnect = ConnNoError;
    m_enableCompression = false;
    m_secure = false;
    m_cancel = true;
    m_fdRequested = false;
  }

}

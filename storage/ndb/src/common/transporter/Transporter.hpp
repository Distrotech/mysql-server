/* Copyright (C) 2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef Transporter_H
#define Transporter_H

#include <ndb_global.h>

#include <SocketClient.hpp>

#include <TransporterRegistry.hpp>
#include <TransporterCallback.hpp>
#include "TransporterDefinitions.hpp"
#include "Packer.hpp"

#include <NdbMutex.h>
#include <NdbThread.h>

class Transporter {
  friend class TransporterRegistry;
public:
  virtual bool initTransporter() = 0;

  /**
   * Destructor
   */
  virtual ~Transporter();

  /**
   * None blocking
   *    Use isConnected() to check status
   */
  bool connect_client();
  bool connect_client(NDB_SOCKET_TYPE sockfd);
  bool connect_server(NDB_SOCKET_TYPE socket);

  /**
   * Blocking
   */
  virtual void doDisconnect();

  /**
   * Are we currently connected
   */
  bool isConnected() const;
  
  /**
   * Remote Node Id
   */
  NodeId getRemoteNodeId() const;

  /**
   * Local (own) Node Id
   */
  NodeId getLocalNodeId() const;

  /**
   * Get port we're connecting to (signed)
   */
  int get_s_port() { return m_s_port; };
  
  /**
   * Set port to connect to (signed)
   */
  void set_s_port(int port) {
    m_s_port = port;
    if(port<0)
      port= -port;
    if(m_socket_client)
      m_socket_client->set_port(port);
  };

  void update_status_overloaded(Uint32 used)
  {
    m_transporter_registry.set_status_overloaded(remoteNodeId,
                                                 used >= m_overload_limit);
  }
  
  virtual bool doSend() = 0;

  bool has_data_to_send()
  {
    return (m_send_iovec_used > 0 ||
            get_callback_obj()->has_data_to_send(remoteNodeId));
  }

  /* Get the configured maximum send buffer usage. */
  Uint32 get_max_send_buffer() { return m_max_send_buffer; }

protected:
  Transporter(TransporterRegistry &,
	      TransporterType,
	      const char *lHostName,
	      const char *rHostName, 
	      int s_port,
	      bool isMgmConnection,
	      NodeId lNodeId,
	      NodeId rNodeId,
	      NodeId serverNodeId,
	      int byteorder, 
	      bool compression, 
	      bool checksum, 
	      bool signalId,
              Uint32 max_send_buffer);

  /**
   * Blocking, for max timeOut milli seconds
   *   Returns true if connect succeded
   */
  virtual bool connect_server_impl(NDB_SOCKET_TYPE sockfd) = 0;
  virtual bool connect_client_impl(NDB_SOCKET_TYPE sockfd) = 0;
  virtual int pre_connect_options(NDB_SOCKET_TYPE sockfd) { return 0;}
  
  /**
   * Blocking
   */
  virtual void disconnectImpl() = 0;
  
  /**
   * Remote host name/and address
   */
  char remoteHostName[256];
  char localHostName[256];
  struct in_addr remoteHostAddress;
  struct in_addr localHostAddress;

  int m_s_port;

  const NodeId remoteNodeId;
  const NodeId localNodeId;
  
  const bool isServer;

  unsigned createIndex;
  
  int byteOrder;
  bool compressionUsed;
  bool checksumUsed;
  bool signalIdUsed;
  Packer m_packer;  
  Uint32 m_max_send_buffer;
  /* Overload limit, as configured with the OverloadLimit config parameter. */
  Uint32 m_overload_limit;

private:

  /**
   * means that we transform an MGM connection into
   * a transporter connection
   */
  bool isMgmConnection;

  SocketClient *m_socket_client;
  struct in_addr m_connect_address;

  virtual bool send_is_possible(struct timeval *timeout) = 0;
  virtual bool send_limit_reached(int bufsize) = 0;

protected:
  static const Uint32 SEND_IOVEC_SIZE = 64;
  Uint32 m_send_iovec_used;
  struct iovec m_send_iovec[SEND_IOVEC_SIZE];

  Uint32 getErrorCount();
  Uint32 m_errorCount;
  Uint32 m_timeOutMillis;

protected:
  bool m_connected;     // Are we connected
  TransporterType m_type;

  TransporterRegistry &m_transporter_registry;
  TransporterCallback *get_callback_obj() { return m_transporter_registry.callbackObj; };
  void do_disconnect(int err){m_transporter_registry.do_disconnect(remoteNodeId,err);};
  void report_error(enum TransporterError err, const char *info = 0)
    { m_transporter_registry.report_error(remoteNodeId, err, info); };

  bool fetch_send_iovec_data();
  void iovec_data_sent(int nBytesSent);
};

inline
bool
Transporter::isConnected() const {
  return m_connected;
}

inline
NodeId
Transporter::getRemoteNodeId() const {
  return remoteNodeId;
}

inline
NodeId
Transporter::getLocalNodeId() const {
  return localNodeId;
}

inline
Uint32
Transporter::getErrorCount()
{ 
  return m_errorCount;
}

/**
 * Get data to send (in addition to data possibly remaining from previous
 * partial send).
 */
inline
bool
Transporter::fetch_send_iovec_data()
{
  Uint32 used = m_send_iovec_used;
  Uint32 avail = SEND_IOVEC_SIZE - used;
  if (avail > 0)
  {
    int count = get_callback_obj()->get_bytes_to_send_iovec(remoteNodeId,
                                                            m_send_iovec + used,
                                                            avail);
    if (count < 0)
      return false;                             // Error
    m_send_iovec_used = used + count;
  }

  assert(m_send_iovec_used < SEND_IOVEC_SIZE);

  return true;
}

/* Drop all iovec's that have been sent (last one maybe partially). */
inline
void
Transporter::iovec_data_sent(int nBytesSent)
{
  Uint32 used_bytes
    = get_callback_obj()->bytes_sent(remoteNodeId, m_send_iovec, nBytesSent);
  update_status_overloaded(used_bytes);

  Uint32 used = m_send_iovec_used;
  int sofar = 0;
  Uint32 i;
  for (i = 0; i < used; i++)
  {
    int len = m_send_iovec[i].iov_len;
    assert(len >= 0);
    int new_sofar = sofar + len;
    if (new_sofar > nBytesSent)
    {
      int partial = nBytesSent - sofar;
      assert(partial >= 0);
      m_send_iovec[i].iov_base = (char *)m_send_iovec[i].iov_base + partial;
      m_send_iovec[i].iov_len = len - partial;
      if (i > 0)
        memmove(m_send_iovec, m_send_iovec + i, (used - i)*sizeof(iovec));
      break;
    }
    sofar = new_sofar;
  }
  m_send_iovec_used = used - i;
}

#endif // Define of Transporter_H

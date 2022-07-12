/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright (c) 2012 University of Oslo.  All Rights Reserved.
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef WIN32
//#include <io.h>
#include <winsock2.h>
#include <hvsocket.h>
#define errorNumber WSAGetLastError()
#else
#define errorNumber errno
#define closesocket close
#include <sys/socket.h>
#include <linux/vm_socket.h>
#include <sys/stat.h>
#include <errno.h>
#endif

#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>

#include <network/VsockSocket.h>
#include <rfb/LogWriter.h>
#include <rfb/Configuration.h>

#ifdef WIN32
#include <os/winerrno.h>
#include <os/os.h>
#endif

#ifndef AF_HYPERV
#define AF_HYPERV 34
#endif

using namespace network;
using namespace rdr;

static rfb::LogWriter vlog("VsockSocket");

int network::getVsockPort(int sock)
{
#ifdef WIN32
  SOCKADDR_HV sa;
#else
  sockaddr_vm sa;
#endif
  memset(&sa, 0, sizeof(sa));

  socklen_t sa_size;
  sa_size = sizeof(sa);
  if (getsockname(sock, (struct sockaddr *) &sa, &sa_size) < 0)
    return 0;

#ifdef WIN32
  return (int) sa.ServiceId.Data1;
#else
  return sa.svm_port;
#endif
}

// -=- VsockSocket
VsockSocket::VsockSocket(int sock) : Socket(sock)
{
  // Disable corking
  cork(false);
}

VsockSocket::VsockSocket(const char *name, int port)
{
#ifdef WIN32
  SOCKADDR_HV addr;
#else
  sockaddr_vm addr;
#endif
  memset(&addr, 0, sizeof(addr));
  socklen_t salen;

  // - Create a socket
  int sock, err, result;
#ifdef WIN32
  sock = socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
#else
  sock = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
#endif
  if (sock == -1) {
    err = errorNumber;
    throw SocketException("unable to create socket", err);
  }

#ifdef WIN32
  // - Parse GUID from name
  GUID vmId;
  memset(&vmId, 0, sizeof(vmId));
  if (StringToGUID(name, &vmId)) {
    if (vmId == GUID_NULL)
      throw Exception("invalid null VSOCK GUID: %s", name);
  }
  else
    throw Exception("invalid VSOCK GUID: %s", name);

  // - Prepare socket
  addr.Family = AF_HYPERV;
  addr.Reserved = 0;
  addr.VmId = vmId;
  //memcpy(&addr.VmId, &vmId, sizeof(addr.VmId));
  addr.ServiceId = HV_GUID_VSOCK_TEMPLATE;
  addr.ServiceId.Data1 = (unsigned long) port;
#else
  // - Parse CID from name
  char *end = NULL;
	long cid = strtol(name, &end, 10);
	if (name == end || *end == '\0') {
	  throw Exception("invalid CID: %s", name);
	}

  // - Prepare socket
  addr.svm_family = AF_VSOCK;
  addr.svm_cid = cid;
  addr.svm_port = port;
#endif

  // - Attempt to connect
  salen = sizeof(addr);
#ifdef WIN32
  vlog.debug("Connecting to VSOCK GUID %s port %d", name, port);
#else
  vlog.debug("Connecting to VSOCK CID %s port %d", name, port);
#endif
  while ((result = connect(sock, (struct sockaddr *) &addr, salen)) == -1) {
    err = errorNumber;
#ifndef WIN32
    if (err == EINTR)
      continue;
    vlog.debug("Failed to connect to VSOCK CID %s port %d: %d", name, port, err);
#else
    vlog.debug("Failed to connect to VSOCK GUID %s port %d: %d", name, port, err);
#endif
    closesocket(sock);
    sock = -1;
    break;
  }

  if (sock == -1) {
    if (err == 0)
      throw Exception("no useful address for host");
    else
      throw SocketException("unable to connect to socket", err);
  }

  if (result == -1)
    throw SocketException("unable to connect to socket", err);

  // - Take proper ownership of socket
  setFd(sock);
}

char* VsockSocket::getPeerAddress() {
#ifdef WIN32
  SOCKADDR_HV addr;
#else
  struct sockaddr_vm addr;
#endif
  memset(&addr, 0, sizeof(addr));

  socklen_t salen;
  salen = sizeof(addr);
  if (getpeername(getFd(), (struct sockaddr *) &addr, &salen) != 0) {
    vlog.error("unable to get peer name for socket");
    return rfb::strDup("");
  }
#ifdef WIN32
  if (salen > (int) offsetof(SOCKADDR_HV, VmId)) {
    unsigned int vmIdLen = 39; // Null-terminated GUID in {} format
    rfb::CharArray vmId(vmIdLen);
    if (!GUIDToString(&addr.VmId, vmId.buf, vmIdLen)) {
      vlog.error("unable to convert GUID to string");
      return rfb::strDup("");
    }

    return rfb::strDup(vmId.buf);
  }
#else
  if (salen > (int) offsetof(struct sockaddr_vm, svm_cid))
    return rfb::strDup(addr.svm_cid);
#endif

  if (getsockname(getFd(), (struct sockaddr *) &addr, &salen) != 0) {
    vlog.error("unable to get local name for socket");
    return rfb::strDup("");
  }

  return rfb::strDup("(unnamed VSOCK)");
}

char* VsockSocket::getPeerEndpoint() {
  rfb::CharArray address;
  address.buf = getPeerAddress();

#ifdef WIN32
  SOCKADDR_HV sa;
#else
  struct sockaddr_vm sa;
#endif
  memset(&sa, 0, sizeof(sa));

  socklen_t sa_size;
  sa_size = sizeof(sa);
  if (getpeername(getFd(), (struct sockaddr *) &sa, &sa_size) != 0)
    vlog.debug("unable to get peer name for socket to get endpoint address");

  int port;
#ifdef WIN32
  port = (int) sa.ServiceId.Data1;
#else
  port = sa.svm_port;
#endif

  int buflen = strlen(address.buf) + 32;
  rfb::CharArray buffer(buflen);
  snprintf(buffer.buf, buflen, "%s::%d", address.buf, port);
  return rfb::strDup(buffer.buf);
}

VsockListener::VsockListener(const struct sockaddr *listenaddr, socklen_t listenaddrlen)
{
#ifdef WIN32
  SOCKADDR_HV sa;
#else
  struct sockaddr_vm sa;
#endif
  memset(&sa, 0, sizeof(sa));

  // - Create a socket
  int sock;
#ifdef WIN32
  if ((sock = socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW)) < 0)
#else
  if ((sock = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0)
#endif
    throw SocketException("unable to create listening socket", errorNumber);

  // - Populate sa sockaddr struct
  memcpy_s(&sa, listenaddrlen, listenaddr, listenaddrlen);

  // - Bind socket
  if (bind(sock, (struct sockaddr *) &sa, listenaddrlen) == -1) {
    int e = errorNumber;
    closesocket(sock);
    throw SocketException("failed to bind socket", e);
  }

  // - Listen on socket
  listen(sock);
}

Socket* VsockListener::createSocket(int fd) {
  return new VsockSocket(fd);
}

int VsockListener::getMyPort() {
  return getVsockPort(getFd());
}

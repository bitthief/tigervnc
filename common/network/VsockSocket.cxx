/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright (C) 2022-2026 Bit Thief
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
#include <winsock2.h>
#include <ws2tcpip.h>
/* hvsocket.h pulls in initguid.h, so the HV_GUID_* constants are
 * defined in this translation unit. Do not include it anywhere else. */
#include <hvsocket.h>
#define errorNumber WSAGetLastError()
#else
#define errorNumber errno
#define closesocket close
#include <sys/socket.h>
#include <linux/vm_sockets.h>
#include <errno.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <stdexcept>

#include <core/Exception.h>
#include <core/LogWriter.h>
#include <core/i18n.h>
#include <core/string.h>

#include <network/VsockSocket.h>

#ifdef WIN32
#include <core/winerrno.h>
#endif

#ifdef WIN32
#ifndef AF_HYPERV
#define AF_HYPERV 34
#endif
typedef SOCKADDR_HV vsock_sockaddr_t;
#else
typedef struct sockaddr_vm vsock_sockaddr_t;
#endif

using namespace network;

static core::LogWriter vlog("VsockSocket");

#ifdef WIN32

static int hexValue(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

// Parses "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx", optionally wrapped in
// braces, into a GUID.
static bool parseGuid(const char* str, GUID* guid)
{
  static const size_t GUID_STR_LEN = 36;
  unsigned char bytes[16];
  size_t len, i, pos;

  if (str == nullptr)
    return false;

  if (str[0] == '{')
    str++;

  len = strlen(str);
  if ((len == GUID_STR_LEN + 1) && (str[GUID_STR_LEN] == '}'))
    len = GUID_STR_LEN;
  if (len != GUID_STR_LEN)
    return false;

  pos = 0;
  for (i = 0; i < GUID_STR_LEN; i++) {
    int hi, lo;

    if ((i == 8) || (i == 13) || (i == 18) || (i == 23)) {
      if (str[i] != '-')
        return false;
      continue;
    }

    hi = hexValue(str[i]);
    lo = hexValue(str[i+1]);
    if ((hi < 0) || (lo < 0))
      return false;

    bytes[pos++] = (unsigned char)((hi << 4) | lo);
    i++;
  }

  guid->Data1 = ((unsigned long)bytes[0] << 24) |
                ((unsigned long)bytes[1] << 16) |
                ((unsigned long)bytes[2] << 8) |
                ((unsigned long)bytes[3]);
  guid->Data2 = (unsigned short)((bytes[4] << 8) | bytes[5]);
  guid->Data3 = (unsigned short)((bytes[6] << 8) | bytes[7]);
  memcpy(guid->Data4, bytes + 8, 8);

  return true;
}

static std::string formatGuid(const GUID* guid)
{
  return core::format("%08lx-%04hx-%04hx-%02x%02x-%02x%02x%02x%02x%02x%02x",
                      (unsigned long)guid->Data1, guid->Data2, guid->Data3,
                      guid->Data4[0], guid->Data4[1], guid->Data4[2],
                      guid->Data4[3], guid->Data4[4], guid->Data4[5],
                      guid->Data4[6], guid->Data4[7]);
}

static void parseVmId(const char* name, GUID* vmId)
{
  if (strcasecmp(name, "parent") == 0)
    *vmId = HV_GUID_PARENT;
  else if (strcasecmp(name, "loopback") == 0)
    *vmId = HV_GUID_LOOPBACK;
  else if (strcasecmp(name, "children") == 0)
    *vmId = HV_GUID_CHILDREN;
  else if ((strcasecmp(name, "any") == 0) ||
           (strcasecmp(name, "wildcard") == 0))
    *vmId = HV_GUID_WILDCARD;
  else if (!parseGuid(name, vmId))
    throw std::invalid_argument(
      core::format(_("Invalid Hyper-V VM ID: %s"), name));
}

#else

static unsigned int parseCid(const char* name)
{
  char* end;
  unsigned long cid;

  if (strcasecmp(name, "host") == 0)
    return VMADDR_CID_HOST;
#ifdef VMADDR_CID_LOCAL
  if (strcasecmp(name, "local") == 0)
    return VMADDR_CID_LOCAL;
#endif
  if ((strcasecmp(name, "any") == 0) || (strcmp(name, "-1") == 0))
    return VMADDR_CID_ANY;

  cid = strtoul(name, &end, 10);
  if ((name == end) || (*end != '\0') || (cid > 0xffffffffUL))
    throw std::invalid_argument(
      core::format(_("Invalid VSOCK CID: %s"), name));

  return (unsigned int)cid;
}

#endif

static void makeAddress(vsock_sockaddr_t* addr, const char* name, int port)
{
  memset(addr, 0, sizeof(*addr));

#ifdef WIN32
  addr->Family = AF_HYPERV;
  addr->Reserved = 0;
  parseVmId(name, &addr->VmId);
  addr->ServiceId = HV_GUID_VSOCK_TEMPLATE;
  addr->ServiceId.Data1 = (unsigned long)port;
#else
  addr->svm_family = AF_VSOCK;
  addr->svm_cid = parseCid(name);
  addr->svm_port = (unsigned int)port;
#endif
}

static int addressPort(const vsock_sockaddr_t* addr)
{
#ifdef WIN32
  return (int)addr->ServiceId.Data1;
#else
  return (int)addr->svm_port;
#endif
}

static std::string addressName(const vsock_sockaddr_t* addr)
{
#ifdef WIN32
  return formatGuid(&addr->VmId);
#else
  if (addr->svm_cid == VMADDR_CID_ANY)
    return "any";
  return core::format("%u", addr->svm_cid);
#endif
}

static int createVsockSocket()
{
  int sock;

#ifdef WIN32
  sock = socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
#else
  sock = socket(AF_VSOCK, SOCK_STREAM, 0);
#endif
  if (sock == -1)
    throw core::socket_error(_("Failed to create socket"), errorNumber);

  return sock;
}

// -=- VsockSocket

VsockSocket::VsockSocket(int sock) : Socket(sock)
{
}

VsockSocket::VsockSocket(const char* name, int port)
{
  int sock, err, result;
  vsock_sockaddr_t addr;

  makeAddress(&addr, name, port);

  vlog.debug("Connecting to VSOCK peer %s port %d", name, port);

  sock = createVsockSocket();

  err = 0;
  while ((result = connect(sock, (struct sockaddr*)&addr,
                           sizeof(addr))) == -1) {
    err = errorNumber;
#ifndef WIN32
    if (err == EINTR)
      continue;
#endif
    vlog.debug("Failed to connect to VSOCK peer %s port %d: %d",
               name, port, err);
    closesocket(sock);
    break;
  }

  if (result == -1)
    throw core::socket_error(_("Failed to connect to VSOCK peer"), err);

  // Take proper ownership of the socket
  setFd(sock);
}

const char* VsockSocket::getPeerAddress()
{
  static std::string buffer;
  vsock_sockaddr_t addr;
  socklen_t salen = sizeof(addr);

  if (getpeername(getFd(), (struct sockaddr*)&addr, &salen) != 0) {
    vlog.error(_("Failed to get peer name for socket"));
    return _("(N/A)");
  }

  buffer = addressName(&addr);

  return buffer.c_str();
}

const char* VsockSocket::getPeerEndpoint()
{
  static std::string buffer;
  vsock_sockaddr_t addr;
  socklen_t salen = sizeof(addr);

  if (getpeername(getFd(), (struct sockaddr*)&addr, &salen) != 0) {
    vlog.error(_("Failed to get peer name for socket"));
    return _("(N/A)");
  }

  buffer = core::format("%s::%d", addressName(&addr).c_str(),
                        addressPort(&addr));

  return buffer.c_str();
}

// -=- VsockListener

VsockListener::VsockListener(const char* name, int port)
{
  vsock_sockaddr_t addr;

  makeAddress(&addr, name, port);

  fd = createVsockSocket();

  if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    int err = errorNumber;
    closesocket(fd);
    throw core::socket_error(_("Failed to bind VSOCK socket"), err);
  }

  listen(fd);
}

VsockListener::VsockListener(int sock) : SocketListener(sock)
{
}

Socket* VsockListener::createSocket(int fd_)
{
  return new VsockSocket(fd_);
}

int VsockListener::getMyPort()
{
  vsock_sockaddr_t addr;
  socklen_t salen = sizeof(addr);

  if (getsockname(getFd(), (struct sockaddr*)&addr, &salen) < 0)
    return 0;

  return addressPort(&addr);
}

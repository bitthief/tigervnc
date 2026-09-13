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

// -=- VsockSocket.h - VM sockets (AF_VSOCK on Linux, AF_HYPERV on
//     Windows) stream sockets. These provide a direct, network-less
//     transport between a Hyper-V host and its guests.
//
//     A peer is identified by a "name" and a port:
//
//       Linux:   the name is a decimal context ID (CID), or one of the
//                keywords "host" (VMADDR_CID_HOST), "local"
//                (VMADDR_CID_LOCAL) or "any" (VMADDR_CID_ANY).
//       Windows: the name is the VM's GUID (with or without braces),
//                or one of the keywords "parent" (HV_GUID_PARENT),
//                "loopback" (HV_GUID_LOOPBACK), "children"
//                (HV_GUID_CHILDREN) or "any" (HV_GUID_WILDCARD).
//
//     On Windows the port is mapped onto the Hyper-V VSOCK service
//     template GUID (<port>-facb-11e6-bd58-64006a7986d3), which is
//     what Linux guests see as a plain AF_VSOCK port.

#ifndef __NETWORK_VSOCK_SOCKET_H__
#define __NETWORK_VSOCK_SOCKET_H__

#include <network/Socket.h>

namespace network {

  class VsockSocket : public Socket {
  public:
    VsockSocket(int sock);
    VsockSocket(const char* name, int port);

    const char* getPeerAddress() override;
    const char* getPeerEndpoint() override;
  };

  class VsockListener : public SocketListener {
  public:
    VsockListener(const char* name, int port);
    VsockListener(int sock);

    int getMyPort() override;

  protected:
    Socket* createSocket(int fd) override;
  };

}

#endif // __NETWORK_VSOCK_SOCKET_H__

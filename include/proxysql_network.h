#ifndef PROXYSQL_NETWORK_H
#define PROXYSQL_NETWORK_H

#include <cstdint>
#include "proxysql_socket.h"

extern "C" {
ProxySQL_Socket listen_on_port(const char *ip, uint16_t port, int backlog, bool reuseport = false);
ProxySQL_Socket listen_on_unix(const char *path, int backlog);
}

#endif // PROXYSQL_NETWORK_H

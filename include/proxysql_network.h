#ifndef PROXYSQL_NETWORK_H
#define PROXYSQL_NETWORK_H

#include <cstdint>
#include <string>
#include <string_view>
#include "proxysql_socket.h"

struct ProxySQL_Listener_Address {
	std::string host;
	uint16_t port = 0;
};

bool parse_listener_address(std::string_view listener_spec, ProxySQL_Listener_Address &address);

extern "C" {
ProxySQL_Socket listen_on_port(const char *ip, uint16_t port, int backlog, bool reuseport = false);
ProxySQL_Socket listen_on_unix(const char *path, int backlog);
}

#endif // PROXYSQL_NETWORK_H

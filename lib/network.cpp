#include "proxysql_network.h"
#include "proxysql_debug.h"

#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <memory>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

bool parse_listener_address(std::string_view listener_spec, ProxySQL_Listener_Address &address) {
	if (listener_spec.empty()) {
		return false;
	}
	size_t host_start = 0;
	size_t host_end = listener_spec.find(':');
	size_t port_start = host_end;
	if (listener_spec.front() == '[') {
		host_start = 1;
		host_end = listener_spec.find(']');
		if (host_end == std::string_view::npos || host_end + 1 >= listener_spec.size() ||
			listener_spec[host_end + 1] != ':') {
			return false;
		}
		port_start = host_end + 1;
	} else if (host_end == std::string_view::npos) {
		address = {std::string(listener_spec), 0};
		return true;
	}
	if (host_end == host_start) {
		return false;
	}
	uint16_t port = 0;
	const char *end = listener_spec.data() + listener_spec.size();
	const auto result = std::from_chars(listener_spec.data() + port_start + 1, end, port);
	if (result.ec != std::errc{} || result.ptr != end || port == 0) {
		return false;
	}
	address = {std::string(listener_spec.substr(host_start, host_end - host_start)), port};
	return true;
}

ProxySQL_Socket listen_on_port(const char *ip, uint16_t port, int backlog, bool reuseport) {
	if (!proxysql_socket_init()) {
		proxy_error("Unable to initialize network sockets: %d\n", proxysql_socket_error());
		return PROXYSQL_INVALID_SOCKET;
	}

	addrinfo hints {};
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	char port_string[NI_MAXSERV];
	const auto port_number = static_cast<unsigned int>(port);
	snprintf(port_string, sizeof(port_string), "%u", port_number);
	addrinfo *resolved = nullptr;
	const int resolve_error = getaddrinfo(ip, port_string, &hints, &resolved);
	if (resolve_error != 0) {
		proxy_error("getaddrinfo(): %s\n", gai_strerror(resolve_error));
		return PROXYSQL_INVALID_SOCKET;
	}
	std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(resolved, freeaddrinfo);

#ifdef _WIN32
	const int bind_option = SO_EXCLUSIVEADDRUSE;
#else
	const int bind_option = SO_REUSEADDR;
#endif
	const int enabled = 1;
	const char *option_value = reinterpret_cast<const char *>(&enabled);
	const char *display_ip = ip ? ip : "*";
	for (const addrinfo *address = addresses.get(); address; address = address->ai_next) {
		const auto listener = proxysql_socket_open(address->ai_family, address->ai_socktype, address->ai_protocol);
		if (listener == PROXYSQL_INVALID_SOCKET) {
			proxy_error("socket() error for %s:%u: %d\n", display_ip, port_number, proxysql_socket_error());
			continue;
		}
#ifdef IPV6_V6ONLY
		if (address->ai_family == AF_INET6 &&
			setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, option_value, sizeof(enabled)) == -1) {
			proxy_error("setsockopt() IPV6_V6ONLY: %d\n", proxysql_socket_error());
		}
#endif
		if (setsockopt(listener, SOL_SOCKET, bind_option, option_value, sizeof(enabled)) == -1) {
			proxy_error("setsockopt() for %s:%u: %d\n", display_ip, port_number, proxysql_socket_error());
			proxysql_socket_close(listener);
			continue;
		}
#if defined(SO_REUSEPORT) && !defined(_WIN32)
		if (reuseport && setsockopt(listener, SOL_SOCKET, SO_REUSEPORT, option_value, sizeof(enabled)) == -1) {
			proxy_error("setsockopt() SO_REUSEPORT for %s:%u: %d\n", display_ip, port_number, proxysql_socket_error());
		}
#else
		(void)reuseport;
#endif
		if (bind(listener, address->ai_addr, static_cast<socklen_t>(address->ai_addrlen)) == -1) {
			const int error = proxysql_socket_error();
			proxy_error("bind() error for %s:%u: %d\n", display_ip, port_number, error);
#ifdef __linux__
			if (error == EADDRNOTAVAIL) {
				proxy_info("Trying to bind to a non-local address requires net.ipv4.ip_nonlocal_bind=1\n");
			}
#endif
			proxysql_socket_close(listener);
			continue;
		}
		if (listen(listener, backlog) == -1) {
			proxy_error("listen() error for %s:%u: %d\n", display_ip, port_number, proxysql_socket_error());
			proxysql_socket_close(listener);
			continue;
		}
		return listener;
	}
	return PROXYSQL_INVALID_SOCKET;
}

ProxySQL_Socket listen_on_unix(const char *path, int backlog) {
#ifdef _WIN32
	(void)backlog;
	proxy_error("Unix socket listeners are unavailable on Windows: %s\n", path);
	return PROXYSQL_INVALID_SOCKET;
#else
	if (unlink(path) == -1 && errno != ENOENT) {
		proxy_error("Error unlink Unix Socket %s\n", path);
		return PROXYSQL_INVALID_SOCKET;
	}

	const auto listener = proxysql_socket_open(AF_UNIX, SOCK_STREAM, 0);
	if (listener == PROXYSQL_INVALID_SOCKET) {
		proxy_error("Error on creating socket: %s\n", strerror(errno));
		return PROXYSQL_INVALID_SOCKET;
	}

	sockaddr_un address {};
	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, path, sizeof(address.sun_path) - 1);

	if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
		proxy_error("Error on bind() for Unix Socket %s: %s\n", path, strerror(errno));
		proxysql_socket_close(listener);
		return PROXYSQL_INVALID_SOCKET;
	}
	if (listen(listener, backlog) != 0) {
		proxy_error("Error on listen() for Unix Socket %s: %s\n", path, strerror(errno));
		proxysql_socket_close(listener);
		return PROXYSQL_INVALID_SOCKET;
	}
	chmod(path, S_IRWXU | S_IRWXG | S_IRWXO);
	return listener;
#endif
}

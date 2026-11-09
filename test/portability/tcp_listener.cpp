#include "proxysql_network.h"
#include "ProxySQL_Thread_Wakeup.h"
#include "proxysql_debug.h"

#include <cstdio>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#endif

extern "C" void proxy_error_func(int, const char *, ...) {}

static bool wait_readable(pollfd &descriptor) {
	return (descriptor.revents & POLLIN) ||
		(proxysql_poll(&descriptor, 1, 2000) == 1 && (descriptor.revents & POLLIN));
}

int main() {
	ProxySQL_Listener_Address endpoint;
	const char listener_spec[] = "[::1]:6033";
	if (!parse_listener_address(listener_spec, endpoint) || endpoint.host != "::1" || endpoint.port != 6033 ||
		!parse_listener_address("127.0.0.1:6032", endpoint) || endpoint.host != "127.0.0.1" || endpoint.port != 6032 ||
		!parse_listener_address("/tmp/proxysql.sock", endpoint) || endpoint.host != "/tmp/proxysql.sock" || endpoint.port != 0 ||
		parse_listener_address("[::1", endpoint) || parse_listener_address("127.0.0.1:65536", endpoint) ||
		parse_listener_address("[::1]6033", endpoint) || parse_listener_address("127.0.0.1:6033extra", endpoint)) {
		std::fputs("Incorrect listener address parsing\n", stderr);
		return 1;
	}
	const auto listener = listen_on_port("127.0.0.1", 0, 4);
	sockaddr_in address {};
	socklen_t length = sizeof(address);
	if (listener == PROXYSQL_INVALID_SOCKET ||
		getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
		std::fputs("Could not listen on an ephemeral TCP port\n", stderr);
		return 1;
	}
	const auto duplicate = listen_on_port("127.0.0.1", ntohs(address.sin_port), 4);
	if (duplicate != PROXYSQL_INVALID_SOCKET) {
		std::fputs("A second listener could bind the same TCP port\n", stderr);
		return 1;
	}

	ProxySQL_Thread_Wakeup wakeup;
	const auto client = proxysql_socket_open(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (client == PROXYSQL_INVALID_SOCKET || !wakeup.open() ||
		proxysql_socket_nonblocking(listener) != 0 ||
		connect(client, reinterpret_cast<sockaddr *>(&address), length) != 0 ||
		wakeup.signal(42) != 1) {
		std::fputs("Could not queue a TCP connection and a wakeup signal\n", stderr);
		return 1;
	}

	pollfd descriptors[] {{listener, POLLIN, 0}, {wakeup.reader(), POLLIN, 0}};
	unsigned char signal;
	if (proxysql_poll(descriptors, 2, 2000) <= 0 ||
		!wait_readable(descriptors[0]) || !wait_readable(descriptors[1]) ||
		wakeup.receive(signal) != 1 || signal != 42) {
		std::fputs("The TCP listener and wakeup did not poll together\n", stderr);
		return 1;
	}

	const auto connection = accept(listener, nullptr, nullptr);
	pollfd accepted {connection, POLLIN, 0};
	char payload = '\0';
	if (connection == PROXYSQL_INVALID_SOCKET || send(client, "P", 1, 0) != 1 ||
		!wait_readable(accepted) || recv(connection, &payload, 1, 0) != 1 || payload != 'P') {
		std::fputs("Could not exchange bytes over the accepted socket\n", stderr);
		return 1;
	}
	proxysql_socket_close(connection);
	proxysql_socket_close(client);
	proxysql_socket_close(listener);
	std::puts("ProxySQL TCP listener and wakeup passed");
	return 0;
}

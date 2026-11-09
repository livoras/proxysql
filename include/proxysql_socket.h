#ifndef PROXYSQL_SOCKET_H
#define PROXYSQL_SOCKET_H

#ifdef _WIN32
#include <cerrno>
#include <winsock2.h>
using ProxySQL_Socket = SOCKET;
constexpr ProxySQL_Socket PROXYSQL_INVALID_SOCKET = INVALID_SOCKET;
#else
#include <poll.h>
using ProxySQL_Socket = int;
constexpr ProxySQL_Socket PROXYSQL_INVALID_SOCKET = -1;
#endif

inline int proxysql_poll(struct pollfd *fds, unsigned int count, int timeout) {
#ifdef _WIN32
	const int result = WSAPoll(fds, count, timeout);
	if (result == SOCKET_ERROR) {
		errno = WSAGetLastError() == WSAEINTR ? EINTR : EIO;
	}
	return result;
#else
	return poll(fds, count, timeout);
#endif
}

#endif // PROXYSQL_SOCKET_H

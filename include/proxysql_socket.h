#ifndef PROXYSQL_SOCKET_H
#define PROXYSQL_SOCKET_H

#ifdef _WIN32
#include <cerrno>
#include <winsock2.h>
using ProxySQL_Socket = SOCKET;
constexpr ProxySQL_Socket PROXYSQL_INVALID_SOCKET = INVALID_SOCKET;
constexpr int PROXYSQL_SHUT_RDWR = SD_BOTH;
constexpr short PROXYSQL_POLL_PRIORITY = POLLRDBAND;
#else
#include <poll.h>
#include <sys/socket.h>
using ProxySQL_Socket = int;
constexpr ProxySQL_Socket PROXYSQL_INVALID_SOCKET = -1;
constexpr int PROXYSQL_SHUT_RDWR = SHUT_RDWR;
constexpr short PROXYSQL_POLL_PRIORITY = POLLPRI;
#endif

bool proxysql_socket_init();
ProxySQL_Socket proxysql_socket_open(int family, int type, int protocol);
int proxysql_socket_close(ProxySQL_Socket socket);
int proxysql_socket_nonblocking(ProxySQL_Socket socket);
int proxysql_socket_error();

int proxysql_socket_recv(ProxySQL_Socket socket, void *buffer, int length);
int proxysql_socket_send(ProxySQL_Socket socket, const void *buffer, int length);

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

#include "proxysql_socket.h"

#ifndef _WIN32
#include <cerrno>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#ifdef _WIN32
namespace {

struct Winsock_Runtime {
	int result;

	Winsock_Runtime() {
		WSADATA data;
		result = WSAStartup(MAKEWORD(2, 2), &data);
	}

	~Winsock_Runtime() {
		if (result == 0) {
			WSACleanup();
		}
	}
};

int socket_io_result(int result) {
	if (result == SOCKET_ERROR) {
		switch (WSAGetLastError()) {
			case WSAEWOULDBLOCK: errno = EAGAIN; break;
			case WSAEINTR: errno = EINTR; break;
			default: errno = EIO; break;
		}
	}
	return result;
}

} // namespace
#endif

bool proxysql_socket_init() {
#ifdef _WIN32
	static const Winsock_Runtime winsock;
	if (winsock.result != 0) {
		WSASetLastError(winsock.result);
		return false;
	}
#endif
	return true;
}

ProxySQL_Socket proxysql_socket_open(int family, int type, int protocol) {
#ifdef _WIN32
	if (!proxysql_socket_init()) {
		return PROXYSQL_INVALID_SOCKET;
	}
	return WSASocketW(family, type, protocol, nullptr, 0,
		WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
#else
	return socket(family, type, protocol);
#endif
}

int proxysql_socket_close(ProxySQL_Socket socket) {
#ifdef _WIN32
	return closesocket(socket);
#else
	return close(socket);
#endif
}

int proxysql_socket_nonblocking(ProxySQL_Socket socket) {
#ifdef _WIN32
	u_long enabled = 1;
	return ioctlsocket(socket, FIONBIO, &enabled);
#else
	int enabled = 1;
	return ioctl(socket, FIONBIO, &enabled);
#endif
}

int proxysql_socket_error() {
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

int proxysql_socket_recv(ProxySQL_Socket socket, void *buffer, int length) {
#ifdef _WIN32
	return socket_io_result(recv(socket, static_cast<char *>(buffer), length, 0));
#else
	return recv(socket, buffer, length, 0);
#endif
}

int proxysql_socket_send(ProxySQL_Socket socket, const void *buffer, int length) {
#ifdef _WIN32
	return socket_io_result(send(socket, static_cast<const char *>(buffer), length, 0));
#elif defined(MSG_NOSIGNAL)
	return send(socket, buffer, length, MSG_NOSIGNAL);
#else
	return send(socket, buffer, length, 0);
#endif
}

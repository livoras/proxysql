#include "ProxySQL_Thread_Wakeup.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

void close_endpoint(ProxySQL_Socket endpoint) {
	if (endpoint == PROXYSQL_INVALID_SOCKET) {
		return;
	}
#ifdef _WIN32
	closesocket(endpoint);
#else
	close(endpoint);
#endif
}

#ifdef _WIN32
struct Winsock_Runtime {
	bool ready;

	Winsock_Runtime() {
		WSADATA data;
		ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
	}

	~Winsock_Runtime() {
		if (ready) {
			WSACleanup();
		}
	}
};

struct Scoped_Socket {
	ProxySQL_Socket handle;
	~Scoped_Socket() { close_endpoint(handle); }
};

ProxySQL_Socket loopback_socket() {
	return WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
}

bool open_wakeup_pair(ProxySQL_Socket &reader, ProxySQL_Socket &writer) {
	static const Winsock_Runtime winsock;
	if (!winsock.ready) {
		return false;
	}

	Scoped_Socket listener {loopback_socket()};
	sockaddr_in address {};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	int length = sizeof(address);
	if (listener.handle == INVALID_SOCKET ||
		bind(listener.handle, reinterpret_cast<sockaddr *>(&address), length) == SOCKET_ERROR ||
		getsockname(listener.handle, reinterpret_cast<sockaddr *>(&address), &length) == SOCKET_ERROR ||
		listen(listener.handle, 1) == SOCKET_ERROR) {
		return false;
	}

	Scoped_Socket sender {loopback_socket()};
	sockaddr_in sender_address {};
	int sender_length = sizeof(sender_address);
	if (sender.handle == INVALID_SOCKET ||
		connect(sender.handle, reinterpret_cast<sockaddr *>(&address), length) == SOCKET_ERROR ||
		getsockname(sender.handle, reinterpret_cast<sockaddr *>(&sender_address), &sender_length) == SOCKET_ERROR) {
		return false;
	}

	// Only accept the connection created by this worker, even if another local process races it.
	sockaddr_in peer {};
	int peer_length = sizeof(peer);
	Scoped_Socket receiver {accept(listener.handle, reinterpret_cast<sockaddr *>(&peer), &peer_length)};
	const int no_delay = 1;
	u_long nonblocking = 1;
	if (receiver.handle == INVALID_SOCKET || peer.sin_port != sender_address.sin_port ||
		peer.sin_addr.s_addr != sender_address.sin_addr.s_addr ||
		!SetHandleInformation(reinterpret_cast<HANDLE>(receiver.handle), HANDLE_FLAG_INHERIT, 0) ||
		setsockopt(sender.handle, IPPROTO_TCP, TCP_NODELAY,
			reinterpret_cast<const char *>(&no_delay), sizeof(no_delay)) == SOCKET_ERROR ||
		ioctlsocket(sender.handle, FIONBIO, &nonblocking) == SOCKET_ERROR ||
		ioctlsocket(receiver.handle, FIONBIO, &nonblocking) == SOCKET_ERROR) {
		return false;
	}

	reader = receiver.handle;
	writer = sender.handle;
	receiver.handle = INVALID_SOCKET;
	sender.handle = INVALID_SOCKET;
	return true;
}
#else
bool configure_pipe_endpoint(int fd) {
	const int flags = fcntl(fd, F_GETFL);
	return flags != -1 &&
		fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1 &&
		fcntl(fd, F_SETFD, FD_CLOEXEC) != -1;
}

bool open_wakeup_pair(ProxySQL_Socket &reader, ProxySQL_Socket &writer) {
	int pipe_fds[2];
	if (pipe(pipe_fds) != 0) {
		return false;
	}
	if (!configure_pipe_endpoint(pipe_fds[0]) || !configure_pipe_endpoint(pipe_fds[1])) {
		close_endpoint(pipe_fds[1]);
		close_endpoint(pipe_fds[0]);
		return false;
	}
	reader = pipe_fds[0];
	writer = pipe_fds[1];
	return true;
}
#endif

} // namespace

ProxySQL_Thread_Wakeup::~ProxySQL_Thread_Wakeup() {
	close_endpoint(writer_);
	close_endpoint(reader_);
}

bool ProxySQL_Thread_Wakeup::open() {
	return reader_ != PROXYSQL_INVALID_SOCKET || open_wakeup_pair(reader_, writer_);
}

int ProxySQL_Thread_Wakeup::signal(unsigned char value) const {
#ifdef _WIN32
	return send(writer_, reinterpret_cast<const char *>(&value), 1, 0);
#else
	return write(writer_, &value, 1);
#endif
}

int ProxySQL_Thread_Wakeup::receive(unsigned char &value) const {
#ifdef _WIN32
	return recv(reader_, reinterpret_cast<char *>(&value), 1, 0);
#else
	return read(reader_, &value, 1);
#endif
}

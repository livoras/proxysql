#ifndef __CLASS_PROXYSQL_POLL
#define __CLASS_PROXYSQL_POLL

#include "proxysql_socket.h"

constexpr ProxySQL_Socket NO_LISTENER_UPDATE = PROXYSQL_INVALID_SOCKET;
constexpr ProxySQL_Socket REMOVE_ALL_LISTENERS = static_cast<ProxySQL_Socket>(-2);

class iface_info {
	public:
	char *iface;
	char *address;
	int port;
	ProxySQL_Socket fd;
	iface_info(char *_i, char *_a, int p, ProxySQL_Socket f) {
		iface=strdup(_i);
		address=strdup(_a);
		port=p;
		fd=f;
	}
	~iface_info() {
		free(iface);
		free(address);
		shutdown(fd, PROXYSQL_SHUT_RDWR);
		proxysql_socket_close(fd);
	}
};

class ProxySQL_Poll {
	private:
	void shrink();
	void expand(unsigned int more);

	public:
	unsigned int len;
	unsigned int size;
	struct pollfd *fds;
	MySQL_Data_Stream **myds;
	unsigned long long *last_recv;
	unsigned long long *last_sent;
	volatile ProxySQL_Socket pending_listener_add;
	volatile ProxySQL_Socket pending_listener_del;
	unsigned int poll_timeout;
	unsigned long loops;
	StatCounters *loop_counters;

	ProxySQL_Poll();
	~ProxySQL_Poll();
	void add(uint32_t _events, ProxySQL_Socket _fd, MySQL_Data_Stream *_myds, unsigned long long sent_time);
	void remove_index_fast(unsigned int i);
	int find_index(ProxySQL_Socket fd);
};
#endif // __CLASS_PROXYSQL_POLL

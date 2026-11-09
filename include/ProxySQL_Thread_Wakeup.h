#ifndef PROXYSQL_THREAD_WAKEUP_H
#define PROXYSQL_THREAD_WAKEUP_H

#include "proxysql_socket.h"

class ProxySQL_Thread_Wakeup {
public:
	ProxySQL_Thread_Wakeup() = default;
	~ProxySQL_Thread_Wakeup();
	ProxySQL_Thread_Wakeup(const ProxySQL_Thread_Wakeup &) = delete;
	ProxySQL_Thread_Wakeup &operator=(const ProxySQL_Thread_Wakeup &) = delete;

	bool open();
	ProxySQL_Socket reader() const { return reader_; }
	int signal(unsigned char value) const;
	int receive(unsigned char &value) const;

private:
	ProxySQL_Socket reader_ = PROXYSQL_INVALID_SOCKET;
	ProxySQL_Socket writer_ = PROXYSQL_INVALID_SOCKET;
};

#endif // PROXYSQL_THREAD_WAKEUP_H

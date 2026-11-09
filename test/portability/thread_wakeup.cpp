#include "ProxySQL_Thread_Wakeup.h"

#include <chrono>
#include <cstdio>
#include <thread>

int main() {
	for (int run = 0; run < 32; ++run) {
		ProxySQL_Thread_Wakeup wakeup;
		if (!wakeup.open()) {
			std::fputs("Could not open the wakeup pair\n", stderr);
			return 1;
		}
		struct pollfd descriptor {wakeup.reader(), POLLIN, 0};
		unsigned char value = 0;
		if (wakeup.receive(value) != -1 || proxysql_poll(&descriptor, 1, 0) != 0) {
			std::fputs("Empty wakeup pair was not nonblocking\n", stderr);
			return 1;
		}

		int write_result = 0;
		std::thread sender([&]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			write_result = wakeup.signal(0);
			if (write_result == 1) {
				write_result = wakeup.signal(200);
			}
		});
		const int ready = proxysql_poll(&descriptor, 1, 2000);
		sender.join();
		if (write_result != 1 || ready != 1 || !(descriptor.revents & POLLIN) ||
			wakeup.receive(value) != 1 || value != 0 ||
			proxysql_poll(&descriptor, 1, 2000) != 1 || !(descriptor.revents & POLLIN) ||
			wakeup.receive(value) != 1 || value != 200 ||
			proxysql_poll(&descriptor, 1, 0) != 0) {
			std::fputs("Wakeup readiness or payload did not match\n", stderr);
			return 1;
		}
	}
	std::puts("ProxySQL worker wakeup passed (32 iterations)");
	return 0;
}

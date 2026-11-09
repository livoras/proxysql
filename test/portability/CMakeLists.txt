cmake_minimum_required(VERSION 3.16)
project(proxysql_portability_tests LANGUAGES CXX)

enable_testing()
find_package(Threads REQUIRED)

add_library(proxysql_portable_network STATIC
	../../lib/network.cpp
	../../lib/ProxySQL_Thread_Wakeup.cpp
	../../lib/proxysql_socket.cpp
)
target_compile_features(proxysql_portable_network PUBLIC cxx_std_11)
target_include_directories(proxysql_portable_network PUBLIC ../../include)
if(WIN32)
	target_link_libraries(proxysql_portable_network PUBLIC ws2_32)
endif()

add_executable(thread_wakeup thread_wakeup.cpp)
target_link_libraries(thread_wakeup PRIVATE proxysql_portable_network Threads::Threads)
add_test(NAME thread_wakeup COMMAND thread_wakeup)

add_executable(tcp_listener tcp_listener.cpp)
target_link_libraries(tcp_listener PRIVATE proxysql_portable_network)
add_test(NAME tcp_listener COMMAND tcp_listener)

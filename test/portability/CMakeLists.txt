cmake_minimum_required(VERSION 3.16)
project(proxysql_portability_tests LANGUAGES CXX)

enable_testing()
find_package(Threads REQUIRED)

add_executable(thread_wakeup
	thread_wakeup.cpp
	../../lib/ProxySQL_Thread_Wakeup.cpp
)
target_compile_features(thread_wakeup PRIVATE cxx_std_11)
target_include_directories(thread_wakeup PRIVATE ../../include)
target_link_libraries(thread_wakeup PRIVATE Threads::Threads)
if(WIN32)
	target_link_libraries(thread_wakeup PRIVATE ws2_32)
endif()

add_test(NAME thread_wakeup COMMAND thread_wakeup)

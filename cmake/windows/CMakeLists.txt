cmake_minimum_required(VERSION 3.20)
project(ProxySQLWindows LANGUAGES CXX)

if(NOT WIN32 OR NOT MINGW OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Use a MinGW-compatible Windows x64 compiler.")
endif()

set(PROXYSQL_WINDOWS_DEPS "" CACHE PATH "Prepared dependency directory")
if(NOT EXISTS "${PROXYSQL_WINDOWS_DEPS}/clang64/include/openssl/ssl.h")
    message(FATAL_ERROR "Set PROXYSQL_WINDOWS_DEPS to the prepared dependency directory.")
endif()

get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(stage "${PROXYSQL_WINDOWS_DEPS}")
set(vendor "${stage}/sources")
set(prefix "${stage}/clang64")

find_package(Git REQUIRED)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
    WORKING_DIRECTORY "${root}"
    OUTPUT_VARIABLE git_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

set(library_sources
    Chart_bundle_js.cpp
    GTID_Server_Data.cpp
    MyHGC.cpp
    MySQL_Authentication.cpp
    MySQL_HostGroups_Manager.cpp
    MySQL_Logger.cpp
    MySQL_Monitor.cpp
    MySQL_PreparedStatement.cpp
    MySQL_Protocol.cpp
    MySQL_ResultSet.cpp
    MySQL_Session.cpp
    MySQL_Thread.cpp
    MySQL_Variables.cpp
    MySQL_encode.cpp
    MySrvC.cpp
    MySrvConnList.cpp
    MySrvList.cpp
    ProxySQL_Admin.cpp
    ProxySQL_Cluster.cpp
    ProxySQL_Config.cpp
    ProxySQL_GloVars.cpp
    ProxySQL_HTTP_Server.cpp
    ProxySQL_Poll.cpp
    ProxySQL_RESTAPI_Server.cpp
    ProxySQL_Restapi.cpp
    ProxySQL_Statistics.cpp
    ProxySQL_Thread_Wakeup.cpp
    QP_query_digest_stats.cpp
    QP_rule_text.cpp
    Query_Cache.cpp
    Query_Processor.cpp
    SpookyV2.cpp
    c_tokenizer.cpp
    configfile.cpp
    debug.cpp
    font-awesome.min.css.cpp
    gen_utils.cpp
    main-bundle.min.css.cpp
    mysql_backend.cpp
    mysql_connection.cpp
    mysql_data_stream.cpp
    network.cpp
    proxy_protocol_info.cpp
    proxysql_coredump.cpp
    proxysql_find_charset.cpp
    proxysql_socket.cpp
    proxysql_sslkeylog.cpp
    proxysql_utils.cpp
    set_parser.cpp
    sha256crypt.cpp
    sqlite3db.cpp
    thread.cpp
)
list(TRANSFORM library_sources PREPEND "${root}/lib/")

add_executable(proxysql
    "${root}/src/main.cpp"
    "${root}/src/proxysql_global.cpp"
    "${root}/src/proxy_tls.cpp"
    "${root}/src/SQLite3_Server.cpp"
    ${library_sources}
)
target_compile_features(proxysql PRIVATE cxx_std_17)
target_compile_options(proxysql PRIVATE -Werror=format)
target_compile_definitions(proxysql PRIVATE
    NOJEM NOMINMAX WIN32_LEAN_AND_MEAN GITVERSION="${git_version}-windows"
)
target_include_directories(proxysql PRIVATE
    "${root}/include"
    "${vendor}/mariadb-connector-c-3.3.8-src/include"
    "${stage}/build-mariadb/include"
    "${vendor}/sqlite-amalgamation-3430200"
    "${vendor}/re2-2022-12-01"
    "${vendor}/cityhash-1.1.1-5-gf5dc541/src"
    "${vendor}/libev-4.33"
    "${vendor}/libhttpserver-0.18.2/src"
    "${vendor}/libinjection-3.10.0/src"
    "${prefix}/include"
)
target_link_libraries(proxysql PRIVATE
    "${stage}/build-mariadb/libmariadb/libmariadbclient.a"
    "${prefix}/lib/libprometheus-cpp-pull.a"
    "${prefix}/lib/libprometheus-cpp-core.a"
    "${prefix}/lib/libhttpserver.a"
    "${prefix}/lib/libre2.a"
    "${prefix}/lib/libcityhash.a"
    "${prefix}/lib/libinjection.a"
    "${prefix}/lib/libev.a"
    "${prefix}/lib/libconfig++.dll.a"
    "${prefix}/lib/libconfig.dll.a"
    "${prefix}/lib/libcurl.dll.a"
    "${prefix}/lib/libmicrohttpd.dll.a"
    "${prefix}/lib/libpcrecpp.dll.a"
    "${prefix}/lib/libpcre.dll.a"
    "${prefix}/lib/libproxysql-sqlite3.a"
    "${prefix}/lib/libssl.dll.a"
    "${prefix}/lib/libcrypto.dll.a"
    "${prefix}/lib/libz.dll.a"
    "${prefix}/lib/libzstd.dll.a"
    pthread ws2_32 rpcrt4 crypt32 bcrypt secur32 shlwapi iphlpapi winmm
)

install(TARGETS proxysql RUNTIME DESTINATION .)

# Native Windows x64 build

The Windows target builds ProxySQL's MySQL frontend, backend connection pool,
query processor, monitor, and SQL admin interface as a foreground
`proxysql.exe`. It targets Windows 11 x64 using the UCRT and Winsock.

## Build

Use the LLVM-MinGW UCRT release dated 2026-11-04 (Clang 23.1.5). The build runs
on macOS or Linux and needs Bash, CMake, Ninja, Git, Python 3, curl, patch, and
tar with Zstandard support. The package manifest locks the CLANG64 dependencies
by filename and SHA-256. The build applies the repository patches to MariaDB
Connector/C, SQLite, Prometheus, libhttpserver, libev, and libinjection.

```sh
cmake/windows/build.sh /path/to/llvm-mingw-20261104-ucrt /path/to/new-build-directory
```

The build places `proxysql.exe` and its runtime DLLs in the build directory's
`windows-x64` subdirectory. Keep that directory together. The CLANG64 C++
runtime supplies symbols used by the CLANG64 libconfig and PCRE libraries.
`PROXYSQL_BUILD_JOBS` controls compilation concurrency.
`PROXYSQL_CLANG64_MIRROR` can point to a mirror or a `file://` directory with
the exact archives listed in the manifest.

## Run and test

```powershell
.\windows-x64\proxysql.exe -f -c C:/path/to/proxysql.cnf
.\test\portability\windows-mysql-smoke.ps1 -ProxySqlExe .\windows-x64\proxysql.exe -MySqlBin 'C:\Program Files\MySQL\MySQL Server 8.0\bin'
```

For the smoke test, use a MySQL 8.0 server distribution with `mysqld.exe`,
`mysql.exe`, and `mysqladmin.exe`. The script uses loopback and three free
ports. It initializes a scratch backend, checks the initial protocol packet
and the SQL admin interface, and reads a backend table through ProxySQL.
It checks the client error while the scratch backend is stopped and confirms
that the same ProxySQL process resumes forwarding after the backend restarts.
The logs and scratch data are left under the printed directory.

## Platform scope

Use TCP listener addresses and a writable data directory. The Windows target
runs in foreground mode so a host process supervisor can own its lifecycle.
The target omits Unix sockets, epoll idle threads, daemon and angel processes,
ClickHouse, Group Replication bootstrap, the ProxySQL binlog reader connection,
and external Web, LDAP, and SQLite plugins. REST routes that execute external
programs and external scheduler jobs require a separate subprocess port.
The separate ProxySQL binlog reader feature is distinct from MySQL's own
replication.

#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 LLVM_MINGW_ROOT NEW_BUILD_DIRECTORY" >&2
    exit 2
fi

here=$(cd -- "$(dirname -- "$0")" && pwd)
repo=$(cd -- "$here/../.." && pwd)
toolchain=$(cd -- "$1" && pwd)
mkdir -- "$2"
stage=$(cd -- "$2" && pwd)
vendor="$stage/sources"
prefix="$stage/clang64"
archives="$stage/archives"
mkdir -p "$vendor" "$prefix" "$archives"

cc="$toolchain/bin/x86_64-w64-mingw32-clang"
cxx="$toolchain/bin/x86_64-w64-mingw32-clang++"
ar="$toolchain/bin/llvm-ar"
jobs="${PROXYSQL_BUILD_JOBS:-4}"
mirror="${PROXYSQL_CLANG64_MIRROR:-https://repo.msys2.org/mingw/clang64}"

if ! "$cxx" --version | grep -F 'clang version 23.1.5' >/dev/null; then
    echo 'Use the LLVM-MinGW 23.1.5 UCRT toolchain for these CLANG64 libraries.' >&2
    exit 2
fi

while read -r expected filename; do
    [[ "$expected" == \#* || -z "$expected" ]] && continue
    curl --fail --silent --show-error --location --retry 3 "$mirror/$filename" -o "$archives/$filename"
    python3 - "$archives/$filename" "$expected" <<'PY'
import hashlib
import sys
from pathlib import Path

archive = Path(sys.argv[1])
if hashlib.sha256(archive.read_bytes()).hexdigest() != sys.argv[2]:
    raise SystemExit(f"Checksum mismatch: {archive}")
PY
    tar -xf "$archives/$filename" -C "$stage" clang64
done < "$here/clang64-packages.sha256"

for archive in \
    cityhash/cityhash-1.1.1-5-gf5dc541.tar.gz \
    libev/libev-4.33.tar.gz \
    libhttpserver/libhttpserver-0.18.2.tar.gz \
    libinjection/libinjection-3.10.0.tar.gz \
    mariadb-client-library/mariadb-connector-c-3.3.8-src.tar.gz \
    prometheus-cpp/prometheus-cpp-v1.1.0.tar.gz \
    re2/re2-2022-12-01.tar.gz \
    sqlite3/sqlite-amalgamation-3430200.tar.gz; do
    tar -xzf "$repo/deps/$archive" -C "$vendor"
done

cmake_flags=(
    -G Ninja
    -DCMAKE_SYSTEM_NAME=Windows
    "-DCMAKE_C_COMPILER=$cc"
    "-DCMAKE_CXX_COMPILER=$cxx"
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_PREFIX_PATH=$prefix"
)

mariadb="$vendor/mariadb-connector-c-3.3.8-src"
for name in \
    plugin_auth_CMakeLists.txt mariadb_stmt.c mariadb_lib.c mariadb_lib.c.collation \
    mariadb_lib.c.ipv6_fix mariadb_async.c ma_password.c mysql.h ma_priv.h ma_alloc.c \
    ma_charset.c unittest_basic-t.c unittest_charset.c mariadb_dyncol.c-multiplication-overflow \
    ma_array.c-multiplication-overflow zutil.c-multiplication-overflow \
    cr_new_stmt_metadata_removal ps_buffer_stmt_read_all_rows mariadb_stmt_store_result_err \
    empty_split_compress_packet sslkeylogfile client_deprecate_eof mariadb_com.h \
    x509cache mariadb_rpl cmakelists.txt mariadb_lib.c.metadata_column_check; do
    patch -d "$mariadb" -p0 < "$repo/deps/mariadb-client-library/$name.patch"
done
cmake -S "$mariadb" -B "$stage/build-mariadb" "${cmake_flags[@]}" \
    "-DOPENSSL_ROOT_DIR=$prefix" -DWITH_SSL=OPENSSL -DWITH_EXTERNAL_ZLIB=ON \
    -DWITH_CURL=OFF -DWITH_UNIT_TESTS=OFF -DWITH_MSI=OFF -DWITH_SIGNCODE=OFF -DWITH_RTC=OFF
cmake --build "$stage/build-mariadb" --target mariadbclient --parallel "$jobs"

re2="$vendor/re2-2022-12-01"
cmake -S "$re2" -B "$stage/build-re2" "${cmake_flags[@]}" \
    "-DCMAKE_INSTALL_PREFIX=$prefix" -DRE2_BUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF
cmake --build "$stage/build-re2" --parallel "$jobs"
cmake --install "$stage/build-re2"

prometheus="$vendor/prometheus-cpp-1.1.0"
tar -xzf "$repo/deps/prometheus-cpp/civetweb-v1.15.tar.gz" --strip-components=1 -C "$prometheus/3rdparty/civetweb"
for name in serial_exposer registry_counters_reset fix_old_distros; do
    patch -d "$prometheus" -p1 < "$repo/deps/prometheus-cpp/$name.patch"
done
cmake -S "$prometheus" -B "$stage/build-prometheus" "${cmake_flags[@]}" \
    "-DOPENSSL_ROOT_DIR=$prefix" "-DCMAKE_INSTALL_PREFIX=$prefix" \
    -DUSE_THIRDPARTY_LIBRARIES=ON -DBUILD_SHARED_LIBS=OFF -DENABLE_TESTING=OFF \
    -DENABLE_PUSH=OFF -DENABLE_COMPRESSION=OFF
cmake --build "$stage/build-prometheus" --parallel "$jobs"
cmake --install "$stage/build-prometheus"

ev="$vendor/libev-4.33"
patch -d "$ev" ev.c < "$repo/deps/libev/ev.c-multiplication-overflow.patch"
"$cc" -std=gnu17 -O2 -DEV_STANDALONE -c "$ev/ev.c" -o "$stage/ev.obj"
"$ar" rcs "$prefix/lib/libev.a" "$stage/ev.obj"

injection="$vendor/libinjection-3.10.0"
patch -d "$injection" -p1 < "$repo/deps/libinjection/libinjection_sqli.c.patch"
for file in libinjection_sqli libinjection_html5 libinjection_xss; do
    "$cc" -std=gnu17 -O2 -c "$injection/src/$file.c" -o "$stage/$file.obj"
done
"$ar" rcs "$prefix/lib/libinjection.a" "$stage"/libinjection_*.obj

city="$vendor/cityhash-1.1.1-5-gf5dc541"
python3 - "$city/src/city.cc" <<'PY'
import sys
from pathlib import Path

source = Path(sys.argv[1])
text = source.read_text()
assert text.count("#ifdef _MSC_VER") == 1
source.write_text(text.replace("#ifdef _MSC_VER", "#ifdef _WIN32"))
PY
printf '#define HAVE_BUILTIN_EXPECT 1\n' > "$city/config.h"
"$cxx" -std=c++17 -O2 -I"$city" -I"$city/src" -c "$city/src/city.cc" -o "$stage/city.obj"
"$ar" rcs "$prefix/lib/libcityhash.a" "$stage/city.obj"

http="$vendor/libhttpserver-0.18.2"
for name in noexcept re2_regex final_val_post_process empty_uri_log_crash; do
    patch -d "$http" -p1 < "$repo/deps/libhttpserver/$name.patch"
done
for name in expose_raw_url replace_static_global_strings; do
    patch -d "$http" -p0 < "$repo/deps/libhttpserver/$name.patch"
done
mkdir "$stage/http-objects"
for file in "$http/src/"*.cpp "$http/src/details/"*.cpp; do
    filename=${file##*/}
    "$cxx" -std=c++17 -O2 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DHTTPSERVER_COMPILATION \
        -I"$prefix/include" -I"$re2" -I"$http/src" -I"$http/src/httpserver" \
        -c "$file" -o "$stage/http-objects/${filename%.cpp}.obj"
done
"$ar" rcs "$prefix/lib/libhttpserver.a" "$stage/http-objects/"*.obj

sqlite="$vendor/sqlite-amalgamation-3430200"
patch -d "$sqlite" -p1 < "$repo/deps/sqlite3/from_unixtime.patch"
patch -d "$sqlite" sqlite3.c < "$repo/deps/sqlite3/sqlite3.c-multiplication-overflow.patch"
for name in sqlite3_pass_exts throw; do
    patch -d "$sqlite" -p0 < "$repo/deps/sqlite3/$name.patch"
done
"$cc" -std=gnu17 -O2 -DSQLITE_ENABLE_MEMORY_MANAGEMENT -DSQLITE_ENABLE_JSON1 \
    -c "$sqlite/sqlite3.c" -o "$stage/sqlite3.obj"
"$ar" rcs "$prefix/lib/libproxysql-sqlite3.a" "$stage/sqlite3.obj"

cmake -S "$here" -B "$stage/build-proxysql" "${cmake_flags[@]}" "-DPROXYSQL_WINDOWS_DEPS=$stage"
cmake --build "$stage/build-proxysql" --parallel "$jobs"
python3 "$here/bundle.py" --objdump "$toolchain/bin/llvm-objdump" \
    --dll-dir "$prefix/bin" --dll-dir "$toolchain/x86_64-w64-mingw32/bin" \
    "$stage/build-proxysql/proxysql.exe" "$stage/windows-x64"

printf 'Windows runtime bundle: %s\n' "$stage/windows-x64"

#ifndef PROXYSQL_UUID_H
#define PROXYSQL_UUID_H

#ifdef _WIN32
#include <rpc.h>
#include <cstring>

using uuid_t = UUID;

inline int uuid_parse(const char *text, uuid_t &uuid) {
	if (strlen(text) != 36) {
		return -1;
	}
	return UuidFromStringA(reinterpret_cast<RPC_CSTR>(const_cast<char *>(text)), &uuid) == RPC_S_OK ? 0 : -1;
}

inline void uuid_generate(uuid_t &uuid) {
	UuidCreate(&uuid);
}

inline void uuid_unparse(const uuid_t &uuid, char *text) {
	RPC_CSTR encoded = nullptr;
	if (UuidToStringA(&uuid, &encoded) == RPC_S_OK) {
		memcpy(text, encoded, 37);
		RpcStringFreeA(&encoded);
	} else {
		*text = '\0';
	}
}
#else
#include <uuid/uuid.h>
#endif

#endif

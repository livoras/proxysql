#ifndef PROXYSQL_WINDOWS_STRINGS_H
#define PROXYSQL_WINDOWS_STRINGS_H

#include <cstdlib>
#include <cstring>

inline const char *strcasestr(const char *text, const char *pattern) {
	const size_t length = strlen(pattern);
	do {
		if (_strnicmp(text, pattern, length) == 0) {
			return text;
		}
	} while (*text++);
	return nullptr;
}

inline char *strcasestr(char *text, const char *pattern) {
	return const_cast<char *>(strcasestr(static_cast<const char *>(text), pattern));
}

inline char *strndup(const char *text, size_t limit) {
	const size_t length = strnlen(text, limit);
	char *copy = static_cast<char *>(malloc(length + 1));
	if (copy != nullptr) {
		memcpy(copy, text, length);
		copy[length] = '\0';
	}
	return copy;
}

inline void *memmem(const void *text, size_t length, const void *pattern, size_t pattern_length) {
	if (pattern_length == 0) {
		return const_cast<void *>(text);
	}
	if (pattern_length <= length) {
		const char *bytes = static_cast<const char *>(text);
		for (size_t offset = 0; offset <= length - pattern_length; ++offset) {
			if (memcmp(bytes + offset, pattern, pattern_length) == 0) {
				return const_cast<char *>(bytes + offset);
			}
		}
	}
	return nullptr;
}

#endif

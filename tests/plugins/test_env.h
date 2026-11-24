#ifndef NVJ_TEST_ENV_H
#define NVJ_TEST_ENV_H

#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>

static const char* nvj_env_text(const char* name, char* buffer, unsigned int size)
{
    DWORD length = GetEnvironmentVariableA(name, buffer, size);
    if (length == 0u || length >= size) {
        return NULL;
    }
    return buffer;
}
#else
static const char* nvj_env_text(const char* name, char* buffer, unsigned int size)
{
    (void)buffer;
    (void)size;
    return getenv(name);
}
#endif

#endif

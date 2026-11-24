#include "abi/v1_0/nyrexvj_plugin.h"
#include "abi_entry.h"

static const AbiEntry kTable[] = {ABI_COMMON_TABLE, ABI_SIZE(NvjHost)};

size_t abi_v1_0_count(void)
{
    return sizeof(kTable) / sizeof(kTable[0]);
}

const AbiEntry* abi_v1_0_entry(size_t index)
{
    return index < abi_v1_0_count() ? &kTable[index] : NULL;
}

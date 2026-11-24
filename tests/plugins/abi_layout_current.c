#include "nyrexvj_plugin.h"
#include "abi_entry.h"

static const AbiEntry kTable[] = {ABI_COMMON_TABLE, ABI_SIZE(NvjHost), ABI_FIELD(NvjHost, has_capability), ABI_FIELD(NvjHost, get_limits), ABI_FIELD(NvjHost, set_error), ABI_SIZE(NvjLimits)};

size_t abi_current_count(void)
{
    return sizeof(kTable) / sizeof(kTable[0]);
}

const AbiEntry* abi_current_entry(size_t index)
{
    return index < abi_current_count() ? &kTable[index] : NULL;
}

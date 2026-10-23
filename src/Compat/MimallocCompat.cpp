#include <mimalloc.h>

// TiltedCore's prebuilt MimallocAllocator references mi_malloc_size, which
// mimalloc 3.5.x no longer exports. Map it to the equivalent public API.
extern "C" size_t mi_malloc_size(const void* aBlock)
{
    return mi_usable_size(aBlock);
}

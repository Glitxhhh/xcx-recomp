// Private translation-unit adapter: native runtime allocation failure, never shipped.
#include <stdlib.h>
static void *test_runtime_calloc(size_t n,size_t size) {
    return getenv("MOD_RUNTIME_FAIL")?NULL:calloc(n,size);
}
#define calloc test_runtime_calloc
#include "../../runtime/mod_runtime.c"

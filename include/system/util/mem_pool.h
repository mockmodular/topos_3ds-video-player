#pragma once
#include <stddef.h>

void   pool_init(void);
size_t pool_get_free(void);
size_t pool_get_chunks(void);

#pragma once

// This header is fake.

typedef int mtx_t;

#define mtx_init(_m,...) ((void)(_m))
#define mtx_destroy(_m,...) ((void)(_m))
#define mtx_lock(_m,...) ((void)(_m))
#define mtx_unlock(_m,...) ((void)(_m))

#define _MTX_INITIALIZER_NP 0

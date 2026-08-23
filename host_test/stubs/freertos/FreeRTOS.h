#pragma once
// Host-test stub: single-threaded, so the real spinlock semantics collapse
// to no-ops. game.c only needs the type and the two macros to exist.
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))

#include <stdio.h>

#define DEBUG       1

#define DBG_ERROR   0
#define DBG_INFO    1
#define DBG_DEBUG   2
#define DBG_INSANE  3

#define DEBUG_LEVEL DBG_INFO

// __FILE__redefinition by Erich Styger (https://mcuoneclipse.com/author/mcuoneclipse)
// Needs -Wno-builtin-macro-redefined
#define __FILE__ (__builtin_strrchr("/"__BASE_FILE__, '/') + 1)

#if DEBUG == 0
#define debug_printf(level, fmt, ...) do {} while (0)
#else
// debug_printf credit to Jonathan Leffler (https://github.com/jleffler)
#define debug_printf(level, fmt, ...) \
            do { if (level <= DEBUG_LEVEL) printf("core%d: %s:%d:%s(): " fmt, get_core_num(), __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0)
#endif

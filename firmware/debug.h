#include <stdio.h>

#define DEBUG               1

// debug_printf credit to Jonathan Leffler (https://github.com/jleffler)
#define debug_printf(fmt, ...) \
            do { if (DEBUG) printf("%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0)

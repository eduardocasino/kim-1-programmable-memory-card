#include <stdint.h>

#define DEBUG               1

// debug_printf credit to Jonathan Leffler (https://github.com/jleffler)
#define debug_printf(fmt, ...) \
            do { if (DEBUG) printf("%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0)

#define MEM_MAP_SIZE        0x10000

#define MAX_SSID_LEN        32
#define MAX_PASSWD_LEN      64

#define MEM_ATTR_ENABLED    0
#define MEM_ATTR_DISABLED   ( 1 <<  0 )
#define MEM_ATTR_RDONLY     0
#define MEM_ATTR_WRITEABLE  ( 1 << 12 ) 

#define MEM_ATTR_CE_MASK    ( 1 <<  0 )
#define MEM_ATTR_RW_MASK    ( 1 << 12 ) 

#define MEM_ATTR_MASK       ( MEM_ATTR_CE_MASK | MEM_ATTR_RW_MASK )
#define MEM_DATA_MASK       ~MEM_ATTR_MASK



typedef struct {
    uint16_t        memory[MEM_MAP_SIZE];
    struct {
        uint32_t    country;
        char        ssid[MAX_SSID_LEN];
        char        passwd[MAX_PASSWD_LEN];
    } network;
} config_t;

extern config_t config;

// Due to the non-contiguous GPIOs of the data bus, we need to perform 16bit DMA
// transfers and twice the memory for storing the 64KBytes of the KIM-1 address map.  
// The base address of the memmap has to have the lower 17 bits to 0 so we can
// calculate the target address by ORing the base with the value of the address
// bus shifted 1 bit to the left (because of the 16bit transfer).
// mem_map is defined in memmap_custom.ld and it is placed at the beginning of the
// physical RAM, so it is well aligned. Had to do that because setting the alignment
// with a directive and letting the linker to do the placement left not enough contiguous
// ram space for other variables.
//
extern uint16_t mem_map[MEM_MAP_SIZE];

void config_copy_default_memory_map( uint16_t * mem_map );


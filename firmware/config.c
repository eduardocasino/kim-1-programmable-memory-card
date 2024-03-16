#include <string.h>

#include "config.h"

#ifdef DEBUG
#include "pins.h"

static void init_mem( uint16_t * mem_map )
{
    // Set KIM-1 vectors
    //
    mem_map[0xfffa] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 0x1c );
    mem_map[0xfffb] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 0x1c );
    mem_map[0xfffc] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 0x22 );
    mem_map[0xfffd] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 0x1c );
    mem_map[0xfffe] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 0x1f );
    mem_map[0xffff] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 0x1c );

    mem_map[0xa000] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 'E' );
    mem_map[0xa001] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 'D' );
    mem_map[0xa002] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 'U' );
    mem_map[0xa003] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 'A' );
    mem_map[0xa004] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 'R' );
    mem_map[0xa005] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 'D' );
    mem_map[0xa006] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 'O' );

    // Enable a 256byte RAM for testing
    //
    for ( int i = 0; i < 256; ++i )
    {
        mem_map[0xaf00+i] = MEM_ATTR_ENABLED | MEM_ATTR_WRITEABLE;
    }

}
#endif

void config_copy_default_memory_map( uint16_t * mem_map )
{

    (void) memcpy( mem_map, &config.memory, MEM_MAP_SIZE*2 );

#ifdef DEBUG
    init_mem( mem_map );
#endif

}




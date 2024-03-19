#include <string.h>

#include "config.h"

void config_copy_default_memory_map( uint16_t * mem_map )
{

    (void) memcpy( mem_map, &config.memory, MEM_MAP_SIZE*2 );

}




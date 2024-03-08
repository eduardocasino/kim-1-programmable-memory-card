#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "pins.h"
#include "protos.h"

// NOTE: I designed the board before the firmware, and the assignments for the data bus are
//       reversed. This does not matter for the board operation, but the program moving data
//       to/from the Pico must address that.

#define MEM_SIZE            0x10000

#define MEM_ATTR_ENABLED    0
#define MEM_ATTR_DISABLED   ( 1 <<  0 )
#define MEM_ATTR_RDONLY     0
#define MEM_ATTR_WRITEABLE  ( 1 << 12 ) 

#define MEM_ATTR_CE_MASK    ( 1 <<  0 )
#define MEM_ATTR_RW_MASK    ( 1 << 12 ) 

uint16_t mem_map[MEM_SIZE] = { 0xFF };

void main()
{
    init_mem();

    init_gpio();
    
    while ( true )
    {
        read_write_loop();
    }
}

static inline void read_write_loop( void )
{
    uint32_t   all_pins = gpio_get_all();

    uint16_t   address  = all_pins & 0xFFFF;
    uint16_t * data     = &mem_map[address];

    if ( *data & MEM_ATTR_CE_MASK )         // Disabled
    {
        gpio_put( CE, HIGH );               // Set buffer in high impedance state
        return;                             // And do nothing
    }
    else
    {
        gpio_put( CE, LOW );                // Enable data bus buffer
    }

    if ( all_pins & PIN_RW_MASK )           // Write enabled
    {
        gpio_set_dir_in_masked( PIN_DATA_MASK );

        if ( *data & MEM_ATTR_RW_MASK )     // Check if memory is writable
        {
            // CE is output, so should read 0 (which is enabled)
            //
            *data = ( gpio_get_all() >> CE ) & MEM_ATTR_WRITEABLE;
        }
    }
    else                                    // Read
    {
        gpio_set_dir_out_masked( PIN_DATA_MASK );

        gpio_put_masked( PIN_DATA_MASK, *data << CE );
    }

}

// TODO: Copy a default map from flash?
//
void init_mem( void )
{
    // Set KIM-1 vectors
    //
    // mem_map[0xfffa] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 0x1c );
    // mem_map[0xfffb] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 0x1c );
    // mem_map[0xfffc] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 0x22 );
    // mem_map[0xfffd] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 0x1c );
    // mem_map[0xfffe] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 0x1f );
    // mem_map[0xffff] = MEM_ATTR_ENABLED | MEM_ATTR_RDONLY | BYTEPREP( 0x1c );

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

void init_gpio( void )
{
    gpio_init_mask( PIN_ADDR_MASK | PIN_DATA_MASK | PIN_CE_MASK | PIN_RW_MASK );
    
    gpio_set_dir_in_masked( PIN_ADDR_MASK | PIN_DATA_MASK | PIN_RW_MASK );
    gpio_set_dir( CE, GPIO_OUT );
}

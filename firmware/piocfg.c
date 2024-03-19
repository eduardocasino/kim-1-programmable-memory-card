#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "mememul.pio.h"

#include "pins.h"
#include "dmacfg.h"

static void piocfg_gpio_pins( PIO pio )
{
    // Configure #CE and #R/W GPIOs
    //
    gpio_init( CE );
    gpio_set_dir( CE, GPIO_OUT );
    pio_gpio_init( pio, CE );

    gpio_init( RW );
    gpio_set_dir( RW, GPIO_IN );
    gpio_pull_down( RW );           // Don't let it float (probably not needed)
    pio_gpio_init( pio, RW );

    // Configure address bus pins as in pins
    //
    for ( int pin = PIN_BASE_ADDR; pin < PIN_BASE_ADDR+16; ++pin ) {
        pio_gpio_init( pio, pin );
        gpio_set_dir( pin, GPIO_IN );
    }    

    // Configure data bus pins initially as out pins
    // Exclude GPIOs 23, 24 and 25
    //
    for ( int pin = PIN_BASE_DATA; pin < PIN_BASE_DATA+11; ++pin ) {
        if ( pin < 23 || pin > 25 ) {
            pio_gpio_init( pio, pin );
            gpio_set_dir( pin, GPIO_OUT );
        }
    }   
}

static int piocfg_create_memread_sm( PIO pio )
{
    int memread_sm      = pio_claim_unused_sm( pio, true );                                 // Claim a free state machine for memory emulation on PIO 0
    uint memread_offset = pio_add_program( pio, &memread_program );                         // Instruction memory offset for the SM
    
    pio_sm_config memread_config = memread_program_get_default_config( memread_offset );    // Get default config for the memory emulation SM

    sm_config_set_in_pins ( &memread_config, PIN_BASE_ADDR );                               // Pin set for IN and GET instructions
    sm_config_set_out_pins( &memread_config, PIN_BASE_DATA, 11 );                           // Pin set for OUT instructions
    sm_config_set_set_pins( &memread_config, CE, 1 );                                       // Pin set for SET instructions
    sm_config_set_jmp_pin ( &memread_config, RW );                                          // Pin for conditional JMP instructions

    sm_config_set_in_shift ( &memread_config, false, true, 16+1 );                          // Shift left address bus and additional 0 from ISR, autopush resultant 32bit address to DMA RXF
    sm_config_set_out_shift( &memread_config, true, true, 13 );                             // Shift right 11 bits to OSR: CE + (8bit data + 3 unused) + RW to data bus, autopull enabled

    pio_sm_set_consecutive_pindirs( pio, memread_sm, PIN_BASE_DATA, 11, true );             // Set data bus pins as outputs
    pio_sm_set_pindirs_with_mask( pio, memread_sm, (1 << CE), (1 << CE)|(1 << RW) );        // Set CE as output, RW as input
    pio_sm_set_pins_with_mask( pio, memread_sm, (1 << CE), (1 << CE) );                     // Ensure CE is disabled by default

    pio_sm_init( pio, memread_sm, memread_offset, &memread_config );

    return memread_sm;
}

static int piocfg_create_memwrite_sm( PIO pio )
{
    int memwrite_sm      = pio_claim_unused_sm( pio, true );                                // Claim a free state machine for memory write on PIO 0
    uint memwrite_offset = pio_add_program( pio, &memwrite_program );                       // Instruction memory offset for the SM
    
    pio_sm_config memwrite_config = memwrite_program_get_default_config( memwrite_offset ); // Get default config for the memory write SM

    // Configure: IN  pins: DATA    (PINCTRL_IN_BASE, PINCTRL_IN_COUNT)
    //            JMP pin:  WR      (EXECCTRL_JMP_PIN)
    //
    sm_config_set_in_pins( &memwrite_config, CE );                                          // Pin set for IN and GET instructions. See FIXME note below
    sm_config_set_jmp_pin( &memwrite_config, RW );                                          // Pin for conditional JMP instructions

    // FIXME: I'm assuming that reading CE, which is an putput pin, will get the correct value (which is 0)
    //        CHECK THAT IT WORKS AND, IF NOT, RECONFIGURE in pins to PIN_BASE_DATA and shift a 0 before data in the pio code
    //
    sm_config_set_in_shift( &memwrite_config, false, false, 13 );                           // Shift left CE, data (8bit data + 3 unused) and RW into ISR, no autopush

    pio_sm_set_pindirs_with_mask( pio, memwrite_sm, (1 << CE), (1 << CE)|(1 << RW) );       // Set CE as output, RW as input

    pio_sm_init( pio, memwrite_sm, memwrite_offset, &memwrite_config );

    return memwrite_sm;
}

void piocfg_setup( uint16_t *mem_map )
{
    // Configure PIO
    //
    PIO pio = pio0;

    piocfg_gpio_pins( pio );

    // Create and configure state machines
    //
    // * memread_sm performs the read operations and, when a write is requested, checks if memory is writable and, if so, starts handles control to memwrite_sm
    // * memwrite_sm performs the write operation and returns control to memread_sm
    //
    int memread_sm      = piocfg_create_memread_sm ( pio );
    int memwrite_sm     = piocfg_create_memwrite_sm( pio );

    // Configure the DMA channels
    //
    // * Channel read_addr_dma:  Moves address from memread_sm RX FiFo ( addr bus combined with the mem_map base address) to the read_data_dma channel config read address trigger. Chains to write_addr_dma
    // * Channel write_addr_dma: Moves address from read_data_dma channel config read address to the write_data_dma channel config write trigger address. Chains to read_addr_dma.
    // * Channel read_data_dma:  Moves data from mem_map (as previosly set by read_addr_dma) to memread_sm TX FiFo. Does not chain.
    // * Channel write_data_dma: Moves data from memwrite_sm RX FiFo to the mem_map addr configured by write_addr_dma.Does not chain.
    //
    int read_addr_dma   = dma_claim_unused_channel( true );
    int read_data_dma   = dma_claim_unused_channel( true );
    int write_addr_dma  = dma_claim_unused_channel( true );
    int write_data_dma  = dma_claim_unused_channel( true );

    dma_channel_config read_addr_dma_config = dmacfg_config_channel(
                read_addr_dma,
                pio_get_dreq( pio, memread_sm, false ),                     // Signals data transfer from PIO, receive
                DMA_SIZE_32,
                write_addr_dma,                                             // Chains to write_addr_dma when finished
                &dma_channel_hw_addr(read_data_dma)->al3_read_addr_trig,    // Writes to read address trigger of read_data_dma
                &pio->rxf[memread_sm],                                      // Reads from memread_sm RX FiFo
                true                                                        // Starts immediately
                );

    dma_channel_config write_addr_dma_config = dmacfg_config_channel(
                write_addr_dma,
                DREQ_FORCE,                                                 // Permanent request transfer
                DMA_SIZE_32,
                read_addr_dma,                                              // Chains to read_addr_dma when finished
                &dma_channel_hw_addr(write_data_dma)->al2_write_addr_trig,  // Writes to write address trigger of write_data_dma
                &dma_channel_hw_addr(read_data_dma)->al3_read_addr_trig,    // Reads from read address trigger of read_data_dma
                false                                                       // Does not start
                );

    dma_channel_config read_data_dma_config = dmacfg_config_channel(
                read_data_dma,
                pio_get_dreq( pio, memread_sm, true ),                      // Signals data transfer from PIO, transmit
                DMA_SIZE_16,
                read_data_dma,                                              // Does not chain (chain to itself means no chain)
                &pio->txf[memread_sm],                                      // Writes memread_sm TX FiFo
                mem_map,                                                    // Reads from mem_map (efective address configured by read_addr_dma)
                false                                                       // Does not start
                );

    dma_channel_config write_data_dma_config = dmacfg_config_channel(
                write_data_dma,
                pio_get_dreq( pio, memwrite_sm, false ),                    // Signals data transfer from PIO, receive
                DMA_SIZE_16,
                write_data_dma,                                             // Does not chain chain (chain to itself means no chain)
                mem_map,                                                    // Writes to mem_map (efective address configured by write_addr_dma)
                &pio->rxf[memwrite_sm],                                     // Reads from memwrite_sm RX FiFo
                false                                                       // Does not start
                );

    // Enable State Machines
    //

    // Put mem_map base address shifted 17 bits, so the 16 gpios + a '0' (multiplied by 2) gives the correct addr
    //
    pio_sm_put( pio, memread_sm, dma_channel_hw_addr( read_data_dma )->read_addr >> 17 );
    pio_sm_set_enabled( pio, memread_sm, true );

    pio_sm_set_enabled( pio, memwrite_sm, true );

}


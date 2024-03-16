#include "hardware/dma.h"

dma_channel_config dmacfg_config_channel(
                int channel,
                uint dreq,
                enum dma_channel_transfer_size size,
                int chain,
                volatile void *write_addr,
                const volatile void *read_addr,
                bool trigger
                )
{
    dma_channel_config dma_config = dma_channel_get_default_config( channel );  // Get default config structure

    channel_config_set_high_priority( &dma_config, true );
    channel_config_set_dreq( &dma_config, dreq );
    channel_config_set_transfer_data_size( &dma_config, size );
    channel_config_set_read_increment( &dma_config, false );                    // No increment, stay on same address
    channel_config_set_chain_to( &dma_config, chain );
    dma_channel_configure(  channel,
                            &dma_config,
                            write_addr,
                            read_addr,
                            1,                                                  // Do 1 transfer
                            trigger );

    return dma_config;
}


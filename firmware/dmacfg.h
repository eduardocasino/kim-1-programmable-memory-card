#include "hardware/dma.h"

dma_channel_config dmacfg_config_channel(
                int channel,
                uint dreq,
                enum dma_channel_transfer_size size,
                int chain,
                volatile void *write_addr,
                const volatile void *read_addr,
                bool trigger
                );

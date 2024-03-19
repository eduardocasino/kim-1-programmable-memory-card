#define A0       0
#define A1       1
#define A2       2
#define A3       3
#define A4       4
#define A5       5
#define A6       6
#define A7       7
#define A8       8
#define A9       9
#define A10     10
#define A11     11
#define A12     12
#define A13     13
#define A14     14
#define A15     15

#define D0      27
#define D1      26
#define D2      22
#define D3      21
#define D4      20
#define D5      19
#define D6      18
#define D7      17

#define CE      16
#define RW      28

#define PIN_BASE_ADDR   A0
#define PIN_BASE_DATA   D7

#define PIN_CE_MASK     ( 1 << CE )
#define PIN_RW_MASK     ( 1 << RW )

#define HIGH            true
#define LOW             false

#define BYTEPREP( X )   ( ( (X&0x80) >> 6 ) | ( (X&0x40) >> 4 ) | ( (X&0x20) >> 2 ) |   (X&0x10)       | \
                          ( (X&0x08) << 2 ) | ( (X&0x04) << 4 ) | ( (X&0x02) << 9 ) | ( (X&0x01) << 11 ) )

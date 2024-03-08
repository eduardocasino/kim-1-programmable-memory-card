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

#define PIN_ADDR_MASK   ( (1 << A15) | (1 << A14) | (1 << A13) | (1 << A12) | (1 << A11) | (1 << A10) | (1 << A9) | (1 << A8) | \
                          (1 << A7 ) | (1 << A6 ) | (1 << A5 ) | (1 << A4 ) | (1 << A3 ) | (1 << A2 ) | (1 << A1) | (1 << A0) ) 
#define PIN_DATA_MASK   ( (1 << D7) | (1 << D6) | (1 << D5) | (1 << D4) | (1 << D3) | (1 << D2) | (1 << D1) | (1 << D0) )
#define PIN_CE_MASK     ( 1 << CE )
#define PIN_RW_MASK     ( 1 << RW )

#define HIGH            true
#define LOW             false

#define BYTEPREP( X )   ( ( (X&0x80) >> 6 ) | ( (X&0x40) >> 4 ) | ( (X&0x20) >> 2 ) |   (X&0x10)       | \
                          ( (X&0x08) << 2 ) | ( (X&0x04) << 4 ) | ( (X&0x02) << 9 ) | ( (X&0x01) << 11 ) )

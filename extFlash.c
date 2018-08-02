#include <extFlash.h>
#include <hardware.h>
#include <NVM.h>
#include <sam.h>
#include <stdlib.h>
#include <string.h>

// Manufacturer ID
#define ADESTO 0x1F
#define MACRONIX 0xC2

// Device ID
#define AT25XE011 0x4200
#define AT25DF041B 0x4402
#define MX25R8035F 0x14

uint32_t _memSize = 0;
uint32_t _prgmSpace = 0;

#define FLASH_SELECT                          \
    {                                         \
        PORT->Group[0].OUTCLR.reg = FLASH_SS; \
    }
#define FLASH_UNSELECT                        \
    {                                         \
        PORT->Group[0].OUTSET.reg = FLASH_SS; \
    }

uint8_t FLASH_busy()
{
    FLASH_SELECT;
    SPI_transfer( SPIFLASH_STATUSREAD );
    uint8_t status = SPI_transfer( 0 );
    FLASH_UNSELECT;
    return ( status & 0x01 );
}

void FLASH_command( uint8_t cmd, uint8_t isWrite )
{
    if( isWrite ) {
        FLASH_command( SPIFLASH_WRITEENABLE, 0 );
        FLASH_UNSELECT;
    }

    while( FLASH_busy() )
        ;

    FLASH_SELECT;
    SPI_transfer( cmd );
}

uint8_t FLASH_readByte( uint32_t addr )
{
    FLASH_command( SPIFLASH_ARRAYREADLOWFREQ, 0 );
    SPI_transfer( addr >> 16 );
    SPI_transfer( addr >> 8 );
    SPI_transfer( addr );
    uint8_t result = SPI_transfer( 0 );
    FLASH_UNSELECT;
    return result;
}

void CheckFlashImage()
{
    // Get manufacturer ID and JEDEC ID
    FLASH_SELECT;
    SPI_transfer( SPIFLASH_JEDECID );
    uint8_t  manufacturerId = SPI_transfer( 0 );
    uint16_t deviceId = SPI_transfer( 0 );
    deviceId <<= 8;
    deviceId |= SPI_transfer( 0 );
    FLASH_UNSELECT;

    // Check against manufacturer ID and JEDEC ID
    switch( manufacturerId ) {
        case ADESTO:
            if( deviceId == AT25XE011 )
                _memSize = 0x20000;
            else if( deviceId == AT25DF041B )
                _memSize = 0x80000;
            break;
        case MACRONIX:
            // TODO: MACRONIX
            if( deviceId == MX25R8035F ) _memSize = 0x100000;
            break;
        default:
            // Unknown manufacturer
            return;
    }

    // Global unprotect
    FLASH_command( SPIFLASH_STATUSWRITE, 1 );
    SPI_transfer( 0 );
    FLASH_UNSELECT;

    /* Memory Layout
     ~~~ |0                   10                  20                  30 | ...
     ~~~ |0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1| ...
     ~~~ +---------------------------------------------------------------+
     ~~~ |F L X I M G|:|X X X X|:|D D D D D D D D D D D D D D D D D D D D| ...
     ~~~ + - - - - - - - - - - - - - - - +-------------------------------+
     ~~~ | ID String |S|Len|S| Binary Image data                         | ...
     ~~~ +---------------------------------------------------------------+
     ~~~~~~ */
    // Check for an image
    if( FLASH_readByte( 0 ) != 'F' )
        return;
    else if( FLASH_readByte( 1 ) != 'L' )
        goto erase;
    else if( FLASH_readByte( 2 ) != 'X' )
        goto erase;
    else if( FLASH_readByte( 6 ) != ':' )
        goto erase;
    else if( FLASH_readByte( 11 ) != ':' )
        goto erase;

    // Grab internal flash memory parameters
    NVMParams_t params = getNVMParams();
    _prgmSpace = ( params.nvmTotalSize - params.bootSize - params.eepromSize );

    // Grab the image size and validate
    uint32_t imagesize = ( FLASH_readByte( 7 ) << 24 ) |
                         ( FLASH_readByte( 8 ) << 16 ) |
                         ( FLASH_readByte( 9 ) << 8 ) | FLASH_readByte( 10 );
    if( imagesize == 0 || imagesize > _memSize || imagesize > _prgmSpace )
        goto erase;

    // Variables for moving the image to internal program space
    uint32_t i;
    uint32_t prgmSpaceAddr = params.bootSize;
    uint16_t cacheIndex = 0;
    uint8_t *cache = (uint8_t *)malloc( sizeof( uint8_t ) * params.pageSize );

    // Copy the image to program space one page at a time
    for( i = 0; i < imagesize; i++ ) {
        cache[cacheIndex++] = FLASH_readByte( i + 12 );
        if( cacheIndex == params.pageSize ) {
            eraseRow( prgmSpaceAddr );
            writeFlash( prgmSpaceAddr, cache, params.pageSize );
            prgmSpaceAddr += params.pageSize;
            cacheIndex = 0;
        }
    }

    free( cache );

erase : {
    uint32_t flashAddr;
    for( flashAddr = 0; flashAddr < imagesize; flashAddr += 0x8000 ) {
        FLASH_command( SPIFLASH_BLOCKERASE_32K, 1 );
        SPI_transfer( flashAddr >> 16 );
        SPI_transfer( flashAddr >> 8 );
        SPI_transfer( flashAddr );
        FLASH_UNSELECT;
    }
}

    // TODO: Reset CPU?
}
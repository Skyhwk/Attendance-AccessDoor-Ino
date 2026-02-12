#include "sdcard.h"
#include <SPI.h>
#include <SD.h>

#define SD_CS 5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK 18

bool SDCard_init()
{
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    return SD.begin(SD_CS);
}

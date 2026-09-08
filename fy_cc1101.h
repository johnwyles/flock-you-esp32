// flock-you-esp32 — CC1101 sub-GHz support
// M5Stack CC1101 Module (315/433/868/915 MHz) via GROVE Port A

#ifndef FY_CC1101_H
#define FY_CC1101_H

#include <Arduino.h>
#include <SPI.h>

// CC1101 SPI pins (shared SPI bus with LoRa on Port B)
// LoRa: SCK=18, MISO=19, MOSI=23, CS=5
// CC1101: SCK=18, MISO=19, MOSI=23, CS=4 (different CS avoids conflict)
#define CC1101_CS   4
#define CC1101_MOSI 23
#define CC1101_MISO 19
#define CC1101_SCK  18
#define CC1101_GDO0 0

// CC1101 register addresses
#define CC1101_IOCFG2       0x00
#define CC1101_IOCFG1       0x01
#define CC1101_IOCFG0       0x02
#define CC1101_FIFOTHR      0x03
#define CC1101_SYNC1        0x04
#define CC1101_SYNC0        0x05
#define CC1101_PKTCTRL1     0x07
#define CC1101_PKTCTRL0     0x08
#define CC1101_ADDR         0x09
#define CC1101_CHANNR       0x0A
#define CC1101_FREQ2        0x0D
#define CC1101_FREQ1        0x0E
#define CC1101_FREQ0        0x0F
#define CC1101_MDMCFG4      0x10
#define CC1101_MDMCFG3      0x11
#define CC1101_MDMCFG2      0x12
#define CC1101_MDMCFG1      0x13
#define CC1101_MDMCFG0      0x14
#define CC1101_DEVIATN      0x15
#define CC1101_MCSM2        0x16
#define CC1101_MCSM1        0x17
#define CC1101_MCSM0        0x18
#define CC1101_FOCCFG       0x19
#define CC1101_BSCFG        0x1A
#define CC1101_AGCCTRL2     0x1B
#define CC1101_AGCCTRL1     0x1C
#define CC1101_AGCCTRL0     0x1D
#define CC1101_WOREVT1      0x1E
#define CC1101_WOREVT0      0x1F
#define CC1101_WORCTRL      0x20
#define CC1101_FREND1       0x21
#define CC1101_FREND0       0x22
#define CC1101_FSCAL3       0x23
#define CC1101_FSCAL2       0x24
#define CC1101_FSCAL1       0x25
#define CC1101_FSCAL0       0x26
#define CC1101_RCCTRL1      0x27
#define CC1101_RCCTRL0      0x28
#define CC1101_FSTEST       0x29
#define CC1101_PTEST        0x2A
#define CC1101_AGCTEST      0x2B
#define CC1101_TEST2        0x2C
#define CC1101_TEST1        0x2D
#define CC1101_TEST0        0x2E
#define CC1101_PARTNUM      0x30
#define CC1101_VERSION      0x31
#define CC1101_FREQEST      0x32
#define CC1101_LQI          0x33
#define CC1101_RSSI         0x34
#define CC1101_MARCSTATE    0x35
#define CC1101_WORTIME1     0x36
#define CC1101_WORTIME0     0x37
#define CC1101_PKTSTATUS    0x38
#define CC1101_VCO_VC_DAC   0x39
#define CC1101_TXBYTES      0x3A
#define CC1101_RXBYTES      0x3B
#define CC1101_PATABLE      0x3E

// CC1101 command strobes
#define CC1101_SRES          0x30
#define CC1101_SFSTXON       0x31
#define CC1101_SXOFF         0x32
#define CC1101_SCAL          0x33
#define CC1101_SRX           0x34
#define CC1101_STX           0x35
#define CC1101_SIDLE         0x36
#define CC1101_SWOR          0x38
#define CC1101_SPWD          0x39
#define CC1101_SFRX          0x3A
#define CC1101_SFTX          0x3B
#define CC1101_SWORRST       0x3C
#define CC1101_SNOP          0x3D

// CC1101 states
#define CC1101_STATE_IDLE    0x00
#define CC1101_STATE_RX      0x10
#define CC1101_STATE_TX      0x20

// Sub-GHz frequency bands
#define CC1101_BAND_315      0
#define CC1101_BAND_433      1
#define CC1101_BAND_868      2
#define CC1101_BAND_915      3

// Detected signal types
#define CC1101_SIG_UNKNOWN   0
#define CC1101_SIG_TPMS      1  // Tire pressure monitoring (433 MHz FSK)
#define CC1101_SIG_REMOTE    2  // Car/key remote (315/433 MHz OOK)
#define CC1101_SIG_WEATHER   3  // Weather station (433 MHz)
#define CC1101_SIG_GARAGE    4  // Garage door (315/433 MHz)
#define CC1101_SIG_LORA      5  // LoRa signal detected

// Structure for detected sub-GHz signal
struct SubGHzDetection {
  uint32_t frequency;  // Hz
  int8_t rssi;         // dBm
  uint8_t band;        // CC1101_BAND_*
  uint8_t length;      // packet length
  uint8_t data[64];    // raw packet data
  uint8_t sigType;     // CC1101_SIG_*
  unsigned long timestampMs;
};

// Global CC1101 state (defined in main.cpp)

// Initialize CC1101 on SPI
void cc1101Init(SPIClass &spi = SPI, uint8_t cs = CC1101_CS);

// Detect CC1101 presence (reads PARTNUM register)
bool cc1101Detect(SPIClass &spi = SPI, uint8_t cs = CC1101_CS);

// Set frequency band (315/433/868/915 MHz)
void cc1101SetBand(uint8_t band);

// Start receiving
void cc1101Rx();

// Read packet if available (non-blocking)
bool cc1101ReadPacket(SubGHzDetection &det);

// Scan all bands for signals
void cc1101Scan();

// Classify signal type from packet data
uint8_t cc1101Classify(const uint8_t *data, uint8_t len, uint8_t band);

// Append sub-GHz fields to detection JSON
void cc1101AppendToJSON(char *buf, size_t len);

// Global state (defined in main.cpp)
extern SubGHzDetection gSubGHzDet;

#endif /* FY_CC1101_H */

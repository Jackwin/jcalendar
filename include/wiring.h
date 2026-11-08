
#ifndef __WIRING_H__
#define __WIRING_H__

#if CONFIG_IDF_TARGET_ESP32C3
// ESP32-C3 DevKitM uses the board's default FSPI pins. This target cannot use
// the classic ESP32 GPIO 22/23 mapping. Hardware wiring remains to be verified.
#define SPI_MOSI GPIO_NUM_6
#define SPI_MISO GPIO_NUM_5
#define SPI_SCK GPIO_NUM_4
#define SPI_CS GPIO_NUM_7
#define SPI_DC GPIO_NUM_3
#define SPI_RST GPIO_NUM_2
#define SPI_BUSY GPIO_NUM_1
#define I2C_SDA GPIO_NUM_9
#define I2C_SCL GPIO_NUM_10
#define KEY_M GPIO_NUM_0
#define PIN_LED_R GPIO_NUM_8
#define PIN_ADC GPIO_NUM_0
#else
// Output PIN
#define SPI_MOSI GPIO_NUM_23
#define SPI_MISO GPIO_NUM_19 // Reserved
#define SPI_SCK GPIO_NUM_18
#define SPI_CS GPIO_NUM_5
#define SPI_DC GPIO_NUM_17
#define SPI_RST GPIO_NUM_16
#define SPI_BUSY GPIO_NUM_4
// I2C
#define I2C_SDA GPIO_NUM_21
#define I2C_SCL GPIO_NUM_22
// Other PIN
#define KEY_M GPIO_NUM_14 // 注意：由于此按键负责唤醒，因此需要选择支持RTC唤醒的PIN脚。
#define PIN_LED_R GPIO_NUM_22

#define PIN_ADC GPIO_NUM_32 // ADC
#endif

#endif

#pragma once

namespace Pins {

constexpr int SD_CS = 13;
constexpr int SD_MISO = 2;
constexpr int SD_MOSI = 15;
constexpr int SD_SCK = 14;

constexpr int I2S_BCLK = 27;
constexpr int I2S_LRC = 25;
constexpr int I2S_DOUT = 26;

constexpr int I2C_SCL = 32;
constexpr int I2C_SDA = 33;
constexpr int PA_EN = 21;

constexpr int OLED_SCK = 18;
constexpr int OLED_MOSI = 23;

constexpr int MCP_SDA = 22;
constexpr int MCP_SCL = 19;
constexpr int MCP_INTA = 5;
constexpr int MCP_INTB = 0;
constexpr uint8_t MCP_I2C_ADDR = 0x20;

}  // namespace Pins

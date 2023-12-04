#include <Arduino.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lut.h"
#include "settings.h"
#include "hal.h"
#include "wdt.h"

#include "drawing.h"

#define CMD_DRV_OUTPUT_CTRL 0x01
#define CMD_SOFT_START_CTRL 0x0C
#define CMD_ENTER_SLEEP 0x10
#define CMD_DATA_ENTRY_MODE 0x11
#define CMD_SOFT_RESET 0x12
#define CMD_SOFT_RESET2 0x13
#define CMD_SETUP_VOLT_DETECT 0x15
#define CMD_TEMP_SENSOR_CONTROL 0x18
#define CMD_ACTIVATION 0x20
#define CMD_DISP_UPDATE_CTRL 0x21
#define CMD_DISP_UPDATE_CTRL2 0x22
#define CMD_WRITE_FB_BW 0x24
#define CMD_WRITE_FB_RED 0x26
#define CMD_VCOM_GLITCH_CTRL 0x2B
#define CMD_LOAD_OTP_LUT 0x31
#define CMD_WRITE_LUT 0x32
#define CMD_BORDER_WAVEFORM_CTRL 0x3C
#define CMD_WINDOW_X_SIZE 0x44
#define CMD_WINDOW_Y_SIZE 0x45
#define CMD_WRITE_PATTERN_RED 0x46
#define CMD_WRITE_PATTERN_BW 0x47
#define CMD_XSTART_POS 0x4E
#define CMD_YSTART_POS 0x4F
#define CMD_ANALOG_BLK_CTRL 0x74
#define CMD_DIGITAL_BLK_CTRL 0x7E

#define SCREEN_CMD_CLOCK_ON 0x80
#define SCREEN_CMD_CLOCK_OFF 0x01
#define SCREEN_CMD_ANALOG_ON 0x40
#define SCREEN_CMD_ANALOG_OFF 0x02
#define SCREEN_CMD_LATCH_TEMPERATURE_VAL 0x20
#define SCREEN_CMD_LOAD_LUT 0x10
#define SCREEN_CMD_USE_MODE_2 0x08  // modified commands 0x10 and 0x04
#define SCREEN_CMD_REFRESH 0xC7

#define commandEnd()                \
    do {                            \
        digitalWrite(EPD_CS, HIGH); \
    } while (0)

#define markCommand()              \
    do {                           \
        digitalWrite(EPD_DC, LOW); \
    } while (0)

#define markData()                  \
    do {                            \
        digitalWrite(EPD_DC, HIGH); \
    } while (0)

static bool isInited = false;

static void commandReadBegin(uint8_t cmd) {
    epdSelect();
    markCommand();
    spi_write(cmd);
    pinMode(EPD_MOSI, INPUT);
    markData();
}
static void commandReadEnd() {
    // set up pins for spi (0.0,0.1,0.2)
    pinMode(EPD_MOSI, OUTPUT);
    epdDeselect();
}
static uint8_t epdReadByte() {
    uint8_t val = 0, i;

    for (i = 0; i < 8; i++) {
        digitalWrite(EPD_CLK, HIGH);
        delayMicroseconds(1);
        val <<= 1;
        if (digitalRead(EPD_MOSI))
            val++;
        digitalWrite(EPD_CLK, LOW);
        delayMicroseconds(1);
    }

    return val;
}
static void shortCommand(uint8_t cmd) {
    epdSelect();
    markCommand();
    spi_write(cmd);
    epdDeselect();
}
static void shortCommand1(uint8_t cmd, uint8_t arg) {
    epdSelect();
    markCommand();
    spi_write(cmd);
    markData();
    spi_write(arg);
    epdDeselect();
}
static void shortCommand2(uint8_t cmd, uint8_t arg1, uint8_t arg2) {
    epdSelect();
    markCommand();
    spi_write(cmd);
    markData();
    spi_write(arg1);
    spi_write(arg2);
    epdDeselect();
}
static void commandBegin(uint8_t cmd) {
    epdSelect();
    markCommand();
    spi_write(cmd);
    markData();
}

void setWindowX(uint16_t start, uint16_t end) {
    epdWrite(CMD_WINDOW_X_SIZE, 2, start / 8, end / 8 - 1);
}
void setWindowY(uint16_t start, uint16_t end) {
    epdWrite(CMD_WINDOW_Y_SIZE, 4, (start)&0xff, (start) >> 8, (end - 1) & 0xff, (end - 1) >> 8);
}
void setPosXY(uint16_t x, uint16_t y) {
    epdWrite(CMD_XSTART_POS, 1, (uint8_t)(x / 8));
    epdWrite(CMD_YSTART_POS, 2, (y)&0xff, (y) >> 8);
}


void selectLUT(uint8_t lut) {
    // implement alternative LUTs here. Currently just reset the watchdog to two minutes,
    // to ensure it doesn't reset during the much longer bootup procedure
    lut += 1;  // make the compiler a happy camper
    wdt120s();
    return;
}


void epdEnterSleep() {
    digitalWrite(EPD_RST, LOW);
    delay(10);
    digitalWrite(EPD_RST, HIGH);
    delay(50);
    shortCommand(CMD_SOFT_RESET2);
    epdBusyWaitFalling(15);
    shortCommand1(CMD_ENTER_SLEEP, 0x03);
    isInited = false;
}
void epdSetup() {
    epdReset();
    shortCommand(CMD_SOFT_RESET);  // software reset
    delay(10);
    shortCommand(CMD_SOFT_RESET2);
    delay(10);
    epdWrite(CMD_ANALOG_BLK_CTRL, 1, 0x54);
    epdWrite(CMD_DIGITAL_BLK_CTRL, 1, 0x3B);
    epdWrite(CMD_VCOM_GLITCH_CTRL, 2, 0x04, 0x63);
    epdWrite(CMD_DRV_OUTPUT_CTRL, 3, (SCREEN_HEIGHT - 1) & 0xff, (SCREEN_HEIGHT - 1) >> 8, 0x00);
    epdWrite(CMD_DISP_UPDATE_CTRL, 2, 0x08, 0x00);
    epdWrite(CMD_BORDER_WAVEFORM_CTRL, 1, 0x01);
    epdWrite(CMD_TEMP_SENSOR_CONTROL, 1, 0x80);
    epdWrite(CMD_DISP_UPDATE_CTRL2, 1, 0xB1);
    epdWrite(CMD_ACTIVATION, 0);
    epdBusyWaitFalling(10000);
    isInited = true;
}
static uint8_t epdGetStatus() {
    uint8_t sta;
    commandReadBegin(0x2F);
    sta = epdReadByte();
    commandReadEnd();
    return sta;
}

void epdWriteDisplayData() {
    setWindowX(SCREEN_XOFFSET, SCREEN_WIDTH + SCREEN_XOFFSET);
    setPosXY(SCREEN_XOFFSET, 0);
    // epdWrite(CMD_DISP_UPDATE_CTRL, 1, 0x08);

    // this display expects two entire framebuffers worth of data to be written, one for b/w and one for red
    uint8_t *buf[2] = {0, 0};  // this will hold pointers to odd/even data lines
    for (uint8_t c = 0; c < 2; c++) {
        if (c == 0) epd_cmd(CMD_WRITE_FB_BW);
        if (c == 1) epd_cmd(CMD_WRITE_FB_RED);
        markData();
        epdSelect();
        for (uint16_t curY = 0; curY < SCREEN_HEIGHT; curY += 2) {
            // Get 'even' screen line
            buf[0] = (uint8_t *)calloc(SCREEN_WIDTH / 8, 1);
            drawItem::renderDrawLine(buf[0], curY, c);

            // on the first pass, the second (buf[1]) buffer is unused, so we don't have to wait for it to flush to the display / free it
            if (buf[1]) {
                // wait for 'odd' display line to finish writing to the screen
                epdSPIWait();
                free(buf[1]);
            }

            // start transfer of even data line to the screen
            epdSPIAsyncWrite(buf[0], (SCREEN_WIDTH / 8));

            // Get 'odd' screen display line
            buf[1] = (uint8_t *)calloc(SCREEN_WIDTH / 8, 1);
            drawItem::renderDrawLine(buf[1], curY + 1, c);

            // wait until the 'even' data has finished writing
            epdSPIWait();
            free(buf[0]);

            // start transfer of the 'odd' data line
            epdSPIAsyncWrite(buf[1], (SCREEN_WIDTH / 8));
        }
        // check if this was the first pass. If it was, we'll need to wait until the last display line finished writing
        if (c == 0) {
            epdSPIWait();
            epdDeselect();
            free(buf[1]);
            buf[1] = nullptr;
        }
    }
    // flush the draw list, make sure items don't appear on subsequent screens
    drawItem::flushDrawItems();

    // wait until the last line of display has finished writing and clean our stuff up
    epdSPIWait();
    epdDeselect();
    if (buf[1]) free(buf[1]);
}

void draw() {
    drawNoWait();
    getVoltage();
    epdBusyWaitFalling(120000);
}
void drawNoWait() {
    epdWriteDisplayData();
    epdWrite(0x22, 1, 0xF7);
    epdWrite(0x20, 0);
}
void epdWaitRdy() {
    epdBusyWaitFalling(120000);
}
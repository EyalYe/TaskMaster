#include "sh1106.h"
#include "board_pins.h"
#include "font5x7.h"

#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "sh1106";

#define SH1106_COL_OFFSET 2          /* visible area starts at RAM column 2 */
#define PAGES (OLED_H / 8)           /* 8 pages of 8 vertical pixels */

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static uint8_t s_fb[OLED_W * PAGES]; /* 1 byte = 8 vertical px; index = page*W + x */

static esp_err_t cmd(uint8_t c)
{
    uint8_t b[2] = {0x00, c};        /* control byte 0x00 = command */
    return i2c_master_transmit(s_dev, b, sizeof(b), 100);
}

esp_err_t sh1106_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "bus");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDR,
        .scl_speed_hz = OLED_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev), TAG, "dev");

    /* SH1106 power-on init sequence (note the SH1106-specific DC-DC cmd 0xAD/0x8B). */
    static const uint8_t seq[] = {
        0xAE,             /* display off */
        0xD5, 0x80,       /* clock div / osc freq */
        0xA8, 0x3F,       /* multiplex = 63 */
        0xD3, 0x00,       /* display offset = 0 */
        0x40,             /* start line = 0 */
        0xAD, 0x8B,       /* DC-DC control: enable */
        0xA1,             /* segment remap */
        0xC8,             /* COM scan direction = remapped */
        0xDA, 0x12,       /* COM pins config */
        0x81, 0x80,       /* contrast */
        0xD9, 0x22,       /* pre-charge period */
        0xDB, 0x35,       /* VCOMH deselect */
        0xA4,             /* resume from RAM */
        0xA6,             /* normal (non-inverted) */
        0xAF,             /* display on */
    };
    for (size_t i = 0; i < sizeof(seq); i++) {
        ESP_RETURN_ON_ERROR(cmd(seq[i]), TAG, "init cmd");
    }

    sh1106_clear();
    return sh1106_flush();
}

void sh1106_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

void sh1106_pixel(int x, int y, int on)
{
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    uint8_t *p = &s_fb[(y / 8) * OLED_W + x];
    uint8_t mask = 1u << (y & 7);
    if (on) *p |= mask; else *p &= ~mask;
}

static void draw_char(int x, int row, char c)
{
    if (c >= 'a' && c <= 'z') c -= 0x20;          /* uppercase */
    if (c < FONT_FIRST || c > FONT_LAST) c = ' ';
    const uint8_t *g = font5x7[c - FONT_FIRST];
    int y0 = row * 8;
    for (int col = 0; col < FONT_W; col++) {
        for (int b = 0; b < 7; b++) {
            sh1106_pixel(x + col, y0 + b, (g[col] >> b) & 1);
        }
    }
}

void sh1106_text(int x, int row, const char *s)
{
    for (; *s && x < OLED_W; s++, x += FONT_W + 1) {
        draw_char(x, row, *s);
    }
}

void sh1106_text_line(int row, const char *s)
{
    if (row < 0 || row >= PAGES) return;
    memset(&s_fb[row * OLED_W], 0, OLED_W);       /* clear the row first */
    sh1106_text(0, row, s);
}

esp_err_t sh1106_flush(void)
{
    uint8_t line[1 + OLED_W];
    line[0] = 0x40;                               /* control byte 0x40 = data */
    for (int page = 0; page < PAGES; page++) {
        ESP_RETURN_ON_ERROR(cmd(0xB0 | page), TAG, "page");
        ESP_RETURN_ON_ERROR(cmd(0x00 | (SH1106_COL_OFFSET & 0x0F)), TAG, "col lo");
        ESP_RETURN_ON_ERROR(cmd(0x10 | (SH1106_COL_OFFSET >> 4)), TAG, "col hi");
        memcpy(&line[1], &s_fb[page * OLED_W], OLED_W);
        ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev, line, sizeof(line), 100), TAG, "data");
    }
    return ESP_OK;
}

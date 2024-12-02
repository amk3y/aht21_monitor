#include "freertos/FreeRTOS.h"

#include "esp_log.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include <driver/i2c.h>

#include "aht20.h"
#define DISP_WIDTH 240
#define DISP_HEIGHT 240
#define DISP_DRAW_BUFFER_HEIGHT 40
#define DISP_GPIO_RES GPIO_NUM_0
#define DISP_GPIO_DC GPIO_NUM_1
#define DISP_SCLK GPIO_NUM_4
#define DISP_MOSI GPIO_NUM_6

static lv_disp_t* disp_handle;

void init_spi_bus(){
    const spi_bus_config_t buscfg = {
            .sclk_io_num = DISP_SCLK,
            .mosi_io_num = DISP_MOSI,
            .miso_io_num = GPIO_NUM_NC,
            .quadwp_io_num = GPIO_NUM_NC,
            .quadhd_io_num = GPIO_NUM_NC,
            .max_transfer_sz = DISP_HEIGHT * DISP_DRAW_BUFFER_HEIGHT * sizeof(uint16_t)
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
}

void init_lvgl_disp(){
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    /* LCD IO */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
            .dc_gpio_num = DISP_GPIO_DC,
            .cs_gpio_num = GPIO_NUM_NC,
            .pclk_hz = 40 * 1000 * 1000,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            // this varies depending on hardware
            // bugfix ref: https://github.com/Bodmer/TFT_eSPI/issues/163
            .spi_mode = 3,
            .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) SPI2_HOST, &io_config, &io_handle));

    /* LCD driver initialization */

    esp_lcd_panel_handle_t lcd_panel_handle;
    const esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = DISP_GPIO_RES,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &lcd_panel_handle));

    esp_lcd_panel_reset(lcd_panel_handle);
    esp_lcd_panel_init(lcd_panel_handle);
    esp_lcd_panel_disp_on_off(lcd_panel_handle, true);
    //  depend on hardware
    esp_lcd_panel_invert_color(lcd_panel_handle, true);
    esp_lcd_panel_set_gap(lcd_panel_handle, 0, 80);

    /* Add LCD screen */
    const lvgl_port_display_cfg_t disp_cfg = {
            .io_handle = io_handle,
            .panel_handle = lcd_panel_handle,
            .buffer_size = DISP_HEIGHT * DISP_DRAW_BUFFER_HEIGHT * sizeof(uint16_t),
            .double_buffer = true,
            .hres = DISP_WIDTH,
            .vres = DISP_HEIGHT,
            .monochrome = false,
            .color_format = LV_COLOR_FORMAT_RGB565,
            .rotation = {
                    .swap_xy = false,
                    .mirror_x = true,
                    .mirror_y = true,
            },
            .flags = {
                    .buff_dma = true,
                    .swap_bytes = false,
            }
    };

    disp_handle = lvgl_port_add_disp(&disp_cfg);
}


void i2c_setup(){
    i2c_config_t i2c_config =
            {
                    .mode = I2C_MODE_MASTER,
                    .sda_io_num = GPIO_NUM_8,
                    .sda_pullup_en = GPIO_PULLUP_ENABLE,
                    .scl_io_num = GPIO_NUM_9,
                    .scl_pullup_en = GPIO_PULLUP_ENABLE,
                    .master.clk_speed =  10 * 10000,
            };
    i2c_param_config(I2C_NUM_0, &i2c_config);
    i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
}

void task_aht20_measurement(void* pvParameters) {
    AHT20_data_t sensor_data =
            {
                    .r_humidity = 0,
                    .temperature = 0,
                    .is_busy = 0
            };
    AHT20_begin(&sensor_data);

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_screen_active(), lv_color_hex(0xffffff), LV_PART_MAIN);

    lv_style_t label_style_title;
    lv_style_t label_style_value;
    lv_style_init(&label_style_title);
    lv_style_init(&label_style_value);

    lv_style_set_pad_all(&label_style_title, 24);
    lv_style_set_pad_all(&label_style_value, 10);
    lv_style_set_text_font(&label_style_title, &lv_font_montserrat_20);
    lv_style_set_text_font(&label_style_value, &lv_font_montserrat_42);

    lv_obj_t * label_temp_title = lv_label_create(lv_screen_active());
    lv_obj_t * label_rh_title = lv_label_create(lv_screen_active());
    lv_obj_t * label_temp_val = lv_label_create(lv_screen_active());
    lv_obj_t * label_rh_val = lv_label_create(lv_screen_active());

    lv_obj_add_style(label_rh_title, &label_style_title, LV_STATE_DEFAULT);
    lv_obj_add_style(label_temp_title, &label_style_title, LV_STATE_DEFAULT);
    lv_obj_add_style(label_rh_val, &label_style_value, LV_STATE_DEFAULT);
    lv_obj_add_style(label_temp_val, &label_style_value, LV_STATE_DEFAULT);
    lv_obj_update_layout(lv_screen_active());

    lv_coord_t coord_height_label_temp_title = lv_obj_get_height(label_temp_title);
    lv_coord_t coord_height_label_rh_title = lv_obj_get_height(label_rh_title);
    lv_coord_t coord_height_label_temp_val = lv_obj_get_height(label_temp_val);
    lv_coord_t coord_height_label_rh_val = lv_obj_get_height(label_rh_val);

    lv_obj_align(label_temp_title, LV_ALIGN_CENTER, 0, 0 - coord_height_label_temp_val / 2- coord_height_label_temp_title / 2);
    lv_obj_align(label_temp_val, LV_ALIGN_CENTER, 0, 0 - coord_height_label_temp_val / 2);
    lv_obj_align(label_rh_title, LV_ALIGN_CENTER, 0, coord_height_label_rh_title / 2);
    lv_obj_align(label_rh_val, LV_ALIGN_CENTER, 0, coord_height_label_rh_title / 2 + coord_height_label_rh_val / 2);

    lv_label_set_text(label_rh_title, "Relative Humidity");
    lv_label_set_text(label_temp_title, "Temperature");
    lv_label_set_text_fmt(label_rh_val, "--");
    lv_label_set_text_fmt(label_temp_val, "--");

    while (1) {
        vTaskDelay(250 / portTICK_PERIOD_MS);
        esp_err_t ret = AHT20_measure(&sensor_data);
        if (ret != ESP_OK) {
            continue;
        }
        ESP_LOGI("task_aht20", "Humidity %.2f, Temperature: %.2f",
                 sensor_data.r_humidity,
                 sensor_data.temperature
        );

        lv_label_set_text_fmt(label_rh_val, "%.1f%%", sensor_data.r_humidity);
        lv_label_set_text_fmt(label_temp_val, "%.1f", sensor_data.temperature);
    }
}

void app_main() {

    init_spi_bus();
    init_lvgl_disp();
    i2c_setup();

    xTaskCreate(&task_aht20_measurement, "task_aht20", 2048, NULL, 2, NULL);
}
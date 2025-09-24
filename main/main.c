#include <stdio.h>
#include "esp32_s3_szp.h"
#include "app_ui.h"
#include "nvs_flash.h"
#include <esp_system.h>

EventGroupHandle_t my_event_group;
static QueueHandle_t gpio_evt_queue = NULL;  // 定义队列句柄
static const char *TAG = "MAIN";
uint32_t app_count = 0;
// 打印内存使用情况
void displayMemoryUsage() {
    size_t totalDRAM = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t freeDRAM = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t usedDRAM = totalDRAM - freeDRAM;

    size_t totalPSRAM = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t freePSRAM = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t usedPSRAM = totalPSRAM - freePSRAM;

    size_t DRAM_largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t PSRAM_largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    float dramUsagePercentage = (float)usedDRAM / totalDRAM * 100;
    float psramUsagePercentage = (float)usedPSRAM / totalPSRAM * 100;

    ESP_LOGI(TAG, "DRAM Total: %zu bytes, Used: %zu bytes, Free: %zu bytes,  DRAM_Largest_block: %zu bytes", 
        totalDRAM, usedDRAM, freeDRAM, DRAM_largest_block);
    ESP_LOGI(TAG, "DRAM Used: %.2f%%", dramUsagePercentage);

    ESP_LOGI(TAG, "PSRAM Total: %zu bytes, Used: %zu bytes, Free: %zu bytes, PSRAM_Largest_block: %zu bytes", 
        totalPSRAM, usedPSRAM, freePSRAM, PSRAM_largest_block);
    ESP_LOGI(TAG, "PSRAM Used: %.2f%%", psramUsagePercentage);
} 

uint32_t read_or_init_boot_count() {
    nvs_handle_t handle;
    uint32_t boot_count = 0;
    
    // 打开自定义分区中的命名空间
    esp_err_t err = nvs_open_from_partition("custom_nvs", "boot_data", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error opening NVS namespace: %s", esp_err_to_name(err));
        return 0;
    }
    
    // 尝试读取
    err = nvs_get_u32(handle, "boot_count", &boot_count);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI("NVS", "Boot count not found, initializing to 0");
        boot_count = 0;
        nvs_set_u32(handle, "boot_count", boot_count);
        nvs_commit(handle);
    } else if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error reading boot count: %s", esp_err_to_name(err));
    }
    
    nvs_close(handle);
    return boot_count;
}

void increment_boot_count(uint32_t current_count) {
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open_from_partition("custom_nvs", "boot_data", NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_u32(handle, "boot_count", current_count + 1));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}

// GPIO中断服务函数
static void IRAM_ATTR gpio_isr_handler(void* arg) 
{
    uint32_t gpio_num = (uint32_t) arg;  // 获取入口参数
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL); // 把入口参数值发送到队列
}

// GPIO任务函数
static void gpio_task_example(void* arg)
{
    uint32_t io_num; // 定义变量 表示哪个GPIO
    for(;;) {
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {  // 死等队列消息
            printf("GPIO[%"PRIu32"] intr, val: %d\n", io_num, gpio_get_level(io_num)); // 打印相关内容
            if(icon_flag == 4) camera_capture_flag = 1;
            if(icon_flag == 0) {
                increment_boot_count(app_count);
                printf("app_count_change\n");
            }
        }
    }
}

void boot_key_init(void)
{
    gpio_config_t io0_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, // 下降沿中断
        .mode = GPIO_MODE_INPUT, // 输入模式
        .pin_bit_mask = 1<<GPIO_NUM_0, // 选择GPIO0
        .pull_down_en = 0, // 禁能内部下拉
        .pull_up_en = 1 // 使能内部上拉
    };
    // 根据上面的配置 设置GPIO
    gpio_config(&io0_conf);

    // 创建一个队列处理GPIO事件
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    // 开启GPIO任务
    xTaskCreate(gpio_task_example, "gpio_task_example", 2048, NULL, 10, NULL);
    // 创建GPIO中断服务
    gpio_install_isr_service(0);
    // 给GPIO0添加中断处理
    gpio_isr_handler_add(GPIO_NUM_0, gpio_isr_handler, (void*) GPIO_NUM_0);
}


// 主界面 任务函数
static void main_page_task(void *pvParameters)
{
    // 进入主界面
    lv_main_page();
    vTaskDelete(NULL);
}


// 主函数

void app_main(void)
{
    // Initialize NVS.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    // 初始化自定义分区
    ret = nvs_flash_init_partition("custom_nvs");
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase_partition("custom_nvs"));
        ESP_ERROR_CHECK(nvs_flash_init_partition("custom_nvs"));
    } else {
        ESP_ERROR_CHECK(ret);
    }

    app_count = read_or_init_boot_count();

    if(app_count % 2 == 0) {
        ESP_LOGI(TAG, "stable helper start");
        bsp_i2c_init();  // I2C初始化
        pca9557_init();  // IO扩展芯片初始化
        bsp_lvgl_start(); // 初始化液晶屏lvgl接口
        bsp_sdcard_mount();
        lv_fs_fatfs_init();
        bsp_codec_init(); // 音频初始化
        boot_key_init();
        xTaskCreatePinnedToCore(main_page_task, "main_page_task", 4*1024, NULL, 5, NULL, 0); // 主界面任务
    } else {
        ESP_LOGI(TAG, "USB extend screen start");
        bsp_i2c_init();  // I2C初始化
        pca9557_init();  // IO扩展芯片初始化
        app_usb_init();
        bsp_lcd_init();
        lcd_set_color(0x0000);
        app_lcd_buffer_init();
        increment_boot_count(app_count);
    }
    

    while (true) {
        displayMemoryUsage();
        vTaskDelay(pdMS_TO_TICKS(2 * 1000));
    }
}

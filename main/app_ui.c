#include "app_ui.h"
#include "audio_player.h"
#include "esp32_s3_szp.h"
#include "file_iterator.h"
#include "string.h"
#include <dirent.h>
// #include "bt/ble_hidd_demo.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_http_client.h"
#include "cJSON.h"


static const char *TAG = "app_ui";

LV_FONT_DECLARE(font_alipuhui20);

lv_obj_t * main_obj; // 主界面
lv_obj_t * main_text_label; // 主界面 欢迎语
lv_obj_t * icon_in_obj; // 应用界面
int icon_flag; // 标记现在进入哪个应用 在主界面时为0

/********************************  WiFi设置 应用程序*****************************************************************************/

// wifi事件组
static EventGroupHandle_t s_wifi_event_group = NULL;
// wifi账号队列
static QueueHandle_t xQueueWifiAccount = NULL;

// 队列要传输的内容
typedef struct {
    char wifi_ssid[32];  // 获取wifi名称
    char wifi_password[64]; // 获取wifi密码  
    char back_flag; // 是否退出       
} wifi_account_t;

lv_obj_t *wifi_scan_page;     // wifi扫描页面 obj
lv_obj_t *wifi_password_page; // wifi密码页面 obj
lv_obj_t *wifi_connect_page;  // wifi连接页面 obj
lv_obj_t *obj_scan_title;     // wifi扫描页面标题
lv_obj_t *label_wifi_scan;    // wif扫描页面 label
lv_obj_t *wifi_list;          // wifi列表  list
lv_obj_t *label_wifi_connect; // wifi连接页面label 
lv_obj_t *ta_pass_text;       // 密码输入文本框 textarea
lv_obj_t *roller_num;         // 数字roller
lv_obj_t *roller_letter_low;  // 小写字母roller
lv_obj_t *roller_letter_up;   // 大写字母roller
lv_obj_t *label_wifi_name;    // wifi名称label


// 密码roller的遮罩显示效果
static void mask_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);

    static int16_t mask_top_id = -1;
    static int16_t mask_bottom_id = -1;

    if(code == LV_EVENT_COVER_CHECK) {
        lv_event_set_cover_res(e, LV_COVER_RES_MASKED);
    }
    else if(code == LV_EVENT_DRAW_MAIN_BEGIN) {
        /* add mask */
        const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
        lv_coord_t line_space = lv_obj_get_style_text_line_space(obj, LV_PART_MAIN);
        lv_coord_t font_h = lv_font_get_line_height(font);

        lv_area_t roller_coords;
        lv_obj_get_coords(obj, &roller_coords);

        lv_area_t rect_area;
        rect_area.x1 = roller_coords.x1;
        rect_area.x2 = roller_coords.x2;
        rect_area.y1 = roller_coords.y1;
        rect_area.y2 = roller_coords.y1 + (lv_obj_get_height(obj) - font_h - line_space) / 2;

        lv_draw_mask_fade_param_t * fade_mask_top = lv_mem_buf_get(sizeof(lv_draw_mask_fade_param_t));
        lv_draw_mask_fade_init(fade_mask_top, &rect_area, LV_OPA_TRANSP, rect_area.y1, LV_OPA_COVER, rect_area.y2);
        mask_top_id = lv_draw_mask_add(fade_mask_top, NULL);

        rect_area.y1 = rect_area.y2 + font_h + line_space - 1;
        rect_area.y2 = roller_coords.y2;

        lv_draw_mask_fade_param_t * fade_mask_bottom = lv_mem_buf_get(sizeof(lv_draw_mask_fade_param_t));
        lv_draw_mask_fade_init(fade_mask_bottom, &rect_area, LV_OPA_COVER, rect_area.y1, LV_OPA_TRANSP, rect_area.y2);
        mask_bottom_id = lv_draw_mask_add(fade_mask_bottom, NULL);

    }
    else if(code == LV_EVENT_DRAW_POST_END) {
        lv_draw_mask_fade_param_t * fade_mask_top = lv_draw_mask_remove_id(mask_top_id);
        lv_draw_mask_fade_param_t * fade_mask_bottom = lv_draw_mask_remove_id(mask_bottom_id);
        lv_draw_mask_free_param(fade_mask_top);
        lv_draw_mask_free_param(fade_mask_bottom);
        lv_mem_buf_release(fade_mask_top);
        lv_mem_buf_release(fade_mask_bottom);
        mask_top_id = -1;
        mask_bottom_id = -1;
    }
}

// 数字键 处理函数
static void btn_num_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Btn-Num Clicked");
        char buf[2]; // 接收roller的值
        lv_roller_get_selected_str(roller_num, buf, sizeof(buf));
        lv_textarea_add_text(ta_pass_text, buf);
    }
}

// 小写字母确认键 处理函数
static void btn_letter_low_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Btn-Letter-Low Clicked");
        char buf[2]; // 接收roller的值
        lv_roller_get_selected_str(roller_letter_low, buf, sizeof(buf));
        lv_textarea_add_text(ta_pass_text, buf);
    }
}

// 大写字母确认键 处理函数
static void btn_letter_up_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Btn-Letter-Up Clicked");
        char buf[2]; // 接收roller的值
        lv_roller_get_selected_str(roller_letter_up, buf, sizeof(buf));
        lv_textarea_add_text(ta_pass_text, buf);
    }
}

static void lv_wifi_connect(void)
{
    lv_obj_del(wifi_password_page); // 删除密码输入界面
    // 创建一个面板对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_radius(&style, 0);  
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    wifi_connect_page = lv_obj_create(lv_scr_act());
    lv_obj_add_style(wifi_connect_page, &style, 0);

    // 绘制label提示
    label_wifi_connect = lv_label_create(wifi_connect_page);
    lv_label_set_text(label_wifi_connect, "WLAN连接中...");
    lv_obj_set_style_text_font(label_wifi_connect, &font_alipuhui20, 0);
    lv_obj_align(label_wifi_connect, LV_ALIGN_CENTER, 0, -50);
}

// WiFi连接按钮 处理函数
static void btn_connect_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "OK Clicked");
        const char *wifi_ssid = lv_label_get_text(label_wifi_name);
        const char *wifi_password = lv_textarea_get_text(ta_pass_text);
        if(*wifi_password != '\0') // 判断是否为空字符串
        {
            wifi_account_t wifi_account;
            strcpy(wifi_account.wifi_ssid, wifi_ssid);
            strcpy(wifi_account.wifi_password, wifi_password);
            ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                    wifi_account.wifi_ssid, wifi_account.wifi_password);
            wifi_account.back_flag = 0; // 正常连接
            lv_wifi_connect(); // 显示wifi连接界面
            // 发送WiFi账号密码信息到队列
            xQueueSend(xQueueWifiAccount, &wifi_account, portMAX_DELAY); 
        }
        
    }
}

// 删除密码按钮
static void btn_del_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Clicked");
        lv_textarea_del_char(ta_pass_text);
    }
}

// 返回按钮
static void btn_back_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "btn_back Clicked");
        lv_obj_del(wifi_password_page); // 删除密码输入界面
    }
}

// 进入输入密码界面
static void list_btn_cb(lv_event_t * e)
{
    // 获取点击到的WiFi名称
    const char *wifi_name=NULL;
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED) {
        wifi_name = lv_list_get_btn_text(wifi_list, obj);
        ESP_LOGI(TAG, "WLAN Name: %s", wifi_name);
    }

    // 创建密码输入页面
    wifi_password_page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(wifi_password_page, 320, 240);
    lv_obj_set_style_border_width(wifi_password_page, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(wifi_password_page, 0, 0);  // 设置间隙
    lv_obj_set_style_radius(wifi_password_page, 0, 0); // 设置圆角

    // 创建返回按钮
    lv_obj_t *btn_back = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_size(btn_back, 60, 40);
    lv_obj_set_style_border_width(btn_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_ALL, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0x000000), 0); 
    lv_obj_align(label_back, LV_ALIGN_TOP_LEFT, 10, 10);

    // 显示选中的wifi名称
    label_wifi_name = lv_label_create(wifi_password_page);
    lv_obj_set_style_text_font(label_wifi_name, &lv_font_montserrat_20, 0);
    lv_label_set_text(label_wifi_name, wifi_name);
    lv_obj_align(label_wifi_name, LV_ALIGN_TOP_MID, 0, 10);

    // 创建密码输入框
    ta_pass_text = lv_textarea_create(wifi_password_page);
    lv_obj_set_style_text_font(ta_pass_text, &lv_font_montserrat_20, 0);
    lv_textarea_set_one_line(ta_pass_text, true);  // 一行显示
    lv_textarea_set_password_mode(ta_pass_text, false); // 是否使用密码输入显示模式
    lv_textarea_set_placeholder_text(ta_pass_text, "password"); // 设置提醒词
    lv_obj_set_width(ta_pass_text, 150); // 宽度
    lv_obj_align(ta_pass_text, LV_ALIGN_TOP_LEFT, 10, 40); // 位置
    lv_obj_add_state(ta_pass_text, LV_STATE_FOCUSED); // 显示光标

    lv_textarea_set_text(ta_pass_text, "12345678");      // 设置初始文本

    // 创建“连接按钮”
    lv_obj_t *btn_connect = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_connect, LV_ALIGN_TOP_LEFT, 170, 40);
    lv_obj_set_width(btn_connect, 65); // 宽度
    lv_obj_add_event_cb(btn_connect, btn_connect_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    lv_obj_t *label_ok = lv_label_create(btn_connect);
    lv_label_set_text(label_ok, "OK");
    lv_obj_set_style_text_font(label_ok, &lv_font_montserrat_20, 0);
    lv_obj_center(label_ok);

    // 创建“删除按钮”
    lv_obj_t *btn_del = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_del, LV_ALIGN_TOP_LEFT, 245, 40);
    lv_obj_set_width(btn_del, 65); // 宽度
    lv_obj_add_event_cb(btn_del, btn_del_cb, LV_EVENT_ALL, NULL);  // 事件处理函数

    lv_obj_t *label_del = lv_label_create(btn_del);
    lv_label_set_text(label_del, LV_SYMBOL_BACKSPACE);
    lv_obj_set_style_text_font(label_del, &lv_font_montserrat_20, 0);
    lv_obj_center(label_del);

    // 创建roller样式
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_bg_color(&style, lv_color_black());
    lv_style_set_text_color(&style, lv_color_white());
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_radius(&style, 0);

    // 创建"数字"roller
    const char * opts_num = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9";

    roller_num = lv_roller_create(wifi_password_page);
    lv_obj_add_style(roller_num, &style, 0);
    lv_obj_set_style_bg_opa(roller_num, LV_OPA_50, LV_PART_SELECTED);

    lv_roller_set_options(roller_num, opts_num, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_num, 3); // 显示3行
    lv_roller_set_selected(roller_num, 5, LV_ANIM_OFF); // 默认选择
    lv_obj_set_width(roller_num, 90);
    lv_obj_set_style_text_font(roller_num, &lv_font_montserrat_20, 0);
    lv_obj_align(roller_num, LV_ALIGN_BOTTOM_LEFT, 15, -53);
    lv_obj_add_event_cb(roller_num, mask_event_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    // 创建"数字"roller 的确认键
    lv_obj_t *btn_num_ok = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_num_ok, LV_ALIGN_BOTTOM_LEFT, 15, -10); // 位置
    lv_obj_set_width(btn_num_ok, 90); // 宽度
    lv_obj_add_event_cb(btn_num_ok, btn_num_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    lv_obj_t *label_num_ok = lv_label_create(btn_num_ok);
    lv_label_set_text(label_num_ok, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(label_num_ok, &lv_font_montserrat_20, 0);
    lv_obj_center(label_num_ok);

    // 创建"小写字母"roller
    const char * opts_letter_low = "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\no\np\nq\nr\ns\nt\nu\nv\nw\nx\ny\nz";

    roller_letter_low = lv_roller_create(wifi_password_page);
    lv_obj_add_style(roller_letter_low, &style, 0);
    lv_obj_set_style_bg_opa(roller_letter_low, LV_OPA_50, LV_PART_SELECTED); // 设置选中项的透明度
    lv_roller_set_options(roller_letter_low, opts_letter_low, LV_ROLLER_MODE_INFINITE); // 循环滚动模式
    lv_roller_set_visible_row_count(roller_letter_low, 3);
    lv_roller_set_selected(roller_letter_low, 15, LV_ANIM_OFF); // 
    lv_obj_set_width(roller_letter_low, 90);
    lv_obj_set_style_text_font(roller_letter_low, &lv_font_montserrat_20, 0);
    lv_obj_align(roller_letter_low, LV_ALIGN_BOTTOM_LEFT, 115, -53);
    lv_obj_add_event_cb(roller_letter_low, mask_event_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    // 创建"小写字母"roller的确认键
    lv_obj_t *btn_letter_low_ok = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_letter_low_ok, LV_ALIGN_BOTTOM_LEFT, 115, -10);
    lv_obj_set_width(btn_letter_low_ok, 90); // 宽度
    lv_obj_add_event_cb(btn_letter_low_ok, btn_letter_low_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    lv_obj_t *label_letter_low_ok = lv_label_create(btn_letter_low_ok);
    lv_label_set_text(label_letter_low_ok, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(label_letter_low_ok, &lv_font_montserrat_20, 0);
    lv_obj_center(label_letter_low_ok);

    // 创建"大写字母"roller
    const char * opts_letter_up = "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL\nM\nN\nO\nP\nQ\nR\nS\nT\nU\nV\nW\nX\nY\nZ";

    roller_letter_up = lv_roller_create(wifi_password_page);
    lv_obj_add_style(roller_letter_up, &style, 0);
    lv_obj_set_style_bg_opa(roller_letter_up, LV_OPA_50, LV_PART_SELECTED); // 设置选中项的透明度
    lv_roller_set_options(roller_letter_up, opts_letter_up, LV_ROLLER_MODE_INFINITE); // 循环滚动模式
    lv_roller_set_visible_row_count(roller_letter_up, 3);
    lv_roller_set_selected(roller_letter_up, 15, LV_ANIM_OFF);
    lv_obj_set_width(roller_letter_up, 90);
    lv_obj_set_style_text_font(roller_letter_up, &lv_font_montserrat_20, 0);
    lv_obj_align(roller_letter_up, LV_ALIGN_BOTTOM_LEFT, 215, -53);
    lv_obj_add_event_cb(roller_letter_up, mask_event_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    // 创建"大写字母"roller的确认键
    lv_obj_t *btn_letter_up_ok = lv_btn_create(wifi_password_page);
    lv_obj_align(btn_letter_up_ok, LV_ALIGN_BOTTOM_LEFT, 215, -10);
    lv_obj_set_width(btn_letter_up_ok, 90); 
    lv_obj_add_event_cb(btn_letter_up_ok, btn_letter_up_cb, LV_EVENT_ALL, NULL); // 事件处理函数

    lv_obj_t *label_letter_up_ok = lv_label_create(btn_letter_up_ok);
    lv_label_set_text(label_letter_up_ok, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(label_letter_up_ok, &lv_font_montserrat_20, 0);
    lv_obj_center(label_letter_up_ok);

}

lv_obj_t * date_label;
lv_obj_t * time_label;

time_t now;
struct tm timeinfo;

// 更新时间函数
void value_update_cb(lv_timer_t * timer)
{
    // 更新日期 星期 时分秒
    time(&now);
    localtime_r(&now, &timeinfo);
    lv_label_set_text_fmt(time_label, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    lv_label_set_text_fmt(date_label, "%d年%02d月%02d日", timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday);
}

// 获得日期时间 任务函数
static void get_time_task(void *pvParameters)
{
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("cn.pool.ntp.org");
    esp_netif_sntp_init(&config);
    // wait for time to be set
    int retry = 0;
    // const int retry_count = 6;
    while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d)", retry++);
    }

    esp_netif_sntp_deinit();
    // 设置时区
    setenv("TZ", "CST-8", 1); 
    tzset();
    // 获取系统时间
    time(&now);
    localtime_r(&now, &timeinfo);

    lvgl_port_lock(0);
    lv_obj_del(main_text_label); // 删除主页的欢迎语 
    // 显示年月日
    date_label = lv_label_create(main_obj);
    lv_obj_set_style_text_font(date_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xffffff), 0); 
    lv_label_set_text_fmt(date_label, "%d年%02d月%02d日", timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday);
    lv_obj_align(date_label, LV_ALIGN_TOP_LEFT, 10, 5);

    // 显示时间  小时:分钟:秒钟
    time_label = lv_label_create(main_obj);
    lv_obj_set_style_text_font(time_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xffffff), 0); 
    lv_label_set_text_fmt(time_label, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    lv_obj_align_to(time_label, date_label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lvgl_port_unlock();

    xEventGroupSetBits(s_wifi_event_group, WIFI_GET_SNTP_BIT);

    lv_timer_create(value_update_cb, 1000, NULL);  // 创建一个lv_timer 每秒更新一次时间
    
    vTaskDelete(NULL);
}

// 网络连接 事件处理函数
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    static int s_retry_num = 0;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_START_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < DEFAULT_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_event_handler_instance_t instance_any_id;
esp_event_handler_instance_t instance_got_ip;
esp_netif_t *sta_netif = NULL;


// 扫描附近wifi
static void wifi_scan(wifi_ap_record_t ap_info[], uint16_t *ap_number)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    uint16_t ap_count = 0;
    
    memset(ap_info, 0, *ap_number * sizeof(wifi_ap_record_t));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_scan_start(NULL, true);

    ESP_LOGI(TAG, "Max AP number ap_info can hold = %u", *ap_number);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));  // 获取扫描到的wifi数量
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(ap_number, ap_info)); // 获取真实的获取到wifi数量和信息
    ESP_LOGI(TAG, "Total APs scanned = %u, actual AP number ap_info holds = %u", ap_count, *ap_number);
}

// 清除wifi初始化内容
static void wifiset_deinit(void)
{
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &instance_got_ip));
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        return;
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(sta_netif));
    esp_netif_destroy(sta_netif);
    sta_netif = NULL;
    ESP_ERROR_CHECK(esp_event_loop_delete_default());
}

//  任务函数
static void wifiset_tips_task(void *pvParameters)
{
    lvgl_port_lock(0);
    label_wifi_scan = lv_label_create(wifi_scan_page);
    lv_label_set_text(label_wifi_scan, "WLAN 已连接");
    lv_obj_set_style_text_color(label_wifi_scan, lv_color_hex(0x000000), 0); 
    lv_obj_set_style_text_font(label_wifi_scan, &font_alipuhui20, 0);
    lv_obj_align(label_wifi_scan, LV_ALIGN_CENTER, 0, -50);
    lvgl_port_unlock();
    vTaskDelay(500 / portTICK_PERIOD_MS); 
    lvgl_port_lock(0);
    lv_obj_del(wifi_scan_page);
    lvgl_port_unlock();

    vTaskDelete(NULL);
}

// WIFI连接任务
static void wifi_connect(void *arg)
{
    wifi_account_t wifi_account;
    while (true)
    {
        // 如果收到wifi账号队列消息
        if(xQueueReceive(xQueueWifiAccount, &wifi_account, portMAX_DELAY))
        {
            if (wifi_account.back_flag == 1){  // 退出任务标志
                break; // 跳出while循环
            }
            
            wifi_config_t wifi_config = {
                .sta = {
                    .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                    .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
                    .sae_h2e_identifier = "",
                    },
            };
            strcpy((char *)wifi_config.sta.ssid, wifi_account.wifi_ssid);
            strcpy((char *)wifi_config.sta.password, wifi_account.wifi_password);
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
            esp_wifi_connect();
            /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
            * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
            EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

            /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
            * happened. */
            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", wifi_config.sta.ssid, wifi_config.sta.password);
                lvgl_port_lock(0);
                lv_label_set_text(label_wifi_connect, "WLAN 连接成功");
                lvgl_port_unlock();
                vTaskDelay(1000 / portTICK_PERIOD_MS); // 给上面的显示一点时间
                lvgl_port_lock(0);
                lv_obj_del(wifi_connect_page); // 删除此页面
                lv_obj_del(wifi_scan_page); // 删除此页面
                lvgl_port_unlock();
                vQueueDelete(xQueueWifiAccount); // 删除队列
                icon_flag = 0; // 标记回到主界面
                xTaskCreatePinnedToCore(get_time_task, "get_time_task", 3 * 1024, NULL, 5, NULL, 0);  // 创建获取时间任务
                break; // 跳出while循环删除任务
            } else if (bits & WIFI_FAIL_BIT) {
                ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", wifi_config.sta.ssid, wifi_config.sta.password);
                lvgl_port_lock(0);
                lv_label_set_text(label_wifi_connect, "WLAN 连接失败");
                lvgl_port_unlock();
                vTaskDelay(1000 / portTICK_PERIOD_MS); // 给上面的显示一点时间
                lvgl_port_lock(0);
                lv_obj_del(wifi_connect_page); // 删除此页面
                lvgl_port_unlock();
                xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT); // 清除此事件标志位

            } else {
                ESP_LOGE(TAG, "UNEXPECTED EVENT");
                lvgl_port_lock(0);
                lv_label_set_text(label_wifi_connect, "WLAN 连接异常");
                lvgl_port_unlock();
                vTaskDelay(1000 / portTICK_PERIOD_MS); // 给上面的显示一点时间
                lvgl_port_lock(0);
                lv_obj_del(wifi_connect_page); // 删除此页面
                lv_obj_del(wifi_scan_page); // 删除此页面
                lvgl_port_unlock();
                wifiset_deinit(); // 清除wifi初始化
            }
        }
    }
    vTaskDelete(NULL);
}

// 返回主界面按钮事件处理函数
static void btn_backmain_cb(lv_event_t * e)
{
    ESP_LOGI(TAG, "btn_backmain Clicked");

    // // 删除wifi扫描界面
    lvgl_port_lock(0);
    lv_obj_del(wifi_scan_page); 
    lvgl_port_unlock();

    // 通知wifi_connect任务退出
    wifi_account_t wifi_account;
    wifi_account.back_flag = 1; 
    xQueueSend(xQueueWifiAccount, &wifi_account, portMAX_DELAY); 
    
    wifiset_deinit();// 清除wifi扫描痕迹
    vQueueDelete(xQueueWifiAccount); // 删除队列
    icon_flag = 0;
}



// wifi连接
void app_wifi_connect(void *arg)
{
    vTaskDelay(200 / portTICK_PERIOD_MS);
    // 扫描WLAN信息
    wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];  // 记录扫描到的wifi信息
    uint16_t ap_number = DEFAULT_SCAN_LIST_SIZE; 
    wifi_scan(ap_info, &ap_number); // 扫描附近wifi

    lvgl_port_lock(0);
    // 修改标题
    lv_label_set_text_fmt(label_wifi_scan, "%d WLAN", ap_number);

    // 创建返回按钮
    lv_obj_t *btn_back = lv_btn_create(obj_scan_title);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(btn_back, 60, 30);
    lv_obj_set_style_border_width(btn_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_back, btn_backmain_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    // 创建wifi信息列表
    wifi_list = lv_list_create(wifi_scan_page);
    lv_obj_set_size(wifi_list, 320, 200);
    lv_obj_align(wifi_list, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_obj_set_style_border_width(wifi_list, 0, 0);
    lv_obj_set_style_text_font(wifi_list, &font_alipuhui20, 0);
    lv_obj_set_scrollbar_mode(wifi_list, LV_SCROLLBAR_MODE_OFF); // 隐藏wifi_list滚动条
    // 显示wifi信息
    lv_obj_t * btn;
    for (int i = 0; i < ap_number; i++) {
        ESP_LOGI(TAG, "SSID \t\t%s", ap_info[i].ssid);  // 终端输出wifi名称
        ESP_LOGI(TAG, "RSSI \t\t%d", ap_info[i].rssi);  // 终端输出wifi信号质量
        // 添加wifi列表
        btn = lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, (const char *)ap_info[i].ssid); 
        lv_obj_add_event_cb(btn, list_btn_cb, LV_EVENT_CLICKED, NULL); // 添加点击回调函数
    }
    lvgl_port_unlock();
    
    // 创建wifi连接任务
    xQueueWifiAccount = xQueueCreate(2, sizeof(wifi_account_t));
    xTaskCreatePinnedToCore(wifi_connect, "wifi_connect", 4 * 1024, NULL, 5, NULL, 1);  // 创建wifi连接任务
    vTaskDelete(NULL);
}


// 进入WIFI设置应用
static void wifiset_event_handler(lv_event_t * e)
{  
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xFFFFFF));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    wifi_scan_page = lv_obj_create(lv_scr_act());
    lv_obj_add_style(wifi_scan_page, &style, 0);

    // 判断wifi是否已连接
    int isconnect_flag = 1;
    if (s_wifi_event_group != NULL) // 如果创建了此事件组
    {
        // 查看是否已经连接wifi
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, 10);
        if (bits & WIFI_CONNECTED_BIT) { // 如果已经连接到wifi
            xTaskCreatePinnedToCore(wifiset_tips_task, "wifiset_tips_task", 2048, NULL, 5, NULL, 0);
        }
        else{
            isconnect_flag = 0;
        } 
    }
    if (s_wifi_event_group == NULL || isconnect_flag == 0) // 没有连接到wifi
    {
    // 创建标题背景
        obj_scan_title = lv_obj_create(wifi_scan_page);
        lv_obj_set_size(obj_scan_title, 320, 40);
        lv_obj_set_style_pad_all(obj_scan_title, 0, 0);  // 设置间隙
        lv_obj_align(obj_scan_title, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_color(obj_scan_title, lv_color_hex(0x008b8b), 0);

        // 显示扫描情况
        label_wifi_scan = lv_label_create(obj_scan_title);
        lv_label_set_text(label_wifi_scan, "WLAN扫描中...");
        lv_obj_set_style_text_color(label_wifi_scan, lv_color_hex(0xffffff), 0); 
        lv_obj_set_style_text_font(label_wifi_scan, &font_alipuhui20, 0);
        lv_obj_align(label_wifi_scan, LV_ALIGN_CENTER, 0, 0);

        icon_flag = 5; // 标记已经进入第5个应用

        xTaskCreatePinnedToCore(app_wifi_connect, "app_wifi_connect", 4*1024, NULL, 5, NULL, 0);
    }
}


/******************************** 第1个图标 天气时钟 应用程序*************************************************************************************/
lv_obj_t * weather_title_label; // 标题栏文字

lv_timer_t *  weather_lv_timer =  NULL;

lv_obj_t *btn_weather_back; // att姿态应用 后退按钮

// 定义全局变量，用于保存UI控件指针
lv_obj_t *weather_img;  // 天气图标
lv_obj_t *desc_label;   // 天气描述
lv_obj_t *city_label;   // 地区名称
lv_obj_t *wea_time_label;
lv_obj_t *wea_date_label;
lv_obj_t *temp_label;   // 温度
lv_obj_t *updata_label;   // 更新时间

time_t weatime_now;
struct tm weatime_info;


static SemaphoreHandle_t weather_del_semaphore = NULL;


// json_str是已经读取的JSON字符串
void update_weather_info(const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        printf("JSON parse error!\n");
        return;
    }
    // 获取results数组
    cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");
    if (!cJSON_IsArray(results) || cJSON_GetArraySize(results) == 0) {
        printf("Invalid results format\n");
        cJSON_Delete(root);
        return;
    }

    // 获取第一个结果项
    cJSON *result_item = cJSON_GetArrayItem(results, 0);
    if (!result_item) {
        cJSON_Delete(root);
        return;
    }

    // 解析location
    cJSON *location = cJSON_GetObjectItemCaseSensitive(result_item, "location");
    const char *city_name = cJSON_IsString(cJSON_GetObjectItem(location, "name")) ? 
                          cJSON_GetStringValue(cJSON_GetObjectItem(location, "name")) : "N/A";

    // 解析now对象
    cJSON *now = cJSON_GetObjectItemCaseSensitive(result_item, "now");
    const char *weather_text = cJSON_IsString(cJSON_GetObjectItem(now, "text")) ? 
                             cJSON_GetStringValue(cJSON_GetObjectItem(now, "text")) : "N/A";
    const char *weather_code = cJSON_IsString(cJSON_GetObjectItem(now, "code")) ? 
                              cJSON_GetStringValue(cJSON_GetObjectItem(now, "code")) : "99";
    const char *temperature = cJSON_IsString(cJSON_GetObjectItem(now, "temperature")) ? 
                             cJSON_GetStringValue(cJSON_GetObjectItem(now, "temperature")) : "N/A";

    // 解析更新时间
    const char *update_time = cJSON_IsString(cJSON_GetObjectItem(result_item, "last_update")) ? 
                             cJSON_GetStringValue(cJSON_GetObjectItem(result_item, "last_update")) : "N/A";


    // 更新天气图标（注意缓冲区大小）
    char img_path[32];
    snprintf(img_path, sizeof(img_path), "S:weather/%s.png", weather_code);
    // 更新LVGL组件
    lvgl_port_lock(0);
    lv_label_set_text(city_label, city_name);
    lv_label_set_text(desc_label, weather_text);
    lv_label_set_text(temp_label, temperature);
    lv_label_set_text(updata_label, update_time);
    lv_img_set_src(weather_img, img_path);
    lvgl_port_unlock();

    // 清理cJSON内存
    cJSON_Delete(root);
}

static void http_weather_task(void *pvParameters)
{
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   //用于接收通过http协议返回的数据
    int content_length = 0;  //http协议头的长度
   
   //定义http配置结构体，并且进行清零
    esp_http_client_config_t config = {
        .url = "https://api.seniverse.com/v3/weather/now.json?key=SDfBtD4qTN3yNKL8b&location=ip&language=zh-Hans&unit=c",
        .cert_pem = NULL,  // 无证书时需跳过验证
        .skip_cert_common_name_check = true,  // 跳过域名检查
        .transport_type = HTTP_TRANSPORT_OVER_SSL, // 显式启用 SSL
    };
    //初始化结构体
    esp_http_client_handle_t client = esp_http_client_init(&config);	//初始化http连接
    //设置发送请求 
    esp_http_client_set_method(client, HTTP_METHOD_GET);

    while(1)
    {
        // 与目标主机创建连接，并且声明写入内容长度为0
        esp_err_t err = esp_http_client_open(client, 0);
        //如果连接失败
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            vTaskDelete(NULL);
        } 
        //如果连接成功
        else {
            //读取目标主机的返回内容的协议头
            content_length = esp_http_client_fetch_headers(client);
            //如果协议头长度小于0，说明没有成功读取到
            if (content_length < 0) {
                ESP_LOGE(TAG, "HTTP client fetch headers failed");
            } //如果成功读取到了协议头
            else {
                //读取目标主机通过http的响应内容
                int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
                if (data_read >= 0) {
                    //打印响应内容，包括响应状态，响应体长度及其内容
                    ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld",
                    esp_http_client_get_status_code(client),				//获取响应状态信息
                    esp_http_client_get_content_length(client));			//获取响应信息长度
                    printf("data:%s\n", output_buffer);
                    update_weather_info(output_buffer);
                } 
                //如果不成功
                else {
                    ESP_LOGE(TAG, "Failed to read response");
                }
            }
        }
        //关闭连接
        esp_http_client_close(client);

        if (xSemaphoreTake(weather_del_semaphore, 10000 / portTICK_PERIOD_MS) == pdTRUE) {
            break; // 收到删除信号，退出循环
        }
    }
    // 清理资源
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "esp_http_client_cleanup ok");
    vTaskDelete(NULL);

}

// 返回主界面按钮事件处理函数
static void btn_weather_back_cb(lv_event_t * e)
{
    if(weather_lv_timer)
    {
        lv_timer_del(weather_lv_timer);
        weather_lv_timer = NULL;
    }
        // 发送删除任务信号量
    if (weather_del_semaphore != NULL) {
        xSemaphoreGive(weather_del_semaphore);
    }
    lv_obj_del(icon_in_obj); // 删除画布
    icon_flag = 0;
}

//根据
void weather_update_time(lv_timer_t * timer)
{
    // 更新日期 星期 时分秒
    time(&weatime_now);
    localtime_r(&weatime_now, &weatime_info);
    lv_label_set_text_fmt(wea_time_label, "%02d:%02d", weatime_info.tm_hour, weatime_info.tm_min);
    lv_label_set_text_fmt(wea_date_label, "%d/%02d/%02d", weatime_info.tm_year+1900, weatime_info.tm_mon+1, weatime_info.tm_mday);
    ESP_LOGI(TAG, "weather time updated!!");
}

// 天气时钟处理任务
static void task_process_weather(void *arg)
{
    if (weather_del_semaphore == NULL) {
        weather_del_semaphore = xSemaphoreCreateBinary();//后面考虑改为事件组管理，待定
        xSemaphoreTake(weather_del_semaphore, 1000 / portTICK_PERIOD_MS);
    }

    // 判断wifi是否已连接
    if (s_wifi_event_group != NULL) // 如果创建了此事件组
    {
        ESP_LOGI(TAG, "weather wifi created!!");
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, 10);
        if (bits & WIFI_CONNECTED_BIT) { // 如果已经连接到wifi
            ESP_LOGI(TAG, "weather wifi connected!!");

            xTaskCreatePinnedToCore(http_weather_task, "http_weather_task", 8 * 1024, NULL, 5, NULL, 0);
            // 创建一个lv_timer 用于更新信息
            time(&weatime_now);
            localtime_r(&weatime_now, &weatime_info);
            lv_label_set_text_fmt(wea_time_label, "%02d:%02d", weatime_info.tm_hour, weatime_info.tm_min);
            lv_label_set_text_fmt(wea_date_label, "%d/%02d/%02d", weatime_info.tm_year+1900, weatime_info.tm_mon+1, weatime_info.tm_mday);
            weather_lv_timer = lv_timer_create(weather_update_time, 1000*30, NULL); 
        }
        else{
            ESP_LOGE(TAG, "weather wifi  not connected!! return ");
            lv_obj_del(icon_in_obj); // 删除画布
            icon_flag = 0;
        } 
    }
    else {
        ESP_LOGE(TAG, "weather wifi  not created!! return ");
    }

    vTaskDelete(NULL);
}

static void weather_event_handler(lv_event_t * e)
{
    // 创建一个界面
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xffffff));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    // 创建标题背景
    lv_obj_t *weather_title_back = lv_obj_create(icon_in_obj);
    lv_obj_set_size(weather_title_back, 320, 40);
    lv_obj_set_style_pad_all(weather_title_back, 0, 0);  // 设置间隙
    lv_obj_align(weather_title_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(weather_title_back, lv_color_hex(0x30a830), 0);
    // 显示标题
    weather_title_label = lv_label_create(weather_title_back);
    lv_label_set_text(weather_title_label, "天气时钟");
    lv_obj_set_style_text_color(weather_title_label, lv_color_hex(0xffffff), 0); 
    lv_obj_set_style_text_font(weather_title_label, &font_alipuhui20, 0);
    lv_obj_align(weather_title_label, LV_ALIGN_CENTER, 0, 0);
    // 创建后退按钮
    btn_weather_back = lv_btn_create(weather_title_back);
    lv_obj_align(btn_weather_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(btn_weather_back, 60, 30);
    lv_obj_set_style_border_width(btn_weather_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_weather_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_weather_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_weather_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_weather_back, btn_weather_back_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_weather_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    //各种控件******************************
    // 创建一个样式并移除边框
    static lv_style_t style_no_border;
    lv_style_init(&style_no_border);
    lv_style_set_border_width(&style_no_border, 0);  // 设置边框宽度为 0

    // 天气图标
    lv_obj_t *weather_img_back = lv_obj_create(icon_in_obj);  
    lv_obj_set_size(weather_img_back, 110, 110);
    lv_obj_align(weather_img_back, LV_ALIGN_DEFAULT, 30, 50);
    lv_obj_clear_flag(weather_img_back, LV_OBJ_FLAG_SCROLLABLE);
    // lv_obj_add_style(weather_img_back, &style_no_border, LV_PART_MAIN);

    weather_img = lv_img_create(weather_img_back);
    lv_img_set_src(weather_img, "S:weather/99.png");  // 默认图标
    // lv_img_set_src(weather_img, &test);
    lv_obj_align(weather_img, LV_ALIGN_CENTER, 0, 0);

    // 天气描述
    lv_obj_t *weather_label_back = lv_obj_create(icon_in_obj);  
    lv_obj_set_size(weather_label_back, 110, 35);
    lv_obj_align(weather_label_back, LV_ALIGN_DEFAULT, 30, 160);
    lv_obj_clear_flag(weather_label_back, LV_OBJ_FLAG_SCROLLABLE);
    // lv_obj_add_style(weather_label_back, &style_no_border, LV_PART_MAIN);

    desc_label = lv_label_create(weather_label_back);
    lv_label_set_text(desc_label, "N/A");  // 默认描述
    lv_obj_set_style_text_font(desc_label, &font_alipuhui20, 0);
    lv_obj_align(desc_label, LV_ALIGN_CENTER, 0, 0);

    //时间显示
    lv_obj_t *time__back = lv_obj_create(icon_in_obj);  
    lv_obj_set_size(time__back, 140, 60);
    lv_obj_align(time__back, LV_ALIGN_DEFAULT, 160, 60);
    lv_obj_clear_flag(time__back, LV_OBJ_FLAG_SCROLLABLE);
    // lv_obj_add_style(time__back, &style_no_border, LV_PART_MAIN);

    wea_time_label = lv_label_create(time__back);
    lv_label_set_text(wea_time_label, "00:00");  // 
    lv_obj_set_style_text_font(wea_time_label, &lv_font_montserrat_48, 0);
    lv_obj_align(wea_time_label, LV_ALIGN_CENTER, 0, 0);

    //日期显示
    lv_obj_t *date_back = lv_obj_create(icon_in_obj);  
    lv_obj_set_size(date_back, 140, 40);
    lv_obj_align(date_back, LV_ALIGN_DEFAULT, 160, 120);
    lv_obj_clear_flag(date_back, LV_OBJ_FLAG_SCROLLABLE);
    // lv_obj_add_style(date_back, &style_no_border, LV_PART_MAIN);

    wea_date_label = lv_label_create(date_back);
    lv_label_set_text(wea_date_label, "2025/1/1");  // 
    lv_obj_set_style_text_font(wea_date_label, &font_alipuhui20, 0);
    lv_obj_align(wea_date_label, LV_ALIGN_CENTER, 0, 0);

    //地区显示
    lv_obj_t *city_back = lv_obj_create(icon_in_obj);  
    lv_obj_set_size(city_back, 70, 35);
    lv_obj_align(city_back, LV_ALIGN_DEFAULT, 160, 160);
    lv_obj_clear_flag(city_back, LV_OBJ_FLAG_SCROLLABLE);
    // lv_obj_add_style(city_back, &style_no_border, LV_PART_MAIN);

    city_label = lv_label_create(city_back);
    lv_label_set_text(city_label, "N/A");  // 默认地区
    lv_obj_set_style_text_font(city_label, &font_alipuhui20, 0);
    lv_obj_align(city_label, LV_ALIGN_RIGHT_MID, 0, 0);

    //温度显示    
    lv_obj_t *temp_back = lv_obj_create(icon_in_obj);  
    lv_obj_set_size(temp_back, 70, 35);
    lv_obj_align(temp_back, LV_ALIGN_DEFAULT, 230, 160);
    lv_obj_clear_flag(temp_back, LV_OBJ_FLAG_SCROLLABLE);
    // lv_obj_add_style(temp_back, &style_no_border, LV_PART_MAIN);

    temp_label = lv_label_create(temp_back);
    lv_label_set_text(temp_label, "N/A");  
    lv_obj_set_style_text_font(temp_label, &font_alipuhui20, 0);
    lv_obj_align(temp_label, LV_ALIGN_RIGHT_MID, -20, 0);

    lv_obj_t * temp_symbol = lv_label_create(temp_back);
    lv_label_set_text(temp_symbol, "°C");  
    lv_obj_set_style_text_font(temp_symbol, &font_alipuhui20, 0);
    lv_obj_align(temp_symbol, LV_ALIGN_RIGHT_MID, 5, 0);

    //更新时间戳显示
    lv_obj_t *updata_back = lv_obj_create(icon_in_obj);
    lv_obj_set_size(updata_back, 320, 30);
    lv_obj_set_style_pad_all(updata_back, 0, 0);  // 设置间隙
    lv_obj_align(updata_back, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_clear_flag(updata_back, LV_OBJ_FLAG_SCROLLABLE);
    // lv_obj_add_style(updata_back, &style_no_border, LV_PART_MAIN);

    updata_label = lv_label_create(updata_back);
    lv_label_set_text(updata_label, "2025-03-11-T13:50:17+08:00");  // 默认时间
    lv_obj_set_style_text_font(updata_label, &font_alipuhui20, 0);
    lv_obj_align(updata_label, LV_ALIGN_CENTER, 0, 0);


    icon_flag = 1; // 标记已经进入第一个应用
    xTaskCreatePinnedToCore(task_process_weather, "task_process_weather", 2 * 1024, NULL, 5, NULL, 1);
}


/*********************  第2个图标   音乐播放器 *********************************************************************************************/
static audio_player_config_t player_config = {0};
static uint8_t g_sys_volume = VOLUME_DEFAULT;
static file_iterator_instance_t *file_iterator = NULL;

lv_obj_t *music_list;
lv_obj_t *label_play_pause;
lv_obj_t *btn_play_pause;
lv_obj_t *volume_slider;

lv_obj_t *music_title_label;
lv_obj_t *btn_music_back;

// 播放指定序号的音乐
static void play_index(int index)
{
    ESP_LOGI(TAG, "play_index(%d)", index);

    char filename[128];
    int retval = file_iterator_get_full_path_from_index(file_iterator, index, filename, sizeof(filename));
    if (retval == 0) {
        ESP_LOGE(TAG, "unable to retrieve filename");
        return;
    }

    FILE *fp = fopen(filename, "rb");
    if (fp) {
        ESP_LOGI(TAG, "Playing '%s'", filename);
        audio_player_play(fp);
    } else {
        ESP_LOGE(TAG, "unable to open index %d, filename '%s'", index, filename);
    }
}

// 设置声音处理函数
static esp_err_t _audio_player_mute_fn(AUDIO_PLAYER_MUTE_SETTING setting)
{
    esp_err_t ret = ESP_OK;
    // 判断是否需要静音
    bsp_codec_mute_set(setting == AUDIO_PLAYER_MUTE ? true : false);
    // 如果不是静音 设置音量
    if (setting == AUDIO_PLAYER_UNMUTE) {
        bsp_codec_volume_set(g_sys_volume, NULL);
    }
    ret = ESP_OK;

    return ret;
}

// 播放音乐函数 播放音乐的时候 会不断进入
static esp_err_t _audio_player_write_fn(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;

    ret = bsp_i2s_write(audio_buffer, len, bytes_written, timeout_ms);

    return ret;
}

// 设置采样率 播放的时候进入一次
static esp_err_t _audio_player_std_clock(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    esp_err_t ret = ESP_OK;

    // ret = bsp_codec_set_fs(rate, bits_cfg, ch); // 如果播放的音乐固定是16000采样率 这里可以不用打开 如果采样率未知 把这里打开
    return ret;
}

// 回调函数 播放器每次动作都会进入
static void _audio_player_callback(audio_player_cb_ctx_t *ctx)
{
    ESP_LOGI(TAG, "ctx->audio_event = %d", ctx->audio_event);
    switch (ctx->audio_event) {
    case AUDIO_PLAYER_CALLBACK_EVENT_IDLE: {  // 播放完一首歌 进入这个case
        ESP_LOGI(TAG, "AUDIO_PLAYER_REQUEST_IDLE");
        // 指向下一首歌
        file_iterator_next(file_iterator);
        int index = file_iterator_get_index(file_iterator);
        ESP_LOGI(TAG, "playing index '%d'", index);
        play_index(index);
        // 修改当前播放的音乐名称
        lvgl_port_lock(0);
        lv_dropdown_set_selected(music_list, index);
        lvgl_port_unlock();
        break;
    }
    case AUDIO_PLAYER_CALLBACK_EVENT_PLAYING: // 正在播放音乐
        ESP_LOGI(TAG, "AUDIO_PLAYER_REQUEST_PLAY");
        pa_en(1); // 打开音频功放
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PAUSE: // 正在暂停音乐
        ESP_LOGI(TAG, "AUDIO_PLAYER_REQUEST_PAUSE");
        pa_en(0); // 关闭音频功放
        break;
    default:
        break;
    }
}

// mp3播放器初始化
void mp3_player_init(void)
{
    // 获取文件信息
    file_iterator = file_iterator_new(SD_MOUNT_MUSIC_POINT);
    assert(file_iterator != NULL);

    // 初始化音频播放
    player_config.mute_fn = _audio_player_mute_fn;
    player_config.write_fn = _audio_player_write_fn;
    player_config.clk_set_fn = _audio_player_std_clock;
    player_config.priority = 6;
    player_config.coreID = 1;

    ESP_ERROR_CHECK(audio_player_new(player_config));
    ESP_ERROR_CHECK(audio_player_callback_register(_audio_player_callback, NULL));
}


// 按钮样式相关定义
typedef struct {
    lv_style_t style_bg;
    lv_style_t style_focus_no_outline;
} button_style_t;

static button_style_t g_btn_styles;

button_style_t *ui_button_styles(void)
{
    return &g_btn_styles;
}

// 按钮样式初始化
static void ui_button_style_init(void)
{
    /*Init the style for the default state*/
    lv_style_init(&g_btn_styles.style_focus_no_outline);
    lv_style_set_outline_width(&g_btn_styles.style_focus_no_outline, 0);

    lv_style_init(&g_btn_styles.style_bg);
    lv_style_set_bg_opa(&g_btn_styles.style_bg, LV_OPA_100);
    lv_style_set_bg_color(&g_btn_styles.style_bg, lv_color_make(255, 255, 255));
    lv_style_set_shadow_width(&g_btn_styles.style_bg, 0);
}

// 播放暂停按钮 事件处理函数
static void btn_play_pause_cb(lv_event_t *event)
{
    lv_obj_t *btn = lv_event_get_target(event);
    lv_obj_t *lab = (lv_obj_t *) btn->user_data;

    audio_player_state_t state = audio_player_get_state();
    printf("state=%d\n", state);
    if(state == AUDIO_PLAYER_STATE_IDLE){
        lvgl_port_lock(0);
        lv_label_set_text_static(lab, LV_SYMBOL_PAUSE);
        lvgl_port_unlock();
        int index = file_iterator_get_index(file_iterator);
        ESP_LOGI(TAG, "playing index '%d'", index);
        play_index(index);
    }else if (state == AUDIO_PLAYER_STATE_PAUSE) {
        lvgl_port_lock(0);
        lv_label_set_text_static(lab, LV_SYMBOL_PAUSE);
        lvgl_port_unlock();
        audio_player_resume();
    } else if (state == AUDIO_PLAYER_STATE_PLAYING) {
        lvgl_port_lock(0);
        lv_label_set_text_static(lab, LV_SYMBOL_PLAY);
        lvgl_port_unlock();
        audio_player_pause();
    }
}

// 上一首 下一首 按键事件处理函数
static void btn_prev_next_cb(lv_event_t *event)
{
    bool is_next = (bool) event->user_data;

    if (is_next) {
        ESP_LOGI(TAG, "btn next");
        file_iterator_next(file_iterator);
    } else {
        ESP_LOGI(TAG, "btn prev");
        file_iterator_prev(file_iterator);
    }
    // 修改当前的音乐名称
    int index = file_iterator_get_index(file_iterator);
    lvgl_port_lock(0);
    lv_dropdown_set_selected(music_list, index);
    // lv_obj_t *label_title = (lv_obj_t *) music_list->user_data;
    // lv_label_set_text_static(label_title, file_iterator_get_name_from_index(file_iterator, index));
    lvgl_port_unlock();
    // 执行音乐事件
    audio_player_state_t state = audio_player_get_state();
    printf("prev_next_state=%d\n", state);
    if (state == AUDIO_PLAYER_STATE_IDLE) { 
        // Nothing to do
    }else if (state == AUDIO_PLAYER_STATE_PAUSE){ // 如果当前正在暂停歌曲
        ESP_LOGI(TAG, "playing index '%d'", index);
        play_index(index);
        audio_player_pause();
    } else if (state == AUDIO_PLAYER_STATE_PLAYING) { // 如果当前正在播放歌曲
        // 播放歌曲
        ESP_LOGI(TAG, "playing index '%d'", index);
        play_index(index);
    }
}

// 音量调节滑动条 事件处理函数
static void volume_slider_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    int volume = lv_slider_get_value(slider); // 获取slider的值
    bsp_codec_volume_set(volume, NULL); // 设置声音大小
    g_sys_volume = volume; // 把声音赋值给g_sys_volume保存
    ESP_LOGI(TAG, "volume '%d'", volume);
}

// 音乐列表 点击事件处理函数
static void music_list_cb(lv_event_t *event)
{   
    uint16_t index = lv_dropdown_get_selected(music_list);
    ESP_LOGI(TAG, "switching index to '%d'", index);
    file_iterator_set_index(file_iterator, index);
    
    audio_player_state_t state = audio_player_get_state();
    if (state == AUDIO_PLAYER_STATE_PAUSE){ // 如果当前正在暂停歌曲
        play_index(index);
        audio_player_pause();
    } else if (state == AUDIO_PLAYER_STATE_PLAYING) { // 如果当前正在播放歌曲
        play_index(index);
    }
}

// 音乐名称加入列表
static void build_file_list(lv_obj_t *music_list)
{
    lvgl_port_lock(0);
    lv_dropdown_clear_options(music_list);
    lvgl_port_unlock();

    for(size_t i = 0; i<file_iterator->count; i++)
    {
        const char *file_name = file_iterator_get_name_from_index(file_iterator, i);
        if (NULL != file_name) {
            lvgl_port_lock(0);
            lv_dropdown_add_option(music_list, file_name, i); // 添加音乐名称到列表中
            lvgl_port_unlock();
        }
    }
    lvgl_port_lock(0);
    lv_dropdown_set_selected(music_list, 0); // 选择列表中的第一个
    lvgl_port_unlock();
}

// 播放器界面初始化
void music_ui(void)
{
    lvgl_port_lock(0);

    ui_button_style_init();// 初始化按键风格

    /* 创建播放暂停控制按键 */
    btn_play_pause = lv_btn_create(icon_in_obj);
    lv_obj_align(btn_play_pause, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_size(btn_play_pause, 50, 50);
    lv_obj_set_style_radius(btn_play_pause, 25, LV_STATE_DEFAULT);
    lv_obj_add_flag(btn_play_pause, LV_OBJ_FLAG_CHECKABLE);

    lv_obj_add_style(btn_play_pause, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_play_pause, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUSED);

    label_play_pause = lv_label_create(btn_play_pause);

    lv_label_set_text_static(label_play_pause, LV_SYMBOL_PLAY);
    lv_obj_center(label_play_pause);

    lv_obj_set_user_data(btn_play_pause, (void *) label_play_pause);
    lv_obj_add_event_cb(btn_play_pause, btn_play_pause_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 创建上一首控制按键 */
    lv_obj_t *btn_play_prev = lv_btn_create(icon_in_obj);
    lv_obj_set_size(btn_play_prev, 50, 50);
    lv_obj_set_style_radius(btn_play_prev, 25, LV_STATE_DEFAULT);
    lv_obj_clear_flag(btn_play_prev, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_align_to(btn_play_prev, btn_play_pause, LV_ALIGN_OUT_LEFT_MID, -40, 0); 

    lv_obj_add_style(btn_play_prev, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_play_prev, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUSED);
    lv_obj_add_style(btn_play_prev, &ui_button_styles()->style_bg, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_play_prev, &ui_button_styles()->style_bg, LV_STATE_FOCUSED);
    lv_obj_add_style(btn_play_prev, &ui_button_styles()->style_bg, LV_STATE_DEFAULT);

    lv_obj_t *label_prev = lv_label_create(btn_play_prev);
    lv_label_set_text_static(label_prev, LV_SYMBOL_PREV);
    lv_obj_set_style_text_font(label_prev, &lv_font_montserrat_24, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_prev, lv_color_make(0, 0, 0), LV_STATE_DEFAULT);
    lv_obj_center(label_prev);
    lv_obj_set_user_data(btn_play_prev, (void *) label_prev);
    lv_obj_add_event_cb(btn_play_prev, btn_prev_next_cb, LV_EVENT_CLICKED, (void *) false);

    /* 创建下一首控制按键 */
    lv_obj_t *btn_play_next = lv_btn_create(icon_in_obj);
    lv_obj_set_size(btn_play_next, 50, 50);
    lv_obj_set_style_radius(btn_play_next, 25, LV_STATE_DEFAULT);
    lv_obj_clear_flag(btn_play_next, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_align_to(btn_play_next, btn_play_pause, LV_ALIGN_OUT_RIGHT_MID, 40, 0);

    lv_obj_add_style(btn_play_next, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_play_next, &ui_button_styles()->style_focus_no_outline, LV_STATE_FOCUSED);
    lv_obj_add_style(btn_play_next, &ui_button_styles()->style_bg, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn_play_next, &ui_button_styles()->style_bg, LV_STATE_FOCUSED);
    lv_obj_add_style(btn_play_next, &ui_button_styles()->style_bg, LV_STATE_DEFAULT);

    lv_obj_t *label_next = lv_label_create(btn_play_next);
    lv_label_set_text_static(label_next, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_font(label_next, &lv_font_montserrat_24, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_next, lv_color_make(0, 0, 0), LV_STATE_DEFAULT);
    lv_obj_center(label_next);
    lv_obj_set_user_data(btn_play_next, (void *) label_next);
    lv_obj_add_event_cb(btn_play_next, btn_prev_next_cb, LV_EVENT_CLICKED, (void *) true);

    /* 创建声音调节滑动条 */
    volume_slider = lv_slider_create(icon_in_obj);
    lv_obj_set_size(volume_slider, 200, 10);
    lv_obj_set_ext_click_area(volume_slider, 15);
    lv_obj_align(volume_slider, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_slider_set_range(volume_slider, 0, 100);
    lv_slider_set_value(volume_slider, g_sys_volume, LV_ANIM_ON);
    lv_obj_add_event_cb(volume_slider, volume_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lab_vol_min = lv_label_create(icon_in_obj);
    lv_label_set_text_static(lab_vol_min, LV_SYMBOL_VOLUME_MID);
    lv_obj_set_style_text_font(lab_vol_min, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align_to(lab_vol_min, volume_slider, LV_ALIGN_OUT_LEFT_MID, -10, 0);

    lv_obj_t *lab_vol_max = lv_label_create(icon_in_obj);
    lv_label_set_text_static(lab_vol_max, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(lab_vol_max, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align_to(lab_vol_max, volume_slider, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    /* 创建音乐列表 */ 
    music_list = lv_dropdown_create(icon_in_obj);
    lv_dropdown_clear_options(music_list);
    lv_dropdown_set_options_static(music_list, "扫描中...");
    lv_obj_set_style_text_font(music_list, &font_alipuhui20, LV_STATE_ANY);
    lv_obj_set_width(music_list, 200);
    lv_obj_align(music_list, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_event_cb(music_list, music_list_cb, LV_EVENT_VALUE_CHANGED, NULL);

    build_file_list(music_list);

    lvgl_port_unlock();
}

// 返回主界面按钮事件处理函数
static void btn_music_back_cb(lv_event_t * e)
{
    lv_obj_del(icon_in_obj); 
    audio_player_delete();
    icon_flag = 0;
}

// 进入音乐播放应用
static void music_event_handler(lv_event_t * e)
{
    // 初始化mp3播放器
    mp3_player_init();
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xffffff));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    // 创建标题背景
    lv_obj_t *music_title = lv_obj_create(icon_in_obj);
    lv_obj_set_size(music_title, 320, 40);
    lv_obj_set_style_pad_all(music_title, 0, 0);  // 设置间隙
    lv_obj_align(music_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(music_title, lv_color_hex(0xf87c30), 0);
    // 显示标题
    music_title_label = lv_label_create(music_title);
    lv_label_set_text(music_title_label, "音乐播放器");
    lv_obj_set_style_text_color(music_title_label, lv_color_hex(0xffffff), 0); 
    lv_obj_set_style_text_font(music_title_label, &font_alipuhui20, 0);
    lv_obj_align(music_title_label, LV_ALIGN_CENTER, 0, 0);
    // 创建后退按钮
    btn_music_back = lv_btn_create(music_title);
    lv_obj_align(btn_music_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(btn_music_back, 60, 30);
    lv_obj_set_style_border_width(btn_music_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_music_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_music_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_music_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_music_back, btn_music_back_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_music_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    icon_flag = 2; // 标记已经进入第二个应用

    music_ui(); // 音乐播放器界面
}


/******************************** 第3个图标 SD卡 应用程序***************************************************************************/
lv_obj_t * sdcard_title; // SD卡页面标题背景
lv_obj_t * sdcard_label; // SD卡页面标题
lv_obj_t * sdcard_file_list; // SD卡文件列表


extern sdmmc_card_t *sdmmc_card;

struct file_path_info
{
    uint8_t path_index;  // 在第几级目录
    char path_now[256]; // 当前文件路径
    char path_back[256]; // 上级文件路径
};
struct file_path_info file_path_info;

// 函数声明
esp_err_t list_sdcard_files(char * path);
static void file_list_btn_cb(lv_event_t * e); 



static void btn_picback_cb(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target(e);  // 获取触发事件的按钮对象
    lv_obj_t * img_container = lv_obj_get_parent(btn);  // 获取按钮的父对象（图片容器）
    if (img_container) {
        lv_obj_del(img_container);  // 删除图片容器及其子对象（图片和按钮）
    }
}

// 返回主界面按钮事件处理函数
static void btn_sdback_cb(lv_event_t * e)
{
    if (file_path_info.path_index == 0){ // 如果当前是根目录
        //bsp_sdcard_unmount(); // 卸载SD卡
        lv_obj_del(icon_in_obj); // 回到主界面
    }else{
        lv_obj_clean(sdcard_file_list); // 清除当前列表
        esp_err_t ret = list_sdcard_files(file_path_info.path_back); // 列出上一级目录文件
        if (ret == ESP_OK){ // 如果成功列出目录
            strcpy(file_path_info.path_now, file_path_info.path_back); // 刚刚进入的这个目录路径 变成当前路径
            file_path_info.path_index--; // 目录级数索引退一级
            // 计算再向下退一级的目录路径
            char *slash = strrchr(file_path_info.path_back, '/'); // 从后往前查找字符'/'
            if (slash!= NULL) { // 如果查找到
                *slash = '\0'; // 替换为NULL 表示字符串结束
            }
            ESP_LOGI(TAG, "path_index: %d", file_path_info.path_index);
            ESP_LOGI(TAG, "path_now: %s", file_path_info.path_now);
            ESP_LOGI(TAG, "path_back: %s", file_path_info.path_back);
        }
    }
}

// 列出SD卡中的文件
esp_err_t list_sdcard_files(char * path) 
{
    esp_err_t ret;
    DIR *dir;
    struct dirent *ent;
    lv_obj_t * btn;
    if ((dir = opendir(path))!= NULL) { // 打开目录
        while ((ent = readdir(dir))!= NULL) { // 读取目录中的文件
            /* 常规文件处理 */
            if (ent->d_type == DT_REG){ // 如果是常规文件
                int file_type_flag = 0;
                const char* extension = strrchr(ent->d_name, '.'); // 从后往前 找到字符'.'
                if (extension != NULL) { // 如果找到了'.'
                    extension++; // 指针地址+1
                    if (strcmp(extension, "mp3") == 0) { // 如果是mp3
                        file_type_flag = 1; // 标记为音乐文件
                    } else if (strcmp(extension, "wav") == 0) { // 如果是wav
                        file_type_flag = 1; // 标记为音乐文件
                    } else if (strcmp(extension, "mp4") == 0) {
                        file_type_flag = 2; // 标记为视频文件
                    } else if (strcmp(extension, "avi") == 0) {
                        file_type_flag = 2; // 标记为视频文件
                    } else if (strcmp(extension, "jpg") == 0) {
                        file_type_flag = 3; // 标记为图片
                    } else if (strcmp(extension, "jpeg") == 0) {
                        file_type_flag = 3; // 标记为图片
                    } else if (strcmp(extension, "png") == 0) {
                        file_type_flag = 3; // 标记为图片
                    } else if (strcmp(extension, "bmp") == 0) {
                        file_type_flag = 3; // 标记为图片
                    } else if (strcmp(extension, "gif") == 0) {
                        file_type_flag = 3; // 标记为图片
                    } else {
                        file_type_flag = 0; // 除了以上文件 其它文件都归类为普通文件
                    }
                }
                lvgl_port_lock(0);
                switch (file_type_flag)
                {
                case 1:
                    btn = lv_list_add_btn(sdcard_file_list, LV_SYMBOL_AUDIO, (const char *)ent->d_name);  // 显示音乐文件图标
                    break;
                case 2:
                    btn = lv_list_add_btn(sdcard_file_list, LV_SYMBOL_VIDEO, (const char *)ent->d_name);  // 显示视频文件图标
                    break;
                case 3:
                    btn = lv_list_add_btn(sdcard_file_list, LV_SYMBOL_IMAGE, (const char *)ent->d_name);  // 显示图片文件图标
                    break;
                default:
                    btn = lv_list_add_btn(sdcard_file_list, LV_SYMBOL_FILE, (const char *)ent->d_name);  // 显示普通文件图标
                    break;
                }
                lv_obj_t *icon = lv_obj_get_child(btn, 0); // 获取图标指针
                lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0); // 修改图标的字体    
                lv_obj_add_event_cb(btn, file_list_btn_cb, LV_EVENT_CLICKED, NULL);  // 添加点击回调函数
                lvgl_port_unlock();
            }
            /* 文件夹处理 */
            else if (ent->d_type == DT_DIR) { // 如果是文件夹
                lvgl_port_lock(0);
                btn = lv_list_add_btn(sdcard_file_list, LV_SYMBOL_DIRECTORY, (const char *)ent->d_name); 
                lv_obj_t *icon = lv_obj_get_child(btn, 0); // 获取图标指针
                lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0); // 修改图标的字体
                lv_obj_add_event_cb(btn, file_list_btn_cb, LV_EVENT_CLICKED, NULL); // 添加点击回调函数
                lvgl_port_unlock();
            }
        }
        closedir(dir);
        ret = ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to open directory %s.", path);
        ret = ESP_FAIL;
    }
    return ret;
}


void display_image(const char *filename) 
{
    static char path[56];
    const char *prefix = "S:/picture/";
    snprintf(path, sizeof(path), "%s%s", prefix, filename);
    ESP_LOGI(TAG, "pic path: %s", path);


    lvgl_port_lock(0);

    // 创建图片容器
    lv_obj_t *img_container = lv_obj_create(icon_in_obj);
    lv_obj_set_size(img_container, 320, 240);  
    lv_obj_set_style_bg_color(img_container, lv_color_white(), 0);  // 设置背景颜色为黑色
    lv_obj_set_style_border_width(img_container, 0, 0);  // 去除边框
    lv_obj_clear_flag(img_container, LV_OBJ_FLAG_SCROLLABLE);/*newnew*/
    
    // 创建图片对象
    lv_obj_t *img = lv_img_create(img_container);
    lv_img_set_src(img, path);  // 设置图片源为文件路径
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);  // 居中显示

    // 创建返回按钮
    lv_obj_t *btn_back = lv_btn_create(img_container);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_size(btn_back, 30, 30);
    lv_obj_set_style_border_width(btn_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_back, btn_picback_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    lvgl_port_unlock();

}


// 文件点击 事件处理函数
static void file_list_btn_cb(lv_event_t * e)
{
    const char *file_name = NULL; // 当前文件名称
    // 获取点击的按钮名称 即文件名称
    file_name = lv_list_get_btn_text(lv_obj_get_parent(e->target), e->target);
    ESP_LOGI(TAG, "file name: %s", file_name);
    // 列出 SD 卡中的文件
    struct stat st; // 获取文件状态信息结构体
    strcpy(file_path_info.path_back, file_path_info.path_now); // 保存上一级目录
    strcat(file_path_info.path_now, "/");
    strcat(file_path_info.path_now, file_name);

    if (stat(file_path_info.path_now, &st) == 0){ // 如果成功获取到状态信息
        if (S_ISDIR(st.st_mode)){ // 如果是目录
            lv_obj_clean(sdcard_file_list); // 清除当前列表
            esp_err_t ret = list_sdcard_files(file_path_info.path_now);
            if (ret == ESP_OK){ // 如果成功列出了目录
                file_path_info.path_index++; // 表示进入到下一级目录
                ESP_LOGI(TAG, "path_index: %d", file_path_info.path_index);
                ESP_LOGI(TAG, "path_now: %s", file_path_info.path_now);
                ESP_LOGI(TAG, "path_back: %s", file_path_info.path_back);
            }
            return;
        } else if (S_ISREG(st.st_mode)) { // 如果是常规文件
            // 检查文件扩展名
            const char *extension = strrchr(file_name, '.'); // 从后往前 找到字符'.'
            if (extension != NULL) { // 如果找到了'.'
                extension++; // 指针地址+1

                strcpy(file_path_info.path_now, file_path_info.path_back);
                char *slash = strrchr(file_path_info.path_back, '/'); // 从后往前查找字符'/'
                if (slash!= NULL) { // 如果查找到
                    *slash = '\0'; // 替换为NULL 表示字符串结束
                }

                if (strcmp(extension, "jpg") == 0 || strcmp(extension, "jpeg") == 0 ||
                    strcmp(extension, "png") == 0 || strcmp(extension, "bmp") == 0 ||
                    strcmp(extension, "gif") == 0) {
                    // 如果是图片文件，显示图片
                    display_image(file_name);
                    return;
                }
            }
        }
    }
    // 如果没有成功进入目录
    strcpy(file_path_info.path_now, file_path_info.path_back); // 没有列出新的列表 还原当前路径
    // 还原再向下退一级的目录路径
    char *slash = strrchr(file_path_info.path_back, '/'); // 从后往前查找字符'/'
    if (slash!= NULL) { // 如果查找到
        *slash = '\0'; // 替换为NULL 表示字符串结束
    }
    ESP_LOGI(TAG, "path_index: %d", file_path_info.path_index);
    ESP_LOGI(TAG, "path_now: %s", file_path_info.path_now);
    ESP_LOGI(TAG, "path_back: %s", file_path_info.path_back);
}

// SD卡处理任务
static void task_process_sdcard(void *arg)
{

    sdmmc_card_print_info(stdout, sdmmc_card);
    // 液晶屏标题栏显示SD卡容量
    lvgl_port_lock(0);
    lv_label_set_text_fmt(sdcard_label, "SD: %lluGB",
        (((uint64_t)sdmmc_card->csd.capacity) * sdmmc_card->csd.sector_size) >> 30);
    lvgl_port_unlock();

    // 创建返回按钮
    lvgl_port_lock(0);
    lv_obj_t *btn_back = lv_btn_create(sdcard_title);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(btn_back, 60, 30);
    lv_obj_set_style_border_width(btn_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_back, btn_sdback_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    // 创建文件列表
    sdcard_file_list = lv_list_create(icon_in_obj);
    lv_obj_set_size(sdcard_file_list, 320, 200);
    lv_obj_align(sdcard_file_list, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_obj_set_style_border_width(sdcard_file_list, 0, 0);
    lv_obj_set_style_text_font(sdcard_file_list, &font_alipuhui20, 0);
    lv_obj_set_scrollbar_mode(sdcard_file_list, LV_SCROLLBAR_MODE_OFF); // 隐藏滚动条
    lvgl_port_unlock();
    // 列出 SD 卡中的文件
    file_path_info.path_index = 0; // 表示当前在根目录
    strcpy(file_path_info.path_now, SD_MOUNT_POINT); // 装入当前路径
    list_sdcard_files(file_path_info.path_now); // 列出当前目录文件

    
    vTaskDelete(NULL);
}


// 进入SD卡应用程序
static void sdcard_event_handler(lv_event_t * e)
{
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xffffff));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    // 创建标题背景
    sdcard_title = lv_obj_create(icon_in_obj);
    lv_obj_set_size(sdcard_title, 320, 40);
    lv_obj_set_style_pad_all(sdcard_title, 0, 0);  // 设置间隙
    lv_obj_align(sdcard_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(sdcard_title, lv_color_hex(0x008b8b), 0);
    // 显示标题
    sdcard_label = lv_label_create(sdcard_title);
    lv_label_set_text(sdcard_label, "TF卡扫描中...");
    lv_obj_set_style_text_color(sdcard_label, lv_color_hex(0xffffff), 0); 
    lv_obj_set_style_text_font(sdcard_label, &font_alipuhui20, 0);
    lv_obj_align(sdcard_label, LV_ALIGN_CENTER, 0, 0);

    icon_flag = 3; // 标记已经进入第三个应用

    xTaskCreatePinnedToCore(task_process_sdcard, "task_process_sdcard", 3 * 1024, NULL, 5, NULL, 1);
}



/******************************** 第4个图标 摄像头 应用程序 *****************************************************************************/
lv_obj_t * img_camera;
char camera_capture_flag = 0;

// 摄像头图像
lv_img_dsc_t img_camera_dsc = {
  .header.cf = LV_IMG_CF_TRUE_COLOR,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = 320,
  .header.h = 240,
  .data_size = 240*320*2,
};

// 摄像头处理任务
static void task_process_camera(void *arg)
{   
    while (icon_flag == 4)
    {
        camera_fb_t *frame = esp_camera_fb_get();

        // 保存原始图片到本地
        esp_err_t res = ESP_OK;
        size_t _jpg_buf_len;
        uint8_t *_jpg_buf;
        char file_name[32];
        static uint8_t count_flag = 1;
        if(camera_capture_flag)
        {
            camera_capture_flag = 0;
            if (!frame) {
                ESP_LOGE(TAG, "Camera capture failed");
                res = ESP_FAIL;
                break;
                }
                if (frame->format != PIXFORMAT_JPEG) {
                bool jpeg_converted = frame2jpg(frame, 80, &_jpg_buf, &_jpg_buf_len);
                if (! jpeg_converted) {
                    ESP_LOGE(TAG, "JPEG compression failed");
                    esp_camera_fb_return(frame);
                    res = ESP_FAIL;
                }
                } else {
                _jpg_buf_len = frame->len;
                _jpg_buf = frame->buf;
                }
                if (res == ESP_OK) {
                    snprintf(file_name, sizeof(file_name), 
                    "/sdcard/picture/image%d.jpg", 
                    count_flag++);
                    if(count_flag > 10) count_flag = 1;
                    ESP_LOGI(TAG, "Opening file %s", file_name);
                    FILE *f = fopen(file_name, "wb");
                    if (f == NULL) {
                        ESP_LOGE(TAG, "Failed to open file for writing\n");
                        fclose(f);
                    } else {
                        fwrite((const char *)_jpg_buf, 1, _jpg_buf_len, f);
                        fclose(f);
                        ESP_LOGI(TAG, "write img finished!\n");
                    }
                }
                if (frame->format != PIXFORMAT_JPEG) {
                free(_jpg_buf);
                }
        }
        

        img_camera_dsc.data = frame->buf;
        lv_img_set_src(img_camera, &img_camera_dsc);
        esp_camera_fb_return(frame);
    }
    esp_camera_deinit(); // 取消初始化摄像头
    lvgl_port_lock(0);
    lv_obj_del(icon_in_obj); // 删除摄像头画布
    lvgl_port_unlock();
    dvp_pwdn(1); // 摄像头进入掉电模式
    vTaskDelete(NULL);
}

// 返回主界面按钮事件处理函数
static void btn_camback_cb(lv_event_t * e)
{
    icon_flag = 0;
}

// 进入摄像头应用
static void camera_event_handler(lv_event_t * e)
{
    bsp_camera_init(); // 摄像头初始化
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xcccccc));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    img_camera = lv_img_create(icon_in_obj);
    lv_obj_set_pos(img_camera, 0, 0);
    lv_obj_set_size(img_camera, 320, 240);

    // 创建返回按钮
    lv_obj_t *btn_back = lv_btn_create(icon_in_obj);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_size(btn_back, 60, 30);
    lv_obj_set_style_border_width(btn_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_back, btn_camback_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    icon_flag = 4; // 标记已经进入第四个应用

    xTaskCreatePinnedToCore(task_process_camera, "task_process_camera", 4 * 1024, NULL, 5, NULL, 1);
}


/******************************** 第5个图标 PC资源 应用程序*************************************************************************************/
lv_obj_t * pc_title_label; // 标题栏文字
lv_obj_t *btn_pc_back; //
lv_obj_t *obj_cpu_bar; 
lv_obj_t *obj_gpu_bar; 
lv_obj_t *cpu_usage_label; 
lv_obj_t *gpu_usage_label;
lv_obj_t *cpu_temp_label; 
lv_obj_t *gpu_temp_label; 
lv_obj_t *obj_cpumem_bar; 
lv_obj_t *obj_gpumem_bar; 
lv_obj_t *cpu_memuse_label; 
lv_obj_t *gpu_memuse_label;
lv_obj_t *ip_label;

static SemaphoreHandle_t pc_del_semaphore = NULL;

// 辅助函数：安全转换字符串为整数
static int safe_atoi(const char *str, int default_value) {
    if (!str || *str == '\0') return default_value;
    
    char *endptr;
    long val = strtol(str, &endptr, 10);
    if (*endptr != '\0') {
        ESP_LOGW(TAG, "Invalid number format: %s", str);
        return default_value;
    }
    return (int)val;
}

void parse_event_stream_data(const char *data)
{
    // ESP_LOGI(TAG, "Raw data: %s", data); // 打印原始数据，用于调试

    const char *data_start = strstr(data, "Page0");
    if (data_start == NULL) {
        ESP_LOGE(TAG, "Invalid data format: 'data:' not found");
        return;
    }
    // ESP_LOGI(TAG, "Found 'data:' at position: %d", (int)(data_start - data)); // 打印找到的位置

    // 跳过 前缀
    data_start += 6;

    const char *data_end = strstr(data_start, "HTTP");
    if (data_end == NULL) {
        data_end = data_start + strlen(data_start);
    }
    // ESP_LOGI(TAG, "Found 'HTTP' at position: %d", (int)(data_end - data)); // 打印找到的位置

    // ESP_LOGI(TAG, "Data block length: %d", (int)(data_end - data_start)); // 打印数据块长度

    // 提取第一个 "data:" 块的内容
    char block[256]; // 假设块的最大长度为 256 字节
    int block_length = data_end - data_start;
    if (block_length >= sizeof(block)) {
        block_length = sizeof(block) - 1; // 防止溢出
    }
    strncpy(block, data_start, block_length);
    block[block_length] = '\0'; // 添加字符串结束符
    ESP_LOGI(TAG, "Extracted block: %s", block); // 打印提取的数据块

    // 解析键值对
    char *saveptr = NULL;
    char *token = strtok_r(block, "{|}", &saveptr);
 
    while (token) {
        char *current_key = token;
        char *current_value = strtok_r(NULL, "{|}", &saveptr);
        if (!current_value) break;

        ESP_LOGI(TAG, "Processing: %s => %s", current_key, current_value);

        // 更新对应控件
        if (strcmp(current_key, "Simple1") == 0) {
            int value = safe_atoi(current_value, 0);
            if (obj_cpu_bar) {
                // lvgl_port_lock(0);
                lv_arc_set_value(obj_cpu_bar, value);
                lv_label_set_text_fmt(cpu_usage_label, "%d", value);
                // lvgl_port_unlock();
            }
        }
        else if (strcmp(current_key, "Simple2") == 0) {
            if (cpu_temp_label) {
                // lvgl_port_lock(0);
                lv_label_set_text(cpu_temp_label, current_value);
                // lvgl_port_unlock();
            }
        }
        else if (strcmp(current_key, "Simple3") == 0) {
            int value = safe_atoi(current_value, 0);
            if (obj_gpu_bar) {
                // lvgl_port_lock(0);
                lv_arc_set_value(obj_gpu_bar, value);
                lv_label_set_text_fmt(gpu_usage_label, "%d", value);
                // lvgl_port_unlock();
            }
        }
        else if (strcmp(current_key, "Simple4") == 0) {
            if (gpu_temp_label) {
                // lvgl_port_lock(0);
                lv_label_set_text(gpu_temp_label, current_value);
                // lvgl_port_unlock();
            }
        }
        else if (strcmp(current_key, "Simple5") == 0) {
            int value = safe_atoi(current_value, 0);
            if (obj_cpumem_bar) {
                // lvgl_port_lock(0);
                lv_bar_set_value(obj_cpumem_bar, value, LV_ANIM_OFF);
                lv_label_set_text_fmt(cpu_memuse_label, "%d", value);
                // lvgl_port_unlock();
            }
        }
        else if (strcmp(current_key, "Simple6") == 0) {
            int value = safe_atoi(current_value, 0);
            if (obj_gpumem_bar) {
                // lvgl_port_lock(0);
                lv_bar_set_value(obj_gpumem_bar, value, LV_ANIM_OFF);
                lv_label_set_text_fmt(gpu_memuse_label, "%d", value);
                // lvgl_port_unlock();
            }
        }
        else if (strcmp(current_key, "Simple7") == 0) {
            if (ip_label) {
                // IP地址格式化处理
                char ip_str[32] = {0};
                snprintf(ip_str, sizeof(ip_str), "   %s", current_value);
                // lvgl_port_lock(0);
                lv_label_set_text(ip_label, ip_str);
                // lvgl_port_unlock();
            }
        }

        token = strtok_r(NULL, "{|}", &saveptr);
    }
}

static void http_pc_task(void *pvParameters)
{
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   //用于接收通过http协议返回的数据
    int content_length = 0;  //http协议头的长度
   
   //定义http配置结构体，并且进行清零
    esp_http_client_config_t config = {
        .url = "http://192.168.43.39:80/sse",
        .transport_type = HTTP_TRANSPORT_OVER_TCP, // 禁用 SSL/TLS
    };
    //初始化结构体
    esp_http_client_handle_t client = esp_http_client_init(&config);	//初始化http连接
    //设置发送请求 
    esp_http_client_set_method(client, HTTP_METHOD_GET);

    while(1)
    {
        // 与目标主机创建连接，并且声明写入内容长度为0
        esp_err_t err = esp_http_client_open(client, 0);
        //如果连接失败
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            vTaskDelete(NULL);
        } 
        //如果连接成功
        else {
            //读取目标主机的返回内容的协议头
            content_length = esp_http_client_fetch_headers(client);
            //如果协议头长度小于0，说明没有成功读取到
            if (content_length < 0) {
                ESP_LOGE(TAG, "HTTP client fetch headers failed");
            } //如果成功读取到了协议头
            else {
                //读取目标主机通过http的响应内容
                int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER/2);
                if (data_read >= 0) {
                    //打印响应内容，包括响应状态，响应体长度及其内容
                    ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld",
                    esp_http_client_get_status_code(client),				//获取响应状态信息
                    esp_http_client_get_content_length(client));			//获取响应信息长度
                    // printf("respone:\n%s\n over\n", output_buffer);
                    //对接收到的数据作相应的处理
                    parse_event_stream_data(output_buffer);
                    
                    // lv_refr_now(NULL);
                } 
                //如果不成功
                else {
                    ESP_LOGE(TAG, "Failed to read response");
                }
            }
        }
        //关闭连接
        esp_http_client_close(client);

        if (xSemaphoreTake(pc_del_semaphore, 2000 / portTICK_PERIOD_MS) == pdTRUE) {
            break; // 收到删除信号，退出循环
        }
    }
    // 清理资源
    esp_http_client_cleanup(client);
    ESP_LOGE(TAG, "esp_http_client_cleanup ok");
    vTaskDelete(NULL);

}

// 返回主界面按钮事件处理函数
static void btn_pc_back_cb(lv_event_t * e)
{
    // 发送删除任务信号量
    if (pc_del_semaphore != NULL) {
        xSemaphoreGive(pc_del_semaphore);
    }
    lv_obj_del(icon_in_obj); // 删除画布
    icon_flag = 0;
}

static void task_process_pc(void *arg)
{
    if (pc_del_semaphore == NULL) {
        pc_del_semaphore = xSemaphoreCreateBinary();
        xSemaphoreTake(pc_del_semaphore, 1000 / portTICK_PERIOD_MS);
    }

    // 判断wifi是否已连接
    if (s_wifi_event_group != NULL) // 如果创建了此事件组
    {
        ESP_LOGI(TAG, "pc wifi created!!");
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, 10);
        if (bits & WIFI_CONNECTED_BIT) { // 如果已经连接到wifi
            ESP_LOGI(TAG, "pc wifi connected!!");

            xTaskCreatePinnedToCore(http_pc_task, "http_pc_task", 8 * 1024, NULL, 5, NULL, 0);
        }
        else{
            ESP_LOGE(TAG, "pc wifi  not connected!! return ");
            lv_obj_del(icon_in_obj); // 删除画布
            icon_flag = 0;
        } 
    }
    else {
        ESP_LOGE(TAG, "pc wifi  not created!! return ");
    }

    vTaskDelete(NULL);
}

static void pc_event_handler(lv_event_t * e)
{
    // 创建一个界面
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xffffff));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    // 创建标题背景
    lv_obj_t *pc_title_back = lv_obj_create(icon_in_obj);
    lv_obj_set_size(pc_title_back, 320, 40);
    lv_obj_set_style_pad_all(pc_title_back, 0, 0);  // 设置间隙
    lv_obj_align(pc_title_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(pc_title_back, lv_color_hex(0x30a830), 0);
    // 显示标题
    pc_title_label = lv_label_create(pc_title_back);
    lv_label_set_text(pc_title_label, "PC监控");
    lv_obj_set_style_text_color(pc_title_label, lv_color_hex(0xffffff), 0); 
    lv_obj_set_style_text_font(pc_title_label, &font_alipuhui20, 0);
    lv_obj_align(pc_title_label, LV_ALIGN_CENTER, 0, 0);
    // 创建后退按钮
    btn_pc_back = lv_btn_create(pc_title_back);
    lv_obj_align(btn_pc_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(btn_pc_back, 60, 30);
    lv_obj_set_style_border_width(btn_pc_back, 0, 0); // 设置边框宽度
    lv_obj_set_style_pad_all(btn_pc_back, 0, 0);  // 设置间隙
    lv_obj_set_style_bg_opa(btn_pc_back, LV_OPA_TRANSP, LV_PART_MAIN); // 背景透明
    lv_obj_set_style_shadow_opa(btn_pc_back, LV_OPA_TRANSP, LV_PART_MAIN); // 阴影透明
    lv_obj_add_event_cb(btn_pc_back, btn_pc_back_cb, LV_EVENT_CLICKED, NULL); // 添加按键处理函数

    lv_obj_t *label_back = lv_label_create(btn_pc_back); 
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);  // 按键上显示左箭头符号
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xffffff), 0); 
    lv_obj_align(label_back, LV_ALIGN_CENTER, -10, 0);

    //style初始化*************************************************************
    // 移除边框样式风格
    static lv_style_t style_no_border;
    lv_style_init(&style_no_border);
    lv_style_set_border_width(&style_no_border, 0);  
  
    // 圆弧主背景样式
    static lv_style_t style_bg;
    lv_style_init(&style_bg);
    lv_style_set_arc_width(&style_bg, 10);  
    lv_style_set_border_width(&style_bg, 0);
    lv_style_set_arc_color(&style_bg, lv_palette_lighten(LV_PALETTE_GREY, 2)); 
  
    // 圆弧指示器样式（指示器）
    static lv_style_t style_indic;
    lv_style_init(&style_indic);
    lv_style_set_arc_width(&style_indic, 10);  
    lv_style_set_arc_color(&style_indic, lv_palette_main(LV_PALETTE_ORANGE));  
  
    // 进度条主背景样式
    static lv_style_t style_bg1;
    lv_style_init(&style_bg1);
    lv_style_set_bg_color(&style_bg1, lv_palette_lighten(LV_PALETTE_GREY, 2));  // 设置背景颜色
    lv_style_set_radius(&style_bg1, 5);  // 设置圆角
  
    // 进度条指示器样式
    static lv_style_t style_indic1;
    lv_style_init(&style_indic1);
    lv_style_set_bg_color(&style_indic1, lv_palette_main(LV_PALETTE_ORANGE));  // 设置指示器颜色
    lv_style_set_radius(&style_indic1, 5);  // 设置圆角
  
    // 创建cpu圆弧形进度条***************************************
    lv_obj_t *cpu_bar_back = lv_obj_create(icon_in_obj);  
    lv_obj_set_size(cpu_bar_back, 130, 130);
    lv_obj_align(cpu_bar_back, LV_ALIGN_CENTER, -75, -15);
    lv_obj_clear_flag(cpu_bar_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cpu_bar_back, &style_no_border, LV_PART_MAIN);
  
    obj_cpu_bar = lv_arc_create(cpu_bar_back);
    lv_obj_set_size(obj_cpu_bar, 120, 120);
    lv_arc_set_angles(obj_cpu_bar, 0, 270);  // 设置圆弧角度范围
    lv_arc_set_range(obj_cpu_bar, 0, 100);  // 设置进度条范围
    lv_arc_set_value(obj_cpu_bar, 50);       // 初始化进度条值
    lv_obj_center(obj_cpu_bar); 
    lv_obj_remove_style(obj_cpu_bar, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(obj_cpu_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(obj_cpu_bar, &style_bg, LV_PART_MAIN);  // 应用样式到主背景
    lv_obj_add_style(obj_cpu_bar, &style_indic, LV_PART_INDICATOR);  // 应用样式到指示器
  
    //cpu使用率
    cpu_usage_label = lv_label_create(cpu_bar_back);  
    lv_obj_align(cpu_usage_label, LV_ALIGN_CENTER, -2, -15);
    lv_obj_set_style_text_font(cpu_usage_label, &lv_font_montserrat_20, 0);
    lv_label_set_text(cpu_usage_label, "98"); 
    // %符号标签
    lv_obj_t * cpu_usage_percent = lv_label_create(cpu_bar_back);
    lv_obj_align(cpu_usage_percent, LV_ALIGN_CENTER, 18, -14);
    lv_obj_set_style_text_font(cpu_usage_percent, &lv_font_montserrat_10, 0);
    lv_label_set_text(cpu_usage_percent, "%");
  
    //cpu标识
    lv_obj_t * cpu_label = lv_label_create(cpu_bar_back);  
    lv_obj_align(cpu_label, LV_ALIGN_CENTER, 0, 10);
    lv_label_set_text(cpu_label, "CPU");    
  
    //cpu温度
    cpu_temp_label = lv_label_create(cpu_bar_back);  
    lv_obj_align(cpu_temp_label, LV_ALIGN_CENTER, -7, 35);
    lv_label_set_text(cpu_temp_label, "27");     
    // °C符号标签
    lv_obj_t * cpu_temp_symbol = lv_label_create(cpu_bar_back);  
    lv_obj_align(cpu_temp_symbol, LV_ALIGN_CENTER, 10, 35);
    lv_label_set_text(cpu_temp_symbol, "°C");
  
  
    // 创建gpu圆弧形进度条***************************************
    lv_obj_t *gpu_bar_back = lv_obj_create(icon_in_obj);  
    lv_obj_set_size(gpu_bar_back, 130, 130);
    lv_obj_align(gpu_bar_back, LV_ALIGN_CENTER, 75, -15);
    lv_obj_clear_flag(gpu_bar_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(gpu_bar_back, &style_no_border, LV_PART_MAIN);
  
    obj_gpu_bar = lv_arc_create(gpu_bar_back);
    lv_obj_set_size(obj_gpu_bar, 120, 120);
    lv_arc_set_angles(obj_gpu_bar, 0, 270);  // 设置圆弧角度范围
    lv_arc_set_range(obj_gpu_bar, 0, 100);  // 设置进度条范围
    lv_arc_set_value(obj_gpu_bar, 50);       // 初始化进度条值
    lv_obj_center(obj_gpu_bar); 
    lv_obj_remove_style(obj_gpu_bar, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(obj_gpu_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(obj_gpu_bar, &style_bg, LV_PART_MAIN);  // 应用样式到主背景
    lv_obj_add_style(obj_gpu_bar, &style_indic, LV_PART_INDICATOR);  // 应用样式到指示器
  
    //gpu使用率
    gpu_usage_label = lv_label_create(gpu_bar_back);  
    lv_obj_align(gpu_usage_label, LV_ALIGN_CENTER, -2, -15);
    lv_obj_set_style_text_font(gpu_usage_label, &lv_font_montserrat_20, 0);
    lv_label_set_text(gpu_usage_label, "98"); 
    // %符号标签
    lv_obj_t * gpu_usage_percent = lv_label_create(gpu_bar_back);
    lv_obj_align(gpu_usage_percent, LV_ALIGN_CENTER, 18, -14);
    lv_obj_set_style_text_font(gpu_usage_percent, &lv_font_montserrat_10, 0);
    lv_label_set_text(gpu_usage_percent, "%");
  
    //gpu标识
    lv_obj_t * gpu_label = lv_label_create(gpu_bar_back);  
    lv_obj_align(gpu_label, LV_ALIGN_CENTER, 0, 10);
    lv_label_set_text(gpu_label, "GPU");    
  
    //gpu温度
    gpu_temp_label = lv_label_create(gpu_bar_back);  
    lv_obj_align(gpu_temp_label, LV_ALIGN_CENTER, -7, 35);
    lv_label_set_text(gpu_temp_label, "27");     
    // °C符号标签
    lv_obj_t * gpu_temp_symbol = lv_label_create(gpu_bar_back);  
    lv_obj_align(gpu_temp_symbol, LV_ALIGN_CENTER, 10, 35);
    lv_label_set_text(gpu_temp_symbol, "°C");
  
  
  
    // 创建cpu mem进度条***************************************
    lv_obj_t *cpumem_bar_back = lv_obj_create(icon_in_obj);  
    lv_obj_set_size(cpumem_bar_back, 130, 55);
    lv_obj_align(cpumem_bar_back, LV_ALIGN_CENTER, -75, 70);
    lv_obj_clear_flag(cpumem_bar_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cpumem_bar_back, &style_no_border, LV_PART_MAIN);
  
    obj_cpumem_bar = lv_bar_create(cpumem_bar_back);
    lv_obj_set_size(obj_cpumem_bar, 120, 10);
    lv_bar_set_range(obj_cpumem_bar, 0, 100);  // 设置进度条范围
    lv_bar_set_value(obj_cpumem_bar,70,LV_ANIM_OFF);      // 初始化进度条值
    lv_obj_align(obj_cpumem_bar, LV_ALIGN_CENTER,0, 0);
    lv_obj_remove_style(obj_cpumem_bar, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(obj_cpumem_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(obj_cpumem_bar, &style_bg1, LV_PART_MAIN);  // 应用样式到主背景
    lv_obj_add_style(obj_cpumem_bar, &style_indic1, LV_PART_INDICATOR);  // 应用样式到指示器
  
    //cpumem使用率标识
    lv_obj_t * cpumem_label = lv_label_create(cpumem_bar_back);  
    lv_obj_align(cpumem_label, LV_ALIGN_CENTER, 0, -18);
    lv_obj_set_style_text_font(cpumem_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(cpumem_label, "Memory Usage");    
  
    //cpumem使用率
    cpu_memuse_label = lv_label_create(cpumem_bar_back);  
    lv_obj_align(cpu_memuse_label, LV_ALIGN_CENTER, -2, +15);
    lv_obj_set_style_text_font(cpu_memuse_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(cpu_memuse_label, "70"); 
    // %符号标签
    lv_obj_t * cpu_memuse_percent = lv_label_create(cpumem_bar_back);
    lv_obj_align(cpu_memuse_percent, LV_ALIGN_CENTER, 14, +15);
    lv_obj_set_style_text_font(cpu_memuse_percent, &lv_font_montserrat_10, 0);
    lv_label_set_text(cpu_memuse_percent, "%");
  
  
  // 创建gpu mem进度条***************************************
    lv_obj_t *gpumem_bar_back = lv_obj_create(icon_in_obj);  
    lv_obj_set_size(gpumem_bar_back, 130, 55);
    lv_obj_align(gpumem_bar_back, LV_ALIGN_CENTER, 75, 70);
    lv_obj_clear_flag(gpumem_bar_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(gpumem_bar_back, &style_no_border, LV_PART_MAIN);
  
    obj_gpumem_bar = lv_bar_create(gpumem_bar_back);
    lv_obj_set_size(obj_gpumem_bar, 120, 10);
    lv_bar_set_range(obj_gpumem_bar, 0, 100);  // 设置进度条范围
    lv_bar_set_value(obj_gpumem_bar,70,LV_ANIM_OFF);      // 初始化进度条值
    lv_obj_align(obj_gpumem_bar, LV_ALIGN_CENTER,0, 0);
    lv_obj_remove_style(obj_gpumem_bar, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(obj_gpumem_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(obj_gpumem_bar, &style_bg1, LV_PART_MAIN);  // 应用样式到主背景
    lv_obj_add_style(obj_gpumem_bar, &style_indic1, LV_PART_INDICATOR);  // 应用样式到指示器
  
    //gpumem使用率标识
    lv_obj_t * gpumem_label = lv_label_create(gpumem_bar_back);  
    lv_obj_align(gpumem_label, LV_ALIGN_CENTER, 0, -18);
    lv_obj_set_style_text_font(gpumem_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(gpumem_label, "Memory Usage");    
  
    //gpumem使用率
    gpu_memuse_label = lv_label_create(gpumem_bar_back);  
    lv_obj_align(gpu_memuse_label, LV_ALIGN_CENTER, -2, +15);
    lv_obj_set_style_text_font(gpu_memuse_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(gpu_memuse_label, "70"); 
    // %符号标签
    lv_obj_t * gpu_memuse_percent = lv_label_create(gpumem_bar_back);
    lv_obj_align(gpu_memuse_percent, LV_ALIGN_CENTER, 14, +15);
    lv_obj_set_style_text_font(gpu_memuse_percent, &lv_font_montserrat_10, 0);
    lv_label_set_text(gpu_memuse_percent, "%");
  
  
    //PC IP显示
    lv_obj_t *ip_back = lv_obj_create(icon_in_obj);
    lv_obj_set_size(ip_back, 320, 25);
    lv_obj_set_style_pad_all(ip_back, 0, 0);  // 设置间隙
    lv_obj_align(ip_back, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_clear_flag(ip_back, LV_OBJ_FLAG_SCROLLABLE);
    // lv_obj_add_style(ip_back, &style_no_border, LV_PART_MAIN);
  
    lv_obj_t *ip_desc = lv_label_create(ip_back);
    lv_label_set_text(ip_desc, "PC IP address: ");  // 默认时间
    lv_obj_align(ip_desc, LV_ALIGN_CENTER, -45, 0);
  
    ip_label = lv_label_create(ip_back);
    lv_label_set_text(ip_label, "192.168.0.1");  // 默认时间
    lv_obj_align(ip_label, LV_ALIGN_CENTER, 45, 0);

    icon_flag = 5; // 标记已经进入第五个应用
    xTaskCreatePinnedToCore(task_process_pc, "task_process_pc", 2 * 1024, NULL, 5, NULL, 1);
}


/******************************** 第6个图标 USB拓展屏幕 应用程序***********************************************************************************/


/******************************** 主界面  ******************************/

void lv_main_page(void)
{
    lvgl_port_lock(0);

    // 创建主界面基本对象
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0); // 修改背景为黑色

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0x00BFFF));
    lv_style_set_bg_grad_color( &style, lv_color_hex( 0x00BF00 ) );
    lv_style_set_bg_grad_dir( &style, LV_GRAD_DIR_VER );
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    main_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(main_obj, &style, 0);

    // 显示左上角欢迎语
    main_text_label = lv_label_create(main_obj);
    lv_obj_set_style_text_font(main_text_label, &font_alipuhui20, 0);
    lv_label_set_long_mode(main_text_label, LV_LABEL_LONG_SCROLL_CIRCULAR);     /*Circular scroll*/
    lv_obj_set_width(main_text_label, 280);
    lv_label_set_text(main_text_label, "Welcome");
    lv_obj_align_to(main_text_label, main_obj, LV_ALIGN_TOP_LEFT, 8, 5);

    // 设置应用图标style
    static lv_style_t btn_style;
    lv_style_init(&btn_style);
    lv_style_set_radius(&btn_style, 16);  
    lv_style_set_bg_opa( &btn_style, LV_OPA_COVER );
    lv_style_set_text_color(&btn_style, lv_color_hex(0xffffff)); 
    lv_style_set_border_width(&btn_style, 0);
    lv_style_set_pad_all(&btn_style, 5);
    lv_style_set_width(&btn_style, 80);  
    lv_style_set_height(&btn_style, 80); 

    // 创建第1个应用图标
    lv_obj_t *icon1 = lv_btn_create(main_obj);
    lv_obj_add_style(icon1, &btn_style, 0);
    lv_obj_set_style_bg_color(icon1, lv_color_hex(0x30a830), 0);
    lv_obj_set_pos(icon1, 15, 50);
    lv_obj_add_event_cb(icon1, weather_event_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t * img1 = lv_img_create(icon1);
    LV_IMG_DECLARE(img_weather_icon);
    lv_img_set_src(img1, &img_weather_icon);
    lv_obj_align(img1, LV_ALIGN_CENTER, 0, 0);

    // 创建第2个应用图标
    lv_obj_t *icon2 = lv_btn_create(main_obj);
    lv_obj_add_style(icon2, &btn_style, 0);
    lv_obj_set_style_bg_color(icon2, lv_color_hex(0xf87c30), 0);
    lv_obj_set_pos(icon2, 120, 50);
    lv_obj_add_event_cb(icon2, music_event_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t * img2 = lv_img_create(icon2);
    LV_IMG_DECLARE(img_music_icon);
    lv_img_set_src(img2, &img_music_icon);
    lv_obj_align(img2, LV_ALIGN_CENTER, 0, 0);

    // 创建第3个应用图标
    lv_obj_t *icon3 = lv_btn_create(main_obj);
    lv_obj_add_style(icon3, &btn_style, 0);
    lv_obj_set_style_bg_color(icon3, lv_color_hex(0x008b8b), 0);
    lv_obj_set_pos(icon3, 225, 50);
    lv_obj_add_event_cb(icon3, sdcard_event_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t * img3 = lv_img_create(icon3);
    LV_IMG_DECLARE(img_sd_icon);
    lv_img_set_src(img3, &img_sd_icon);
    lv_obj_align(img3, LV_ALIGN_CENTER, 0, 0);

    // 创建第4个应用图标
    lv_obj_t *icon4 = lv_btn_create(main_obj);
    lv_obj_add_style(icon4, &btn_style, 0);
    lv_obj_set_style_bg_color(icon4, lv_color_hex(0xd8b010), 0);
    lv_obj_set_pos(icon4, 15, 147);
    lv_obj_add_event_cb(icon4, camera_event_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t * img4 = lv_img_create(icon4);
    LV_IMG_DECLARE(img_camera_icon);
    lv_img_set_src(img4, &img_camera_icon);
    lv_obj_align(img4, LV_ALIGN_CENTER, 0, 0);

    // 创建第5个应用图标
    lv_obj_t *icon5 = lv_btn_create(main_obj);
    lv_obj_add_style(icon5, &btn_style, 0);
    lv_obj_set_style_bg_color(icon5, lv_color_hex(0xcd5c5c), 0);
    lv_obj_set_pos(icon5, 120, 147);
    lv_obj_add_event_cb(icon5, pc_event_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t * img5 = lv_img_create(icon5);
    LV_IMG_DECLARE(img_computer_icon);
    lv_img_set_src(img5, &img_computer_icon);
    lv_obj_align(img5, LV_ALIGN_CENTER, 0, 0);

    // 创建第6个应用图标
    lv_obj_t *icon6 = lv_btn_create(main_obj);
    lv_obj_add_style(icon6, &btn_style, 0);
    lv_obj_set_style_bg_color(icon6, lv_color_hex(0xb87fa8), 0);
    lv_obj_set_pos(icon6, 225, 147);
    lv_obj_add_event_cb(icon6, wifiset_event_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t * img6 = lv_img_create(icon6);
    LV_IMG_DECLARE(img_wifiset_icon);
    lv_img_set_src(img6, &img_wifiset_icon);
    lv_obj_align(img6, LV_ALIGN_CENTER, 0, 0);

    lvgl_port_unlock();
}


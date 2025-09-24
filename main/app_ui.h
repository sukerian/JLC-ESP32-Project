#pragma once


/*********************** WIFI参数 ****************************/
#define MAX_HTTP_OUTPUT_BUFFER 512

// wifi事件
#define WIFI_CONNECTED_BIT    BIT0
#define WIFI_FAIL_BIT         BIT1
#define WIFI_START_BIT        BIT2
#define WIFI_GET_SNTP_BIT     BIT3

#define DEFAULT_ESP_MAXIMUM_RETRY  2 
#define DEFAULT_SCAN_LIST_SIZE   5  // 最大扫描wifi个数

#define WEATHER_DEL_BIT BIT0

/*********************** 主界面 ****************************/
void lv_main_page(void);


/*********************** 音乐播放器 ****************************/
void mp3_player_init(void);
void music_ui(void);

extern char camera_capture_flag;
extern int icon_flag;

// #define AIDA64_SERVER_IP "192.168.31.32"  // 替换为AIDA64服务器的IP地址
// #define AIDA64_SERVER_PORT "80"         // 替换为AIDA64服务器的端口号

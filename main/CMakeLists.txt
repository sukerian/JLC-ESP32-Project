
set(srcs "usb/app_hid.c" "usb/app_uac.c" "usb/app_usb.c" "usb/app_vendor.c"
 "usb/usb_frame.c" "usb/uac/usb_device_uac.c" "usb/usb_device/usb_descriptors.c")

# if(CONFIG_IDF_TARGET_ESP32S3)
#     list(APPEND srcs "usb/app_lcd_s3.c")
# endif()

idf_component_register(SRCS "esp32_s3_szp.c" "main.c" "app_ui.c"
                       SRCS "assets/img_weather_icon.c"
                       SRCS "assets/img_computer_icon.c"
                       SRCS "assets/img_camera_icon.c"
                       SRCS "assets/img_music_icon.c"
                       SRCS "assets/img_sd_icon.c"
                       SRCS "assets/img_wifiset_icon.c"
                       SRCS "assets/font_alipuhui20.c"
                       SRCS ${srcs}
                       INCLUDE_DIRS "."
                       INCLUDE_DIRS "usb/include" "usb/uac" "usb/usb_device")        

idf_component_get_property(tusb_lib leeebo__tinyusb_src COMPONENT_LIB)
cmake_policy(SET CMP0079 NEW)
target_link_libraries(${tusb_lib} PRIVATE ${COMPONENT_LIB})   
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable)

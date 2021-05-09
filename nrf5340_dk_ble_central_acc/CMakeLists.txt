cmake_minimum_required(VERSION 3.13.1)

set(SHIELD adafruit_2_8_tft_touch_v2)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(ble_central_acc)

target_sources(app PRIVATE src/main.c src/fonts/calibri_20b.c src/fonts/calibri_20.c)

zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)

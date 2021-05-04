cmake_minimum_required(VERSION 3.8)

include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(ble_peripheral_acc)

target_sources(app PRIVATE
  src/main.c src/acc_service.c
)

zephyr_library_include_directories(.)


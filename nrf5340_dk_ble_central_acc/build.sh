#!/bin/sh

rm -rf build
west build -b nrf5340dk_nrf5340_cpuapp
west flash

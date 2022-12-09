cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

project(019__Clock-V1)
pico_sdk_init()

add_executable(019__Clock-V1 019__Clock-V1.cpp)

# Pull in our pico_stdlib which pulls in commonly used features
target_link_libraries(019__Clock-V1 pico_stdlib)
target_link_libraries(019__Clock-V1 pico_multicore)
target_link_libraries(019__Clock-V1 hardware_gpio)
target_link_libraries(019__Clock-V1 hardware_adc)

# enable usb output, disable uart output
pico_enable_stdio_usb(019__Clock-V1 1)
pico_enable_stdio_uart(019__Clock-V1 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(019__Clock-V1)

# add url via pico_set_program_url
#example_auto_set_url(019__Clock-V1)


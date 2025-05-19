cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Define a placa alvo
set(PICO_BOARD pico_w)

# Importa o SDK do Raspberry Pi Pico
include(pico_sdk_import.cmake)

# Configuração do FreeRTOS
set(FREERTOS_KERNEL_PATH "C:/FreeRTOS-Kernel")
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

project(alert-station C CXX ASM)

# Inicializa o SDK do Pico
pico_sdk_init()

# Cria o executável com os arquivos do projeto
add_executable(alert-station 
    alert-station.c 
    lib/ssd1306.c
)

# Geração do cabeçalho do PIO
pico_generate_pio_header(alert-station ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)

# Define nome e versão do programa
pico_set_program_name(alert-station "alert-station")
pico_set_program_version(alert-station "0.1")

# Ativa saída padrão via USB
pico_enable_stdio_uart(alert-station 0)
pico_enable_stdio_usb(alert-station 1)

# Diretórios de inclusão
target_include_directories(alert-station PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_SOURCE_DIR}/lib
)

# Ligações com as bibliotecas necessárias
target_link_libraries(alert-station
    pico_stdlib
    hardware_i2c
    hardware_timer
    hardware_watchdog
    hardware_clocks
    hardware_pwm
    hardware_adc
    hardware_pio
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
)

# Geração de arquivos extras (.uf2, etc.)
pico_add_extra_outputs(alert-station)

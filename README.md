# Estação de Monitoramento de Enchentes 🚨🌊

*Raspberry Pi Pico • FreeRTOS • Simulação BitDogLab*

![Pico‑SDK](https://img.shields.io/badge/Pico--SDK-%3E%3D1.5-blue?style=flat-square)
![FreeRTOS](https://img.shields.io/badge/FreeRTOS-ready-brightgreen?style=flat-square)
![License](https://img.shields.io/badge/licença-MIT-yellow?style=flat-square)

---

## Índice

1. [Visão Geral](#visão-geral)
2. [Recursos](#recursos)
3. [Hardware Necessário](#hardware-necessário)
4. [Mapa de Pinos](#mapa-de-pinos)
5. [Compilação &amp; Gravação](#compilação--gravação)
6. [Execução no BitDogLab](#execução-no-bitdoglab)
7. [Operação](#operação)
8. [Personalização](#personalização)
9. [Licença](#licença)

---

## Visão Geral

**Estação de Monitoramento de Enchentes** é um sistema de alerta em tempo‑real baseado no **Raspberry Pi Pico** executando **FreeRTOS**.Dois potenciômetros simulam os sensores de **nível da água** e **volume de chuva**; feedback visual e sonoro é fornecido via LED RGB, matriz WS2812 (5×5), display OLED (SSD1306) e buzzer piezoelétrico.

> Para implantação real basta substituir os potenciômetros por sensores de nível (ex.: ultrassônico) e pluviômetro de balde basculante.

---

## Recursos

- Multitarefa **preemptiva** (7 tasks) com prioridades bem definidas.
- Comunicação entre tasks por **filas** (`xQueueOverwrite`), livre de bloqueios.
- Display OLED **128 × 64** com interface amigável em português.
- **Matriz WS2812** exibindo ondas no modo normal e sinal de alerta piscante.
- **Buzzer não‑bloqueante** com frequência configurável.
- Código totalmente **stand‑alone**: sem dependências externas após gravação.
- Compatível com o simulador **BitDogLab** e com hardware real.

---

## Hardware Necessário

| Qtde | Componente                          | Observações           |
| ---: | ----------------------------------- | ----------------------- |
|    1 | Raspberry Pi Pico                 | RP2040 @ 133 MHz      |
|    1 | Display OLED SSD1306 I²C 128×64 | 3,3 V                  |
|    1 | Matriz WS2812 5×5                 | 5 V                    |
|    2 | Potenciômetro 10 kΩ             | Simula sensores         |
|    1 | LED RGB Cátodo Comum              | Resistores ≈ 220 Ω |
|    1 | Buzzer piezo (ativo ou passivo)    | —                      |
|   — | Jumpers & Protoboard                | Montagem                |

---

## Mapa de Pinos

| Função           | Pico                   | Notas     |
| ------------------ | ---------------------- | --------- |
| I²C SDA (OLED)   | **GP14**         | I²C1     |
| I²C SCL (OLED)   | **GP15**         | I²C1     |
| ADC Nível Água | **GP26 / ADC0**  | POT 1    |
| ADC Volume Chuva | **GP27 / ADC1**  | POT 2    |
| LED RGB — R    | **GP13 / PWM7A** | PWM       |
| LED RGB — G    | **GP11**         | Digital   |
| LED RGB — B    | **GP12 / PWM6B** | PWM       |
| WS2812 DIN        | **GP7**          | PIO0‑SM0 |
| Buzzer             | **GP10 / PWM5A** | PWM       |

---

## Compilação & Gravação

> Requisitos: **Pico‑SDK ≥ 1.5**, **FreeRTOS Kernel**, CMake, ARM GCC e **VS Code** com a extensão *Raspberry Pi Pico*.

### Via CLI

```bash
git clone https://github.com/raspberrypi/pico-sdk
git clone https://github.com/FreeRTOS/FreeRTOS-Kernel freertos
export PICO_SDK_PATH=$PWD/pico-sdk

mkdir build && cd build
cmake -DPICO_BOARD=pico -DFREERTOS_KERNEL_PATH=../freertos ..
make -j

# Gravação (UF2)
cp flood_station.uf2 /media/$USER/RPI-RP2
```

### Via VS Code

1. Instale a extensão **Raspberry Pi Pico**.
2. `F1 → Pico: Configure Project` e escolha `flood_station`.
3. `Ctrl+Shift+B` para compilar.
4. `F1 → Pico: Flash (UF2)` para gravar.

---

## Execução no BitDogLab

1. Abra o BitDogLab e escolha **“RP2040 + Peripheral Playground”**.
2. Arraste o `flood_station.uf2` para o Pico virtual.
3. Mapeie os periféricos:
   * OLED → I²C1
   * Matriz WS2812 → GPIO 7
   * LED RGB e Buzzer → pinos correspondentes
4. Ajuste os potenciômetros virtuais para testar cenários de enchente.

![bitdoglab](docs/bitdoglab_mapping.png)

---

## Operação

| Modo             | Visual                           | Som            | Display                          |
| ---------------- | -------------------------------- | -------------- | -------------------------------- |
| **Normal** | LED verde, ondas azuis na matriz | —             | “Status: NORMAL”               |
| **Alerta** | LED e matriz vermelhos piscando  | 1 kHz pulsado | “***ALERTA***” + causa |

Os limiares podem ser alterados em `main.c`:

```c
#define LIMIAR_NIVEL_AGUA   70.0f   // %
#define LIMIAR_VOLUME_CHUVA 80.0f   // %
```

---

## Personalização

- **Sensores reais:** HC‑SR04 (nível) + pluviômetro (chuva).
- **Conectividade:** LoRaWAN ou Wi‑Fi para painéis remotos.
- **Registro de dados:** micro‑SD com FatFS.
- **Energia autônoma:** bateria Li‑ion + solar.
- **UI gráfica:** bitmaps em `lib/bitmap.h`.

---

## Licença

Distribuído sob a **Licença MIT** – consulte o arquivo [`LICENSE`](LICENSE).

<br>

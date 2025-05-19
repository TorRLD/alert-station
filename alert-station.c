/*
 * Estação de Monitoramento de Enchentes com FreeRTOS
 * 
 * Sistema de alerta para monitoramento de nível de água e volume de chuva
 * usando Raspberry Pi Pico com FreeRTOS e comunicação por filas.
 * 
 * 
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/timer.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"

// Bibliotecas do display OLED 
#include "lib/ssd1306.h"    // Biblioteca do SSD1306
#include "lib/font.h"       // Fonte para o OLED

// Biblioteca do FreeRTOS
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// Biblioteca para Matriz RGB 
#include "ws2812.pio.h"

//===============================================
// Configurações dos pinos 
//===============================================

// OLED via I2C
const uint8_t SDA = 14;
const uint8_t SCL = 15;
#define I2C_ADDR 0x3C
#define I2C_PORT i2c1
#define SSD1306_WIDTH 128
#define SSD1306_HEIGHT 64

// Potenciômetros (simulando sensores)
#define POT_NIVEL_PIN 26      // Simula sensor de nível de água
#define POT_VOLUME_PIN 27     // Simula sensor de volume de chuva

// LEDs RGB (PWM)
#define R_LED_PIN 13
#define G_LED_PIN 11
#define B_LED_PIN 12
#define PWM_WRAP 255

// Buzzer
#define BUZZER_PIN 10

// Matriz WS2812
#define NUM_PIXELS 25
#define WS2812_PIN 7
#define IS_RGBW false

//===============================================
// Configurações de tempo
//===============================================
#define TEMPO_ATUALIZACAO_SENSORES 100
#define TEMPO_ATUALIZACAO_DISPLAY 500
#define TEMPO_ANIMACAO_MATRIZ 300
#define TEMPO_BUZZER_ON 500
#define TEMPO_BUZZER_OFF 500

//===============================================
// Limiares de alerta
//===============================================
#define LIMIAR_NIVEL_AGUA 70.0f   // 70%
#define LIMIAR_VOLUME_CHUVA 80.0f // 80%

//===============================================
// Cores para a matriz WS2812
//===============================================
#define COR_WS2812_R 10
#define COR_WS2812_G 20
#define COR_WS2812_B 10

//===============================================
// Estados e estruturas
//===============================================

// Estados do sistema
typedef enum {
    MODO_NORMAL,
    MODO_ALERTA
} ModoSistema;

// Estrutura de dados dos sensores
typedef struct {
    float nivel_agua;
    float volume_chuva;
    ModoSistema modo;
    uint32_t timestamp;
} DadosSensores;

// Estrutura para controle de atuadores
typedef struct {
    uint8_t r, g, b;
    bool buzzer_ativo;
    uint32_t freq_buzzer;
    bool matriz_alerta;
} ControleAtuadores;

//===============================================
// Variáveis globais
//===============================================

// Matriz de pixels 
bool buffer_leds[NUM_PIXELS] = {false};

// Display OLED
ssd1306_t display;

// Estado do buzzer
volatile bool estado_buzzer = false;
volatile uint32_t tempo_ultimo_toggle_buzzer = 0;

// Filas FreeRTOS
QueueHandle_t fila_sensores;
QueueHandle_t fila_atuadores;
QueueHandle_t fila_display;

//===============================================
// Padrões visuais para a matriz de LEDs (5x5)
//===============================================

// Padrão normal - ondas de água
const bool padrao_agua[5][5] = {
    {false, true, false, true, false},
    {true, false, true, false, true},
    {false, true, false, true, false},
    {true, false, true, false, true},
    {false, true, false, true, false}
};

// Padrão de alerta - símbolo de perigo
const bool padrao_alerta[5][5] = {
    {false, false, true, false, false},
    {false, true, true, true, false},
    {true, true, true, true, true},
    {false, true, true, true, false},
    {false, false, true, false, false}
};

//===============================================
// Funções auxiliares 
//===============================================

// Inicializa o LED RGB com PWM 
void init_rgb_led() {
    gpio_set_function(R_LED_PIN, GPIO_FUNC_PWM);
    gpio_set_function(B_LED_PIN, GPIO_FUNC_PWM);
    
    uint slice_r = pwm_gpio_to_slice_num(R_LED_PIN);
    uint slice_b = pwm_gpio_to_slice_num(B_LED_PIN);
    
    pwm_set_wrap(slice_r, PWM_WRAP);
    pwm_set_clkdiv(slice_r, 125.0f);
    pwm_set_enabled(slice_r, true);
    
    if (slice_b != slice_r) {
        pwm_set_wrap(slice_b, PWM_WRAP);
        pwm_set_clkdiv(slice_b, 125.0f);
        pwm_set_enabled(slice_b, true);
    }
    
    // LED Verde como saída digital 
    gpio_init(G_LED_PIN);
    gpio_set_dir(G_LED_PIN, GPIO_OUT);
}

// Define cor do LED RGB 
void set_rgb_color(uint8_t r, uint8_t g, uint8_t b) {
    pwm_set_chan_level(pwm_gpio_to_slice_num(R_LED_PIN), pwm_gpio_to_channel(R_LED_PIN), r);
    gpio_put(G_LED_PIN, g > 10); // Digital on/off baseado na intensidade
    pwm_set_chan_level(pwm_gpio_to_slice_num(B_LED_PIN), pwm_gpio_to_channel(B_LED_PIN), b);
}

// Inicia buzzer 
void iniciar_buzzer(uint32_t freq) {
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
    uint chan = pwm_gpio_to_channel(BUZZER_PIN);
    
    uint32_t clock = 125000000;
    uint32_t divider = 100;
    uint32_t wrap = clock / (divider * freq);
    
    pwm_set_clkdiv(slice_num, divider);
    pwm_set_wrap(slice_num, wrap);
    pwm_set_chan_level(slice_num, chan, wrap / 2);
    pwm_set_enabled(slice_num, true);
    estado_buzzer = true;
}

// Para buzzer 
void parar_buzzer() {
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
    pwm_set_enabled(slice_num, false);
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_SIO);
    gpio_put(BUZZER_PIN, 0);
    estado_buzzer = false;
}

//===============================================
// Funções da matriz de LEDs 
//===============================================

// Função auxiliar para formatar cores para a matriz de LEDs
static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 8) | ((uint32_t)g << 16) | (uint32_t)b;
}

// Função auxiliar para enviar um pixel para a matriz
static inline void enviar_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

// Define os LEDs da matriz com base no buffer
void definir_leds(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t cor = urgb_u32(r, g, b);
    for (int i = 0; i < NUM_PIXELS; i++) {
        if (buffer_leds[i])
            enviar_pixel(cor);
        else
            enviar_pixel(0);
    }
    sleep_us(60);
}

// Atualiza o buffer com um padrão específico
void atualizar_buffer_matriz(const bool padrão[5][5]) {
    for (int linha = 0; linha < 5; linha++) {
        for (int coluna = 0; coluna < 5; coluna++) {
            int indice = linha * 5 + coluna;
            buffer_leds[indice] = padrão[linha][coluna];
        }
    }
}

//===============================================
// Inicialização do display (usando biblioteca ssd1306)
//===============================================

void init_display() {
    // Inicialização do I2C 
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(SDA, GPIO_FUNC_I2C);
    gpio_set_function(SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SDA);
    gpio_pull_up(SCL);
    
    // Inicialização do display usando a biblioteca ssd1306
    ssd1306_init(&display, SSD1306_WIDTH, SSD1306_HEIGHT, false, I2C_ADDR, I2C_PORT);
    ssd1306_config(&display);
    ssd1306_fill(&display, 0);
    ssd1306_send_data(&display);
}

//===============================================
// Tasks FreeRTOS
//===============================================

// Task para leitura de sensores
void vSensorTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(TEMPO_ATUALIZACAO_SENSORES);
    
    while (1) {
        DadosSensores dados;
        
        // Lê nível da água (potenciômetro 1)
        adc_select_input(POT_NIVEL_PIN - 26);
        uint16_t adc_nivel = adc_read();
        dados.nivel_agua = (adc_nivel / 4095.0f) * 100.0f;
        
        // Lê volume de chuva (potenciômetro 2)
        adc_select_input(POT_VOLUME_PIN - 26);
        uint16_t adc_volume = adc_read();
        dados.volume_chuva = (adc_volume / 4095.0f) * 100.0f;
        
        // Determina modo do sistema
        if (dados.nivel_agua >= LIMIAR_NIVEL_AGUA || dados.volume_chuva >= LIMIAR_VOLUME_CHUVA) {
            dados.modo = MODO_ALERTA;
        } else {
            dados.modo = MODO_NORMAL;
        }
        
        dados.timestamp = to_ms_since_boot(get_absolute_time());
        
        // Envia para fila
        xQueueOverwrite(fila_sensores, &dados);
        
        printf("Sensores - Nivel: %.1f%%, Chuva: %.1f%%, Modo: %s\n", 
               dados.nivel_agua, dados.volume_chuva, 
               dados.modo == MODO_ALERTA ? "ALERTA" : "NORMAL");
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// Task de controle principal
void vControlTask(void *pvParameters) {
    DadosSensores dados_sensores;
    ControleAtuadores controle;
    static ModoSistema modo_anterior = MODO_NORMAL;
    
    while (1) {
        // Recebe dados dos sensores
        if (xQueueReceive(fila_sensores, &dados_sensores, pdMS_TO_TICKS(50)) == pdPASS) {
            
            // Configura atuadores baseado no modo
            if (dados_sensores.modo == MODO_ALERTA) {
                controle.r = 60; controle.g = 0; controle.b = 0; // Vermelho
                controle.buzzer_ativo = true;
                controle.freq_buzzer = 1000; // 1kHz
                controle.matriz_alerta = true;
                
                if (modo_anterior != MODO_ALERTA) {
                    printf("*** ALERTA ATIVADO ***\n");
                    if (dados_sensores.nivel_agua >= LIMIAR_NIVEL_AGUA) {
                        printf("Motivo: Nivel critico (%.1f%%)\n", dados_sensores.nivel_agua);
                    }
                    if (dados_sensores.volume_chuva >= LIMIAR_VOLUME_CHUVA) {
                        printf("Motivo: Chuva intensa (%.1f%%)\n", dados_sensores.volume_chuva);
                    }
                }
            } else {
                controle.r = 0; controle.g = 60; controle.b = 0; // Verde
                controle.buzzer_ativo = false;
                controle.freq_buzzer = 0;
                controle.matriz_alerta = false;
                
                if (modo_anterior == MODO_ALERTA) {
                    printf("*** ALERTA DESATIVADO ***\n");
                }
            }
            
            modo_anterior = dados_sensores.modo;
            
            // Envia controles para as outras tasks
            xQueueOverwrite(fila_atuadores, &controle);
            xQueueOverwrite(fila_display, &dados_sensores);
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Task para controle do buzzer
void vBuzzerTask(void *pvParameters) {
    ControleAtuadores controle;
    
    while (1) {
        if (xQueuePeek(fila_atuadores, &controle, pdMS_TO_TICKS(100)) == pdPASS) {
            uint32_t tempo_atual = to_ms_since_boot(get_absolute_time());
            
            if (controle.buzzer_ativo) {
                // Controle de alternância do buzzer
                if (estado_buzzer) {
                    if (tempo_atual - tempo_ultimo_toggle_buzzer >= TEMPO_BUZZER_ON) {
                        parar_buzzer();
                        tempo_ultimo_toggle_buzzer = tempo_atual;
                        printf("Buzzer OFF\n");
                    }
                } else {
                    if (tempo_atual - tempo_ultimo_toggle_buzzer >= TEMPO_BUZZER_OFF) {
                        iniciar_buzzer(controle.freq_buzzer);
                        tempo_ultimo_toggle_buzzer = tempo_atual;
                        printf("Buzzer ON (%d Hz)\n", controle.freq_buzzer);
                    }
                }
            } else {
                if (estado_buzzer) {
                    parar_buzzer();
                    printf("Buzzer PARADO\n");
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Task para LED RGB
void vLEDTask(void *pvParameters) {
    ControleAtuadores controle;
    
    while (1) {
        if (xQueuePeek(fila_atuadores, &controle, portMAX_DELAY) == pdPASS) {
            set_rgb_color(controle.r, controle.g, controle.b);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Task para matriz de LEDs
void vMatrixTask(void *pvParameters) {
    ControleAtuadores controle;
    static bool animacao_estado = false;
    static uint32_t tempo_ultima_animacao = 0;
    
    while (1) {
        if (xQueuePeek(fila_atuadores, &controle, pdMS_TO_TICKS(100)) == pdPASS) {
            uint32_t tempo_atual = to_ms_since_boot(get_absolute_time());
            
            if (controle.matriz_alerta) {
                // Animação piscante para alerta
                if (tempo_atual - tempo_ultima_animacao >= TEMPO_ANIMACAO_MATRIZ) {
                    animacao_estado = !animacao_estado;
                    tempo_ultima_animacao = tempo_atual;
                    
                    if (animacao_estado) {
                        atualizar_buffer_matriz(padrao_alerta);
                        definir_leds(COR_WS2812_R * 10, 0, 0); // Vermelho intenso
                    } else {
                        // Limpa matriz
                        for (int i = 0; i < NUM_PIXELS; i++) {
                            buffer_leds[i] = false;
                        }
                        definir_leds(0, 0, 0);
                    }
                }
            } else {
                // Padrão normal - ondas de água em azul
                atualizar_buffer_matriz(padrao_agua);
                definir_leds(0, 0, COR_WS2812_B * 10); // Azul
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Task para display (usando biblioteca ssd1306)
void vDisplayTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(TEMPO_ATUALIZACAO_DISPLAY);
    DadosSensores dados;
    
    while (1) {
        if (xQueuePeek(fila_display, &dados, 0) == pdPASS) {
            // Limpa o display
            ssd1306_fill(&display, 0);
            
            // Desenha título e status
            if (dados.modo == MODO_ALERTA) {
                ssd1306_draw_string(&display, "*** ALERTA ***", 5, 0);
                ssd1306_draw_string(&display, "RISCO DE ENCHENTE", 0, 12);
            } else {
                ssd1306_draw_string(&display, "Estacao Monitor", 5, 0);
                ssd1306_draw_string(&display, "Status: NORMAL", 10, 12);
            }
            
            // Desenha dados dos sensores
            char linha1[32], linha2[32];
            sprintf(linha1, "Nivel: %.1f%%", dados.nivel_agua);
            sprintf(linha2, "Chuva: %.1f%%", dados.volume_chuva);
            
            ssd1306_draw_string(&display, linha1, 5, 24);
            ssd1306_draw_string(&display, linha2, 5, 36);
            
            // Desenha alertas específicos
            if (dados.modo == MODO_ALERTA) {
                if (dados.nivel_agua >= LIMIAR_NIVEL_AGUA) {
                    ssd1306_draw_string(&display, "! NIVEL CRITICO !", 0, 48);
                }
                if (dados.volume_chuva >= LIMIAR_VOLUME_CHUVA) {
                    ssd1306_draw_string(&display, "! CHUVA INTENSA !", 0, 48);
                }
            }
            
            // Atualiza o display
            ssd1306_send_data(&display);
            printf("Display atualizado - Nivel: %.1f%%, Chuva: %.1f%%, Modo: %s\n",
                   dados.nivel_agua, dados.volume_chuva,
                   dados.modo == MODO_ALERTA ? "ALERTA" : "NORMAL");
        }
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// Task de inicialização/startup 
void vStartupTask(void *pvParameters) {
    // Tela de inicialização no display
    ssd1306_fill(&display, 0);
    ssd1306_draw_string(&display, "Estacao de", 15, 16);
    ssd1306_draw_string(&display, "Enchentes", 20, 28);
    ssd1306_draw_string(&display, "Inicializando...", 5, 40);
    ssd1306_send_data(&display);
    
    // Sequência sonora de inicialização 
    iniciar_buzzer(523); // Dó
    vTaskDelay(pdMS_TO_TICKS(200));
    parar_buzzer();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    iniciar_buzzer(659); // Mi
    vTaskDelay(pdMS_TO_TICKS(200));
    parar_buzzer();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    iniciar_buzzer(784); // Sol
    vTaskDelay(pdMS_TO_TICKS(200));
    parar_buzzer();
    
    // Inicializa matriz WS2812 via PIO 
    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);
    
    // Efeito visual de inicialização na matriz
    for (int i = 0; i < NUM_PIXELS; i++) {
        buffer_leds[i] = true;
        definir_leds(COR_WS2812_R, COR_WS2812_G, COR_WS2812_B);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    for (int i = 0; i < NUM_PIXELS; i++) {
        buffer_leds[i] = false;
        definir_leds(COR_WS2812_R, COR_WS2812_G, COR_WS2812_B);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // Aguarda um pouco
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Cria as outras tasks
    xTaskCreate(vSensorTask, "Sensor", configMINIMAL_STACK_SIZE * 2, NULL, 3, NULL);
    xTaskCreate(vControlTask, "Control", configMINIMAL_STACK_SIZE * 2, NULL, 2, NULL);
    xTaskCreate(vBuzzerTask, "Buzzer", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(vLEDTask, "LED", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(vMatrixTask, "Matrix", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(vDisplayTask, "Display", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
    
    printf("Sistema inicializado com sucesso!\n");
    
    // Mostra tela final de inicialização
    ssd1306_fill(&display, 0);
    ssd1306_draw_string(&display, "Sistema Ativo", 10, 20);
    ssd1306_draw_string(&display, "Monitorando...", 10, 35);
    ssd1306_send_data(&display);
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Remove a própria task
    vTaskDelete(NULL);
}

//===============================================
// Função principal
//===============================================

int main() {
    stdio_init_all();
    
    // Inicializa os periféricos básicos
    adc_init();
    adc_gpio_init(POT_NIVEL_PIN);
    adc_gpio_init(POT_VOLUME_PIN);
    
    init_rgb_led();
    init_display();
    
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    gpio_put(BUZZER_PIN, 0);
    
    // Cria as filas FreeRTOS
    fila_sensores = xQueueCreate(1, sizeof(DadosSensores));
    fila_atuadores = xQueueCreate(1, sizeof(ControleAtuadores));
    fila_display = xQueueCreate(1, sizeof(DadosSensores));
    
    // Verifica se as filas foram criadas com sucesso
    if (!fila_sensores || !fila_atuadores || !fila_display) {
        printf("ERRO: Falha ao criar filas FreeRTOS!\n");
        
        // Mostra erro no display
        ssd1306_fill(&display, 0);
        ssd1306_draw_string(&display, "ERRO: FILAS", 20, 20);
        ssd1306_draw_string(&display, "Sistema falhou", 5, 35);
        ssd1306_send_data(&display);
        
        while (1) {}
    }
    
    // Cria task de startup
    if (xTaskCreate(vStartupTask, "Startup", configMINIMAL_STACK_SIZE * 2, NULL, 4, NULL) != pdPASS) {
        printf("ERRO: Falha ao criar task de startup!\n");
        while (1) {}
    }
    
    printf("Iniciando sistema de monitoramento de enchentes...\n");
    
    // Inicia o scheduler do FreeRTOS
    vTaskStartScheduler();
    
    // Se chegou aqui, algo deu errado
    printf("ERRO: Falha no scheduler do FreeRTOS!\n");
    while (1) {}
    
    return 0;
}
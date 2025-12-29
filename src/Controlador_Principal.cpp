/*******************************************************************************
 * PROJETO: Controlador Principal - Monta-Prato 2026 (Projeto 1)
 * 
 * FUNÇÃO: MESTRE - VERSÃO: 1.0
 * 
 * DESENVOLVEDOR: Osiris Silva
 * 
 * CARGO: Técnico em Eletrônica
 * 
 * DATA: 25/12/2025
 * 
 * HARDWARE: ESP32 CYD (Cheap Yellow Display)
 * - Modelo: ESP32-2432S028R | Driver: ILI9341
 * - Resolução: 320x240
 * 
 * NOTAS TÉCNICAS:
 * - Configurado via TFT_eSPI (User_Setup.h -> ILI9341).
 * - Gerencia periféricos externos (Projeto 2 - Receptores de Andar).
 *******************************************************************************/

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <cstdint> 
#include "driver/twai.h" // Inclui a biblioteca TWAI nativa do ESP32
#include "../include/Meu_logo_III.h" 

// --- MAPEAMENTO DE PINOS ESP32-2432S028 ---
const int pinBotoes[] = {22, 27, 35};        
const int pinTrincos = 17;                   
const int pinSensorCabina = 25;              
const int motorSobe = 4;                     
const int motorDesce = 16;                   

TFT_eSPI tft = TFT_eSPI();

// --- CONFIGURAÇÃO DE BRILHO (PWM) ---
#define TFT_BL_PIN 21
#define LEDC_CHANNEL_0 0
#define LEDC_RESOLUTION 8 
#define LEDC_FREQUENCY 5000

// --- CONFIGURAÇÃO TWAI (CAN BUS) ---
const gpio_num_t canTxPin = GPIO_NUM_27; 
const gpio_num_t canRxPin = GPIO_NUM_35; 

// --- VARIÁVEIS DE CONTROLE ---
int andarAtual = 1;
int andarDestino = 1;
bool emMovimento = false;
bool subindo = false;
bool detectouPrimeiroIman = false; 
bool sensorUltimoEstado = HIGH;
unsigned long millisPiscar = 0;
unsigned long millisSeta = 0;
bool mostrarNumero = true;
int animSetaY = 0;

// Protótipos de funções
void desenharBackground();
void pararElevador();
void setupCan();
void enviarStatusAndar(int andar);

void desenharBackground() {
    tft.pushImage(0, 0, 320, 240, (uint16_t*)Meu_logo_III); 
}

void setup() {
    Serial.begin(115200);
    tft.init();
    tft.setRotation(1);
    tft.invertDisplay(true); 
    tft.setSwapBytes(true); 
    
    ledcSetup(LEDC_CHANNEL_0, LEDC_FREQUENCY, LEDC_RESOLUTION);
    ledcAttachPin(TFT_BL_PIN, LEDC_CHANNEL_0);
    ledcWrite(LEDC_CHANNEL_0, 150); 
    
    setupCan(); // Inicializa o CAN Bus

    desenharBackground(); 

    for(int i=0; i < sizeof(pinBotoes) / sizeof(int); i++) pinMode(pinBotoes[i], INPUT_PULLUP);
    pinMode(pinTrincos, INPUT_PULLUP);
    pinMode(pinSensorCabina, INPUT_PULLUP);
    pinMode(motorSobe, OUTPUT);
    pinMode(motorDesce, OUTPUT);

    digitalWrite(motorSobe, LOW);
    digitalWrite(motorDesce, LOW);

    tft.setTextColor(TFT_BLUE); 
    tft.drawCentreString("OSIRIS ELETRONICA", 160, 10, 2);
    tft.setTextColor(TFT_WHITE); 
    tft.drawCentreString("MONTA-PRATO 2026", 160, 27, 2);
}

void setupCan() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(canTxPin, canRxPin, TWAI_MODE_NORMAL);
    // Use 125KBIT/s para longa distancia na escala real
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_125KBITS(); 
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        Serial.println("Driver TWAI instalado com sucesso.");
    } else {
        Serial.println("Falha ao instalar driver TWAI.");
        tft.drawCentreString("CAN DRIVER ERROR", 160, 50, 2);
        while(1);
    }

    if (twai_start() == ESP_OK) {
        Serial.println("Driver TWAI iniciado.");
    } else {
        Serial.println("Falha ao iniciar driver TWAI.");
        while(1);
    }
}

void enviarStatusAndar(int andar) {
    twai_message_t message;
    message.identifier = 0x100; // ID da mensagem
    message.flags = TWAI_MSG_FLAG_NONE;
    message.data_length_code = 1; // 1 byte de dados
    message.data[0] = (uint8_t)andar; // O número do andar

    if (twai_transmit(&message, pdMS_TO_TICKS(100)) == ESP_OK) {
        Serial.println("Mensagem CAN enviada");
    } else {
        Serial.println("Falha ao enviar mensagem CAN");
    }
}


void pararElevador() {
    digitalWrite(motorSobe, LOW);
    digitalWrite(motorDesce, LOW);
    emMovimento = false;
    detectouPrimeiroIman = false;
    desenharBackground(); 
    enviarStatusAndar(andarAtual); // Envia o andar atualizado via CAN
}

void loop() {
    unsigned long agora = millis();

    if (digitalRead(pinTrincos) == HIGH) {
        if (emMovimento) {
            pararElevador();
        }
        tft.setTextColor(TFT_RED, TFT_BLACK); 
        tft.drawCentreString("PORTA ABERTA - BLOQUEADO", 160, 210, 2);
        return; 
    } else {
         tft.fillRect(0, 210, 320, 25, TFT_BLACK); 
    }

    if (!emMovimento) {
        if (agora - millisPiscar >= 800) {
            millisPiscar = agora;
            mostrarNumero = !mostrarNumero;
            if (mostrarNumero) {
                tft.setTextColor(TFT_GREEN); 
                tft.setTextSize(12);
                tft.drawNumber(andarAtual, 125, 60);
            } else {
                tft.pushImage(125, 60, 100, 100, (uint16_t*)Meu_logo_III); 
            }
        }
        for (int i = 0; i < sizeof(pinBotoes) / sizeof(int); i++) { 
            if (digitalRead(pinBotoes[i]) == LOW) {
                int desejado = i + 1;
                if (desejado != andarAtual) {
                    andarDestino = desejado;
                    subindo = (andarDestino > andarAtual);
                    emMovimento = true;
                    detectouPrimeiroIman = false;
                    desenharBackground(); 
                    tft.setTextColor(TFT_WHITE);
                    tft.drawCentreString("EM MOVIMENTO", 160, 10, 2);
                }
            }
        }
    }

    if (emMovimento) {
        digitalWrite(motorSobe, subindo ? HIGH : LOW);
        digitalWrite(motorDesce, subindo ? LOW : HIGH);
        tft.setTextColor(TFT_YELLOW);
        tft.setTextSize(12);
        tft.drawNumber(andarAtual, 125, 60);

        if (agora - millisSeta >= 150) {
            millisSeta = agora;
            tft.pushImage(10, 50, 60, 140, (uint16_t*)Meu_logo_III); 
            tft.setTextColor(TFT_CYAN);
            tft.setTextSize(5);

            animSetaY = (animSetaY + (subindo ? -15 : 15));
            if (abs(animSetaY) > 45) animSetaY = 0;

            tft.drawChar(subindo ? '^' : 'v', 20, 90 + animSetaY);
            tft.drawChar(subindo ? '^' : 'v', 20, 130 + animSetaY);
        }

        bool leituraSensor = (digitalRead(pinSensorCabina) == LOW);
        if (leituraSensor && sensorUltimoEstado == HIGH) {
            if (!detectouPrimeiroIman) {
                detectouPrimeiroIman = true;
            } else {
                if (subindo) andarAtual++; else andarAtual--;
                detectouPrimeiroIman = false; 

                if (andarAtual == andarDestino) {
                    pararElevador(); 
                } else {
                    enviarStatusAndar(andarAtual); // Envia o andar intermediário via CAN
                }
            }
            delay(200); 
        }
        sensorUltimoEstado = leituraSensor;
    }
}

// ============================================================================
// ESP32 Toslink LED - Bluetooth A2DP + TFT Display
// KenWoody - O Príncipe (Ken + Woody mashup)
// PIX QR Code fixo na tela
// + Sistema de Monitoramento de Sinais para Kenwood A-G9
// ============================================================================

#include "ESP_I2S.h"
#include "BluetoothA2DPSink.h"
#include <TFT_eSPI.h>
#include <SPI.h>
#include "qrcode.h"

// ---- Configuração de Hardware ----
const uint8_t I2S_SDOUT = 22;  // RESERVADO - Saída I2S para Toslink/LED
const uint8_t I2S_SCK   = 14;  // RESERVADO - I2S Clock
const uint8_t I2S_WS    = 15;  // RESERVADO - I2S Word Select

// ============================================================================
// PINOS DE MONITORAMENTO - Kenwood A-G9
// ============================================================================
// Usamos pinos ADC1 (GPIO 32-39) porque ADC2 conflita com WiFi/BT
// GPIO 34-39 são somente entrada (input-only), perfeitos para monitorar
//
// ⚠️  IMPORTANTE: O ESP32 suporta no MÁXIMO 3.3V nos pinos!
//     Se o Kenwood usar 5V ou mais, você PRECISA de um divisor de tensão:
//
//     Sinal Kenwood ──[R1=10kΩ]──┬──[R2=10kΩ]── GND
//                                │
//                            GPIO do ESP32
//
//     Com R1=R2=10kΩ: Vinput = Vsinal / 2
//     (5V vira 2.5V, 12V vira 6V → para 12V use R1=22kΩ, R2=10kΩ)
//
// Proteção extra: coloque um diodo zener de 3.3V entre o GPIO e GND
// ============================================================================

// Canais de monitoramento (pinos ADC1, input-only)
#define NUM_PROBE_CHANNELS 6

const uint8_t probePins[NUM_PROBE_CHANNELS] = {
    34,   // PROBE 1 - Pino somente entrada, ADC1_CH6
    35,   // PROBE 2 - Pino somente entrada, ADC1_CH7
    36,   // PROBE 3 - Pino somente entrada, ADC1_CH0 (VP)
    39,   // PROBE 4 - Pino somente entrada, ADC1_CH3 (VN)
    32,   // PROBE 5 - ADC1_CH4
    33    // PROBE 6 - ADC1_CH5
};

const char* probeNames[NUM_PROBE_CHANNELS] = {
    "P1:34", "P2:35", "P3:36", "P4:39", "P5:32", "P6:33"
};

// Cores para cada canal no osciloscópio
const uint16_t probeColors[] = {
    0xF800,  // Vermelho
    0x07E0,  // Verde
    0x001F,  // Azul
    0xFFE0,  // Amarelo
    0xF81F,  // Magenta
    0x07FF   // Ciano
};

// ---- Buffers de captura ----
#define CAPTURE_BUFFER_SIZE 280  // Largura útil do gráfico no TFT (320 - margens)
#define MAX_PATTERNS 20         // Máximo de padrões gravados

// Buffer circular para cada canal
int captureBuffer[NUM_PROBE_CHANNELS][CAPTURE_BUFFER_SIZE];
int captureIndex = 0;
bool captureRunning = false;

// Detecção de pulsos
volatile unsigned long lastPulseTime[NUM_PROBE_CHANNELS] = {0};
volatile unsigned long pulseCount[NUM_PROBE_CHANNELS] = {0};
volatile int lastPulseState[NUM_PROBE_CHANNELS] = {0};

// Limiares de detecção (ADC 12-bit: 0-4095)
int triggerThreshold = 2048;  // Meio da escala = ~1.65V
int noiseFloor = 100;         // Abaixo disso é ruído

// ---- Padrões gravados para mapeamento ----
struct SignalPattern {
    char name[20];           // Nome do comando (ex: "VOL_UP", "INPUT_CD")
    int channel;             // Canal que gerou o padrão
    int peakValue;           // Valor de pico do sinal
    unsigned long duration;  // Duração do pulso em microsegundos
    unsigned long frequency; // Frequência em Hz (se periódico)
    bool active;             // Se este slot está em uso
};

SignalPattern patterns[MAX_PATTERNS];
int patternCount = 0;

// ---- Modos de operação ----
enum AppMode {
    MODE_LOGO,        // Tela do logo KenWoody (padrão no boot)
    MODE_PIX,         // Tela PIX com QR Code
    MODE_MONITOR,     // Osciloscópio em tempo real
    MODE_CAPTURE,     // Capturando padrão
    MODE_MAPPING,     // Visualizando padrões mapeados
    MODE_CONTROL      // Enviando comandos ao Kenwood
};

AppMode currentMode = MODE_LOGO;

// Botão para alternar modos (use um botão físico ou toque na tela)
#define MODE_BUTTON_PIN 0   // GPIO 0 = botão BOOT do ESP32

// ---- Objetos globais ----
TFT_eSPI tft = TFT_eSPI();
I2SClass i2s;
BluetoothA2DPSink a2dp_sink(i2s);

// ---- PIX ----
String cfgPIX = "34204935842"; // CPF sem formatação

// ---- Cores customizadas ----
#define DARK_BG         tft.color565(8, 8, 20)
#define MID_GRAY        tft.color565(100, 100, 100)
#define DARK_GRAY       tft.color565(40, 40, 40)
#define PIX_GREEN       tft.color565(0, 200, 100)

// Cores do Ken (Barbie)
#define KEN_HAIR        tft.color565(240, 210, 80)
#define KEN_HAIR_DARK   tft.color565(200, 170, 40)
#define KEN_SKIN        tft.color565(255, 210, 170)
#define KEN_SKIN_SHADE  tft.color565(220, 175, 140)
#define KEN_PINK        tft.color565(255, 105, 180)
#define KEN_PINK_DARK   tft.color565(200, 60, 130)
#define KEN_BLUE_EYE    tft.color565(60, 130, 230)

// Cores do Woody (Toy Story)
#define WOODY_HAT       tft.color565(139, 90, 43)
#define WOODY_HAT_DARK  tft.color565(100, 60, 25)
#define WOODY_HAT_BAND  tft.color565(180, 120, 60)
#define WOODY_YELLOW    tft.color565(255, 220, 80)
#define WOODY_RED       tft.color565(200, 40, 30)
#define WOODY_VEST      tft.color565(110, 70, 35)
#define WOODY_BADGE     tft.color565(255, 215, 0)

// Cores do Príncipe
#define CROWN_GOLD      tft.color565(255, 200, 0)
#define CROWN_JEWEL     tft.color565(220, 20, 60)
#define CROWN_JEWEL2    tft.color565(0, 100, 255)
#define CAPE_PURPLE     tft.color565(120, 40, 160)
#define CAPE_LIGHT      tft.color565(160, 80, 200)

// Cores do osciloscópio
#define SCOPE_BG        tft.color565(0, 5, 0)
#define SCOPE_GRID      tft.color565(0, 30, 0)
#define SCOPE_TEXT       tft.color565(0, 200, 0)
#define SCOPE_TRIGGER    tft.color565(200, 200, 0)

// ============================================================================
// DESENHO DO KENWOODY - O PRÍNCIPE (Ken + Woody pixel art)
// ============================================================================

void drawKenWoody(int x, int y, int scale) {
    int s = scale;
    
    // --- COROA DOURADA ---
    for (int i = 8; i <= 22; i++) {
        tft.fillRect(x + i*s, y + 0*s, s, s, CROWN_GOLD);
    }
    tft.fillRect(x + 9*s,  y - 1*s, s, s, CROWN_GOLD);
    tft.fillRect(x + 10*s, y - 2*s, s, s, CROWN_GOLD);
    tft.fillRect(x + 10*s, y - 1*s, s, s, CROWN_GOLD);
    tft.fillRect(x + 15*s, y - 2*s, s, s, CROWN_GOLD);
    tft.fillRect(x + 15*s, y - 1*s, s, s, CROWN_GOLD);
    tft.fillRect(x + 20*s, y - 1*s, s, s, CROWN_GOLD);
    tft.fillRect(x + 21*s, y - 2*s, s, s, CROWN_GOLD);
    tft.fillRect(x + 21*s, y - 1*s, s, s, CROWN_GOLD);
    // Joias
    tft.fillRect(x + 12*s, y + 0*s, s, s, CROWN_JEWEL);
    tft.fillRect(x + 15*s, y + 0*s, s, s, CROWN_JEWEL2);
    tft.fillRect(x + 18*s, y + 0*s, s, s, CROWN_JEWEL);
    
    // --- CHAPÉU DO WOODY ---
    for (int i = 5; i <= 25; i++) {
        tft.fillRect(x + i*s, y + 3*s, s, s, WOODY_HAT);
    }
    for (int row = 1; row <= 2; row++) {
        for (int i = 9; i <= 21; i++) {
            tft.fillRect(x + i*s, y + row*s, s, s, WOODY_HAT);
        }
    }
    for (int i = 9; i <= 21; i++) {
        tft.fillRect(x + i*s, y + 2*s, s, s, WOODY_HAT_BAND);
    }
    for (int i = 5; i <= 8; i++) tft.fillRect(x + i*s, y + 3*s, s, s, WOODY_HAT_DARK);
    for (int i = 22; i <= 25; i++) tft.fillRect(x + i*s, y + 3*s, s, s, WOODY_HAT_DARK);
    
    // --- CABELO LOIRO ---
    for (int i = 8; i <= 22; i++) tft.fillRect(x + i*s, y + 4*s, s, s, KEN_HAIR);
    for (int row = 4; row <= 7; row++) {
        tft.fillRect(x + 7*s, y + row*s, s, s, KEN_HAIR);
        tft.fillRect(x + 23*s, y + row*s, s, s, KEN_HAIR);
    }
    tft.fillRect(x + 6*s, y + 5*s, s, s, KEN_HAIR_DARK);
    tft.fillRect(x + 24*s, y + 5*s, s, s, KEN_HAIR_DARK);
    
    // --- ROSTO ---
    for (int row = 5; row <= 10; row++) {
        for (int i = 8; i <= 22; i++) tft.fillRect(x + i*s, y + row*s, s, s, KEN_SKIN);
    }
    for (int row = 6; row <= 9; row++) {
        tft.fillRect(x + 8*s, y + row*s, s, s, KEN_SKIN_SHADE);
        tft.fillRect(x + 22*s, y + row*s, s, s, KEN_SKIN_SHADE);
    }
    // Olhos
    tft.fillRect(x + 11*s, y + 6*s, s*2, s*2, TFT_WHITE);
    tft.fillRect(x + 18*s, y + 6*s, s*2, s*2, TFT_WHITE);
    tft.fillRect(x + 12*s, y + 7*s, s, s, KEN_BLUE_EYE);
    tft.fillRect(x + 19*s, y + 7*s, s, s, KEN_BLUE_EYE);
    tft.fillRect(x + 12*s, y + 7*s, s/2, s/2, TFT_BLACK);
    tft.fillRect(x + 19*s, y + 7*s, s/2, s/2, TFT_BLACK);
    // Sobrancelhas
    for (int i = 11; i <= 13; i++) tft.fillRect(x + i*s, y + 5*s, s, s/2, KEN_HAIR_DARK);
    for (int i = 18; i <= 20; i++) tft.fillRect(x + i*s, y + 5*s, s, s/2, KEN_HAIR_DARK);
    // Sorriso
    tft.fillRect(x + 13*s, y + 9*s, s, s/2, tft.color565(200, 80, 80));
    tft.fillRect(x + 14*s, y + 9*s, s, s, tft.color565(200, 80, 80));
    tft.fillRect(x + 15*s, y + 9*s + s/2, s, s/2, tft.color565(200, 80, 80));
    tft.fillRect(x + 16*s, y + 9*s, s, s, tft.color565(200, 80, 80));
    tft.fillRect(x + 17*s, y + 9*s, s, s/2, tft.color565(200, 80, 80));
    tft.fillRect(x + 15*s, y + 8*s, s, s/2, KEN_SKIN_SHADE);
    
    // --- PESCOÇO ---
    for (int i = 13; i <= 17; i++) tft.fillRect(x + i*s, y + 11*s, s, s, KEN_SKIN);
    
    // --- LENÇO VERMELHO ---
    for (int i = 10; i <= 20; i++) tft.fillRect(x + i*s, y + 12*s, s, s, WOODY_RED);
    tft.fillRect(x + 14*s, y + 13*s, s*3, s, WOODY_RED);
    tft.fillRect(x + 15*s, y + 14*s, s, s, WOODY_RED);
    
    // --- CAMISA XADREZ ---
    for (int row = 13; row <= 22; row++) {
        for (int i = 9; i <= 21; i++) {
            bool isRed = ((i + row) % 3 == 0);
            tft.fillRect(x + i*s, y + row*s, s, s, isRed ? WOODY_RED : WOODY_YELLOW);
        }
    }
    for (int i = 10; i <= 20; i++) tft.fillRect(x + i*s, y + 12*s, s, s, WOODY_RED);
    
    // --- COLETE ---
    for (int row = 14; row <= 22; row++) {
        tft.fillRect(x + 9*s, y + row*s, s, s, WOODY_VEST);
        tft.fillRect(x + 10*s, y + row*s, s, s, WOODY_VEST);
        tft.fillRect(x + 20*s, y + row*s, s, s, WOODY_VEST);
        tft.fillRect(x + 21*s, y + row*s, s, s, WOODY_VEST);
    }
    
    // --- ESTRELA XERIFE ---
    tft.fillRect(x + 14*s, y + 15*s, s*3, s, WOODY_BADGE);
    tft.fillRect(x + 15*s, y + 14*s, s, s*3, WOODY_BADGE);
    
    // --- CAPA ROXA ---
    for (int row = 12; row <= 24; row++) {
        tft.fillRect(x + 6*s, y + row*s, s, s, CAPE_PURPLE);
        tft.fillRect(x + 7*s, y + row*s, s, s, CAPE_LIGHT);
        tft.fillRect(x + 8*s, y + row*s, s, s, CAPE_PURPLE);
        tft.fillRect(x + 22*s, y + row*s, s, s, CAPE_PURPLE);
        tft.fillRect(x + 23*s, y + row*s, s, s, CAPE_LIGHT);
        tft.fillRect(x + 24*s, y + row*s, s, s, CAPE_PURPLE);
    }
    for (int row = 22; row <= 26; row++) {
        tft.fillRect(x + 5*s, y + row*s, s, s, CAPE_PURPLE);
        tft.fillRect(x + 25*s, y + row*s, s, s, CAPE_PURPLE);
    }
    for (int i = 5; i <= 25; i++) tft.fillRect(x + i*s, y + 26*s, s, s, CAPE_PURPLE);
    
    // --- CINTO ---
    for (int i = 9; i <= 21; i++) tft.fillRect(x + i*s, y + 22*s, s, s, WOODY_HAT_DARK);
    tft.fillRect(x + 14*s, y + 22*s, s*3, s, CROWN_GOLD);
    
    // --- BRAÇOS ---
    for (int row = 14; row <= 20; row++) {
        tft.fillRect(x + 7*s, y + row*s, s*2, s, WOODY_YELLOW);
        tft.fillRect(x + 22*s, y + row*s, s*2, s, WOODY_YELLOW);
    }
    tft.fillRect(x + 6*s, y + 20*s, s*2, s*2, KEN_SKIN);
    tft.fillRect(x + 23*s, y + 20*s, s*2, s*2, KEN_SKIN);
    
    // --- CALÇA JEANS ---
    for (int row = 23; row <= 28; row++) {
        for (int i = 11; i <= 14; i++) tft.fillRect(x + i*s, y + row*s, s, s, tft.color565(50, 80, 160));
        for (int i = 16; i <= 19; i++) tft.fillRect(x + i*s, y + row*s, s, s, tft.color565(50, 80, 160));
    }
    
    // --- BOTAS ---
    for (int i = 10; i <= 14; i++) { tft.fillRect(x + i*s, y + 29*s, s, s*2, WOODY_HAT_DARK); }
    for (int i = 16; i <= 20; i++) { tft.fillRect(x + i*s, y + 29*s, s, s*2, WOODY_HAT_DARK); }
    for (int i = 9; i <= 14; i++) tft.fillRect(x + i*s, y + 31*s, s, s, TFT_BLACK);
    for (int i = 16; i <= 21; i++) tft.fillRect(x + i*s, y + 31*s, s, s, TFT_BLACK);
}

// ============================================================================
// ANIMAÇÃO DE BOOT
// ============================================================================

void playKenWoodyAnimation() {
    int centerX = tft.width() / 2;
    int centerY = tft.height() / 2;
    
    tft.fillScreen(TFT_BLACK);
    delay(300);
    
    // Estrelas
    for (int i = 0; i < 60; i++) {
        int sx = random(0, tft.width());
        int sy = random(0, tft.height());
        uint8_t brightness = random(40, 200);
        tft.fillRect(sx, sy, 2, 2, tft.color565(brightness, brightness, brightness/2));
        delay(20);
    }
    delay(400);
    
    // Personagem revelado linha por linha
    int charX = centerX - 15 * 4;
    int charY = 15;
    int charScale = 4;
    
    for (int revealRow = 0; revealRow <= 32; revealRow += 2) {
        tft.fillRect(charX, charY + revealRow * charScale, 30 * charScale, charScale * 2, TFT_BLACK);
        drawKenWoody(charX, charY, charScale);
        if (revealRow < 30) {
            tft.fillRect(charX - 5, charY + (revealRow + 2) * charScale,
                         30 * charScale + 10, (32 - revealRow) * charScale, TFT_BLACK);
        }
        delay(50);
    }
    drawKenWoody(charX, charY, charScale);
    delay(500);
    
    // Nome letra por letra
    int textY = charY + 33 * charScale + 5;
    const char* part1 = "Ken";
    int startX1 = centerX - 75;
    char singleChar[2] = {0, 0};
    
    for (int i = 0; i < 3; i++) {
        singleChar[0] = part1[i];
        int letterX = startX1 + i * 26;
        for (int flash = 0; flash < 3; flash++) {
            tft.setTextColor(tft.color565(255, 50 + flash*40, 100 + flash*30), TFT_BLACK);
            tft.drawString(singleChar, letterX, textY, 4);
            delay(40);
        }
        tft.setTextColor(KEN_PINK, TFT_BLACK);
        tft.drawString(singleChar, letterX, textY, 4);
        delay(80);
    }
    
    const char* part2 = "Woody";
    int startX2 = startX1 + 3 * 26;
    for (int i = 0; i < 5; i++) {
        singleChar[0] = part2[i];
        int letterX = startX2 + i * 22;
        for (int flash = 0; flash < 3; flash++) {
            tft.setTextColor(tft.color565(139 + flash*20, 90 + flash*10, 43), TFT_BLACK);
            tft.drawString(singleChar, letterX, textY, 4);
            delay(40);
        }
        tft.setTextColor(WOODY_HAT, TFT_BLACK);
        tft.drawString(singleChar, letterX, textY, 4);
        delay(80);
    }
    delay(300);
    
    // "O Príncipe"
    int subY = textY + 28;
    for (int bright = 0; bright <= 255; bright += 20) {
        uint16_t fadeGold = tft.color565(bright, (bright * 200) / 255, 0);
        tft.setTextColor(fadeGold, TFT_BLACK);
        tft.drawCentreString("O Principe", centerX, subY, 2);
        delay(25);
    }
    tft.setTextColor(CROWN_GOLD, TFT_BLACK);
    tft.drawCentreString("O Principe", centerX, subY, 2);
    
    for (int spark = 0; spark < 15; spark++) {
        int sparkX = centerX + random(-100, 100);
        int sparkY = subY + random(-20, 30);
        tft.fillRect(sparkX, sparkY, 3, 3, CROWN_GOLD);
        delay(40);
        tft.fillRect(sparkX, sparkY, 3, 3, TFT_BLACK);
    }
    delay(1500);
    
    // Fade out cortina
    for (int curtain = 0; curtain < tft.height() / 2; curtain += 3) {
        tft.drawFastHLine(0, curtain, tft.width(), TFT_BLACK);
        tft.drawFastHLine(0, tft.height() - 1 - curtain, tft.width(), TFT_BLACK);
        delay(8);
    }
    tft.fillScreen(TFT_BLACK);
    delay(300);
}

// ============================================================================
// TELA DO LOGO KENWOODY (ESTÁTICA - sem animação)
// ============================================================================

void drawLogoScreen() {
    tft.fillScreen(TFT_BLACK);
    
    int centerX = tft.width() / 2;
    
    // Personagem KenWoody grande centralizado
    int charX = centerX - 15 * 4; // escala 4
    int charY = 8;
    drawKenWoody(charX, charY, 4);
    
    // Nome "KenWoody"
    int textY = charY + 33 * 4 + 5;
    
    // "Ken" em rosa
    tft.setTextColor(KEN_PINK, TFT_BLACK);
    tft.drawString("Ken", centerX - 75, textY, 4);
    
    // "Woody" em marrom
    tft.setTextColor(WOODY_HAT, TFT_BLACK);
    tft.drawString("Woody", centerX - 75 + 3 * 26, textY, 4);
    
    // "O Príncipe" em dourado
    tft.setTextColor(CROWN_GOLD, TFT_BLACK);
    tft.drawCentreString("O Principe", centerX, textY + 28, 2);
    
    // Rodapé
    tft.setTextColor(MID_GRAY, TFT_BLACK);
    tft.drawCentreString("Serial: PIX | LOGO | HELP", tft.width()/2, tft.height() - 16, 1);
}

// ============================================================================
// TELA PIX - QR CODE
// ============================================================================

void drawPixScreen() {
    tft.fillScreen(TFT_BLACK);
    
    QRCode qrcode;
    uint8_t qData[qrcode_getBufferSize(4)];
    qrcode_initText(&qrcode, qData, 4, 2, cfgPIX.c_str());

    int esc = 5;
    int qSize = qrcode.size * esc;
    int xOff = (tft.width() - qSize) / 2;
    int yOff = 8;

    tft.setTextColor(PIX_GREEN, TFT_BLACK);
    tft.drawCentreString("PIX", xOff - 25, yOff + qSize/2 - 8, 2);
    
    tft.drawRect(xOff - 6, yOff - 6, qSize + 12, qSize + 12, PIX_GREEN);
    tft.drawRect(xOff - 5, yOff - 5, qSize + 10, qSize + 10, tft.color565(0, 100, 50));
    tft.fillRect(xOff - 3, yOff - 3, qSize + 6, qSize + 6, TFT_WHITE);

    for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                tft.fillRect(xOff + (x * esc), yOff + (y * esc), esc, esc, TFT_BLACK);
            }
        }
    }

    int iconSize = 20;
    int cx = xOff + (qSize / 2) - (iconSize / 2);
    int cy = yOff + (qSize / 2) - (iconSize / 2);
    tft.fillRect(cx - 2, cy - 2, iconSize + 4, iconSize + 4, TFT_WHITE);
    uint16_t pixTeal = tft.color565(32, 191, 163);
    int mid = iconSize / 2;
    for (int row = 0; row < iconSize; row++) {
        int dist = abs(row - mid);
        int startCol = dist;
        int endCol = iconSize - dist;
        if (startCol < endCol) tft.drawFastHLine(cx + startCol, cy + row, endCol - startCol, pixTeal);
    }

    int infoY = yOff + qSize + 10;
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("PAGAR VIA PIX", tft.width()/2, infoY, 2);
    tft.drawFastHLine((tft.width() - 180)/2, infoY + 18, 180, DARK_GRAY);
    tft.setTextColor(tft.color565(180, 180, 180), TFT_BLACK);
    tft.drawCentreString("Chave CPF:", tft.width()/2, infoY + 24, 1);
    
    // Formata o CPF para exibição (se tiver 11 dígitos)
    String cpfDisplay = cfgPIX;
    if (cfgPIX.length() == 11) {
        cpfDisplay = cfgPIX.substring(0,3) + "." + cfgPIX.substring(3,6) + "." + 
                     cfgPIX.substring(6,9) + "-" + cfgPIX.substring(9,11);
    }
    tft.setTextColor(PIX_GREEN, TFT_BLACK);
    tft.drawCentreString(cpfDisplay, tft.width()/2, infoY + 38, 2);
    
    // Mini KenWoody decorativo
    drawKenWoody(tft.width() - 55, infoY + 10, 1);
    
    // Rodapé
    tft.setTextColor(MID_GRAY, TFT_BLACK);
    tft.drawCentreString("Serial: LOGO | PIXC <cpf>", tft.width()/2, tft.height() - 16, 1);
}

void updateBtStatus(bool connected) {
    tft.fillRect(0, tft.height() - 16, tft.width(), 16, TFT_BLACK);
    if (connected) {
        tft.setTextColor(tft.color565(0, 180, 255), TFT_BLACK);
        tft.fillCircle(8, tft.height() - 9, 4, PIX_GREEN);
        tft.drawString("BT OK", 16, tft.height() - 14, 1);
    }
    // Mostra dica do modo atual
    tft.setTextColor(MID_GRAY, TFT_BLACK);
    if (currentMode == MODE_LOGO) {
        tft.drawCentreString("Serial: PIX | HELP", tft.width()/2, tft.height() - 16, 1);
    } else if (currentMode == MODE_PIX) {
        tft.drawCentreString("Serial: LOGO | PIXC <cpf>", tft.width()/2, tft.height() - 16, 1);
    }
}

// ============================================================================
// SISTEMA DE MONITORAMENTO DE SINAIS - OSCILOSCÓPIO
// ============================================================================

// Desenha a grade do osciloscópio
void drawScopeGrid() {
    tft.fillScreen(SCOPE_BG);
    
    // Título
    tft.setTextColor(SCOPE_TEXT, SCOPE_BG);
    tft.drawString("KENWOOD A-G9 Signal Monitor", 5, 2, 1);
    
    // Borda da área do gráfico
    int graphX = 30, graphY = 15, graphW = CAPTURE_BUFFER_SIZE, graphH = 160;
    tft.drawRect(graphX - 1, graphY - 1, graphW + 2, graphH + 2, SCOPE_GRID);
    
    // Grade vertical (divisões de tempo)
    for (int i = 1; i < 10; i++) {
        int lineX = graphX + (graphW * i / 10);
        for (int y = graphY; y < graphY + graphH; y += 4) {
            tft.drawPixel(lineX, y, SCOPE_GRID);
        }
    }
    
    // Grade horizontal (divisões de tensão)
    for (int i = 1; i < 8; i++) {
        int lineY = graphY + (graphH * i / 8);
        for (int x = graphX; x < graphX + graphW; x += 4) {
            tft.drawPixel(x, lineY, SCOPE_GRID);
        }
    }
    
    // Linha de trigger
    int trigY = graphY + graphH - (triggerThreshold * graphH / 4095);
    for (int x = graphX; x < graphX + graphW; x += 2) {
        tft.drawPixel(x, trigY, SCOPE_TRIGGER);
    }
    
    // Escala de tensão (lateral esquerda)
    tft.setTextColor(SCOPE_TEXT, SCOPE_BG);
    tft.drawString("3.3V", 0, graphY, 1);
    tft.drawString("1.6V", 0, graphY + graphH/2 - 4, 1);
    tft.drawString("0.0V", 0, graphY + graphH - 8, 1);
    
    // Legenda dos canais (embaixo)
    int legendY = graphY + graphH + 8;
    for (int ch = 0; ch < NUM_PROBE_CHANNELS; ch++) {
        int legendX = 5 + ch * 52;
        tft.fillRect(legendX, legendY, 8, 8, probeColors[ch]);
        tft.setTextColor(TFT_WHITE, SCOPE_BG);
        tft.drawString(probeNames[ch], legendX + 10, legendY, 1);
    }
    
    // Instruções
    tft.setTextColor(MID_GRAY, SCOPE_BG);
    tft.drawString("[BOOT]=Modo | Serial: comandos", 5, tft.height() - 10, 1);
}

// Atualiza o osciloscópio em tempo real
void updateScope() {
    int graphX = 30, graphY = 15, graphH = 160;
    
    // Lê todos os canais
    for (int ch = 0; ch < NUM_PROBE_CHANNELS; ch++) {
        int rawValue = analogRead(probePins[ch]);
        
        // Limpa pixel anterior nesta coluna
        if (captureIndex > 0) {
            int prevY = graphY + graphH - (captureBuffer[ch][captureIndex] * graphH / 4095);
            if (prevY >= graphY && prevY < graphY + graphH) {
                tft.drawPixel(graphX + captureIndex, prevY, SCOPE_BG);
            }
        }
        
        // Salva no buffer
        captureBuffer[ch][captureIndex] = rawValue;
        
        // Desenha pixel novo
        int plotY = graphY + graphH - (rawValue * graphH / 4095);
        if (plotY >= graphY && plotY < graphY + graphH) {
            tft.drawPixel(graphX + captureIndex, plotY, probeColors[ch]);
        }
        
        // Detecção de pulso (borda de subida)
        if (rawValue > triggerThreshold && lastPulseState[ch] == 0) {
            lastPulseState[ch] = 1;
            unsigned long now = micros();
            if (lastPulseTime[ch] > 0) {
                unsigned long period = now - lastPulseTime[ch];
                // Envia info pelo Serial
                Serial.printf("[CH%d] PULSO! Valor:%d Periodo:%luus Freq:%.1fHz\n",
                              ch, rawValue, period, 1000000.0 / period);
            }
            lastPulseTime[ch] = now;
            pulseCount[ch]++;
        } else if (rawValue < triggerThreshold - noiseFloor) {
            lastPulseState[ch] = 0;
        }
    }
    
    // Avança o índice circular
    captureIndex = (captureIndex + 1) % CAPTURE_BUFFER_SIZE;
    
    // Limpa a próxima coluna (cursor)
    for (int y = graphY; y < graphY + graphH; y++) {
        tft.drawPixel(graphX + captureIndex, y, SCOPE_BG);
    }
    // Desenha cursor
    tft.drawFastVLine(graphX + captureIndex, graphY, graphH, tft.color565(50, 50, 50));
    
    // Atualiza valores numéricos no rodapé (a cada 20 amostras)
    static int updateCounter = 0;
    if (++updateCounter >= 20) {
        updateCounter = 0;
        
        int numY = 195;
        tft.fillRect(0, numY, tft.width(), 12, SCOPE_BG);
        
        for (int ch = 0; ch < NUM_PROBE_CHANNELS; ch++) {
            int rawVal = captureBuffer[ch][(captureIndex - 1 + CAPTURE_BUFFER_SIZE) % CAPTURE_BUFFER_SIZE];
            float voltage = rawVal * 3.3 / 4095.0;
            
            char buf[12];
            snprintf(buf, sizeof(buf), "%.2fV", voltage);
            
            int numX = 5 + ch * 52;
            tft.setTextColor(probeColors[ch], SCOPE_BG);
            tft.drawString(buf, numX, numY, 1);
        }
    }
}

// ============================================================================
// TELA DE MAPEAMENTO DE PADRÕES
// ============================================================================

void drawMappingScreen() {
    tft.fillScreen(DARK_BG);
    
    tft.setTextColor(CROWN_GOLD, DARK_BG);
    tft.drawCentreString("KENWOOD A-G9", tft.width()/2, 5, 2);
    tft.drawCentreString("Padroes Mapeados", tft.width()/2, 22, 2);
    
    tft.drawFastHLine(20, 38, tft.width() - 40, DARK_GRAY);
    
    if (patternCount == 0) {
        tft.setTextColor(MID_GRAY, DARK_BG);
        tft.drawCentreString("Nenhum padrao salvo", tft.width()/2, 80, 2);
        tft.drawCentreString("Use Serial: SAVE <nome>", tft.width()/2, 110, 1);
        tft.drawCentreString("no modo Monitor", tft.width()/2, 125, 1);
    } else {
        // Lista os padrões salvos
        for (int i = 0; i < patternCount && i < 10; i++) {
            int rowY = 42 + i * 18;
            
            tft.setTextColor(probeColors[patterns[i].channel % NUM_PROBE_CHANNELS], DARK_BG);
            char line[60];
            snprintf(line, sizeof(line), "%d. %-12s CH%d %dmV %luus",
                     i + 1, patterns[i].name, patterns[i].channel,
                     (int)(patterns[i].peakValue * 3300.0 / 4095.0),
                     patterns[i].duration);
            tft.drawString(line, 10, rowY, 1);
        }
    }
    
    tft.setTextColor(MID_GRAY, DARK_BG);
    tft.drawString("[BOOT]=Modo | Serial: LIST, SAVE, DELETE", 5, tft.height() - 10, 1);
}

// ============================================================================
// COMANDOS SERIAL - Interface de controle
// ============================================================================

void processSerialCommand() {
    if (!Serial.available()) return;
    
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();
    
    Serial.printf("> CMD: %s\n", cmd.c_str());
    
    // ---- Comandos de monitoramento ----
    if (cmd == "STATUS") {
        Serial.println("=== STATUS ESP32 Toslink LED ===");
        Serial.printf("Modo atual: %d (0=PIX 1=MONITOR 2=CAPTURE 3=MAPPING)\n", currentMode);
        Serial.printf("Amostras capturadas: %d\n", captureIndex);
        for (int ch = 0; ch < NUM_PROBE_CHANNELS; ch++) {
            int rawVal = analogRead(probePins[ch]);
            float voltage = rawVal * 3.3 / 4095.0;
            Serial.printf("  CH%d (GPIO%d): %d raw = %.3fV | Pulsos: %lu\n",
                          ch, probePins[ch], rawVal, voltage, pulseCount[ch]);
        }
        Serial.printf("Padroes salvos: %d/%d\n", patternCount, MAX_PATTERNS);
    }
    else if (cmd == "READ") {
        // Leitura instantânea de todos os canais
        Serial.println("--- Leitura Instantanea ---");
        for (int ch = 0; ch < NUM_PROBE_CHANNELS; ch++) {
            // Faz 10 leituras e calcula média (reduz ruído)
            long sum = 0;
            int minVal = 4095, maxVal = 0;
            for (int i = 0; i < 10; i++) {
                int v = analogRead(probePins[ch]);
                sum += v;
                if (v < minVal) minVal = v;
                if (v > maxVal) maxVal = v;
                delayMicroseconds(100);
            }
            int avg = sum / 10;
            float avgV = avg * 3.3 / 4095.0;
            float minV = minVal * 3.3 / 4095.0;
            float maxV = maxVal * 3.3 / 4095.0;
            Serial.printf("  CH%d (GPIO%d): AVG=%.3fV  MIN=%.3fV  MAX=%.3fV  Pulsos=%lu\n",
                          ch, probePins[ch], avgV, minV, maxV, pulseCount[ch]);
        }
    }
    else if (cmd.startsWith("SAVE ")) {
        // Salva o padrão atual do sinal mais forte
        String patternName = cmd.substring(5);
        patternName.trim();
        
        if (patternCount >= MAX_PATTERNS) {
            Serial.println("ERRO: Limite de padroes atingido! Use DELETE primeiro.");
            return;
        }
        
        // Encontra o canal com maior sinal
        int bestCh = 0;
        int bestVal = 0;
        for (int ch = 0; ch < NUM_PROBE_CHANNELS; ch++) {
            int val = analogRead(probePins[ch]);
            if (val > bestVal) {
                bestVal = val;
                bestCh = ch;
            }
        }
        
        // Mede duração do pulso
        unsigned long startTime = micros();
        while (analogRead(probePins[bestCh]) > triggerThreshold && (micros() - startTime) < 1000000) {
            // Espera o pulso acabar (timeout 1s)
        }
        unsigned long duration = micros() - startTime;
        
        // Salva
        strncpy(patterns[patternCount].name, patternName.c_str(), 19);
        patterns[patternCount].name[19] = '\0';
        patterns[patternCount].channel = bestCh;
        patterns[patternCount].peakValue = bestVal;
        patterns[patternCount].duration = duration;
        patterns[patternCount].frequency = (duration > 0) ? (1000000 / duration) : 0;
        patterns[patternCount].active = true;
        patternCount++;
        
        Serial.printf("SALVO: '%s' -> CH%d Pico=%d (%.2fV) Dur=%luus\n",
                      patternName.c_str(), bestCh, bestVal,
                      bestVal * 3.3 / 4095.0, duration);
    }
    else if (cmd == "LIST") {
        Serial.println("=== PADROES MAPEADOS ===");
        if (patternCount == 0) {
            Serial.println("  (nenhum padrao salvo)");
        }
        for (int i = 0; i < patternCount; i++) {
            Serial.printf("  %d. %-15s CH%d Pico=%d (%.2fV) Dur=%luus Freq=%luHz\n",
                          i, patterns[i].name, patterns[i].channel,
                          patterns[i].peakValue,
                          patterns[i].peakValue * 3.3 / 4095.0,
                          patterns[i].duration,
                          patterns[i].frequency);
        }
    }
    else if (cmd.startsWith("DELETE ")) {
        int idx = cmd.substring(7).toInt();
        if (idx >= 0 && idx < patternCount) {
            Serial.printf("DELETADO: '%s'\n", patterns[idx].name);
            // Shift array
            for (int i = idx; i < patternCount - 1; i++) {
                patterns[i] = patterns[i + 1];
            }
            patternCount--;
        } else {
            Serial.println("ERRO: Indice invalido. Use LIST para ver.");
        }
    }
    else if (cmd.startsWith("TRIGGER ")) {
        // Ajusta o limiar de trigger
        int newThreshold = cmd.substring(8).toInt();
        if (newThreshold >= 0 && newThreshold <= 4095) {
            triggerThreshold = newThreshold;
            Serial.printf("Trigger ajustado para: %d (%.2fV)\n",
                          triggerThreshold, triggerThreshold * 3.3 / 4095.0);
        }
    }
    else if (cmd == "RESET") {
        // Reset contadores de pulso
        for (int ch = 0; ch < NUM_PROBE_CHANNELS; ch++) {
            pulseCount[ch] = 0;
            lastPulseTime[ch] = 0;
            lastPulseState[ch] = 0;
        }
        captureIndex = 0;
        Serial.println("Contadores resetados.");
    }
    else if (cmd == "SCAN") {
        // Varredura intensiva: lê rápido por 2 segundos e reporta atividade
        Serial.println("=== VARREDURA 2s - Ative algo no Kenwood AGORA! ===");
        
        int baseline[NUM_PROBE_CHANNELS];
        int peakMax[NUM_PROBE_CHANNELS];
        int peakMin[NUM_PROBE_CHANNELS];
        int changeCount[NUM_PROBE_CHANNELS];
        
        // Baseline
        for (int ch = 0; ch < NUM_PROBE_CHANNELS; ch++) {
            baseline[ch] = analogRead(probePins[ch]);
            peakMax[ch] = baseline[ch];
            peakMin[ch] = baseline[ch];
            changeCount[ch] = 0;
        }
        
        // Varredura
        unsigned long endTime = millis() + 2000;
        while (millis() < endTime) {
            for (int ch = 0; ch < NUM_PROBE_CHANNELS; ch++) {
                int val = analogRead(probePins[ch]);
                if (val > peakMax[ch]) peakMax[ch] = val;
                if (val < peakMin[ch]) peakMin[ch] = val;
                if (abs(val - baseline[ch]) > noiseFloor) changeCount[ch]++;
            }
            delayMicroseconds(50);  // ~20kHz por canal
        }
        
        Serial.println("--- Resultado da Varredura ---");
        for (int ch = 0; ch < NUM_PROBE_CHANNELS; ch++) {
            float baseV = baseline[ch] * 3.3 / 4095.0;
            float maxV = peakMax[ch] * 3.3 / 4095.0;
            float minV = peakMin[ch] * 3.3 / 4095.0;
            float rangeV = maxV - minV;
            
            const char* activity = (changeCount[ch] > 100) ? "*** ATIVO ***" :
                                   (changeCount[ch] > 10)  ? "~ variacao ~" :
                                                              "  quieto";
            
            Serial.printf("  CH%d (GPIO%d): Base=%.2fV Min=%.2fV Max=%.2fV Range=%.2fV Changes=%d %s\n",
                          ch, probePins[ch], baseV, minV, maxV, rangeV, changeCount[ch], activity);
        }
    }
    // ---- Comandos de tela ----
    else if (cmd == "LOGO") {
        currentMode = MODE_LOGO;
        drawLogoScreen();
        Serial.println(">> TELA: Logo KenWoody (fixo)");
    }
    else if (cmd == "PIX") {
        currentMode = MODE_PIX;
        drawPixScreen();
        Serial.println(">> TELA: PIX QR Code (fixo)");
        Serial.printf("   Chave atual: %s\n", cfgPIX.c_str());
    }
    else if (cmd.startsWith("PIXC ")) {
        // Muda a chave PIX e atualiza a tela
        String novaChave = cmd.substring(5);
        novaChave.trim();
        // Remove formatação (pontos, traços) se o usuário digitar
        novaChave.replace(".", "");
        novaChave.replace("-", "");
        novaChave.replace(" ", "");
        // Volta para minúscula caso seja email/etc
        // (toUpperCase já foi chamado, mas PIX pode ser email)
        
        cfgPIX = novaChave;
        Serial.printf("PIX atualizado para: %s\n", cfgPIX.c_str());
        
        // Se está na tela PIX, redesenha com a nova chave
        if (currentMode == MODE_PIX) {
            drawPixScreen();
            Serial.println("Tela PIX atualizada!");
        } else {
            Serial.println("Use PIX para ver a tela com a nova chave.");
        }
    }
    else if (cmd == "HELP") {
        Serial.println("=== COMANDOS DE TELA ===");
        Serial.println("  LOGO         - Mostra tela do logo KenWoody");
        Serial.println("  PIX          - Mostra tela PIX (QR Code)");
        Serial.println("  PIXC <cpf>   - Muda a chave PIX (ex: PIXC 2939129231)");
        Serial.println("");
        Serial.println("=== COMANDOS DE MONITORAMENTO ===");
        Serial.println("  STATUS       - Estado geral do sistema");
        Serial.println("  READ         - Leitura instantanea de todos os canais");
        Serial.println("  SCAN         - Varredura 2s (para descobrir sinais)");
        Serial.println("  SAVE <nome>  - Salva padrao do sinal atual");
        Serial.println("  LIST         - Lista padroes mapeados");
        Serial.println("  DELETE <n>   - Deleta padrao pelo indice");
        Serial.println("  TRIGGER <n>  - Ajusta limiar de trigger (0-4095)");
        Serial.println("  RESET        - Zera contadores de pulso");
        Serial.println("  HELP         - Esta ajuda");
        Serial.println("");
        Serial.println("=== PINOS DE MONITORAMENTO ===");
        for (int ch = 0; ch < NUM_PROBE_CHANNELS; ch++) {
            Serial.printf("  CH%d = GPIO %d (%s)\n", ch, probePins[ch], probeNames[ch]);
        }
        Serial.println("");
        Serial.println("=== CIRCUITO DE PROTECAO ===");
        Serial.println("  Kenwood ──[R1]──┬──[R2]── GND");
        Serial.println("                  │");
        Serial.println("              GPIO ESP32");
        Serial.println("  5V:  R1=10k R2=10k (divide por 2)");
        Serial.println("  12V: R1=22k R2=10k (divide por 3.2)");
        Serial.println("  + Diodo Zener 3.3V entre GPIO e GND");
    }
    else {
        Serial.printf("Comando desconhecido: '%s'. Digite HELP.\n", cmd.c_str());
    }
}

// ============================================================================
// CALLBACK DO BLUETOOTH
// ============================================================================

void bt_connection_state_changed(esp_a2d_connection_state_t state, void *ptr) {
    if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        if (currentMode == MODE_PIX) updateBtStatus(true);
        Serial.println("Bluetooth: Conectado!");
    } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        if (currentMode == MODE_PIX) updateBtStatus(false);
        Serial.println("Bluetooth: Desconectado.");
    }
}

// ============================================================================
// SETUP & LOOP
// ============================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("");
    Serial.println("====================================");
    Serial.println(" ESP32 Toslink LED + Signal Monitor");
    Serial.println(" KenWoody - O Principe");
    Serial.println(" Kenwood A-G9 Controller");
    Serial.println("====================================");
    Serial.println("Digite HELP para ver os comandos.");
    Serial.println("");
    
    // 1. Configura botão de modo (GPIO 0 = BOOT)
    pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
    
    // 2. Configura pinos de monitoramento
    for (int ch = 0; ch < NUM_PROBE_CHANNELS; ch++) {
        pinMode(probePins[ch], INPUT);
    }
    // Resolução ADC 12-bit
    analogReadResolution(12);
    // Atenuação para leitura até ~3.3V
    analogSetAttenuation(ADC_11db);
    
    // 3. Inicializa memória dos padrões
    memset(patterns, 0, sizeof(patterns));
    memset(captureBuffer, 0, sizeof(captureBuffer));
    
    // 4. Inicializa o display TFT
    tft.init();
    tft.setRotation(1); // Paisagem 320x240
    
    // CORREÇÃO CORES INVERTIDAS: 
    // Se as cores ficarem erradas, troque true por false
    tft.invertDisplay(true);
    
    tft.fillScreen(TFT_BLACK);
    
    // 5. Animação do KenWoody (logo de boot)
    playKenWoodyAnimation();
    
    // 6. Logo fica FIXO na tela após a animação
    drawLogoScreen();
    currentMode = MODE_LOGO;
    
    // 7. Configura I2S
    i2s.setPins(I2S_SCK, I2S_WS, I2S_SDOUT);
    
    if (!i2s.begin(I2S_MODE_STD, 44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial.println("Erro ao inicializar o I2S!");
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawCentreString("ERRO I2S!", tft.width()/2, tft.height()/2, 4);
        while (1);
    }
    
    // 8. Registra callback e inicia Bluetooth
    a2dp_sink.set_on_connection_state_changed(bt_connection_state_changed);
    a2dp_sink.start("ESP32_Toslink_LED");
    
    Serial.println("Sistema pronto! BT: 'ESP32_Toslink_LED'");
    Serial.println("Pressione o botao BOOT para alternar modos.");
}

// Debounce do botão
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 300;

void loop() {
    // 1. Verifica botão de modo (GPIO 0 = BOOT, ativo LOW)
    if (digitalRead(MODE_BUTTON_PIN) == LOW) {
        unsigned long now = millis();
        if (now - lastButtonPress > debounceDelay) {
            lastButtonPress = now;
            
            // Alterna entre os modos: LOGO → PIX → MONITOR → MAPPING → LOGO
            switch (currentMode) {
                case MODE_LOGO:
                    currentMode = MODE_PIX;
                    drawPixScreen();
                    Serial.println(">> MODO: Tela PIX");
                    break;
                    
                case MODE_PIX:
                    currentMode = MODE_MONITOR;
                    captureIndex = 0;
                    drawScopeGrid();
                    Serial.println(">> MODO: Monitor de Sinais (Osciloscopio)");
                    break;
                    
                case MODE_MONITOR:
                    currentMode = MODE_MAPPING;
                    drawMappingScreen();
                    Serial.println(">> MODO: Padroes Mapeados");
                    break;
                    
                case MODE_MAPPING:
                    currentMode = MODE_LOGO;
                    drawLogoScreen();
                    Serial.println(">> MODO: Logo KenWoody");
                    break;
                    
                default:
                    currentMode = MODE_LOGO;
                    drawLogoScreen();
                    break;
            }
        }
    }
    
    // 2. Executa lógica do modo atual
    switch (currentMode) {
        case MODE_MONITOR:
            updateScope();
            delayMicroseconds(500); // ~2kHz de amostragem por canal
            break;
            
        case MODE_LOGO:
        case MODE_PIX:
        case MODE_MAPPING:
            // Telas estáticas - nada a atualizar
            delay(10);
            break;
            
        default:
            delay(10);
            break;
    }
    
    // 3. Processa comandos Serial (sempre ativo, em qualquer modo)
    processSerialCommand();
}
#include "pins_config.h"
#include "LovyanGFX_Driver.h"
#include <Arduino.h>
#include <lvgl.h>
#include <Wire.h>
#include "touch.h"
#include "dashboard_ui.h"

// ── Bibliothèques capteurs ────────────────────────────────────
#include <OneWire.h>
#include <DallasTemperature.h>

// ── WiFi + API ────────────────────────────────────────────────
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ════════════════════════════════════════════════════════════════
//  CONFIGURATION WIFI + API
// ════════════════════════════════════════════════════════════════
const char* WIFI_SSID     = "---------";
const char* WIFI_PASSWORD = "---------";
const char* API_URL       = "https://sousscamp.duckdns.org/unified_data";

// ════════════════════════════════════════════════════════════════
//  OBJETS GLOBAUX
// ════════════════════════════════════════════════════════════════

// Display + LVGL
LGFX gfx;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf;
static lv_color_t *buf1;

// DS18B20
OneWire           oneWire(PIN_DS18B20);
DallasTemperature ds18b20(&oneWire);

// YF-S201 — compteur impulsions ISR
volatile uint32_t pulse_count  = 0;
static   uint32_t last_flow_ms = 0;

// Timing
static uint32_t last_modbus_ms = 0;
static uint32_t last_sensor_ms = 0;
static uint32_t last_wifi_ms   = 0;

// Compteurs erreurs Modbus
static uint8_t err_pm5100  = 0;
static uint8_t err_pmp51   = 0;
static uint8_t err_prowirl = 0;
static uint8_t err_zenner  = 0;   // ZENNER Smart Gas Meter
#define COMM_ERROR_MAX 3

// ── Variables ZENNER — définies dans dashboard_ui.cpp ────────
extern float g_gas_index_m3;      // Index total gaz (m³, 1 décimale)
extern float g_gas_flow_m3h;      // Débit instantané gaz (m³/h, 2 décimales)
extern bool  g_alarm_gas;         // débit gaz > GAS_ALARM_FLOW (5.0 m³/h)

// ── Alarmes ZENNER supplémentaires ───────────────────────────
bool  g_alarm_zenner     = false; // index gaz > GAS_ALARM_INDEX (2000.0 m³)
bool  g_alarm_comm_zenner= false; // perte communication ZENNER

// Seuil surintensité (A) — ajustable selon installation
#define OVERCURRENT_ALARM_A   80.0f

// ════════════════════════════════════════════════════════════════
//  ISR YF-S201 (anti-rebond 5 ms)
// ════════════════════════════════════════════════════════════════

void IRAM_ATTR flow_isr(void) {
    static uint32_t last_us = 0;
    uint32_t now_us = micros();
    if (now_us - last_us > 5000) {
        pulse_count++;
        last_us = now_us;
    }
}

// ════════════════════════════════════════════════════════════════
//  MODBUS RTU — fonctions communes
// ════════════════════════════════════════════════════════════════

uint16_t crc16(const uint8_t *data, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

float bytes_to_float(const uint8_t *b) {
    union { uint8_t raw[4]; float f; } u;
    u.raw[3] = b[0]; u.raw[2] = b[1];
    u.raw[1] = b[2]; u.raw[0] = b[3];
    return u.f;
}

int64_t bytes_to_int64(const uint8_t *b) {
    return ((int64_t)b[0]<<56)|((int64_t)b[1]<<48)|
           ((int64_t)b[2]<<40)|((int64_t)b[3]<<32)|
           ((int64_t)b[4]<<24)|((int64_t)b[5]<<16)|
           ((int64_t)b[6]<< 8)|((int64_t)b[7]);
}

/**
 * Lire 1 Float32 (2 registres) en FC03 — PM5100
 */
bool pm_read_float(uint16_t reg, float &val) {
    uint8_t req[8];
    req[0] = MODBUS_ADDR_PM5100; req[1] = 0x03;
    req[2] = reg >> 8;           req[3] = reg & 0xFF;
    req[4] = 0x00;               req[5] = 0x02;
    uint16_t c = crc16(req, 6);
    req[6] = c & 0xFF; req[7] = c >> 8;

    while (Serial1.available()) Serial1.read();
    digitalWrite(RS485_DE_PIN, HIGH); delayMicroseconds(100);
    Serial1.write(req, 8); Serial1.flush();
    delayMicroseconds(100); digitalWrite(RS485_DE_PIN, LOW);

    uint8_t resp[9]; uint8_t idx = 0;
    uint32_t t = millis();
    while (idx < 9 && (millis() - t) < MODBUS_TIMEOUT_MS)
        if (Serial1.available()) resp[idx++] = Serial1.read();

    if (idx < 9)                          return false;
    if (resp[0] != MODBUS_ADDR_PM5100)    return false;
    if (resp[1] & 0x80)                   return false;
    uint16_t cr = resp[7] | (uint16_t)(resp[8] << 8);
    if (cr != crc16(resp, 7))             return false;

    val = bytes_to_float(&resp[3]);
    return true;
}

/**
 * Lire énergie int64 (4 registres) en FC03 — PM5100
 */
bool pm_read_int64(uint16_t reg, int64_t &val) {
    uint8_t req[8];
    req[0] = MODBUS_ADDR_PM5100; req[1] = 0x03;
    req[2] = reg >> 8;           req[3] = reg & 0xFF;
    req[4] = 0x00;               req[5] = 0x04;
    uint16_t c = crc16(req, 6);
    req[6] = c & 0xFF; req[7] = c >> 8;

    while (Serial1.available()) Serial1.read();
    digitalWrite(RS485_DE_PIN, HIGH); delayMicroseconds(100);
    Serial1.write(req, 8); Serial1.flush();
    delayMicroseconds(100); digitalWrite(RS485_DE_PIN, LOW);

    uint8_t resp[13]; uint8_t idx = 0;
    uint32_t t = millis();
    while (idx < 13 && (millis() - t) < MODBUS_TIMEOUT_MS)
        if (Serial1.available()) resp[idx++] = Serial1.read();

    if (idx < 13)                         return false;
    if (resp[0] != MODBUS_ADDR_PM5100)    return false;
    if (resp[1] & 0x80)                   return false;
    uint16_t cr = resp[11] | (uint16_t)(resp[12] << 8);
    if (cr != crc16(resp, 11))            return false;

    val = bytes_to_int64(&resp[3]);
    return true;
}

/**
 * Lire 1 Float32 (2 registres) en FC04 — E+H PMP51 ou Prowirl
 * BUS 2 : Serial2 + RS485B_DE_PIN
 */
bool eh_read_float(uint8_t addr, uint16_t reg, float &val) {
    uint8_t req[8];
    req[0] = addr; req[1] = 0x04;
    req[2] = reg >> 8; req[3] = reg & 0xFF;
    req[4] = 0x00;     req[5] = 0x02;
    uint16_t c = crc16(req, 6);
    req[6] = c & 0xFF; req[7] = c >> 8;

    while (Serial2.available()) Serial2.read();
    digitalWrite(RS485B_DE_PIN, HIGH); delayMicroseconds(100);
    Serial2.write(req, 8); Serial2.flush();
    delayMicroseconds(100); digitalWrite(RS485B_DE_PIN, LOW);

    uint8_t resp[9]; uint8_t idx = 0;
    uint32_t t = millis();
    while (idx < 9 && (millis() - t) < MODBUS_TIMEOUT_MS)
        if (Serial2.available()) resp[idx++] = Serial2.read();

    if (idx < 9)              return false;
    if (resp[0] != addr)      return false;
    if (resp[1] & 0x80)       return false;
    uint16_t cr = resp[7] | (uint16_t)(resp[8] << 8);
    if (cr != crc16(resp, 7)) return false;

    val = bytes_to_float(&resp[3]);
    return true;
}

/**
 * Lire 1 UINT32 (2 registres) en FC04 — ZENNER Gas Meter
 * Format : Big Endian, reg_hi = reg, reg_lo = reg+1
 * BUS 2 : Serial2 + RS485B_DE_PIN
 */
bool zenner_read_uint32(uint8_t addr, uint16_t reg, uint32_t &val) {
    uint8_t req[8];
    req[0] = addr; req[1] = 0x04;
    req[2] = reg >> 8; req[3] = reg & 0xFF;
    req[4] = 0x00;     req[5] = 0x02;
    uint16_t c = crc16(req, 6);
    req[6] = c & 0xFF; req[7] = c >> 8;

    while (Serial2.available()) Serial2.read();
    digitalWrite(RS485B_DE_PIN, HIGH); delayMicroseconds(100);
    Serial2.write(req, 8); Serial2.flush();
    delayMicroseconds(100); digitalWrite(RS485B_DE_PIN, LOW);

    uint8_t resp[9]; uint8_t idx = 0;
    uint32_t t = millis();
    while (idx < 9 && (millis() - t) < MODBUS_TIMEOUT_MS)
        if (Serial2.available()) resp[idx++] = Serial2.read();

    if (idx < 9)              return false;
    if (resp[0] != addr)      return false;
    if (resp[1] & 0x80)       return false;
    uint16_t cr = resp[7] | (uint16_t)(resp[8] << 8);
    if (cr != crc16(resp, 7)) return false;

    // UINT32 Big Endian : resp[3]=HI_HI, resp[4]=HI_LO, resp[5]=LO_HI, resp[6]=LO_LO
    val = ((uint32_t)resp[3] << 24) | ((uint32_t)resp[4] << 16)
        | ((uint32_t)resp[5] <<  8) |  (uint32_t)resp[6];
    return true;
}


//  Séquence : PM5100 → délai → PMP51 → délai → Prowirl
// ════════════════════════════════════════════════════════════════

void modbus_read(void) {
    uint32_t now = millis();
    if (now - last_modbus_ms < MODBUS_READ_MS) return;
    last_modbus_ms = now;

    Serial.println("\n[MODBUS] === Cycle lecture ===");

    // ── PM5100 (FC03) ────────────────────────────────────
    float v = 0, i = 0, p = 0, pf = 0, f = 0;
    int64_t e = 0;

    bool okV  = pm_read_float(REG_PM_VOLTAGE, v);  delay(80);
    bool okI  = pm_read_float(REG_PM_CURRENT, i);  delay(80);
    bool okP  = pm_read_float(REG_PM_POWER,   p);  delay(80);
    bool okE  = pm_read_int64(REG_PM_ENERGY,  e);  delay(80);
    bool okPF = pm_read_float(REG_PM_PF,      pf); delay(80);
    bool okF  = pm_read_float(REG_PM_FREQ,    f);  delay(80);

    if (!okV && !okI && !okP) {
        err_pm5100++;
        if (err_pm5100 >= COMM_ERROR_MAX) {
            g_alarm_comm_pm = true;
            Serial.println("[PM5100] COMM ERREUR");
        }
    } else {
        err_pm5100 = 0; g_alarm_comm_pm = false;
        if (okV)  g_voltage_v      = v;
        if (okI)  g_current_a      = i;
        if (okP)  g_power_kw       = p;
        if (okE)  g_energy_kwh     = (float)e;
        if (okPF) { pf = fabsf(pf); if (pf > 1.0f) pf = 1.0f;
                    g_power_factor = pf; }
        if (okF)  g_frequency_hz   = f;
        Serial.printf("[PM5100] U=%.1fV P=%.1fW FP=%.3f f=%.1fHz E=%.0fWh\n",
            g_voltage_v, g_power_kw, g_power_factor,
            g_frequency_hz, g_energy_kwh);
    }

    delay(100);  // repos bus

    // ── E+H PMP51 (FC04, addr=2) ─────────────────────────
    float pression = 0.0f;
    if (eh_read_float(MODBUS_ADDR_PMP51, REG_EH_VALUE, pression)) {
        g_pression_bar  = pression;
        err_pmp51       = 0;
        g_alarm_comm_pmp = false;
        Serial.printf("[PMP51] P=%.2f bar\n", pression);
    } else {
        err_pmp51++;
        if (err_pmp51 >= COMM_ERROR_MAX) g_alarm_comm_pmp = true;
        Serial.println("[PMP51] TIMEOUT");
    }

    delay(100);  // repos bus

    // ── E+H Prowirl (FC04, addr=2) ───────────────────────
    float debit = 0.0f;
    if (eh_read_float(MODBUS_ADDR_PROWIRL, REG_EH_VALUE, debit)) {
        g_debit_vol     = debit;
        err_prowirl     = 0;
        g_alarm_comm_pw = false;
        Serial.printf("[PROWIRL] Q=%.0f m3/h\n", debit);
    } else {
        err_prowirl++;
        if (err_prowirl >= COMM_ERROR_MAX) g_alarm_comm_pw = true;
        Serial.println("[PROWIRL] TIMEOUT");
    }

    delay(100);  // repos bus

    // ── ZENNER Smart Gas (FC04, addr=3) ──────────────────
    // Lecture index total (registres 0x0001-0x0002) — UINT32 × 10
    uint32_t raw_index = 0, raw_flow = 0;
    bool okGasIdx  = zenner_read_uint32(MODBUS_ADDR_ZENNER, REG_GAS_INDEX, raw_index);
    delay(50);
    bool okGasFlow = zenner_read_uint32(MODBUS_ADDR_ZENNER, REG_GAS_FLOW,  raw_flow);

    if (okGasIdx || okGasFlow) {
        err_zenner = 0;
        g_alarm_comm_zenner = false;
        if (okGasIdx) {
            g_gas_index_m3  = (float)raw_index / 10.0f;   // m³ (1 décimale)
        }
        if (okGasFlow) {
            g_gas_flow_m3h  = (float)raw_flow  / 100.0f;  // m³/h (2 décimales)
        }
        // Évaluation alarmes ZENNER
        g_alarm_gas    = (g_gas_flow_m3h  > GAS_ALARM_FLOW);    // débit > 5.0 m³/h
        g_alarm_zenner = (raw_index       > GAS_ALARM_INDEX);   // index > 20000 (= 2000 m³)
        Serial.printf("[ZENNER] Index=%.1f m3  Debit=%.2f m3/h  AlmFlow=%d  AlmIdx=%d\n",
            g_gas_index_m3, g_gas_flow_m3h, g_alarm_gas, g_alarm_zenner);
    } else {
        err_zenner++;
        if (err_zenner >= COMM_ERROR_MAX) g_alarm_comm_zenner = true;
        Serial.println("[ZENNER] TIMEOUT");
    }
}

// ════════════════════════════════════════════════════════════════
//  CAPTEURS ENVIRONNEMENTAUX (2 sec)
// ════════════════════════════════════════════════════════════════

void read_temperature(void) {
    ds18b20.requestTemperatures();
    float t = ds18b20.getTempCByIndex(0);
    if (t == DEVICE_DISCONNECTED_C || t < -55.0f || t > 125.0f) {
        g_temp_valid  = false;
        g_alarm_sensor = true;
        Serial.println("[DS18B20] Capteur absent !");
    } else {
        g_temp_c      = t;
        g_temp_valid  = true;
        g_alarm_sensor = false;
        Serial.printf("[DS18B20] T=%.2f °C\n", t);
    }
}

void read_flow(void) {
    uint32_t now = millis();
    uint32_t dt  = now - last_flow_ms;
    if (dt < FLOW_COUNT_MS) return;

    noInterrupts();
    uint32_t pulses = pulse_count;
    pulse_count = 0;
    interrupts();
    last_flow_ms = now;

    // Calcul débit instantané
    float freq_hz = (float)pulses / ((float)dt / 1000.0f);
    if (freq_hz < 2.0f) freq_hz = 0.0f;  // seuil anti-bruit

    float flow = freq_hz / FLOW_CALIBRATION;  // L/min

    // Volume cumulé — correction
    if (flow > 0.0f) {
        g_volume_l += (flow / 60.0f) * ((float)dt / 1000.0f);  // L
    }

    g_flow_lpm = flow;

    Serial.printf("[YF-S201] pulses=%lu freq=%.2fHz flow=%.2f L/min | Vol: %.3f L\n",
        pulses, freq_hz, flow, g_volume_l);
}

void sensors_read(void) {
    uint32_t now = millis();
    if (now - last_sensor_ms < SENSOR_READ_MS) return;
    last_sensor_ms = now;

    Serial.println("\n[ENV] === Lecture capteurs ===");
    read_temperature();
    // Note : gaz lus via Modbus ZENNER dans modbus_read()
}

// ════════════════════════════════════════════════════════════════
//  PROTOCOLES ALTERNATIFS — BLOCS COMMENTÉS
//  Pour intégrer un nouvel instrument, décommenter le bloc
//  correspondant et adapter les paramètres spécifiques
// ════════════════════════════════════════════════════════════════

// ----------------------------------------------------------------
//  PROTOCOLE : 4-20 mA (courant analogique)
//  Instruments compatibles : capteurs de pression, débit, niveau
//  Utilisation : lire la valeur via ADC (Analog-to-Digital Converter)
//  Exemple : Endress+Hauser PMP51 en version 4-20 mA
// ----------------------------------------------------------------
/*
#define ANALOG_PIN        34        // GPIO ADC — a adapter selon cablage
#define ANALOG_MIN_MA     4.0f      // 4 mA  = valeur physique minimale
#define ANALOG_MAX_MA     20.0f     // 20 mA = valeur physique maximale
#define PHYS_MIN          0.0f      // Valeur physique min (ex: 0 bar)
#define PHYS_MAX          10.0f     // Valeur physique max (ex: 10 bar)

float read_4_20mA(void) {
    int raw = analogRead(ANALOG_PIN);
    float voltage = raw * 3.3f / 4095.0f;
    float current_ma = (voltage / 250.0f) * 1000.0f;
    float val = PHYS_MIN + (current_ma - ANALOG_MIN_MA)
                * (PHYS_MAX - PHYS_MIN) / (ANALOG_MAX_MA - ANALOG_MIN_MA);
    return constrain(val, PHYS_MIN, PHYS_MAX);
}
*/

// ----------------------------------------------------------------
//  PROTOCOLE : HART (Highway Addressable Remote Transducer)
//  Instruments compatibles : Endress+Hauser PMP51, Prowirl 200
//  Utilisation : communication numerique superposee sur 4-20 mA
//  Necessite : module HART Modem (ex: HCF_DC-01)
// ----------------------------------------------------------------
/*
#define HART_BAUD         1200
#define HART_ADDR         0
#define HART_CMD_READ     3

void hart_send_command(uint8_t cmd) {
    uint8_t frame[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                       0x02, HART_ADDR, cmd, 0x00, 0x00};
    uint8_t crc = 0;
    for (int i = 5; i < 9; i++) crc ^= frame[i];
    frame[9] = crc;
    Serial1.write(frame, sizeof(frame));
}

float hart_read_primary_value(void) {
    hart_send_command(HART_CMD_READ);
    delay(300);
    uint8_t resp[20]; uint8_t idx = 0;
    uint32_t t = millis();
    while (idx < 20 && (millis() - t) < 500)
        if (Serial1.available()) resp[idx++] = Serial1.read();
    if (idx < 14) return -1.0f;
    return bytes_to_float(&resp[10]);
}
*/

// ----------------------------------------------------------------
//  PROTOCOLE : I2C (Inter-Integrated Circuit)
//  Instruments compatibles : capteurs temperature, humidite, pression
//  Exemple : BME280 (temperature + humidite + pression)
//  Pins : SDA=GPIO15, SCL=GPIO16
// ----------------------------------------------------------------
/*
#include <Adafruit_BME280.h>

#define I2C_ADDR_BME280   0x76

Adafruit_BME280 bme280;

bool i2c_init_bme280(void) {
    if (!bme280.begin(I2C_ADDR_BME280)) return false;
    return true;
}

void i2c_read_bme280(float &temp, float &humidity, float &pressure) {
    temp     = bme280.readTemperature();
    humidity = bme280.readHumidity();
    pressure = bme280.readPressure() / 100.0f;
}
*/

// ----------------------------------------------------------------
//  PROTOCOLE : SPI (Serial Peripheral Interface)
//  Instruments compatibles : capteurs thermocouple, afficheurs
//  Exemple : MAX31855 (thermocouple type K)
//  Pins : MOSI=GPIO11, MISO=GPIO13, SCK=GPIO12, CS=GPIO10
// ----------------------------------------------------------------
/*
#include <Adafruit_MAX31855.h>

#define SPI_CS_PIN        10

Adafruit_MAX31855 thermocouple(SPI_CS_PIN);

float spi_read_thermocouple(void) {
    float temp = thermocouple.readCelsius();
    if (isnan(temp)) return -1.0f;
    return temp;
}
*/

// ----------------------------------------------------------------
//  PROTOCOLE : Impulsions numeriques (Pulse Counter)
//  Instruments compatibles : debitmètres a turbine, compteurs gaz/eau
//  Meme principe ISR que YF-S201 — adapter le facteur de calibration
// ----------------------------------------------------------------
/*
#define PULSE_PIN_ALT         35
#define PULSE_FACTOR_ALT      7.5f  // Impulsions/litre — selon datasheet

volatile uint32_t pulse_count_alt = 0;

void IRAM_ATTR pulse_isr_alt(void) {
    static uint32_t last_us = 0;
    uint32_t now_us = micros();
    if (now_us - last_us > 5000) {
        pulse_count_alt++;
        last_us = now_us;
    }
}

float read_flow_alt_lpm(uint32_t interval_ms) {
    uint32_t count = pulse_count_alt;
    pulse_count_alt = 0;
    return (count / PULSE_FACTOR_ALT) / (interval_ms / 60000.0f);
}
*/

// ════════════════════════════════════════════════════════════════
//  WIFI + FastAPI — envoi JSON unifié toutes 30 sec
// ════════════════════════════════════════════════════════════════

void wifi_connect(void) {
    Serial.printf("[WIFI] Connexion %s...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint8_t tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 15) {
        delay(500); Serial.print("."); tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        g_wifi_connected = true;
        Serial.printf("\n[WIFI] OK  IP=%s\n",
            WiFi.localIP().toString().c_str());
    } else {
        g_wifi_connected = false;
        Serial.println("\n[WIFI] Echec — mode hors-ligne");
    }
}

void send_to_api(void) {
    if (WiFi.status() != WL_CONNECTED) {
        g_wifi_connected = false;
        wifi_connect();
        return;
    }
    g_wifi_connected = true;

    StaticJsonDocument<512> doc;

    // PM5100
    doc["voltage_v"]      = g_voltage_v;
    doc["current_a"]      = g_current_a;
    doc["power_kw"]       = g_power_kw;
    doc["energy_kwh"]     = g_energy_kwh;
    doc["power_factor"]   = g_power_factor;
    doc["frequency_hz"]   = g_frequency_hz;
    doc["cost_dh"]        = g_cost_dh;

    // E+H
    doc["pression_bar"]   = g_pression_bar;
    doc["debit_m3h"]      = g_debit_vol;

    // Capteurs env.
    doc["temp_c"]         = round(g_temp_c * 100.0f) / 100.0f;
    doc["temp_valid"]     = g_temp_valid;
    doc["gas_index_m3"]   = round(g_gas_index_m3 * 10.0f) / 10.0f;   // 1 décimale
    doc["gas_flow_m3h"]   = round(g_gas_flow_m3h * 100.0f) / 100.0f; // 2 décimales
    doc["flow_lpm"]       = round(g_flow_lpm * 100.0f) / 100.0f;
    doc["volume_l"]       = round(g_volume_l * 100.0f) / 100.0f;

    // Alarmes
    doc["alarm_pm"]          = g_alarm_overload || g_alarm_voltage;
    doc["alarm_overcurrent"] = g_alarm_overcurrent;
    doc["alarm_low_pf"]      = g_alarm_low_pf;
    doc["alarm_frequency"]   = g_alarm_frequency;
    doc["alarm_pmp51"]       = g_alarm_pmp;
    doc["alarm_prowirl"]     = g_alarm_pw;
    doc["alarm_temp"]        = g_alarm_temp;
    doc["alarm_gas_debit"]         = g_alarm_gas;      
    doc["alarm_gas_total"]      = g_alarm_zenner;   
    doc["alarm_flow"]        = g_alarm_flow;
    doc["alarm_comm"]        = g_alarm_comm_pm || g_alarm_comm_pmp || g_alarm_comm_pw || g_alarm_comm_zenner;

    doc["device_id"]      = "UNIFIED_01";

    String json;
    serializeJson(doc, json);

    HTTPClient http;
    http.begin(API_URL);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    int code = http.POST(json);
    http.end();

    Serial.printf("[API] %s  code=%d\n",
        code == 200 ? "OK" : "ERREUR", code);
}

// ════════════════════════════════════════════════════════════════
//  DISPLAY — callbacks LVGL
// ════════════════════════════════════════════════════════════════

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area,
                   lv_color_t *color_p) {
    if (gfx.getStartCount() > 0) gfx.endWrite();
    gfx.pushImageDMA(area->x1, area->y1,
                     area->x2 - area->x1 + 1,
                     area->y2 - area->y1 + 1,
                     (lgfx::rgb565_t*)&color_p->full);
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    uint16_t touchX, touchY;
    data->state = LV_INDEV_STATE_REL;
    if (gfx.getTouch(&touchX, &touchY)) {
        data->state   = LV_INDEV_STATE_PR;
        data->point.x = LCD_H_RES - touchX;
        data->point.y = touchY;
    }
}

// ════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    Serial.println("[BOOT] Dashboard UNIFIE v1.0");
    Serial.println("[BOOT] PM5100 + E+H PMP51/Prowirl + ZENNER Gaz + DS18B20/YF-S201");

    // ── GT911 Touch reset ────────────────────────────────
    Wire.begin(15, 16);
    delay(50);
    pinMode(1, OUTPUT); pinMode(2, OUTPUT);
    digitalWrite(1, LOW); digitalWrite(2, LOW); delay(20);
    digitalWrite(2, HIGH); delay(100);
    pinMode(1, INPUT);

    // ── Display LovyanGFX ────────────────────────────────
    gfx.init();
    gfx.initDMA();
    gfx.startWrite();
    gfx.fillScreen(TFT_BLACK);

    // ── LVGL ─────────────────────────────────────────────
    lv_init();
    size_t bsz = sizeof(lv_color_t) * LCD_H_RES * LCD_V_RES;
    buf  = (lv_color_t*)heap_caps_malloc(bsz, MALLOC_CAP_SPIRAM);
    buf1 = (lv_color_t*)heap_caps_malloc(bsz, MALLOC_CAP_SPIRAM);
    if (!buf || !buf1) {
        bsz  = sizeof(lv_color_t) * LCD_H_RES * 20;
        buf  = (lv_color_t*)heap_caps_malloc(bsz, MALLOC_CAP_INTERNAL);
        buf1 = (lv_color_t*)heap_caps_malloc(bsz, MALLOC_CAP_INTERNAL);
        lv_disp_draw_buf_init(&draw_buf, buf, buf1, LCD_H_RES * 20);
    } else {
        lv_disp_draw_buf_init(&draw_buf, buf, buf1, LCD_H_RES * LCD_V_RES);
    }

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = LCD_H_RES;
    disp_drv.ver_res  = LCD_V_RES;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // ── Backlight + Touch ─────────────────────────────────
    pinMode(PIN_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_BACKLIGHT, HIGH);
    touch_init(0x5D);
    gfx.fillScreen(TFT_BLACK);

    // ── Dashboard — écran visible immédiatement ───────────
    dashboard_ui_init();
    Serial.println("[BOOT] Ecran OK");

    // ── RS485 BUS 1 — Serial1 — PM5100 ───────────────────
    pinMode(RS485_DE_PIN, OUTPUT);
    digitalWrite(RS485_DE_PIN, LOW);
    Serial1.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    Serial.printf("[BOOT] RS485 Bus1 (PM5100)  Baud=%d 8N1  TX=%d RX=%d DE=%d\n",
        RS485_BAUD, RS485_TX_PIN, RS485_RX_PIN, RS485_DE_PIN);

    // ── RS485 BUS 2 — Serial2 — Émulateur E+H + ZENNER ──────
    pinMode(RS485B_DE_PIN, OUTPUT);
    digitalWrite(RS485B_DE_PIN, LOW);
    // ⚠ SERIAL_8N1 (pas 8E1) — doit correspondre à l'Arduino ZENNER
    Serial2.begin(RS485B_BAUD, SERIAL_8N1, RS485B_RX_PIN, RS485B_TX_PIN);
    Serial.printf("[BOOT] RS485 Bus2 (E+H+ZENNER) Baud=%d 8N1  TX=%d RX=%d DE=%d\n",
        RS485B_BAUD, RS485B_TX_PIN, RS485B_RX_PIN, RS485B_DE_PIN);

    Serial.printf("[BOOT] Adresses: PM5100=%d  PMP51=%d  Prowirl=%d  ZENNER=%d\n",
        MODBUS_ADDR_PM5100, MODBUS_ADDR_PMP51, MODBUS_ADDR_PROWIRL, MODBUS_ADDR_ZENNER);

    // ── DS18B20 ───────────────────────────────────────────
    ds18b20.begin();
    Serial.printf("[BOOT] DS18B20: %d capteur(s) sur GPIO%d\n",
        ds18b20.getDeviceCount(), PIN_DS18B20);
    ds18b20.setResolution(12);


    // ── YF-S201 ───────────────────────────────────────────
    pinMode(PIN_FLOW, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW), flow_isr, RISING);
    last_flow_ms = millis();
    Serial.printf("[BOOT] YF-S201 sur GPIO%d\n", PIN_FLOW);

    
    wifi_connect();

    Serial.println("[BOOT] ✓ Pret — supervision unifiee active !");
}

// ════════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════════

void loop() {
    // ── Modbus RS-485 (séquentiel, 2 sec) ────────────────
    modbus_read();         // PM5100 + PMP51 + Prowirl

    // ── Capteurs environnementaux (2 sec) ─────────────────
    sensors_read();        // DS18B20 (gaz ZENNER dans modbus_read)
    read_flow();           // YF-S201 (fenêtre 1 sec)

    // ── LVGL + Dashboard UI (1 sec) ───────────────────────
    lv_timer_handler();
    dashboard_update();

    // ── Envoi WiFi (30 sec) ───────────────────────────────
    uint32_t now = millis();
    if (now - last_wifi_ms >= WIFI_SEND_MS) {
        last_wifi_ms = now;
        send_to_api();
    }

    delay(5);
}

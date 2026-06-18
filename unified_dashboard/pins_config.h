#pragma once

// ── Résolution affichage ──────────────────────────────────────
#define LCD_H_RES   320
#define LCD_V_RES   240

// ── Backlight ─────────────────────────────────────────────────
#define PIN_BACKLIGHT   38

// ══════════════════════════════════════════════════════════════
//  RS485 BUS 1 — Serial1 — PM5100 (compteur énergie)
//  FC03 — addr=1
// ══════════════════════════════════════════════════════════════
#define RS485_RX_PIN    18   // RO du MAX485 #1
#define RS485_TX_PIN    17   // DI du MAX485 #1
#define RS485_DE_PIN     2   // DE+RE du MAX485 #1
#define RS485_BAUD      9600

// ══════════════════════════════════════════════════════════════
//  RS485 BUS 2 — Serial2 — Émulateur E+H (PMP51 + Prowirl)
//  FC04 — addr=2 (PMP51) / addr=3 (Prowirl)
// ══════════════════════════════════════════════════════════════
#define RS485B_RX_PIN   44   // RO du MAX485 #2
#define RS485B_TX_PIN   43   // DI du MAX485 #2
#define RS485B_DE_PIN   46   // DE+RE du MAX485 #2
#define RS485B_BAUD     9600

// ── Adresses Modbus ──────────────────────────────────────────
// Projet 1 — PM5100 compteur énergie (FC03)
#define MODBUS_ADDR_PM5100    1

// Projet 3 — Instruments E+H + ZENNER Gaz (FC04)
#define MODBUS_ADDR_PMP51     1   // Cerabar PMP51       — pression eau
#define MODBUS_ADDR_PROWIRL   2   // Prowirl 200 DN200   — débit air
#define MODBUS_ADDR_ZENNER    3   // ZENNER Smart Gas G4-G25 — gaz

#define MODBUS_TIMEOUT_MS   600

// ── Registres PM5100 (FC03, Float32, Big Endian) ─────────────
#define REG_PM_VOLTAGE  0x0BD3
#define REG_PM_CURRENT  0x0BB7
#define REG_PM_POWER    0x0BED
#define REG_PM_ENERGY   0x0C8F   // int64, 4 registres
#define REG_PM_PF       0x0C0B
#define REG_PM_FREQ     0x0C25

// ── Registres E+H PMP51 + Prowirl (FC04, Float32, reg 0x0001) ─
#define REG_EH_VALUE    0x0001

// ── Registres ZENNER Smart Gas (FC04, UINT32, Big Endian) ─────
// 0x0001–0x0002 : Index total gaz (m³ × 10) — valeur ÷ 10 = m³ réel
// 0x0003–0x0004 : Débit instantané (m³/h × 100) — valeur ÷ 100 = m³/h réel
#define REG_GAS_INDEX   0x0001   // Index total (registre de départ, 2 reg)
#define REG_GAS_FLOW    0x0003   // Débit instantané (registre de départ, 2 reg)

// Seuil alarme débit gaz (m³/h)
#define GAS_FLOW_ALARM  6.0f     // > Qmax G4 = alarme

// ══════════════════════════════════════════════════════════════
//  CAPTEURS ENVIRONNEMENTAUX (Projet 2)
// ══════════════════════════════════════════════════════════════

// DS18B20 Température (OneWire)
#define PIN_DS18B20     3       // GPIO3 — pull-up 4.7kΩ obligatoire


// YF-S201 Débitmètre Eau (Impulsion)
#define PIN_FLOW        10
//  ⚠ Diviseur de tension 5V→3.3V obligatoire :
//      5V ──[10kΩ]──┬──[20kΩ]── GND
//                   └── GPIO7
#define FLOW_CALIBRATION 7.5f   // pulses/(L/min)
#define FLOW_COUNT_MS    1000   // fenêtre comptage 1 sec

// ══════════════════════════════════════════════════════════════
//  SEUILS D'ALARME
// ══════════════════════════════════════════════════════════════

// PM5100 — énergie + électrique
#define VOLTAGE_NOM       220.0f
#define VOLTAGE_MIN       200.0f
#define VOLTAGE_MAX       240.0f
#define POWER_ALARM_KW    40.0f
#define TARIF_DH_KWH      0.50f
#define CURRENT_ALARM_A   80.0f    // Surintensité
#define PF_ALARM_MIN      0.8f     // Facteur de puissance faible
#define FREQ_ALARM_MIN    49.0f    // Fréquence basse (Hz)
#define FREQ_ALARM_MAX    51.0f    // Fréquence haute (Hz)

// E+H PMP51 — pression eau (bar)
#define PMP51_ALARM_HIGH    5.5f
#define PMP51_ALARM_LOW     0.5f

// E+H Prowirl 200 — débit air (m³/h)
#define PROWIRL_QMIN       160.0f
#define PROWIRL_ALARM_HIGH 2880.0f

// DS18B20 — température
#define TEMP_MIN            0.0f
#define TEMP_MAX           50.0f

// ── Seuils alarme ZENNER (m³/h) ──────────────────────────────
#define GAS_WARN_FLOW      3.50f   // m³/h — avertissement débit élevé
#define GAS_ALARM_FLOW     5.00f   // m³/h — alarme débit critique

// ── Seuils alarme ZENNER (m³ cumulés × 10) ───────────────────
#define GAS_WARN_INDEX     15000UL  // = 1 500,0 m³
#define GAS_ALARM_INDEX    20000UL  // = 2 000,0 m³

// YF-S201 — débit eau
#define FLOW_MIN            0.1f
#define FLOW_MAX           25.0f

// ══════════════════════════════════════════════════════════════
//  TIMING
// ══════════════════════════════════════════════════════════════
#define SENSOR_READ_MS    2000   // capteurs env. toutes 2 sec
#define MODBUS_READ_MS    2000   // lecture Modbus toutes 2 sec
#define WIFI_SEND_MS     30000   // envoi API toutes 30 sec

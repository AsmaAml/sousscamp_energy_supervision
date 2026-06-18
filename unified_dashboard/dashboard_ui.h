#pragma once
#include <lvgl.h>

// Donnees PM5100
extern float g_voltage_v;
extern float g_current_a;
extern float g_power_kw;
extern float g_energy_kwh;
extern float g_power_factor;
extern float g_frequency_hz;
extern float g_cost_dh;
extern bool  g_alarm_overload;
extern bool  g_alarm_voltage;
extern bool  g_alarm_comm_pm;
extern bool  g_alarm_overcurrent;   // Surintensité  (I > 80 A)
extern bool  g_alarm_low_pf;        // Facteur de puissance faible (FP < 0.8)
extern bool  g_alarm_frequency;     // Fréquence anormale (< 49 Hz ou > 51 Hz)

// Donnees E+H
extern float g_pression_bar;
extern float g_debit_vol;
extern bool  g_alarm_pmp;
extern bool  g_alarm_pw;
extern bool  g_alarm_comm_pmp;
extern bool  g_alarm_comm_pw;

// Donnees capteurs environnementaux
extern float g_temp_c;
extern bool  g_temp_valid;
extern float g_gas_index_m3;     // Index total gaz ZENNER (m³, 1 décimale)
extern float g_gas_flow_m3h;     // Débit instantané gaz ZENNER (m³/h, 2 décimales)
extern float g_flow_lpm;
extern float g_volume_l;
extern bool  g_alarm_temp;
extern bool  g_alarm_gas;
extern bool  g_alarm_flow;
extern bool  g_alarm_sensor;

// WiFi status
extern bool  g_wifi_connected;

// API
void dashboard_ui_init(void);
void dashboard_update(void);

#include "dashboard_ui.h"
#include "pins_config.h"
#include <Arduino.h>

// Donnees globales
float g_voltage_v      = 0.0f;
float g_current_a      = 0.0f;
float g_power_kw       = 0.0f;
float g_energy_kwh     = 0.0f;
float g_power_factor   = 1.0f;
float g_frequency_hz   = 50.0f;
float g_cost_dh        = 0.0f;
bool  g_alarm_overload = false;
bool  g_alarm_voltage  = false;
bool  g_alarm_comm_pm  = false;
bool  g_alarm_overcurrent = false;  // Surintensité
bool  g_alarm_low_pf      = false;  // FP faible
bool  g_alarm_frequency   = false;  // Fréquence anormale

float g_pression_bar   = 0.0f;
float g_debit_vol      = 0.0f;
bool  g_alarm_pmp      = false;
bool  g_alarm_pw       = false;
bool  g_alarm_comm_pmp = false;
bool  g_alarm_comm_pw  = false;

float g_temp_c         = 0.0f;
bool  g_temp_valid     = false;
float g_gas_index_m3   = 0.0f;   // Index total gaz ZENNER (m³)
float g_gas_flow_m3h   = 0.0f;   // Débit instantané ZENNER (m³/h)
float g_flow_lpm       = 0.0f;
float g_volume_l       = 0.0f;
bool  g_alarm_temp     = false;
bool  g_alarm_gas      = false;
bool  g_alarm_flow     = false;
bool  g_alarm_sensor   = false;

bool  g_wifi_connected = false;

// Palette couleurs
#define C_BG      lv_color_hex(0x0D1B2A)
#define C_HDR     lv_color_hex(0x071320)
#define C_CARD    lv_color_hex(0x0F2030)
#define C_CARD2   lv_color_hex(0x0A1C2C)
#define C_BORD    lv_color_hex(0x1A3A5C)
#define C_CYAN    lv_color_hex(0x00B4D8)  // PM5100
#define C_TEAL    lv_color_hex(0x00C896)  // E+H
#define C_YELLOW  lv_color_hex(0xFFD166)  // Temp
#define C_GREEN   lv_color_hex(0x2DC653)  // OK / Air
#define C_PURPLE  lv_color_hex(0xA78BFA)  // Eau
#define C_ORANGE  lv_color_hex(0xF4A261)  // Cout / warn
#define C_RED     lv_color_hex(0xE63946)  // Alarme
#define C_TEXT    lv_color_hex(0xD0E8F0)  // Texte clair
#define C_MUTED   lv_color_hex(0x5A7A90)  // Texte discret
#define C_DRK_G   lv_color_hex(0x0A2A15)
#define C_DRK_R   lv_color_hex(0x2A0A0A)

// Widgets dynamiques
static lv_obj_t *scr;

// Header
static lv_obj_t *w_time, *w_wifi, *w_err;

// PM5100
static lv_obj_t *w_volt, *w_freq, *w_pow, *w_fp;
static lv_obj_t *w_energy, *w_cost_pm;

// E+H
static lv_obj_t *w_pmp, *w_pmp_alm;
static lv_obj_t *w_pw,  *w_pw_alm;

// Capteurs droite
static lv_obj_t *w_temp, *w_temp_alm;
static lv_obj_t *w_gas_index;    // Index total gaz ZENNER
static lv_obj_t *w_gas_flow;     // Débit instantané gaz ZENNER
static lv_obj_t *w_flow, *w_vol;

// Barres
static lv_obj_t *w_bar_p, *w_bar_p_pct;
static lv_obj_t *w_bar_q, *w_bar_q_pct;

// LEDs
static lv_obj_t *led_pm, *led_eh, *led_t, *led_air, *led_eau;
static lv_obj_t *w_rs485;

// Barre bas
static lv_obj_t *w_cur, *w_cur_pm, *w_cost_big, *w_fps;

// Timing
static uint32_t t_last  = 0;
static uint32_t uptime  = 0;
static uint32_t fps_cnt = 0;
static uint32_t fps_t   = 0;
static uint16_t fps_val = 0;

// ─────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────

static lv_obj_t* card(lv_obj_t *p, int x,int y,int w,int h,
                      lv_color_t bg, lv_color_t bc) {
    lv_obj_t *c = lv_obj_create(p);
    lv_obj_set_pos(c,x,y); lv_obj_set_size(c,w,h);
    lv_obj_set_style_bg_color(c,bg,0);
    lv_obj_set_style_bg_opa(c,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(c,bc,0);
    lv_obj_set_style_border_width(c,1,0);
    lv_obj_set_style_radius(c,3,0);
    lv_obj_set_style_pad_all(c,3,0);
    lv_obj_clear_flag(c,LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t* lbl(lv_obj_t *p, const char *t,
                     lv_color_t col, const lv_font_t *f,
                     lv_align_t a, int ox, int oy) {
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l,t);
    lv_obj_set_style_text_color(l,col,0);
    lv_obj_set_style_text_font(l,f,0);
    lv_obj_align(l,a,ox,oy);
    return l;
}

static lv_obj_t* lbl_pos(lv_obj_t *p, const char *t,
                          lv_color_t col, const lv_font_t *f,
                          int x, int y) {
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l,t);
    lv_obj_set_style_text_color(l,col,0);
    lv_obj_set_style_text_font(l,f,0);
    lv_obj_set_pos(l,x,y);
    return l;
}

static lv_obj_t* sep_h(lv_obj_t *p, int x,int y,int w) {
    lv_obj_t *s = lv_obj_create(p);
    lv_obj_set_pos(s,x,y); lv_obj_set_size(s,w,1);
    lv_obj_set_style_bg_color(s,C_BORD,0);
    lv_obj_set_style_bg_opa(s,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(s,0,0);
    lv_obj_clear_flag(s,LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

static lv_obj_t* sep_v(lv_obj_t *p, int x,int y,int h) {
    lv_obj_t *s = lv_obj_create(p);
    lv_obj_set_pos(s,x,y); lv_obj_set_size(s,1,h);
    lv_obj_set_style_bg_color(s,C_BORD,0);
    lv_obj_set_style_bg_opa(s,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(s,0,0);
    lv_obj_clear_flag(s,LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

static lv_obj_t* led8(lv_obj_t *p, int x, int y) {
    lv_obj_t *d = lv_obj_create(p);
    lv_obj_set_pos(d,x,y); lv_obj_set_size(d,9,9);
    lv_obj_set_style_radius(d,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(d,C_DRK_G,0);
    lv_obj_set_style_bg_opa(d,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(d,C_GREEN,0);
    lv_obj_set_style_border_width(d,1,0);
    lv_obj_clear_flag(d,LV_OBJ_FLAG_SCROLLABLE);
    return d;
}

static void led_set(lv_obj_t *d, bool alarm) {
    lv_obj_set_style_bg_color(d, alarm ? C_DRK_R  : C_DRK_G, 0);
    lv_obj_set_style_border_color(d, alarm ? C_RED : C_GREEN, 0);
}

// ─────────────────────────────────────────────────────────────
//  Build
// ─────────────────────────────────────────────────────────────

static void build(void) {
    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr,C_BG,0);
    lv_obj_set_style_bg_opa(scr,LV_OPA_COVER,0);
    lv_obj_clear_flag(scr,LV_OBJ_FLAG_SCROLLABLE);

    // ── HEADER y=0 h=22 ───────────────────────────────────
    lv_obj_t *hdr = card(scr,0,0,320,22,C_HDR,C_BORD);
    lv_obj_set_style_radius(hdr,0,0);
    lv_obj_set_style_border_side(hdr,LV_BORDER_SIDE_BOTTOM,0);
    lv_obj_set_style_border_color(hdr,C_CYAN,0);

    lv_obj_t *ht = lv_label_create(hdr);
    lv_label_set_text(ht, LV_SYMBOL_CHARGE " SUPERVISION");
    lv_obj_set_style_text_color(ht,C_CYAN,0);
    lv_obj_set_style_text_font(ht,&lv_font_montserrat_12,0);
    lv_obj_align(ht,LV_ALIGN_LEFT_MID,2,0);

    w_time = lbl(hdr,"00:00:00",C_MUTED,&lv_font_montserrat_10,LV_ALIGN_CENTER,0,0);
    w_wifi = lbl(hdr,LV_SYMBOL_WIFI,C_MUTED,&lv_font_montserrat_10,LV_ALIGN_RIGHT_MID,-48,0);
    w_err  = lbl(hdr,"",C_GREEN,&lv_font_montserrat_10,LV_ALIGN_RIGHT_MID,-2,0);

    // ── COL GAUCHE — PM5100  x=0 y=22 w=109 h=140 ────────
    lv_obj_t *cpm = card(scr,0,22,109,140,C_CARD,C_CYAN);

    lbl_pos(cpm, LV_SYMBOL_CHARGE " PM5100", C_CYAN,  &lv_font_montserrat_10, 2, 0);

    // Tension + freq sur meme ligne
    w_volt = lbl_pos(cpm,"220V",C_TEXT,&lv_font_montserrat_14,2,13);
    w_freq = lbl_pos(cpm,"50Hz",C_MUTED,&lv_font_montserrat_10,64,16);

    // Puissance en grand
    w_pow  = lbl_pos(cpm,"0.0 kW",C_CYAN,&lv_font_montserrat_20,2,30);

    // FP
    w_fp   = lbl_pos(cpm,"FP: 1.00",C_MUTED,&lv_font_montserrat_10,2,54);

    sep_h(cpm,0,65,103);

    // Energie
    lbl_pos(cpm,"ENERGIE",C_MUTED,&lv_font_montserrat_10,2,67);
    w_energy = lbl_pos(cpm,"0 Wh",C_TEXT,&lv_font_montserrat_12,2,78);

    // Cout
    w_cost_pm = lbl_pos(cpm,"0.00 DH",C_ORANGE,&lv_font_montserrat_12,2,93);

    // Courant — encadre distinct en bas
    lv_obj_t *ci = lv_obj_create(cpm);
    lv_obj_set_pos(ci,0,108); lv_obj_set_size(ci,103,26);
    lv_obj_set_style_bg_color(ci,lv_color_hex(0x0A2840),0);
    lv_obj_set_style_bg_opa(ci,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(ci,C_CYAN,0);
    lv_obj_set_style_border_width(ci,1,0);
    lv_obj_set_style_radius(ci,3,0);
    lv_obj_set_style_pad_all(ci,3,0);
    lv_obj_clear_flag(ci,LV_OBJ_FLAG_SCROLLABLE);

    lbl(ci,"I:",C_MUTED,&lv_font_montserrat_10,LV_ALIGN_LEFT_MID,2,0);
    w_cur_pm = lbl(ci, "0.00 A", C_CYAN, &lv_font_montserrat_16, LV_ALIGN_RIGHT_MID, -2, 0);

    // ── COL CENTRE — PMP51  x=110 y=22 w=101 h=69 ────────
    lv_obj_t *cpmp = card(scr,110,22,101,69,C_CARD,C_TEAL);

    lbl(cpmp,"PRESSION",C_MUTED,&lv_font_montserrat_10,LV_ALIGN_TOP_MID,0,0);
    lbl_pos(cpmp,"PMP51",C_TEAL,&lv_font_montserrat_10,2,0);

    w_pmp = lv_label_create(cpmp);
    lv_label_set_text(w_pmp,"0.00");
    lv_obj_set_style_text_color(w_pmp,C_TEAL,0);
    lv_obj_set_style_text_font(w_pmp,&lv_font_montserrat_28,0);
    lv_obj_align(w_pmp,LV_ALIGN_CENTER,0,5);

    lbl(cpmp,"bar",C_MUTED,&lv_font_montserrat_10,LV_ALIGN_BOTTOM_MID,0,0);

    w_pmp_alm = lbl(cpmp,"",C_RED,&lv_font_montserrat_12,LV_ALIGN_TOP_RIGHT,0,0);

    // ── COL CENTRE — Prowirl  x=110 y=92 w=101 h=70 ──────
    lv_obj_t *cpw = card(scr,110,92,101,70,C_CARD2,C_TEAL);

    lbl(cpw,"DEBIT AIR",C_MUTED,&lv_font_montserrat_10,LV_ALIGN_TOP_MID,0,0);
    lbl_pos(cpw,"PW200",C_TEAL,&lv_font_montserrat_10,2,0);

    w_pw = lv_label_create(cpw);
    lv_label_set_text(w_pw,"0");
    lv_obj_set_style_text_color(w_pw,C_TEAL,0);
    lv_obj_set_style_text_font(w_pw,&lv_font_montserrat_28,0);
    lv_obj_align(w_pw,LV_ALIGN_CENTER,0,5);

    lbl(cpw,"m3/h",C_MUTED,&lv_font_montserrat_10,LV_ALIGN_BOTTOM_MID,0,0);
    w_pw_alm = lbl(cpw,"",C_RED,&lv_font_montserrat_12,LV_ALIGN_TOP_RIGHT,0,0);

    // ── COL DROITE — Temp  x=212 y=22 w=108 h=57 ─────────
    lv_obj_t *ctemp = card(scr,212,22,108,57,C_CARD,C_YELLOW);

    lbl_pos(ctemp,LV_SYMBOL_WARNING " TEMP",C_YELLOW,&lv_font_montserrat_10,2,0);
    lbl(ctemp,"DS18B20",C_MUTED,&lv_font_montserrat_10,LV_ALIGN_TOP_RIGHT,0,0);

    w_temp = lv_label_create(ctemp);
    lv_label_set_text(w_temp,"--.-");
    lv_obj_set_style_text_color(w_temp,C_YELLOW,0);
    lv_obj_set_style_text_font(w_temp,&lv_font_montserrat_28,0);
    lv_obj_align(w_temp,LV_ALIGN_CENTER,0,5);

    lbl(ctemp,"°C",C_MUTED,&lv_font_montserrat_10,LV_ALIGN_BOTTOM_MID,0,0);
    w_temp_alm = lbl(ctemp,"",C_RED,&lv_font_montserrat_12,LV_ALIGN_BOTTOM_RIGHT,0,0);

    // ── COL DROITE — Gaz ZENNER  x=212 y=80 w=108 h=41 ──────
    lv_obj_t *cgas = card(scr,212,80,108,41,C_CARD2,C_GREEN);

    lbl_pos(cgas,"GAZ ZENNER",C_MUTED,&lv_font_montserrat_10,2,0);
    lbl(cgas,"G4-G25",C_MUTED,&lv_font_montserrat_10,LV_ALIGN_TOP_RIGHT,0,0);

    // Valeur 1 : Index total — grand a gauche (comme la photo)
    w_gas_index = lv_label_create(cgas);
    lv_label_set_text(w_gas_index,"0.0");
    lv_obj_set_style_text_color(w_gas_index,C_GREEN,0);
    lv_obj_set_style_text_font(w_gas_index,&lv_font_montserrat_20,0);
    lv_obj_align(w_gas_index,LV_ALIGN_LEFT_MID,2,3);

    // Unite index — bas gauche
    lbl(cgas,"m3",C_MUTED,&lv_font_montserrat_10,LV_ALIGN_BOTTOM_LEFT,2,0);

    // Valeur 2 : Debit instantane — petit a droite (comme la photo)
    w_gas_flow = lv_label_create(cgas);
    lv_label_set_text(w_gas_flow,"0 L");
    lv_obj_set_style_text_color(w_gas_flow,C_MUTED,0);
    lv_obj_set_style_text_font(w_gas_flow,&lv_font_montserrat_10,0);
    lv_obj_align(w_gas_flow,LV_ALIGN_RIGHT_MID,-2,-3);

    // Unite debit — bas droite
    lbl(cgas,"m3/h",C_MUTED,&lv_font_montserrat_10,LV_ALIGN_BOTTOM_RIGHT,-2,0);

    // ── COL DROITE — Eau  x=212 y=122 w=108 h=40 ─────────
    lv_obj_t *ceau = card(scr,212,122,108,40,C_CARD,lv_color_hex(0x7B61CC));

    lbl_pos(ceau,"EAU",C_PURPLE,&lv_font_montserrat_10,2,0);
    lbl(ceau,"YF-S201",C_MUTED,&lv_font_montserrat_10,LV_ALIGN_TOP_RIGHT,0,0);

    // Debit — valeur lisible en grand
    w_flow = lv_label_create(ceau);
    lv_label_set_text(w_flow,"0.00");
    lv_obj_set_style_text_color(w_flow,C_PURPLE,0);
    lv_obj_set_style_text_font(w_flow,&lv_font_montserrat_20,0);
    lv_obj_align(w_flow,LV_ALIGN_LEFT_MID,2,3);

    // Unite L/min sous la valeur
    lbl(ceau,"L/min",C_MUTED,&lv_font_montserrat_10,LV_ALIGN_BOTTOM_LEFT,2,0);

    // Volume cumule a droite
    w_vol = lbl(ceau,"0 L",C_MUTED,&lv_font_montserrat_10,LV_ALIGN_BOTTOM_RIGHT,-2,0);

    // ── BARRES  y=162..187 ────────────────────────────────
    // Barre Pression
    lv_obj_t *rb1 = lv_obj_create(scr);
    lv_obj_set_pos(rb1,0,162); lv_obj_set_size(rb1,320,13);
    lv_obj_set_style_bg_color(rb1,C_HDR,0);
    lv_obj_set_style_bg_opa(rb1,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(rb1,0,0);
    lv_obj_set_style_radius(rb1,0,0);
    lv_obj_set_style_pad_all(rb1,1,0);
    lv_obj_clear_flag(rb1,LV_OBJ_FLAG_SCROLLABLE);

    lbl(rb1,"P:",C_TEAL,&lv_font_montserrat_10,LV_ALIGN_LEFT_MID,2,0);

    w_bar_p = lv_bar_create(rb1);
    lv_obj_set_size(w_bar_p,270,5);
    lv_obj_align(w_bar_p,LV_ALIGN_CENTER,-12,0);
    lv_bar_set_range(w_bar_p,0,600);
    lv_obj_set_style_bg_color(w_bar_p,C_BORD,LV_PART_MAIN);
    lv_obj_set_style_bg_color(w_bar_p,C_TEAL,LV_PART_INDICATOR);
    lv_obj_set_style_radius(w_bar_p,2,LV_PART_MAIN);
    lv_obj_set_style_radius(w_bar_p,2,LV_PART_INDICATOR);

    w_bar_p_pct = lbl(rb1,"0%",C_TEAL,&lv_font_montserrat_10,LV_ALIGN_RIGHT_MID,-2,0);

    // Barre Debit
    lv_obj_t *rb2 = lv_obj_create(scr);
    lv_obj_set_pos(rb2,0,175); lv_obj_set_size(rb2,320,13);
    lv_obj_set_style_bg_color(rb2,C_HDR,0);
    lv_obj_set_style_bg_opa(rb2,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(rb2,0,0);
    lv_obj_set_style_radius(rb2,0,0);
    lv_obj_set_style_pad_all(rb2,1,0);
    lv_obj_clear_flag(rb2,LV_OBJ_FLAG_SCROLLABLE);

    lbl(rb2,"Q:",C_TEAL,&lv_font_montserrat_10,LV_ALIGN_LEFT_MID,2,0);

    w_bar_q = lv_bar_create(rb2);
    lv_obj_set_size(w_bar_q,270,5);
    lv_obj_align(w_bar_q,LV_ALIGN_CENTER,-12,0);
    lv_bar_set_range(w_bar_q,0,3200);
    lv_obj_set_style_bg_color(w_bar_q,C_BORD,LV_PART_MAIN);
    lv_obj_set_style_bg_color(w_bar_q,C_TEAL,LV_PART_INDICATOR);
    lv_obj_set_style_radius(w_bar_q,2,LV_PART_MAIN);
    lv_obj_set_style_radius(w_bar_q,2,LV_PART_INDICATOR);

    w_bar_q_pct = lbl(rb2,"0%",C_TEAL,&lv_font_montserrat_10,LV_ALIGN_RIGHT_MID,-2,0);

    // ── LIGNE LEDs  y=188 h=13 ────────────────────────────
    lv_obj_t *ral = lv_obj_create(scr);
    lv_obj_set_pos(ral,0,188); lv_obj_set_size(ral,320,13);
    lv_obj_set_style_bg_color(ral,C_HDR,0);
    lv_obj_set_style_bg_opa(ral,LV_OPA_COVER,0);
    lv_obj_set_style_border_side(ral,LV_BORDER_SIDE_TOP,0);
    lv_obj_set_style_border_color(ral,C_BORD,0);
    lv_obj_set_style_border_width(ral,1,0);
    lv_obj_set_style_radius(ral,0,0);
    lv_obj_set_style_pad_all(ral,1,0);
    lv_obj_clear_flag(ral,LV_OBJ_FLAG_SCROLLABLE);

    const char *led_names[] = {"PM","E+H","T","Air","Eau"};
    lv_obj_t **led_ptrs[]   = {&led_pm,&led_eh,&led_t,&led_air,&led_eau};
    int xpos[] = {2, 34, 72, 94, 124};

    for (int i = 0; i < 5; i++) {
        *(led_ptrs[i]) = led8(ral, xpos[i], 1);
        lv_obj_t *lt = lv_label_create(ral);
        lv_label_set_text(lt, led_names[i]);
        lv_obj_set_style_text_color(lt,C_GREEN,0);
        lv_obj_set_style_text_font(lt,&lv_font_montserrat_10,0);
        lv_obj_set_pos(lt, xpos[i]+11, 0);
    }

    w_rs485 = lbl(ral,"RS485:OK",C_GREEN,&lv_font_montserrat_10,LV_ALIGN_RIGHT_MID,-2,0);

    // ── BARRE BAS  y=201 h=39 ─────────────────────────────
    // 3 zones : Courant | Cout | FPS/CPU
    lv_obj_t *bot = lv_obj_create(scr);
    lv_obj_set_pos(bot,0,201); lv_obj_set_size(bot,320,39);
    lv_obj_set_style_bg_color(bot,C_HDR,0);
    lv_obj_set_style_bg_opa(bot,LV_OPA_COVER,0);
    lv_obj_set_style_border_side(bot,LV_BORDER_SIDE_TOP,0);
    lv_obj_set_style_border_color(bot,C_BORD,0);
    lv_obj_set_style_border_width(bot,1,0);
    lv_obj_set_style_radius(bot,0,0);
    lv_obj_set_style_pad_all(bot,3,0);
    lv_obj_clear_flag(bot,LV_OBJ_FLAG_SCROLLABLE);

    // Zone courant (gauche)
    lbl_pos(bot,"COURANT",C_MUTED,&lv_font_montserrat_10,3,0);
    w_cur = lbl_pos(bot,"0.00 A",C_CYAN,&lv_font_montserrat_16,3,12);

    sep_v(bot,104,2,33);

    // Zone cout (centre)
    lbl_pos(bot,"COUT",C_MUTED,&lv_font_montserrat_10,110,0);
    w_cost_big = lbl_pos(bot,"0.00 DH",C_ORANGE,&lv_font_montserrat_16,110,12);

    sep_v(bot,224,2,33);

    // Zone FPS (droite)
    w_fps = lbl(bot,"-- FPS\n-% CPU",C_MUTED,&lv_font_montserrat_12,LV_ALIGN_RIGHT_MID,-3,0);
}

// ─────────────────────────────────────────────────────────────
//  Init
// ─────────────────────────────────────────────────────────────

void dashboard_ui_init(void) {
    build();
    lv_scr_load(scr);
    Serial.println("[UI] Dashboard UNIFIE v3.0 OK");
}

// ─────────────────────────────────────────────────────────────
//  Update  (appele depuis loop, toutes les ~5ms)
// ─────────────────────────────────────────────────────────────

void dashboard_update(void) {
    // FPS counter
    fps_cnt++;
    uint32_t now = millis();
    if (now - fps_t >= 1000) {
        fps_val = (uint16_t)fps_cnt;
        fps_cnt = 0;
        fps_t   = now;
    }

    if (now - t_last < 1000) return;
    t_last = now;
    uptime++;

    char b[32];

    // ── Calcul alarmes ────────────────────────────────────
    g_alarm_voltage     = (g_voltage_v > 1.0f) &&
                          (g_voltage_v < VOLTAGE_MIN || g_voltage_v > VOLTAGE_MAX);
    g_alarm_overload = (g_power_kw > POWER_ALARM_KW);
    g_alarm_overcurrent = (g_current_a > CURRENT_ALARM_A);
    g_alarm_low_pf      = (g_power_factor < PF_ALARM_MIN);
    g_alarm_frequency   = (g_frequency_hz < FREQ_ALARM_MIN || g_frequency_hz > FREQ_ALARM_MAX);
    g_alarm_pmp      = (g_pression_bar > PMP51_ALARM_HIGH) ||
                       (g_pression_bar > 0.1f && g_pression_bar < PMP51_ALARM_LOW);
    g_alarm_pw       = (g_debit_vol > PROWIRL_ALARM_HIGH) ||
                       (g_debit_vol > 1.0f && g_debit_vol < PROWIRL_QMIN);
    g_alarm_temp     = g_temp_valid &&
                       (g_temp_c < TEMP_MIN || g_temp_c > TEMP_MAX);
    g_alarm_gas      = (g_gas_flow_m3h > GAS_FLOW_ALARM);
    g_alarm_flow     = (g_flow_lpm < FLOW_MIN && g_flow_lpm > 0.05f) ||
                        g_flow_lpm > FLOW_MAX;
    g_cost_dh        = (g_energy_kwh / 1000.0f) * TARIF_DH_KWH;

    bool alarm_pm  = g_alarm_overload || g_alarm_voltage || g_alarm_comm_pm
                   || g_alarm_overcurrent || g_alarm_low_pf || g_alarm_frequency;
    bool alarm_eh  = g_alarm_pmp || g_alarm_pw || g_alarm_comm_pmp || g_alarm_comm_pw;
    bool alarm_rs  = g_alarm_comm_pm || g_alarm_comm_pmp || g_alarm_comm_pw;
    bool alarm_any = alarm_pm || alarm_eh || g_alarm_temp || g_alarm_gas || g_alarm_flow;

    // ── PM5100 ───────────────────────────────────────────
    snprintf(b,sizeof(b),"%.0fV", g_voltage_v);
    lv_label_set_text(w_volt,b);
    lv_obj_set_style_text_color(w_volt, g_alarm_voltage ? C_RED : C_TEXT, 0);

    snprintf(b,sizeof(b),"%.1fHz", g_frequency_hz);
    lv_label_set_text(w_freq,b);
    lv_obj_set_style_text_color(w_freq, g_alarm_frequency ? C_RED : C_MUTED, 0);

    snprintf(b,sizeof(b),"%.2f kW", g_power_kw);
    lv_label_set_text(w_pow,b);
    lv_obj_set_style_text_color(w_pow, g_alarm_overload ? C_RED : C_CYAN, 0);

    snprintf(b,sizeof(b),"FP: %.2f", g_power_factor);
    lv_label_set_text(w_fp,b);
    lv_obj_set_style_text_color(w_fp, g_alarm_low_pf ? C_ORANGE : C_MUTED, 0);

    // Energie — toujours en Wh (unite fixe, pas de conversion)
    snprintf(b,sizeof(b),"%.0f Wh", g_energy_kwh);
    lv_label_set_text(w_energy,b);

    snprintf(b,sizeof(b),"%.2f DH", g_cost_dh);
    lv_label_set_text(w_cost_pm,b);
    lv_label_set_text(w_cost_big,b);

    // Courant — meme valeur dans 2 endroits
    snprintf(b,sizeof(b),"%.2f A", g_current_a);
    lv_label_set_text(w_cur,b);
    lv_obj_set_style_text_color(w_cur, g_alarm_overcurrent ? C_RED : C_CYAN, 0);

    lv_label_set_text(w_cur_pm, b);
    lv_obj_set_style_text_color(w_cur_pm, g_alarm_overcurrent ? C_RED : C_CYAN, 0);

    // ── PMP51 ────────────────────────────────────────────
    snprintf(b,sizeof(b),"%.2f", g_pression_bar);
    lv_label_set_text(w_pmp,b);
    lv_obj_set_style_text_color(w_pmp, g_alarm_pmp ? C_RED : C_TEAL, 0);
    lv_label_set_text(w_pmp_alm, g_alarm_pmp ? "!" : "");

    // ── Prowirl ──────────────────────────────────────────
    snprintf(b,sizeof(b),"%.0f", g_debit_vol);
    lv_label_set_text(w_pw,b);
    lv_obj_set_style_text_color(w_pw, g_alarm_pw ? C_RED : C_TEAL, 0);
    lv_label_set_text(w_pw_alm, g_alarm_pw ? "!" : "");

    // ── Temperature ───────────────────────────────────────
    if (g_temp_valid)
        snprintf(b,sizeof(b),"%.1f", g_temp_c);
    else
        snprintf(b,sizeof(b),"--.-");
    lv_label_set_text(w_temp,b);
    lv_obj_set_style_text_color(w_temp,
        (!g_temp_valid || g_alarm_temp) ? C_RED : C_YELLOW, 0);
    lv_label_set_text(w_temp_alm,
        !g_temp_valid ? "?" : (g_alarm_temp ? "!" : ""));

    // ── Gaz ZENNER ────────────────────────────────────────────
    // Index total — 1 décimale (m³)
    snprintf(b,sizeof(b),"%.1f", g_gas_index_m3);
    lv_label_set_text(w_gas_index,b);
    lv_obj_set_style_text_color(w_gas_index, g_alarm_gas ? C_RED : C_GREEN, 0);

    // Débit instantané — 2 décimales (m³/h)
    snprintf(b,sizeof(b),"%.2f", g_gas_flow_m3h);
    lv_label_set_text(w_gas_flow,b);
    lv_obj_set_style_text_color(w_gas_flow, g_alarm_gas ? C_RED : C_MUTED, 0);

    // ── Debit eau YF-S201 ────────────────────────────────
    snprintf(b,sizeof(b),"%.2f", g_flow_lpm);
    lv_label_set_text(w_flow,b);
    lv_obj_set_style_text_color(w_flow, g_alarm_flow ? C_RED : C_PURPLE, 0);
    
    if (g_volume_l >= 1000.0f)
        snprintf(b, sizeof(b), "%.1f kL", g_volume_l / 1000.0f);
    else if (g_volume_l >= 10.0f)
        snprintf(b, sizeof(b), "%.1f L", g_volume_l);   // ex: 18.3 L
    else
        snprintf(b, sizeof(b), "%.2f L", g_volume_l);   // ex: 1.83 L
    lv_label_set_text(w_vol, b);

    // ── Barres ───────────────────────────────────────────
    {
        int pct = constrain((int)((g_pression_bar/6.0f)*100.0f),0,100);
        lv_bar_set_value(w_bar_p,(int)(g_pression_bar*100.0f),LV_ANIM_ON);
        lv_color_t pc = pct<70?C_TEAL:pct<90?C_ORANGE:C_RED;
        lv_obj_set_style_bg_color(w_bar_p,pc,LV_PART_INDICATOR);
        snprintf(b,sizeof(b),"%d%%",pct);
        lv_label_set_text(w_bar_p_pct,b);
        lv_obj_set_style_text_color(w_bar_p_pct,pc,0);
    }
    {
        int pct = constrain((int)((g_debit_vol/3200.0f)*100.0f),0,100);
        lv_bar_set_value(w_bar_q,(int)g_debit_vol,LV_ANIM_ON);
        lv_color_t qc = pct<70?C_TEAL:pct<90?C_ORANGE:C_RED;
        lv_obj_set_style_bg_color(w_bar_q,qc,LV_PART_INDICATOR);
        snprintf(b,sizeof(b),"%d%%",pct);
        lv_label_set_text(w_bar_q_pct,b);
        lv_obj_set_style_text_color(w_bar_q_pct,qc,0);
    }

    // ── LEDs ─────────────────────────────────────────────
    led_set(led_pm,  alarm_pm);
    led_set(led_eh,  alarm_eh);
    led_set(led_t,   g_alarm_temp || g_alarm_sensor);
    led_set(led_air, g_alarm_gas);
    led_set(led_eau, g_alarm_flow);

    // ── RS485 ────────────────────────────────────────────
    lv_label_set_text(w_rs485, alarm_rs ? "RS485:ERR" : "RS485:OK");
    lv_obj_set_style_text_color(w_rs485, alarm_rs ? C_RED : C_GREEN, 0);

    // ── Header ERR ───────────────────────────────────────
    lv_label_set_text(w_err, alarm_any ?
        LV_SYMBOL_CLOSE " ALM" : LV_SYMBOL_OK " OK");
    lv_obj_set_style_text_color(w_err, alarm_any ? C_RED : C_GREEN, 0);

    // ── WiFi ─────────────────────────────────────────────
    lv_obj_set_style_text_color(w_wifi,
        g_wifi_connected ? C_GREEN : C_MUTED, 0);

    // ── Uptime ────────────────────────────────────────────
    uint32_t h=uptime/3600, m=(uptime%3600)/60, s=uptime%60;
    snprintf(b,sizeof(b),"%02u:%02u:%02u",h,m,s);
    lv_label_set_text(w_time,b);

    // ── FPS ───────────────────────────────────────────────
    uint8_t cpu = (uint8_t)constrain((fps_val*100)/200,0,100);
    snprintf(b,sizeof(b),"%u FPS\n%u%% CPU",fps_val,cpu);
    lv_label_set_text(w_fps,b);
    lv_obj_set_style_text_color(w_fps,
        fps_val<30?C_RED:fps_val<50?C_ORANGE:C_MUTED,0);
}

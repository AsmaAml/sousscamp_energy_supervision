
#include <Arduino.h>

// ── Brochage MAX485 ──────────────────────────────────────────
#define RS485_DE_PIN          6
#define RS485_BAUD         9600

// ── Adresses Modbus ──────────────────────────────────────────
#define ADDR_PMP51            1   // Cerabar PMP51  — pression
#define ADDR_PROWIRL          2   // Prowirl 200    — débit air
#define ADDR_ZENNER           3   // ZENNER Gas Meter

// ── Timeout inter-trame (3.5 char @ 9600 = ~4 ms) ────────────
#define INTER_FRAME_DELAY_US  4000

// ── Buffers réponse ───────────────────────────────────────────
#define RESP_MAX_DATA        80   // max 40 registres × 2 octets
#define RESP_FRAME_MAX       85   // 3 entête + 80 data + 2 CRC

// ============================================================
//  Valeurs simulées — PMP51 Cerabar (pression eau 0–6 bar)
// ============================================================
float pmp_pression   = 0.0f;   // bar  — démarre à 0 (rampe)
float pmp_temp       = 24.8f;   // °C
float pmp_courant    = 12.08f;  // mA
float pmp_pct        = 57.5f;   // %
uint16_t pmp_statut  = 0x0000;

// Holding PMP51
float pmp_lrv  = 0.0f;   // bar — bas de plage
float pmp_urv  = 6.0f;   // bar — haut de plage
float pmp_damp = 2.0f;   // s   — amortissement

// ============================================================
//  Valeurs simulées — Prowirl 200 DN200 (débit air)
// ============================================================
float pw_debit_vol   =    0.0f;  // m³/h — démarre à 0 (rampe)
float pw_debit_mass  = 2432.0f;  // kg/h
float pw_debit_norm  = 9320.0f;  // Nm³/h
float pw_vitesse     =    3.54f; // m/s
float pw_pression    =    6.0f;  // bar
float pw_temp        =   25.0f;  // °C
float pw_freq_vortex =   98.4f;  // Hz
float pw_pct         =   50.0f;  // %
float pw_total       = 125430.0f;// m³
uint16_t pw_diag     = 0x0000;

// Holding Prowirl 200
float pw_qmax    = 3200.0f;   // m³/h
float pw_dn      =  200.0f;   // mm
float pw_kfactor =    0.492f; // imp/m³
float pw_p_ref   =    1.01325f; // bar
float pw_damp    =    2.0f;   // s

// ============================================================
//  Valeurs simulées — ZENNER Smart Gas Meter G4-G25
//  Index = 1 254,3 m³  → stocké × 10 = 12543
//  Débit = 2,50 m³/h   → stocké × 100 = 250
// ============================================================
uint32_t zenner_index  = 12543UL;  // m³ × 10
uint32_t zenner_debit  =   250UL;  // m³/h × 100
uint16_t zenner_statut = 0x0000;

// ============================================================
//  Buffer réception Modbus
// ============================================================
static uint8_t  rxbuf[16];
static uint8_t  rxlen    = 0;
static uint32_t lastByte = 0;

// ============================================================
//  CRC16 Modbus
// ============================================================
uint16_t crc16(const uint8_t* buf, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

// ============================================================
//  Float IEEE754 → 2 registres Modbus Big Endian
// ============================================================
void f2reg(float f, uint16_t &hi, uint16_t &lo) {
    union { float f; uint32_t u; } c;
    c.f = f;
    hi = (c.u >> 16) & 0xFFFF;
    lo =  c.u        & 0xFFFF;
}

// ============================================================
//  UINT32 → 2 registres Modbus Big Endian
// ============================================================
void u32reg(uint32_t val, uint16_t &hi, uint16_t &lo) {
    hi = (uint16_t)((val >> 16) & 0xFFFF);
    lo = (uint16_t)( val        & 0xFFFF);
}

// ============================================================
//  Envoi réponse RS-485
// ============================================================
void sendResponse(uint8_t slaveAddr, uint8_t fc,
                  const uint8_t* data, uint8_t dataLen) {
    if (dataLen > RESP_MAX_DATA) return;
    uint8_t resp[RESP_FRAME_MAX];
    resp[0] = slaveAddr;
    resp[1] = fc;
    resp[2] = dataLen;
    memcpy(resp + 3, data, dataLen);
    uint8_t  total = 3 + dataLen;
    uint16_t crc   = crc16(resp, total);
    resp[total]     = crc & 0xFF;
    resp[total + 1] = (crc >> 8) & 0xFF;

    digitalWrite(RS485_DE_PIN, HIGH);
    delayMicroseconds(200);
    Serial.write(resp, total + 2);
    Serial.flush();
    delayMicroseconds(200);
    digitalWrite(RS485_DE_PIN, LOW);
}

// ============================================================
//  Registres — PMP51
// ============================================================
uint16_t pmp51_get_input(uint16_t reg) {
    uint16_t hi, lo;
    switch (reg) {
        case 0x0001: f2reg(pmp_pression, hi, lo); return hi;
        case 0x0002: f2reg(pmp_pression, hi, lo); return lo;
        case 0x0003: f2reg(pmp_temp,     hi, lo); return hi;
        case 0x0004: f2reg(pmp_temp,     hi, lo); return lo;
        case 0x0005: f2reg(pmp_courant,  hi, lo); return hi;
        case 0x0006: f2reg(pmp_courant,  hi, lo); return lo;
        case 0x0007: f2reg(pmp_pct,      hi, lo); return hi;
        case 0x0008: f2reg(pmp_pct,      hi, lo); return lo;
        case 0x0009: return pmp_statut;
        default:     return 0;
    }
}

uint16_t pmp51_get_holding(uint16_t reg) {
    uint16_t hi, lo;
    switch (reg) {
        case 0x0020: f2reg(pmp_lrv,  hi, lo); return hi;
        case 0x0021: f2reg(pmp_lrv,  hi, lo); return lo;
        case 0x0022: f2reg(pmp_urv,  hi, lo); return hi;
        case 0x0023: f2reg(pmp_urv,  hi, lo); return lo;
        case 0x0024: f2reg(pmp_damp, hi, lo); return hi;
        case 0x0025: f2reg(pmp_damp, hi, lo); return lo;
        default:     return 0;
    }
}

// ============================================================
//  Registres — Prowirl 200
// ============================================================
uint16_t prowirl_get_input(uint16_t reg) {
    uint16_t hi, lo;
    switch (reg) {
        case 0x0001: f2reg(pw_debit_vol,   hi, lo); return hi;
        case 0x0002: f2reg(pw_debit_vol,   hi, lo); return lo;
        case 0x0003: f2reg(pw_debit_mass,  hi, lo); return hi;
        case 0x0004: f2reg(pw_debit_mass,  hi, lo); return lo;
        case 0x0005: f2reg(pw_debit_norm,  hi, lo); return hi;
        case 0x0006: f2reg(pw_debit_norm,  hi, lo); return lo;
        case 0x0007: f2reg(pw_vitesse,     hi, lo); return hi;
        case 0x0008: f2reg(pw_vitesse,     hi, lo); return lo;
        case 0x0009: f2reg(pw_pression,    hi, lo); return hi;
        case 0x000A: f2reg(pw_pression,    hi, lo); return lo;
        case 0x000B: f2reg(pw_temp,        hi, lo); return hi;
        case 0x000C: f2reg(pw_temp,        hi, lo); return lo;
        case 0x000D: f2reg(pw_freq_vortex, hi, lo); return hi;
        case 0x000E: f2reg(pw_freq_vortex, hi, lo); return lo;
        case 0x000F: f2reg(pw_pct,         hi, lo); return hi;
        case 0x0010: f2reg(pw_pct,         hi, lo); return lo;
        case 0x0011: f2reg(pw_total,       hi, lo); return hi;
        case 0x0012: f2reg(pw_total,       hi, lo); return lo;
        case 0x0013: return pw_diag;
        default:     return 0;
    }
}

uint16_t prowirl_get_holding(uint16_t reg) {
    uint16_t hi, lo;
    switch (reg) {
        case 0x0020: f2reg(pw_qmax,    hi, lo); return hi;
        case 0x0021: f2reg(pw_qmax,    hi, lo); return lo;
        case 0x0022: f2reg(pw_dn,      hi, lo); return hi;
        case 0x0023: f2reg(pw_dn,      hi, lo); return lo;
        case 0x0024: f2reg(pw_kfactor, hi, lo); return hi;
        case 0x0025: f2reg(pw_kfactor, hi, lo); return lo;
        case 0x0026: f2reg(pw_p_ref,   hi, lo); return hi;
        case 0x0027: f2reg(pw_p_ref,   hi, lo); return lo;
        case 0x0028: f2reg(pw_damp,    hi, lo); return hi;
        case 0x0029: f2reg(pw_damp,    hi, lo); return lo;
        default:     return 0;
    }
}

// ============================================================
//  Registres — ZENNER
// ============================================================
uint16_t zenner_get_input(uint16_t reg) {
    uint16_t hi, lo;
    switch (reg) {
        case 0x0001: u32reg(zenner_index, hi, lo); return hi;
        case 0x0002: u32reg(zenner_index, hi, lo); return lo;
        case 0x0003: u32reg(zenner_debit, hi, lo); return hi;
        case 0x0004: u32reg(zenner_debit, hi, lo); return lo;
        case 0x0005: return zenner_statut;
        default:     return 0x0000;
    }
}

uint16_t zenner_get_holding(uint16_t reg) {
    switch (reg) {
        case 0x0020: return (uint16_t)ADDR_ZENNER;
        case 0x0021: return 3;   // code baud 9600
        default:     return 0x0000;
    }
}

// ============================================================
//  Traitement d'une trame Modbus reçue
// ============================================================
void processFrame(uint8_t* frame, uint8_t len) {
    if (len < 8) return;

    uint8_t  slaveAddr = frame[0];
    uint8_t  fc        = frame[1];
    uint16_t startReg  = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t count     = ((uint16_t)frame[4] << 8) | frame[5];

    // Vérifier CRC
    uint16_t crcRx   = (uint16_t)frame[len-2] | ((uint16_t)frame[len-1] << 8);
    if (crcRx != crc16(frame, len - 2)) return;

    // Filtres
    if (slaveAddr != ADDR_PMP51 &&
        slaveAddr != ADDR_PROWIRL &&
        slaveAddr != ADDR_ZENNER) return;
    if (fc != 0x03 && fc != 0x04) return;
    if (count == 0 || count > 40) return;

    uint8_t data[RESP_MAX_DATA];
    uint8_t dataLen = (uint8_t)(count * 2);

    for (uint16_t i = 0; i < count; i++) {
        uint16_t reg = startReg + i;
        uint16_t val = 0;

        if (slaveAddr == ADDR_PMP51) {
            val = (fc == 0x04) ? pmp51_get_input(reg)
                               : pmp51_get_holding(reg);
        } else if (slaveAddr == ADDR_PROWIRL) {
            val = (fc == 0x04) ? prowirl_get_input(reg)
                               : prowirl_get_holding(reg);
        } else {  // ADDR_ZENNER
            val = (fc == 0x04) ? zenner_get_input(reg)
                               : zenner_get_holding(reg);
        }

        data[i * 2]     = (uint8_t)((val >> 8) & 0xFF);
        data[i * 2 + 1] = (uint8_t)( val       & 0xFF);
    }

    sendResponse(slaveAddr, fc, data, dataLen);
}

// ============================================================
//  Simulation PMP51 + Prowirl — toutes les 200 ms
//  Rampe linéaire cyclique (0 → max, puis retour à 0)
// ============================================================
static uint32_t lastSimEH = 0;

// Pas d'incrément par tick (200 ms)
#define PMP_STEP    0.02f      // bar  → cycle 0–6 bar en ~60 s
#define PW_STEP    10.0f       // m³/h → cycle 0–3200 m³/h en ~64 s

void updateSimEH() {
    uint32_t now = millis();
    if (now - lastSimEH < 200) return;
    lastSimEH = now;

    // ── PMP51 : rampe 0 → 6 bar ──────────────────────────────
    pmp_pression += PMP_STEP;
    if (pmp_pression > 6.0f) pmp_pression = 0.0f;

    pmp_pct     = (pmp_pression / pmp_urv) * 100.0f;
    pmp_courant = 4.0f + (pmp_pct / 100.0f) * 16.0f;
    pmp_temp    = 24.8f;   // valeur fixe (ou ajuster si besoin)

    // ── Prowirl 200 : rampe 0 → 3200 m³/h ───────────────────
    pw_debit_vol += PW_STEP;
    if (pw_debit_vol > pw_qmax) pw_debit_vol = 0.0f;

    pw_vitesse     = pw_debit_vol / 3600.0f / 0.031416f;
    pw_freq_vortex = pw_kfactor * (pw_debit_vol / 3600.0f);

    float rho      = (pw_pression * 100000.0f) / (287.05f * (pw_temp + 273.15f));
    pw_debit_mass  = pw_debit_vol * rho;
    pw_debit_norm  = pw_debit_vol * (pw_pression / pw_p_ref)
                                  * (293.15f / (pw_temp + 273.15f));

    pw_pct   = (pw_debit_vol / pw_qmax) * 100.0f;
    pw_total += pw_debit_vol / 36000.0f;
    pw_temp   = 25.0f;     // valeur fixe
}

// ============================================================
//  Simulation ZENNER — toutes les 500 ms
//  Rampe linéaire cyclique (0 → 6 m³/h)
//  Index : compteur croissant continu (0 → 4 294 967 295)
// ============================================================
static uint32_t lastSimZen = 0;
static float    debit_reel = 0.0f;
static float    index_acc  = 0.0f;

// Pas d'incrément : 0 → 6 m³/h en ~60 s (tick 500 ms → 120 ticks)
#define ZEN_STEP  0.05f   // m³/h par tick

void updateSimZenner() {
    uint32_t now = millis();
    if (now - lastSimZen < 500) return;
    lastSimZen = now;

    // Rampe débit 0 → 6 m³/h, puis retour à 0
    debit_reel += ZEN_STEP;
    if (debit_reel > 6.0f) debit_reel = 0.0f;

    zenner_debit = (uint32_t)(debit_reel * 100.0f + 0.5f);

    // Index toujours croissant via accumulateur (évite l'arrondi à 0)
    index_acc += debit_reel * 10.0f / 72.0f;
    if (index_acc >= 1.0f) {
        uint32_t full = (uint32_t)index_acc;
        zenner_index += full;          // débordement naturel uint32 OK
        index_acc    -= (float)full;
    }

    zenner_statut = 0x0000;
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
    pinMode(RS485_DE_PIN, OUTPUT);
    digitalWrite(RS485_DE_PIN, LOW);
    // 8N1 — compatible avec les 3 instruments
    Serial.begin(RS485_BAUD, SERIAL_8N1);
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
    updateSimEH();
    updateSimZenner();

    while (Serial.available()) {
        if (rxlen < sizeof(rxbuf)) {
            rxbuf[rxlen++] = (uint8_t)Serial.read();
            lastByte = micros();
        } else {
            Serial.read();
            rxlen = 0;
        }
    }

    if (rxlen > 0 && (micros() - lastByte) > INTER_FRAME_DELAY_US) {
        processFrame(rxbuf, rxlen);
        rxlen = 0;
    }
}

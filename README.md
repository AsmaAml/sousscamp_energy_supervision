# Système de Supervision Énergétique Industrielle — SoussCamp
# Industrial Energy Supervision System — SoussCamp

🎓 Projet de Fin d'Études — Master d'Excellence en « Ingénierie Informatique et Système Embarqué »

Ce projet est le PFE réalisé au Centre d'excellence IT, Faculté des Sciences, Agadir, en collaboration avec l'entreprise **SOUSSCAMP** et le laboratoire **LASIME**. Il présente la conception et le déploiement d'un système intelligent de supervision énergétique basé sur les technologies IIoT.

| Détail | Information |
|---|---|
| Auteure | Asma AMLAL |
| Encadrant | Najib ABEKIRI |
| Entreprise | SOUSSCAMP — Agadir, Maroc |
| Laboratoire | LASIME |
| Année Universitaire | 2025-2026 |

## 💡 Aperçu du Projet
Le système assure la supervision en temps réel de la consommation énergétique du site SoussCamp (électricité, eau, gaz, air comprimé et température), la détection automatique des anomalies et la notification via Telegram.

## ✨ Fonctionnalités Clés
- Collecte des données via Modbus RTU (RS485) et GPIO
- Transmission WiFi HTTPS vers serveur VPS
- Stockage dans InfluxDB 2.7
- Dashboard web 6 pages (HTML5/CSS3/JS)
- Interface locale LVGL sur écran IPS ESP32-S3
- 12 alarmes avec notifications Telegram

## 🛠️ Technologies Utilisées

| Catégorie | Outils |
|---|---|
| Microcontrôleur | CrowPanel ESP32-S3 |
| Protocole | Modbus RTU, RS485, GPIO |
| Backend | FastAPI, Python |
| Base de données | InfluxDB 2.7 |
| Serveur | VPS Contabo, Ubuntu 24.04, Nginx |
| Frontend | HTML5, CSS3, JavaScript |
| Interface locale | LVGL, C++ |
| Notifications | Telegram Bot API |

## 📁 Structure du Dépôt
sousscamp_energy_supervision/

├── energy_api/

│   ├── main.py

│   └── static/

│       ├── index.html

│       ├── logo_sousscamp.png

│       └── icone_sousscamp.png

├── unified_dashboard/

│   ├── unified_dashboard_3.ino

│   ├── dashboard_ui.cpp

│   ├── dashboard_ui.h

│   ├── LovyanGFX_Driver.h

│   ├── pins_config.h

│   ├── touch.cpp

│   └── touch.h

└── arduino_all_slaves/

└── arduino_all_slaves.ino
## 🚀 Mise en Place

### Prérequis
- Python 3.x
- Git
- Arduino IDE

### Installation Backend
```bash
git clone https://github.com/[ton_username]/sousscamp_energy_supervision.git
cd sousscamp_energy_supervision
pip install -r requirements.txt
uvicorn energy_api.main:app --reload
```

### Accès au Dashboard
https://sousscamp.duckdns.org
## 📊 Résultats Clés
- Supervision en temps réel de 6 grandeurs énergétiques
- 12 alarmes configurées avec notifications automatiques
- Dashboard web accessible depuis n'importe quel navigateur
- Latence de transmission inférieure à 30 secondes

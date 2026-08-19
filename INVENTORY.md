# Hardware Inventory — electronics-learning

Authoritative bench inventory for this project. `CLAUDE.md` keeps a short,
learning-oriented summary; **this file is the detailed source of truth.** Update
it when hardware arrives, is gifted, or is returned (part of the 5S closing
ritual). Latest photo audit: **2026-07-24** (with in-situ verifications through
2026-08-15).

Boxes labeled with a Dymo 420P: MCU · SENSORES · PROTO · CONSUMIBLES · CABLES.

---

# 🧰 Electronics Inventory — photo audit 2026-07-24

> 5S estándar: revisar ESTE doc antes de comprar. Actualizarlo al comprar/regalar/devolver (parte del ritual de cierre).
> Cajas etiquetadas con Dymo 420P: MCU · SENSORES · PROTO · CONSUMIBLES · CABLES

## 🟦 MCU — Microcontrollers
| Item | Detalle | Estado |
|---|---|---|
| Arduino UNO R4 WiFi | original, ESP32-S3 radio a bordo | ✅ |
| Arduino MEGA 2560 (clon) | rojo, USB-C | ✅ |
| Arduino UNO (clon CH340) | azul, USB-B | ✅ |
| Freenove ESP32-WROOM ×2 | FNK0090 — para sensores de plantas | ✅ sellado |
| Seeed XIAO SAMD21 + expansion board | OLED, buzzer, RTC, 8× Grove | ✅ |
| Grove Shield para XIAO | sellado | ✅ |
| M5Stack Core | pantalla + 3 botones, caja azul con accesorios | ✅ |
| BBC micro:bit GO | kit completo | ✅ (candidato a regalo) |
| ESP32-S3 D1 UNO ×2 | placa morada formato UNO — ESP32-S3 N16R8 (16MB flash / 8MB PSRAM), WiFi+BT, USB-C, CH340C, barrel jack DC, conector u.FL antena ext. Acepta shields Arduino UNO. SKU TD1UNOESP32-S3 (etiqueta WZHY18108) | ✅ llegó 2026-07-30 · **verificado en sitio 2026-08-11 (foto) ×2** |
| ESP32-S3 DevKit N16R8 + OLED ×2 | **Módulo ESP32-S3-WROOM-1 genuino Espressif** (logo+QR verificados) N16R8, 44 pines, dual USB-C (UART CH343 + USB nativo), botones BOOT/RET, LED RGB, antena PCB (sin u.FL — p/ nodos cerca del AP). Incluye pantalla OLED 0.96" I²C y headers amarillos sin soldar. SKU ESP32S3-N16R8-OLED096-01 · ⚠ solo 1 de las 2 OLED apareció en el lote (ver fila "OLED 0.96 suelta" en 🟨 PROTO) — ubicar la 2ª | ✅ llegó 2026-07-30 · **verificado en sitio 2026-08-11 (foto) ×2** |
| M5Stack CoreS3 Dev Kit | ESP32-S3 @240 MHz, 16MB flash / 8MB PSRAM · LCD IPS 2.0" táctil 320×240 · cámara 0.3MP GC0308 · **dual mic + bocina 1W I2S** · IMU 6 ejes + magnetómetro · sensor luz/prox LTR-553ALS-WA · 3× Grove (I2C/GPIO/UART) · SD · M5BUS · PMIC AXP2101, bat 3.7V 500mAh, DC 9-24V · **DIN Base + hangers incl. (montaje riel/pared, 54×54×31mm)** · MAC 80:45:6B:4D:5C:20 · FCC 2AN3WCORES3 · ⭐ candidato #1 a **satélite de voz HA (proyecto 06)**: mics+speaker+pantalla integrados podrían volver innecesario el INMP441 pendiente de compra | ✅ llegó 2026-08-09 |
| M5Stack StackChan | Robot de escritorio AI open-source (modelo M5STACKCHAN, co-creado con @stack_chan) — ESP32-S3 @240 MHz, 16MB/8MB PSRAM · **servos con feedback: pan 360° + tilt 90°** · dual mic + codec ES7210 · bocina 1W AW88298 · 12× RGB WS2812C · LCD 2.0" táctil (ILI9342C+FT6336U) · cámara GC0308 640×480 · IMU 9 ejes BMI270+BMM150 · touch 3 zonas · **NFC ST25R3916** (supera al RC522 p/ idea armar/desarmar con llavero) · **IR Rx/Tx IRM56384** (blaster p/ TV/AC desde HA) · RTC BM8563 · 550 mAh · Grove ×3 · microSD · montaje LEGO · ⭐ satélite de voz CON PERSONALIDAD: cara animada + sigue con la mirada (servos+IMU) — hermano expresivo del CoreS3 p/ proyecto 06 | ✅ llegó 2026-08-09 |
| StackChan Core (IoT Controller) | Unidad "cabeza" suelta del StackChan en blíster — board **CoreS3 CAP.TOUCH rev1** (verificado abriendo la tapa): ESP32-S3, LCD 2" táctil, mic/bocina (AW88298), AXP2101, M-BUS + PortA Grove · marketing: AI Conversation & Agent, Smartphone Connection, open-source HW/SW · = controlador de repuesto / segunda cabeza para cuerpos StackChan o unidad de desarrollo independiente | ✅ llegó 2026-08-09 |
| M5Stack Voice Pyramid (Smart Audio Base) | Base de audio p/ satélite de voz — DAC ES8311 · ADC ES7210 (mics) · amp AW87559 · **bocina 5W** · **circuito AEC (cancelación de eco = far-field voice utilizable)** · LEDs WS2812 · 2 touch laterales · 5V USB-C (power abajo / data arriba) o Grove · 83.6×83.6×60.2 mm · socket I2S+I2C para Atom · requiere controlador Atom-series (no incluido) → **✅ DESBLOQUEADA: el AtomS3R del kit AI Chatbot (fila siguiente) le queda** — opción: compartir ese Atom entre ambas bases, o comprar un 2º Atom (~$300) para tener Pyramid y Chatbot activos a la vez · ⭐ con AtomS3R probablemente ES el satélite de voz definitivo del proyecto 06 (AEC > mics desnudos del CoreS3) | ✅ llegó 2026-08-09 |
| M5Stack AI Chatbot kit (AtomS3R + Voice Base) | Blíster con DOS piezas: **AtomS3R** (ESP32-S3-PICO · LCD 0.85" 128×128 · IMU BMI270+BMM150 · emisor **IR en G47** · botón G41 · Grove Port.A · header GPIO) + **Voice Base para Atom** (DAC ES8311 + mic · amp NS4150B + bocina · passthrough I2S + Grove) · = satélite de voz compacto listo-para-armar · el AtomS3R también encaja en la **Voice Pyramid** (fila anterior) — un cerebro, dos cuerpos | ✅ llegó 2026-08-09 |
| M5Stack Cardputer ADV | Computadora de bolsillo tamaño tarjeta — módulo **StampS3A desmontable** (ESP32-S3FN8, antena 3D, GPIO+ADC expuestos) · **teclado QWERTY 4×14** (TCA8418) · LCD IPS 1.14" ST7789V2 · codec ES8311 + bocina NS4150 1W + **mic** + jack audífonos (out-only, HP_DET) · IMU BMI270 · **bat 1750 mAh interna** (TP4057, DC/DC SY8089+SY7088) · microSD (G40/39/14/12) · **emisor IR G44** · Grove Port.A con switch 5VIN/5VOUT · botones G0/Rst + OFF/ON físico · usos lab: terminal portátil / tester de sensores Grove / control IR / juguete de diagnóstico WiFi (firmware comunidad M5Launcher) — y el StampS3A se puede extraer para proyectos propios | ✅ llegó 2026-08-09 |
| M5StickS3 | Stick compacto (48×24×15 mm, 20 g) — ESP32-S3-PICO-1-N8R8 (8MB flash + 8MB PSRAM) · LCD 1.14" ST7789P3 135×240 · codec ES8311 + **mic MEMS** + bocina 1W 8Ω (amp AW8737) · IMU BMI270 · **IR TX G46 + RX G42** · 2 botones (KEY1 G11 / KEY2 G12) · Grove Port.A · bat 250 mAh · USB-C · usos: control remoto de bolsillo p/ HA, nodo sensor portátil, beacon BLE, mini-display de estado | ✅ llegó 2026-08-09 |
| ESP32-S3 Super Mini ×2 | Placa azul mini formato Zero — chip **ESP32-S3 directo en PCB** (sin módulo blindado, típicamente FH4R2: 4MB flash + 2MB PSRAM embebidos) · **bordes castellados** (montable superficial sobre otra PCB) · USB-C · botones BOOT/RESET · LED RGB · 2 tiras de headers 10-pin sin soldar · etiqueta PHKW51038 · ⭐ el nodo ESPHome más pequeño del inventario: BLE proxy de bolsillo, sensor embebido en cajas chicas, wearables — complementa a los devkits 44-pin (esos = nodos con antena ext., estos = donde no cabe nada más) | ✅ **llegó — verificado 2026-08-11 (foto) ×2** |
| ESP32-S3 DevKit 44-pin N16R8 + antena ext. ×3 | DevKit 44 pines ESP32-S3 N16R8 (16MB/8MB PSRAM) · UART **CH343** · **dual USB-C** · bajo consumo · **3 antenas extendidas incluidas** (u.FL → mejor alcance WiFi que antena PCB, útil p/ nodos lejos del AP o dentro de gabinete metálico) · Arduino IDE / ESPHome · verificado por foto: marca **UICPAL**, BOOT/RST, LED RGB WS2812, u.FL en zona de antena | ✅ **verificado en sitio 2026-08-11 (foto) ×3** — entregado 2026-07-31 📍 Toluca |
| Seeed XIAO ESP32-S3 ×3 | **XIAO ESP32S3 3PCS Pack** (SKU X004ETOYRT) — modelo XIAO-ESP32-S3, FCC 2AJ7T-XIAOESP32S3 · formato pulgar con **bordes castellados** · USB-C · **conector u.FL p/ antena externa** (⚠ verificar si las 3 antenas 2.4 GHz venían en el pack — no visibles en foto) · headers sin soldar ×3 pares · ⭐ por fin un XIAO que corre **ESPHome** (el SAMD21 no puede): cerebro directo p/ la **XIAO expansion board / Grove Shield ya en inventario** (OLED+RTC+buzzer+8× Grove plug-and-play) · completa la escala de nodos: Super Mini = mínimo absoluto · XIAO = mínimo con ecosistema Grove · devkit 44-pin = antena ext. | ✅ llegó CDMX — verificado por foto 2026-08-15 ×3 · 🧳 viaja a Toluca próximo viaje |

## 🟩 SENSORES
| Item | Detalle | Estado |
|---|---|---|
| Grove Beginner Kit (Seeed) | DHT (temp/hum), luz, sonido, presión BMP280, acelerómetro, OLED, buzzer, pot, LED, botón — placa todo-en-uno | ✅ |
| AHT20 + BMP280 combo ×3 | Módulo breakout doble sensor **I²C** (SKU MAHT20-BMP280): **AHT20** temp/humedad (±0.3°C / ±2%RH, sucesor del DHT) + **BMP280** presión barométrica/altitud · 2 sensores en una placa, un solo bus I²C (direcciones distintas: 0x38 + 0x76/77) · 3.3V/5V · ⭐ = llegó (parcial) el pedido de importación "BME280/SHT31" que estaba en 🟥 CONSUMIBLES — variante combo AHT20+BMP280 cubre lo mismo (temp+hum+presión) · candidatos p/ proyecto 07 sensores de plantas con los Freenove ESP32 / ESPHome (alcanza p/ 3 nodos: uno por sitio + 1 spare o exterior) | ✅ llegó 2026-08-09 ×3 |
| M5Stack Unit PaHub v2.1 | **Expansor/mux I²C Grove** (U040-B-V21, chip PCA9548-class) — 1 puerto Grove → **6 canales I²C independientes** · resuelve conflictos de dirección: permite colgar varios sensores con la MISMA dirección (p.ej. 2-3 combos AHT20+BMP280 en un solo MCU) · encadenable · cable Grove incl. · SN U040BV2126040101326 | ✅ llegó 2026-08-09 |
| M5Stack Unit MIC | **Micrófono analógico Grove** (U096) — mic con amplificación y **trimmer de ganancia ajustable** (pot azul), salida analógica · para detección de nivel de ruido / claps / vigilancia acústica en cualquier host con Grove (XIAO, CoreS3, devkits) · NO es I2S de voz — es el sensor de "qué tan fuerte", complementa (no sustituye) los mics de los satélites | ✅ llegó 2026-08-09 |
| M5Stack Unit NFC | **Lector NFC/RFID 13.56 MHz Grove I²C** (U216, chip WS1850S ≈ MFRC522) — lee/escribe MIFARE, tags, tarjetas · plug-and-play por Grove (sin cablear SPI como el RC522 suelto) · ⭐ upgrade directo de la idea "armar/desarmar alerta con llavero": Grove al CoreS3/XIAO y listo · el RC522 del starter kit queda de backup/didáctico | ✅ llegó 2026-08-09 |
| GY-521 (MPU-6050) ×1 | **IMU 6 ejes I²C** — acelerómetro 3 ejes + giroscopio 3 ejes + sensor de temp interno · placa azul 8 pines (VCC/GND/SCL/SDA/XDA/XCL/AD0/INT; AD0 = selector de dirección 0x68/0x69, INT = interrupt p/ wake-on-motion) · regulador 3.3V a bordo (tolera 5V) · headers sin soldar · usos: detección de vibración/movimiento en máquinas, detector de golpe/apertura en puertas, niveles/orientación | ✅ **llegó — verificado 2026-08-11 (foto) ×1** |
| Elechouse Voice Recognition V3.1 | Módulo de **reconocimiento de voz OFFLINE** (vendido como "FACULX V3") — hasta **80 comandos entrenables**, 7 activos simultáneos · speaker-dependent (se entrena con TU voz) · control por **UART o GPIO** · incluye mic electret de cuello flexible con jack 3.5mm · 100% local, cero nube · para proyecto 06: comandos de voz directos a un Arduino/ESP32 sin pipeline HA (p.ej. botonera invisible: "lámpara", "alarma") — complementa a los satélites (esos = conversación vía HA; este = comandos fijos instantáneos) | ✅ llegó 2026-08-07 |
| INMP441 mic MEMS I2S ×5 | Micrófono **omnidireccional MEMS con salida digital I2S** — alta precisión, bajo consumo, el mic clásico de los **satélites de voz DIY con ESPHome** (micro-wake-word) · ⚠ nota histórica: estos eran "pendientes de compra" en proyecto 06 y resulta que YA estaban comprados y entregados — la fila del CoreS3 especulaba volverlos innecesarios, pero ×5 abren la vía paralela: **satélites DIY baratos** con los 5 devkits S3 + breakouts de tornillo (mismo paquete Toluca) + bocina/amp · M5 = satélites premium, INMP441+S3 = flota de cobertura por cuarto · placa redonda, pines SD/VDD/GND + L/R/WS/SCK, headers 6-pin sin soldar (bolsa EC Buying SKU 91699) | ✅ entregado 2026-07-31 📍 Toluca · **verificados en sitio 2026-08-11 (foto) ×5** |

## 🏠 SMART HOME — dispositivos por desplegar
| Item | Detalle | Estado |
|---|---|---|
| ThirdReality Smart Plug Gen2 | Enchufe inteligente **ZIGBEE** (badge oficial "Works via Zigbee with HA") — medición de energía en tiempo real · **router/repetidor de mesh Zigbee** (mains-powered = extiende la red, doctrina ✓) · safety setting (estado On/Off al restaurar corriente) · ⚠ **placement: requiere coordinador Zigbee = Toluca (SLZB-MR3U, Z2M ch 15)** — CDMX no tiene stack Zigbee aún; viaja a Toluca o espera · nota: NO confundir con el plug ThirdReality **Matter/WiFi** "Cargador erick" ya desplegado (ese no extiende nada; este SÍ) | ✅ llegó 2026-08-09 |
| ThirdReality Presence Sensor R3 | Sensor de presencia **radar mmWave 60 GHz** (detecta presencia estática, no solo movimiento como PIR) — USB-C · luz RGB de estado · sensor de iluminación (lux) · **repetidor Zigbee** (USB-powered = router de mesh) · ⚠ Zigbee → coordinador = Toluca (SLZB-MR3U) · caso de uso: presencia por habitación p/ automatizar luces sin timeouts de PIR · QSG "Applicable to version 1.00.33 and above" | ✅ llegó CDMX — verificado por foto 2026-08-15 · 🧳 viaja a Toluca próximo viaje (coordinador Zigbee) |
| Shelly Plug US Gen4 ×2 | Enchufe blanco con **medición de consumo 2500W** — cuádruple radio: **WiFi + BT + Zigbee + Matter**, UL · ⭐ flexible: en **CDMX puede operar YA por WiFi/Matter** (no necesita coordinador Zigbee) o en Toluca unirse a Z2M · en modo WiFi además actúa de **BT gateway p/ dispositivos Shelly BLU** (ver filas siguientes) · integración HA nativa | 🚚 llega 2026-08-16 ×2 |
| Shelly BLU RC Button 4 ZB | Control remoto 4 botones **Bluetooth Y Zigbee** (elegible por modo) — multi-clic (hasta 3 acciones/botón = 12+ automatizaciones) · soporte magnético, sin cables (pila) · en modo BLU necesita gateway BT (un Shelly Gen3/Gen4 cercano o BT-proxy ESPHome con los ESP32 del inventario); en modo ZB → Toluca | 🚚 llega jue 2026-08-13 |
| Shelly BLU H&T ZB | Sensor temp/humedad **Bluetooth Y Zigbee**, IP54 (aguanta baño/exterior techado) — pila, pantalla e-paper no (modelo básico) · mismo dilema de gateway que el botón BLU: WiFi-plug Gen4 como gateway BT en CDMX, o modo Zigbee en Toluca · complementa los AHT20+BMP280 (esos = nodos DIY cableados, este = inalámbrico colocable donde sea) | 🚚 llega jue 2026-08-13 |
| Shelly 2PM Gen4 In-Wall ×1 | Relé empotrable **2 canales con medición de consumo por canal** — 110-240V~ / 24V⎓ · cuádruple radio **WiFi + BT + Zigbee + Matter** (badges en caja) · entradas S1/S2 p/ apagadores existentes → **retrofit de apagador doble SIN cambiar la tecla** (misma idea que los 1PM Mini Gen4 de Toluca, pero doble canal y multi-protocolo) · también modo cover p/ persiana/motor · instalar con los WAGO 221 · en CDMX puede operar YA por WiFi/Matter; en Toluca unirse a Z2M por Zigbee | ✅ llegó CDMX — verificado por foto 2026-08-15 ×1 · 🧳 viaja a Toluca próximo viaje |

## 🟨 PROTO — Prototipado
| Item | Detalle | Estado |
|---|---|---|
| Protoboard Radox 690-815 | 830 pts | ✅ sellado |
| Kit MB-102 | protoboard 400+830 pts, módulo de poder, 65 jumpers | ✅ |
| Kit de resistencias | 600 pzas, 30 valores 10Ω–1MΩ, ¼W 1% metal film | ✅ |
| Placas perforadas | 5×5 cm, 4×6 cm, 9×15 cm | ✅ |
| Perfboard PCB 5×7 cm ×10 | Placas de prototipo **doble cara** para soldar — stock p/ montajes permanentes (nodos sensores, sockets con headers hembra) | ✅ entregado 2026-07-29 ×10 |
| WAGO 221-613 ×30 | conector de palanca 3×6mm² — cableado AC/pared | ✅ |
| Freenove terminal block shield ESP32/S3 | Escudo de **terminales de tornillo** p/ ESP32/ESP32-S3 WROVER/WROOM — cabezal de clavija, salidas de alimentación 5V/3.3V, **LED de estado por GPIO**, desarmador incl. · ⭐ pareja natural de los **Freenove ESP32 ×2** (proyecto 07 plantas): sensores cableados SIN soldar y con debug visual por LED | ✅ entregado 2026-07-31 **📍 Toluca** (pend. verificar en sitio) |
| Breakout GPIO 44-pin p/ ESP32-S3 ×5 | Placa de ruptura "1 en 2" con terminales de tornillo, compatible 44 pines (devkits S3 N8R2/N16R8) — el devkit se monta encima y cada GPIO sale a tornillo · ×5 = uno por cada nodo S3 desplegable (los 3 con antena ext. + los 2 con OLED) · despliegue permanente sin protoboard | ✅ entregado 2026-07-31 **📍 Toluca** (pend. verificar en sitio) |
| OLED 0.96" I²C suelta ×1 | Pantalla OLED 0.96" I²C 4 pines (GND/VCC/SCL/SDA, driver tipo SSD1306), con película de fábrica — **estaba suelta** en el lote 2026-08-11; origen probable: bundle de un devkit N16R8+OLED o repuesto · display genérico p/ cualquier nodo I²C | ✅ verificada 2026-08-11 (foto) — ubicar en caja 🟨 PROTO |
| Bocina mini 4Ω 3W 35×25mm | Altavoz negro con **terminal JST 1.25mm** (SKU MB4O3W35X25) — salida de audio p/ proyectos: feedback sonoro del Elechouse VR3.1 (vía amp), alarmas de nodos ESP32 (con amp I2S tipo MAX98357), o repuesto genérico de bocina pequeña | ✅ llegó 2026-08-07 |
| MAX98357 I2S amp clase D ×5 | Breakout **DAC + amplificador clase D 3W I2S** (bolsa DIGYes, SKU X004UUR50F) — entrada I2S digital (LRC/BCLK/DIN), salida a bocina por **terminal de tornillo verde**, ganancia seleccionable por pin GAIN (9/12/15 dB) · headers sin soldar · ⭐ **CIERRA el BOM de la flota de satélites de voz DIY (proyecto 06)**: 5× devkit S3 44-pin (Toluca) + 5× INMP441 mic in (Toluca) + 5× MAX98357 audio out + bocina mini 4Ω = 5 satélites micro-wake-word completos vía ESPHome — la "vía paralela barata" de la fila INMP441 ya tiene todas sus piezas | ✅ llegó CDMX — verificado por foto 2026-08-15 ×5 · 🧳 viaja a Toluca próximo viaje |

## ⬜ CABLES
| Item | Detalle | Estado |
|---|---|---|
| USB A→B azules ×2 | para UNO clon / MEGA | ✅ |
| USB data (C / micro) | verificados con ESP32 | ✅ |
| AviWarp HDMI 2.1 EDID emulator ×2 | Dummy plug / emulador EDID **HDMI 2.1** — 4K/1440p/1080p **@120Hz** · engaña a la GPU: "hay monitor conectado" → render + aceleración activas sin pantalla física · ⭐ para **nodos headless con GPU**: nodo Linux/AI streameando via Sunshine/Moonlight, msi como host remoto, o VMs con GPU-passthrough · 2 unidades = uno por GPU headless | ✅ llegó 2026-08-07 ×2 |
| Cable step-up USB 5V→9V ×4 | Convertidor elevador en cable: **USB-A 5V → barrel 2.1×5.5mm 9V** (SKU CSPUPUSB9V) — alimenta placas con jack DC (Arduino UNO/MEGA 7-12V, **ESP32-S3 D1 UNO** con barrel jack) desde cualquier cargador USB o power bank · ⭐ despliegue de nodos sin buscar eliminador de 9V: un cargador de teléfono basta · ×4 = hasta 4 nodos fijos alimentados así | ✅ llegó 2026-08-09 ×4 |

## 🟥 CONSUMIBLES
| Item | Detalle | Estado |
|---|---|---|
| Estaño Radox 110-717 | 60/40, 1 mm, 200 g | ✅ COMPLETO — no comprar |
| Flux, malla, bomba, termofit, tercera mano, limpia-puntas | — | ✅ COMPRADO CDMX — ver "Compras CDMX 2026-07" abajo |
| Kit 500 capacitores cerámica 0.1µF–10µF | 10 valores, caja organizadora transparente — desacople y filtrado p/ perfboard | ✅ entregado 2026-07-29 |
| Pulsera antiestática DOMOKO ESD | correa de muñeca, cable 1.8m, rotación 360°, clip caimán — usar al manejar CPUs/RAM/NVMe y MCUs | ✅ entregado 2026-07-29 |
| Headers hembra 40-pin 2.54mm ×10 tiras | Tiras Dupont hembra de 40 posiciones (SKU 870-360) — para hacer sockets: montar devkits/módulos desmontables sobre perfboard sin soldarles pines directo · recortables al largo necesario · (del pedido "headers" — tiras MACHO: no vistas aún, verificar si venían) | ✅ llegó 2026-08-09 ×10 |
| Sensores capacitivos de suelo + BME280/SHT31 | agotados en tienda · **update 2026-08-09:** la parte temp/hum/presión YA llegó como combo AHT20+BMP280 ×3 (ver 🟩 SENSORES) — pendiente solo: sensores capacitivos de suelo | 🚚 parcial — suelo capacitivo aún en camino |

## 🔄 Salidas (Seiri)
- ThirdReality Smart Hub Gen2 Plus → ✅ DEVUELTO 2026-07-27 (redundante con SLZB-MR3U)
- micro:bit GO → decidir: ¿regalo? No almacenar indefinidamente.

## 🧩 SUELTOS identificados (fotos 2 — pila del starter kit)
> Origen probable: kit de inicio Arduino (del UNO/MEGA clon). Destino: repartir en las 5 cajas.

| Item | Qué es / para qué | Caja destino |
|---|---|---|
| 2× Terminal Adapter FOR ESP32 | ⭐ adaptador de terminales de tornillo para los Freenove ESP32 — sensor de plantas SIN soldar | 🟦 MCU |
| HC-SR04 | sensor ultrasónico de distancia | 🟩 SENSORES |
| HC-SR501 (domo blanco) | sensor de movimiento PIR — prototipo de automatización antes de comprar Zigbee | 🟩 SENSORES |
| MQ-x "Flying-Fish" | sensor de gas/humo analógico — gemelo DIY del detector Zigbee | 🟩 SENSORES |
| Módulo de sonido (rojo) | micrófono/detector de ruido | 🟩 SENSORES |
| Sensor de nivel de agua (rojo) | detección de agua/nivel | 🟩 SENSORES |
| DHT11 (azul chico) | temp/humedad básico (BME280 lo supera) | 🟩 SENSORES |
| RFID-RC522 + tarjeta/llavero | lector NFC 13.56MHz — idea: armar/desarmar alerta de puerta con llavero | 🟩 SENSORES |
| LCD 16×2, TFT color ~1.8", OLED 0.96", matriz 8×8 + driver | pantallas/salidas | 🟨 PROTO |
| Relay 1 canal (azul) | conmutar cargas desde MCU | 🟨 PROTO |
| Joystick, 2× potenciómetros, LEDs surtidos | entradas/salidas de práctica | 🟨 PROTO |
| 2× MB102 módulo de poder, portapilas 2×AA | alimentación de protoboard | 🟨 PROTO |
| RTC con pila de botón (DS1307/DS3231) | reloj de tiempo real I²C | 🟨 PROTO |
| Jumpers, cable ribbon arcoíris, cables Grove | conexión (Grove = XIAO/Wio/Grove Kit) | ⬜ CABLES |
| Cinta aislante Tuk 330 PRO | vinilo eléctrico | 🟥 CONSUMIBLES |
| Set de puntas de precisión (naranja) | herramienta | banco |

## ⭐ Hardware destacado adicional
| Item | Detalle | Estado |
|---|---|---|
| Seeed Wio Terminal D51R | SAMD51 + RTL8720DN WiFi/BT, pantalla 2.4", 3 botones, puertos Grove, header 40-pin RPi | ✅ — candidato a display HA de escritorio/pared (junto con M5Stack) |
| Fuente 54V p/ USW-Lite-8-PoE (Kircuit) | Adaptador AC/DC **54VDC 1.1A 59.4W** (NY-PW0L2-05401100), compatible Ubiquiti **USW-Lite-8-PoE** — fuente de repuesto/reemplazo p/ el switch PoE UniFi · → inventario home-lab (red), no cajas de electrónica | ✅ entregado 2026-08-05 |

## 🛒 Compras CDMX 2026-07 (audit por fotos, 2026-07-27)

> Sensores (suelo capacitivo · BME280 · SHT31): AGOTADOS en tienda → pedidos
> por importación, ETA ~med-agosto. Todo lo demás del rubro soldadura llegó.

### Banco de soldadura (→ 🟥 CONSUMIBLES / banco)
| Item | Detalle | Caja/lugar |
|---|---|---|
| Estación de soldadura Weller | digital, control de temperatura, cautín con soporte de resorte | banco |
| Estación de desoldar aire caliente **LEFAVOR 858D** | pistola de aire sin escobillas, pantalla digital, **100-480°C**, flujo ajustable **perilla 1-8**, botones ▲▼ temp + CAL, conector GX 5 pines, portátil — desoldar SMD/headers, termofit, rework · completa el banco: Weller = soldar, esta = desoldar/retrabajar | ✅ **llegó — verificada en banco 2026-08-11 (foto)** |
| Puntas de repuesto Weller | juego ×4-5 en bolsa | banco (bandeja del soporte) |
| Mat de silicón magnético Steren | azul, bandejas de partes + regla impresa, "magnetic/Parts" | banco |
| Pasta para soldar Truper | 60 g, lead-free (flux) | 🟥 CONSUMIBLES |
| Malla de cobre limpia-puntas | en lata/vaso metálico | banco |
| Bomba desoldadora | metálica | banco |
| Tercera mano | base con clips + lupa | banco |
| Soporte/prensa PCB ajustable | azul/morado, mordazas corredizas | banco |
| Lámpara lupa LED de aro | negra, brazo | banco |
| Kit terminales crimp + termofit | anillo/pala/faston rojo-azul, empalmes, zapatas cobre, latón + termorretráctil surtido (colores/calibres) en organizador | 🟥 CONSUMIBLES |
| Jumpers dupont ribbon 40-pin | 30 cm, macho-hembra 2.54 mm (X0054GGWSZ) | ⬜ CABLES |
| Caja(s) organizadora(s) transparentes | para el 5S de las 5 cajas | almacenaje |
| Carrete de estaño Steren | 60/40, 1 mm, 100 g (segundo carrete; el Radox 200 g sigue COMPLETO) | 🟥 CONSUMIBLES |
| Pilas AA Duracell ×6 + guantes algodón | menores | almacenaje |

# MEDA - Ricerca Tecnica

Software di acquisizione dati da dispositivi Modbus su RS-485, impacchettamento in formato MiniSEED e pubblicazione tramite RingServer, su router Teltonika RUT956 con RutOS.

---

## 1. Piattaforma Target: Teltonika RUT956

### Hardware selezionato

| Spec | Valore |
|------|--------|
| **Modello** | Teltonika RUT956 |
| **CPU** | MediaTek MT7628, MIPS 24KEc, **580 MHz** |
| **RAM** | **128 MB DDR2** |
| **Flash** | **16 MB SPI NOR** (~3-6 MB liberi dopo RutOS) |
| **LTE** | Cat 4 (150/50 Mbps), Dual-SIM |
| **Ethernet** | 4x 10/100 (1 WAN + 3 LAN) |
| **WiFi** | 802.11 b/g/n, 2.4 GHz |
| **GPS** | Integrato |
| **Seriale** | **RS-232 + RS-485** (connettore DB9) |
| **I/O** | 4x DI, 2x DO (relay), 1x AI (0-30V) |
| **Alimentazione** | 9-30 VDC (connettore 4-pin) |
| **Temperatura** | -40 C a +75 C |
| **Dimensioni** | 100 x 110 x 50 mm, 287 g |
| **OS** | RutOS (basato su OpenWrt, Linux, musl libc) |

### Porta RS-485 del RUT956
- **Connettore**: DB9 condiviso con RS-232
- **Modalita'**: Half-duplex (2 fili) o Full-duplex (4 fili)
- **Cablaggio half-duplex**: collegare D_P a R_P e D_N a R_N
- **Device path Linux**: **`/dev/ttyATH1`** (RS-485) / `/dev/ttyS0` (RS-232)
- **Baud rate**: 300-115200
- **Funzioni**: Console, Serial over IP, Modbus gateway, NTRIP
- **Terminazione**: resistenza 120 ohm ai capi del bus

### Perche' C e non Python

| | Python + ObsPy | C puro |
|---|---|---|
| **Dimensione** | ~50+ MB (Python + NumPy + ObsPy) | **~400 KB** totali |
| **Flash necessaria** | Non entra nei 16 MB | Entra comodamente |
| **RAM** | ~30-50 MB runtime | **< 1 MB** runtime |
| **FPU** | NumPy su MIPS senza FPU = lentissimo | STEIM2 e' tutto intero |
| **Startup** | Secondi (interprete Python) | **Millisecondi** |
| **Verdetto** | **NON fattibile su RUT956** | **Scelta obbligata** |

Python/ObsPy resta utile **lato server** per analisi, visualizzazione e debug dei dati.

### Modbus Gateway integrato in RutOS
RutOS include un **gateway Modbus TCP/RTU** configurabile da WebUI (Services > Modbus):
- Bridge Modbus RTU (RS-485) <-> Modbus TCP
- Raccolta dati seriali con pubblicazione MQTT
- **Nota**: per il nostro progetto NON useremo il gateway integrato, ma accesso diretto alla porta seriale `/dev/ttyATH1`.

### SDK RUT956 (verificato)

L'SDK e' stato scaricato e verificato: `rutos-ramips-rut9m-sdk/`

**Versione**: `RUT9M_R_GPL_00.07.20.3` (basato su OpenWrt)

**Configurazione target confermata dal `.config`:**
```
CONFIG_ARCH="mipsel"
CONFIG_CPU_TYPE="24kc"
CONFIG_TARGET_ramips=y
CONFIG_TARGET_ramips_mt76x8=y
CONFIG_TARGET_ARCH_PACKAGES="mipsel_24kc"
CONFIG_TARGET_SUFFIX="musl"
CONFIG_TARGET_OPTIMIZATION="-Os -pipe -mno-branch-likely -mips32r2 -mtune=24kc"
```

**Toolchain**: `mipsel-openwrt-linux-musl-gcc` (MIPS little-endian, musl libc)

**libmodbus GIA' INCLUSA nell'SDK** (`package/libs/libmodbus/`):
- Versione: v3.1.6 da GitHub (stephane/libmodbus)
- Build automatico con autoreconf
- Disponibile come shared library e per linking statico

**Altre librerie utili gia' presenti nell'SDK:**
- `cjson` - parsing JSON (per configurazione)
- `libgpiod` - controllo GPIO (per I/O digitali)
- `openssl` / `zlib` - se servono
- `sqlite3` - per buffering locale opzionale

**Build system**: Docker consigliato (`./scripts/dockerbuild`) oppure build nativo su Ubuntu 22.04.

### Come creare un pacchetto per MEDA

Basato sull'`ipk-example/` fornito da Teltonika:

```bash
# 1. Copiare il package nella struttura SDK
cp -r meda-package/ rutos-ramips-rut9m-sdk/package/base/meda/

# 2. Configurare (selezionare [M] per il pacchetto meda)
cd rutos-ramips-rut9m-sdk
./scripts/dockerbuild make menuconfig

# 3. Compilare solo il pacchetto
./scripts/dockerbuild make package/meda/{clean,compile}

# 4. Il .ipk sara' in bin/packages/ramips/base/meda_*.ipk

# 5. Deploy sul router
scp bin/packages/ramips/base/meda_*.ipk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 "opkg install /tmp/meda_*.ipk"
```

**Makefile del pacchetto** (struttura per codice C con sorgenti):
```makefile
define Build/Compile
    $(MAKE) -C $(PKG_BUILD_DIR)/ \
        CC="$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS)" \
        LDFLAGS="$(TARGET_LDFLAGS)" \
        all
endef

define Package/meda/install
    $(INSTALL_DIR) $(1)/usr/bin
    $(INSTALL_BIN) $(PKG_BUILD_DIR)/meda $(1)/usr/bin/
    $(INSTALL_DIR) $(1)/etc/init.d
    $(INSTALL_BIN) ./files/meda.init $(1)/etc/init.d/meda
    $(INSTALL_DIR) $(1)/etc/config
    $(INSTALL_CONF) ./files/meda.conf $(1)/etc/config/meda
endef
```

### Init script (procd) per avvio automatico
```bash
#!/bin/sh /etc/rc.common
START=99
STOP=10
USE_PROCD=1

start_service() {
    procd_open_instance
    procd_set_param command /usr/bin/meda
    procd_set_param respawn 3600 5 0   # restart dopo crash
    procd_set_param stdout 1           # log stdout su syslog
    procd_set_param stderr 1           # log stderr su syslog
    procd_close_instance
}
```

### Limitazioni RUT956
- **musl libc** (non glibc) - alcune funzioni possono differire
- **16 MB Flash** - solo ~3-6 MB liberi, usare strip dei binari
- **No FPU hardware** (MIPS 24KEc) - evitare floating-point intensivi. STEIM2 e' OK (intero).
- **128 MB RAM** - ~50-80 MB liberi dopo il sistema, abbondanti per il nostro software
- **Sysupgrade**: aggiungere file a `/etc/sysupgrade.conf` per sopravvivere agli aggiornamenti

---

## 2. Dispositivo di Acquisizione: Waveshare Modbus RTU Analog Input 8CH

### Specifiche hardware

| Spec | Valore |
|------|--------|
| **Canali** | 8 ingressi analogici |
| **Risoluzione** | 12 bit (ADC) |
| **Range tensione** | 0-5V / 1-5V (versione standard) |
| **Range corrente** | 0-20mA / 4-20mA |
| **Versione B** | 0-10V / 2-10V + 0-20mA / 4-20mA |
| **Precisione** | 12 bit, errore <= 3% |
| **Update rate** | 25 Hz (25 campioni/secondo per canale) |
| **Interfaccia** | RS-485 (Modbus RTU) |
| **Baud rate** | 4800-256000 (default: 9600 8N1) |
| **Indirizzo** | Configurabile 1-255 (default: 1) |
| **Alimentazione** | DC 7-36V |
| **Protezioni** | Isolamento ottico, isolamento magnetico RS-485, TVS, fusibile resettabile |
| **Ingressi** | Single-ended/differenziale, tensione e corrente simultanei |

### Mappa registri Modbus

#### Input Registers (FC 0x04) - Dati analogici
| Indirizzo | Descrizione | Formato |
|-----------|-------------|---------|
| 0x0000 | Canale 1 - valore analogico | uint16 |
| 0x0001 | Canale 2 - valore analogico | uint16 |
| 0x0002 | Canale 3 - valore analogico | uint16 |
| 0x0003 | Canale 4 - valore analogico | uint16 |
| 0x0004 | Canale 5 - valore analogico | uint16 |
| 0x0005 | Canale 6 - valore analogico | uint16 |
| 0x0006 | Canale 7 - valore analogico | uint16 |
| 0x0007 | Canale 8 - valore analogico | uint16 |

**Lettura tutti i canali**: `01 04 00 00 00 08 F1 CC`

I valori restituiti dipendono dal data type configurato:
- **0-5V**: valore in mV (0-5000) o (0-10000 per versione B 0-10V)
- **1-5V**: valore in mV (1000-5000)
- **0-20mA**: valore in uA (0-20000)
- **4-20mA**: valore in uA (4000-20000)
- **Raw**: codice ADC grezzo (0-4095)

#### Holding Registers (FC 0x03, 0x06, 0x10) - Configurazione
| Indirizzo | Descrizione | Valori |
|-----------|-------------|--------|
| 0x1000-0x1007 | Data type canali 1-8 | 0=0-5V, 1=1-5V, 2=0-20mA, 3=4-20mA, 4=Raw |
| 0x2000 | Parametri UART | High byte=parita' (0=N,1=E,2=O), Low byte=baud |
| 0x4000 | Indirizzo dispositivo | 1-255 |
| 0x8000 | Versione software | Read-only |

**Esempi comandi:**
```
Lettura 8 canali:           01 04 00 00 00 08 F1 CC
Lettura data types:         01 03 10 00 00 08 40 CC
Set canale 1 a 4-20mA:     01 06 10 00 00 03 CD 0B
Set baud 115200:            00 06 20 00 00 05 43 D8
Set indirizzo slave a 1:    00 06 40 00 00 01 5C 1B
```

### Considerazioni per il progetto
- **25 Hz update rate** = max 25 campioni/sec per canale
- Con 8 canali a 25 Hz: **200 campioni/sec** totali
- Un singolo `FC04` legge tutti 8 canali in una transazione (~40ms a 9600 baud)
- **Banda code SEED**: `S` (Short period, 10-80 Hz) dato che 25 Hz e' nel range 10-80
- **Channel code suggerito**: `SF1`-`SF8` (Short period, generico, canale 1-8)
- Per acquisire a 25 Hz: polling ogni 40ms con margine sufficiente a 9600 baud

---

## 3. Modbus RTU su RS-485

### Protocollo Modbus RTU - Fondamentali

**Frame format:**
```
[Slave Address: 1 byte][Function Code: 1 byte][Data: N bytes][CRC-16: 2 bytes]
```

- **Slave Address**: 1-247 (0 = broadcast, 248-255 riservati)
- **CRC-16**: polinomio 0xA001, byte basso prima
- **Delimitatore frame**: silenzio di 3.5 caratteri (es. ~4ms a 9600 baud)
- **Half-duplex**: un solo dispositivo parla alla volta sul bus

### Function Codes per acquisizione dati
| Code | Nome | Descrizione |
|------|------|-------------|
| 0x01 | Read Coils | Lettura bit discreti (output) |
| 0x02 | Read Discrete Inputs | Lettura bit discreti (input) |
| **0x03** | **Read Holding Registers** | **Lettura registri 16-bit (read/write)** |
| **0x04** | **Read Input Registers** | **Lettura registri 16-bit (read-only)** |

I function code **0x03 e 0x04** sono i piu' usati per acquisizione dati da sensori. Ogni registro e' 16 bit. Per valori a 32 bit (float/int32), si leggono 2 registri consecutivi.

### RS-485 - Livello fisico
- **Coppie differenziali**: 2 fili (A/B) + GND (raccomandato)
- **Half-duplex**: direzione controllata dal driver (automatic su Teltonika)
- **Terminazione**: resistenza 120 ohm ai due estremi del bus
- **Lunghezza max**: 1200m a 9600 baud, meno a baud rate piu' alti
- **Max dispositivi**: 32 su bus standard, 256 con repeater
- **Baud rate tipici**: 9600, 19200, 38400, 115200

### Configurazione seriale tipica
- **9600 8N1**: 9600 baud, 8 data bit, No parity, 1 stop bit (default piu' comune)
- **9600 8E1**: con Even parity (richiesto da alcuni dispositivi)
- **19200 8N1**: per polling piu' veloce

### libmodbus - Libreria C per Modbus

**Repository**: https://github.com/stephane/libmodbus
**Licenza**: LGPL v2.1+

**API principale per RTU:**

```c
#include <modbus.h>

/* Creare contesto RTU */
modbus_t *ctx = modbus_new_rtu(
    "/dev/ttyATH1",   /* Porta seriale */
    9600,            /* Baud rate */
    'N',             /* Parity: 'N', 'E', 'O' */
    8,               /* Data bits */
    1                /* Stop bits */
);

/* Configurazione */
modbus_set_slave(ctx, 1);                          /* Indirizzo slave */
modbus_set_response_timeout(ctx, 1, 0);            /* Timeout: 1s, 0us */
modbus_set_byte_timeout(ctx, 0, 500000);           /* Timeout byte: 500ms */
modbus_rtu_set_serial_mode(ctx, MODBUS_RTU_RS485); /* Modo RS-485 */
modbus_set_debug(ctx, TRUE);                       /* Debug on/off */

/* Connessione */
if (modbus_connect(ctx) == -1) {
    fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
    modbus_free(ctx);
    return -1;
}

/* Lettura registri */
uint16_t regs[10];

/* Read Holding Registers (FC 0x03): da registro 0, 10 registri */
int rc = modbus_read_registers(ctx, 0, 10, regs);
if (rc == -1) {
    fprintf(stderr, "Read failed: %s\n", modbus_strerror(errno));
}

/* Read Input Registers (FC 0x04): da registro 100, 5 registri */
rc = modbus_read_input_registers(ctx, 100, 5, regs);

/* Conversione 2 registri -> float (big-endian, ordine AB CD) */
float value;
modbus_get_float_abcd(regs, &value);

/* Oppure ordine inverso a seconda del dispositivo */
modbus_get_float_dcba(regs, &value);
modbus_get_float_badc(regs, &value);
modbus_get_float_cdab(regs, &value);

/* Chiusura */
modbus_close(ctx);
modbus_free(ctx);
```

**Polling multi-dispositivo sullo stesso bus:**
```c
/* Cambiare slave address al volo */
for (int slave_id = 1; slave_id <= N_DEVICES; slave_id++) {
    modbus_set_slave(ctx, slave_id);
    rc = modbus_read_registers(ctx, start_addr, n_regs, regs);
    /* Processare dati... */
    usleep(50000); /* 50ms pausa tra dispositivi (buona pratica) */
}
```

**Cross-compilazione per RUT956 (MIPS):**
```bash
# Con l'SDK Teltonika per RUT956
export CC=mipsel-openwrt-linux-musl-gcc
export HOST=mipsel-openwrt-linux-musl

cd libmodbus-3.1.10
./configure --host=$HOST --prefix=/usr \
    --enable-static --disable-shared
make
# Risultato: src/.libs/libmodbus.a
```

**Alternative a libmodbus:**
- Implementazione diretta del protocollo (poche centinaia di righe di C per lettura registri)
- `nanomodbus` - libreria minimale header-only per embedded

### Strategia di polling
Per acquisizione continua ad alta frequenza:
- **Timer-based polling**: usare `timerfd_create()` o `clock_nanosleep()` per intervalli precisi
- **Throughput massimo**: a 9600 baud, lettura di 10 registri ~ 20ms round-trip
  - Max ~50 poll/s per singolo dispositivo
  - Con N dispositivi: ~50/N poll/s per dispositivo
- A 115200 baud: ~2ms round-trip per 10 registri, ~500 poll/s teorici
- **Errori e retry**: max 2-3 retry, poi segnalare gap nei dati
- **Buffering**: accumulare campioni localmente, impacchettare periodicamente in miniSEED

---

## 3. Formato MiniSEED

### Cos'e' MiniSEED
MiniSEED e' il formato standard de facto per la distribuzione di dati sismologici continui. E' la versione "solo dati" del piu' ampio formato SEED (Standard for the Exchange of Earthquake Data), mantenuto dalla FDSN (International Federation of Digital Seismograph Networks).

### Versione 2 vs Versione 3
| Caratteristica | miniSEED v2 | miniSEED v3 |
|---------------|-------------|-------------|
| Record length | Fisso, potenza di 2 (tipicamente 512 o 4096 byte) | **Variabile** (max ~16 MB) |
| Header | 48 byte fixed + blockettes | 40 byte fixed header |
| Identificatore | Net.Sta.Loc.Cha (max 2+5+2+3 = 12 char) | **FDSN Source ID** (URI-style, molto piu' lungo) |
| Tempo | BCD-encoded, precisione 0.1ms | **Nanosecond precision** |
| Encoding extra headers | Blockettes binarie | **JSON extra headers** |
| CRC | Opzionale (blockette 1000) | **CRC-32C obbligatorio** |
| Stato | Legacy, ancora molto usato | Standard corrente |

**Per il nostro progetto**: usare **miniSEED v2** (512 byte record) per massima compatibilita' con RingServer e per minimizzare uso di banda/memoria. La v2 e' ancora lo standard dominante nelle reti sismiche.

### Struttura record miniSEED v2 (512 byte)

```
Byte 0-19:   Fixed Data Header (20 byte)
  - Sequence number (6 char ASCII)
  - Data quality indicator ('D', 'R', 'Q', 'M')
  - Station code (5 char)
  - Location code (2 char)
  - Channel code (3 char)
  - Network code (2 char)

Byte 20-29:  Time fields (10 byte, BCD)
  - Year, Day-of-year, Hour, Min, Sec, 0.0001s

Byte 30-47:  Additional header fields (18 byte)
  - Number of samples
  - Sample rate factor / multiplier
  - Activity/IO/Quality flags
  - Number of blockettes that follow
  - Time correction
  - Offset to beginning of data
  - Offset to first blockette

Byte 48+:    Blockettes
  - Blockette 1000 (obbligatoria): encoding, byte order, record length
  - Blockette 1001 (opzionale): timing quality, microsecond offset

Data section: Campioni compressi (STEIM1, STEIM2, INT16, INT32, etc.)
```

### Codifiche dati
| Encoding | Codice | Descrizione | Compressione |
|----------|--------|-------------|-------------|
| INT16 | 1 | Interi 16-bit | Nessuna |
| INT32 | 3 | Interi 32-bit | Nessuna |
| FLOAT32 | 4 | IEEE float 32-bit | Nessuna |
| **STEIM1** | **10** | **Compressione differenziale** | **~2:1** |
| **STEIM2** | **11** | **Compressione differenziale avanzata** | **~3:1** |

**STEIM2 e' la codifica raccomandata** - offre la migliore compressione per dati sismici/geofisici (campioni interi con variazioni tipicamente piccole tra campioni consecutivi).

### Naming conventions SEED

**Network code** (2 char): identifica la rete (es. "IV" = INGV Italia, "AM" = rete amatoriale)
**Station code** (max 5 char): identifica la stazione (es. "MEDA1")
**Location code** (2 char): identifica il sensore/posizione (es. "00", "01", "  " = vuoto)
**Channel code** (3 char): identifica il canale secondo la convenzione:

```
Carattere 1: Band code (frequenza di campionamento)
  E = Short period (80-250 Hz)
  S = Short period (10-80 Hz)
  B = Broad band (10-80 Hz)
  H = High broad band (80-250 Hz)
  L = Long period (1 Hz)
  V = Very long period (0.1 Hz)
  U = Ultra long period (0.01 Hz)

Carattere 2: Instrument code
  H = High gain seismometer
  L = Low gain seismometer
  N = Accelerometer
  P = Geophone
  D = Pressure (barometro/idrometro)
  K = Temperature
  W = Wind
  F = Generic (generico)

Carattere 3: Orientation code
  Z = Vertical
  N = North-South
  E = East-West
  1, 2, 3 = Orientamento generico
```

**Esempi per acquisizione Modbus:**
- `EHZ` - sensore sismico short-period, verticale, 100 Hz
- `SHZ` - sensore sismico short-period, verticale, 50 Hz
- `EDZ` - pressione, short-period, verticale
- `EKZ` - temperatura, short-period
- `EFZ` - generico, short-period

### libmseed - Libreria C per MiniSEED

**Repository**: https://github.com/EarthScope/libmseed
**Versione corrente**: libmseed 3.x (supporta sia v2 che v3)
**Licenza**: Apache 2.0

**API principale per creazione record:**

```c
#include <libmseed.h>

/* Struttura record miniSEED v3 */
MS3Record *msr = msr3_init(NULL);

/* Impostare l'identificatore sorgente (FDSN Source ID) */
/* Formato: "FDSN:NET_STA_LOC_B_I_O" */
strcpy(msr->sid, "FDSN:AM_MEDA1_00_E_H_Z");

/* Parametri */
msr->reclen     = 512;         /* Lunghezza record */
msr->pubversion = 1;           /* Versione pubblicazione */
msr->samprate   = 100.0;       /* Sample rate Hz */
msr->encoding   = DE_STEIM2;   /* Encoding STEIM2 */
msr->sampletype = 'i';         /* 'i'=int32, 'f'=float32, 'd'=float64 */

/* Fornire campioni */
int32_t samples[100] = { /* ... dati dal Modbus ... */ };
msr->datasamples = samples;
msr->numsamples  = 100;
msr->starttime   = ms_timestr2nstime("2026-02-12T10:30:00.000000Z");

/* Callback per record completati */
void record_handler(char *record, int reclen, void *handlerdata) {
    /* record contiene il record miniSEED completo, pronto per invio */
    /* Qui si usa libdali per inviarlo al RingServer */
}

/* Impacchettare */
int64_t packed = 0;
int rv = msr3_pack(msr, record_handler, NULL, &packed, 0);

/* Cleanup */
msr->datasamples = NULL; /* non liberare, e' il nostro buffer */
msr3_destroy(&msr);
```

**Per produrre miniSEED v2** (maggiore compatibilita'):
```c
/* Usare le flag per forzare output v2 */
uint32_t flags = MSF_PACKVER2;  /* Forza formato v2 */
msr3_pack(msr, record_handler, NULL, &packed, flags);
```

**Cross-compilazione:**
```bash
cd libmseed
make CC=mipsel-openwrt-linux-musl-gcc
# Produce: libmseed.a (~150 KB)
```

**Footprint**: ~150 KB statico, ~50 KB RAM runtime.

---

## 4. RingServer e Distribuzione Dati

### Cos'e' RingServer
RingServer e' un server di distribuzione dati sismici in tempo reale sviluppato da EarthScope (ex IRIS). Implementa un **ring buffer** circolare in memoria mappata su disco, e serve i dati tramite i protocolli **SeedLink** e **DataLink**.

### Architettura
```
                    +--------------------------------------------------+
                    |              RingServer                          |
                    |                                                  |
  DataLink:16000 -->|  [DataLink Handler] --> [Ring Buffer] <-- [MSeedScan] |
                    |                              |                   |
  SeedLink:18000 <--|  [SeedLink Handler] <--------+                   |
                    +--------------------------------------------------+
```

- **Ring buffer**: file memory-mapped, dimensione configurabile (es. 1 GB)
- **Pacchetti**: ogni record miniSEED e' un pacchetto nel ring, con ID sequenziale
- **Sovrascrittura circolare**: i dati piu' vecchi vengono sovrascritti quando il ring e' pieno

### Protocolli

**DataLink** (porta 16000) - per **scrivere** e leggere dati:
- Protocollo bidirezionale
- Comando `WRITE` per inviare pacchetti miniSEED
- Usato dai nodi di acquisizione per pubblicare dati

**SeedLink** (porta 18000) - per **distribuire** dati:
- Protocollo di sola lettura (lato client)
- I client si connettono e ricevono stream continui
- Supporta v3 (legacy) e v4 (moderno, con JSON)
- Usato da SeisComP, Earthworm, ObsPy etc.

### Invio dati tramite DataLink (libdali)

**Repository**: https://github.com/EarthScope/libdali
**Licenza**: Apache 2.0

**API principale:**
```c
#include <libdali.h>

/* Creare connessione */
DLCP *dlconn = dl_newdlcp("192.168.1.100:16000", "meda-sender");

/* Connettere */
if (dl_connect(dlconn) < 0) {
    fprintf(stderr, "Connessione fallita\n");
    return -1;
}

/* Inviare un record miniSEED */
/* streamid formato: NET_STA_LOC_CHA/MSEED */
int rv = dl_write(dlconn,
    record_buffer,          /* Puntatore al record miniSEED */
    record_length,          /* Lunghezza in byte (es. 512) */
    "AM_MEDA1_00_EHZ/MSEED", /* Stream ID */
    starttime_hptime,       /* Tempo inizio (microsec da epoch) */
    endtime_hptime,         /* Tempo fine */
    1);                     /* 1 = richiedi ACK */

/* Disconnettere */
dl_disconnect(dlconn);
dl_freedlcp(dlconn);
```

**Conversione tempo:**
```c
/* hptime_t = int64_t, microsecondi dal 1970-01-01 */
hptime_t t = dl_time2hptime(2026, 43, 10, 30, 0, 0);
/* oppure da nstime_t di libmseed (nanosecondi) */
hptime_t t = (hptime_t)(nstime / 1000);
```

**Cross-compilazione:**
```bash
cd libdali
make CC=mipsel-openwrt-linux-musl-gcc
# Produce: libdali.a (~80 KB)
```

**Footprint**: ~80 KB statico, ~10 KB RAM runtime. Nessuna dipendenza esterna.

### Configurazione RingServer (lato server)

File `ring.conf`:
```ini
RingDirectory   /data/ring
RingSize        1073741824          # 1 GB ring buffer

DataLinkPort    16000               # Porta DataLink (scrittura)
SeedLinkPort    18000               # Porta SeedLink (lettura)

# Controllo accesso: solo questi IP possono scrivere
WriteIP         192.168.1.0/24
WriteIP         10.0.0.0/8

# Logging
TransferLogDirectory /var/log/ringserver/
TransferLogInterval  24

ServerID "MEDA Network RingServer"
```

### Architettura deployement per il progetto MEDA

```
+---------------------------+                    +--------------------+
| Teltonika RUT956          |                    | Server Centrale    |
| (campo, 9-30VDC)          |                    |                    |
|                           |   4G/LTE           |                    |
| Waveshare 8CH             |                    |   RingServer       |
| (RS-485, /dev/ttyATH1)    |                    |   (ring buffer)    |
|     |                     |   DataLink:16000   |                    |
|     v                     | -----------------> |                    |
| libmodbus (FC04, 25 Hz)   |   TCP over VPN     |   porta 18000      |
|     |                     |                    |       |            |
|     v                     |                    |       v            |
| libmseed (STEIM2, 512B)   |                    |   SeedLink clients |
|     |                     |                    |   (SeisComP,       |
|     v                     |                    |    ObsPy, etc.)    |
| libdali (dl_write)        |                    |                    |
+---------------------------+                    +--------------------+
```

### Banda necessaria
- 1 canale a 100 Hz, STEIM2, record 512 byte: **~1-3 KB/s**
- 3 canali a 100 Hz: **~5-10 KB/s**
- Perfettamente gestibile via connessione cellulare 4G

---

## 5. Stack Software Completo

### Librerie C necessarie

| Libreria | Scopo | Dimensione (statica) | Repository |
|----------|-------|---------------------|-----------|
| **libmodbus** | Comunicazione Modbus RTU | ~100 KB | github.com/stephane/libmodbus |
| **libmseed** | Creazione record miniSEED | ~150 KB | github.com/EarthScope/libmseed |
| **libdali** | Invio dati a RingServer | ~80 KB | github.com/EarthScope/libdali |

**Totale footprint embedded: ~330 KB di librerie + binario applicazione (~50 KB) = ~400 KB**

### Flusso dati completo

```
Sensore Modbus (RS-485)
    |
    | [Modbus RTU: read_registers()]
    v
Buffer campioni int32 (in RAM)
    |
    | [libmseed: msr3_pack() con STEIM2]
    v
Record miniSEED (512 byte)
    |
    | [libdali: dl_write() via TCP]
    v
RingServer (ring buffer)
    |
    | [SeedLink protocol]
    v
Client (SeisComP, ObsPy, etc.)
```

### Considerazioni architetturali

1. **Timing**: usare NTP o GPS per timestamp accurati. I router Teltonika hanno NTP integrato.
2. **Buffering locale**: accumulare campioni in un ring buffer locale in RAM in caso di disconnessione dal RingServer. Scrivere su flash come ultima risorsa.
3. **Reconnect automatica**: libdali supporta riconnessione automatica al RingServer.
4. **Watchdog**: usare il watchdog hardware del router per riavvio automatico in caso di crash.
5. **Configurazione**: file di configurazione per parametri Modbus (slave ID, registri, baud rate), parametri di rete (indirizzo RingServer), parametri SEED (codici rete/stazione/canale).
6. **Logging**: syslog per diagnostica, rotazione log per non riempire la flash.

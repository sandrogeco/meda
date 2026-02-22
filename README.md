# meda — Modbus RTU Acquisition & miniSEED Logger

Acquisitore Modbus RTU con output miniSEED v3, pensato per girare su Teltonika RUT956 (MIPS mipsel_24kc).

## Funzionalità

- Polling Modbus RTU multi-device, multi-canale, multi-frequenza
- Funzioni FC3 (holding registers) e FC4 (input registers)
- Output miniSEED v3 su file (encoding Steim2)
- Output DataLink (opzionale, verso ringserver)
- Cross-compilazione per RUT956 via SDK OpenWrt

## Struttura

```
src/          sorgenti meda (C)
tools/        modsim — simulatore Modbus RTU su PTY virtuale
docs/         documentazione e memoria di sessione
config.json           config principale (sviluppo)
config-test.json      config test con modsim (/tmp/vserial0)
config-waveshare.json config Waveshare ADC 8CH (/dev/ttyUSB0)
```

---

## Build locale (x86/Ubuntu)

### 1. Dipendenze di sistema

```bash
sudo apt install build-essential libmodbus-dev libcjson-dev
```

### 2. Librerie EarthScope (clonare nella root del repo)

```bash
git clone --branch v3.2.4 https://github.com/EarthScope/libmseed.git
git clone --branch v1.8.1 https://github.com/EarthScope/libdali.git
```

### 3. Build

```bash
make
```

Output: `meda` (acquisitore) e `modsim` (simulatore Modbus)

### 4. Test locale con simulatore

```bash
mkdir -p /tmp/mseed
./modsim &
./meda config-test.json
```

I file miniSEED vengono scritti in `/tmp/mseed/`.

Ispezione file generati:
```bash
gcc -o /tmp/mseedview libmseed/example/mseedview.c -Ilibmseed libmseed/libmseed.a -lm
/tmp/mseedview -d /tmp/mseed/XX.MEDA1.00.E.H.Z.mseed
```

---

## Cross-compile per RUT956 (MIPS)

### 1. SDK RutOS

Scaricare l'SDK da Teltonika (versione `RUT9M_R_GPL_00.07.20.3`) e posizionarlo in:
```
rutos-ramips-rut9m-sdk/
```

L'SDK non è incluso nel repo (troppo grande). Richiede Ubuntu 22.04 o Docker.

### 2. Build

```bash
export STAGING_DIR=$(pwd)/rutos-ramips-rut9m-sdk/staging_dir
make rut
```

Output: `build-rut/meda`, `build-rut/modsim`

Il target `rut`:
- Compila libmseed e libdali con il cross-compiler MIPS
- Linka staticamente cJSON (preso dall'SDK build_dir)
- Linka dinamicamente libmodbus (presente sul RUT956)

### 3. Deploy su RUT956

> **Importante:** gli eseguibili vanno in `/usr/local/bin/` — il filesystem `/root` è montato noexec (squashfs + overlay).

```bash
# Binari
sshpass -p 'PASSWORD' scp build-rut/meda build-rut/modsim root@IP:/usr/local/bin/

# Config e directory dati
sshpass -p 'PASSWORD' ssh root@IP "mkdir -p /root/meda"
sshpass -p 'PASSWORD' scp config-test.json config-waveshare.json root@IP:/root/meda/
```

### 4. Test su RUT956 con simulatore

```bash
ssh root@IP
mkdir -p /tmp/mseed
modsim &>/tmp/modsim.log &
sleep 1
meda /root/meda/config-test.json
```

### 5. Test su RUT956 con Waveshare ADC

Il Waveshare deve essere collegato alla porta RS-485 del RUT956 (`/dev/ttyATH1`) o via USB-RS485 (`/dev/ttyUSB0`).

```bash
meda /root/meda/config-waveshare.json
```

Il modulo Waveshare deve essere configurato in modalità 0-5V (registro 0x1000-0x1007 = 0x0000).

---

## Aggiornamento binari (workflow tipico)

```bash
# 1. Modifica sorgenti in src/
# 2. Ricompila
export STAGING_DIR=$(pwd)/rutos-ramips-rut9m-sdk/staging_dir
make rut

# 3. Deploy
sshpass -p 'PASSWORD' scp build-rut/meda root@IP:/usr/local/bin/
```

---

## Hardware target

| Componente | Dettaglio |
|---|---|
| Router | Teltonika RUT956 |
| CPU | MIPS mipsel_24kc, musl libc, GCC 8.4.0 |
| RAM | 128 MB DDR2 (~65 MB liberi a runtime) |
| Flash | 16 MB SPI NOR (~3-6 MB liberi) |
| Porta RS-485 nativa | `/dev/ttyATH1` |
| ADC | Waveshare Analog Input 8CH (RS485/Modbus RTU) |
| Connessione ADC (test) | `/dev/ttyUSB0`, 9600 baud, 8N1, slave ID 1 |

---

## TODO

- [ ] Spostare `MEDA_MSEED_BUFSIZE` (buffer campioni) in config.json
- [ ] Fix warning `uint8_t*` vs `uint16_t*` nelle chiamate modbus (SDK RutOS ha firma estesa)
- [ ] Testare DataLink end-to-end con RingServer reale
- [ ] Init script procd per avvio automatico su RUT956
- [ ] Pacchetto .ipk per installazione via opkg
- [ ] Testare Waveshare ADC collegato direttamente al RUT956 via `/dev/ttyATH1`

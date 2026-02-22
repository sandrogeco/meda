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

## Dipendenze

- [libmseed v3.2.4](https://github.com/EarthScope/libmseed) → clonare in `libmseed/`
- [libdali v1.8.1](https://github.com/EarthScope/libdali) → clonare in `libdali/`
- libmodbus (sistema o SDK)
- libcjson (sistema o SDK — per RUT: compilata staticamente da SDK)

## Build locale (x86)

```bash
make
```

## Cross-compile per RUT956

Richiede l'SDK RutOS in `rutos-ramips-rut9m-sdk/` (non incluso nel repo, troppo grande).

```bash
export STAGING_DIR=$(pwd)/rutos-ramips-rut9m-sdk/staging_dir
make rut
```

Output: `build-rut/meda`, `build-rut/modsim`

## Deploy su RUT956

Gli eseguibili vanno in `/usr/local/bin/` (overlay rw+exec).
Le config vanno in `/root/meda/`.

```bash
sshpass -p 'PASSWORD' scp build-rut/meda build-rut/modsim root@IP:/usr/local/bin/
sshpass -p 'PASSWORD' scp config-test.json root@IP:/root/meda/
```

> **Nota:** non usare `/root` per gli eseguibili — il filesystem è squashfs + overlay montato noexec.

## Test con simulatore

Sul RUT956:

```bash
mkdir -p /tmp/mseed
modsim &
meda /root/meda/config-test.json
```

## Test con Waveshare ADC 8CH

```bash
meda /root/meda/config-waveshare.json
```

Il modulo deve essere configurato in modalità 0-5V (registro 0x1000-0x1007 = 0x0000).

## Hardware target

| Componente | Dettaglio |
|---|---|
| Router | Teltonika RUT956 |
| CPU | MIPS mipsel_24kc, musl, GCC 8.4.0 |
| ADC | Waveshare Analog Input 8CH (RS485/Modbus RTU) |
| Connessione ADC | /dev/ttyUSB0, 9600 baud, 8N1, slave ID 1 |

## TODO

- [ ] Spostare `MEDA_MSEED_BUFSIZE` (buffer campioni) in config.json
- [ ] Fix warning `uint8_t*` vs `uint16_t*` in modbus SDK RutOS
- [ ] Testare encoding Steim2 con dati reali Waveshare su RUT956

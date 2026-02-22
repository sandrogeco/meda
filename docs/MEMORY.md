# MEDA Project - Memoria di sessione

## Preferenze utente
- Chiedere SEMPRE prima di usare sudo, non cercare alternative
- Non prendere iniziative se qualcosa non funziona: chiedere
- Usare Docker per cross-compilazione RUT956

## RUT956
- IP LAN: 192.168.3.1 (br-lan, nessun cavo collegato)
- IP ZeroTier: 192.168.194.207 (interfaccia ztrf2rylod)
- IP WiFi client: 192.168.1.188 (wlan0-1, gateway 192.168.1.1)
- SSH user: root
- Password: paciugO80==
- Internet: funziona via wlan0-1
- ZeroTier attivo: raggiungibile da remoto via 192.168.194.207

### Deploy binari su RUT956
- Il filesystem root è squashfs (read-only) + overlay
- NON eseguire da /root (overlay noexec)
- Posto corretto per eseguibili custom: /usr/local/bin (overlay rw+exec)
- Config e file dati: /root/meda/ (solo storage, non exec)
- Binari installati: /usr/local/bin/meda, /usr/local/bin/modsim

### Comandi deploy
```bash
sshpass -p 'paciugO80==' scp build-rut/meda build-rut/modsim root@192.168.194.207:/usr/local/bin/
sshpass -p 'paciugO80==' scp config-test.json root@192.168.194.207:/root/meda/
```

## Setup hardware
- Waveshare Analog Input 8CH su /dev/ttyUSB0
- Baud: 9600, 8N1, Modbus ID: 1
- Funzione: FC4 (input registers)
- Registri dati: 0x0000-0x0007 (canali A1-A8)
- Configurazione canali: 0x1000-0x1007 → valore 0x0000 = modalità 0-5V
- Configurazione software: 0x0002 = modalità 0-20mA (SBAGLIATA, già corretta a 0x0000)
- mbpoll: attendere 3-4s (sleep 4) tra chiamate successive (porta rimane locked)

## Build
- Cross-compile per RUT956 (MIPS mipsel_24kc, musl, GCC 8.4.0)
- SDK: rutos-ramips-rut9m-sdk
- Usare Docker per isolamento ambiente
- Output: build-rut/

## miniSEED / DataLink
- Librerie: libmseed v3.2.4, libdali v1.8.1
- Encoding: DE_STEIM2 (voluto dall'utente)
- Record length: 512 byte
- Output file: /home/sandro/Documents/meda/mseed/
- FDSN SID: FDSN:XX_MEDA1_00_B_S_S
- Config waveshare: config-waveshare.json

## Comandi utili
```bash
# Lanciare meda con waveshare (serve dialout o sudo)
sudo ./meda config-waveshare.json

# Ispezionare file miniSEED
/tmp/mseedview -d mseed/XX.MEDA1.00.E.H.Z.mseed

# Compilare mseedview
gcc -o /tmp/mseedview libmseed/example/mseedview.c -Ilibmseed libmseed/libmseed.a -lm

# mbpoll (sempre sleep 4 dopo)
mbpoll -a 1 -b 9600 -P none -t 3 /dev/ttyUSB0 ...
sleep 4
```

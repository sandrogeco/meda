# rstilt — Manuale utente

`rstilt` è uno strumento di acquisizione dati da sensori su porta seriale RS232/RS485. Legge righe di testo dalla seriale, le converte in campioni numerici tramite un parser configurabile e le scrive in un ring buffer miniSEED via protocollo DataLink (ringserver).

---

## Utilizzo

```sh
rstilt <config.json>
```

Il processo rimane in esecuzione fino a SIGINT o SIGTERM. Scrive su stderr i messaggi di stato e di errore.

---

## File di configurazione

Il file di configurazione è in formato JSON e contiene quattro sezioni obbligatorie:

```json
{
  "serial":   { ... },
  "parser":   "thk",
  "mseed":    { ... },
  "channels": [ ... ]
}
```

---

### Sezione `serial`

Parametri della porta seriale.

| Campo        | Tipo   | Default              | Descrizione                        |
|--------------|--------|----------------------|------------------------------------|
| `device`     | string | `/dev/ttyCH343USB0`  | Percorso del device seriale        |
| `baud`       | int    | `9600`               | Velocità: 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 |
| `data_bits`  | int    | `8`                  | Bit dati: 5, 6, 7, 8               |
| `stop_bits`  | int    | `1`                  | Bit di stop: 1, 2                  |
| `parity`     | string | `"N"`                | Parità: `"N"` (nessuna), `"E"` (pari), `"O"` (dispari) |

Esempio:
```json
"serial": {
  "device": "/dev/ttyCH343USB0",
  "baud": 9600,
  "parity": "N",
  "data_bits": 8,
  "stop_bits": 1
}
```

---

### Campo `parser`

Nome del parser da usare per interpretare le righe ricevute dalla seriale.

| Valore  | Sensore supportato        |
|---------|---------------------------|
| `"thk"` | Tiltmetro THK (formato CSV fisso 42 caratteri) |

```json
"parser": "thk"
```

I parser sono estensibili: aggiungere un nuovo file `rstilt_parser_<nome>.c` e registrarlo in `rstilt_parser.c`.

---

### Sezione `mseed`

Parametri per la produzione di dati miniSEED e per il trasporto verso il ring buffer.

| Campo      | Tipo   | Default       | Descrizione                                         |
|------------|--------|---------------|-----------------------------------------------------|
| `network`  | string | `"XX"`        | Codice rete FDSN (2 caratteri)                      |
| `station`  | string | `"RSTILT"`    | Codice stazione FDSN (max 5 caratteri)              |
| `location` | string | `"00"`        | Codice location FDSN (2 caratteri)                  |
| `format`   | int    | `2`           | Versione miniSEED: `2` o `3`                        |
| `reclen`   | int    | `512`         | Lunghezza record in byte (tipicamente 512 o 4096)   |
| `encoding` | string | `"steim2"`    | Encoding campioni: `"steim2"`, `"steim1"`, `"int32"`, `"int16"` |
| `datalink` | string | —             | Indirizzo DataLink del ringserver (`"host:porta"`). Se assente i dati non vengono inviati alla rete. |
| `file_dir` | string | —             | Directory in cui scrivere file miniSEED locali. Utilizzabile in alternativa o insieme a `datalink`. |

> **Nota su `encoding`:** per sensori con valori ADC di grande ampiezza (es. THK con campo `volt` a 13 cifre) usare `"int32"`. La codifica `"steim2"` può fallire se la differenza tra campioni consecutivi non è rappresentabile in 30 bit.

> **Nota su `file_dir` e `datalink`:** i due campi sono indipendenti e possono coesistere. Se la chiavetta non è montata o è piena, rstilt logga un avviso (al massimo una volta al minuto) e continua a inviare dati via DataLink senza interruzioni. La directory viene ricreata automaticamente ad ogni record, quindi la chiavetta può essere inserita a caldo senza riavviare rstilt. **Importante:** il punto di mount `/mnt/usb` non deve esistere come directory sul filesystem del RUT — deve esistere solo quando la chiavetta è effettivamente montata, altrimenti i file verrebbero scritti sulla flash interna.

Esempio:
```json
"mseed": {
  "network":  "XX",
  "station":  "THK",
  "location": "00",
  "format":   2,
  "reclen":   512,
  "encoding": "int32",
  "datalink": "localhost:16000"
}
```

---

### Sezione `channels`

Array di canali, **in ordine corrispondente all'output del parser**. Ogni elemento definisce un canale miniSEED.

| Campo            | Tipo   | Default | Descrizione                                      |
|------------------|--------|---------|--------------------------------------------------|
| `label`          | string | `"ch"`  | Nome descrittivo (solo per log)                  |
| `mseed_channel`  | string | —       | Codice canale FDSN (3 caratteri, es. `"LQV"`)    |
| `sample_rate`    | float  | `1.0`   | Frequenza di campionamento in Hz                 |

L'identificatore FDSN completo di ogni stream è: `NETWORK_STATION_LOCATION_CHANNEL`  
Esempio: `XX_THK_00_LQV`

Esempio per il tiltmetro THK:
```json
"channels": [
  { "label": "volt",    "mseed_channel": "LQV", "sample_rate": 1 },
  { "label": "accel_y", "mseed_channel": "LAX", "sample_rate": 1 },
  { "label": "accel_x", "mseed_channel": "LAY", "sample_rate": 1 },
  { "label": "temp",    "mseed_channel": "LKD", "sample_rate": 1 }
]
```

---

## Parser THK

Il parser `thk` interpreta il formato del tiltmetro THK: una riga CSV di **esattamente 42 caratteri** terminata da `\r\n`.

```
0000001908148,14510518,05997123,00787,0932
|---- 13 ----|--- 8 --|--- 8 --|-- 5-|- 4-|
  campo 0      campo 1  campo 2  campo 3  campo 4
```

| Campo | Larghezza | Canale | Descrizione          |
|-------|-----------|--------|----------------------|
| 0     | 13        | LQV    | Tensione (mV × k)    |
| 1     | 8         | LAX    | Accelerazione asse Y |
| 2     | 8         | LAY    | Accelerazione asse X |
| 3     | 5         | LKD    | Temperatura          |
| 4     | 4         | —      | Ignorato             |

**Validazione:** righe con lunghezza diversa da 42, campi con lunghezza errata o caratteri non numerici vengono scartate silenziosamente. Questo evita l'acquisizione di campioni corrotti tipici della prima riga parziale al boot.

---

## File e percorsi (RUT956)

| File | Percorso |
|------|----------|
| Eseguibile | `/usr/local/home/root/meda/rstilt` |
| Configurazione | `/usr/local/home/root/meda/config-rstilt.json` |
| Init script | `/etc/init.d/rstilt` |
| Symlink boot | `/etc/rc.d/S99rstilt` |
| Log di esecuzione | `/tmp/rstilt.log` |
| Log diagnostico timing (opzionale) | valore del campo `"timelog"` in config |

I file in `/tmp` sono su tmpfs (RAM) e vengono persi al riavvio.

Gli altri servizi correlati:

| Servizio | Eseguibile | Configurazione | Log |
|----------|------------|----------------|-----|
| ringserver | `/usr/local/bin/ringserver` | `/usr/local/home/root/meda/ring.conf` | `/tmp/ring.log` |
| meda | `/usr/local/home/root/meda/meda` | `/usr/local/home/root/meda/config-test.json` | `/tmp/meda.log` |

---

## Avvio automatico (RUT956)

Il servizio è gestito da `/etc/init.d/rstilt` (START=99), che parte dopo ringserver (START=98).

```sh
/etc/init.d/rstilt start   # avvio manuale
/etc/init.d/rstilt stop    # arresto
/etc/init.d/rstilt enable  # abilita avvio automatico al boot
/etc/init.d/rstilt disable # disabilita avvio automatico
```

Log di esecuzione: `/tmp/rstilt.log`

---

## Aggiungere un nuovo parser

1. Creare `src/rstilt_parser_<nome>.c` implementando la funzione:
   ```c
   static int <nome>_parse(const char *line, int32_t *samples, int max_ch);
   ```
2. Esportare la struttura:
   ```c
   const rstilt_parser_t rstilt_parser_<nome> = { .name="<nome>", .parse=<nome>_parse };
   ```
3. Dichiarare l'`extern` in `rstilt_parser.h`
4. Aggiungere il puntatore all'array `parsers[]` in `rstilt_parser.c`
5. Aggiungere il file sorgente a `RSTILT_SRCS` nel `Makefile`

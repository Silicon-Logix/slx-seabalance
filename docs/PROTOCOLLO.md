<!--
Silicon LogiX / SLX SeaBalance
Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
Author: Marco Pezzullo
License: Silicon LogiX Evaluation License 1.0. See LICENSE.
-->

# SDK C e protocollo SLXB v1

`sdk/include/slx/seabalance.h` definisce la ABI C. I due binari esportano
rispettivamente `SLX_SB_GetModelAPI` e `SLX_SB_GetControlAPI`. Il modello
usa ABI 2 per curva GZ, manovra e riferimento esteso. Il controllore e SLXB
mantengono versione 1. Passare la versione e dimensione della tabella,
inizializzare `size` e `abi` delle strutture e
chiudere ogni handle con la funzione `destroy` della tabella di origine.
`create` copia la configurazione. Le copie dei campioni appartengono al
chiamante. Le istanze mantengono lo stato per sessione e richiedono un
singolo thread per handle. Caricare il modulo per tutta la vita degli handle.

SeaBalance 0.3.0 aggiunge `queue_update` in coda alla tabella del modello
ABI 2. Un chiamante che richiede la dimensione della tabella precedente riceve
le operazioni originali. Il nuovo puntatore accetta una richiesta per
passo, valida i campi selezionati e applica i valori al primo campione
dopo `advance`. Le modifiche al modello e le iniezioni restano sul PC.
Il servizio della MP1 continua a ricevere solo i campioni e gli ingressi
operativi previsti da SLXB 1, con la stessa compatibilità della versione 0.2.

| Risultato | Significato |
| --- | --- |
| `0` | Successo |
| `-1` | Puntatore o argomento richiesto assente |
| `-2` | Versione o dimensione incompatibile |
| `-3` | Sequenza o tempo non ammessi |
| `-4` | Allocazione fallita |
| `-5` | Valore, modalità o calibrazione non ammessi |

Il PC usa l'API di riferimento documentata per separare `Sample` e
`Reference`. L'ABI è nativa per piattaforma. SLXB usa una serializzazione
esplicita per interoperare tra Windows x64, Linux x86_64 e ARMv7 hard-float.

## Datagrammi

UDP IPv4 sulla porta predefinita 47420. Un frame per datagramma fino a 144
byte nel profilo base oppure 168 byte con l'estensione operatore.
Interi e IEEE-754 binary64 little-endian. Header comune di 48 byte.

| Offset | Tipo | Campo |
| --- | --- | --- |
| 0 | 4 byte | ASCII `SLXB` |
| 4 | u16 | Versione 1 |
| 6 | u16 | Tipo |
| 8 | u32 | Lunghezza esatta del payload |
| 12 | u32 | Riservato, zero |
| 16 | u64 | ID di sessione diverso da zero |
| 24 | u64 | Sequenza |
| 32 | u64 | Tempo simulato in ns = sequenza × 20.000.000 |
| 40 | u64 | Clock monotono del mittente in ns |

| Tipo | Dati in ordine | Byte |
| --- | --- | --- |
| 1 Hello | ABI, versione configurazione, calibrazione, timing (1 lockstep, 2 timed) come u32, passo ns u64, pendenza comandi rad/s f64 | 32 |
| 2 Welcome | ABI, calibrazione, build, capability bit 0 SLX e bit 1 operatore, piattaforma, esito come u32, ID avvio u64 | 32 |
| 3 Sample | timestamp IMU u64, flag e modalità u32, accelerometro XYZ e gyro XYZ f64, velocità, encoder BB/TB, comando manuale f64 | 96 |
| 4 Command | stato, causa, flag, modalità effettiva u32, angoli BB/TB, stima angolo e velocità rollio f64 | 48 |
| 5 Heartbeat | Conferma diagnostica con ID e sequenza correnti | 0 |
| 6 Diagnostic | Stato, causa osservata, frame scartati, esito come u32 | 16 |
| 7 End | Richiesta di chiusura sessione | 0 |
| 8 End ack | Conferma di chiusura sessione | 0 |
| 9 Command con operatore | Command base, revisione u64, flag e modalità del pannello u32, pinna manuale rad, velocità obiettivo m/s e rotta obiettivo rad come f64 | 88 |
| 10 Sample con manovra | Sample base, rotta effettiva rad, velocità obiettivo m/s e rotta obiettivo rad come f64 | 120 |

Il servizio dichiara piattaforma 1 per Windows x64, 2 per Linux x86_64 e
3 per Linux ARMv7 hard-float. L'identificazione di un banco fisico richiede
verifica indipendente del target. I codici esito sono 0 OK, 1 incompatibile,
2 occupato, 3 dati inammissibili. Il tempo IMU è riferito al tempo simulato.
I flag del campione e del comando sono quelli in `seabalance.h`.
Accelerometro e gyro sono in m/s² e rad/s. La velocità è in m/s.

## Ciclo

1. Hello e Welcome a sequenza zero fissano contratto e versioni. La versione
   di configurazione 1 usa Sample e Command base. La versione 2 e la capability
   operatore abilitano i frame 10 e 9. Il PC riprova con configurazione 1 se
   la scheda precedente dichiara incompatibile la versione di configurazione.
   Il servizio accetta una sessione alla volta e abbina endpoint, ID, sequenza e timestamp.
2. Sample k produce Command k. Solo un comando valido consente di avanzare
   il modello a k+1. Datagrammi parziali, eccessivi, incompatibili, duplicati
   o fuori ordine vengono scartati. Valori fuori intervallo ricevono esito
   `bad_input` senza avanzare il servizio.
3. Heartbeat e Diagnostic hanno frame propri. Il watchdog del controllore
   richiede campioni progressivi anche se arrivano messaggi ausiliari.
4. Il timeout del PC, 250 ms per default, interrompe la prova al passo
   mancante. Dopo 800 ms senza campioni il servizio chiude la sessione e
   conserva l'allarme. Un nuovo Hello esplicito avvia una sessione con l'eventuale
   causa persistente. La cancellazione della causa richiede il riconoscimento.
5. End libera il servizio. Il PC registra un'eventuale conferma mancante.

I flag del pannello sono bit 0 per autorità locale, bit 1 per ordine di
manovra e bit 2 per marcatore video. Il servizio azzera la revisione
a ogni nuova sessione e accetta solo quella successiva. I frame 9 ripetono
l'ultima revisione. Il PC elabora una volta sola ogni nuova revisione per
aggiornare la manovra e registrare i marcatori. La sessione conserva l'ora
dell'applicazione in tempo simulato. Una modalità richiesta può restare
**Spento** come modalità effettiva se interviene la supervisione.

In modalità Temporizzata l'host usa una griglia monotona a 20 ms. Una risposta
tardiva ma entro timeout viene applicata al passo e conteggiata come scadenza
mancata. I timestamp monotoni dei due processi appartengono a orologi distinti.
L'RTT è misurato sul PC. Linux ed Ethernet sono usati come sistema temporizzato
a cadenza nominale. SLXB è destinato a un collegamento di laboratorio e nel
profilo base usa il bind su loopback.

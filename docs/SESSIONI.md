<!--
Silicon LogiX / SLX SeaBalance
Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
Author: Marco Pezzullo
License: Silicon LogiX Evaluation License 1.0. See LICENSE.
-->

# Sessioni, metriche e visualizzazione

`scenario.json` contiene configurazione, curva GZ e calibrazione. `samples.csv` contiene
campione grezzo, riferimento fisico, stima, comandi, posizioni, saturazioni
e tempi. La versione 2 aggiunge scomposizione dei momenti, rollio continuo,
GM, GZ, acqua, riserva ed efficacia delle pinne. La versione 3 aggiunge
rotta e velocità obiettivo, orientamento della nave e ultimo ordine del
pannello. Lo schema 4 conserva i valori applicati durante la prova e
la maschera `live_fields` sul primo campione dell'intervento. Il replay
legge anche gli schemi 1, 2 e 3. `initial-gz.json` conserva
la curva mostrata all'inizio. `events.csv` indica tempo, origine, evento e codice. Il manifest
finale registra versione, architettura dichiarata dal controllore, origine
sintetica, modalità temporale, durata e integrità della registrazione.
`analysis-v1/` contiene metriche JSON e rapporto HTML. I CSV sono UTF-8,
con decimali in punto e una prima riga di attribuzione. Le unità compaiono
nei nomi delle colonne. Un campo vuoto indica dato assente. I campioni
clippati conservano i valori grezzi con i relativi bit di validità.
`requested_mode` è la richiesta del PC, `panel_mode` è l'ultima modalità
chiesta dal pannello, `effective_mode` è quella applicata dalla centralina.
Il bit 0 di `panel_flags` identifica l'autorità del pannello.
Il cambio di modalità effettiva e ogni `video_mark` appaiono nella tabella
eventi. Selezionare il tempo di un evento posiziona il replay sul relativo passo.
Il replay del ribaltamento propone **Rollio continuo** per seguire il moto
anche oltre ±180°. Deselezionandolo si torna al confronto con la stima IMU.
**Riserva ed energia** mostra la riserva istantanea di raddrizzamento e
l'energia cinetica di rollio in kJ. Le linee verticali distinguono condizioni
applicate, eventi di guasto e ribaltamento. Nel banco live scorrono gli ultimi
20 secondi. In replay il cursore indica il campione selezionato.

Lo schema 4 registra stato del mare, altezza e periodo obiettivo, fattore di
carico, quota del baricentro, perdita di GM da superficie libera, offset
trasversale, ritardo dell'attuatore e durata impostata del guasto. L'elevazione
istantanea resta in `wave_elevation_m`. I bit 0–4 di `live_fields` indicano
mare, carico, ritardo, manovra e guasto. In `events.csv` compaiono
`live_update`, `fault_injected`, `fault_cancelled` e `live_update_rejected`.
`scenario.json` conserva la configurazione iniziale. Il manifest riporta
il numero di interventi applicati.

L'acquisizione scrive campioni, indice ed eventi in modo incrementale.
La modalità continua dura fino all'arresto dell'operatore o a un errore
dichiarato di acquisizione. Il replay usa l'indice `samples.idx` per
accedere ai campioni su disco. Una registrazione interrotta conserva i
file `.partial` e il replay può leggere la parte già indicizzata.
Usa una nuova cartella per ogni acquisizione o rianalisi.

Per aggiungere un'annotazione o usare un'altra finestra, eseguire
`slx-seabalance-cli analyze prova-base --output prova-base/analysis-v2 --warmup 20 --note "Finestra estesa"`.
Il nuovo rapporto riporta l'ID di origine e la finestra usata. CSV, scenario e
manifest della prova rimangono i riferimenti della registrazione.

## Criteri v2

- La finestra usa i campioni con tempo ≥ transitorio escluso fino all'ultimo campione disponibile. RMS e picco partono da 50 campioni, con angoli in gradi.
- L'errore RMS usa la distanza angolare minima sulle righe con stima valida. Il ritardo della stima è il lag del massimo di correlazione in ±0,50 s, campionato a 20 ms, quando serie, varianza e correlazione bastano. Il ritardo positivo indica una stima posticipata.
- L'attività normalizzata divide la somma degli spostamenti assoluti di entrambe le pinne per due volte la corsa in velocità nominale accumulabile nella stessa durata. Il tempo in saturazione somma gli intervalli di 20 ms con comando o posizione al limite.
- L'assestamento è disponibile a onde spente e perturbazione iniziale dichiarata. Usa la prima entrata definitiva entro il maggiore fra 5% del rollio iniziale e 0,1°, con almeno 2 s acquisiti nella banda.
- Il miglioramento A/B è `100 × (1 − RMS controllato / RMS spento)`. Può essere negativo. Bracci con ribaltamento, bracci incompleti, finestre insufficienti e riferimenti entro 0,01° RMS producono N/D.
- Il rapporto distingue latenza di andata e ritorno, scarto dalla cadenza del PC e risposte oltre il passo nominale.
- Prima immersione, perdita di raddrizzamento, ribaltamento, GM minimo e acqua imbarcata usano l'intera registrazione, compreso l'avvio.

## Asset 3D

Il manifest asset v0.1.0 specifica `SLX_Vessel_Root`,
`SLX_Fin_Port_Pivot` e `SLX_Fin_Starboard_Pivot`. Il GLB runtime è neutro.
Il renderer legge le basi di riposo, applica rotta attorno a +Y glTF, rollio attorno a +X e le
posizioni effettive delle due pinne attorno a −Z locale in glTF.
Questa corrisponde a `C(x,y,z)=(x,z,−y)` da Blender a glTF. La GUI mostra
separatamente posizione e comando. L'ispezione modifica camera e visibilità
della superficie del mare.

La superficie del mare segue tempo, elevazione registrata, direzione,
altezza e frequenza d'incontro dello scenario. La vista di presentazione
del repository permette di ispezionare la nave con la stessa geometria
usata durante simulazione, confronto e replay.

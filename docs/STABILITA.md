<!--
Silicon LogiX / SLX SeaBalance
Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
Author: Marco Pezzullo
License: Silicon LogiX Evaluation License 1.0. See LICENSE.
-->

# Stabilità e scenari critici

La pagina **Stabilità** mostra la curva GZ iniziale e il punto al campione
corrente. Il bilancio dei momenti indica separatamente onde, pinne,
galleggiamento, smorzamento e carico. Il confronto fra bracci ripete la
stessa eccitazione incidente e riporta gli esiti di ribaltamento con il
relativo tempo. In **Sessioni**, i pulsanti immersione, perdita di
raddrizzamento e ribaltamento portano al primo evento osservato.

| Scenario | Esperimento |
| --- | --- |
| Mare al traverso | Confrontare spento e PID con lo stesso seme delle onde. Se il modulo di controllo è disponibile, aggiungere SLX. |
| Rollio parametrico | Usare il mare quasi longitudinale e confrontare l'andamento con modulazione GM attiva e azzerata. L'eccitazione trasversale è ridotta. |
| Carico alto e superficie libera | Variare quota del CG, perdita di GM e braccio del carico. Osservare come cambiano curva, equilibrio e riserva. |
| Ingresso d'acqua asimmetrico | Seguire l'immersione delle aperture, la massa d'acqua e il momento sbandante. |
| Sequenza critica fino al ribaltamento | Osservare gruppo d'onda, raddrizzamento perso, immersione delle pinne e posa invertita. La registrazione prosegue finché si arresta la prova. |
| Pinne a velocità nulla | Applicare un comando manuale e confrontare posizione, immersione e momento effettivo. |

## Definizioni del modello

Il ramo parametrico usa una curva simmetrica con pendenza iniziale GM,
passaggio per zero al limite del ramo positivo e ramo negativo verso la
posa invertita. Carico, CG, superficie libera, fase delle onde e acqua
imbarcata modificano il momento. Il modello di apertura usa una quota
coerente con l'angolo di immersione a secco e valuta il livello locale
dell'onda. Massa, sbandamento asimmetrico e perdita di GM seguono l'acqua
accumulata.

La perdita di raddrizzamento scatta quando si supera il limite
positivo GZ istantaneo del lato o quando il ramo positivo scompare.
La riserva stimata usa l'area positiva del momento di raddrizzamento
al netto della coppia statica avversa, dall'angolo corrente al limite
GZ, con la configurazione istantanea congelata. L'energia di rollio è
`½ I p²`. Il confronto fra queste due quantità aiuta a leggere la perdita
di stabilità. Quando l'inversione si mantiene, il simulatore registra il
ribaltamento e lascia proseguire l'integrazione.

Il profilo rappresenta il rollio con un grado di libertà. Le aperture
e il comportamento dopo l'inversione sono approssimati. Le valutazioni
di conformità e la previsione per una nave richiedono il suo fascicolo
di stabilità, condizioni di carico, idrostatica, smorzamento e dati
indipendenti. Le guide IMO trattano più modi di perdita di stabilità
dinamica. [IMO · Codice IS 2008](https://wwwcdn.imo.org/localresources/en/KnowledgeCentre/IndexofIMOResolutions/MSCResolutions/MSC.267%2885%29.pdf) · [IMO · Modi di perdita di stabilità](https://www.imo.org/en/mediacentre/meetingsummaries/pages/sdc-7.aspx)

## Curva del cliente

La funzione **Importa GZ** e l'opzione `--gz` accettano CSV con
intestazione `heel_deg,gz_m`, in UTF-8 e con decimali in punto.
`examples/gz-workboat.csv` mostra il formato. Il lato positivo comprende
da 4 a 32 punti ordinati da 0° a 180°, con GZ zero a entrambi gli estremi,
rami positivo e negativo, primo punto utile fra 1° e 20° e pendenza
iniziale fino a 3 m/rad. Il modello interpola i punti e assume simmetria
per l'altro lato. Curva e stato di carico devono riferirsi alla stessa
configurazione di partenza. Il JSON della prova conserva la tabella usata.

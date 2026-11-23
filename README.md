<!--
Silicon LogiX / SLX SeaBalance
Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
Author: Marco Pezzullo
License: Silicon LogiX Evaluation License 1.0. See LICENSE.
-->

# SLX SeaBalance

SLX SeaBalance esplora il controllo attivo del rollio navale. Il PC simula
la nave, il mare e le pinne. In modalità Ethernet la centralina STM32MP135F-DK
stima il movimento, regola le pinne virtuali e mostra modalità e allarmi
sul pannello touch. Il collegamento usa SLXB su UDP.

Questo repository contiene il servizio, il pannello, il simulatore desktop,
l'interfaccia SDK e un controllore PID di riferimento. Il core proprietario
del modello e quello della strategia SLX sono distribuiti separatamente con
l'SDK di valutazione. I binari dell'SDK vanno forniti per la piattaforma
su cui si esegue l'applicazione.

La GUI usa un unico modello GLB con lo scafo e le due pinne articolate.
La superficie del mare viene generata dal renderer a partire dallo scenario.

## Compilare il servizio e il PID

La configurazione senza binari esterni compila la libreria SDK, il servizio
con il PID di riferimento e i test di contratto. Servono CMake 3.22 o
successivo e un compilatore C++20. Su Windows x64 lo script usa Visual Studio
Build Tools 2022 con il carico di lavoro C++ e il Windows SDK.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build-windows.ps1
```

Su Linux x86_64 puoi usare Ninja.

```sh
cmake -S . -B build/controller -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSLX_BUILD_PC=OFF -DSLX_BUILD_DESKTOP=OFF
cmake --build build/controller --parallel
ctest --test-dir build/controller --output-on-failure
```

Il servizio risultante è in `build/controller/bin`. L'opzione
`-DSLX_BUILD_HMI=ON` compila anche il pannello su Linux con GTK 3 e pkg-config.

## Compilare il simulatore con l'SDK di valutazione

`SLX_CORE_DIR` indica la directory dei binari per il target. Il modello è
necessario per il simulatore. Con il binario di controllo si abilita anche
la strategia SLX. In sua assenza sono disponibili PID e i comandi manuali.

Su Windows x64, passa allo script la cartella che contiene
`slx_seabalance_model.dll`, poi avvia `Run-SLX-SeaBalance.cmd`.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build-windows.ps1 -CoreDirectory 'C:\SDK\SeaBalance\windows-x64'
```

Su Linux x86_64 la GUI richiede GLFW 3.3 e OpenGL 3.3. Usa la cartella
dell'SDK che contiene `libslx_seabalance_model.so`.

```sh
cmake -S . -B build/pc -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSLX_BUILD_PC=ON -DSLX_BUILD_DESKTOP=ON -DSLX_CORE_DIR=/percorso/sdk/linux-x64
cmake --build build/pc --parallel
ctest --test-dir build/pc --output-on-failure
build/pc/bin/SLX-SeaBalance
```

Imposta `SLX_BUILD_DESKTOP=OFF` per compilare solo gli strumenti PC da
terminale. Su Windows lo stesso risultato si ottiene con `-Headless`.

## Simulazione e collegamento alla scheda

Nel **Banco di prova** scegli **Software locale** e **PID**. Confronta con
la stabilizzazione spenta a parità di onde. In **Scenario in corso** puoi
modificare mare, carico e ritardo delle pinne oppure introdurre un guasto.
In **Sessioni** puoi rivedere il moto, i comandi e gli eventi registrati.

Per usare il PC come generatore di campioni e la scheda come centralina,
seleziona **Centralina Ethernet · 50 Hz**. Dalla vista **Bilancio** del
pannello puoi prendere il comando e cambiare modalità durante la simulazione.
Il pannello **Manovra** cambia i riferimenti di velocità e rotta della nave
virtuale. [Guida per STM32MP135F-DK](docs/STM32MP135F-DK.md)

Con il modello installato, dalla directory di build del PC puoi creare
e riaprire una sessione critica.

```sh
bin/slx-seabalance-cli run --output prova-critica --preset capsize --mode off --seconds 80
bin/slx-seabalance-cli replay prova-critica --at 25
```

Gli esempi iniziali sono il [profilo di scenario](examples/scenario-demo.json)
e la [curva GZ dimostrativa](examples/gz-workboat.csv).

## Documentazione

- Flusso delle misure e dei comandi. [Architettura](docs/ARCHITETTURA.md)
- Interfaccia di integrazione via Ethernet. [Protocollo SLXB](docs/PROTOCOLLO.md)
- Registrazione, riproduzione e visualizzazione. [Sessioni](docs/SESSIONI.md)
- Carico, raddrizzamento e scenari critici. [Stabilità](docs/STABILITA.md)

## Articoli Silicon LogiX

- [Centralina di rollio su STM32MP135](https://www.siliconlogix.it/solution/slx-seabalance-centralina-di-rollio-su-stm32mp135) · [English](https://www.siliconlogix.it/en/solution/slx-seabalance-stm32mp135-roll-stabilization-controller)
- [Simulatore di stabilizzazione navale](https://www.siliconlogix.it/solution/slx-seabalance-simulatore-di-stabilizzazione-navale) · [English](https://www.siliconlogix.it/en/solution/slx-seabalance-ship-roll-and-stabilization-simulator)

## Licenze

I componenti originali seguono la [licenza di valutazione](LICENSE).
La documentazione delle dipendenze e degli asset si trova negli
[avvisi di terze parti](THIRD_PARTY_NOTICES.md) e nelle
[note sugli asset](assets/ASSET_NOTICE.txt).

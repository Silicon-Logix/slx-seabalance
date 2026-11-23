<!--
Silicon LogiX / SLX SeaBalance
Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
Author: Marco Pezzullo
License: Silicon LogiX Evaluation License 1.0. See LICENSE.
-->

# STM32MP135F-DK

Sulla STM32MP135F-DK il servizio SeaBalance esegue il controllo del rollio.
Il pannello GTK 3 su Weston mostra stima, posizione delle pinne, modalità
e allarmi sul display da 480 × 272. Il PC simula lo scafo, il mare, i
sensori e gli attuatori. [Scheda ST](https://www.st.com/en/evaluation-tools/stm32mp135f-dk.html)

## Compilare servizio e pannello

Il layer usa OpenSTLinux 6.2.1, Yocto Scarthgap e la macchina
`stm32mp13-disco`. Per compilare direttamente i componenti SeaBalance,
attiva l'ambiente SDK ST `environment-setup-cortexa7t2hf-neon-vfpv4-ostl-linux-gnueabi`.
Il sysroot deve offrire GTK 3 per il pannello. [Developer Package ST](https://wiki.st.com/stm32mpu-ecosystem-v6/wiki/STM32MPU_Developer_Package)

```sh
cmake -S . -B build/mp135 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DSLX_BUILD_PC=OFF \
  -DSLX_BUILD_DESKTOP=OFF -DSLX_BUILD_HMI=ON
cmake --build build/mp135 --parallel
cmake --install build/mp135 --prefix "$PWD/build/mp135/stage"
```

Per abilitare la strategia SLX sulla scheda, imposta
`-DSLX_CORE_DIR=/percorso/sdk/linux-armv7`. Il binario ARMv7 hard-float
del controllore proviene dall'SDK SeaBalance di valutazione.

## Integrare SeaBalance in un'immagine Yocto

Il repository contiene il layer e lo script per preparare gli input del
BSP ST. L'SDK del BSP, il modulo SeaBalance ARMv7 e l'immagine SD sono
artefatti separati. Per ricreare l'immagine usa il [manifest OpenSTLinux](https://github.com/STMicroelectronics/oe-manifest/tree/openstlinux-6.6-yocto-scarthgap-mpu-v26.06.10).

```sh
repo init -u https://github.com/STMicroelectronics/oe-manifest.git \
  -b refs/tags/openstlinux-6.6-yocto-scarthgap-mpu-v26.06.10
repo sync
python3 /percorso/seabalance/scripts/stage-yocto.py \
  --layer "$PWD/layers/meta-slx" \
  --control-core /percorso/sdk/libslx_seabalance_control.so
DISTRO=openstlinux-weston MACHINE=stm32mp13-disco \
  source layers/meta-st/scripts/envsetup.sh --no-ui
bitbake-layers add-layer ../layers/meta-slx
cat /percorso/seabalance/embedded/yocto/local.conf.fragment >> conf/local.conf
bitbake slx-image-seabalance
```

La ricetta deriva da `st-image-weston` e avvia il pannello a schermo
intero. Il frammento di configurazione abilita WIC, bmap e l'archiver dei
sorgenti. I componenti del BSP seguono le licenze e i manifest di ST e
dei rispettivi fornitori.

## Collegare PC e scheda

Collega il PC e la scheda alla stessa rete. Trova l'indirizzo IPv4 della
scheda con `ip -brief address` e inseriscilo in **IPv4 centralina** nel
simulatore. Seleziona **Centralina Ethernet · 50 Hz**. Il servizio
ascolta sulla porta UDP 47420 e restituisce un comando per ciascun
campione SLXB ricevuto.

| Vista | Funzione |
| --- | --- |
| Assetto | Mostra assetto stimato, velocità, comandi e posizioni delle pinne. |
| Bilancio | Gestisce l'autorità di comando e le modalità Spento, Manuale, PID e SLX. |
| Manovra | Regola velocità e rotta obiettivo della nave virtuale. |
| Allarmi | Mostra lo stato del collegamento e permette di riconoscere gli allarmi. |

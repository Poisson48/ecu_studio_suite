# Getting Started

[English](#english) · [Français](#français) · back to **[Home](Home)**

---

<a id="english"></a>

## English

**Free forever** (GPL-3.0) — no account, no subscription, no paid tier. **100% local** — no telemetry, no cloud; your ROMs and OBD logs stay on your machine. The only optional network use is AppImage auto-update from GitHub (user-triggered, signed).

### 1. Download (recommended)

The fastest path is the self-contained **AppImage**. Qt6, libusb and every runtime library are bundled, so it runs on a clean PC with **nothing installed** and **no root access**.

1. Grab the latest release: **[ECU Studio AppImage](https://github.com/Poisson48/ecu_studio_suite/releases/latest/download/ECU_Studio-x86_64.AppImage)** (~37 MB, Linux x86_64). Current feature set includes **OBD drive-mode tune validation** (v1.6.6+).
2. Make it executable and run it:

```bash
chmod +x ECU_Studio-x86_64.AppImage
./ECU_Studio-x86_64.AppImage
```

All releases are on the [GitHub releases page](https://github.com/Poisson48/ecu_studio_suite/releases/latest). The AppImage can **auto-update itself in place** (Ed25519-signed manifest + SHA-256 verification).

### 2. Build from source (developers)

Most users should just download the AppImage. Build from source only to develop or hack on the code.

**Dependencies (Ubuntu / Debian):**

```bash
sudo apt install \
    qt6-base-dev qt6-charts-dev qt6-serialbus-dev qt6-serialport-dev \
    libusb-1.0-0-dev libgit2-dev liblua5.4-dev \
    nlohmann-json3-dev libgtest-dev cmake ninja-build
```

**Quick build (simulation mode — no hardware required):**

```bash
git clone --recurse-submodules https://github.com/Poisson48/ecu_studio_suite
cd ecu_studio_suite
bash build.sh          # configure + build in ./build  (~1 min)
./build/apps/ecu-studio/ecu_studio
```

`build.sh` checks every dependency first and enables simulation mode by default, so you do not need an MPPS programmer to explore the UI.

**Manual CMake** (if you prefer control):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DECU_MPPS_SIMULATION=ON -DECU_BUILD_TESTS=OFF -G Ninja
cmake --build build --target ecu_studio -j$(nproc)
./build/apps/ecu-studio/ecu_studio
```

**Run the tests:**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DECU_MPPS_SIMULATION=OFF -DECU_BUILD_TESTS=ON -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

**CMake options:**

| Option | Default | Description |
|--------|---------|-------------|
| `ECU_BUILD_ECU_STUDIO` | `ON` | Build the ECU Studio GUI. |
| `ECU_BUILD_SOCKETSPY` | `ON` | Build the SocketSpy companion (submodule). |
| `ECU_BUILD_TESTS` | `ON` | Build GTest unit + integration tests. |
| `ECU_MPPS_SIMULATION` | `OFF` | Simulate the MPPS — no real USB device needed. |
| `ECU_MPPS_PROTOCOL_LOG` | `OFF` | Log all MPPS frames to stdout (for reverse-engineering). |

> Use `--recurse-submodules` — SocketSpy is a git submodule. If you already cloned without it: `git submodule update --init --recursive`.

### 3. Connect real hardware (Linux)

**MPPS V21 programmer** — grant non-root USB access once:

```bash
sudo cp libs/60-mpps.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
# Plug in the MPPS V21, then launch ECU Studio and hit Refresh in the MPPS panel.
```

Linux talks to the device through **libusb** directly — no FTDI D2XX driver needed.

**CAN bus (for SocketSpy verification)** — bring up a SocketCAN interface:

```bash
sudo ip link set can0 type can bitrate 500000
sudo ip link set up can0
# No hardware? Use a virtual bus:
sudo modprobe vcan && sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0
```

Requires a Linux kernel ≥ 5.4 with SocketCAN support.

**ELM327 (OBD drive mode)** — plug in USB-serial or pair Bluetooth, then pick the port in the **OBD** panel. No special udev rule beyond normal serial access (`dialout` group on many distros). Full walkthrough: **[OBD Drive Mode](OBD-Drive-Mode)**.

### 4. First run — two ways to verify

**A. Road validation (simplest hardware)**  
1. Load a ROM with OpenDAMOS maps in ECU Studio.  
2. Open **OBD** → select ELM327 → **▶ Start drive session**.  
3. Drive and watch measured vs expected (boost banner). Details: **[OBD Drive Mode](OBD-Drive-Mode)**.

**B. Bench / CAN validation**  
1. Edit a map (or apply an [OpenDAMOS](OpenDAMOS) recipe / AutoMod).  
2. Flash via the MPPS panel.  
3. Launch **SocketSpy** and confirm the live CAN signal at the right operating point.

That round-trip — **edit → flash → verify** — is the whole point of the suite. See **[Architecture](Architecture)** for how it is wired.

### Next steps
- **[OBD Drive Mode](OBD-Drive-Mode)** — one-button road validation with ELM327.
- **[Architecture](Architecture)** — how the hub and sub-programs fit together.
- **[Sub-Programs](Sub-Programs)** — full feature tour of ECU Studio and SocketSpy.
- **[OpenDAMOS](OpenDAMOS)** — one tuning recipe across firmware variants.
- **[FAQ](FAQ)** — hardware, legality, troubleshooting.

---

<a id="français"></a>

## Français

**Gratuit pour toujours** (GPL-3.0) — pas de compte, pas d’abonnement, pas d’offre payante. **100 % local** — aucune télémétrie, aucun cloud ; vos ROM et logs OBD restent sur votre machine. Le seul réseau optionnel est la mise à jour AppImage depuis GitHub (déclenchée par l’utilisateur, signée).

### 1. Télécharger (recommandé)

Le plus simple est l'**AppImage** autonome. Qt6, libusb et toutes les bibliothèques runtime sont embarquées : ça tourne sur un PC vierge **sans rien installer** et **sans accès root**.

1. Récupérez la dernière version : **[AppImage ECU Studio](https://github.com/Poisson48/ecu_studio_suite/releases/latest/download/ECU_Studio-x86_64.AppImage)** (~37 Mo, Linux x86_64). Inclut la **validation tune OBD en mode conduite** (v1.6.6+).
2. Rendez-la exécutable et lancez-la :

```bash
chmod +x ECU_Studio-x86_64.AppImage
./ECU_Studio-x86_64.AppImage
```

Toutes les versions sont sur la [page des releases GitHub](https://github.com/Poisson48/ecu_studio_suite/releases/latest). L'AppImage peut **se mettre à jour toute seule** (manifeste signé Ed25519 + vérification SHA-256).

### 2. Compiler depuis les sources (développeurs)

La plupart des utilisateurs prennent l'AppImage. Compilez seulement pour développer ou bidouiller le code.

**Dépendances (Ubuntu / Debian) :**

```bash
sudo apt install \
    qt6-base-dev qt6-charts-dev qt6-serialbus-dev qt6-serialport-dev \
    libusb-1.0-0-dev libgit2-dev liblua5.4-dev \
    nlohmann-json3-dev libgtest-dev cmake ninja-build
```

**Compilation rapide (mode simulation — sans matériel) :**

```bash
git clone --recurse-submodules https://github.com/Poisson48/ecu_studio_suite
cd ecu_studio_suite
bash build.sh          # configure + compile dans ./build  (~1 min)
./build/apps/ecu-studio/ecu_studio
```

`build.sh` vérifie d'abord chaque dépendance et active le mode simulation par défaut : pas besoin de programmateur MPPS pour explorer l'interface.

**CMake manuel** (si vous voulez le contrôle) :

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DECU_MPPS_SIMULATION=ON -DECU_BUILD_TESTS=OFF -G Ninja
cmake --build build --target ecu_studio -j$(nproc)
./build/apps/ecu-studio/ecu_studio
```

**Lancer les tests :**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DECU_MPPS_SIMULATION=OFF -DECU_BUILD_TESTS=ON -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

**Options CMake :**

| Option | Défaut | Description |
|--------|--------|-------------|
| `ECU_BUILD_ECU_STUDIO` | `ON` | Compile l'interface ECU Studio. |
| `ECU_BUILD_SOCKETSPY` | `ON` | Compile le compagnon SocketSpy (sous-module). |
| `ECU_BUILD_TESTS` | `ON` | Compile les tests GTest (unitaires + intégration). |
| `ECU_MPPS_SIMULATION` | `OFF` | Simule le MPPS — aucun périphérique USB réel nécessaire. |
| `ECU_MPPS_PROTOCOL_LOG` | `OFF` | Journalise toutes les trames MPPS sur stdout (reverse engineering). |

> Utilisez `--recurse-submodules` — SocketSpy est un sous-module git. Si vous avez déjà cloné sans : `git submodule update --init --recursive`.

### 3. Brancher le vrai matériel (Linux)

**Programmateur MPPS V21** — accordez l'accès USB sans root une bonne fois :

```bash
sudo cp libs/60-mpps.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
# Branchez le MPPS V21, lancez ECU Studio et cliquez Refresh dans le panneau MPPS.
```

Sous Linux, on parle au périphérique via **libusb** directement — pas de pilote FTDI D2XX.

**Bus CAN (pour la vérification SocketSpy)** — montez une interface SocketCAN :

```bash
sudo ip link set can0 type can bitrate 500000
sudo ip link set up can0
# Pas de matériel ? Bus virtuel :
sudo modprobe vcan && sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0
```

Nécessite un noyau Linux ≥ 5.4 avec le support SocketCAN.

**ELM327 (mode conduite OBD)** — branchez l’USB-série ou appairage Bluetooth, puis choisissez le port dans le panneau **OBD**. Souvent le groupe `dialout` suffit. Guide complet : **[OBD Drive Mode](OBD-Drive-Mode)**.

### 4. Premier lancement — deux façons de vérifier

**A. Validation route (matériel le plus simple)**  
1. Chargez une ROM avec cartes OpenDAMOS.  
2. Panneau **OBD** → ELM327 → **▶ Lancer session conduite**.  
3. Roulez et comparez mesuré vs attendu (bandeau boost). Détails : **[OBD Drive Mode](OBD-Drive-Mode)**.

**B. Validation banc / CAN**  
1. Éditez une carte (ou recette [OpenDAMOS](OpenDAMOS) / AutoMod).  
2. Flashez via le panneau MPPS.  
3. Lancez **SocketSpy** et confirmez le signal CAN au bon point de fonctionnement.

Cet aller-retour — **éditer → flasher → vérifier** — est tout l'intérêt de la suite. Voir **[Architecture](Architecture)** pour le câblage.

### Étapes suivantes
- **[OBD Drive Mode](OBD-Drive-Mode)** — validation route en un bouton avec ELM327.
- **[Architecture](Architecture)** — comment le hub et les sous-programmes s'emboîtent.
- **[Sub-Programs](Sub-Programs)** — tour complet des fonctionnalités d'ECU Studio et SocketSpy.
- **[OpenDAMOS](OpenDAMOS)** — une seule recette de tuning sur plusieurs variantes firmware.
- **[FAQ](FAQ)** — matériel, légalité, dépannage.

# Getting Started

[English](#english) · [Français](#français) · back to **[Home](Home)**

---

<a id="english"></a>

## English

### 1. Download (recommended)

The fastest path is the self-contained **AppImage**. Qt6, libusb and every runtime library are bundled, so it runs on a clean PC with **nothing installed** and **no root access**.

1. Grab the latest release: **[ECU Studio AppImage](https://github.com/Poisson48/ecu_studio_suite/releases/latest/download/ECU_Studio-x86_64.AppImage)** (~37 MB, Linux x86_64).
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

### 4. First run — the flagship loop

1. In **ECU Studio**, open or create a project, load a ROM, and edit a map (or apply an [OpenDAMOS](OpenDAMOS) recipe / AutoMod).
2. Flash it to the ECU through the MPPS panel.
3. Launch **SocketSpy** side-by-side (ECU Studio has a CAN-companion launcher) and watch the live signal change on the bus.

That round-trip — **edit → flash → verify** — is the whole point of the suite. See **[Architecture](Architecture)** for how it is wired.

### Next steps
- **[Architecture](Architecture)** — how the hub and sub-programs fit together.
- **[Sub-Programs](Sub-Programs)** — full feature tour of ECU Studio and SocketSpy.
- **[OpenDAMOS](OpenDAMOS)** — one tuning recipe across firmware variants.
- **[FAQ](FAQ)** — hardware, legality, troubleshooting.

---

<a id="français"></a>

## Français

### 1. Télécharger (recommandé)

Le plus simple est l'**AppImage** autonome. Qt6, libusb et toutes les bibliothèques runtime sont embarquées : ça tourne sur un PC vierge **sans rien installer** et **sans accès root**.

1. Récupérez la dernière version : **[AppImage ECU Studio](https://github.com/Poisson48/ecu_studio_suite/releases/latest/download/ECU_Studio-x86_64.AppImage)** (~37 Mo, Linux x86_64).
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

### 4. Premier lancement — la boucle phare

1. Dans **ECU Studio**, ouvrez/créez un projet, chargez une ROM, éditez une carte (ou appliquez une recette [OpenDAMOS](OpenDAMOS) / un AutoMod).
2. Flashez-la dans l'ECU via le panneau MPPS.
3. Lancez **SocketSpy** côte à côte (ECU Studio a un lanceur compagnon CAN) et observez le signal changer en direct sur le bus.

Cet aller-retour — **éditer → flasher → vérifier** — est tout l'intérêt de la suite. Voir **[Architecture](Architecture)** pour le câblage.

### Étapes suivantes
- **[Architecture](Architecture)** — comment le hub et les sous-programmes s'emboîtent.
- **[Sub-Programs](Sub-Programs)** — tour complet des fonctionnalités d'ECU Studio et SocketSpy.
- **[OpenDAMOS](OpenDAMOS)** — une seule recette de tuning sur plusieurs variantes firmware.
- **[FAQ](FAQ)** — matériel, légalité, dépannage.

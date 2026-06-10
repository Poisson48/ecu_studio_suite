#pragma once
//
// Edc17Checksum — vérification / correction des checksums Bosch EDC17 / MED17
// (TriCore). Contrairement à l'EDC16 (fenêtre fixe, CRC-16/ARC), l'EDC17 décrit
// ses régions checksummées dans une TABLE DE BLOCS auto-portée présente dans la
// ROM. Chaque bloc se termine par le marqueur 0xDEADBEEF et liste des structures
// checksum (CRC32 / ADD32 / ADD16) sur des régions [start..end].
//
// Algorithme + structure VALIDÉS sur 3 dumps EDC17 réels (CRC32 = 100 % valides),
// voir docs/ecu-research/edc17.md §0. Paramètres :
//   - découverte bloc : (u32@+0 & 0xFF) ∈ {0x10,0x30,0x40,0x60,0xC0},
//     size=u32@+4 (0x40..reste), 0xDEADBEEF à start+size-4 ;
//   - en-tête : +0x2C = nb structures, +0x34 = 1ère structure (32 o) ;
//   - structure (LE) : +4 start, +8 end (inclus), +12 init, +16 attendu, +28 algo&0xFF
//     (0x00=CRC32, 0x01=ADD32, 0x10=ADD16) ;
//   - adresse → offset : addr & 0x1FFFFFFF (miroirs TriCore) ;
//   - CRC32 poly 0xEDB88320, init=champ init, dwords LE, attendu 0x35015001 ;
//     ADD32/ADD16 init=champ init, attendu=champ attendu (0xCAFEAFFE).
//
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ecu {

enum class Edc17Algo { Crc32, Add32, Add16, Unknown };

// Une structure checksum d'un bloc.
struct Edc17Cs {
    Edc17Algo    algo      = Edc17Algo::Unknown;
    std::uint32_t startAddr = 0;   // adresse TriCore (affichage)
    std::uint32_t endAddr   = 0;   // adresse TriCore, inclusive
    std::size_t   startOff  = 0;   // offset fichier
    std::size_t   endOff    = 0;   // offset fichier, inclusive
    std::uint32_t init      = 0;
    std::uint32_t expected  = 0;
    std::uint32_t computed  = 0;
    bool          valid     = false;
    bool          inBounds  = false;  // false = région hors de l'image fournie
    std::size_t   structOff = 0;   // offset de la structure (pour debug)
};

struct Edc17Block {
    std::uint32_t        id       = 0;   // (octet de poids faible ∈ BLOCK_IDS)
    std::size_t          fileOff  = 0;
    std::size_t          size     = 0;
    std::vector<Edc17Cs> cs;
};

struct Edc17Result {
    bool                    isEdc17 = false;  // au moins une structure checksum trouvée
    std::vector<Edc17Block> blocks;

    int  total()      const;     // nb total de structures checksum
    int  validCount() const;     // nb valides
    int  inBoundsCount() const;  // nb dont la région est dans l'image
    bool allValid()   const;     // toutes les structures in-bounds sont valides
};

// Vérifie (lecture seule) la table de blocs EDC17/MED17 d'une image.
Edc17Result edc17Verify(std::span<const std::uint8_t> image);

// Corrige en place toutes les structures invalides (in-bounds). ADD : ajuste les
// 4 derniers octets de la région ; CRC32 : résout le patch 4 octets par élimination
// de Gauss sur GF(2). Renvoie le nombre de checksums corrigés, ou nullopt si l'image
// n'est pas une EDC17 (aucun bloc trouvé).
std::optional<int> edc17Correct(std::span<std::uint8_t> image);

} // namespace ecu

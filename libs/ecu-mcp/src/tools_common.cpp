// tools_common.cpp — helpers partagés par les fabriques d'outils MCP
// (accès fichiers ROM, conversions enum→libellé, formatage d'adresses).

#include "tools_internal.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace ecu::mcp {

std::string requireString(const json& params, const char* key) {
    if (!params.contains(key) || !params[key].is_string())
        throw std::runtime_error(std::string("paramètre requis manquant : ") + key);
    return params[key].get<std::string>();
}

double requireNumber(const json& params, const char* key) {
    if (!params.contains(key) || !params[key].is_number())
        throw std::runtime_error(std::string("paramètre numérique requis manquant : ") + key);
    return params[key].get<double>();
}

// Lit un fichier ROM entier en mémoire. Lève si introuvable.
std::vector<uint8_t> loadRom(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("fichier ROM introuvable : " + path);
    const std::streamsize size = f.tellg();
    if (size <= 0) throw std::runtime_error("fichier ROM vide : " + path);
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<std::size_t>(size));
    if (!f.read(reinterpret_cast<char*>(buf.data()), size))
        throw std::runtime_error("échec de lecture du fichier ROM : " + path);
    return buf;
}

// Écrit un buffer dans un fichier de sortie (chemin distinct). Lève en cas d'échec.
void writeRom(const std::string& path, const std::vector<uint8_t>& buf) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("impossible d'ouvrir le fichier de sortie : " + path);
    f.write(reinterpret_cast<const char*>(buf.data()),
            static_cast<std::streamsize>(buf.size()));
    if (!f) throw std::runtime_error("échec d'écriture du fichier de sortie : " + path);
}

const char* addrSourceStr(ecu::AddressSource s) {
    switch (s) {
        case ecu::AddressSource::Fingerprint:     return "fingerprint";
        case ecu::AddressSource::Anchor:          return "anchor";
        case ecu::AddressSource::DefaultFallback: return "default-fallback";
    }
    return "unknown";
}

const char* riskStr(ecu::RecipeRisk r) {
    switch (r) {
        case ecu::RecipeRisk::Low:    return "low";
        case ecu::RecipeRisk::Medium: return "medium";
        case ecu::RecipeRisk::High:   return "high";
    }
    return "unknown";
}

const char* damosTypeStr(ecu::DamosType t) {
    switch (t) {
        case ecu::DamosType::Map:   return "MAP";
        case ecu::DamosType::Curve: return "CURVE";
        case ecu::DamosType::Value: return "VALUE";
        default:                    return "UNKNOWN";
    }
}

json hexAddr(std::size_t a) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "0x%zX", a);
    return std::string(buf);
}

} // namespace ecu::mcp

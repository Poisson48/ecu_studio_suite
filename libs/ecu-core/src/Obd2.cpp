// Obd2.cpp — voir Obd2.hpp. Module pur (sans matériel), formules J1979.

#include "ecu/Obd2.hpp"

#include <QRegularExpression>

namespace ecu::obd2 {

namespace {

// Tokenise un texte ELM327 en flux d'octets : on garde uniquement les jetons de
// EXACTEMENT 2 caractères hexadécimaux. Cela élimine les headers CAN 11 bits
// ("7E8"), les préfixes de ligne multi-trame ("0:", "1:") et les lignes de
// longueur ("014"), tout en conservant les octets de données dans l'ordre.
std::vector<std::uint8_t> hexBytes(const QString& text) {
    std::vector<std::uint8_t> out;
    const QStringList toks = text.split(QRegularExpression(QStringLiteral("[^0-9A-Fa-f]+")),
                                        Qt::SkipEmptyParts);
    for (const QString& t : toks) {
        if (t.size() != 2) continue;
        bool ok = false;
        const uint v = t.toUInt(&ok, 16);
        if (ok) out.push_back(static_cast<std::uint8_t>(v));
    }
    return out;
}

char dtcLetter(std::uint8_t hi) {
    static const char L[4] = { 'P', 'C', 'B', 'U' };
    return L[(hi >> 6) & 0x3];
}

} // namespace

const QList<LivePid>& livePids() {
    static const QList<LivePid> kPids = {
        { 0x0C, "Régime",            "rpm"  },
        { 0x0D, "Vitesse",           "km/h" },
        { 0x04, "Charge moteur",     "%"    },
        { 0x0B, "Pression admission","kPa"  },  // MAP / boost
        { 0x10, "Débit d'air (MAF)", "g/s"  },
        { 0x05, "Temp. liquide",     "°C"   },
        { 0x0F, "Temp. admission",   "°C"   },
        { 0x11, "Papillon",          "%"    },
        { 0x0E, "Avance allumage",   "°"    },
        { 0x24, "Lambda (commandé)", "λ"    },
        { 0x06, "Trim court (B1)",   "%"    },
        { 0x07, "Trim long (B1)",    "%"    },
        { 0x42, "Tension module",    "V"    },
        { 0x33, "Pression baro.",    "kPa"  },
    };
    return kPids;
}

QString pidRequest(std::uint8_t pid, std::uint8_t mode) {
    return QStringLiteral("%1%2")
        .arg(mode, 2, 16, QLatin1Char('0'))
        .arg(pid,  2, 16, QLatin1Char('0'))
        .toUpper();
}

Obd2Resp parseResponse(const QString& elmText, std::uint8_t wantMode, std::uint8_t wantPid) {
    Obd2Resp r;
    const auto b = hexBytes(elmText);
    const std::uint8_t respMode = static_cast<std::uint8_t>(wantMode + 0x40);
    for (std::size_t i = 0; i + 1 < b.size(); ++i) {
        if (b[i] == respMode && b[i + 1] == wantPid) {
            r.ok = true;
            r.mode = wantMode;
            r.pid  = wantPid;
            std::size_t j = i + 2;
            while (r.len < 8 && j < b.size()) r.data[r.len++] = b[j++];
            return r;
        }
    }
    return r;
}

std::optional<double> interpret(std::uint8_t pid, const std::uint8_t* data, std::uint8_t len) {
    if (!data || len < 1) return std::nullopt;
    const double A = data[0];
    const double B = (len >= 2) ? data[1] : 0.0;
    switch (pid) {
        case 0x04: return A * 100.0 / 255.0;          // charge moteur %
        case 0x05: return A - 40.0;                   // temp liquide °C
        case 0x06: return (A - 128.0) * 100.0 / 128.0; // trim court B1 %
        case 0x07: return (A - 128.0) * 100.0 / 128.0; // trim long B1 %
        case 0x0A: return A * 3.0;                     // pression carburant kPa
        case 0x0B: return A;                           // MAP kPa
        case 0x0C: return (256.0 * A + B) / 4.0;       // RPM
        case 0x0D: return A;                           // vitesse km/h
        case 0x0E: return A / 2.0 - 64.0;              // avance allumage °
        case 0x0F: return A - 40.0;                    // temp admission °C
        case 0x10: return (256.0 * A + B) / 100.0;     // MAF g/s
        case 0x11: return A * 100.0 / 255.0;           // papillon %
        case 0x1F: return 256.0 * A + B;               // temps moteur s
        case 0x21: return 256.0 * A + B;               // distance MIL km
        case 0x24: return (256.0 * A + B) * 2.0 / 65536.0; // lambda commandé (ratio)
        case 0x2F: return A * 100.0 / 255.0;           // niveau carburant %
        case 0x31: return 256.0 * A + B;               // distance depuis effacement km
        case 0x33: return A;                           // pression baro kPa
        case 0x42: return (256.0 * A + B) / 1000.0;    // tension module V
        case 0x46: return A - 40.0;                    // temp ambiante °C
        case 0x5C: return A - 40.0;                    // temp huile °C
        default:   return std::nullopt;
    }
}

QString pidName(std::uint8_t pid) {
    for (const auto& p : livePids())
        if (p.pid == pid) return QString::fromUtf8(p.name);
    switch (pid) {
        case 0x0A: return QStringLiteral("Pression carburant");
        case 0x1F: return QStringLiteral("Temps moteur");
        case 0x2F: return QStringLiteral("Niveau carburant");
        case 0x46: return QStringLiteral("Temp. ambiante");
        case 0x5C: return QStringLiteral("Temp. huile");
        default:   return QStringLiteral("PID 0x%1").arg(pid, 2, 16, QLatin1Char('0')).toUpper();
    }
}

QString pidUnit(std::uint8_t pid) {
    for (const auto& p : livePids())
        if (p.pid == pid) return QString::fromUtf8(p.unit);
    switch (pid) {
        case 0x0A: return QStringLiteral("kPa");
        case 0x1F: return QStringLiteral("s");
        case 0x2F: return QStringLiteral("%");
        case 0x46: case 0x5C: return QStringLiteral("°C");
        default:   return QString();
    }
}

QStringList decodeDtcs(const QString& elmText) {
    QStringList out;
    const auto b = hexBytes(elmText);
    // Cherche la réponse mode 03 (0x43), puis décode les paires d'octets (un DTC
    // = 2 octets). 00 00 = padding / pas de code.
    for (std::size_t i = 0; i + 1 < b.size(); ++i) {
        if (b[i] != 0x43) continue;
        for (std::size_t j = i + 1; j + 1 < b.size(); j += 2) {
            const std::uint8_t hi = b[j], lo = b[j + 1];
            if (hi == 0 && lo == 0) continue;     // padding
            out << QStringLiteral("%1%2%3%4%5")
                       .arg(dtcLetter(hi))
                       .arg((hi >> 4) & 0x3)
                       .arg(hi & 0xF, 0, 16)
                       .arg((lo >> 4) & 0xF, 0, 16)
                       .arg(lo & 0xF, 0, 16)
                       .toUpper();
        }
        break;
    }
    out.removeDuplicates();
    return out;
}

QString decodeVin(const QString& elmText) {
    const auto b = hexBytes(elmText);
    for (std::size_t i = 0; i + 1 < b.size(); ++i) {
        if (b[i] == 0x49 && b[i + 1] == 0x02) {
            // Après "49 02" vient un octet de comptage (souvent 01), puis le VIN ASCII.
            std::size_t j = i + 2;
            if (j < b.size() && b[j] <= 0x0F) ++j;   // saute l'octet de comptage
            QString vin;
            for (; j < b.size() && vin.size() < 17; ++j) {
                const std::uint8_t c = b[j];
                if (c >= 0x20 && c < 0x7F) vin += QChar(c);
            }
            return vin;
        }
    }
    return QString();
}

} // namespace ecu::obd2

#include "ecu/A2lParser.hpp"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

namespace ecu {

bool A2lParser::parse(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    // TODO: implémenter le parser ASAP2 complet
    // Pour l'instant stub minimal — lit les blocs /begin CHARACTERISTIC
    QTextStream in(&file);
    QString content = in.readAll();

    static QRegularExpression re(
        R"re(/begin CHARACTERISTIC\s+(\w+)\s+"([^"]*)"\s+(\w+)\s+(0x[\dA-Fa-f]+|\d+))re",
        QRegularExpression::MultilineOption);

    auto it = re.globalMatch(content);
    while (it.hasNext()) {
        auto m = it.next();
        Characteristic c;
        c.name            = m.captured(1);
        c.longIdentifier  = m.captured(2);
        c.type            = m.captured(3);
        bool ok;
        c.address = m.captured(4).toUInt(&ok, 0);
        if (ok) m_chars.append(c);
    }
    return true;
}

std::optional<Characteristic> A2lParser::findByAddress(uint32_t address) const {
    for (const auto& c : m_chars)
        if (c.address == address) return c;
    return std::nullopt;
}

std::optional<Characteristic> A2lParser::findByName(const QString& name) const {
    for (const auto& c : m_chars)
        if (c.name == name) return c;
    return std::nullopt;
}

} // namespace ecu

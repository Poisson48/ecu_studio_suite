#pragma once
#include <QString>
#include <QList>
#include <optional>
#include <cstdint>

namespace ecu {

struct Characteristic {
    QString      name;
    QString      longIdentifier;
    uint32_t     address = 0;
    QString      type;       // VALUE, CURVE, MAP, VAL_BLK
    int          nx = 1, ny = 1;
    float        factor = 1.0f, offset = 0.0f;
    QString      unit;
    QString      axisXName, axisYName;
    QList<float> axisX, axisY;
};

class A2lParser {
public:
    bool parse(const QString& path);
    const QList<Characteristic>& characteristics() const { return m_chars; }
    std::optional<Characteristic> findByAddress(uint32_t address) const;
    std::optional<Characteristic> findByName(const QString& name) const;

private:
    QList<Characteristic> m_chars;
};

} // namespace ecu

#include "rom_document.h"
#include <QFile>
#include <QFileInfo>

namespace ecu_studio {

RomDocument::RomDocument(QObject* parent) : QObject(parent) {}

bool RomDocument::loadFromFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    m_rom  = f.readAll();
    m_path = path;
    m_name = QFileInfo(path).fileName();
    m_modified = false;
    emit modifiedStateChanged(false);
    emit romLoaded();
    return true;
}

bool RomDocument::loadFromData(const QByteArray& data, const QString& name) {
    if (data.isEmpty()) return false;
    m_rom  = data;
    m_path.clear();
    m_name = name;
    m_modified = false;
    emit modifiedStateChanged(false);
    emit romLoaded();
    return true;
}

bool RomDocument::saveToFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(m_rom);
    m_path = path;
    m_name = QFileInfo(path).fileName();
    if (m_modified) { m_modified = false; emit modifiedStateChanged(false); }
    return true;
}

void RomDocument::setEcuId(const QString& id) {
    if (m_ecuId == id) return;
    m_ecuId = id;
    emit ecuChanged(id);
}

void RomDocument::markModified(qsizetype offset, qsizetype length) {
    if (!m_modified) { m_modified = true; emit modifiedStateChanged(true); }
    emit romModified(offset, length);
}

} // namespace ecu_studio

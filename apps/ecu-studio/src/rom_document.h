#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <memory>
#include "ecu/ProjectManager.hpp"

namespace ecu_studio {

// Modèle de document partagé : la ROM actuellement chargée et son contexte.
// Tous les panels (Hex, Maps, Checksum, Compare, AutoMods, Git, A2L) lisent et
// modifient cet objet unique et réagissent à ses signaux. C'est l'équivalent
// du « projet courant » côté ECU Studio.
class RomDocument : public QObject {
    Q_OBJECT
public:
    explicit RomDocument(QObject* parent = nullptr);

    // ── Chargement / sauvegarde ────────────────────────────────────────────
    bool loadFromFile(const QString& path);
    bool loadFromData(const QByteArray& data, const QString& name);
    bool saveToFile(const QString& path);

    // ── Accès ───────────────────────────────────────────────────────────────
    bool                isLoaded() const { return !m_rom.isEmpty(); }
    const QByteArray&   rom()      const { return m_rom; }
    QByteArray&         romMutable()     { return m_rom; }   // pour patching in-place
    QString             path()     const { return m_path; }
    QString             name()     const { return m_name; }
    QString             ecuId()    const { return m_ecuId; }
    bool                isModified() const { return m_modified; }

    void setEcuId(const QString& id);

    // À appeler après une modification in-place de romMutable() pour notifier
    // les panels. offset/length décrivent la plage touchée (-1 = tout).
    void markModified(qsizetype offset = -1, qsizetype length = -1);

signals:
    void romLoaded();                                  // nouvelle ROM chargée
    void romModified(qsizetype offset, qsizetype length); // octets modifiés
    void ecuChanged(const QString& ecuId);
    void modifiedStateChanged(bool modified);

private:
    QByteArray m_rom;
    QString    m_path;
    QString    m_name;
    QString    m_ecuId;
    bool       m_modified = false;
};

} // namespace ecu_studio

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
    // `managed` = true quand le fichier est une COPIE DE TRAVAIL gérée par l'app
    // (ex. rom.bin d'un projet, dont l'original est déjà préservé en
    // rom.original.bin). Pour un fichier EXTERNE ouvert « en express » (managed
    // == false), on protège l'original : une sauvegarde immuable « .orig » est
    // créée à côté, et l'autosave NE l'écrasera jamais (cf. MainWindow::autoSave).
    bool loadFromFile(const QString& path, bool managed = false);
    bool loadFromData(const QByteArray& data, const QString& name);
    bool saveToFile(const QString& path);

    // true si la ROM courante est une copie de travail gérée (autosave autorisé).
    bool                isManaged() const { return m_managed; }
    // Chemin de la sauvegarde immuable de l'original sur disque (vide si aucune).
    QString             originalBackupPath() const { return m_originalBackup; }

    // ── Accès ───────────────────────────────────────────────────────────────
    bool                isLoaded() const { return !m_rom.isEmpty(); }
    const QByteArray&   rom()      const { return m_rom; }
    QByteArray&         romMutable()     { return m_rom; }   // pour patching in-place
    QString             path()     const { return m_path; }
    QString             name()     const { return m_name; }
    QString             ecuId()    const { return m_ecuId; }
    bool                isModified() const { return m_modified; }

    // ── Baseline (mode fantôme) ────────────────────────────────────────────
    // Snapshot capturé au chargement (et figé jusqu'à un nouveau load ou un
    // resetBaseline()). Les panels 2D/3D s'en servent pour superposer la ROM
    // d'origine en transparence sous l'édition courante.
    bool                hasBaseline() const { return !m_baseline.isEmpty(); }
    const QByteArray&   baseline()    const { return m_baseline; }
    // Force la baseline = état actuel — à appeler après un commit / save afin
    // que les modifications suivantes soient comparées au nouvel état "stable".
    void                resetBaseline();
    // Définit la baseline depuis des octets explicites (ex. ROM extraite d'un
    // commit git spécifique via `git show <sha>:<path>`).
    void                setBaselineFromBytes(const QByteArray& bytes,
                                              const QString&    label = {});
    // Étiquette descriptive de la baseline (pour la barre d'info) : "au chargement",
    // "commit abc1234 (il y a 2 jours)", "fichier maRom.bak"...
    QString             baselineLabel() const { return m_baselineLabel; }

    void setEcuId(const QString& id);

    // À appeler après une modification in-place de romMutable() pour notifier
    // les panels. offset/length décrivent la plage touchée (-1 = tout).
    void markModified(qsizetype offset = -1, qsizetype length = -1);

signals:
    void romLoaded();                                  // nouvelle ROM chargée
    void romModified(qsizetype offset, qsizetype length); // octets modifiés
    void ecuChanged(const QString& ecuId);
    void modifiedStateChanged(bool modified);
    void baselineChanged();                            // resetBaseline() / nouvelle baseline

private:
    QByteArray m_rom;
    QByteArray m_baseline;   // copie figée au load (mode fantôme)
    QString    m_baselineLabel;
    QString    m_path;
    QString    m_name;
    QString    m_ecuId;
    QString    m_originalBackup;       // sauvegarde immuable de l'original (disque)
    bool       m_managed   = false;    // copie de travail gérée → autosave autorisé
    bool       m_modified  = false;
};

} // namespace ecu_studio

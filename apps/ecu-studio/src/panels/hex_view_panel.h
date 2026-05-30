#pragma once
#include <QWidget>
#include <QString>
#include <QByteArray>
#include <QList>
#include <QPair>

class QLabel;

namespace ecu_studio {

class RomDocument;
class HexView;   // vue de défilement custom (définie dans le .cpp)

// Panneau « Hex » : éditeur hexadécimal performant pour ROMs 2–6 Mo.
//
// Toute la logique de rendu / défilement virtuel vit dans HexView
// (QAbstractScrollArea, défini dans le .cpp). Ce panneau enveloppe la vue
// d'une ligne de statut et expose l'API publique attendue par le reste de
// l'application.
class HexViewPanel : public QWidget {
    Q_OBJECT
public:
    // Signature attendue par le reste du code : document ROM partagé.
    explicit HexViewPanel(RomDocument* doc, QWidget* parent = nullptr);

    // Surcharge de commodité : crée et possède un RomDocument interne.
    // Utilisée par les sites d'appel ne disposant pas (encore) d'un document
    // partagé (ex. main_window passe « this »).
    explicit HexViewPanel(QWidget* parent = nullptr);

    RomDocument* document() const { return m_doc; }

    void loadRom(const QString& path);    // -> doc->loadFromFile(path)
    void loadRom(const QByteArray& data); // -> doc->loadFromData(data, "ROM")

public slots:
    // Défile jusqu'à l'offset et le sélectionne (curseur).
    void gotoOffset(quint32 offset);
    // Régions (offset, longueur) peintes avec un fond accentué translucide.
    void setMapHighlights(const QList<QPair<quint32, quint32>>& ranges);

private:
    void buildUi();
    void updateStatus(quint32 offset);

    RomDocument* m_doc{nullptr};
    bool         m_ownsDoc{false};
    HexView*     m_view{nullptr};
    QLabel*      m_status{nullptr};
};

} // namespace ecu_studio

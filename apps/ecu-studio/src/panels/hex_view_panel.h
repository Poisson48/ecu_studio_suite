#pragma once
#include <QWidget>
#include <QString>
#include <QByteArray>
#include <QList>
#include <QPair>

class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class QToolButton;

namespace ecu_studio {

class RomDocument;
class HexView;   // vue de défilement custom (définie dans le .cpp)

// Panneau « Hex » : éditeur hexadécimal performant pour ROMs 2–6 Mo.
//
// Toute la logique de rendu / défilement virtuel vit dans HexView
// (QAbstractScrollArea, défini dans le .cpp). Ce panneau enveloppe la vue
// d'une barre d'outils (goto, recherche hex/ASCII, groupement, base), d'un
// encart d'inspection de la sélection (u8/s8/u16/u32/ASCII) et d'une ligne de
// statut. Il expose l'API publique attendue par le reste de l'application.
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
    void updateInspector(quint32 offset);

    // Barre d'outils.
    void onGotoSubmitted();
    void doSearch(bool forward);
    void onGroupingChanged(int index);
    void onBaseChanged(int index);

    // Menu contextuel.
    void showContextMenu(const QPoint& globalPos, quint32 offset);
    void copyHex(quint32 offset);
    void copyCArray(quint32 offset);
    void copyAddress(quint32 offset);
    void fillSelection(quint32 offset);
    void pasteHex(quint32 offset);

    // Recherche : convertit la requête (hex ou ASCII) en motif d'octets.
    bool buildSearchPattern(QByteArray& out, QString& err) const;

    RomDocument* m_doc{nullptr};
    bool         m_ownsDoc{false};
    HexView*     m_view{nullptr};

    // Barre d'outils.
    QLineEdit*   m_gotoEdit{nullptr};
    QLineEdit*   m_searchEdit{nullptr};
    QComboBox*   m_searchMode{nullptr};   // Hex / ASCII
    QToolButton* m_searchPrev{nullptr};
    QToolButton* m_searchNext{nullptr};
    QComboBox*   m_grouping{nullptr};     // octet / mot / dword
    QComboBox*   m_base{nullptr};         // offset hex / dec

    // Inspecteur de sélection + statut.
    QLabel*      m_inspector{nullptr};
    QLabel*      m_status{nullptr};

    qsizetype    m_lastMatch{-1};         // dernier offset trouvé (pour next/prev)
};

} // namespace ecu_studio

#pragma once
#include <QWidget>
#include <QString>
#include <vector>

class QListWidget;
class QListWidgetItem;
class QPushButton;
class QLabel;
class QTextEdit;

namespace ecu_studio {

class RomDocument;

// Panneau « Auto-mods / Templates véhicule ».
// Liste les modifications en un clic disponibles pour l'ECU du document courant
// (autoModPatterns, autoModAddresses) ainsi que les templates véhicule
// (VehicleTemplates::listTemplatesForEcu). L'utilisateur sélectionne un template
// ou des auto-mods individuels puis applique (avec confirmation). Toute
// modification est écrite in-place dans doc->romMutable() et notifiée via
// doc->markModified(). Un journal style MppsPanel trace les changements.
class AutoModsPanel : public QWidget {
    Q_OBJECT
public:
    explicit AutoModsPanel(RomDocument* doc, QWidget* parent = nullptr);

public slots:
    void refresh();          // recharge les mods disponibles pour l'ECU courant

private slots:
    void applySelection();   // applique le template / les auto-mods cochés
    void restoreSelection(); // restaure (si octets de restauration disponibles)

private:
    // Catégorie d'une entrée listée.
    enum class Kind { Template, Pattern, Address };

    struct Entry {
        Kind    kind;
        QString id;          // id de l'auto-mod ou du template
    };

    void buildUi();
    void log(const QString& msg, bool error = false);

    // Applique une entrée donnée. Retourne true si au moins un octet a changé.
    bool applyTemplate(const QString& templateId);
    bool applyPattern(const QString& patternId);
    bool applyAddress(const QString& addressId);

    // Restaure une entrée (pattern/address) si des octets de restauration existent.
    bool restorePattern(const QString& patternId);
    bool restoreAddress(const QString& addressId);

    std::vector<Entry> selectedEntries() const;

    RomDocument* m_doc{nullptr};

    QLabel*      m_ecuLabel{nullptr};
    QListWidget* m_list{nullptr};
    QPushButton* m_applyBtn{nullptr};
    QPushButton* m_restoreBtn{nullptr};
    QPushButton* m_refreshBtn{nullptr};
    QTextEdit*   m_log{nullptr};
};

} // namespace ecu_studio

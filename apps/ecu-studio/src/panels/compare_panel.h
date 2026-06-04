#pragma once
#include <QWidget>
#include <QByteArray>
#include <QString>
#include <cstdint>

class QLabel;
class QPushButton;
class QTableWidget;

namespace ecu_studio {

class RomDocument;

// Panel de comparaison de ROMs. Chaque opérande (A et B) peut provenir de trois
// sources : la ROM courante du document, un commit git du dépôt du projet, ou un
// fichier .bin sur disque. On peut donc notamment comparer deux commits entre
// eux. Calcule ecu::diffIntervals(spanA, spanB) et liste chaque plage modifiée
// (début, fin, longueur). Un clic sur un intervalle émet gotoAddressRequested()
// pour que la fenêtre principale positionne l'éditeur hexadécimal.
class ComparePanel : public QWidget {
    Q_OBJECT
public:
    // Constructeur principal, relié au RomDocument partagé.
    explicit ComparePanel(RomDocument* doc, QWidget* parent = nullptr);
    // Surcharge de commodité (instanciation sans document, ex. main_window
    // historique) : délègue avec un document nul.
    explicit ComparePanel(QWidget* parent = nullptr);
    ~ComparePanel() override;

public slots:
    // Ouvre le sélecteur de source pour la ROM B (entrée du menu Outils
    // « Comparer ROMs »).
    void openComparison();

signals:
    // Émis quand l'utilisateur sélectionne un intervalle : adresse de début.
    void gotoAddressRequested(quint32 address);

private:
    // Source d'un opérande de comparaison.
    struct Slot {
        bool       fromDoc = false;  // true = suit la ROM courante du document
        QByteArray bytes;            // octets figés (commit/fichier) si !fromDoc
        QString    label;            // libellé de la source (nom fichier / commit)
    };

    void buildUi();
    void refresh();          // recalcule la comparaison A vs B
    void onRomLoadedChanged();
    void onIntervalActivated(int row);

    // Dialogue de choix de source (document / commit git / fichier .bin). Renvoie
    // true et remplit `slot` si l'utilisateur a validé une source.
    bool pickSource(const QString& title, Slot& slot);
    // Octets effectifs d'un slot (lit le document en direct si fromDoc).
    QByteArray slotBytes(const Slot& slot) const;
    // Libellé affiché pour un slot (« ROM A : … »).
    QString    slotText(const QString& which, const Slot& slot) const;

    RomDocument* m_doc{nullptr};
    Slot m_a;   // défaut : ROM courante du document
    Slot m_b;   // défaut : aucune source

    QLabel*       m_romALabel{nullptr};
    QLabel*       m_romBLabel{nullptr};
    QLabel*       m_summary{nullptr};
    QPushButton*  m_chooseABtn{nullptr};
    QPushButton*  m_chooseBBtn{nullptr};
    QTableWidget* m_table{nullptr};
};

} // namespace ecu_studio

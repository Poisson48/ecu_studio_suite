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

// Panel de comparaison de ROMs : compare la ROM courante du document (A) à une
// seconde ROM choisie sur disque (B) et affiche les plages d'octets modifiées.
// Calcule ecu::diffIntervals(spanA, spanB) et liste chaque intervalle (début,
// fin, longueur). Un clic sur un intervalle émet gotoAddressRequested() pour que
// la fenêtre principale positionne l'éditeur hexadécimal.
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
    // Ouvre un dialogue pour choisir la ROM B, la charge et lance la comparaison.
    void openComparison();

signals:
    // Émis quand l'utilisateur sélectionne un intervalle : adresse de début.
    void gotoAddressRequested(quint32 address);

private:
    void buildUi();
    void refresh();          // recalcule la comparaison A vs B
    void onRomLoadedChanged();
    void onIntervalActivated(int row);

    RomDocument* m_doc{nullptr};
    QByteArray   m_romB;      // seconde ROM chargée depuis le disque
    QString      m_romBName;

    QLabel*       m_romALabel{nullptr};
    QLabel*       m_romBLabel{nullptr};
    QLabel*       m_summary{nullptr};
    QPushButton*  m_chooseBtn{nullptr};
    QTableWidget* m_table{nullptr};
};

} // namespace ecu_studio

#pragma once
#include <QWidget>
#include <QString>
#include <memory>
#include "ecu/A2lParser.hpp"

class QTableWidget;
class QLineEdit;
class QLabel;
class QPushButton;

namespace ecu_studio {

class RomDocument;

// Panneau A2L/DAMOS : charge un fichier A2L, liste les CHARACTERISTIC dans un
// tableau filtrable et permet de sauter à l'adresse d'une caractéristique
// (double-clic) pour positionner l'éditeur hex / l'éditeur de maps.
class A2lPanel : public QWidget {
    Q_OBJECT
public:
    explicit A2lPanel(RomDocument* doc, QWidget* parent = nullptr);

    const ecu::A2lParser& parser() const { return m_parser; }
    bool isLoaded() const { return m_loaded; }

public slots:
    void loadA2l();                       // ouvre un dialogue de fichier
    bool loadA2lFile(const QString& path);// charge un chemin précis
    void exportA2l();                     // exporte un A2L depuis le recipe open_damos

signals:
    // Émis sur double-clic d'une ligne : le main_window positionne l'éditeur
    // hex / l'éditeur de maps sur cette adresse.
    void gotoAddressRequested(quint32 address);

    void a2lLoaded(const QString& path, int characteristicCount);

private slots:
    void applyFilter(const QString& text);
    void onCellDoubleClicked(int row, int column);

private:
    void buildUi();
    void populateTable();
    void maybeOfferAutoLoad();

    RomDocument*   m_doc{nullptr};
    ecu::A2lParser m_parser;
    bool           m_loaded{false};

    QPushButton*   m_loadBtn{nullptr};
    QPushButton*   m_exportBtn{nullptr};
    QLineEdit*     m_filterEdit{nullptr};
    QTableWidget*  m_table{nullptr};
    QLabel*        m_countLabel{nullptr};
    QLabel*        m_pathLabel{nullptr};
};

} // namespace ecu_studio

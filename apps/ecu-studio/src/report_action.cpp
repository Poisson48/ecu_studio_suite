// report_action.cpp — implémentation de MainWindow::generateReport().
//
// Isolé dans son propre TU : ReportGenerator.hpp tire MapDiffer.hpp qui définit
// ecu::Characteristic, en conflit avec la struct homonyme d'A2lParser.hpp
// incluse dans main_window.cpp (via a2l_panel.h). Ici on n'inclut PAS
// A2lParser, donc pas de double définition.
#include "main_window.h"
#include "rom_document.h"
#include "ecu/ReportGenerator.hpp"

#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>
#include <span>
#include <cstdint>

namespace ecu_studio {

void MainWindow::generateReport() {
    if (!m_doc->isLoaded()) {
        QMessageBox::information(this, tr("Rapport"), tr("Aucune ROM chargée."));
        return;
    }

    // ROM originale optionnelle — sinon on compare la ROM à elle-même.
    const QString origPath = QFileDialog::getOpenFileName(
        this, tr("ROM originale pour comparaison (optionnel — Annuler pour ignorer)"),
        {}, tr("ROM (*.bin *.ori *.mod);;Tous (*.*)"));
    QByteArray original = m_doc->rom();
    if (!origPath.isEmpty()) {
        QFile of(origPath);
        if (of.open(QIODevice::ReadOnly)) original = of.readAll();
    }

    ecu::ReportInput in;
    in.project.name    = m_doc->name();
    in.project.ecu     = m_doc->ecuId();
    in.project.romName = m_doc->name();
    in.originalBuf = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(original.constData()),
        static_cast<std::size_t>(original.size()));
    in.currentBuf = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(m_doc->rom().constData()),
        static_cast<std::size_t>(m_doc->rom().size()));

    auto html = ecu::ReportGenerator{}.generate(in);
    if (!html) {
        QMessageBox::warning(this, tr("Rapport"), tr("Génération impossible."));
        return;
    }

    const QString suggested =
        QString("rapport_%1.html").arg(m_doc->name().isEmpty() ? "ecu" : m_doc->name());
    QString out = QFileDialog::getSaveFileName(this, tr("Enregistrer le rapport"),
                                               suggested, tr("HTML (*.html)"));
    if (out.isEmpty()) return;

    QFile of(out);
    if (!of.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Rapport"), tr("Écriture impossible : %1").arg(out));
        return;
    }
    of.write(html->toUtf8());
    of.close();
    QDesktopServices::openUrl(QUrl::fromLocalFile(out));
}

} // namespace ecu_studio

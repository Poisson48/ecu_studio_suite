// project_panel_list.cpp — liste des projets, filtre, détails et menu
// contextuel du ProjectPanel. Extrait de project_panel.cpp.
#include "project_panel.h"
#include "../rom_document.h"

#include "ecu/EcuCatalog.hpp"

#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonObject>
#include <QTextEdit>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include "project_panel_internal.h"

namespace ecu_studio {

namespace {
QString humanSize(qint64 bytes) {
    if (bytes <= 0) return QObject::tr("—");
    if (bytes < 1024) return QString("%1 o").arg(bytes);
    const double kb = bytes / 1024.0;
    if (kb < 1024.0) return QString("%1 Ko").arg(kb, 0, 'f', 0);
    return QString("%1 Mo").arg(kb / 1024.0, 0, 'f', 2);
}
} // namespace

void ProjectPanel::showList() {
    m_stack->setCurrentIndex(0);
}

void ProjectPanel::showForm() {
    m_nameEdit->clear();
    m_vehicleEdit->clear();
    m_immatEdit->clear();
    m_yearEdit->clear();
    if (m_ecuCombo->count() > 0) m_ecuCombo->setCurrentIndex(0);
    m_formError->hide();
    m_formError->clear();
    m_stack->setCurrentIndex(1);
    m_nameEdit->setFocus();
}

void ProjectPanel::refreshList() {
    m_list->clear();

    auto res = m_manager->list();
    if (!res) {
        m_emptyLabel->setText(tr("Erreur lecture des projets : %1").arg(res.error()));
        m_emptyLabel->show();
        m_list->hide();
        m_openBtn->setEnabled(false);
        m_importBtn->setEnabled(false);
        return;
    }

    const QList<ecu::ProjectMeta>& projects = *res;
    if (projects.isEmpty()) {
        m_emptyLabel->setText(tr("Aucun projet — créez-en un avec « Nouveau projet »."));
        m_emptyLabel->show();
        m_list->hide();
    } else {
        m_emptyLabel->hide();
        m_list->show();
        for (const auto& p : projects) {
            QString sub = p.ecu;
            if (!p.vehicle.isEmpty()) sub += " — " + p.vehicle;
            if (!p.immat.isEmpty())   sub += " — " + p.immat;
            if (!p.year.isEmpty())    sub += " (" + p.year + ")";
            const QString rom = p.hasRom
                ? tr("ROM: %1").arg(p.romName.isEmpty() ? tr("présente") : p.romName)
                : tr("ROM: aucune");

            auto* item = new QListWidgetItem(QString("%1\n%2  •  %3").arg(p.name, sub, rom), m_list);
            item->setData(IdRole, p.id);
            item->setData(EcuRole, p.ecu);
            // Texte indexé pour la recherche.
            item->setData(Qt::UserRole + 10,
                QStringList{ p.name, p.ecu, p.vehicle, p.immat, p.year }.join(' ').toLower());
        }
        m_list->setCurrentRow(0);
    }

    applyFilter();

    const QString id = selectedProjectId();
    const bool has = !id.isEmpty();
    m_openBtn->setEnabled(has);
    m_importBtn->setEnabled(has);
    m_duplicateBtn->setEnabled(has);
    m_renameBtn->setEnabled(has);
    m_deleteBtn->setEnabled(has);
    m_addSlotBtn->setEnabled(has);
    updateDetails(id);
}

void ProjectPanel::applyFilter() {
    const QString q = m_search ? m_search->text().trimmed().toLower() : QString();
    int firstVisible = -1;
    for (int i = 0; i < m_list->count(); ++i) {
        auto* it = m_list->item(i);
        const bool match = q.isEmpty()
            || it->data(Qt::UserRole + 10).toString().contains(q);
        it->setHidden(!match);
        if (match && firstVisible < 0) firstVisible = i;
    }
    // Si la sélection courante est masquée, sélectionne le premier visible.
    auto* cur = m_list->currentItem();
    if ((!cur || cur->isHidden()) && firstVisible >= 0)
        m_list->setCurrentRow(firstVisible);
}

QString ProjectPanel::selectedProjectId() const {
    auto* item = m_list->currentItem();
    if (!item || m_list->isHidden() || item->isHidden()) return {};
    return item->data(IdRole).toString();
}

QString ProjectPanel::selectedSlotSlug() const {
    if (!m_slotTree) return {};
    auto* it = m_slotTree->currentItem();
    return it ? it->data(0, SlugRole).toString() : QString();
}

void ProjectPanel::updateDetails(const QString& id) {
    m_slotTree->clear();
    if (id.isEmpty()) {
        m_detailCard->setText(tr("Sélectionnez un projet."));
        m_addSlotBtn->setEnabled(false);
        return;
    }
    auto meta = m_manager->get(id);
    if (!meta) {
        m_detailCard->setText(tr("Projet introuvable."));
        m_addSlotBtn->setEnabled(false);
        return;
    }

    auto row = [](const QString& k, const QString& v) {
        return QString("<tr><td style='color:#7c8fa6;padding-right:10px'>%1</td>"
                       "<td style='color:#e5e7eb'>%2</td></tr>")
            .arg(k, v.isEmpty() ? QStringLiteral("—") : v.toHtmlEscaped());
    };
    const QString created = meta->createdAt.isValid()
        ? meta->createdAt.toLocalTime().toString("yyyy-MM-dd HH:mm")
        : tr("—");
    const QString romLine = meta->hasRom
        ? QString("%1 (%2)").arg(meta->romName.isEmpty() ? tr("rom.bin") : meta->romName,
                                 humanSize(meta->romSize))
        : tr("aucune");

    QString html = QString("<b style='font-size:14px;color:#ffffff'>%1</b><br/>")
                       .arg(meta->name.toHtmlEscaped());
    html += "<table cellspacing='0' cellpadding='1'>";
    html += row(tr("ECU"),       meta->ecu);
    html += row(tr("Véhicule"),  meta->vehicle);
    html += row(tr("Immat."),    meta->immat);
    html += row(tr("Année"),     meta->year);
    html += row(tr("ROM principale"), romLine);
    html += row(tr("Créé le"),   created);
    html += "</table>";
    m_detailCard->setText(html);

    // Slots ROM (catalogue + slots additionnels).
    if (meta->hasRom) {
        auto* it = new QTreeWidgetItem(m_slotTree);
        it->setText(0, tr("rom.bin (principale)"));
        it->setText(1, humanSize(meta->romSize));
        it->setText(2, meta->romImportedAt.isValid()
            ? meta->romImportedAt.toLocalTime().toString("yyyy-MM-dd HH:mm") : tr("—"));
        it->setData(0, SlugRole, QString());   // slug vide => ROM principale
    }
    for (const auto& s : m_manager->listRomSlots(id)) {
        auto* it = new QTreeWidgetItem(m_slotTree);
        it->setText(0, s.slug);
        it->setText(1, humanSize(s.size));
        it->setText(2, s.createdAt.isValid()
            ? s.createdAt.toLocalTime().toString("yyyy-MM-dd HH:mm") : tr("—"));
        it->setData(0, SlugRole, s.slug);
    }
    if (m_slotTree->topLevelItemCount() == 0) {
        auto* it = new QTreeWidgetItem(m_slotTree);
        it->setText(0, tr("(aucune ROM)"));
        it->setForeground(0, QColor(0x7c, 0x8f, 0xa6));
    }
    m_slotTree->resizeColumnToContents(0);
    m_addSlotBtn->setEnabled(true);
}

void ProjectPanel::showContextMenu(const QPoint& pos) {
    const QString id = selectedProjectId();
    if (id.isEmpty()) return;
    QMenu menu(this);
    menu.addAction(tr("Ouvrir"),       this, [this, id]() { openProjectById(id); });
    menu.addAction(tr("Importer ROM..."), this, [this, id]() { importRomFor(id); });
    menu.addAction(tr("Ajouter ROM..."),  this, [this, id]() { addRomSlotFor(id); });
    menu.addSeparator();
    menu.addAction(tr("Modifier…"),    this, [this, id]() { editProject(id); });
    menu.addAction(tr("Dupliquer"),    this, [this, id]() { duplicateProject(id); });
    menu.addAction(tr("Supprimer"),    this, [this, id]() { deleteProject(id); });
    menu.exec(m_list->mapToGlobal(pos));
}


} // namespace ecu_studio

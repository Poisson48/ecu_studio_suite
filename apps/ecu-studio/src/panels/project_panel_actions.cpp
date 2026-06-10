// project_panel_actions.cpp — création/édition/duplication/suppression de
// projets, ouverture et import de ROM du ProjectPanel. Extrait de project_panel.cpp.
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

void ProjectPanel::addRomSlotFor(const QString& id) {
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Ajouter une ROM (slot)"), {},
        tr("ROM (*.bin *.ori *.mod *.hex *.ols);;Tous (*.*)"));
    if (file.isEmpty()) return;

    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Ajouter ROM"),
                             tr("Impossible d'ouvrir le fichier : %1").arg(file));
        return;
    }
    const QByteArray data = f.readAll();
    f.close();

    auto res = m_manager->addRomSlot(id, data, QFileInfo(file).fileName());
    if (!res) {
        QMessageBox::warning(this, tr("Ajouter ROM"),
                             tr("Échec : %1").arg(res.error()));
        return;
    }
    updateDetails(id);
}

void ProjectPanel::deleteProject(const QString& id) {
    auto meta = m_manager->get(id);
    const QString name = meta ? meta->name : id;
    if (QMessageBox::question(this, tr("Supprimer le projet"),
            tr("Supprimer définitivement le projet « %1 » et toutes ses ROMs ?").arg(name),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;
    m_manager->remove(id);
    refreshList();
}

void ProjectPanel::editProject(const QString& id) {
    auto meta = m_manager->get(id);
    if (!meta) {
        QMessageBox::warning(this, tr("Modifier"), tr("Projet introuvable."));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Modifier le projet"));
    dlg.setMinimumWidth(420);

    auto* lay  = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout;

    auto* nameEdit    = new QLineEdit(meta->name, &dlg);
    auto* ecuCombo    = new QComboBox(&dlg);
    int curIdx = -1;
    for (const auto& s : ecu::listEcus()) {
        const QString eid  = QString::fromUtf8(s.id.data(), (qsizetype)s.id.size());
        const QString name = QString::fromUtf8(s.name.data(), (qsizetype)s.name.size());
        ecuCombo->addItem(name.isEmpty() ? eid : QString("%1 (%2)").arg(name, eid), eid);
        if (eid == meta->ecu) curIdx = ecuCombo->count() - 1;
    }
    if (curIdx >= 0) ecuCombo->setCurrentIndex(curIdx);

    auto* vehicleEdit = new QLineEdit(meta->vehicle, &dlg);
    auto* immatEdit   = new QLineEdit(meta->immat,   &dlg);
    auto* yearEdit    = new QLineEdit(meta->year,    &dlg);
    auto* descEdit    = new QTextEdit(&dlg);
    descEdit->setPlainText(meta->description);
    descEdit->setMaximumHeight(110);

    form->addRow(tr("Nom *"),           nameEdit);
    form->addRow(tr("ECU"),             ecuCombo);
    form->addRow(tr("Véhicule"),        vehicleEdit);
    form->addRow(tr("Immatriculation"), immatEdit);
    form->addRow(tr("Année"),           yearEdit);
    form->addRow(tr("Description"),     descEdit);
    lay->addLayout(form);

    auto* err = new QLabel(&dlg);
    err->setStyleSheet("color:#ef4444; font-size:11px;");
    err->hide();
    lay->addWidget(err);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() {
        const QString trimmed = nameEdit->text().trimmed();
        if (trimmed.isEmpty()) { err->setText(tr("Le nom est obligatoire.")); err->show(); return; }
        QJsonObject fields;
        fields["name"]        = trimmed;
        fields["ecu"]         = ecuCombo->currentData().toString();
        fields["vehicle"]     = vehicleEdit->text().trimmed();
        fields["immat"]       = immatEdit->text().trimmed();
        fields["year"]        = yearEdit->text().trimmed();
        fields["description"] = descEdit->toPlainText();
        auto res = m_manager->update(id, fields);
        if (!res) {
            err->setText(tr("Échec : %1").arg(res.error())); err->show(); return;
        }
        dlg.accept();
    });

    if (dlg.exec() == QDialog::Accepted) {
        refreshList();
        // Restaure la sélection sur le projet renommé.
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(IdRole).toString() == id) {
                m_list->setCurrentRow(i); break;
            }
        }
    }
}

void ProjectPanel::duplicateProject(const QString& id) {
    auto meta = m_manager->get(id);
    if (!meta) {
        QMessageBox::warning(this, tr("Dupliquer"), tr("Projet introuvable."));
        return;
    }

    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Dupliquer le projet"),
        tr("Nom de la copie :"), QLineEdit::Normal,
        tr("%1 (copie)").arg(meta->name), &ok);
    if (!ok || newName.trimmed().isEmpty()) return;

    ecu::CreateProjectParams params;
    params.name        = newName.trimmed();
    params.ecu         = meta->ecu;
    params.description = meta->description;
    params.vehicle     = meta->vehicle;
    params.immat       = meta->immat;
    params.year        = meta->year;

    auto created = m_manager->create(params);
    if (!created) {
        QMessageBox::warning(this, tr("Dupliquer"),
                             tr("Création échouée : %1").arg(created.error()));
        return;
    }

    // Copie la ROM principale si présente.
    if (auto src = m_manager->romPath(id); src && QFile::exists(*src)) {
        QFile f(*src);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray data = f.readAll();
            f.close();
            m_manager->importRom(created->id, data,
                                 meta->romName.isEmpty() ? tr("rom.bin") : meta->romName);
        }
    }
    // Copie les slots additionnels.
    for (const auto& s : m_manager->listRomSlots(id)) {
        if (auto sp = m_manager->romSlotPath(id, s.slug); sp && QFile::exists(*sp)) {
            QFile f(*sp);
            if (f.open(QIODevice::ReadOnly)) {
                const QByteArray data = f.readAll();
                f.close();
                m_manager->addRomSlot(created->id, data, s.slug);
            }
        }
    }

    refreshList();
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(IdRole).toString() == created->id) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

void ProjectPanel::newProject() {
    showForm();
}

void ProjectPanel::submitForm() {
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) {
        m_formError->setText(tr("Le nom du projet est obligatoire."));
        m_formError->show();
        m_nameEdit->setFocus();
        return;
    }
    if (m_ecuCombo->count() == 0 || m_ecuCombo->currentData().toString().isEmpty()) {
        m_formError->setText(tr("Sélectionnez un ECU."));
        m_formError->show();
        return;
    }

    ecu::CreateProjectParams params;
    params.name    = name;
    params.ecu     = m_ecuCombo->currentData().toString();
    params.vehicle = m_vehicleEdit->text().trimmed();
    params.immat   = m_immatEdit->text().trimmed();
    params.year    = m_yearEdit->text().trimmed();

    auto res = m_manager->create(params);
    if (!res) {
        m_formError->setText(tr("Création échouée : %1").arg(res.error()));
        m_formError->show();
        return;
    }

    showList();
    refreshList();

    // Sélectionne le projet fraîchement créé.
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(IdRole).toString() == res->id) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

void ProjectPanel::openProject() {
    const QString id = selectedProjectId();
    if (id.isEmpty()) {
        QMessageBox::information(this, tr("Ouvrir projet"),
                                 tr("Sélectionnez d'abord un projet dans la liste."));
        return;
    }
    openProjectById(id);
}

void ProjectPanel::openProjectById(const QString& id) {
    auto meta = m_manager->get(id);
    if (!meta) {
        QMessageBox::warning(this, tr("Ouvrir projet"),
                             tr("Projet introuvable."));
        return;
    }

    auto path = m_manager->romPath(id);
    if (path && QFile::exists(*path)) {
        if (m_doc) {
            // Copie de travail gérée (rom.bin du projet) — autosave autorisé ;
            // l'original reste préservé séparément en rom.original.bin.
            if (!m_doc->loadFromFile(*path, /*managed=*/true)) {
                QMessageBox::warning(this, tr("Ouvrir projet"),
                                     tr("Impossible de charger la ROM : %1").arg(*path));
                return;
            }
            m_doc->setEcuId(meta->ecu);
        }
    } else {
        QMessageBox::information(this, tr("Ouvrir projet"),
            tr("Le projet « %1 » ne contient pas encore de ROM.\n"
               "Utilisez « Importer ROM... » pour en ajouter une.").arg(meta->name));
        // On signale tout de même l'ECU associé.
    }

    emit projectOpened(meta->ecu);
}

void ProjectPanel::importRomFor(const QString& id) {
    auto meta = m_manager->get(id);
    if (!meta) {
        QMessageBox::warning(this, tr("Importer ROM"), tr("Projet introuvable."));
        return;
    }

    const QString file = QFileDialog::getOpenFileName(
        this, tr("Importer une ROM"), {},
        tr("ROM (*.bin *.ori *.mod);;Tous (*.*)"));
    if (file.isEmpty()) return;

    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Importer ROM"),
                             tr("Impossible d'ouvrir le fichier : %1").arg(file));
        return;
    }
    const QByteArray data = f.readAll();
    f.close();

    auto res = m_manager->importRom(id, data, QFileInfo(file).fileName());
    if (!res) {
        QMessageBox::warning(this, tr("Importer ROM"),
                             tr("Import échoué : %1").arg(res.error()));
        return;
    }

    refreshList();
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(IdRole).toString() == id) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

} // namespace ecu_studio

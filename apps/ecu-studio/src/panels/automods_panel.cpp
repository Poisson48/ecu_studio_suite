#include "automods_panel.h"
#include "../rom_document.h"
#include "../byte_span.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QMessageBox>
#include <QDateTime>
#include <QFont>
#include <QColor>

#include <cstdint>
#include <span>
#include <string>

#include "ecu/EcuCatalog.hpp"
#include "ecu/VehicleTemplates.hpp"
#include "ecu/RomPatcher.hpp"
#include "ecu/OpenDamos.hpp"
#include "ecu/OpenDamosRecipes.hpp"

#include <unordered_map>
#include <unordered_set>

namespace ecu_studio {

namespace {

// Rôle Qt::UserRole : Kind ; UserRole+1 : id (QString).
constexpr int kRoleKind = Qt::UserRole;
constexpr int kRoleId   = Qt::UserRole + 1;


// Libellé court du niveau de risque d'une recette open_damos.
QString riskLabel(ecu::RecipeRisk r) {
    switch (r) {
        case ecu::RecipeRisk::Low:    return AutoModsPanel::tr("risque faible");
        case ecu::RecipeRisk::Medium: return AutoModsPanel::tr("risque moyen");
        case ecu::RecipeRisk::High:   return AutoModsPanel::tr("risque \303\251lev\303\251");
    }
    return {};
}


} // namespace

AutoModsPanel::AutoModsPanel(RomDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc) {
    buildUi();

    if (m_doc) {
        connect(m_doc, &RomDocument::romLoaded, this, &AutoModsPanel::refresh);
        connect(m_doc, &RomDocument::ecuChanged, this,
                [this](const QString&) { refresh(); });
    }

    refresh();
}

void AutoModsPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(8, 8, 8, 8);

    // ── En-tête ECU ─────────────────────────────────────────────────────────
    auto* headRow = new QHBoxLayout;
    headRow->addWidget(new QLabel(tr("ECU :"), this));
    m_ecuLabel = new QLabel(this);
    m_ecuLabel->setStyleSheet("color:#22c55e; font-weight:600;");
    headRow->addWidget(m_ecuLabel);
    headRow->addStretch();
    m_refreshBtn = new QPushButton(tr("Actualiser"), this);
    headRow->addWidget(m_refreshBtn);
    root->addLayout(headRow);

    // ── Liste des mods / templates ──────────────────────────────────────────
    auto* listBox = new QGroupBox(tr("Modifications disponibles"), this);
    auto* listLay = new QVBoxLayout(listBox);
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    listLay->addWidget(m_list);

    auto* btnRow = new QHBoxLayout;
    m_applyBtn = new QPushButton(tr("Appliquer"), this);
    m_applyBtn->setObjectName("accentBtn");
    m_restoreBtn = new QPushButton(tr("Restaurer"), this);
    btnRow->addWidget(m_applyBtn);
    btnRow->addWidget(m_restoreBtn);
    btnRow->addStretch();
    listLay->addLayout(btnRow);
    root->addWidget(listBox, 1);

    // ── Journal ─────────────────────────────────────────────────────────────
    auto* logBox = new QGroupBox(tr("Journal"), this);
    auto* logLay = new QVBoxLayout(logBox);
    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    m_log->document()->setMaximumBlockCount(500);
    m_log->setFont(QFont("Monospace", 10));
    m_log->setStyleSheet("background:#111827; color:#e5e7eb; border:none;");
    logLay->addWidget(m_log);
    root->addWidget(logBox, 1);

    connect(m_refreshBtn, &QPushButton::clicked, this, &AutoModsPanel::refresh);
    connect(m_applyBtn,   &QPushButton::clicked, this, &AutoModsPanel::applySelection);
    connect(m_restoreBtn, &QPushButton::clicked, this, &AutoModsPanel::restoreSelection);
}

void AutoModsPanel::log(const QString& msg, bool error) {
    const QString time  = QDateTime::currentDateTime().toString("HH:mm:ss");
    const QString color = error ? "#ef4444" : "#7c8fa6";
    m_log->append(QString("<span style='color:%1'>%2</span> "
                          "<span style='color:#f1f5f9'>%3</span>")
                  .arg(color, time, msg.toHtmlEscaped()));
}

void AutoModsPanel::refresh() {
    m_list->clear();

    const QString ecuId = m_doc ? m_doc->ecuId() : QString();
    m_ecuLabel->setText(ecuId.isEmpty() ? tr("(aucun)") : ecuId);

    const bool loaded = m_doc && m_doc->isLoaded();
    m_applyBtn->setEnabled(loaded);
    m_restoreBtn->setEnabled(loaded);

    if (ecuId.isEmpty()) {
        log(tr("Aucun ECU sélectionné — impossible de lister les auto-mods."));
        return;
    }

    const std::string ecuStd = ecuId.toStdString();
    auto ecu = ecu::getEcu(ecuStd);
    if (!ecu) {
        log(tr("ECU « %1 » introuvable dans le catalogue.").arg(ecuId), true);
        return;
    }

    auto addItem = [this](const QString& text, Kind kind, const QString& id,
                          const QString& tooltip = QString(),
                          bool enabled = true) -> QListWidgetItem* {
        auto* it = new QListWidgetItem(text, m_list);
        it->setData(kRoleKind, static_cast<int>(kind));
        it->setData(kRoleId, id);
        if (!tooltip.isEmpty())
            it->setToolTip(tooltip);
        if (!enabled) {
            // Affiché mais non sélectionnable : Appliquer ne peut pas le viser.
            it->setFlags(it->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsEnabled);
            it->setForeground(QColor("#6b7280"));
        }
        return it;
    };

    int nbTpl = 0, nbPat = 0, nbAddr = 0, nbRecipe = 0, nbDamosMod = 0;

    // ── Templates véhicule ──────────────────────────────────────────────────
    auto templates = ecu::listTemplatesForEcu(ecuStd);
    for (const auto& t : templates) {
        QString label = QString("[Template] %1")
                            .arg(QString::fromStdString(t.name));
        if (!t.vehicles.empty())
            label += QString("  (%1)").arg(QString::fromStdString(t.vehicles));
        if (t.hasStage1)
            label += tr("  · stage1");
        if (t.autoModCount)
            label += tr("  · %1 auto-mod(s)").arg(static_cast<qulonglong>(t.autoModCount));
        addItem(label, Kind::Template, QString::fromStdString(t.id));
        ++nbTpl;
    }

    // ── Auto-mods : motifs (search/replace) ─────────────────────────────────
    if (ecu->autoModPatterns) {
        for (const auto& p : *ecu->autoModPatterns) {
            QString id = QString::fromStdString(std::string(p.id));
            QString label = QString("[Pattern] %1  (%2 octet(s))")
                                .arg(id)
                                .arg(static_cast<qulonglong>(p.search.size()));
            addItem(label, Kind::Pattern, id);
            ++nbPat;
        }
    }

    // ── Auto-mods : écriture à une adresse ──────────────────────────────────
    if (ecu->autoModAddresses) {
        for (const auto& a : *ecu->autoModAddresses) {
            QString id = QString::fromStdString(std::string(a.id));
            QString label = QString("[Address] %1  @ 0x%2")
                                .arg(id)
                                .arg(a.address, 0, 16);
            if (!a.note.empty())
                label += QString("  — %1").arg(QString::fromStdString(std::string(a.note)));
            addItem(label, Kind::Address, id);
            ++nbAddr;
        }
    }

    // ── Recettes open_damos (OpenDamosRecipes) ──────────────────────────────
    // Les recettes ciblent des caractéristiques par NOM (ex. VSSCD_vMax_C). On
    // résout ces noms en adresses via la relocalisation open_damos de l'ECU.
    // Une recette est applicable si la recette open_damos charge ET qu'au moins
    // une de ses opérations cible une caractéristique relocalisée de façon sûre.
    {
        const auto recipes = ecu::listRecipes();
        if (!recipes.empty()) {
            // Charge + relocalise une seule fois pour évaluer la disponibilité.
            std::unordered_set<std::string> resolvable;  // noms relocalisés sûrs
            QString loadErr;
            auto damos = ecu::OpenDamos::loadRecipe(ecuId);
            if (!damos) {
                loadErr = QString::fromStdString(damos.error());
            } else if (m_doc->isLoaded()) {
                ecu::OpenDamos od;
                od.setRecipe(std::move(*damos));
                const QByteArray& rom = m_doc->rom();
                auto relocated = od.relocate(QByteArrayView(rom.constData(), rom.size()));
                for (const auto& r : relocated) {
                    const bool safe =
                        r.addressSource != ecu::AddressSource::DefaultFallback
                        && r.score != 0.0;
                    if (safe) resolvable.insert(r.name);
                }
            }

            // Sépare visuellement la section.
            auto* sep = new QListWidgetItem(tr("──  Recettes open_damos  ──"), m_list);
            sep->setFlags(Qt::NoItemFlags);
            sep->setForeground(QColor("#9ca3af"));

            for (const auto& s : recipes) {
                // Une recette n'est applicable que si la recette open_damos a
                // chargé ET qu'au moins une op résout sur cette ROM.
                bool applicable = loadErr.isEmpty();
                QString unavailable;
                if (!applicable) {
                    unavailable = loadErr;
                } else if (const ecu::Recipe* full = ecu::getRecipe(s.id)) {
                    bool anyResolvable = false;
                    for (const auto& op : full->ops)
                        if (resolvable.count(op.entry)) { anyResolvable = true; break; }
                    if (!anyResolvable) {
                        applicable = false;
                        unavailable = tr("aucune caract\303\251ristique relocalisable "
                                         "sur cette ROM");
                    }
                }

                QString label = QString("[Recipe] %1  \302\267 %2  \302\267 %3")
                                    .arg(QString::fromStdString(s.name),
                                         QString::fromStdString(s.category),
                                         riskLabel(s.risk));
                label += tr("  (%1 op.)").arg(s.opsCount);
                if (!applicable)
                    label += tr("  \342\200\224 indisponible");

                QString tip = QString("%1\n\n%2\n%3")
                                  .arg(QString::fromStdString(s.name),
                                       QString::fromStdString(s.category),
                                       QString::fromStdString(s.description));
                if (!applicable && !unavailable.isEmpty())
                    tip += tr("\n\nIndisponible : %1").arg(unavailable);

                addItem(label, Kind::Recipe, QString::fromStdString(s.id), tip,
                        applicable);
                if (!applicable)
                    log(tr("[Recipe %1] indisponible : %2")
                            .arg(QString::fromStdString(s.id), unavailable));
                ++nbRecipe;
            }
        }
    }

    // ── Auto-mods embarqués dans open_damos.json (versionnés avec le recipe) ──
    // Chargés à la volée — ne nécessitent pas de relocalisation puisqu'ils ciblent
    // soit un pattern (search/replace) soit une adresse fixe.
    {
        auto damos = ecu::OpenDamos::loadRecipe(ecuId);
        if (damos && !damos->autoMods.empty()) {
            auto* sep = new QListWidgetItem(tr("──  Auto-mods (open_damos.json)  ──"),
                                            m_list);
            sep->setFlags(Qt::NoItemFlags);
            sep->setForeground(QColor("#9ca3af"));
            for (const auto& a : damos->autoMods) {
                const QString id = QString::fromStdString(a.id);
                QString label;
                if (a.type == ecu::DamosAutoModType::Pattern) {
                    label = QString("[Damos·Pattern] %1  (%2 octet(s))")
                                .arg(id).arg(a.search.size());
                } else if (a.type == ecu::DamosAutoModType::Address && a.address) {
                    label = QString("[Damos·Address] %1  @ 0x%2  (%3 octet(s))")
                                .arg(id).arg(*a.address, 0, 16).arg(a.replace.size());
                } else {
                    label = QString("[Damos·?] %1").arg(id);
                }
                if (!a.description.empty())
                    label += QString("  — %1").arg(QString::fromStdString(a.description));
                QString tip;
                if (!a.description.empty()) tip += QString::fromStdString(a.description);
                if (!a.note.empty()) { if (!tip.isEmpty()) tip += "\n"; tip += QString::fromStdString(a.note); }
                addItem(label, Kind::DamosAutoMod, id, tip);
                ++nbDamosMod;
            }
        }
    }

    log(tr("ECU %1 : %2 template(s), %3 pattern(s), %4 adresse(s), %5 recette(s), "
           "%6 damos-auto-mod(s).")
            .arg(ecuId).arg(nbTpl).arg(nbPat).arg(nbAddr).arg(nbRecipe).arg(nbDamosMod));

    if (m_list->count() == 0)
        log(tr("Aucune modification disponible pour cet ECU."));
}

std::vector<AutoModsPanel::Entry> AutoModsPanel::selectedEntries() const {
    std::vector<Entry> out;
    for (auto* it : m_list->selectedItems()) {
        Entry e;
        e.kind = static_cast<Kind>(it->data(kRoleKind).toInt());
        e.id   = it->data(kRoleId).toString();
        out.push_back(e);
    }
    return out;
}

void AutoModsPanel::applySelection() {
    if (!m_doc || !m_doc->isLoaded()) {
        log(tr("Aucune ROM chargée."), true);
        return;
    }

    auto entries = selectedEntries();
    if (entries.empty()) {
        log(tr("Sélectionnez au moins un élément à appliquer."), true);
        return;
    }

    QStringList names;
    for (const auto& e : entries) names << e.id;

    if (QMessageBox::question(
            this, tr("Confirmer l'application"),
            tr("Appliquer %1 modification(s) à la ROM ?\n\n%2\n\n"
               "La ROM sera modifiée en mémoire.")
                .arg(static_cast<qulonglong>(entries.size()))
                .arg(names.join("\n")),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    bool anyChange = false;
    for (const auto& e : entries) {
        bool changed = false;
        switch (e.kind) {
            case Kind::Template:     changed = applyTemplate(e.id);     break;
            case Kind::Pattern:      changed = applyPattern(e.id);      break;
            case Kind::Address:      changed = applyAddress(e.id);      break;
            case Kind::Recipe:       changed = applyRecipe(e.id);       break;
            case Kind::DamosAutoMod: changed = applyDamosAutoMod(e.id); break;
        }
        anyChange = anyChange || changed;
    }

    if (anyChange) {
        m_doc->markModified();
        log(tr("[OK] Modifications appliquées et marquées."));
    } else {
        log(tr("Aucun octet modifié."));
    }
}

void AutoModsPanel::restoreSelection() {
    if (!m_doc || !m_doc->isLoaded()) {
        log(tr("Aucune ROM chargée."), true);
        return;
    }

    auto entries = selectedEntries();
    if (entries.empty()) {
        log(tr("Sélectionnez au moins un élément à restaurer."), true);
        return;
    }

    if (QMessageBox::question(
            this, tr("Confirmer la restauration"),
            tr("Restaurer les octets d'origine pour %1 élément(s) ?")
                .arg(static_cast<qulonglong>(entries.size())),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    bool anyChange = false;
    for (const auto& e : entries) {
        bool changed = false;
        switch (e.kind) {
            case Kind::Pattern:      changed = restorePattern(e.id);      break;
            case Kind::Address:      changed = restoreAddress(e.id);      break;
            case Kind::DamosAutoMod: changed = restoreDamosAutoMod(e.id); break;
            case Kind::Template:
                log(tr("Restauration non disponible pour le template « %1 ».")
                        .arg(e.id), true);
                break;
            case Kind::Recipe:
                log(tr("Restauration non disponible pour la recette « %1 ».")
                        .arg(e.id), true);
                break;
        }
        anyChange = anyChange || changed;
    }

    if (anyChange) {
        m_doc->markModified();
        log(tr("[OK] Restauration appliquée."));
    } else {
        log(tr("Aucun octet restauré."));
    }
}


} // namespace ecu_studio

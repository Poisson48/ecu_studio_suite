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

QString bytesToHex(std::span<const uint8_t> b) {
    QString s;
    s.reserve(static_cast<int>(b.size()) * 3);
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (i) s += ' ';
        s += QString("%1").arg(b[i], 2, 16, QChar('0')).toUpper();
    }
    return s;
}

// Libellé court du niveau de risque d'une recette open_damos.
QString riskLabel(ecu::RecipeRisk r) {
    switch (r) {
        case ecu::RecipeRisk::Low:    return AutoModsPanel::tr("risque faible");
        case ecu::RecipeRisk::Medium: return AutoModsPanel::tr("risque moyen");
        case ecu::RecipeRisk::High:   return AutoModsPanel::tr("risque \303\251lev\303\251");
    }
    return {};
}

// Recherche d'un motif d'octets dans la ROM. Retourne l'offset ou -1.
qsizetype findPattern(const QByteArray& rom, std::span<const uint8_t> needle) {
    if (needle.empty() || rom.size() < static_cast<qsizetype>(needle.size()))
        return -1;
    QByteArray pat(reinterpret_cast<const char*>(needle.data()),
                   static_cast<qsizetype>(needle.size()));
    return rom.indexOf(pat, 0);
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

    int nbTpl = 0, nbPat = 0, nbAddr = 0, nbRecipe = 0;

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

    log(tr("ECU %1 : %2 template(s), %3 pattern(s), %4 adresse(s), %5 recette(s).")
            .arg(ecuId).arg(nbTpl).arg(nbPat).arg(nbAddr).arg(nbRecipe));

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
            case Kind::Template: changed = applyTemplate(e.id); break;
            case Kind::Pattern:  changed = applyPattern(e.id);  break;
            case Kind::Address:  changed = applyAddress(e.id);  break;
            case Kind::Recipe:   changed = applyRecipe(e.id);   break;
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
            case Kind::Pattern: changed = restorePattern(e.id); break;
            case Kind::Address: changed = restoreAddress(e.id); break;
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

bool AutoModsPanel::applyPattern(const QString& patternId) {
    auto ecu = ecu::getEcu(m_doc->ecuId().toStdString());
    if (!ecu || !ecu->autoModPatterns) return false;

    const std::string id = patternId.toStdString();
    for (const auto& p : *ecu->autoModPatterns) {
        if (std::string(p.id) != id) continue;

        QByteArray& rom = m_doc->romMutable();
        qsizetype off = findPattern(rom, p.search);
        if (off < 0) {
            log(tr("[Pattern %1] motif introuvable dans la ROM.").arg(patternId), true);
            return false;
        }
        if (p.replace.size() != p.search.size()) {
            // Tailles différentes : on remplace octet par octet sur la longueur
            // du motif de remplacement (les signatures du catalogue sont de
            // taille égale, mais on reste défensif).
            if (off + static_cast<qsizetype>(p.replace.size()) > rom.size()) {
                log(tr("[Pattern %1] remplacement hors limites.").arg(patternId), true);
                return false;
            }
        }
        for (std::size_t i = 0; i < p.replace.size(); ++i)
            rom[off + static_cast<qsizetype>(i)] =
                static_cast<char>(p.replace[i]);

        log(tr("[Pattern %1] appliqué @ 0x%2 : %3 → %4")
                .arg(patternId)
                .arg(static_cast<qulonglong>(off), 0, 16)
                .arg(bytesToHex(p.search))
                .arg(bytesToHex(p.replace)));
        return true;
    }
    log(tr("[Pattern %1] introuvable dans le catalogue.").arg(patternId), true);
    return false;
}

bool AutoModsPanel::restorePattern(const QString& patternId) {
    auto ecu = ecu::getEcu(m_doc->ecuId().toStdString());
    if (!ecu || !ecu->autoModPatterns) return false;

    const std::string id = patternId.toStdString();
    for (const auto& p : *ecu->autoModPatterns) {
        if (std::string(p.id) != id) continue;

        if (p.restore.empty()) {
            log(tr("[Pattern %1] pas d'octets de restauration.").arg(patternId), true);
            return false;
        }
        // On retrouve la signature « replace » et on remet « restore ».
        QByteArray& rom = m_doc->romMutable();
        qsizetype off = findPattern(rom, p.replace);
        if (off < 0) {
            log(tr("[Pattern %1] motif modifié introuvable (déjà restauré ?).")
                    .arg(patternId), true);
            return false;
        }
        for (std::size_t i = 0; i < p.restore.size(); ++i)
            rom[off + static_cast<qsizetype>(i)] =
                static_cast<char>(p.restore[i]);

        log(tr("[Pattern %1] restauré @ 0x%2")
                .arg(patternId).arg(static_cast<qulonglong>(off), 0, 16));
        return true;
    }
    return false;
}

bool AutoModsPanel::applyAddress(const QString& addressId) {
    auto ecu = ecu::getEcu(m_doc->ecuId().toStdString());
    if (!ecu || !ecu->autoModAddresses) return false;

    const std::string id = addressId.toStdString();
    for (const auto& a : *ecu->autoModAddresses) {
        if (std::string(a.id) != id) continue;

        QByteArray& rom = m_doc->romMutable();
        const qsizetype addr = static_cast<qsizetype>(a.address);
        if (addr < 0 || addr + static_cast<qsizetype>(a.bytes.size()) > rom.size()) {
            log(tr("[Address %1] adresse 0x%2 hors limites.")
                    .arg(addressId).arg(a.address, 0, 16), true);
            return false;
        }
        for (std::size_t i = 0; i < a.bytes.size(); ++i)
            rom[addr + static_cast<qsizetype>(i)] =
                static_cast<char>(a.bytes[i]);

        log(tr("[Address %1] @ 0x%2 ← %3")
                .arg(addressId)
                .arg(a.address, 0, 16)
                .arg(bytesToHex(a.bytes)));
        return true;
    }
    log(tr("[Address %1] introuvable dans le catalogue.").arg(addressId), true);
    return false;
}

bool AutoModsPanel::restoreAddress(const QString& addressId) {
    auto ecu = ecu::getEcu(m_doc->ecuId().toStdString());
    if (!ecu || !ecu->autoModAddresses) return false;

    const std::string id = addressId.toStdString();
    for (const auto& a : *ecu->autoModAddresses) {
        if (std::string(a.id) != id) continue;

        if (!a.restore) {
            log(tr("[Address %1] pas d'octets de restauration.").arg(addressId), true);
            return false;
        }
        const auto& restoreBytes = *a.restore;
        QByteArray& rom = m_doc->romMutable();
        const qsizetype addr = static_cast<qsizetype>(a.address);
        if (addr < 0 || addr + static_cast<qsizetype>(restoreBytes.size()) > rom.size()) {
            log(tr("[Address %1] adresse 0x%2 hors limites.")
                    .arg(addressId).arg(a.address, 0, 16), true);
            return false;
        }
        for (std::size_t i = 0; i < restoreBytes.size(); ++i)
            rom[addr + static_cast<qsizetype>(i)] =
                static_cast<char>(restoreBytes[i]);

        log(tr("[Address %1] restauré @ 0x%2")
                .arg(addressId).arg(a.address, 0, 16));
        return true;
    }
    return false;
}

bool AutoModsPanel::applyTemplate(const QString& templateId) {
    auto tpl = ecu::getTemplate(templateId.toStdString());
    if (!tpl) {
        log(tr("[Template %1] introuvable.").arg(templateId), true);
        return false;
    }

    auto ecu = ecu::getEcu(m_doc->ecuId().toStdString());
    if (!ecu) {
        log(tr("[Template %1] ECU courant introuvable au catalogue.")
                .arg(templateId), true);
        return false;
    }

    bool anyChange = false;

    // ── Stage 1 : appliquer un pourcentage à chaque map nommée ──────────────
    if (tpl->stage1) {
        if (!ecu->stage1Maps) {
            log(tr("[Template %1] cet ECU n'a pas de maps stage1.")
                    .arg(templateId), true);
        } else {
            for (const auto& [mapName, pct] : tpl->stage1->pcts) {
                // Résoudre l'adresse depuis le catalogue stage1Maps par nom.
                bool found = false;
                for (const auto& m : *ecu->stage1Maps) {
                    if (std::string(m.name) != mapName) continue;
                    found = true;

                    QByteArray& rom = m_doc->romMutable();
                    auto romSpan = mutByteSpan(rom);
                    auto res = ecu::applyPctToMap(romSpan, m.address,
                                                  static_cast<double>(pct));
                    if (!res) {
                        log(tr("[Template %1] map « %2 » : %3")
                                .arg(templateId)
                                .arg(QString::fromStdString(mapName))
                                .arg(QString::fromStdString(res.error())), true);
                    } else {
                        const std::size_t n = res->size();
                        if (n) anyChange = true;
                        log(tr("[Template %1] map « %2 » @ 0x%3 : %4%5%% "
                               "(%6 cellule(s))")
                                .arg(templateId)
                                .arg(QString::fromStdString(mapName))
                                .arg(m.address, 0, 16)
                                .arg(pct >= 0 ? "+" : "")
                                .arg(pct)
                                .arg(static_cast<qulonglong>(n)));
                    }
                    break;
                }
                if (!found)
                    log(tr("[Template %1] map « %2 » absente du catalogue stage1.")
                            .arg(templateId)
                            .arg(QString::fromStdString(mapName)), true);
            }
        }
    }

    // ── Auto-mods référencés par le template ────────────────────────────────
    for (const auto& modId : tpl->autoMods) {
        const QString qid = QString::fromStdString(modId);
        bool changed = false;

        // L'auto-mod peut être un pattern ou une adresse : on essaie les deux.
        bool isPattern = false, isAddress = false;
        if (ecu->autoModPatterns)
            for (const auto& p : *ecu->autoModPatterns)
                if (std::string(p.id) == modId) { isPattern = true; break; }
        if (ecu->autoModAddresses)
            for (const auto& a : *ecu->autoModAddresses)
                if (std::string(a.id) == modId) { isAddress = true; break; }

        if (isPattern)      changed = applyPattern(qid);
        else if (isAddress) changed = applyAddress(qid);
        else
            log(tr("[Template %1] auto-mod « %2 » introuvable pour cet ECU.")
                    .arg(templateId).arg(qid), true);

        anyChange = anyChange || changed;
    }

    if (tpl->autoMods.empty() && !tpl->stage1)
        log(tr("[Template %1] aucun changement défini.").arg(templateId));

    return anyChange;
}

bool AutoModsPanel::applyRecipe(const QString& recipeId) {
    const ecu::Recipe* recipe = ecu::getRecipe(recipeId.toStdString());
    if (!recipe) {
        log(tr("[Recipe %1] introuvable dans la bibliothèque open_damos.")
                .arg(recipeId), true);
        return false;
    }

    QByteArray& rom = m_doc->romMutable();
    auto romSpan = mutByteSpan(rom);

    // applyRecipe() charge l'open_damos de l'ECU, relocalise chaque
    // caractéristique nommée → adresse, puis applique chaque opération in-place.
    auto res = ecu::applyRecipe(*recipe, romSpan, m_doc->ecuId());
    if (!res) {
        log(tr("[Recipe %1] échec : %2")
                .arg(recipeId, QString::fromStdString(res.error())), true);
        return false;
    }

    bool anyChange = false;
    for (const ecu::OpResult& op : res->operations) {
        const QString entry = QString::fromStdString(op.entry);

        if (op.error) {
            // Adresse non résolue ou opération non supportée : avertissement clair.
            log(tr("[Recipe %1] « %2 » ignorée : %3")
                    .arg(recipeId, entry, QString::fromStdString(*op.error)), true);
            continue;
        }

        const QString method = QString::fromStdString(op.method);
        QString addrTxt = op.address
            ? QString("0x%1").arg(static_cast<qulonglong>(*op.address), 0, 16)
            : tr("(adresse inconnue)");
        if (!op.addressSource.empty())
            addrTxt += QString(" [%1]").arg(QString::fromStdString(op.addressSource));

        QString detail;
        if (method == "setPhys" || method == "setMapAll") {
            detail = tr("%1 = %2 (raw %3)")
                         .arg(method)
                         .arg(op.physValue ? *op.physValue : 0.0)
                         .arg(op.rawValue ? *op.rawValue : 0);
            if (op.cellsChanged)
                detail += tr(", %1 cellule(s)").arg(*op.cellsChanged);
            if (op.prevRaw && op.rawValue)
                detail += tr(" (avant raw %1)").arg(*op.prevRaw);
        } else if (method == "setRaw") {
            detail = tr("setRaw = %1").arg(op.rawValue ? *op.rawValue : 0);
            if (op.prevRaw)
                detail += tr(" (avant raw %1)").arg(*op.prevRaw);
        } else if (method == "addPct") {
            detail = tr("addPct %1%2%% (%3 cellule(s))")
                         .arg(op.pct.value_or(0.0) >= 0 ? "+" : "")
                         .arg(op.pct.value_or(0.0))
                         .arg(op.cellsChanged ? *op.cellsChanged : 0);
        } else {
            detail = method;
        }

        log(tr("[Recipe %1] « %2 » @ %3 : %4")
                .arg(recipeId, entry, addrTxt, detail));
        anyChange = true;
    }

    if (res->bytesChanged > 0)
        log(tr("[Recipe %1] %2 octet(s) modifié(s).")
                .arg(recipeId).arg(res->bytesChanged));
    else if (anyChange)
        log(tr("[Recipe %1] opérations appliquées (aucun octet net modifié).")
                .arg(recipeId));

    // anyChange = au moins une op a réussi (≈ res->ok), mais on signale le vrai
    // changement octet pour markModified() côté appelant.
    return res->bytesChanged > 0;
}

} // namespace ecu_studio

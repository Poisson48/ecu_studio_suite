// automods_panel_ops.cpp — primitives d'application/restauration des auto-mods
// (pattern, address, template, recette open_damos) du AutoModsPanel.
// Extrait de automods_panel.cpp.
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

QString bytesToHex(std::span<const uint8_t> b) {
    QString s;
    s.reserve(static_cast<int>(b.size()) * 3);
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (i) s += ' ';
        s += QString("%1").arg(b[i], 2, 16, QChar('0')).toUpper();
    }
    return s;
}

// Recherche d'un motif d'octets dans la ROM. Retourne l'offset ou -1.
qsizetype findPattern(const QByteArray& rom, std::span<const uint8_t> needle) {
    if (needle.empty() || rom.size() < static_cast<qsizetype>(needle.size()))
        return -1;
    QByteArray pat(reinterpret_cast<const char*>(needle.data()),
                   static_cast<qsizetype>(needle.size()));
    return rom.indexOf(pat, 0);
}

// Résout l'adresse réelle d'un auto-mod « adresse » sur cette ROM.
//
// Les AutoModAddress du catalogue portent l'adresse relevée sur ori.BIN. Sur un
// autre firmware de la même famille (ex. SW 1037383736), le scalaire visé est à
// un offset différent : écrire à l'adresse figée corrompt des octets voisins.
// On relocalise donc via open_damos en reliant l'auto-mod à la caractéristique
// VALUE qui partage la même defaultAddress (ex. egr_off @0x1C41B8 ==
// AirCtl_nMin_C). Retourne l'adresse relocalisée si — et seulement si — la
// relocalisation est fiable (ancre/empreinte, score > 0).
std::optional<std::pair<std::size_t, QString>>
relocatedAddressFor(const QString& ecuId, const QByteArray& rom,
                    std::size_t hardcodedAddr) {
    auto recipe = ecu::OpenDamos::loadRecipe(ecuId);
    if (!recipe) return std::nullopt;

    ecu::OpenDamos od;
    od.setRecipe(std::move(*recipe));
    for (const ecu::RelocResult& r : od.relocate(rom)) {
        if (r.type != ecu::DamosType::Value) continue;
        if (r.defaultAddress != hardcodedAddr) continue;
        // Garde identique à applyRecipe : on refuse une relocalisation non fiable.
        if (r.addressSource == ecu::AddressSource::DefaultFallback || r.score == 0.0)
            return std::nullopt;
        const QString src = r.addressSource == ecu::AddressSource::Anchor
                                ? QStringLiteral("anchor")
                                : QStringLiteral("fingerprint");
        return std::make_pair(r.address, src);
    }
    return std::nullopt;
}

} // namespace

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

        // 1) Tenter la relocalisation par open_damos (adresse figée = ori.BIN).
        qsizetype addr = static_cast<qsizetype>(a.address);
        QString addrSrc = QStringLiteral("figée");
        bool relocated = false;
        if (auto reloc = relocatedAddressFor(m_doc->ecuId(), rom, a.address)) {
            addr    = static_cast<qsizetype>(reloc->first);
            addrSrc = reloc->second;
            relocated = true;
        }

        if (addr < 0 || addr + static_cast<qsizetype>(a.bytes.size()) > rom.size()) {
            log(tr("[Address %1] adresse 0x%2 hors limites.")
                    .arg(addressId).arg(addr, 0, 16), true);
            return false;
        }

        // 2) Fallback non relocalisé : garde de sécurité. On n'écrit QUE si les
        //    octets courants correspondent au stock (a.restore) ou sont déjà à la
        //    cible (a.bytes). Sinon on tape peut-être à côté sur cette ROM → skip.
        if (!relocated && a.restore) {
            const auto& stock = *a.restore;
            bool matchesStock = stock.size() == a.bytes.size();
            bool matchesTarget = true;
            for (std::size_t i = 0; i < a.bytes.size(); ++i) {
                const auto cur = static_cast<uint8_t>(rom[addr + static_cast<qsizetype>(i)]);
                if (i >= stock.size() || cur != stock[i]) matchesStock = false;
                if (cur != a.bytes[i]) matchesTarget = false;
            }
            if (!matchesStock && !matchesTarget) {
                log(tr("[Address %1] @ 0x%2 [figée] : octets courants inattendus "
                       "(ni stock ni cible) — non relocalisable sur cette ROM, "
                       "ignoré par sécurité.")
                        .arg(addressId).arg(addr, 0, 16), true);
                return false;
            }
        }

        for (std::size_t i = 0; i < a.bytes.size(); ++i)
            rom[addr + static_cast<qsizetype>(i)] =
                static_cast<char>(a.bytes[i]);

        log(tr("[Address %1] @ 0x%2 [%3] ← %4")
                .arg(addressId)
                .arg(addr, 0, 16)
                .arg(addrSrc)
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

        // Même relocalisation qu'à l'application, pour restaurer au bon endroit.
        qsizetype addr = static_cast<qsizetype>(a.address);
        if (auto reloc = relocatedAddressFor(m_doc->ecuId(), rom, a.address))
            addr = static_cast<qsizetype>(reloc->first);

        if (addr < 0 || addr + static_cast<qsizetype>(restoreBytes.size()) > rom.size()) {
            log(tr("[Address %1] adresse 0x%2 hors limites.")
                    .arg(addressId).arg(addr, 0, 16), true);
            return false;
        }
        for (std::size_t i = 0; i < restoreBytes.size(); ++i)
            rom[addr + static_cast<qsizetype>(i)] =
                static_cast<char>(restoreBytes[i]);

        log(tr("[Address %1] restauré @ 0x%2")
                .arg(addressId).arg(addr, 0, 16));
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
    //
    // IMPORTANT : les adresses du catalogue (ecu->stage1Maps) sont relevées sur
    // la ROM de référence ori.BIN. Sur un autre firmware de la même famille
    // (ex. SW 1037383736 Berlingo 1.6 HDi 75), les mêmes maps existent à des
    // offsets DIFFÉRENTS : écrire à l'adresse codée en dur tombe sur des octets
    // qui ne sont pas la carto → aucun gain, voire corruption. On relocalise
    // donc chaque map par empreinte d'axes via open_damos (même mécanisme que
    // applyRecipe), et on saute toute map non relocalisée de façon fiable.
    if (tpl->stage1) {
        auto damosRecipe = ecu::OpenDamos::loadRecipe(m_doc->ecuId());
        if (!damosRecipe) {
            log(tr("[Template %1] open_damos indisponible (%2) — Stage 1 annulé "
                   "pour ne pas écrire à des adresses non vérifiées.")
                    .arg(templateId,
                         QString::fromStdString(damosRecipe.error())), true);
        } else {
            ecu::OpenDamos od;
            od.setRecipe(std::move(*damosRecipe));

            // Relocalisation sur la ROM propre (les empreintes portent sur les
            // axes, jamais modifiés par le Stage 1 → l'ordre importe peu).
            std::unordered_map<std::string, ecu::RelocResult> relocByName;
            {
                const QByteArray& romC = m_doc->romMutable();
                for (auto& r : od.relocate(romC))
                    relocByName.emplace(r.name, std::move(r));
            }

            for (const auto& [mapName, pct] : tpl->stage1->pcts) {
                auto it = relocByName.find(mapName);
                if (it == relocByName.end()) {
                    log(tr("[Template %1] map « %2 » absente d'open_damos — ignorée.")
                            .arg(templateId, QString::fromStdString(mapName)), true);
                    continue;
                }
                const ecu::RelocResult& rel = it->second;

                // Même garde de sécurité que applyRecipe : on refuse d'écrire si
                // la map n'a pas été relocalisée avec confiance.
                if (rel.addressSource == ecu::AddressSource::DefaultFallback ||
                    rel.score == 0.0) {
                    log(tr("[Template %1] map « %2 » non relocalisée sur cette ROM "
                           "(%3) — ignorée pour sécurité.")
                            .arg(templateId, QString::fromStdString(mapName),
                                 QString::fromStdString(
                                     rel.warning.value_or("empreinte introuvable"))),
                        true);
                    continue;
                }

                QByteArray& rom = m_doc->romMutable();
                auto romSpan = mutByteSpan(rom);
                auto res = ecu::applyPctToMap(romSpan, rel.address,
                                              static_cast<double>(pct));
                if (!res) {
                    log(tr("[Template %1] map « %2 » @ 0x%3 : %4")
                            .arg(templateId, QString::fromStdString(mapName))
                            .arg(static_cast<qulonglong>(rel.address), 0, 16)
                            .arg(QString::fromStdString(res.error())), true);
                    continue;
                }
                const std::size_t n = res->size();
                if (n) anyChange = true;
                log(tr("[Template %1] map « %2 » @ 0x%3 [reloc] : %4%5%% "
                       "(%6 cellule(s))")
                        .arg(templateId, QString::fromStdString(mapName))
                        .arg(static_cast<qulonglong>(rel.address), 0, 16)
                        .arg(pct >= 0 ? "+" : "")
                        .arg(pct)
                        .arg(static_cast<qulonglong>(n)));
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

// ── Auto-mods embarqués dans open_damos.json ──────────────────────────────────

namespace {

// Localise un auto-mod par id dans le recipe open_damos courant.
const ecu::DamosAutoMod*
findDamosAutoMod(const ecu::DamosRecipe& r, const QString& id) {
    const std::string needle = id.toStdString();
    for (const auto& a : r.autoMods)
        if (a.id == needle) return &a;
    return nullptr;
}

// Écrit `bytes` à `off` dans `rom`. Renvoie true si au moins un octet a changé.
bool writeBytes(QByteArray& rom, qsizetype off,
                const std::vector<std::uint8_t>& bytes) {
    if (off < 0 || off + qsizetype(bytes.size()) > rom.size()) return false;
    bool changed = false;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const char c = static_cast<char>(bytes[i]);
        if (rom[off + qsizetype(i)] != c) {
            rom[off + qsizetype(i)] = c;
            changed = true;
        }
    }
    return changed;
}

} // namespace

bool AutoModsPanel::applyDamosAutoMod(const QString& autoModId) {
    auto recipe = ecu::OpenDamos::loadRecipe(m_doc->ecuId());
    if (!recipe) {
        log(tr("[Damos %1] recipe introuvable : %2")
                .arg(autoModId, QString::fromStdString(recipe.error())), true);
        return false;
    }
    const ecu::DamosAutoMod* a = findDamosAutoMod(*recipe, autoModId);
    if (!a) {
        log(tr("[Damos %1] auto-mod introuvable dans le recipe.").arg(autoModId), true);
        return false;
    }

    QByteArray& rom = m_doc->romMutable();
    if (a->type == ecu::DamosAutoModType::Pattern) {
        if (a->search.empty() || a->replace.empty()) {
            log(tr("[Damos %1] pattern incomplet (search/replace vide).").arg(autoModId), true);
            return false;
        }
        if (a->search.size() != a->replace.size()) {
            log(tr("[Damos %1] tailles search (%2) et replace (%3) différentes.")
                    .arg(autoModId).arg(a->search.size()).arg(a->replace.size()), true);
            return false;
        }
        std::span<const uint8_t> needle(a->search.data(), a->search.size());
        const qsizetype off = findPattern(rom, needle);
        if (off < 0) {
            log(tr("[Damos %1] motif introuvable dans la ROM.").arg(autoModId), true);
            return false;
        }
        const bool changed = writeBytes(rom, off, a->replace);
        log(tr("[Damos %1] pattern @ 0x%2 → %3 octet(s) %4")
                .arg(autoModId).arg(off, 0, 16).arg(a->replace.size())
                .arg(changed ? tr("modifiés") : tr("déjà à la cible")));
        return changed;
    }
    if (a->type == ecu::DamosAutoModType::Address) {
        if (!a->address || a->replace.empty()) {
            log(tr("[Damos %1] address incomplète (address/bytes manquants).")
                    .arg(autoModId), true);
            return false;
        }
        const qsizetype off = static_cast<qsizetype>(*a->address);
        const bool changed = writeBytes(rom, off, a->replace);
        if (!changed && off + qsizetype(a->replace.size()) > rom.size()) {
            log(tr("[Damos %1] adresse 0x%2 hors ROM.")
                    .arg(autoModId).arg(off, 0, 16), true);
            return false;
        }
        log(tr("[Damos %1] address @ 0x%2 → %3 octet(s) %4")
                .arg(autoModId).arg(off, 0, 16).arg(a->replace.size())
                .arg(changed ? tr("modifiés") : tr("déjà à la cible")));
        return changed;
    }
    log(tr("[Damos %1] type non supporté.").arg(autoModId), true);
    return false;
}

bool AutoModsPanel::restoreDamosAutoMod(const QString& autoModId) {
    auto recipe = ecu::OpenDamos::loadRecipe(m_doc->ecuId());
    if (!recipe) return false;
    const ecu::DamosAutoMod* a = findDamosAutoMod(*recipe, autoModId);
    if (!a) return false;
    if (a->restore.empty()) {
        log(tr("[Damos %1] pas de bytes de restauration définis.").arg(autoModId), true);
        return false;
    }

    QByteArray& rom = m_doc->romMutable();
    if (a->type == ecu::DamosAutoModType::Pattern) {
        // Pour un pattern, on cherche `replace` (l'état actuel "appliqué") puis on
        // ré-écrit `restore` à la même position.
        std::span<const uint8_t> needle(a->replace.data(), a->replace.size());
        const qsizetype off = findPattern(rom, needle);
        if (off < 0) {
            log(tr("[Damos %1] motif appliqué introuvable — déjà restauré ?")
                    .arg(autoModId), true);
            return false;
        }
        const bool changed = writeBytes(rom, off, a->restore);
        log(tr("[Damos %1] restauré @ 0x%2 (%3 octets).")
                .arg(autoModId).arg(off, 0, 16).arg(a->restore.size()));
        return changed;
    }
    if (a->type == ecu::DamosAutoModType::Address) {
        if (!a->address) return false;
        const qsizetype off = static_cast<qsizetype>(*a->address);
        const bool changed = writeBytes(rom, off, a->restore);
        log(tr("[Damos %1] restauré @ 0x%2 (%3 octets).")
                .arg(autoModId).arg(off, 0, 16).arg(a->restore.size()));
        return changed;
    }
    return false;
}

} // namespace ecu_studio

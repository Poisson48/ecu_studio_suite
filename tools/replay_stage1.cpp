// replay_stage1.cpp — rejoue un template Stage 1 sur une ROM en utilisant le
// VRAI moteur ecu-core (OpenDamos::relocate + applyPctToMap), exactement comme
// AutoModsPanel::applyTemplate. Sert à vérifier hors-GUI ce que le Stage 1
// modifie réellement sur une ROM donnée.
//
//   ./replay_stage1 <ecuId> <templateId> <rom_in> <rom_out>

#include "ecu/OpenDamos.hpp"
#include "ecu/RomPatcher.hpp"
#include "ecu/VehicleTemplates.hpp"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <cstdio>
#include <span>
#include <unordered_map>

using namespace ecu;

static const char* srcName(AddressSource s) {
    switch (s) {
        case AddressSource::Fingerprint:     return "fingerprint";
        case AddressSource::Anchor:          return "anchor";
        case AddressSource::DefaultFallback: return "default-fallback";
    }
    return "?";
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <ecuId> <templateId> <rom_in> <rom_out>\n", argv[0]);
        return 2;
    }
    const QString ecuId = QString::fromUtf8(argv[1]);
    const QString tplId = QString::fromUtf8(argv[2]);

    QFile fin(QString::fromUtf8(argv[3]));
    if (!fin.open(QIODevice::ReadOnly)) { std::fprintf(stderr, "cannot open ROM\n"); return 1; }
    QByteArray rom = fin.readAll();
    fin.close();
    std::printf("ROM chargee : %lld octets\n", static_cast<long long>(rom.size()));

    auto tpl = getTemplate(tplId.toStdString());
    if (!tpl || !tpl->stage1) { std::fprintf(stderr, "template/stage1 introuvable\n"); return 1; }
    std::printf("Template    : %s\n\n", tpl->name.c_str());

    auto recipe = OpenDamos::loadRecipe(ecuId);
    if (!recipe) { std::fprintf(stderr, "open_damos KO: %s\n", recipe.error().c_str()); return 1; }
    OpenDamos od; od.setRecipe(std::move(*recipe));

    // facteurs d'echelle pour l'affichage physique
    std::unordered_map<std::string, std::pair<double,double>> scale; // name -> (factor, offset)
    std::unordered_map<std::string, std::string> unit;
    if (od.recipe())
        for (const auto& e : od.recipe()->characteristics) {
            scale[e.name] = { e.data.factor, e.data.offset };
            unit[e.name]  = e.data.unit;
        }

    std::unordered_map<std::string, RelocResult> byName;
    for (auto& r : od.relocate(rom)) byName.emplace(r.name, std::move(r));

    std::printf("%-26s %-11s %-16s %-6s %-11s %s\n",
                "MAP", "adresse", "source", "score", "mode", "resultat");
    std::printf("%s\n", std::string(96,'-').c_str());

    int totalCells = 0;
    for (const auto& [name, pct] : tpl->stage1->pcts) {
        auto it = byName.find(name);
        if (it == byName.end()) {
            std::printf("%-26s %-11s %-16s %-6s %-11s %s\n", name.c_str(), "-", "-", "-", "-",
                        "ABSENTE d'open_damos -> ignoree");
            continue;
        }
        const RelocResult& rel = it->second;
        if (rel.addressSource == AddressSource::DefaultFallback || rel.score == 0.0) {
            std::printf("%-26s 0x%08zX %-16s %-6.2f %-11s %s\n",
                        name.c_str(), rel.address, srcName(rel.addressSource),
                        rel.score, rel.matchMode.c_str(),
                        "NON RELOCALISEE -> SKIP securite");
            continue;
        }

        // valeurs physiques avant
        auto pre = readMapData(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(rom.constData()), rom.size()), rel.address);
        double fac = scale.count(name) ? scale[name].first  : 1.0;
        double off = scale.count(name) ? scale[name].second : 0.0;
        double preMax = 0, postMax = 0;
        if (pre) { int16_t m = pre->data.empty()?0:pre->data[0];
                   for (auto v : pre->data) if (v>m) m=v; preMax = m*fac+off; }

        std::span<uint8_t> sp(reinterpret_cast<uint8_t*>(rom.data()), rom.size());
        auto res = applyPctToMap(sp, rel.address, static_cast<double>(pct));
        if (!res) {
            std::printf("%-26s 0x%08zX %-16s %-6.2f %-11s ERREUR: %s\n",
                        name.c_str(), rel.address, srcName(rel.addressSource),
                        rel.score, rel.matchMode.c_str(), res.error().c_str());
            continue;
        }
        auto post = readMapData(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(rom.constData()), rom.size()), rel.address);
        if (post) { int16_t m = post->data.empty()?0:post->data[0];
                    for (auto v : post->data) if (v>m) m=v; postMax = m*fac+off; }

        totalCells += static_cast<int>(res->size());
        char buf[160];
        std::snprintf(buf, sizeof buf, "%+d%%  %zu cellules  max %.1f->%.1f %s",
                      pct, res->size(), preMax, postMax,
                      unit.count(name)?unit[name].c_str():"");
        std::printf("%-26s 0x%08zX %-16s %-6.2f %-11s %s\n",
                    name.c_str(), rel.address, srcName(rel.addressSource),
                    rel.score, rel.matchMode.c_str(), buf);
    }

    std::printf("\nTotal cellules modifiees : %d\n", totalCells);

    QFile fout(QString::fromUtf8(argv[4]));
    if (!fout.open(QIODevice::WriteOnly)) { std::fprintf(stderr, "cannot write out\n"); return 1; }
    fout.write(rom); fout.close();
    std::printf("ROM ecrite : %s\n", argv[4]);
    return 0;
}

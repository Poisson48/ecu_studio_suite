#include "obd/mcp_read_dtc.h"
#include "obd/elm327.h"

#include <QEventLoop>
#include <QTimer>

#include <stdexcept>

namespace ecu_studio {

namespace {

QString pickPort(const QString& requested) {
    if (!requested.isEmpty()) return requested;
    const auto ports = elm::Elm327::listPorts();
    for (const auto& p : ports) {
        if (p.likelyElm) return p.port;
    }
    if (ports.size() == 1) return ports.front().port;
    if (ports.isEmpty())
        throw std::runtime_error(
            "Aucun port série détecté. Branche un ELM327 USB ou passe port=…");
    throw std::runtime_error(
        "Plusieurs ports série sans pont ELM reconnaissable — passe port=… "
        "(ex. /dev/ttyACM0)");
}

} // namespace

ecu::mcp::Tool makeReadDtcTool() {
    using json = ecu::mcp::json;

    ecu::mcp::Tool t;
    t.name = "read_dtc";
    t.description =
        "Lit les codes défaut OBD-II via un adaptateur ELM327 USB (mode 03 "
        "mémorisés, ou mode 07 en attente si pending=true). Ne modifie rien "
        "sur le véhicule — l'effacement (mode 04) n'est pas exposé en MCP.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"port", {{"type", "string"},
                      {"description",
                       "Chemin du port série (ex. /dev/ttyACM0). Optionnel : "
                       "auto-détecte un pont USB-série typique ELM327."}}},
            {"baud", {{"type", "integer"},
                      {"description",
                       "Débit (0 = auto 38400 puis 115200). Défaut 0."}}},
            {"pending", {{"type", "boolean"},
                         {"description",
                          "true = mode 07 (en attente), false = mode 03 "
                          "(mémorisés). Défaut false."}}}
        }},
        {"required", json::array()}
    };
    t.handler = [](const json& p) -> json {
        QString port;
        if (p.contains("port") && p["port"].is_string())
            port = QString::fromStdString(p["port"].get<std::string>());
        port = pickPort(port);

        int baud = 0;
        if (p.contains("baud") && p["baud"].is_number_integer())
            baud = p["baud"].get<int>();

        const bool pending = p.contains("pending") && p["pending"].is_boolean()
                                 && p["pending"].get<bool>();

        elm::Elm327 elm;
        QEventLoop loop;
        QStringList codes;
        QString error;
        bool gotCodes = false;
        bool connected = false;

        QObject::connect(&elm, &elm::Elm327::connected, &loop, [&](const QString&) {
            connected = true;
            elm.readDtcs(pending);
        });
        QObject::connect(&elm, &elm::Elm327::dtcsReady, &loop,
                         [&](const QStringList& c, bool) {
                             codes = c;
                             gotCodes = true;
                             loop.quit();
                         });
        QObject::connect(&elm, &elm::Elm327::errorOccurred, &loop,
                         [&](const QString& m) {
                             error = m;
                             loop.quit();
                         });

        // Init ELM + négociation protocole auto : jusqu'à ~20 s sur KWP lent.
        QTimer::singleShot(20000, &loop, &QEventLoop::quit);

        elm.connectPort(port, baud);
        loop.exec();
        elm.disconnectPort();

        if (!error.isEmpty())
            throw std::runtime_error(error.toStdString());
        if (!connected)
            throw std::runtime_error(
                "Timeout : ELM327 ne s'est pas initialisé sur " + port.toStdString());
        if (!gotCodes)
            throw std::runtime_error(
                "Timeout : pas de réponse DTC (mode " +
                std::string(pending ? "07" : "03") + ")");

        json arr = json::array();
        for (const QString& c : codes) arr.push_back(c.toStdString());
        return {
            {"port", port.toStdString()},
            {"mode", pending ? "07" : "03"},
            {"pending", pending},
            {"codes", arr},
            {"count", arr.size()},
        };
    };
    return t;
}

} // namespace ecu_studio

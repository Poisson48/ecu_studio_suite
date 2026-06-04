#include "opendamos_library_panel.h"
#include "../rom_document.h"
#include "ecu/OpenDamos.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QShowEvent>
#include <QColor>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QUrl>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

namespace ecu_studio {

namespace {
// URL du manifest de la bibliothèque OpenDAMOS publiée sur GitHub (raw).
constexpr const char* kManifestUrl =
    "https://raw.githubusercontent.com/Poisson48/ecu_studio_suite/main/"
    "ressources/index.json";

constexpr const char* kUserAgent = "ECU-Studio-OpenDamosLibrary";

// Colonnes du tableau.
enum Col { ColEcu = 0, ColName, ColMaturity, ColEndian, ColMaps, ColSource, ColCount };

// Couleur de fond de la pilule de maturité (spec « WinOLS libre »).
QString maturityColor(const QString& maturity) {
    const QString m = maturity.toLower();
    if (m == QLatin1String("proven")) return QStringLiteral("#22c55e"); // vert
    if (m == QLatin1String("beta"))   return QStringLiteral("#f59e0b"); // ambre
    return QStringLiteral("#6b7280");                                   // gris (incoming/inconnu)
}

void applyTlsSystemCerts(QNetworkAccessManager* nam) {
    nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    // Comme l'updater : charge explicitement les CA système pour fiabiliser HTTPS
    // (notamment en AppImage où Qt peut ne pas les trouver tout seul).
    QSslConfiguration cfg = QSslConfiguration::defaultConfiguration();
    auto certs = QSslConfiguration::systemCaCertificates();
    if (!certs.isEmpty()) {
        cfg.setCaCertificates(certs);
        QSslConfiguration::setDefaultConfiguration(cfg);
    }
}

QNetworkRequest makeRequest(const QUrl& url) {
    QNetworkRequest req{url};
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}
} // namespace

OpenDamosLibraryPanel::OpenDamosLibraryPanel(RomDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc) {
    m_nam = new QNetworkAccessManager(this);
    applyTlsSystemCerts(m_nam);

    buildUi();

    if (m_doc) {
        // « Trouver pour ma ROM » n'a de sens que quand une ROM est chargée.
        connect(m_doc, &RomDocument::romLoaded, this, [this]() {
            if (m_findBtn) m_findBtn->setEnabled(m_doc->isLoaded());
            applyFilter();   // re-restrict to the freshly-loaded ECU
        });
    }
    if (m_findBtn) m_findBtn->setEnabled(m_doc && m_doc->isLoaded());
}

OpenDamosLibraryPanel::OpenDamosLibraryPanel(QWidget* parent)
    : OpenDamosLibraryPanel(nullptr, parent) {}

void OpenDamosLibraryPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(8, 8, 8, 8);

    // ── En-tête : titre + description ────────────────────────────────────────
    auto* title = new QLabel(tr("Bibliothèque OpenDAMOS"), this);
    title->setStyleSheet("font-size:15px; font-weight:700;");
    root->addWidget(title);

    auto* subtitle = new QLabel(
        tr("Récupère les recettes OpenDAMOS publiées sur GitHub — installez "
           "les recettes ECU sans recompiler l'application."),
        this);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet("color:#7c8fa6;");
    root->addWidget(subtitle);

    // ── Barre d'outils : filtre + actions ────────────────────────────────────
    auto* toolbar = new QHBoxLayout;

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Filtrer (ECU, nom, source)..."));
    m_filterEdit->setClearButtonEnabled(true);
    connect(m_filterEdit, &QLineEdit::textChanged, this,
            [this](const QString&) { applyFilter(); });
    toolbar->addWidget(m_filterEdit, 1);

    // Par défaut on n'affiche QUE les recettes correspondant à l'ECU chargé.
    m_ecuOnly = new QCheckBox(tr("Cet ECU uniquement"), this);
    m_ecuOnly->setChecked(true);
    m_ecuOnly->setToolTip(
        tr("N'affiche que les recettes correspondant à l'ECU de la ROM chargée. "
           "Décochez pour parcourir toute la bibliothèque."));
    connect(m_ecuOnly, &QCheckBox::toggled, this, [this](bool) { applyFilter(); });
    toolbar->addWidget(m_ecuOnly);

    m_findBtn = new QPushButton(tr("Trouver pour ma ROM"), this);
    m_findBtn->setEnabled(false);
    m_findBtn->setToolTip(
        tr("Filtre la liste sur l'ECU de la ROM actuellement chargée."));
    connect(m_findBtn, &QPushButton::clicked, this, [this]() {
        if (m_doc && !m_doc->ecuId().isEmpty()) {
            if (m_ecuOnly) m_ecuOnly->setChecked(true);
            m_filterEdit->clear();
            applyFilter();
        }
    });
    toolbar->addWidget(m_findBtn);

    m_installBtn = new QPushButton(tr("Installer"), this);
    m_installBtn->setObjectName("accentBtn");
    m_installBtn->setEnabled(false);
    connect(m_installBtn, &QPushButton::clicked, this,
            &OpenDamosLibraryPanel::installSelected);
    toolbar->addWidget(m_installBtn);

    m_refreshBtn = new QPushButton(tr("Rafraîchir"), this);
    connect(m_refreshBtn, &QPushButton::clicked, this,
            &OpenDamosLibraryPanel::refresh);
    toolbar->addWidget(m_refreshBtn);

    root->addLayout(toolbar);

    // ── Tableau ──────────────────────────────────────────────────────────────
    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels({
        tr("ECU"), tr("Nom"), tr("Maturité"), tr("Endian"),
        tr("Cartes"), tr("Source"),
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    auto* hh = m_table->horizontalHeader();
    hh->setSectionResizeMode(ColName, QHeaderView::Stretch);
    hh->setSectionResizeMode(ColSource, QHeaderView::Stretch);
    hh->setSectionResizeMode(ColEcu, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(ColMaturity, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(ColEndian, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(ColMaps, QHeaderView::ResizeToContents);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_installBtn->setEnabled(!m_table->selectedItems().isEmpty());
    });
    connect(m_table, &QTableWidget::doubleClicked, this,
            [this](const QModelIndex& idx) { if (idx.isValid()) installRow(idx.row()); });
    // Menu contextuel « Installer » sur la ligne.
    connect(m_table, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                const QModelIndex idx = m_table->indexAt(pos);
                if (idx.isValid()) installRow(idx.row());
            });

    root->addWidget(m_table, 1);

    // ── Ligne de statut ──────────────────────────────────────────────────────
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color:#7c8fa6;");
    root->addWidget(m_statusLabel);

    setStatus(tr("Cliquez sur « Rafraîchir » pour charger la bibliothèque."));
}

void OpenDamosLibraryPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // Charge automatiquement au premier affichage (paresseux : aucun trafic
    // réseau tant que l'utilisateur n'ouvre pas l'onglet).
    if (!m_loaded) refresh();
}

void OpenDamosLibraryPanel::setStatus(const QString& msg, bool error) {
    if (!m_statusLabel) return;
    m_statusLabel->setText(msg);
    m_statusLabel->setStyleSheet(error ? "color:#ef4444;" : "color:#7c8fa6;");
}

void OpenDamosLibraryPanel::refresh() {
    if (!QSslSocket::supportsSsl()) {
        setStatus(tr("TLS/SSL indisponible — impossible de contacter GitHub. "
                     "Vérifiez l'installation d'OpenSSL du système."),
                  /*error=*/true);
        return;
    }

    setStatus(tr("Téléchargement de la bibliothèque..."));
    if (m_refreshBtn) m_refreshBtn->setEnabled(false);

    QNetworkReply* reply = m_nam->get(makeRequest(QUrl(QString::fromLatin1(kManifestUrl))));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_refreshBtn) m_refreshBtn->setEnabled(true);

        if (reply->error() != QNetworkReply::NoError) {
            setStatus(tr("Erreur réseau : %1").arg(reply->errorString()),
                      /*error=*/true);
            return;
        }
        parseManifest(reply->readAll());
    });
}

void OpenDamosLibraryPanel::parseManifest(const QByteArray& body) {
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        setStatus(tr("Manifeste mal formé : %1").arg(perr.errorString()),
                  /*error=*/true);
        return;
    }

    const QJsonObject obj = doc.object();
    m_raw = obj.value(QStringLiteral("raw")).toString();
    const QJsonArray arr = obj.value(QStringLiteral("recipes")).toArray();

    m_recipes.clear();
    m_recipes.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        if (!v.isObject()) continue;
        const QJsonObject r = v.toObject();
        Recipe rec;
        rec.ecu       = r.value(QStringLiteral("ecu")).toString();
        rec.path      = r.value(QStringLiteral("path")).toString();
        rec.name      = r.value(QStringLiteral("name")).toString();
        rec.maturity  = r.value(QStringLiteral("maturity")).toString();
        rec.byteOrder = r.value(QStringLiteral("byteOrder")).toString();
        // `maps` peut être un nombre ou un tableau selon la recette.
        const QJsonValue mapsV = r.value(QStringLiteral("maps"));
        if (mapsV.isDouble())     rec.maps = mapsV.toInt();
        else if (mapsV.isArray()) rec.maps = mapsV.toArray().size();
        rec.comAxis = r.value(QStringLiteral("comAxis")).toBool();
        rec.source  = r.value(QStringLiteral("source")).toString();
        if (rec.ecu.isEmpty() || rec.path.isEmpty()) continue;
        m_recipes.push_back(rec);
    }

    m_loaded = true;
    populateTable();

    const int installed = [this]() {
        int n = 0;
        for (const Recipe& r : m_recipes) if (isInstalled(r.ecu)) ++n;
        return n;
    }();
    setStatus(tr("%1 recette(s) disponible(s) · %2 installée(s).")
                  .arg(m_recipes.size()).arg(installed));
}

QWidget* OpenDamosLibraryPanel::makeMaturityPill(const QString& maturity,
                                                 QWidget* parent) {
    // Conteneur pour centrer la pilule dans la cellule.
    auto* wrap = new QWidget(parent);
    auto* lay  = new QHBoxLayout(wrap);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setAlignment(Qt::AlignCenter);

    auto* pill = new QLabel(maturity.isEmpty() ? QStringLiteral("incoming") : maturity, wrap);
    pill->setAlignment(Qt::AlignCenter);
    pill->setStyleSheet(QStringLiteral(
                            "background-color:%1; color:#ffffff; border-radius:8px;"
                            " padding:1px 9px; font-size:10px; font-weight:600;")
                            .arg(maturityColor(maturity)));
    lay->addWidget(pill);
    return wrap;
}

void OpenDamosLibraryPanel::populateTable() {
    m_table->setRowCount(0);
    m_table->setRowCount(m_recipes.size());

    for (int row = 0; row < m_recipes.size(); ++row) {
        const Recipe& r = m_recipes[row];
        const bool installed = isInstalled(r.ecu);

        auto* ecuItem = new QTableWidgetItem(r.ecu);
        // Stocke l'index de la recette pour retrouver la ligne après filtrage.
        ecuItem->setData(Qt::UserRole, row);
        if (installed) {
            // ✓ vert + indication « installé » via couleur de texte.
            ecuItem->setText(QStringLiteral("\xe2\x9c\x93 ") + r.ecu);
            ecuItem->setForeground(QColor(QStringLiteral("#22c55e")));
            ecuItem->setToolTip(tr("Déjà installée"));
        }
        m_table->setItem(row, ColEcu, ecuItem);

        m_table->setItem(row, ColName, new QTableWidgetItem(r.name));
        m_table->setCellWidget(row, ColMaturity, makeMaturityPill(r.maturity, m_table));

        QString endian = r.byteOrder;
        if (endian.compare(QLatin1String("big"), Qt::CaseInsensitive) == 0)
            endian = QStringLiteral("BE");
        else if (endian.compare(QLatin1String("little"), Qt::CaseInsensitive) == 0)
            endian = QStringLiteral("LE");
        m_table->setItem(row, ColEndian, new QTableWidgetItem(endian));

        auto* mapsItem = new QTableWidgetItem(r.maps > 0 ? QString::number(r.maps)
                                                         : QString());
        mapsItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, ColMaps, mapsItem);

        m_table->setItem(row, ColSource, new QTableWidgetItem(r.source));
    }

    applyFilter();
}

void OpenDamosLibraryPanel::applyFilter() {
    const QString needle = m_filterEdit ? m_filterEdit->text().trimmed().toLower()
                                        : QString();
    const QString sel = (m_doc && !m_doc->ecuId().isEmpty())
                            ? m_doc->ecuId().toLower() : QString();
    // « Cet ECU uniquement » : actif seulement quand une ROM avec un ecuId est
    // chargée — sinon il masquerait toute la bibliothèque.
    const bool ecuOnly = m_ecuOnly && m_ecuOnly->isChecked() && !sel.isEmpty();
    int shown = 0;
    for (int row = 0; row < m_recipes.size(); ++row) {
        const Recipe& r = m_recipes[row];
        const QString re = r.ecu.toLower();
        bool show = needle.isEmpty()
                    || re.contains(needle)
                    || r.name.toLower().contains(needle)
                    || r.source.toLower().contains(needle);
        if (ecuOnly) {
            const bool match = (re == sel) || re.startsWith(sel) || sel.startsWith(re);
            show = show && match;
        }
        m_table->setRowHidden(row, !show);
        if (show) ++shown;
    }
    if (ecuOnly && m_statusLabel)
        setStatus(tr("%1 recette(s) pour « %2 » (décochez « Cet ECU uniquement » "
                     "pour tout voir).").arg(shown).arg(m_doc->ecuId()));
}

bool OpenDamosLibraryPanel::isInstalled(const QString& ecu) {
    if (ecu.isEmpty()) return false;
    const QString path = QDir(ecu::OpenDamos::userRecipeDir())
                             .filePath(ecu + QStringLiteral("/open_damos.json"));
    return QFileInfo::exists(path);
}

void OpenDamosLibraryPanel::installSelected() {
    const auto sel = m_table->selectionModel()->selectedRows();
    if (!sel.isEmpty()) { installRow(sel.first().row()); return; }
    // Repli : déduire la ligne d'un item sélectionné quelconque.
    const auto items = m_table->selectedItems();
    if (!items.isEmpty()) installRow(items.first()->row());
}

void OpenDamosLibraryPanel::installRow(int row) {
    if (row < 0 || row >= m_recipes.size()) return;
    const Recipe r = m_recipes[row];

    if (m_raw.isEmpty()) {
        setStatus(tr("URL de base absente du manifeste — impossible d'installer."),
                  /*error=*/true);
        return;
    }

    // Concatène base (raw) + chemin relatif. QUrl::resolved gère le « / » médian.
    const QUrl url = QUrl(m_raw).resolved(QUrl(r.path));

    setStatus(tr("Installation de %1...").arg(r.ecu));
    if (m_installBtn) m_installBtn->setEnabled(false);

    QNetworkReply* reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, r]() {
        reply->deleteLater();
        if (m_installBtn) m_installBtn->setEnabled(!m_table->selectedItems().isEmpty());

        if (reply->error() != QNetworkReply::NoError) {
            setStatus(tr("Échec du téléchargement de %1 : %2")
                          .arg(r.ecu, reply->errorString()),
                      /*error=*/true);
            return;
        }

        const QByteArray data = reply->readAll();
        // Garde-fou : refuse un corps qui n'est pas du JSON (ex. page 404 HTML).
        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            setStatus(tr("Recette %1 invalide (JSON attendu).").arg(r.ecu),
                      /*error=*/true);
            return;
        }

        const QString dir = QDir(ecu::OpenDamos::userRecipeDir()).filePath(r.ecu);
        if (!QDir().mkpath(dir)) {
            setStatus(tr("Impossible de créer le dossier %1.").arg(dir),
                      /*error=*/true);
            return;
        }
        const QString outPath = dir + QStringLiteral("/open_damos.json");
        QFile f(outPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            setStatus(tr("Écriture impossible : %1").arg(outPath), /*error=*/true);
            return;
        }
        f.write(data);
        f.close();

        // Re-marque la ligne comme installée + statut succès.
        populateTable();
        setStatus(tr("Recette « %1 » installée dans %2.").arg(r.ecu, outPath));
    });
}

} // namespace ecu_studio

#include "damos_editor_panel.h"
#include "../rom_document.h"
#include "../byte_span.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include "ecu/MapFinder.hpp"
#include "ecu/RomPatcher.hpp"

#include <QRegularExpression>
#include <algorithm>

namespace ecu_studio {

namespace {

const char* kDataTypes[] = { "SBYTE", "UBYTE", "SWORD_BE", "UWORD_BE", "SLONG_BE", "ULONG_BE" };

ecu::DamosDataType comboToDataType(int idx) {
    using DT = ecu::DamosDataType;
    switch (idx) {
        case 0: return DT::SByte;
        case 1: return DT::UByte;
        case 2: return DT::SWordBE;
        case 3: return DT::UWordBE;
        case 4: return DT::SLongBE;
        case 5: return DT::ULongBE;
    }
    return DT::SWordBE;
}

int dataTypeToCombo(ecu::DamosDataType t) {
    using DT = ecu::DamosDataType;
    switch (t) {
        case DT::SByte:   return 0;
        case DT::UByte:   return 1;
        case DT::SWordBE: return 2;
        case DT::UWordBE: return 3;
        case DT::SLongBE: return 4;
        case DT::ULongBE: return 5;
    }
    return 2;
}

const char* kTypes[] = { "MAP", "CURVE", "VALUE" };

ecu::DamosType comboToType(int idx) {
    switch (idx) {
        case 0: return ecu::DamosType::Map;
        case 1: return ecu::DamosType::Curve;
        case 2: return ecu::DamosType::Value;
    }
    return ecu::DamosType::Map;
}

int typeToCombo(ecu::DamosType t) {
    switch (t) {
        case ecu::DamosType::Map:     return 0;
        case ecu::DamosType::Curve:   return 1;
        case ecu::DamosType::Value:   return 2;
        case ecu::DamosType::Unknown: return 0;
    }
    return 0;
}

QString typeLabel(ecu::DamosType t) {
    switch (t) {
        case ecu::DamosType::Map:   return "MAP";
        case ecu::DamosType::Curve: return "CURVE";
        case ecu::DamosType::Value: return "VALUE";
        default:                    return "?";
    }
}

QString fpToCsv(const std::vector<int64_t>& v) {
    QStringList parts;
    parts.reserve(static_cast<int>(v.size()));
    for (auto x : v) parts << QString::number(x);
    return parts.join(", ");
}

std::vector<int64_t> csvToFp(const QString& csv) {
    std::vector<int64_t> out;
    const QStringList parts = csv.split(QRegularExpression("[,;\\s]+"),
                                        Qt::SkipEmptyParts);
    for (const auto& p : parts) {
        bool ok = false;
        const long long v = p.toLongLong(&ok);
        if (ok) out.push_back(static_cast<int64_t>(v));
    }
    return out;
}

quint32 parseHex(const QString& s) {
    QString t = s.trimmed();
    if (t.startsWith("0x", Qt::CaseInsensitive)) t = t.mid(2);
    bool ok = false;
    return t.toUInt(&ok, 16);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

DamosEditorPanel::DamosEditorPanel(RomDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc) {
    buildUi();

    if (m_doc) {
        // Quand l'ECU change (nouvelle ROM), si l'on n'a aucun recipe en cours,
        // propose d'en charger un. Le prompt « ECU inconnu → créer ? » est géré
        // par MainWindow pour rester non-intrusif si l'utilisateur ne veut pas.
        connect(m_doc, &RomDocument::ecuChanged, this, [this](const QString& ecu) {
            if (m_recipe.characteristics.empty() && m_path.isEmpty() && !ecu.isEmpty())
                m_ecuIdEdit->setText(ecu);
        });
    }
}

void DamosEditorPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    // ── Barre fichier ────────────────────────────────────────────────────
    auto* fileRow = new QHBoxLayout;
    fileRow->setSpacing(6);
    m_newBtn    = new QPushButton(tr("Nouveau"), this);
    m_openBtn   = new QPushButton(tr("Ouvrir…"), this);
    m_saveBtn   = new QPushButton(tr("Enregistrer"), this);
    m_saveBtn->setObjectName("accentBtn");
    m_saveAsBtn = new QPushButton(tr("Enregistrer sous…"), this);
    fileRow->addWidget(m_newBtn);
    fileRow->addWidget(m_openBtn);
    fileRow->addWidget(m_saveBtn);
    fileRow->addWidget(m_saveAsBtn);
    fileRow->addSpacing(12);
    fileRow->addWidget(new QLabel(tr("ECU :"), this));
    m_ecuIdEdit = new QLineEdit(this);
    m_ecuIdEdit->setPlaceholderText(tr("edc16c34, simos11, …"));
    m_ecuIdEdit->setMaximumWidth(180);
    fileRow->addWidget(m_ecuIdEdit);
    fileRow->addStretch();
    m_pathLabel = new QLabel(tr("(non sauvegardé)"), this);
    m_pathLabel->setStyleSheet("color:#7c8fa6; font-size:11px;");
    fileRow->addWidget(m_pathLabel);
    root->addLayout(fileRow);

    // ── Splitter : liste à gauche, formulaire à droite ──────────────────
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    auto* listBox = new QGroupBox(tr("Characteristics"), this);
    auto* listLay = new QVBoxLayout(listBox);

    auto* actionRow = new QHBoxLayout;
    m_addBtn    = new QPushButton(tr("+ Ajouter"), this);
    m_dupBtn    = new QPushButton(tr("Dupliquer"), this);
    m_delBtn    = new QPushButton(tr("Supprimer"), this);
    m_detectBtn = new QPushButton(tr("Détecter depuis ROM"), this);
    m_detectBtn->setToolTip(tr("Lance ecu::findMaps sur la ROM courante et ajoute "
                               "les candidates comme nouvelles characteristics."));
    actionRow->addWidget(m_addBtn);
    actionRow->addWidget(m_dupBtn);
    actionRow->addWidget(m_delBtn);
    actionRow->addStretch();
    actionRow->addWidget(m_detectBtn);
    listLay->addLayout(actionRow);

    m_entryTable = new QTableWidget(this);
    m_entryTable->setColumnCount(4);
    m_entryTable->setHorizontalHeaderLabels({ tr("Nom"), tr("Type"), tr("Adresse"), tr("Dims") });
    m_entryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_entryTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_entryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_entryTable->verticalHeader()->setVisible(false);
    m_entryTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_entryTable->setColumnWidth(0, 240);
    m_entryTable->setColumnWidth(1, 70);
    m_entryTable->setColumnWidth(2, 90);
    m_entryTable->setColumnWidth(3, 70);
    listLay->addWidget(m_entryTable);
    splitter->addWidget(listBox);

    // ── Formulaire à droite ──────────────────────────────────────────────
    auto* formScroll = new QScrollArea(this);
    formScroll->setWidgetResizable(true);
    formScroll->setFrameShape(QFrame::NoFrame);
    auto* formHost = new QWidget;
    auto* formLay  = new QVBoxLayout(formHost);
    formLay->setContentsMargins(8, 0, 0, 0);
    formLay->setSpacing(10);

    // Bloc « Général »
    {
        auto* g = new QGroupBox(tr("Général"), formHost);
        auto* f = new QFormLayout(g);
        m_nameEdit     = new QLineEdit(g);
        m_typeCombo    = new QComboBox(g);
        for (const auto* s : kTypes) m_typeCombo->addItem(s);
        m_categoryEdit = new QLineEdit(g);
        m_categoryEdit->setPlaceholderText(tr("stage1, info, egr…"));
        m_descEdit     = new QTextEdit(g);
        m_descEdit->setMaximumHeight(80);
        m_addrEdit     = new QLineEdit(g);
        m_addrEdit->setPlaceholderText(tr("0x1C1448"));
        f->addRow(tr("Nom :"),         m_nameEdit);
        f->addRow(tr("Type :"),        m_typeCombo);
        f->addRow(tr("Catégorie :"),   m_categoryEdit);
        f->addRow(tr("Description :"), m_descEdit);
        f->addRow(tr("Adresse :"),     m_addrEdit);
        formLay->addWidget(g);
    }

    // Bloc « Dimensions »
    {
        auto* g = new QGroupBox(tr("Dimensions"), formHost);
        auto* h = new QHBoxLayout(g);
        m_nxSpin = new QSpinBox(g);
        m_nxSpin->setRange(0, 1024);
        m_nySpin = new QSpinBox(g);
        m_nySpin->setRange(0, 1024);
        h->addWidget(new QLabel(tr("nx :"), g));
        h->addWidget(m_nxSpin);
        h->addSpacing(12);
        h->addWidget(new QLabel(tr("ny :"), g));
        h->addWidget(m_nySpin);
        h->addStretch();
        formLay->addWidget(g);
    }

    auto mkAxisBlock = [&](const QString& title,
                           QComboBox*& tc, QLineEdit*& ue,
                           QDoubleSpinBox*& fs, QDoubleSpinBox*& os,
                           QLineEdit*& fp, QPushButton*& capture) {
        auto* g = new QGroupBox(title, formHost);
        auto* f = new QFormLayout(g);
        tc = new QComboBox(g);
        for (const auto* s : kDataTypes) tc->addItem(s);
        ue = new QLineEdit(g);
        ue->setPlaceholderText(tr("rpm, kg/h, %…"));
        fs = new QDoubleSpinBox(g);
        fs->setDecimals(6); fs->setRange(-1e9, 1e9); fs->setValue(1.0);
        fs->setButtonSymbols(QAbstractSpinBox::PlusMinus);
        os = new QDoubleSpinBox(g);
        os->setDecimals(6); os->setRange(-1e9, 1e9); os->setValue(0.0);
        os->setButtonSymbols(QAbstractSpinBox::PlusMinus);
        fp = new QLineEdit(g);
        fp->setPlaceholderText(tr("400, 650, 1000, 1500…"));
        capture = new QPushButton(tr("Capturer depuis ROM"), g);
        capture->setToolTip(tr("Lit les valeurs d'axe à l'adresse de cette entrée "
                               "dans la ROM courante et les colle ici."));
        f->addRow(tr("Type :"),   tc);
        f->addRow(tr("Unité :"),  ue);
        f->addRow(tr("Facteur :"),fs);
        f->addRow(tr("Offset :"), os);
        f->addRow(tr("Fingerprint :"), fp);
        f->addRow("",             capture);
        formLay->addWidget(g);
    };
    mkAxisBlock(tr("Axe X"), m_xAxisTypeCombo, m_xAxisUnitEdit,
                m_xAxisFactorSpin, m_xAxisOffsetSpin, m_xAxisFpEdit, m_xAxisCaptureBtn);
    mkAxisBlock(tr("Axe Y (MAP uniquement)"), m_yAxisTypeCombo, m_yAxisUnitEdit,
                m_yAxisFactorSpin, m_yAxisOffsetSpin, m_yAxisFpEdit, m_yAxisCaptureBtn);

    // Bloc « Data »
    {
        auto* g = new QGroupBox(tr("Data (valeurs)"), formHost);
        auto* f = new QFormLayout(g);
        m_dataTypeCombo = new QComboBox(g);
        for (const auto* s : kDataTypes) m_dataTypeCombo->addItem(s);
        m_dataTypeCombo->setCurrentIndex(2);  // SWORD_BE par défaut
        m_dataUnitEdit  = new QLineEdit(g);
        m_dataUnitEdit->setPlaceholderText(tr("Nm, mg/cyc, hPa…"));
        m_dataFactorSpin = new QDoubleSpinBox(g);
        m_dataFactorSpin->setDecimals(6); m_dataFactorSpin->setRange(-1e9, 1e9);
        m_dataFactorSpin->setValue(1.0);
        m_dataFactorSpin->setButtonSymbols(QAbstractSpinBox::PlusMinus);
        m_dataOffsetSpin = new QDoubleSpinBox(g);
        m_dataOffsetSpin->setDecimals(6); m_dataOffsetSpin->setRange(-1e9, 1e9);
        m_dataOffsetSpin->setButtonSymbols(QAbstractSpinBox::PlusMinus);
        f->addRow(tr("Type :"),    m_dataTypeCombo);
        f->addRow(tr("Unité :"),   m_dataUnitEdit);
        f->addRow(tr("Facteur :"), m_dataFactorSpin);
        f->addRow(tr("Offset :"),  m_dataOffsetSpin);
        formLay->addWidget(g);
    }

    formLay->addStretch();
    formScroll->setWidget(formHost);
    splitter->addWidget(formScroll);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 520, 480 });

    // Onglets : Characteristics + AutoMods.
    auto* tabs = new QTabWidget(this);
    tabs->addTab(splitter, tr("Characteristics"));
    tabs->addTab(buildAutoModsPage(), tr("AutoMods"));
    root->addWidget(tabs, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color:#7c8fa6; font-size:11px;");
    root->addWidget(m_statusLabel);

    // ── Connexions ──────────────────────────────────────────────────────
    connect(m_newBtn,    &QPushButton::clicked, this, [this]() {
        if (m_dirty && QMessageBox::question(this, tr("Nouveau"),
                tr("Modifications non sauvegardées — continuer ?")) != QMessageBox::Yes)
            return;
        newEmptyRecipe(m_doc ? m_doc->ecuId() : QString());
    });
    connect(m_openBtn,   &QPushButton::clicked, this, &DamosEditorPanel::openRecipeFile);
    connect(m_saveBtn,   &QPushButton::clicked, this, &DamosEditorPanel::saveRecipe);
    connect(m_saveAsBtn, &QPushButton::clicked, this, &DamosEditorPanel::saveRecipeAs);
    connect(m_addBtn,    &QPushButton::clicked, this, &DamosEditorPanel::onAddEntry);
    connect(m_dupBtn,    &QPushButton::clicked, this, &DamosEditorPanel::onDuplicateEntry);
    connect(m_delBtn,    &QPushButton::clicked, this, &DamosEditorPanel::onDeleteEntry);
    connect(m_detectBtn, &QPushButton::clicked, this, &DamosEditorPanel::detectFromRom);
    connect(m_xAxisCaptureBtn, &QPushButton::clicked, this,
            [this]() { captureFingerprintHere(); });
    connect(m_yAxisCaptureBtn, &QPushButton::clicked, this,
            [this]() { captureFingerprintHere(); });

    connect(m_entryTable, &QTableWidget::itemSelectionChanged,
            this, &DamosEditorPanel::onEntrySelectionChanged);

    // À chaque modif du formulaire, on rebascule vers writeFormToCurrent + dirty.
    auto markDirtyOnChange = [this]() {
        if (m_loadingForm) return;
        writeFormToCurrent();
        markDirty(true);
    };
    for (QLineEdit* w : { m_nameEdit, m_categoryEdit, m_addrEdit,
                          m_xAxisUnitEdit, m_yAxisUnitEdit,
                          m_xAxisFpEdit, m_yAxisFpEdit, m_dataUnitEdit }) {
        connect(w, &QLineEdit::textChanged, this, markDirtyOnChange);
    }
    connect(m_descEdit, &QTextEdit::textChanged, this, markDirtyOnChange);
    for (auto* w : { m_typeCombo, m_xAxisTypeCombo, m_yAxisTypeCombo, m_dataTypeCombo }) {
        connect(w, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [markDirtyOnChange](int){ markDirtyOnChange(); });
    }
    for (auto* w : { m_nxSpin, m_nySpin }) {
        connect(w, qOverload<int>(&QSpinBox::valueChanged), this,
                [markDirtyOnChange](int){ markDirtyOnChange(); });
    }
    for (auto* w : { m_xAxisFactorSpin, m_xAxisOffsetSpin, m_yAxisFactorSpin,
                     m_yAxisOffsetSpin, m_dataFactorSpin, m_dataOffsetSpin }) {
        connect(w, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [markDirtyOnChange](double){ markDirtyOnChange(); });
    }
    connect(m_ecuIdEdit, &QLineEdit::textChanged, this, [this](const QString& s) {
        m_recipe.ecuId = s.toStdString();
        markDirty(true);
    });

    setStatus(tr("Aucun recipe chargé — Nouveau ou Ouvrir."));
}

void DamosEditorPanel::setRecipe(ecu::DamosRecipe recipe, const QString& sourcePath) {
    m_recipe = std::move(recipe);
    m_path   = sourcePath;
    m_ecuIdEdit->setText(QString::fromStdString(m_recipe.ecuId));
    rebuildEntryTable();
    rebuildAutoModTable();
    if (!m_recipe.characteristics.empty())
        m_entryTable->selectRow(0);
    else
        loadCurrentToForm();
    if (!m_recipe.autoMods.empty() && m_autoModTable)
        m_autoModTable->selectRow(0);
    else
        loadCurrentAutoModToForm();
    markDirty(false);
    m_pathLabel->setText(m_path.isEmpty() ? tr("(non sauvegardé)") : m_path);
    setStatus(tr("%1 characteristic(s) chargée(s).").arg(m_recipe.characteristics.size()));
}

void DamosEditorPanel::newEmptyRecipe(const QString& ecuId) {
    ecu::DamosRecipe r;
    r.ecuId = ecuId.toStdString();
    setRecipe(std::move(r));
    m_path.clear();
    m_pathLabel->setText(tr("(non sauvegardé)"));
}

void DamosEditorPanel::openRecipeFile() {
    const QString path = QFileDialog::getOpenFileName(this,
        tr("Ouvrir un recipe open_damos"), {},
        tr("Recipe DAMOS (*.json);;Tous (*.*)"),
        nullptr, QFileDialog::DontUseNativeDialog);
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(tr("Impossible d'ouvrir %1").arg(path), true);
        return;
    }
    auto parsed = ecu::OpenDamos::parseRecipe(f.readAll().toStdString());
    if (!parsed) {
        setStatus(tr("Recipe invalide : %1").arg(QString::fromStdString(parsed.error())), true);
        return;
    }
    setRecipe(std::move(*parsed), path);
}

void DamosEditorPanel::saveRecipe() {
    if (m_path.isEmpty()) { saveRecipeAs(); return; }

    auto res = ecu::OpenDamos::saveRecipe(m_recipe, m_path);
    if (!res) {
        setStatus(QString::fromStdString(res.error()), true);
        return;
    }
    markDirty(false);
    setStatus(tr("Sauvegardé : %1").arg(m_path));
    emit recipeSaved(QString::fromStdString(m_recipe.ecuId), m_path);
}

void DamosEditorPanel::saveRecipeAs() {
    const QString ecuId = QString::fromStdString(m_recipe.ecuId);
    const QString defaultPath = QString("ressources/%1/open_damos.json")
                                    .arg(ecuId.isEmpty() ? QStringLiteral("custom") : ecuId);
    const QString path = QFileDialog::getSaveFileName(this,
        tr("Enregistrer recipe sous"), defaultPath,
        tr("Recipe DAMOS (*.json)"),
        nullptr, QFileDialog::DontUseNativeDialog);
    if (path.isEmpty()) return;
    m_path = path;
    m_pathLabel->setText(m_path);
    saveRecipe();
}

void DamosEditorPanel::rebuildEntryTable() {
    m_entryTable->setRowCount(static_cast<int>(m_recipe.characteristics.size()));
    for (int row = 0; row < static_cast<int>(m_recipe.characteristics.size()); ++row) {
        const auto& e = m_recipe.characteristics[static_cast<std::size_t>(row)];
        auto* nameItem = new QTableWidgetItem(QString::fromStdString(e.name));
        auto* typeItem = new QTableWidgetItem(typeLabel(e.type));
        auto* addrItem = new QTableWidgetItem(QString::fromStdString(e.defaultAddress));
        const QString dims = (e.type == ecu::DamosType::Map)
            ? QString("%1×%2").arg(e.dims.nx).arg(e.dims.ny)
            : (e.type == ecu::DamosType::Curve ? QString::number(e.dims.nx) : QStringLiteral("—"));
        auto* dimsItem = new QTableWidgetItem(dims);
        m_entryTable->setItem(row, 0, nameItem);
        m_entryTable->setItem(row, 1, typeItem);
        m_entryTable->setItem(row, 2, addrItem);
        m_entryTable->setItem(row, 3, dimsItem);
    }
}

int DamosEditorPanel::selectedRow() const {
    const auto rows = m_entryTable->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

void DamosEditorPanel::onEntrySelectionChanged() {
    loadCurrentToForm();
}

void DamosEditorPanel::loadCurrentToForm() {
    m_loadingForm = true;
    const int row = selectedRow();
    if (row < 0 || row >= static_cast<int>(m_recipe.characteristics.size())) {
        m_nameEdit->clear(); m_categoryEdit->clear(); m_descEdit->clear();
        m_addrEdit->clear(); m_nxSpin->setValue(0); m_nySpin->setValue(0);
        m_xAxisUnitEdit->clear(); m_yAxisUnitEdit->clear();
        m_xAxisFpEdit->clear(); m_yAxisFpEdit->clear();
        m_dataUnitEdit->clear();
        m_loadingForm = false;
        return;
    }
    const auto& e = m_recipe.characteristics[static_cast<std::size_t>(row)];
    m_nameEdit->setText(QString::fromStdString(e.name));
    m_typeCombo->setCurrentIndex(typeToCombo(e.type));
    m_categoryEdit->setText(QString::fromStdString(e.category));
    m_descEdit->setPlainText(QString::fromStdString(e.description));
    m_addrEdit->setText(QString::fromStdString(e.defaultAddress));
    m_nxSpin->setValue(e.dims.nx);
    m_nySpin->setValue(e.dims.ny);

    auto fillAxis = [](const ecu::DamosAxis* ax, QComboBox* tc, QLineEdit* ue,
                       QDoubleSpinBox* fs, QDoubleSpinBox* os, QLineEdit* fp) {
        if (!ax) { tc->setCurrentIndex(2); ue->clear(); fs->setValue(1.0);
                   os->setValue(0.0); fp->clear(); return; }
        tc->setCurrentIndex(dataTypeToCombo(ax->dataType));
        ue->setText(QString::fromStdString(ax->unit));
        fs->setValue(ax->factor);
        os->setValue(ax->offset);
        fp->setText(fpToCsv(ax->fingerprint));
    };
    const ecu::DamosAxis* xAx = e.axes.size() > 0 ? &e.axes[0] : nullptr;
    const ecu::DamosAxis* yAx = e.axes.size() > 1 ? &e.axes[1] : nullptr;
    fillAxis(xAx, m_xAxisTypeCombo, m_xAxisUnitEdit, m_xAxisFactorSpin,
             m_xAxisOffsetSpin, m_xAxisFpEdit);
    fillAxis(yAx, m_yAxisTypeCombo, m_yAxisUnitEdit, m_yAxisFactorSpin,
             m_yAxisOffsetSpin, m_yAxisFpEdit);

    m_dataTypeCombo->setCurrentIndex(dataTypeToCombo(e.data.dataType));
    m_dataUnitEdit->setText(QString::fromStdString(e.data.unit));
    m_dataFactorSpin->setValue(e.data.factor);
    m_dataOffsetSpin->setValue(e.data.offset);
    m_loadingForm = false;
}

void DamosEditorPanel::writeFormToCurrent() {
    const int row = selectedRow();
    if (row < 0 || row >= static_cast<int>(m_recipe.characteristics.size())) return;
    auto& e = m_recipe.characteristics[static_cast<std::size_t>(row)];
    e.name        = m_nameEdit->text().toStdString();
    e.type        = comboToType(m_typeCombo->currentIndex());
    e.category    = m_categoryEdit->text().toStdString();
    e.description = m_descEdit->toPlainText().toStdString();
    e.defaultAddress = m_addrEdit->text().toStdString();
    e.dims.nx = m_nxSpin->value();
    e.dims.ny = m_nySpin->value();

    e.axes.clear();
    auto pushAxis = [&](QComboBox* tc, QLineEdit* ue, QDoubleSpinBox* fs,
                        QDoubleSpinBox* os, QLineEdit* fp) {
        ecu::DamosAxis ax;
        ax.dataType = comboToDataType(tc->currentIndex());
        ax.unit     = ue->text().toStdString();
        ax.factor   = fs->value();
        ax.offset   = os->value();
        ax.fingerprint = csvToFp(fp->text());
        e.axes.push_back(std::move(ax));
    };
    pushAxis(m_xAxisTypeCombo, m_xAxisUnitEdit, m_xAxisFactorSpin,
             m_xAxisOffsetSpin, m_xAxisFpEdit);
    if (e.type == ecu::DamosType::Map) {
        pushAxis(m_yAxisTypeCombo, m_yAxisUnitEdit, m_yAxisFactorSpin,
                 m_yAxisOffsetSpin, m_yAxisFpEdit);
    }

    e.data.dataType = comboToDataType(m_dataTypeCombo->currentIndex());
    e.data.unit     = m_dataUnitEdit->text().toStdString();
    e.data.factor   = m_dataFactorSpin->value();
    e.data.offset   = m_dataOffsetSpin->value();

    // Met à jour la ligne dans le tableau sans déclencher reselectChanged.
    QSignalBlocker bl(m_entryTable);
    if (auto* it = m_entryTable->item(row, 0)) it->setText(QString::fromStdString(e.name));
    if (auto* it = m_entryTable->item(row, 1)) it->setText(typeLabel(e.type));
    if (auto* it = m_entryTable->item(row, 2)) it->setText(QString::fromStdString(e.defaultAddress));
    if (auto* it = m_entryTable->item(row, 3)) {
        const QString dims = (e.type == ecu::DamosType::Map)
            ? QString("%1×%2").arg(e.dims.nx).arg(e.dims.ny)
            : (e.type == ecu::DamosType::Curve ? QString::number(e.dims.nx) : QStringLiteral("—"));
        it->setText(dims);
    }
}

void DamosEditorPanel::onAddEntry() {
    ecu::DamosEntry e;
    e.name = "new_characteristic";
    e.type = ecu::DamosType::Map;
    e.data.dataType = ecu::DamosDataType::SWordBE;
    m_recipe.characteristics.push_back(std::move(e));
    rebuildEntryTable();
    m_entryTable->selectRow(static_cast<int>(m_recipe.characteristics.size()) - 1);
    markDirty(true);
}

void DamosEditorPanel::onDuplicateEntry() {
    const int row = selectedRow();
    if (row < 0) return;
    auto copy = m_recipe.characteristics[static_cast<std::size_t>(row)];
    copy.name += "_copy";
    m_recipe.characteristics.push_back(std::move(copy));
    rebuildEntryTable();
    m_entryTable->selectRow(static_cast<int>(m_recipe.characteristics.size()) - 1);
    markDirty(true);
}

void DamosEditorPanel::onDeleteEntry() {
    const int row = selectedRow();
    if (row < 0) return;
    m_recipe.characteristics.erase(m_recipe.characteristics.begin() + row);
    rebuildEntryTable();
    if (!m_recipe.characteristics.empty()) {
        const int last = static_cast<int>(m_recipe.characteristics.size()) - 1;
        m_entryTable->selectRow(row < last ? row : last);
    }
    markDirty(true);
}

void DamosEditorPanel::detectFromRom() {
    if (!m_doc || !m_doc->isLoaded()) {
        setStatus(tr("Chargez une ROM avant de détecter."), true);
        return;
    }
    auto span = constByteSpan(m_doc->rom());
    auto candidates = ecu::findMaps(span);

    int added = 0;
    for (const auto& c : candidates) {
        ecu::DamosEntry e;
        e.name = "detected_" + std::to_string(c.address);
        e.type = ecu::DamosType::Map;
        e.dims.nx = c.nx;
        e.dims.ny = c.ny;
        e.defaultAddress = QString("0x%1").arg(c.address, 6, 16, QChar('0'))
                               .toUpper().replace("0X", "0x").toStdString();
        e.data.dataType = ecu::DamosDataType::SWordBE;
        // 2 axes vides — l'utilisateur capturera l'empreinte ensuite.
        e.axes.push_back({});
        e.axes.push_back({});
        m_recipe.characteristics.push_back(std::move(e));
        ++added;
    }
    rebuildEntryTable();
    markDirty(added > 0);
    setStatus(tr("%1 candidate(s) ajoutée(s) (à nommer et empreinter).").arg(added));
}

quint32 DamosEditorPanel::currentEntryAddress() const {
    const int row = selectedRow();
    if (row < 0) return 0;
    return parseHex(QString::fromStdString(
        m_recipe.characteristics[static_cast<std::size_t>(row)].defaultAddress));
}

void DamosEditorPanel::captureFingerprintHere() {
    if (!m_doc || !m_doc->isLoaded()) {
        setStatus(tr("Aucune ROM chargée."), true);
        return;
    }
    const int row = selectedRow();
    if (row < 0) {
        setStatus(tr("Sélectionnez une characteristic."), true);
        return;
    }
    const quint32 addr = currentEntryAddress();
    if (addr == 0) {
        setStatus(tr("Adresse vide ou invalide."), true);
        return;
    }
    auto md = ecu::readMapData(constByteSpan(m_doc->rom()), addr);
    if (!md) {
        setStatus(tr("Lecture map impossible : %1").arg(QString::fromStdString(md.error())), true);
        return;
    }
    QStringList xs, ys;
    for (auto v : md->xAxis) xs << QString::number(v);
    for (auto v : md->yAxis) ys << QString::number(v);
    m_xAxisFpEdit->setText(xs.join(", "));
    m_yAxisFpEdit->setText(ys.join(", "));
    m_nxSpin->setValue(md->nx);
    m_nySpin->setValue(md->ny);
    setStatus(tr("Empreintes capturées : X=%1 valeurs, Y=%2 valeurs.")
                  .arg(xs.size()).arg(ys.size()));
}

void DamosEditorPanel::setStatus(const QString& msg, bool error) {
    m_statusLabel->setStyleSheet(error ? "color:#ef4444; font-size:11px;"
                                       : "color:#7c8fa6; font-size:11px;");
    m_statusLabel->setText(msg);
}

void DamosEditorPanel::markDirty(bool dirty) {
    m_dirty = dirty;
    if (dirty)
        m_pathLabel->setText((m_path.isEmpty() ? tr("(non sauvegardé)") : m_path) + " *");
    else
        m_pathLabel->setText(m_path.isEmpty() ? tr("(non sauvegardé)") : m_path);
}

} // namespace ecu_studio

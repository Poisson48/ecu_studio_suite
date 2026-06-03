// damos_editor_panel_automods.cpp — page « AutoMods » de l'éditeur DAMOS
// (2e onglet) : liste des patches embarqués, formulaire d'édition et
// ajout/duplication/suppression. Extrait de damos_editor_panel.cpp.

#include "damos_editor_panel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ecu_studio {

namespace {

QString fmtHexBytes(const std::vector<std::uint8_t>& b) {
    QStringList parts;
    parts.reserve(static_cast<int>(b.size()));
    char buf[4];
    for (auto v : b) { std::snprintf(buf, sizeof(buf), "%02X", v); parts << buf; }
    return parts.join(' ');
}

std::vector<std::uint8_t> parseHexBytesFromUi(const QString& s) {
    std::vector<std::uint8_t> out;
    std::string acc;
    for (QChar c : s) {
        if (c.isLetterOrNumber() && c.toUpper().unicode() <= 'F') {
            const char ch = c.toLatin1();
            if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F')) {
                acc.push_back(ch);
                if (acc.size() == 2) {
                    out.push_back(static_cast<std::uint8_t>(std::stoul(acc, nullptr, 16)));
                    acc.clear();
                }
            }
        }
    }
    return out;
}

QString fmtAutoModSummary(const ecu::DamosAutoMod& a) {
    switch (a.type) {
        case ecu::DamosAutoModType::Pattern:
            return QString("pat: %1→%2 (%3o)")
                .arg(QString::fromStdString(std::string(a.search.begin(),
                                                        a.search.begin() +
                                                        std::min<std::size_t>(a.search.size(), 0))))
                .arg(QString())
                .arg(a.search.size());
        case ecu::DamosAutoModType::Address: {
            const QString addr = a.address
                ? QString("0x%1").arg(*a.address, 0, 16).toUpper()
                : QString("?");
            return QString("addr %1 (%2o)").arg(addr).arg(a.replace.size());
        }
        case ecu::DamosAutoModType::Unknown:
        default:
            return QStringLiteral("—");
    }
}

} // namespace

QWidget* DamosEditorPanel::buildAutoModsPage() {
    auto* page = new QWidget(this);
    auto* lay  = new QHBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);

    auto* splitter = new QSplitter(Qt::Horizontal, page);

    // ── Liste ──
    auto* listBox = new QGroupBox(tr("AutoMods (patches embarqués)"), page);
    auto* listLay = new QVBoxLayout(listBox);
    auto* actRow  = new QHBoxLayout;
    m_addAutoModBtn = new QPushButton(tr("+ Ajouter"), page);
    m_dupAutoModBtn = new QPushButton(tr("Dupliquer"), page);
    m_delAutoModBtn = new QPushButton(tr("Supprimer"), page);
    actRow->addWidget(m_addAutoModBtn);
    actRow->addWidget(m_dupAutoModBtn);
    actRow->addWidget(m_delAutoModBtn);
    actRow->addStretch();
    listLay->addLayout(actRow);

    m_autoModTable = new QTableWidget(page);
    m_autoModTable->setColumnCount(4);
    m_autoModTable->setHorizontalHeaderLabels({ tr("ID"), tr("Type"), tr("Détail"), tr("Description") });
    m_autoModTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_autoModTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_autoModTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_autoModTable->verticalHeader()->setVisible(false);
    m_autoModTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_autoModTable->setColumnWidth(0, 200);
    m_autoModTable->setColumnWidth(1, 80);
    m_autoModTable->setColumnWidth(2, 200);
    m_autoModTable->setColumnWidth(3, 280);
    listLay->addWidget(m_autoModTable);
    splitter->addWidget(listBox);

    // ── Formulaire ──
    auto* formScroll = new QScrollArea(page);
    formScroll->setWidgetResizable(true);
    formScroll->setFrameShape(QFrame::NoFrame);
    auto* formHost = new QWidget;
    auto* formLay  = new QVBoxLayout(formHost);

    auto* gen = new QGroupBox(tr("Général"), formHost);
    auto* genF = new QFormLayout(gen);
    m_amIdEdit   = new QLineEdit(gen);
    m_amIdEdit->setPlaceholderText(tr("egr_disable, popbang_enable…"));
    m_amTypeCombo = new QComboBox(gen);
    m_amTypeCombo->addItems({ tr("Pattern (search/replace)"), tr("Address (écrire à adresse)") });
    m_amDescEdit = new QLineEdit(gen);
    m_amNoteEdit = new QLineEdit(gen);
    m_amNoteEdit->setPlaceholderText(tr("Notes, source, etc."));
    genF->addRow(tr("ID :"),          m_amIdEdit);
    genF->addRow(tr("Type :"),        m_amTypeCombo);
    genF->addRow(tr("Description :"), m_amDescEdit);
    genF->addRow(tr("Note :"),        m_amNoteEdit);
    formLay->addWidget(gen);

    auto* payload = new QGroupBox(tr("Payload"), formHost);
    auto* pf = new QFormLayout(payload);
    m_amAddressEdit = new QLineEdit(payload);
    m_amAddressEdit->setPlaceholderText(tr("0x1A2B3C  (Address uniquement)"));
    m_amSearchEdit  = new QLineEdit(payload);
    m_amSearchEdit->setPlaceholderText(tr("AA BB CC DD  (Pattern uniquement)"));
    m_amReplaceEdit = new QLineEdit(payload);
    m_amReplaceEdit->setPlaceholderText(tr("00 00 00 00  (replace ou bytes selon Type)"));
    m_amRestoreEdit = new QLineEdit(payload);
    m_amRestoreEdit->setPlaceholderText(tr("AA BB CC DD  (optionnel — bytes de restauration)"));
    pf->addRow(tr("Address :"),      m_amAddressEdit);
    pf->addRow(tr("Search :"),       m_amSearchEdit);
    pf->addRow(tr("Replace/Bytes :"),m_amReplaceEdit);
    pf->addRow(tr("Restore :"),      m_amRestoreEdit);
    formLay->addWidget(payload);

    formLay->addStretch();
    formScroll->setWidget(formHost);
    splitter->addWidget(formScroll);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 520, 480 });
    lay->addWidget(splitter);

    // ── Connexions ──
    connect(m_addAutoModBtn, &QPushButton::clicked, this, &DamosEditorPanel::onAddAutoMod);
    connect(m_dupAutoModBtn, &QPushButton::clicked, this, &DamosEditorPanel::onDuplicateAutoMod);
    connect(m_delAutoModBtn, &QPushButton::clicked, this, &DamosEditorPanel::onDeleteAutoMod);
    connect(m_autoModTable, &QTableWidget::itemSelectionChanged,
            this, &DamosEditorPanel::onAutoModSelectionChanged);

    auto markDirtyOnChange = [this]() {
        if (m_loadingAutoMod) return;
        writeAutoModFormToCurrent();
        markDirty(true);
    };
    for (QLineEdit* w : { m_amIdEdit, m_amDescEdit, m_amNoteEdit,
                          m_amAddressEdit, m_amSearchEdit,
                          m_amReplaceEdit, m_amRestoreEdit }) {
        connect(w, &QLineEdit::textChanged, this, markDirtyOnChange);
    }
    connect(m_amTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [markDirtyOnChange](int){ markDirtyOnChange(); });

    return page;
}

int DamosEditorPanel::selectedAutoModRow() const {
    if (!m_autoModTable) return -1;
    const auto rows = m_autoModTable->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

void DamosEditorPanel::rebuildAutoModTable() {
    if (!m_autoModTable) return;
    m_autoModTable->setRowCount(static_cast<int>(m_recipe.autoMods.size()));
    for (int row = 0; row < static_cast<int>(m_recipe.autoMods.size()); ++row) {
        const auto& a = m_recipe.autoMods[static_cast<std::size_t>(row)];
        QString type;
        switch (a.type) {
            case ecu::DamosAutoModType::Pattern: type = "pattern"; break;
            case ecu::DamosAutoModType::Address: type = "address"; break;
            default: type = "?"; break;
        }
        m_autoModTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(a.id)));
        m_autoModTable->setItem(row, 1, new QTableWidgetItem(type));
        m_autoModTable->setItem(row, 2, new QTableWidgetItem(fmtAutoModSummary(a)));
        m_autoModTable->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(a.description)));
    }
}

void DamosEditorPanel::onAutoModSelectionChanged() {
    loadCurrentAutoModToForm();
}

void DamosEditorPanel::loadCurrentAutoModToForm() {
    if (!m_autoModTable) return;
    m_loadingAutoMod = true;
    const int row = selectedAutoModRow();
    if (row < 0 || row >= static_cast<int>(m_recipe.autoMods.size())) {
        m_amIdEdit->clear(); m_amDescEdit->clear(); m_amNoteEdit->clear();
        m_amAddressEdit->clear(); m_amSearchEdit->clear();
        m_amReplaceEdit->clear(); m_amRestoreEdit->clear();
        m_amTypeCombo->setCurrentIndex(0);
        m_loadingAutoMod = false;
        return;
    }
    const auto& a = m_recipe.autoMods[static_cast<std::size_t>(row)];
    m_amIdEdit->setText(QString::fromStdString(a.id));
    m_amTypeCombo->setCurrentIndex(a.type == ecu::DamosAutoModType::Address ? 1 : 0);
    m_amDescEdit->setText(QString::fromStdString(a.description));
    m_amNoteEdit->setText(QString::fromStdString(a.note));
    m_amAddressEdit->setText(a.address
        ? QString("0x%1").arg(*a.address, 6, 16, QChar('0')).toUpper().replace("0X", "0x")
        : QString());
    m_amSearchEdit->setText(fmtHexBytes(a.search));
    m_amReplaceEdit->setText(fmtHexBytes(a.replace));
    m_amRestoreEdit->setText(fmtHexBytes(a.restore));
    m_loadingAutoMod = false;
}

void DamosEditorPanel::writeAutoModFormToCurrent() {
    const int row = selectedAutoModRow();
    if (row < 0 || row >= static_cast<int>(m_recipe.autoMods.size())) return;
    auto& a = m_recipe.autoMods[static_cast<std::size_t>(row)];
    a.id          = m_amIdEdit->text().toStdString();
    a.type        = m_amTypeCombo->currentIndex() == 1
                  ? ecu::DamosAutoModType::Address
                  : ecu::DamosAutoModType::Pattern;
    a.description = m_amDescEdit->text().toStdString();
    a.note        = m_amNoteEdit->text().toStdString();
    a.search      = parseHexBytesFromUi(m_amSearchEdit->text());
    a.replace     = parseHexBytesFromUi(m_amReplaceEdit->text());
    a.restore     = parseHexBytesFromUi(m_amRestoreEdit->text());

    const QString addrTxt = m_amAddressEdit->text().trimmed();
    if (addrTxt.isEmpty()) {
        a.address.reset();
    } else {
        QString t = addrTxt;
        if (t.startsWith("0x", Qt::CaseInsensitive)) t = t.mid(2);
        bool ok = false;
        const std::uint64_t v = t.toULongLong(&ok, 16);
        if (ok) a.address = v; else a.address.reset();
    }

    // Reflète dans le tableau sans déclencher la sélection.
    QSignalBlocker bl(m_autoModTable);
    if (auto* it = m_autoModTable->item(row, 0)) it->setText(QString::fromStdString(a.id));
    if (auto* it = m_autoModTable->item(row, 1)) {
        it->setText(a.type == ecu::DamosAutoModType::Address ? "address" : "pattern");
    }
    if (auto* it = m_autoModTable->item(row, 2)) it->setText(fmtAutoModSummary(a));
    if (auto* it = m_autoModTable->item(row, 3)) it->setText(QString::fromStdString(a.description));
}

void DamosEditorPanel::onAddAutoMod() {
    ecu::DamosAutoMod a;
    a.id   = "new_automod";
    a.type = ecu::DamosAutoModType::Pattern;
    m_recipe.autoMods.push_back(std::move(a));
    rebuildAutoModTable();
    m_autoModTable->selectRow(static_cast<int>(m_recipe.autoMods.size()) - 1);
    markDirty(true);
}

void DamosEditorPanel::onDuplicateAutoMod() {
    const int row = selectedAutoModRow();
    if (row < 0) return;
    auto copy = m_recipe.autoMods[static_cast<std::size_t>(row)];
    copy.id += "_copy";
    m_recipe.autoMods.push_back(std::move(copy));
    rebuildAutoModTable();
    m_autoModTable->selectRow(static_cast<int>(m_recipe.autoMods.size()) - 1);
    markDirty(true);
}

void DamosEditorPanel::onDeleteAutoMod() {
    const int row = selectedAutoModRow();
    if (row < 0) return;
    m_recipe.autoMods.erase(m_recipe.autoMods.begin() + row);
    rebuildAutoModTable();
    if (!m_recipe.autoMods.empty()) {
        const int last = static_cast<int>(m_recipe.autoMods.size()) - 1;
        m_autoModTable->selectRow(row < last ? row : last);
    }
    markDirty(true);
}

} // namespace ecu_studio

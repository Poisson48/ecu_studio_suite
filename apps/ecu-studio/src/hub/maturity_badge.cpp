#include "hub/maturity_badge.h"

#include <QString>

namespace ecu_studio {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
MaturityBadge::MaturityBadge(Maturity maturity, QWidget* parent)
    : QLabel(parent), m_maturity(maturity) {
    setObjectName(QStringLiteral("maturityBadge"));
    // Centré, taille au plus juste : la pilule épouse le texte.
    setAlignment(Qt::AlignCenter);
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    applyStyle();
}

MaturityBadge::MaturityBadge(QWidget* parent)
    : MaturityBadge(Maturity::Incoming, parent) {}

// ─────────────────────────────────────────────────────────────────────────────
// API publique
// ─────────────────────────────────────────────────────────────────────────────
void MaturityBadge::setMaturity(Maturity maturity) {
    if (maturity == m_maturity && !text().isEmpty()) {
        return;  // déjà à jour
    }
    m_maturity = maturity;
    applyStyle();
}

QString MaturityBadge::labelFor(Maturity maturity) {
    switch (maturity) {
        case Maturity::Proven:   return tr("Proven");
        case Maturity::Beta:     return tr("Beta");
        case Maturity::Incoming: return tr("Incoming");
    }
    return tr("Incoming");  // défaut sûr (et valeurs hors-énum éventuelles)
}

// ─────────────────────────────────────────────────────────────────────────────
// Interne
// ─────────────────────────────────────────────────────────────────────────────
const char* MaturityBadge::colorFor(Maturity maturity) {
    switch (maturity) {
        case Maturity::Proven:   return Palette::kBadgeProven;
        case Maturity::Beta:     return Palette::kBadgeBeta;
        case Maturity::Incoming: return Palette::kBadgeIncoming;
    }
    return Palette::kBadgeIncoming;
}

void MaturityBadge::applyStyle() {
    setText(labelFor(m_maturity));

    const QString bg = QString::fromLatin1(colorFor(m_maturity));
    // Pilule arrondie, texte blanc, fond plein coloré. On cible par objectName
    // pour ne pas styler d'autres QLabel par cascade.
    setStyleSheet(QStringLiteral(
                      "QLabel#maturityBadge {"
                      "  background-color: %1;"
                      "  color: #ffffff;"
                      "  border-radius: 9px;"
                      "  padding: 1px 8px;"
                      "  font-size: 10px;"
                      "  font-weight: 600;"
                      "}")
                      .arg(bg));
}

}  // namespace ecu_studio

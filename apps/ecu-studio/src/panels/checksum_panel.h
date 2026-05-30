#pragma once
#include <QWidget>
#include <QString>
#include <cstdint>

class QComboBox;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextEdit;
class QLabel;

namespace ecu_studio {

class RomDocument;

// Panel « Checksum » : vérifie et corrige les checksums d'une ROM ECU.
//
// Il n'existe pas (encore) de moteur de checksum porté dans la suite. Ce panel
// embarque donc une petite implémentation, volontairement générique et
// documentée, ciblant la famille Bosch EDC16 (EDC16C34, EDC16U/UC, …).
//
// Comme les offsets exacts des checksums EDC16 ne sont pas tous connus, le
// panel offre un cadre paramétrable : l'utilisateur fixe la (les) région(s) à
// sommer (début/fin) et l'offset de stockage du checksum, avec des valeurs par
// défaut « raisonnables » EDC16. Les emplacements spécifiques à une ECU donnée
// sont marqués TODO_REVERSE dans le .cpp.
class ChecksumPanel : public QWidget {
    Q_OBJECT
public:
    // Constructeur principal : reçoit le RomDocument partagé.
    explicit ChecksumPanel(RomDocument* doc, QWidget* parent = nullptr);
    // Surcharge de commodité (main_window construit le panel avec son parent
    // seul). Délègue au constructeur principal avec un document nul.
    explicit ChecksumPanel(QWidget* parent = nullptr);

    // Algorithmes de checksum supportés.
    enum class Algo {
        MppsCrc16Arc, // VRAI checksum MPPS reverse-engineered (CRC-16/ARC EDC16) — défaut
        Sum32,        // somme additive 32 bits des mots (générique)
        Sum16,        // somme additive 16 bits des half-words (générique)
        Xor32,        // XOR 32 bits des mots (générique)
    };

public slots:
    // Calcule les checksums et affiche calculé vs stocké dans le tableau.
    void verify();
    // Corrige : écrit le checksum calculé à l'offset de stockage dans la ROM
    // mutable du document, puis appelle markModified(). Appelé par le menu
    // Outils de main_window.
    void runCorrection();

private:
    void buildUi();
    void log(const QString& msg, bool error = false);
    void onRomLoaded();
    void updateEnabled();
    // Active/désactive les champs « génériques » selon l'algo sélectionné
    // (le mode MPPS utilise une région fixe auto-détectée par taille).
    void onAlgoChanged();

    // ── Mode MPPS CRC-16/ARC (moteur réel ecu::ChecksumEngine) ───────────────
    // Vérifie via le moteur réel (région auto-détectée 32k/64k par la taille).
    void verifyMpps();
    // Corrige via le moteur réel puis re-vérifie.
    void runCorrectionMpps();
    // true si l'algo courant est le mode MPPS.
    bool isMppsMode() const;

    // ── Helpers d'analyse ────────────────────────────────────────────────────
    // Lit les bornes saisies par l'utilisateur. Retourne false (et journalise)
    // si invalides vis-à-vis de la ROM courante.
    bool readParams(qsizetype& start, qsizetype& end,
                    qsizetype& storeOffset, bool& bigEndian, Algo& algo);

    // Calcule le checksum sur [start, end) selon l'algorithme demandé.
    static quint32 computeChecksum(const QByteArray& rom, qsizetype start,
                                   qsizetype end, Algo algo, bool bigEndian);
    // Lit le checksum stocké (32 bits) à storeOffset.
    static quint32 readStored(const QByteArray& rom, qsizetype storeOffset,
                              bool bigEndian);
    // Petit CRC-32 (polynôme IEEE 0xEDB88320) sur [start, end), fourni en option
    // d'affichage.
    static quint32 computeCrc32(const QByteArray& rom, qsizetype start,
                                qsizetype end);

    static quint32 parseHex(const QString& text, bool* ok = nullptr);

    RomDocument* m_doc{nullptr};

    QComboBox*    m_algoCombo{nullptr};
    QComboBox*    m_endianCombo{nullptr};
    QLineEdit*    m_startEdit{nullptr};
    QLineEdit*    m_endEdit{nullptr};
    QLineEdit*    m_storeEdit{nullptr};
    QPushButton*  m_verifyBtn{nullptr};
    QPushButton*  m_correctBtn{nullptr};
    QTableWidget* m_table{nullptr};
    QTextEdit*    m_log{nullptr};
    QLabel*       m_romLabel{nullptr};
};

} // namespace ecu_studio

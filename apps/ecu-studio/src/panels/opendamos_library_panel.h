#pragma once
// ─── OpenDamosLibraryPanel ───────────────────────────────────────────────────
// Panneau « Bibliothèque » : récupère la bibliothèque de recettes OpenDAMOS
// publiée sur GitHub (manifest index.json) pour que l'utilisateur dispose des
// 127+ recettes ECU sans recompiler l'application — esprit « WinOLS libre ».
//
// Flux :
//   1. GET du manifest JSON via HTTPS (QNetworkAccessManager, CA système).
//      Shape : { "raw": "<base url>", "count": N, "recipes": [ { ecu, path,
//      name, maturity, byteOrder, maps, comAxis, source }, ... ] }.
//   2. Affichage filtrable dans un QTableWidget (ECU · Nom · Maturité · Endian ·
//      Cartes · Source), avec une pilule de maturité colorée et un marquage des
//      recettes déjà installées (présentes sous OpenDamos::userRecipeDir()).
//   3. « Installer » sur une ligne : GET <raw>+<path> puis écriture dans
//      userRecipeDir()/<ecu>/open_damos.json (mkpath). loadRecipe() les retrouve.
//
// Les erreurs réseau sont gérées proprement (ligne de statut, aucun crash).
#include <QWidget>
#include <QString>
#include <QVector>

class QLineEdit;
class QPushButton;
class QCheckBox;
class QTableWidget;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;

namespace ecu_studio {

class RomDocument;

class OpenDamosLibraryPanel : public QWidget {
    Q_OBJECT
public:
    // Constructeur principal : reçoit le RomDocument partagé (utilisé pour
    // activer « Trouver pour ma ROM » quand une ROM est chargée).
    explicit OpenDamosLibraryPanel(RomDocument* doc, QWidget* parent = nullptr);
    // Surcharge de commodité (parent seul, document nul).
    explicit OpenDamosLibraryPanel(QWidget* parent = nullptr);

public slots:
    // (Re)télécharge le manifest depuis GitHub et reconstruit le tableau.
    void refresh();

protected:
    // Premier affichage : déclenche un refresh automatique si le manifest n'a
    // pas encore été chargé.
    void showEvent(QShowEvent* e) override;

private:
    // Une entrée du manifest (une recette publiée).
    struct Recipe {
        QString ecu;
        QString path;       // relatif à `raw`
        QString name;
        QString maturity;   // proven / beta / incoming
        QString byteOrder;  // big / little
        int     maps = 0;
        bool    comAxis = false;
        QString source;
    };

    void buildUi();
    void setStatus(const QString& msg, bool error = false);

    // Scanne le DOSSIER LOCAL des recettes (OpenDamos::userRecipeDir()) — marche
    // hors-ligne. Source par défaut ; l'import GitHub est optionnel.
    void loadLocal();
    void openLocalFolder();

    // Parse le corps JSON du manifest dans m_raw / m_recipes, puis peuple la table.
    void parseManifest(const QByteArray& body);
    void populateTable();
    void applyFilter();

    // true si la recette <ecu> est déjà installée dans le cache utilisateur.
    static bool isInstalled(const QString& ecu);

    // Installe la recette de la ligne donnée (GET <raw>+<path> → écriture disque).
    void installRow(int row);
    // Installe la recette actuellement sélectionnée (bouton « Installer »).
    void installSelected();

    // Pilule de maturité (QSS inline) posée dans la cellule « Maturité ».
    static QWidget* makeMaturityPill(const QString& maturity, QWidget* parent);

    RomDocument* m_doc{nullptr};
    QNetworkAccessManager* m_nam{nullptr};

    QString          m_raw;        // base URL du manifest (champ "raw")
    QVector<Recipe>  m_recipes;
    bool             m_loaded{false};

    QLineEdit*    m_filterEdit{nullptr};
    QCheckBox*    m_ecuOnly{nullptr};   // « Cet ECU uniquement » (coché par défaut)
    QPushButton*  m_refreshBtn{nullptr};
    QPushButton*  m_installBtn{nullptr};
    QPushButton*  m_findBtn{nullptr};
    QTableWidget* m_table{nullptr};
    QLabel*       m_statusLabel{nullptr};
};

} // namespace ecu_studio

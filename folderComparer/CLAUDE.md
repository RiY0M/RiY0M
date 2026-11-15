# folderComparer

Application de bureau (C++20 + Qt 6 Widgets) pour comparer deux gros dossiers
(A et B) et synchroniser leurs différences, avec annulation de chaque action.

## Ce que fait l'application

1. **Sélection des dossiers** : deux champs « Folder A » / « Folder B » avec bouton *Browse...*.
2. **Validation** avant comparaison :
   - champ vide : `Invalid folder` / `Folder must not be empty.`
   - dossier inexistant : `Invalid folder` / `Folder does not exist.`
   - même dossier (y compris via une autre écriture du chemin) : `Invalid folders` / `Folder A and Folder B are the same folder.`
   - les espaces autour du chemin sont ignorés.
3. **Options de comparaison**
   - *Recursive* (coché par défaut) : inclut tous les sous-dossiers ; les dossiers symboliques ne sont pas parcourus. Sans *Recursive*, seuls les fichiers à la racine sont comparés (aucun dossier).
   - *Deep comparison* (décoché par défaut) : compare le contenu octet par octet ; sinon seulement nom + taille.
4. **Comparaison en tâche de fond** avec barre de progression (feuille de style, politique de taille
   `Expanding` : elle remplit la hauteur de la ligne, comme les boutons), suivie de la progression en vraie
   fraction (`FractionLabel` : courant au-dessus d'un trait, total en dessous) et du pourcentage
   (`progressPercent`). À la fin, le texte reste tel que le scan l'a laissé (n sur n, 100 %). *Entrée* dans un
   champ de dossier lance la comparaison ; la fenêtre reste réactive, la fermer annule la comparaison.
5. **Classement** de chaque fichier **et dossier** de A ∪ B : `Identical`, `Different`, `Only A`, `Only B`.
   Un dossier présent d'un seul côté est *Only A/B* ; un dossier présent des deux côtés est *Different*
   s'il contient une différence, *Identical* sinon. Listes triées par chaîne native (`fc::pathLess`).
6. **Résumé** `Total`, `Identical`, `All differences`, `Different`, `Only A`, `Only B`, chacun au format
   `E(entrées) D(dossiers) F(fichiers)` avec E = D + F, disposés ainsi :
   `Total | All differences | Only A`, `Identical | Different | Only B`, puis `Skipped` sous *Identical*.
   **Les labels de différences et *Skipped* sont les boutons de filtre** (le filtre actif a un fond coloré, sans
   autre marque ; défaut : *All differences* = Different puis Only A puis Only B). *Total* et *Identical* sont des
   boutons au même cadre mais ignorant la souris, pour que tous les textes s'alignent. *Skipped* liste toutes les lignes
   passées (chemin complet, triées) et compte ces lignes plus le contenu des dossiers passés.
   **Les compteurs suivent les actions de l'application** (`LiveCounts`) : l'état de la comparaison (côtés où
   existe chaque entrée, contenu identique ou non) est rejoué avec les actions encore appliquées puis reclassé.
   Copie X → Y : la cible devient identique (avec tout le contenu d'un dossier) et les dossiers parents créés
   existent des deux côtés ; suppression : l'entrée disparaît de ce côté (un *Different* devient *Only* de l'autre
   côté, un *Only* disparaît du total) ; un dossier commun redevient *Identical* quand plus rien ne diffère dedans ;
   `Skip` ne change rien ; `Undo` retire simplement l'action du rejeu. Les lignes, elles, gardent leur catégorie
   d'origine (avec l'action affichée) pour pouvoir être annulées.
7. **Liste** : une ligne colorée par fichier différent ; **un dossier absent de l'autre côté est une seule ligne**
   (avec son contenu `D(n) F(n)`), dépliable avec la flèche (flèches natives Qt) en sous-lignes
   (dossiers d'abord, puis fichiers), récursivement ; l'encadré entier des sous-lignes est indenté.
   Ordre dans une ligne : flèche, statut, icône dossier, nom, contenu du dossier, boutons.
   **Couleurs** : Different ambre `#B88600`, Only A orange `#E07000`, Only B terre cuite `#A0522D`
   (dégradé chaud des différences) ; COPIED vert, DELETED rouge `#CC0000` (le rouge est réservé à la
   suppression), SKIPPED gris.
   **Clic sur le nom** : ouvre le fichier / dossier avec l'application par défaut. **Icône dossier** à gauche du nom :
   l'affiche sélectionné dans l'Explorateur (`explorer.exe /select,`, Finder `open -R`, sinon le dossier parent).
   Si l'élément existe des deux côtés (Different, ou après une copie), un petit menu demande la version A ou B ;
   la présence est vérifiée sur le disque au moment du clic (`ComparisonSession::existingLocations`).
   Les boutons de ligne et de filtre ne prennent pas le focus ; après chaque action le focus va à la liste.
8. **Actions par ligne** (fichier ou dossier entier) :
   - Different : `B → A`, `A → B`, `Delete A`, `Delete B`, `Delete both`, `Skip` ;
   - Only A : `Copy → B`, `Delete A`, `Skip` ; Only B : `Copy → A`, `Delete B`, `Skip`.
   `Skip` (« vu, rien à faire ») ne touche à aucun fichier : il marque seulement la ligne.
   Chaque bouton a la couleur de la ligne qu'il produit (`commandStyle`) : copie verte, suppression rouge,
   skip gris ; `Undo` garde le style par défaut.
   `Delete both` supprime les deux versions en une seule action (tout ou rien, annulable d'un coup).
   La ligne affiche ensuite `COPIED → B`, `DELETED A`, `DELETED A+B`, `SKIPPED`… avec un bouton `Undo` ; le contenu d'un dossier traité
   affiche la même action, sans boutons. Une ligne n'a pas d'action possible tant qu'un dossier parent ou un
   descendant a déjà une action (garantit que chaque action est annulable indépendamment).
9. **Annulation** : `Undo` sur la ligne, ou bouton *Undo* global / Ctrl+Z (dernière action d'abord).
   **Rétablir** : bouton *Redo* / Ctrl+Y, réapplique la dernière action annulée (par *Undo* global ou de ligne) ;
   toute nouvelle action, nouvelle comparaison ou réinitialisation vide la liste ; un *Redo* qui échoue (fichiers
   changés entre-temps) reste disponible.
   Rien n'est détruit tant qu'une action est annulable : un élément supprimé, ou écrasé par une copie, est
   déplacé dans `<dossier comparé>/.folderComparer-trash/<jeton de session>/<id>/` (même arborescence ⇒ simple
   renommage). Cette corbeille n'est jamais listée. Elle est vidée (actions définitives) à une nouvelle
   comparaison et à la fermeture, après confirmation si elle contient des données récupérables.
   Annuler une copie supprime aussi les dossiers parents qu'elle avait créés.
10. **Copie** : crée les dossiers parents, conserve les dates de modification, passe par `<cible>.fc-partial`
    puis un renommage (jamais de cible tronquée).
11. **Erreurs** : `Copy error` / `Delete error` / `Undo error` ; un fichier illisible pendant la comparaison est
    classé `Different` et signalé. Une action ratée n'est pas enregistrée (on peut réessayer).
12. **CLI** `folder_comparer_cli [--deep] [--no-recursive] [--summary] <A> <B>` : `statut<TAB>chemin/relatif` par
    fichier (identical, different, only_a, only_b), `--summary` affiche les compteurs E/D/F.
    Code retour 0 = aucune différence de fichier, 1 = différences, 2 = erreur.

## Architecture

```
├── CMakeLists.txt / CMakePresets.json
├── cmake/CompilerWarnings.cmake   # warnings max + -Werror / /WX
├── cmake/Sanitizers.cmake         # ASan + UBSan
├── .clang-tidy / .clang-format
├── src/core/        # bibliothèque fc_core : AUCUNE dépendance (std uniquement)
│   ├── ComparisonResult   # fichiers + dossiers par catégorie, EntryCounts (E/D/F)
│   ├── FileManager        # listEntries, filesAreIdentical, copyEntry, moveEntry, formatSize, pathLess
│   ├── FolderManager      # compare (progress + std::stop_token), normalize, validateFolders
│   ├── ActionHistory      # copie / suppression annulables, corbeille, undo, discard
│   ├── LiveCounts         # compteurs E/D/F recalculés avec les actions appliquées
│   ├── Presentation       # libellés, couleurs, compteurs, filtres, commandes de ligne
│   └── ComparisonSession  # état de l'UI : arbre de lignes, dépliage, actions, règles de blocage
├── src/gui/         # Qt 6 Widgets : MainWindow ne fait que dessiner ce que décrit la session
├── src/cli/         # outil en ligne de commande
└── tests/           # framework de test maison + tests unitaires / non-régression
```

Règle d'or : **toute logique va dans `src/core`** (testable sans Qt). `MainWindow` affiche
`session_.rows(filtre)` et `summaryText(...)`, relaie les clics à `ComparisonSession::apply` /
`toggleExpanded` / `undoLast`, puis appelle `populateFileList()`, qui réconcilie l'affichage :
une ligne dont le `Row` n'a pas changé garde son widget, seules les lignes nouvelles ou modifiées sont reconstruites.

Ajouter une action : une valeur dans `RowCommand` et `FileAction` (+ `commandLabel`, `actionStatusText`,
`commandAction`, `actionStyle`), l'opération réversible dans `ActionHistory` si elle touche au disque, le cas
dans `ComparisonSession::apply` et `buttonsFor`, les `switch` de `MainWindow` (titres d'erreur), puis des tests.
Les actions sont ordonnées par `AppliedAction::sequence` (pour `undoLast`) ; `historyId` est vide pour `Skip`.

## Compiler

Prérequis : CMake ≥ 3.21, un compilateur C++20 (GCC ≥ 11, Clang ≥ 15, MSVC 2022), Qt ≥ 6.2 pour l'interface.

```powershell
# Windows / MinGW livré avec Qt (PowerShell) — kit installé : C:/Qt/6.12.0/mingw_64
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;$env:PATH"
cmake --preset release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/mingw_64
cmake --build --preset release
ctest --test-dir build/release --output-on-failure
```

```bash
cmake --preset core && cmake --build --preset core && ctest --preset core              # sans Qt
cmake --preset sanitizers && cmake --build --preset sanitizers && ctest --preset sanitizers
cmake --preset tidy && cmake --build --preset tidy                                     # clang-tidy en erreurs
```

Sous Windows, `windeployqt` copie après le build les DLL Qt (et MinGW) à côté de l'exécutable.
Options CMake : `FC_BUILD_GUI`, `FC_BUILD_CLI`, `FC_BUILD_TESTS`, `FC_WARNINGS_AS_ERRORS` (ON),
`FC_ENABLE_SANITIZERS`, `FC_ENABLE_CLANG_TIDY`.

## Règles de qualité (à respecter)

- C++20 strict, sans extensions. **Aucun warning toléré** : `-Wall -Wextra -Wpedantic -Wconversion
  -Wsign-conversion -Wshadow -Wold-style-cast -Wuseless-cast -Wnull-dereference …` + `-Werror` ; MSVC `/W4 /permissive- /WX`.
  Vérifier aussi en Release (`-O3`) : GCC y émet des diagnostics supplémentaires.
- `.clang-tidy` : bugprone, cert, cppcoreguidelines, modernize, performance, readability… en erreurs.
- `_GLIBCXX_ASSERTIONS` toujours actif ; `-fstack-protector-strong` hors MinGW.
- Chemins : toujours `std::filesystem::path` ; texte via `fc::toUtf8` (jamais `path::string()`) et
  `QString::fromStdU16String` côté Qt. Listes triées / maps avec `fc::pathLess` / `fc::PathLess`.
- Toute opération qui modifie les dossiers de l'utilisateur passe par `ActionHistory` (donc annulable).
- Fonctions qui retournent une valeur : `[[nodiscard]]`. Enums : `enum class … : std::uint8_t`. Pas de récursion
  (piles explicites).
- Pas d'exception qui remonte dans la boucle d'événements Qt : chaque slot attrape `std::exception`.
- Le thread de comparaison ne touche jamais aux widgets : il poste avec
  `QMetaObject::invokeMethod(this, …, Qt::QueuedConnection)`. `worker_` (`std::jthread`) est déclaré en dernier.

## Tests

`ctest` lance `fc_unit_tests` (`build/<preset>/tests/fc_tests`, filtrable : `fc_tests session`, `fc_tests --list`) :
`FileManager`, `FolderManager`, `ActionHistory`, `Presentation`, `ComparisonSession`. Les tests `regression_*`
verrouillent des bugs corrigés.

Ajouter un test : `FC_TEST(nom) { FC_CHECK(...); FC_CHECK_EQ(a, b); FC_REQUIRE(...); FC_CHECK_THROWS_AS(expr, Type); }`
dans un `tests/test_*.cpp` (déclaré dans `tests/CMakeLists.txt`), avec `fc::test::TempDir` pour créer des fichiers.

## Limites connues / pistes

- **Modifications hors de l'application** (à faire) : les compteurs et les lignes reflètent la comparaison
  plus les actions de l'application ; un fichier modifié, ajouté ou supprimé par l'explorateur ou un autre
  programme n'est vu qu'à la comparaison suivante. Pistes : surveiller les deux dossiers (`QFileSystemWatcher`,
  limité en nombre de chemins, ou `ReadDirectoryChangesW` / inotify), ou au minimum revérifier sur le disque
  les lignes concernées avant chaque action et proposer une re-comparaison quand l'état diverge.
- Un chemin qui est un dossier d'un côté et un fichier de l'autre (`x/` dans A, `x` dans B) est bien compté,
  mais la session ne peut afficher qu'une des deux lignes (`entries_` est indexé par chemin seul).
- Les copies et suppressions s'exécutent dans le thread de l'interface (curseur d'attente) : une très grosse
  copie de dossier fige la fenêtre le temps de l'opération.
- Si l'application est tuée brutalement, la corbeille `.folderComparer-trash` reste dans le dossier comparé
  (les fichiers y sont récupérables à la main) ; elle reste ignorée par les comparaisons suivantes.
- Pour de très grandes listes, remplacer les lignes-widgets par un `QTreeView` + modèle.

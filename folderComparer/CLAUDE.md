# folderComparer

Petite application de bureau pour comparer deux gros dossiers (A et B) et
synchroniser leurs différences fichier par fichier.

## Ce que fait l'application

1. **Sélection des dossiers** : deux champs « Folder A » / « Folder B » avec bouton *Browse...*.
2. **Validation** avant comparaison (messages identiques au Python) :
   - champ vide : `Invalid folder` / `Folder must not be empty.`
   - dossier inexistant : `Invalid folder` / `Folder does not exist.`
   - même dossier (y compris via une autre écriture du chemin) : `Invalid folders` / `Folder A and Folder B are the same folder.`
   - les espaces autour du chemin sont ignorés.
3. **Options de comparaison**
   - *Recursive* (coché par défaut) : inclut tous les sous-dossiers. Les dossiers symboliques ne sont pas parcourus (comme `os.walk`).
   - *Deep comparison* (décoché par défaut) : compare le contenu octet par octet ; sinon seulement nom + taille.
4. **Comparaison en tâche de fond** avec barre de progression `courant / total` ; la fenêtre reste réactive et la fermer annule la comparaison proprement.
5. **Classement** de chaque chemin relatif : `Identical`, `Different`, `Only A`, `Only B`. Les listes sont triées comme le `sorted()` Python (ordre de la chaîne native).
6. **Résumé** : `Total`, `Identical`, `All differences`, `Different`, `Only A`, `Only B`, avec les couleurs d'origine.
7. **Filtres** : `All` (défaut : Different puis Only A puis Only B), `Different`, `Only A`, `Only B`. Le filtre actif est préfixé par `●`.
8. **Liste des fichiers** : une ligne colorée par fichier (statut + chemin) et des boutons :
   - Only A → `Copy → B` ; Only B → `Copy → A` ; Different → `B → A` et `A → B`.
9. **Copie** : crée les dossiers parents, écrase la destination, conserve la date de modification, passe par un fichier temporaire `*.fc-partial` (jamais de fichier tronqué). La ligne devient `COPIED` (vert, sans boutons) ; seule cette ligne est reconstruite.
10. **Erreurs** : une copie ratée affiche `Copy error` / `Could not copy: ...` ; un fichier illisible pendant la comparaison est classé `Different` et signalé dans une boîte d'avertissement.
11. **CLI** `folder_comparer_cli [--deep] [--no-recursive] [--summary] <A> <B>` : sortie `statut<TAB>chemin/relatif` (statut ∈ identical, different, only_a, only_b), code retour 0 = aucune différence, 1 = différences, 2 = erreur.

## Architecture (`cpp/`)

```
cpp/
├── CMakeLists.txt / CMakePresets.json
├── cmake/CompilerWarnings.cmake   # warnings max + -Werror / /WX
├── cmake/Sanitizers.cmake         # ASan + UBSan
├── .clang-tidy / .clang-format
├── src/core/        # bibliothèque fc_core : AUCUNE dépendance (std uniquement)
│   ├── ComparisonResult   # 4 listes triées + erreurs, entrées étiquetées (FileEntry)
│   ├── FileManager        # getFiles, filesAreIdentical, copyFile, formatSize, getFileInfo
│   ├── FolderManager      # compare (progress + std::stop_token), normalize, validateFolders
│   ├── Presentation       # tables SUMMARY / FILTERS / FILE_ACTIONS du Python (libellés, couleurs)
│   └── ComparisonSession  # état de l'UI : résultat, actions, lignes à afficher, copie
├── src/gui/         # Qt 6 Widgets : MainWindow ne fait que dessiner ce que décrit la session
├── src/cli/         # outil en ligne de commande
└── tests/           # framework de test maison + tests unitaires / non-régression / parité
```

Règle d'or : **toute logique va dans `src/core`** (testable sans Qt). `MainWindow`
ne prend aucune décision : il affiche `session_.rows(filtre)`, `summaryText(...)`,
`filterButtonText(...)` et relaie les clics à `ComparisonSession::copy`.

Correspondance avec le Python :

| Python | C++ |
|---|---|
| `Objects/ComparisonResult.py` | `core/ComparisonResult` |
| `Managers/FileManager.py` | `core/FileManager` |
| `Managers/FolderManager.py` | `core/FolderManager` |
| `App.SUMMARY_LABELS / FILTERS / FILE_ACTIONS` | `core/Presentation` |
| logique de `App` (filtres, lignes, copie) | `core/ComparisonSession` |
| widgets de `App`, `Views/ToolTip.py` | `gui/MainWindow` (infobulles Qt natives) |

## Compiler

Prérequis : CMake ≥ 3.21, un compilateur C++20 (MSVC 2022, GCC ≥ 11, Clang ≥ 15), Qt ≥ 6.2 pour l'interface.

```bash
cd cpp
# Tout (interface comprise) — indiquer Qt si besoin : -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
# Windows / MinGW livré avec Qt (PowerShell) — kit installé : C:/Qt/6.12.0/mingw_64
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;$env:PATH"
cmake --preset release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/mingw_64
cmake --build --preset release ; ctest --test-dir build/release --output-on-failure
# Windows / Visual Studio (nécessite le kit Qt « MSVC 2022 64-bit », non installé par défaut)
cmake --preset msvc -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64
cmake --build --preset msvc-release
# Sans Qt (cœur + CLI + tests)
cmake --preset core && cmake --build --preset core && ctest --preset core
# Sanitizers (GCC/Clang) et analyse statique
cmake --preset sanitizers && cmake --build --preset sanitizers && ctest --preset sanitizers
cmake --preset tidy && cmake --build --preset tidy
```

Sous Windows, `windeployqt` est lancé après le build pour copier les DLL Qt à côté de l'exécutable.

Options CMake : `FC_BUILD_GUI`, `FC_BUILD_CLI`, `FC_BUILD_TESTS`, `FC_WARNINGS_AS_ERRORS` (ON),
`FC_ENABLE_SANITIZERS`, `FC_ENABLE_CLANG_TIDY`, `FC_PYTHON_SOURCES` (dossier du Python pour la parité).

## Règles de qualité (à respecter)

- C++20 strict, sans extensions. **Aucun warning toléré** : `-Wall -Wextra -Wpedantic -Wconversion
  -Wsign-conversion -Wshadow -Wold-style-cast -Wuseless-cast …` + `-Werror` ; MSVC `/W4 /permissive- /WX`.
- `.clang-tidy` : bugprone, cert, cppcoreguidelines, modernize, performance, readability… en erreurs.
- `_GLIBCXX_ASSERTIONS` et `-fstack-protector-strong` toujours actifs (GCC/Clang).
- Chemins : toujours `std::filesystem::path` ; conversion vers texte via `fc::toUtf8` (jamais
  `path::string()`, qui peut lever une exception sous Windows) et `QString::fromStdU16String` côté Qt.
- Listes de chemins triées avec `fc::pathLess` (ordre natif = ordre Python), pas `path::operator<`.
- Fonctions qui retournent une valeur : `[[nodiscard]]`. Les enums : `enum class … : std::uint8_t`.
- Pas d'exception qui remonte dans la boucle d'événements Qt : chaque slot attrape `std::exception`.
- Le thread de comparaison ne touche jamais aux widgets : il poste vers la fenêtre avec
  `QMetaObject::invokeMethod(this, …, Qt::QueuedConnection)`. `worker_` (`std::jthread`) est
  déclaré en dernier pour être arrêté avant la destruction du reste.

## Tests

`ctest` lance :

- `fc_unit_tests` (`tests/fc_tests`, filtrable : `fc_tests regression`, `fc_tests --list`) :
  tests de `FileManager`, `FolderManager`, `Presentation`, `ComparisonSession`.
  Les tests `regression_*` verrouillent les bugs corrigés de la version Python.
- `fc_python_parity` (si Python 3 et les sources Python sont présents) : `tests/parity/parity_check.py`
  génère 25 arborescences aléatoires et vérifie, pour les 4 combinaisons d'options, que le CLI C++
  donne exactement les mêmes listes (ordre compris) que `Managers/FolderManager.compare`.

Ajouter un test : `FC_TEST(nom) { FC_CHECK(...); FC_CHECK_EQ(a, b); FC_REQUIRE(...); FC_CHECK_THROWS_AS(expr, Type); }`
dans un `tests/test_*.cpp` (à déclarer dans `tests/CMakeLists.txt`), avec `fc::test::TempDir` pour créer des fichiers.

## Différences volontaires avec la version Python

Bugs corrigés (chacun couvert par un test) :

- Le filtre **All** n'affichait que les fichiers *Only B* (la liste était vidée à chaque appel récursif).
- Les caches de filtres n'étaient jamais vidés : une 2ᵉ comparaison affichait les fichiers de la 1ʳᵉ.
- Les fichiers copiés n'étaient jamais marqués `COPIED` (tuple `(path,)` comparé à `path`).
- Un dossier invalide ne stoppait pas la comparaison (plantage sur `None`).
- Les champs et boutons restaient désactivés après une comparaison.
- La comparaison bloquait l'interface ; elle tourne désormais dans un thread annulable.
- Une erreur de lecture faisait planter toute la comparaison ; le fichier est maintenant signalé.

Autres choix :

- Comparaison profonde octet par octet au lieu de SHA-256 : même résultat, sans risque de collision, arrêt à la 1ʳᵉ différence.
- Seuls les fichiers réguliers sont listés (le Python incluait aussi FIFO, sockets, liens cassés, puis plantait dessus).
- Pas de passage en minuscules du chemin sous Windows (`normcase`) : l'égalité de dossiers repose sur `std::filesystem::equivalent`.
- Après une copie, le résumé n'est pas recalculé (comme le Python) : il reflète la dernière comparaison.

## Améliorations restant à faire (README)

- bouton *Reset* (`ComparisonSession::reset()` existe déjà) ;
- boutons *Skip* et *Delete* : `FileAction::Skip/Delete` et leurs styles existent dans `Presentation`,
  il reste à ajouter les méthodes dans `ComparisonSession` (+ tests) puis les boutons dans `buttonsFor` ;
- pour de très grandes listes, remplacer les lignes-widgets par un `QListView` + délégué.

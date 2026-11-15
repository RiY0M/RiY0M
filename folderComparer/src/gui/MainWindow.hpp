#pragma once

#include "core/ComparisonSession.hpp"
#include "core/FileManager.hpp"
#include "core/FolderManager.hpp"
#include "core/Presentation.hpp"

#include <QMainWindow>

#include <array>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <thread>
#include <vector>

class QCheckBox;
class QCloseEvent;
class QFont;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QProgressBar;
class QScrollArea;
class QPushButton;
class QString;
class QVBoxLayout;
class QWidget;

namespace fc::gui {

class FractionLabel;

/// Main window. Every decision (rows, colors, buttons, validation, actions)
/// comes from fc::ComparisonSession and fc::Presentation; this class only
/// renders them and forwards the clicks.
class MainWindow final : public QMainWindow {
public:
    MainWindow();
    ~MainWindow() override;

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    // Main UI
    void buildUi();

    // Folder selection
    void buildFolderSelection(QVBoxLayout* mainLayout);
    QLineEdit* buildFolderLine(QGridLayout* grid, int row, const QString& label, const QString& title);
    void browseFolder(const QString& title, QLineEdit* target);
    void updateFolderArea(bool enabled);

    // Progress
    void buildProgressArea(QVBoxLayout* mainLayout);
    QCheckBox* buildProgressCheckbox(QHBoxLayout* layout, const QString& label, bool checked,
                                     const QString& tooltip);
    void updateProgressArea(bool enabled);
    void updateUndoButton();

    // Summary: the difference labels are the filter buttons
    void buildSummary(QVBoxLayout* mainLayout);
    void updateSummary();
    void setFilter(Filter filter);

    // File list
    void buildFileList(QVBoxLayout* mainLayout);
    void populateFileList();
    QWidget* buildRow(const Row& row);
    /// Fixed width of a row button, the same for every button of its kind.
    [[nodiscard]] int rowButtonWidth(RowCommand command, const QFont& font);

    // Compare
    void compareFolders();
    void isScanning();
    void comparisonProgress(std::size_t current, std::size_t total);
    void showProgressText(std::size_t current, std::size_t total);
    void scanComplete(const ValidatedFolders& folders, ComparisonResult result);
    void scanFailed(const QString& message);
    [[nodiscard]] bool confirmDiscardActions(const QString& consequence);

    // Actions
    void runCommand(const std::filesystem::path& relativePath, RowCommand command);
    void toggleFolder(const std::filesystem::path& relativePath);
    void undoLast();
    void redoLast();
    void focusFileList();

    // Opening files
    void openEntry(const std::filesystem::path& relativePath);
    void revealEntry(const std::filesystem::path& relativePath);
    /// The single existing location, or the one picked in a small A / B menu.
    [[nodiscard]] std::optional<Location> chooseLocation(const std::filesystem::path& relativePath,
                                                         const QString& action);

    struct RowWidget {
        Row row;
        QWidget* widget = nullptr;
    };

    QLineEdit* folderAEdit_ = nullptr;
    QLineEdit* folderBEdit_ = nullptr;
    QCheckBox* recursiveCheckbox_ = nullptr;
    QCheckBox* deepComparisonCheckbox_ = nullptr;
    QPushButton* undoButton_ = nullptr;
    QPushButton* redoButton_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* progressLabel_ = nullptr; ///< "Ready", "Scanning...", "Comparison failed"
    FractionLabel* fraction_ = nullptr;
    QLabel* percentLabel_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QWidget* fileContainer_ = nullptr;
    QVBoxLayout* fileLayout_ = nullptr;
    bool scanning_ = false;

    std::vector<QWidget*> folderAreaItems_;
    std::vector<QWidget*> progressAreaItems_;
    std::array<QPushButton*, kSummaryItems.size()> summaryButtons_{}; ///< checkable ones are the filters
    std::map<std::filesystem::path, RowWidget, PathLess> rowWidgets_;
    std::array<int, 4> buttonWidths_{}; ///< Copy, Delete, Skip, Undo; computed on first use

    ComparisonSession session_;
    Filter currentFilter_ = kDefaultFilter;

    // Declared last: destroyed (stopped and joined) before everything above.
    std::jthread worker_;
};

} // namespace fc::gui

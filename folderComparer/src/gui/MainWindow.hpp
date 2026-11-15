#pragma once

#include "core/ComparisonSession.hpp"
#include "core/FolderManager.hpp"
#include "core/Presentation.hpp"

#include <QMainWindow>

#include <array>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <thread>
#include <vector>

class QCheckBox;
class QCloseEvent;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace fc::gui {

/// Qt port of the Tkinter App: same layout, same tables, same behavior. Every
/// decision (rows, colors, buttons, validation) comes from fc::ComparisonSession
/// and fc::Presentation; this class only renders them.
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

    // Summary
    void buildSummary(QVBoxLayout* mainLayout);
    void updateSummary();

    // Filters
    void buildFilterButtons(QVBoxLayout* mainLayout);
    void updateFilterButtons();
    void setFilter(Filter filter);

    // File list
    void buildFileList(QVBoxLayout* mainLayout);
    void populateFileList();
    void clearFileList();
    QWidget* buildRow(const Row& row);
    void refreshRow(const std::filesystem::path& relativePath);

    // Compare
    void compareFolders();
    void isScanning();
    void comparisonProgress(std::size_t current, std::size_t total);
    void scanComplete(const ValidatedFolders& folders, ComparisonResult result);
    void scanFailed(const QString& message);

    // Copy
    void copyPath(const std::filesystem::path& relativePath, CopyDirection direction);

    QLineEdit* folderAEdit_ = nullptr;
    QLineEdit* folderBEdit_ = nullptr;
    QCheckBox* recursiveCheckbox_ = nullptr;
    QCheckBox* deepComparisonCheckbox_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* progressLabel_ = nullptr;
    QWidget* fileContainer_ = nullptr;
    QVBoxLayout* fileLayout_ = nullptr;

    std::vector<QWidget*> folderAreaItems_;
    std::vector<QWidget*> progressAreaItems_;
    std::array<QLabel*, kSummaryItems.size()> summaryLabels_{};
    std::array<QPushButton*, kFilters.size()> filterButtons_{};
    std::map<std::filesystem::path, QWidget*, PathLess> rowWidgets_;

    ComparisonSession session_;
    Filter currentFilter_ = kDefaultFilter;

    // Declared last: destroyed (stopped and joined) before everything above.
    std::jthread worker_;
};

} // namespace fc::gui

#include "gui/MainWindow.hpp"

#include "core/FileManager.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <climits>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace fs = std::filesystem;

namespace fc::gui {

namespace {

constexpr int kStatusColumnCharacters = 12;
constexpr int kMaxListedErrors = 10;
constexpr auto kProgressInterval = std::chrono::milliseconds(50);

QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString toQString(const fs::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

fs::path toPath(const QString& text)
{
    return {text.toStdU16String()};
}

int toInt(std::size_t value)
{
    return static_cast<int>(std::min<std::size_t>(value, static_cast<std::size_t>(INT_MAX)));
}

QFont appFont(int pointSize, bool bold = false)
{
    QFont font(qs(kFontFamily), pointSize);
    font.setBold(bold);
    return font;
}

QString colorStyleSheet(std::string_view color)
{
    return qs("color: %1;").arg(qs(color));
}

} // namespace

MainWindow::MainWindow()
{
    setWindowTitle(qs("Folder Comparison"));
    resize(1050, 750);
    setMinimumSize(850, 600);

    buildUi();
}

MainWindow::~MainWindow()
{
    worker_.request_stop(); // joined by std::jthread's destructor
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    worker_.request_stop();
    QMainWindow::closeEvent(event);
}

// =========================================================
// Main UI
// =========================================================

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    setCentralWidget(central);

    buildFolderSelection(mainLayout);
    buildProgressArea(mainLayout);
    buildSummary(mainLayout);
    buildFilterButtons(mainLayout);
    buildFileList(mainLayout);
}

// =========================================================
// Folder selection
// =========================================================

void MainWindow::buildFolderSelection(QVBoxLayout* mainLayout)
{
    auto* frame = new QGroupBox(qs("Folders"));
    auto* grid = new QGridLayout(frame);
    grid->setColumnStretch(1, 1);
    mainLayout->addWidget(frame);

    folderAEdit_ = buildFolderLine(grid, 0, qs("Folder A:"), qs("Select Folder A"));
    folderBEdit_ = buildFolderLine(grid, 1, qs("Folder B:"), qs("Select Folder B"));
}

QLineEdit* MainWindow::buildFolderLine(QGridLayout* grid, int row, const QString& label,
                                       const QString& title)
{
    auto* entry = new QLineEdit;
    auto* button = new QPushButton(qs("Browse..."));

    grid->addWidget(new QLabel(label), row, 0);
    grid->addWidget(entry, row, 1);
    grid->addWidget(button, row, 2);

    connect(button, &QPushButton::clicked, this, [this, title, entry] { browseFolder(title, entry); });

    folderAreaItems_.push_back(entry);
    folderAreaItems_.push_back(button);
    return entry;
}

void MainWindow::browseFolder(const QString& title, QLineEdit* target)
{
    const QString folder = QFileDialog::getExistingDirectory(this, title, target->text());
    if (!folder.isEmpty()) {
        target->setText(QDir::toNativeSeparators(folder));
    }
}

void MainWindow::updateFolderArea(bool enabled)
{
    for (QWidget* item : folderAreaItems_) {
        item->setEnabled(enabled);
    }
}

// =========================================================
// Progress
// =========================================================

void MainWindow::buildProgressArea(QVBoxLayout* mainLayout)
{
    auto* layout = new QHBoxLayout;
    mainLayout->addLayout(layout);

    auto* compareButton = new QPushButton(qs("Compare"));
    layout->addWidget(compareButton);
    connect(compareButton, &QPushButton::clicked, this, [this] { compareFolders(); });

    recursiveCheckbox_ = buildProgressCheckbox(
        layout, qs("Recursive"), true,
        qs("When enabled, files inside all subfolders are included.\n"
           "When disabled, only files directly inside the selected\n"
           "folders are compared."));

    deepComparisonCheckbox_ = buildProgressCheckbox(
        layout, qs("Deep comparison"), false,
        qs("When enabled, files will be compared through name and content.\n"
           "When disabled, files will be compared through name and size.\n"
           "TLDR: enabled = more accurate, disabled = faster."));

    progressAreaItems_ = {compareButton, recursiveCheckbox_, deepComparisonCheckbox_};

    progress_ = new QProgressBar;
    progress_->setRange(0, 1);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    layout->addWidget(progress_, 1);

    progressLabel_ = new QLabel(qs("Ready"));
    layout->addWidget(progressLabel_);
}

QCheckBox* MainWindow::buildProgressCheckbox(QHBoxLayout* layout, const QString& label, bool checked,
                                             const QString& tooltip)
{
    auto* checkbox = new QCheckBox(label);
    checkbox->setChecked(checked);
    if (!tooltip.isEmpty()) {
        checkbox->setToolTip(tooltip);
    }
    layout->addWidget(checkbox);
    layout->addSpacing(15);
    return checkbox;
}

void MainWindow::updateProgressArea(bool enabled)
{
    for (QWidget* item : progressAreaItems_) {
        item->setEnabled(enabled);
    }
}

// =========================================================
// Summary
// =========================================================

void MainWindow::buildSummary(QVBoxLayout* mainLayout)
{
    auto* frame = new QGroupBox(qs("Summary"));
    auto* layout = new QHBoxLayout(frame);
    mainLayout->addWidget(frame);

    for (std::size_t index = 0; index < kSummaryItems.size(); ++index) {
        auto* label = new QLabel;
        label->setFont(appFont(11, true));
        label->setStyleSheet(colorStyleSheet(summaryStyle(kSummaryItems.at(index)).color));
        layout->addWidget(label);
        layout->addSpacing(15);
        summaryLabels_.at(index) = label;
    }
    layout->addStretch(1);

    updateSummary();
}

void MainWindow::updateSummary()
{
    for (std::size_t index = 0; index < kSummaryItems.size(); ++index) {
        summaryLabels_.at(index)->setText(qs(summaryText(kSummaryItems.at(index), session_.result())));
    }
}

// =========================================================
// Filter buttons
// =========================================================

void MainWindow::buildFilterButtons(QVBoxLayout* mainLayout)
{
    auto* layout = new QHBoxLayout;
    mainLayout->addLayout(layout);

    layout->addWidget(new QLabel(qs("Show:")));
    layout->addSpacing(8);

    for (std::size_t index = 0; index < kFilters.size(); ++index) {
        const Filter filter = kFilters.at(index);
        auto* button = new QPushButton;
        connect(button, &QPushButton::clicked, this, [this, filter] { setFilter(filter); });
        layout->addWidget(button);
        filterButtons_.at(index) = button;
    }
    layout->addStretch(1);

    updateFilterButtons();
}

void MainWindow::updateFilterButtons()
{
    for (std::size_t index = 0; index < kFilters.size(); ++index) {
        filterButtons_.at(index)->setText(qs(filterButtonText(kFilters.at(index), currentFilter_)));
    }
}

void MainWindow::setFilter(Filter filter)
{
    currentFilter_ = filter;
    updateFilterButtons();
    populateFileList();
}

// =========================================================
// File list
// =========================================================

void MainWindow::buildFileList(QVBoxLayout* mainLayout)
{
    auto* frame = new QGroupBox(qs("Files"));
    auto* frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->addWidget(frame, 1);

    auto* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    frameLayout->addWidget(scrollArea);

    fileContainer_ = new QWidget;
    fileLayout_ = new QVBoxLayout(fileContainer_);
    fileLayout_->setContentsMargins(2, 2, 2, 2);
    fileLayout_->setSpacing(4);
    fileLayout_->addStretch(1);
    scrollArea->setWidget(fileContainer_);
}

void MainWindow::populateFileList()
{
    clearFileList();
    if (!session_.hasResult()) {
        return;
    }

    fileContainer_->setUpdatesEnabled(false);
    for (const Row& row : session_.rows(currentFilter_)) {
        QWidget* widget = buildRow(row);
        fileLayout_->insertWidget(fileLayout_->count() - 1, widget); // keep the stretch last
        rowWidgets_.insert_or_assign(row.path, widget);
    }
    fileContainer_->setUpdatesEnabled(true);
}

void MainWindow::clearFileList()
{
    for (auto& [path, widget] : rowWidgets_) {
        fileLayout_->removeWidget(widget);
        widget->hide();
        widget->deleteLater(); // may be called from one of the row's own buttons
    }
    rowWidgets_.clear();
}

QWidget* MainWindow::buildRow(const Row& row)
{
    auto* frame = new QFrame;
    frame->setObjectName(qs("fileRow"));
    frame->setStyleSheet(
        qs("QFrame#fileRow { background-color: %1; border: 1px solid #9E9E9E; }").arg(qs(row.style.background)));

    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(8, 4, 5, 4);

    auto* statusLabel = new QLabel(qs(row.statusText));
    statusLabel->setFont(appFont(9, true));
    statusLabel->setStyleSheet(colorStyleSheet(row.style.color));
    statusLabel->setFixedWidth(QFontMetrics(statusLabel->font()).averageCharWidth() * kStatusColumnCharacters);
    layout->addWidget(statusLabel);

    auto* filenameLabel = new QLabel(toQString(row.path));
    filenameLabel->setFont(appFont(10));
    filenameLabel->setStyleSheet(colorStyleSheet(row.style.color));
    filenameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    filenameLabel->setMinimumHeight(QFontMetrics(filenameLabel->font()).height() + 14);
    layout->addWidget(filenameLabel, 1);

    for (const RowButton& rowButton : row.buttons) {
        auto* button = new QPushButton(qs(rowButton.label));
        const fs::path path = row.path;
        const CopyDirection direction = rowButton.direction;
        connect(button, &QPushButton::clicked, this, [this, path, direction] { copyPath(path, direction); });
        layout->addWidget(button);
    }

    return frame;
}

void MainWindow::refreshRow(const fs::path& relativePath)
{
    const auto found = rowWidgets_.find(relativePath);
    const auto row = session_.row(relativePath);
    if (found == rowWidgets_.end() || !row) {
        populateFileList();
        return;
    }

    QWidget* oldWidget = found->second;
    QWidget* newWidget = buildRow(*row);
    const int index = fileLayout_->indexOf(oldWidget);
    fileLayout_->removeWidget(oldWidget);
    fileLayout_->insertWidget(index, newWidget);
    oldWidget->hide();
    oldWidget->deleteLater(); // we are inside one of its buttons' clicked() signal
    found->second = newWidget;
}

// =========================================================
// Compare
// =========================================================

void MainWindow::compareFolders()
{
    const FolderValidation validation =
        FolderManager::validateFolders(toPath(folderAEdit_->text()), toPath(folderBEdit_->text()));
    if (const auto* error = std::get_if<FolderValidationError>(&validation)) {
        QMessageBox::critical(this, qs(error->title), qs(error->message));
        return;
    }

    const ValidatedFolders folders = std::get<ValidatedFolders>(validation);
    const CompareOptions options{
        .deepComparison = deepComparisonCheckbox_->isChecked(),
        .recursive = recursiveCheckbox_->isChecked(),
    };

    // Reset state
    session_.reset();
    clearFileList();
    updateSummary();
    isScanning();

    // The comparison runs on a worker thread so the window stays responsive;
    // results are posted back to the GUI thread.
    worker_ = std::jthread([this, folders, options](const std::stop_token& stopToken) {
        auto lastUpdate = std::chrono::steady_clock::time_point{};
        const auto onProgress = [this, &lastUpdate](std::size_t current, std::size_t total, const fs::path&) {
            const auto now = std::chrono::steady_clock::now();
            if (current != total && now - lastUpdate < kProgressInterval) {
                return; // throttled: do not flood the event queue
            }
            lastUpdate = now;
            QMetaObject::invokeMethod(
                this, [this, current, total] { comparisonProgress(current, total); }, Qt::QueuedConnection);
        };

        try {
            auto result = std::make_shared<ComparisonResult>(
                FolderManager::compare(folders.folderA, folders.folderB, options, onProgress, stopToken));
            QMetaObject::invokeMethod(
                this, [this, folders, result] { scanComplete(folders, std::move(*result)); },
                Qt::QueuedConnection);
        }
        catch (const OperationCancelled&) { // NOLINT(bugprone-empty-catch): the window is closing
        }
        catch (const std::exception& error) {
            const QString message = QString::fromLocal8Bit(error.what());
            QMetaObject::invokeMethod(this, [this, message] { scanFailed(message); }, Qt::QueuedConnection);
        }
    });
}

void MainWindow::isScanning()
{
    updateFolderArea(false);
    updateProgressArea(false);
    progress_->setRange(0, 1);
    progress_->setValue(0);
    progressLabel_->setText(qs("Scanning..."));
}

void MainWindow::comparisonProgress(std::size_t current, std::size_t total)
{
    progress_->setMaximum(std::max(toInt(total), 1));
    progress_->setValue(toInt(current));
    progressLabel_->setText(qs("%1 / %2").arg(current).arg(total));
}

void MainWindow::scanComplete(const ValidatedFolders& folders, ComparisonResult result)
{
    const std::vector<ComparisonError> errors = result.errors;
    session_.setResult(folders.folderA, folders.folderB, std::move(result));

    updateSummary();
    populateFileList();
    progress_->setMaximum(1);
    progress_->setValue(1);
    progressLabel_->setText(qs("Comparison complete"));
    updateFolderArea(true); // the Python version never re-enabled the inputs
    updateProgressArea(true);

    if (!errors.empty()) {
        QString details;
        const auto listed = std::min(errors.size(), static_cast<std::size_t>(kMaxListedErrors));
        for (std::size_t index = 0; index < listed; ++index) {
            details += qs("\n\n%1\n%2").arg(toQString(errors.at(index).path), qs(errors.at(index).message));
        }
        if (errors.size() > listed) {
            details += qs("\n\n... and %1 more.").arg(errors.size() - listed);
        }
        QMessageBox::warning(this, qs("Comparison errors"),
                             qs("%1 file(s) could not be compared and are listed as different:")
                                     .arg(errors.size()) +
                                 details);
    }
}

void MainWindow::scanFailed(const QString& message)
{
    progressLabel_->setText(qs("Comparison failed"));
    updateFolderArea(true);
    updateProgressArea(true);
    QMessageBox::critical(this, qs("Comparison error"), qs("Could not compare the folders:\n\n") + message);
}

// =========================================================
// Copy
// =========================================================

void MainWindow::copyPath(const fs::path& relativePath, CopyDirection direction)
{
    try {
        session_.copy(relativePath, direction);
    }
    catch (const std::exception& error) {
        QMessageBox::critical(this, qs("Copy error"),
                              qs("Could not copy:\n\n%1\n\n%2")
                                  .arg(toQString(relativePath), QString::fromLocal8Bit(error.what())));
        return;
    }

    refreshRow(relativePath); // only the concerned row is rebuilt
}

} // namespace fc::gui

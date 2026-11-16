#include "gui/MainWindow.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QEnterEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <climits>
#include <exception>
#include <optional>
#include <set>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace fs = std::filesystem;

namespace fc::gui {

namespace {

constexpr int kStatusColumnCharacters = 12;
constexpr int kIndentPerDepth = 28;
constexpr int kToggleWidth = 26;
constexpr int kNameScrollBarHeight = 6;
constexpr int kButtonPadding = 8;       // left / right text padding of the row buttons
constexpr int kMenuIndicatorRoom = 14;  // extra room on the right of a menu button for its arrow

QString qs(std::string_view text);

constexpr int kCheckBoxWidth = 22;
constexpr std::string_view kProgressRunning = "#43A047"; // green
constexpr std::string_view kProgressPaused = "#F9A825";  // yellow, like a paused Explorer copy

QString progressBarStyle(std::string_view chunkColor);

/// Colors of an action button (also used for the menu of a menu button).
Style actionButtonColors(RowCommand command)
{
    const Style style = commandStyle(command);
    return style.color.empty() ? Style{{}, "#424242", "#FFFFFF"} : style; // Undo: neutral
}

/// Copy green, Delete red, Skip grey (the color of the row the action produces), Undo neutral.
QString actionButtonStyleSheet(RowCommand command, bool hasMenu)
{
    const Style style = actionButtonColors(command);
    return qs("QPushButton { color: %1; background-color: %2; border: 1px solid %1; border-radius: 3px;"
              " padding: 3px %3px 3px %4px; }"
              "QPushButton:hover { background-color: #FFFFFF; }"
              "QPushButton:pressed { background-color: %1; color: #FFFFFF; }"
              "QPushButton::menu-indicator { subcontrol-origin: padding; subcontrol-position: center right;"
              " right: 4px; }")
        .arg(qs(style.color), qs(style.background))
        .arg(kButtonPadding + (hasMenu ? kMenuIndicatorRoom : 0))
        .arg(kButtonPadding);
}

/// Row buttons have one fixed width per kind (Copy, Delete, Skip, Undo), the
/// same on every row, so that they line up in columns.
std::size_t buttonSlot(RowCommand command)
{
    switch (command) {
    case RowCommand::CopyAToB:
    case RowCommand::CopyBToA:
        return 0;
    case RowCommand::DeleteA:
    case RowCommand::DeleteB:
    case RowCommand::DeleteBoth:
        return 1;
    case RowCommand::Skip:
        return 2;
    case RowCommand::Undo:
        return 3;
    }
    return 3;
}
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

/// Busy cursor while a (possibly long) file operation runs.
class WaitCursor {
public:
    WaitCursor() { QGuiApplication::setOverrideCursor(Qt::WaitCursor); }
    ~WaitCursor() { QGuiApplication::restoreOverrideCursor(); }
    WaitCursor(const WaitCursor&) = delete;
    WaitCursor& operator=(const WaitCursor&) = delete;
    WaitCursor(WaitCursor&&) = delete;
    WaitCursor& operator=(WaitCursor&&) = delete;
};

/// A label that reacts to clicks (underlined on hover), used for file names.
class ClickableLabel final : public QLabel {
public:
    ClickableLabel(const QString& text, std::function<void()> onClick)
        : QLabel(text), onClick_(std::move(onClick))
    {
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QLabel::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
            onClick_();
        }
    }

    void enterEvent(QEnterEvent* event) override
    {
        setUnderline(true);
        QLabel::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        setUnderline(false);
        QLabel::leaveEvent(event);
    }

private:
    void setUnderline(bool underline)
    {
        QFont current = font();
        current.setUnderline(underline);
        setFont(current);
    }

    std::function<void()> onClick_;
};

/// Horizontal scroller for a long name: the name scrolls inside its own area,
/// so the extension and the buttons of the row stay visible. A vertical wheel
/// keeps scrolling the list; only Shift + wheel or a horizontal wheel / touchpad
/// gesture scrolls the name.
class NameScroller final : public QScrollArea {
public:
    explicit NameScroller(QWidget* content)
    {
        setWidget(content);
        setWidgetResizable(false); // the name keeps its natural width
        setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setFocusPolicy(Qt::NoFocus);
        // Takes the space left in the row and never pushes the other widgets.
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setFixedHeight(content->sizeHint().height() + kNameScrollBarHeight);

        setStyleSheet(qs("QScrollArea { background: transparent; border: none; }"
                         "QScrollBar:horizontal { height: %1px; background: transparent; margin: 0; }"
                         "QScrollBar::handle:horizontal { background: #9E9E9E; border-radius: 3px; min-width: 24px; }"
                         "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
                         "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }")
                          .arg(kNameScrollBarHeight));
        viewport()->setAutoFillBackground(false);
        content->setAutoFillBackground(false);
    }

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        const QPoint delta = event->angleDelta();
        const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
        int amount = delta.x(); // horizontal wheel / touchpad
        if (amount == 0 && shift) {
            amount = delta.y(); // Shift + vertical wheel
        }
        QScrollBar* bar = horizontalScrollBar();
        if (amount == 0 || bar->maximum() == 0) {
            event->ignore(); // the list scrolls
            return;
        }
        bar->setValue(bar->value() - (amount / 2));
        event->accept();
    }
};

QString progressBarStyle(std::string_view chunkColor)
{
    return qs("QProgressBar { border: 1px solid #BDBDBD; border-radius: 4px; background-color: #FAFAFA; }"
              "QProgressBar::chunk { background-color: %1; border-radius: 3px; }")
        .arg(qs(chunkColor));
}

QString nativePath(const fs::path& path)
{
    return QDir::toNativeSeparators(toQString(path));
}

QString sideName(Side side)
{
    return side == Side::A ? qs("A") : qs("B");
}

/// Open the file manager on the folder holding `path`, with `path` selected
/// when the platform allows it.
bool revealInFileManager(const fs::path& path)
{
#if defined(_WIN32)
    return QProcess::startDetached(qs("explorer.exe"), QStringList{qs("/select,"), nativePath(path)});
#elif defined(__APPLE__)
    return QProcess::startDetached(qs("open"), QStringList{qs("-R"), toQString(path)});
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(toQString(path.parent_path())));
#endif
}

QString fileManagerName()
{
#if defined(_WIN32)
    return qs("Explorer");
#elif defined(__APPLE__)
    return qs("Finder");
#else
    return qs("the file manager");
#endif
}

QString commandErrorTitle(RowCommand command)
{
    switch (command) {
    case RowCommand::CopyAToB:
    case RowCommand::CopyBToA:
        return qs("Copy error");
    case RowCommand::DeleteA:
    case RowCommand::DeleteB:
    case RowCommand::DeleteBoth:
        return qs("Delete error");
    case RowCommand::Skip:
        return qs("Skip error");
    case RowCommand::Undo:
        return qs("Undo error");
    }
    return qs("Error");
}

QString commandErrorVerb(RowCommand command)
{
    switch (command) {
    case RowCommand::CopyAToB:
    case RowCommand::CopyBToA:
        return qs("copy");
    case RowCommand::DeleteA:
    case RowCommand::DeleteB:
        return qs("delete");
    case RowCommand::DeleteBoth:
        return qs("delete both versions of");
    case RowCommand::Skip:
        return qs("skip");
    case RowCommand::Undo:
        return qs("undo the action on");
    }
    return {};
}

} // namespace

/// "current over total" as a real fraction: the numerator above a bar above the denominator.
class FractionLabel final : public QWidget {
public:
    FractionLabel() : numerator_(new QLabel), denominator_(new QLabel)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* bar = new QFrame;
        bar->setFixedHeight(1);
        bar->setStyleSheet(qs("background-color: #424242; border: none;"));

        for (QLabel* label : {numerator_, denominator_}) {
            label->setFont(appFont(8));
            label->setAlignment(Qt::AlignHCenter);
        }
        layout->addWidget(numerator_);
        layout->addWidget(bar);
        layout->addWidget(denominator_);
    }

    void setValues(std::size_t current, std::size_t total)
    {
        numerator_->setText(QString::number(current));
        denominator_->setText(QString::number(total));
    }

private:
    QLabel* numerator_;
    QLabel* denominator_;
};

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
    if (!confirmDiscardActions(qs("Closing the application"))) {
        event->ignore();
        return;
    }
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
    connect(entry, &QLineEdit::returnPressed, this, [this] { compareFolders(); }); // Enter compares

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

    undoButton_ = new QPushButton(qs("Undo"));
    undoButton_->setShortcut(QKeySequence(qs("Ctrl+Z")));
    undoButton_->setToolTip(qs("Undo the last action (Ctrl+Z)"));
    layout->addWidget(undoButton_);
    connect(undoButton_, &QPushButton::clicked, this, [this] { undoLast(); });

    redoButton_ = new QPushButton(qs("Redo"));
    redoButton_->setShortcut(QKeySequence(qs("Ctrl+Y")));
    redoButton_->setToolTip(qs("Redo the last undone action (Ctrl+Y)"));
    layout->addWidget(redoButton_);
    layout->addSpacing(15);
    connect(redoButton_, &QPushButton::clicked, this, [this] { redoLast(); });

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
    // Drawn by a style sheet (the native style draws a thin line), and allowed
    // to grow vertically (QProgressBar is fixed-height by default): it fills the
    // row, exactly as high as the buttons next to it.
    progress_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    progress_->setStyleSheet(progressBarStyle(kProgressRunning));
    layout->addWidget(progress_, 1);

    progressLabel_ = new QLabel(qs("Ready"));
    layout->addWidget(progressLabel_);

    fraction_ = new FractionLabel;
    fraction_->hide();
    layout->addWidget(fraction_);

    percentLabel_ = new QLabel;
    percentLabel_->setFont(appFont(10, true));
    percentLabel_->setMinimumWidth(QFontMetrics(percentLabel_->font()).horizontalAdvance(qs("100%")));
    percentLabel_->hide();
    layout->addWidget(percentLabel_);

    // Pause / resume and stop, like the copy dialog of the Windows Explorer.
    const auto scanButton = [layout](QStyle::StandardPixmap icon, const QString& tooltip) {
        auto* button = new QToolButton;
        button->setIcon(QApplication::style()->standardIcon(icon));
        button->setToolTip(tooltip);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setEnabled(false);
        layout->addWidget(button);
        return button;
    };
    pauseButton_ = scanButton(QStyle::SP_MediaPause, qs("Pause the comparison"));
    stopButton_ = scanButton(QStyle::SP_MediaStop, qs("Stop the comparison"));
    connect(pauseButton_, &QToolButton::clicked, this, [this] { togglePause(); });
    connect(stopButton_, &QToolButton::clicked, this, [this] { stopComparison(); });

    updateUndoButton();
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

void MainWindow::updateUndoButton()
{
    undoButton_->setEnabled(!scanning_ && session_.canUndo());
    redoButton_->setEnabled(!scanning_ && session_.canRedo());
}

// =========================================================
// Summary / filters
// =========================================================

void MainWindow::buildSummary(QVBoxLayout* mainLayout)
{
    auto* frame = new QGroupBox(qs("Summary"));
    frame->setToolTip(qs(countersHelp()));
    auto* layout = new QGridLayout(frame);
    layout->setVerticalSpacing(2);
    mainLayout->addWidget(frame);

    //   Total       All differences   Only A
    //   Identical   Different         Only B
    //   Skipped
    const auto cell = [](SummaryItem item) -> std::pair<int, int> {
        switch (item) {
        case SummaryItem::Total:
            return {0, 0};
        case SummaryItem::Identical:
            return {1, 0};
        case SummaryItem::AllDifferences:
            return {0, 1};
        case SummaryItem::Different:
            return {1, 1};
        case SummaryItem::OnlyA:
            return {0, 2};
        case SummaryItem::OnlyB:
            return {1, 2};
        case SummaryItem::Skipped:
            return {2, 0};
        }
        return {0, 0};
    };
    for (int column = 0; column < 3; ++column) {
        layout->setColumnStretch(column, 1);
    }

    for (std::size_t index = 0; index < kSummaryItems.size(); ++index) {
        const SummaryItem item = kSummaryItems.at(index);
        const Style style = summaryStyle(item);
        const auto [row, column] = cell(item);

        // Every item is a button with the same frame, so all the texts line up;
        // Total and Identical are plain labels: they ignore the mouse.
        auto* button = new QPushButton;
        button->setFocusPolicy(Qt::NoFocus);
        button->setFont(appFont(11, true));
        button->setStyleSheet(
            qs("QPushButton { color: %1; border: 1px solid transparent; border-radius: 4px; padding: 3px 8px;"
               " text-align: left; }"
               "QPushButton:hover { border-color: %1; }"
               "QPushButton:checked { background-color: %2; border-color: %1; }")
                .arg(qs(style.color), style.background.empty() ? qs("#FFFDE7") : qs(style.background)));

        if (const auto filter = summaryFilter(item)) {
            button->setCheckable(true);
            button->setCursor(Qt::PointingHandCursor);
            const Filter target = *filter;
            connect(button, &QPushButton::clicked, this, [this, target] { setFilter(target); });
        }
        else {
            button->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
        layout->addWidget(button, row, column, Qt::AlignLeft);
        summaryButtons_.at(index) = button;
    }

    updateSummary();
}

void MainWindow::updateSummary()
{
    for (std::size_t index = 0; index < kSummaryItems.size(); ++index) {
        const SummaryItem item = kSummaryItems.at(index);
        QPushButton* button = summaryButtons_.at(index);
        button->setText(qs(summaryText(item, session_.summaryCounts(item))));
        if (button->isCheckable()) {
            button->setChecked(summaryFilter(item) == currentFilter_);
        }
    }
}

void MainWindow::setFilter(Filter filter)
{
    currentFilter_ = filter;
    session_.clearSelection(); // never act on rows that are not shown
    updateSummary();
    populateFileList();
    focusFileList();
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

    scrollArea_ = new QScrollArea;
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setFocusPolicy(Qt::StrongFocus); // receives the focus after each action
    // Always shown, so that the header below keeps the same columns as the rows.
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    buildListHeader(frameLayout);
    frameLayout->addWidget(scrollArea_);

    fileContainer_ = new QWidget;
    fileLayout_ = new QVBoxLayout(fileContainer_);
    fileLayout_->setContentsMargins(2, 2, 2, 2);
    fileLayout_->setSpacing(4);
    fileLayout_->addStretch(1);
    scrollArea_->setWidget(fileContainer_);

    updateSelectionBar();
}

void MainWindow::buildListHeader(QVBoxLayout* frameLayout)
{
    // Same columns as a row: check box | toggle | status | icon | Name ... | selection | mass actions.
    // Margins: list container (2) + row border (1) + row layout (4 left, 5 right), plus the scroll bar.
    auto* header = new QHBoxLayout;
    header->setContentsMargins(2 + 1 + 4, 0, 2 + 1 + 5 + scrollArea_->verticalScrollBar()->sizeHint().width(), 2);
    frameLayout->addLayout(header);

    selectAllBox_ = new QCheckBox;
    selectAllBox_->setTristate(true); // shows "some selected"
    selectAllBox_->setFocusPolicy(Qt::NoFocus);
    selectAllBox_->setFixedWidth(kCheckBoxWidth);
    selectAllBox_->setToolTip(qs("Select every row shown"));
    connect(selectAllBox_, &QCheckBox::clicked, this, [this] { selectAllClicked(); });
    header->addWidget(selectAllBox_);

    // Empty slots as wide as the toggle, status and icon columns of the rows.
    const auto slot = [header](int width) {
        auto* empty = new QWidget;
        empty->setFixedWidth(width);
        header->addWidget(empty);
    };
    slot(kToggleWidth);
    slot(QFontMetrics(appFont(9, true)).averageCharWidth() * kStatusColumnCharacters);
    auto* iconSlot = new QToolButton; // invisible twin of the rows' icon button, for its exact width
    iconSlot->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
    iconSlot->setAutoRaise(true);
    QSizePolicy keepSize = iconSlot->sizePolicy();
    keepSize.setRetainSizeWhenHidden(true);
    iconSlot->setSizePolicy(keepSize);
    iconSlot->hide();
    header->addWidget(iconSlot);

    sortButton_ = new QToolButton;
    sortButton_->setText(qs("Name"));
    sortButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    sortButton_->setAutoRaise(true);
    sortButton_->setFocusPolicy(Qt::NoFocus);
    sortButton_->setFont(appFont(10, true));
    connect(sortButton_, &QToolButton::clicked, this, [this] { toggleSortOrder(); });
    header->addWidget(sortButton_);
    header->addSpacing(10);

    typesButton_ = new QToolButton;
    typesButton_->setPopupMode(QToolButton::InstantPopup);
    typesButton_->setAutoRaise(true);
    typesButton_->setFocusPolicy(Qt::NoFocus);
    typesButton_->setToolTip(qs("Show only some file types (folders holding none are hidden)"));
    typesButton_->setStyleSheet(qs("QToolButton::menu-indicator { image: none; width: 0px; }"));
    typesMenu_ = new QMenu(typesButton_);
    connect(typesMenu_, &QMenu::aboutToShow, this, [this] { buildTypesMenu(); });
    typesButton_->setMenu(typesMenu_);
    header->addWidget(typesButton_);
    header->addStretch(1);

    // Right-aligned on the "D() F()" / extension column of the rows.
    selectionLabel_ = new QLabel;
    selectionLabel_->setFont(appFont(9, true));
    selectionLabel_->setStyleSheet(colorStyleSheet(colors::kGrey));
    selectionLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    selectionLabel_->setToolTip(qs("Selected: E = entries, D = folders, F = files.\n"
                                   "A folder counts with its content; with a type filter, only the\n"
                                   "matching files, which are the only ones its actions touch."));
    header->addWidget(selectionLabel_);

    // One fixed slot per button column of the rows (Copy, Delete, Skip / Undo),
    // kept even when empty so that everything stays in its column.
    constexpr std::array<RowCommand, 3> kinds{RowCommand::CopyAToB, RowCommand::DeleteA, RowCommand::Skip};
    for (std::size_t column = 0; column < massSlots_.size(); ++column) {
        auto* holder = new QWidget;
        auto* holderLayout = new QHBoxLayout(holder);
        holderLayout->setContentsMargins(0, 0, 0, 0);
        holder->setFixedWidth(rowButtonWidth(kinds.at(column)));
        header->addWidget(holder);
        massSlots_.at(column) = holderLayout;
    }

    updateSortButton();
    updateTypesButton();
}

void MainWindow::toggleSortOrder()
{
    session_.setSortOrder(session_.sortOrder() == SortOrder::Ascending ? SortOrder::Descending
                                                                        : SortOrder::Ascending);
    updateSortButton();
    populateFileList();
    focusFileList();
}

void MainWindow::updateSortButton()
{
    const bool ascending = session_.sortOrder() == SortOrder::Ascending;
    sortButton_->setArrowType(ascending ? Qt::UpArrow : Qt::DownArrow);
    sortButton_->setToolTip(ascending ? qs("Sorted by name, A to Z (click: Z to A)")
                                      : qs("Sorted by name, Z to A (click: A to Z)"));
}

void MainWindow::buildTypesMenu()
{
    // Rebuilt each time it opens: the types depend on the comparison and on the
    // view. Check boxes inside the menu let several choices be made without closing it.
    typesMenu_->clear();
    typeChecks_.clear();
    groupChecks_.clear();
    allTypesCheck_ = nullptr;

    const std::map<std::string, std::size_t> types = session_.fileTypes(currentFilter_);

    auto* panel = new QWidget;
    auto* columns = new QHBoxLayout(panel);
    columns->setContentsMargins(8, 6, 8, 6);
    columns->setSpacing(20);

    const auto newColumn = [](const QString& title) {
        auto* column = new QVBoxLayout;
        column->setSpacing(2);
        auto* heading = new QLabel(title);
        heading->setFont(appFont(9, true));
        heading->setStyleSheet(colorStyleSheet(colors::kGrey));
        column->addWidget(heading);
        return column;
    };
    const auto newCheck = [](QVBoxLayout* column, const QString& text) {
        auto* check = new QCheckBox(text);
        check->setFocusPolicy(Qt::NoFocus);
        column->addWidget(check);
        return check;
    };

    // First column: every type found in the view. Its heading stays outside the
    // scrolling list (sticky), level with the "Groups" heading.
    auto* typeOuter = newColumn(qs("Types"));
    auto* typeColumn = new QVBoxLayout;
    typeColumn->setSpacing(2);
    typeColumn->setContentsMargins(0, 0, 0, 0);
    allTypesCheck_ = newCheck(typeColumn, qs("All types"));
    connect(allTypesCheck_, &QCheckBox::clicked, this, [this](bool checked) {
        applyTypeFilter(checked ? std::nullopt : std::optional<std::set<std::string>>{std::set<std::string>{}});
    });
    for (const auto& [type, count] : types) {
        QCheckBox* check = newCheck(typeColumn, qs("%1 (%2)").arg(qs(typeLabel(type))).arg(count));
        const std::string extension = type;
        connect(check, &QCheckBox::clicked, this, [this, extension](bool checked) {
            std::set<std::string> selected = selectedTypes();
            if (checked) {
                selected.insert(extension);
            }
            else {
                selected.erase(extension);
            }
            applyTypeSelection(std::move(selected));
        });
        typeChecks_.emplace_back(extension, check);
    }
    typeColumn->addStretch(1);

    // Many types: the first column scrolls.
    auto* typeWidget = new QWidget;
    typeWidget->setLayout(typeColumn);
    auto* typeScroll = new QScrollArea;
    typeScroll->setWidget(typeWidget);
    typeScroll->setWidgetResizable(true);
    typeScroll->setFrameShape(QFrame::NoFrame);
    typeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    constexpr int kMaxColumnHeight = 380;
    typeScroll->setMinimumHeight(std::min(typeWidget->sizeHint().height(), kMaxColumnHeight));
    typeScroll->setMinimumWidth(typeWidget->sizeHint().width() + typeScroll->verticalScrollBar()->sizeHint().width());
    typeOuter->addWidget(typeScroll);
    columns->addLayout(typeOuter);

    // Second column: groups of types (Images, Videos, Code...).
    auto* groupColumn = newColumn(qs("Groups"));
    for (const TypeGroup& group : typeGroups()) {
        std::vector<std::string> found; // the group's types present in the view
        std::size_t files = 0;
        for (const std::string_view extension : group.extensions) {
            if (const auto type = types.find(std::string(extension)); type != types.end()) {
                found.push_back(type->first);
                files += type->second;
            }
        }
        QCheckBox* check = newCheck(groupColumn, found.empty() ? qs(group.name)
                                                               : qs("%1 (%2)").arg(qs(group.name)).arg(files));
        check->setTristate(true); // some of its types selected
        check->setToolTip(qs(typeGroupDescription(group)) +
                          (found.empty() ? qs("\nNone of these types in this view.") : QString()));
        check->setEnabled(!found.empty());
        connect(check, &QCheckBox::clicked, this, [this, found] {
            std::set<std::string> selected = selectedTypes();
            const bool whole = std::ranges::all_of(found, [&selected](const std::string& type) {
                return selected.contains(type);
            });
            for (const std::string& type : found) {
                if (whole) {
                    selected.erase(type);
                }
                else {
                    selected.insert(type);
                }
            }
            applyTypeSelection(std::move(selected));
        });
        groupChecks_.emplace_back(std::move(found), check);
    }
    groupColumn->addStretch(1);
    columns->addLayout(groupColumn);
    columns->setAlignment(typeOuter, Qt::AlignTop);
    columns->setAlignment(groupColumn, Qt::AlignTop);

    auto* action = new QWidgetAction(typesMenu_);
    action->setDefaultWidget(panel);
    typesMenu_->addAction(action);
    updateTypeChecks();
}

std::set<std::string> MainWindow::selectedTypes() const
{
    if (const auto& filter = session_.typeFilter()) {
        return *filter;
    }
    std::set<std::string> all; // "every type" made explicit
    for (const auto& [type, files] : session_.fileTypes(currentFilter_)) {
        all.insert(type);
    }
    return all;
}

void MainWindow::applyTypeSelection(std::set<std::string> types)
{
    // Every type of the view selected: back to "All types".
    const auto inView = session_.fileTypes(currentFilter_);
    const bool all = std::ranges::all_of(inView, [&types](const auto& type) { return types.contains(type.first); });
    applyTypeFilter(all ? std::nullopt : std::optional<std::set<std::string>>{std::move(types)});
}

void MainWindow::updateTypeChecks()
{
    // Without signals: these updates must not run the handlers again.
    const auto& filter = session_.typeFilter();
    if (allTypesCheck_ != nullptr) {
        const QSignalBlocker blocker(allTypesCheck_);
        allTypesCheck_->setChecked(!filter.has_value());
    }
    for (const auto& [extension, check] : typeChecks_) {
        const QSignalBlocker blocker(check);
        check->setChecked(!filter || filter->contains(extension));
    }
    for (const auto& [found, check] : groupChecks_) {
        const QSignalBlocker blocker(check);
        const auto selected = static_cast<std::size_t>(std::ranges::count_if(
            found, [&filter](const std::string& type) { return !filter || filter->contains(type); }));
        if (selected == 0) {
            check->setCheckState(Qt::Unchecked);
        }
        else {
            check->setCheckState(selected == found.size() ? Qt::Checked : Qt::PartiallyChecked);
        }
    }
}

void MainWindow::applyTypeFilter(std::optional<std::set<std::string>> types)
{
    session_.setTypeFilter(std::move(types));
    updateTypeChecks();
    updateTypesButton();
    populateFileList();
}

void MainWindow::updateTypesButton()
{
    const auto& filter = session_.typeFilter();
    QString text;
    if (!filter) {
        text = qs("All types");
    }
    else if (filter->empty()) {
        text = qs("None");
    }
    else if (filter->size() <= 3) {
        QStringList names;
        for (const std::string& type : *filter) {
            names.append(qs(typeLabel(type)));
        }
        text = names.join(qs(", "));
    }
    else {
        text = qs("%1 types").arg(filter->size());
    }
    typesButton_->setText(qs("Types: %1").arg(text));
}

void MainWindow::populateFileList()
{
    // Reconcile the displayed rows with the session: a row whose content did
    // not change keeps its widget, only new / changed rows are rebuilt.
    std::vector<Row> rows = session_.hasResult() ? session_.rows(currentFilter_) : std::vector<Row>{};
    std::map<fs::path, RowWidget, PathLess> previous = std::move(rowWidgets_);
    rowWidgets_.clear();

    fileContainer_->setUpdatesEnabled(false);
    while (fileLayout_->count() > 0) {
        delete fileLayout_->takeAt(fileLayout_->count() - 1); // the widgets themselves are kept
    }

    for (Row& row : rows) {
        QWidget* widget = nullptr;
        if (const auto found = previous.find(row.path); found != previous.end() && found->second.row == row) {
            widget = found->second.widget;
            previous.erase(found);
        }
        else {
            widget = buildRow(row);
        }
        fileLayout_->addWidget(widget);
        const fs::path path = row.path;
        rowWidgets_.insert_or_assign(path, RowWidget{std::move(row), widget});
    }
    fileLayout_->addStretch(1);

    for (auto& [path, stale] : previous) {
        stale.widget->hide();
        stale.widget->deleteLater(); // may be the row whose button is being clicked
    }
    fileContainer_->setUpdatesEnabled(true);
    updateSelectionBar();
}

QPushButton* MainWindow::makeActionButton(const RowButton& rowButton, const std::function<void(RowCommand)>& run)
{
    auto* button = new QPushButton(qs(rowButton.label));
    button->setFocusPolicy(Qt::NoFocus);
    const bool hasMenu = !rowButton.choices.empty();

    // Copy green, Delete red, Skip grey: the color of the row the action produces;
    // Undo neutral. Every button gets the same frame (same height) and the fixed
    // width of its kind (the columns line up).
    const Style style = actionButtonColors(rowButton.command);
    button->setStyleSheet(actionButtonStyleSheet(rowButton.command, hasMenu));
    button->setFixedWidth(rowButtonWidth(rowButton.command));

    if (hasMenu) {
        // Pick the precise command in a menu (e.g. A → B or B → A).
        auto* menu = new QMenu(button);
        menu->setFont(button->font());
        // Same colors as its button: text and border in the action color, the
        // highlighted entry filled like a pressed button.
        menu->setStyleSheet(qs("QMenu { color: %1; background-color: %2; border: 1px solid %1; padding: 2px 0; }"
                               "QMenu::item { padding: 4px 10px; background-color: transparent; }"
                               "QMenu::item:selected { background-color: %1; color: #FFFFFF; }")
                                .arg(qs(style.color), qs(style.background)));
        // At least as wide as its button (known only once the row is laid out).
        connect(menu, &QMenu::aboutToShow, menu, [menu, button] { menu->setMinimumWidth(button->width()); });
        for (const RowChoice& choice : rowButton.choices) {
            QAction* item = menu->addAction(qs(choice.label));
            const RowCommand command = choice.command;
            connect(item, &QAction::triggered, this, [run, command] { run(command); });
        }
        button->setMenu(menu);
    }
    else {
        const RowCommand command = rowButton.command;
        connect(button, &QPushButton::clicked, this, [run, command] { run(command); });
    }
    return button;
}

int MainWindow::rowButtonWidth(RowCommand command)
{
    if (buttonWidths_.at(0) == 0) {
        // Widest label of each kind among every button a row can show, plus the
        // frame (padding, border, menu arrow) and a small margin, in the font
        // of the buttons.
        const QPushButton probe;
        const QFontMetrics metrics(probe.font());
        const auto widen = [this, &metrics](const RowButton& button) {
            const int text = metrics.horizontalAdvance(qs(button.label));
            const int frame = (2 * kButtonPadding) + 2 + (button.choices.empty() ? 0 : kMenuIndicatorRoom) + 6;
            int& width = buttonWidths_.at(buttonSlot(button.command));
            width = std::max(width, text + frame);
        };
        for (const FileStatus status : {FileStatus::Different, FileStatus::OnlyA, FileStatus::OnlyB}) {
            for (const RowButton& button : ComparisonSession::buttonsFor(status)) {
                widen(button);
            }
        }
        widen(RowButton{std::string(commandLabel(RowCommand::Undo)), RowCommand::Undo, {}});
        // Skip and Undo use the same (last) column.
        const int last = std::max(buttonWidths_.at(2), buttonWidths_.at(3));
        buttonWidths_.at(2) = last;
        buttonWidths_.at(3) = last;
    }
    return buttonWidths_.at(buttonSlot(command));
}

QWidget* MainWindow::buildRow(const Row& row)
{
    auto* frame = new QFrame;
    frame->setObjectName(qs("fileRow"));
    frame->setStyleSheet(
        qs("QFrame#fileRow { background-color: %1; border: 1px solid #9E9E9E; }").arg(qs(row.style.background)));

    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(4, 4, 5, 4);

    const fs::path path = row.path;

    // Check box for mass actions (an empty slot keeps the rows aligned).
    if (row.selectable) {
        auto* check = new QCheckBox;
        check->setChecked(row.selected);
        check->setFocusPolicy(Qt::NoFocus);
        check->setFixedWidth(kCheckBoxWidth);
        check->setToolTip(qs("Select for a mass action"));
        connect(check, &QCheckBox::toggled, this, [this, path](bool checked) { selectRow(path, checked); });
        layout->addWidget(check);
    }
    else {
        auto* slot = new QWidget;
        slot->setFixedWidth(kCheckBoxWidth);
        layout->addWidget(slot);
    }

    if (row.expandable) {
        auto* toggle = new QToolButton;
        toggle->setArrowType(row.expanded ? Qt::DownArrow : Qt::RightArrow);
        toggle->setAutoRaise(true);
        toggle->setFocusPolicy(Qt::NoFocus);
        toggle->setFixedWidth(kToggleWidth);
        toggle->setToolTip(row.expanded ? qs("Hide the content") : qs("Show the content"));
        connect(toggle, &QToolButton::clicked, this, [this, path] { toggleFolder(path); });
        layout->addWidget(toggle);
    }
    else {
        // An empty widget, not a spacer: the layout spacing then matches the toggle's.
        auto* placeholder = new QWidget;
        placeholder->setFixedWidth(kToggleWidth);
        layout->addWidget(placeholder);
    }

    auto* statusLabel = new QLabel(qs(row.statusText));
    statusLabel->setFont(appFont(9, true));
    statusLabel->setStyleSheet(colorStyleSheet(row.style.color));
    statusLabel->setFixedWidth(QFontMetrics(statusLabel->font()).averageCharWidth() * kStatusColumnCharacters);
    layout->addWidget(statusLabel);

    auto* revealButton = new QToolButton;
    revealButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
    revealButton->setAutoRaise(true);
    revealButton->setFocusPolicy(Qt::NoFocus);
    revealButton->setCursor(Qt::PointingHandCursor);
    revealButton->setToolTip(qs("Show in %1").arg(fileManagerName()));
    connect(revealButton, &QToolButton::clicked, this, [this, path] { revealEntry(path); });
    layout->addWidget(revealButton);

    auto* nameLabel = new ClickableLabel(toQString(row.displayName), [this, path] { openEntry(path); });
    nameLabel->setFont(appFont(10, row.kind == EntryKind::Folder));
    nameLabel->setStyleSheet(colorStyleSheet(row.style.color));
    nameLabel->setToolTip(qs("%1\nClick to open").arg(nativePath(row.path)));
    nameLabel->setMinimumHeight(QFontMetrics(nameLabel->font()).height() + 8);
    layout->addWidget(new NameScroller(nameLabel), 1);

    // Folder content ("D(2) F(10)") or file extension ("JPG"): the same plain grey text.
    if (const std::string& info = row.detail.empty() ? row.extension : row.detail; !info.empty()) {
        auto* infoLabel = new QLabel(qs(info));
        infoLabel->setFont(appFont(9, true));
        infoLabel->setStyleSheet(colorStyleSheet(colors::kGrey));
        infoLabel->setAlignment(Qt::AlignCenter);
        // A common minimum width keeps the short extensions aligned.
        infoLabel->setMinimumWidth(QFontMetrics(infoLabel->font()).horizontalAdvance(qs("JPEG")));
        layout->addWidget(infoLabel);
    }

    // Three fixed columns (Copy, Delete, Skip / Undo), kept even when empty: the
    // extension / "D() F()" stays aligned with the other rows and the header.
    std::array<QHBoxLayout*, 3> slots{};
    constexpr std::array<RowCommand, 3> kinds{RowCommand::CopyAToB, RowCommand::DeleteA, RowCommand::Skip};
    for (std::size_t column = 0; column < slots.size(); ++column) {
        auto* holder = new QWidget;
        slots.at(column) = new QHBoxLayout(holder);
        slots.at(column)->setContentsMargins(0, 0, 0, 0);
        holder->setFixedWidth(rowButtonWidth(kinds.at(column)));
        layout->addWidget(holder);
    }
    for (const RowButton& rowButton : row.buttons) {
        slots.at(std::min(buttonSlot(rowButton.command), std::size_t{2}))
            ->addWidget(makeActionButton(rowButton, [this, path](RowCommand command) { runCommand(path, command); }));
    }

    if (row.depth == 0) {
        return frame;
    }

    // Sub-rows: the whole frame is indented, not only its content.
    auto* holder = new QWidget;
    auto* holderLayout = new QHBoxLayout(holder);
    holderLayout->setContentsMargins(toInt(row.depth) * kIndentPerDepth, 0, 0, 0);
    holderLayout->setSpacing(0);
    holderLayout->addWidget(frame);
    return holder;
}

// =========================================================
// Compare
// =========================================================

bool MainWindow::confirmDiscardActions(const QString& consequence)
{
    if (!session_.hasUndoableBackups()) {
        return true;
    }
    const auto answer = QMessageBox::question(
        this, qs("Actions can still be undone"),
        qs("Some deleted or overwritten files can still be restored with Undo.\n\n"
           "%1 makes these actions permanent. Continue?")
            .arg(consequence),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return answer == QMessageBox::Yes;
}

void MainWindow::compareFolders()
{
    const FolderValidation validation =
        FolderManager::validateFolders(toPath(folderAEdit_->text()), toPath(folderBEdit_->text()));
    if (const auto* error = std::get_if<FolderValidationError>(&validation)) {
        QMessageBox::critical(this, qs(error->title), qs(error->message));
        return;
    }
    if (!confirmDiscardActions(qs("Starting a new comparison"))) {
        return;
    }

    const ValidatedFolders folders = std::get<ValidatedFolders>(validation);
    const CompareOptions options{
        .deepComparison = deepComparisonCheckbox_->isChecked(),
        .recursive = recursiveCheckbox_->isChecked(),
    };

    // Reset state (the previous actions become permanent)
    session_.reset();
    populateFileList();
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
                FolderManager::compare(folders.folderA, folders.folderB, options, onProgress,
                                       ScanControl{stopToken, &pauseControl_}));
            QMetaObject::invokeMethod(
                this, [this, folders, result] { scanComplete(folders, std::move(*result)); },
                Qt::QueuedConnection);
        }
        catch (const OperationCancelled&) {
            // Stop button (if the window is closing, the posted call is dropped with it).
            QMetaObject::invokeMethod(this, [this] { scanStopped(); }, Qt::QueuedConnection);
        }
        catch (const std::exception& error) {
            const QString message = QString::fromLocal8Bit(error.what());
            QMetaObject::invokeMethod(this, [this, message] { scanFailed(message); }, Qt::QueuedConnection);
        }
    });
}

void MainWindow::isScanning()
{
    scanning_ = true;
    pauseControl_.resume();
    updatePauseUi();
    pauseButton_->setEnabled(true);
    stopButton_->setEnabled(true);
    updateFolderArea(false);
    updateProgressArea(false);
    updateUndoButton();
    progress_->setRange(0, 1);
    progress_->setValue(0);
    progressLabel_->setText(qs("Scanning..."));
    progressLabel_->show();
    fraction_->hide();
    percentLabel_->hide();
}

void MainWindow::comparisonProgress(std::size_t current, std::size_t total)
{
    progress_->setMaximum(std::max(toInt(total), 1));
    progress_->setValue(toInt(current));
    showProgressText(current, total);
}

void MainWindow::showProgressText(std::size_t current, std::size_t total)
{
    progressLabel_->hide();
    fraction_->setValues(current, total);
    fraction_->show();
    percentLabel_->setText(qs("%1%").arg(progressPercent(current, total)));
    percentLabel_->show();
}

void MainWindow::scanComplete(const ValidatedFolders& folders, ComparisonResult result)
{
    const std::vector<ComparisonError> errors = result.errors;
    session_.setResult(folders.folderA, folders.folderB, std::move(result));

    scanning_ = false;
    endScanControls();
    updateTypesButton(); // a new comparison shows every type again
    updateSummary();
    populateFileList();
    progress_->setMaximum(1);
    progress_->setValue(1);
    if (fraction_->isHidden()) {
        showProgressText(0, 0); // no common file to compare: nothing was reported
    }
    // Otherwise the text stays as the scan left it (n over n, 100%).
    updateFolderArea(true);
    updateProgressArea(true);
    updateUndoButton();
    focusFileList();

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

void MainWindow::endScanControls()
{
    pauseControl_.resume();
    updatePauseUi();
    pauseButton_->setEnabled(false);
    stopButton_->setEnabled(false);
}

void MainWindow::togglePause()
{
    if (!scanning_) {
        return;
    }
    if (pauseControl_.isPaused()) {
        pauseControl_.resume();
    }
    else {
        pauseControl_.pause();
    }
    updatePauseUi();
}

void MainWindow::updatePauseUi()
{
    const bool paused = pauseControl_.isPaused();
    pauseButton_->setIcon(QApplication::style()->standardIcon(paused ? QStyle::SP_MediaPlay : QStyle::SP_MediaPause));
    pauseButton_->setToolTip(paused ? qs("Resume the comparison") : qs("Pause the comparison"));
    progress_->setStyleSheet(progressBarStyle(paused ? kProgressPaused : kProgressRunning));
    if (scanning_ && fraction_->isHidden()) {
        progressLabel_->setText(paused ? qs("Paused") : qs("Scanning...")); // still listing the folders
    }
}

void MainWindow::stopComparison()
{
    worker_.request_stop(); // also wakes a paused scan
    pauseButton_->setEnabled(false);
    stopButton_->setEnabled(false);
}

void MainWindow::scanStopped()
{
    scanning_ = false;
    endScanControls();
    progress_->setRange(0, 1);
    progress_->setValue(0);
    progressLabel_->setText(qs("Comparison stopped"));
    progressLabel_->show();
    fraction_->hide();
    percentLabel_->hide();
    updateFolderArea(true);
    updateProgressArea(true);
    updateUndoButton();
}

void MainWindow::scanFailed(const QString& message)
{
    scanning_ = false;
    endScanControls();
    progressLabel_->setText(qs("Comparison failed"));
    progressLabel_->show();
    fraction_->hide();
    percentLabel_->hide();
    updateFolderArea(true);
    updateProgressArea(true);
    updateUndoButton();
    QMessageBox::critical(this, qs("Comparison error"), qs("Could not compare the folders:\n\n") + message);
}

// =========================================================
// Actions
// =========================================================

void MainWindow::runCommand(const fs::path& relativePath, RowCommand command)
{
    try {
        const WaitCursor waitCursor;
        session_.apply(relativePath, command);
    }
    catch (const std::exception& error) {
        QMessageBox::critical(this, commandErrorTitle(command),
                              qs("Could not %1:\n\n%2\n\n%3")
                                  .arg(commandErrorVerb(command), toQString(relativePath),
                                       QString::fromLocal8Bit(error.what())));
    }
    populateFileList(); // only the rows that changed are rebuilt
    updateSummary();    // the Skipped counts follow the actions
    updateUndoButton();
    focusFileList();
}

void MainWindow::toggleFolder(const fs::path& relativePath)
{
    try {
        session_.toggleExpanded(relativePath);
    }
    catch (const std::exception& error) {
        QMessageBox::critical(this, qs("Error"), QString::fromLocal8Bit(error.what()));
    }
    populateFileList();
    focusFileList();
}

void MainWindow::undoLast()
{
    try {
        const WaitCursor waitCursor;
        (void)session_.undoLast();
    }
    catch (const std::exception& error) {
        QMessageBox::critical(this, qs("Undo error"),
                              qs("Could not undo the last action:\n\n%1").arg(QString::fromLocal8Bit(error.what())));
    }
    populateFileList();
    updateSummary();
    updateUndoButton();
    focusFileList();
}

void MainWindow::redoLast()
{
    try {
        const WaitCursor waitCursor;
        (void)session_.redoLast();
    }
    catch (const std::exception& error) {
        QMessageBox::critical(this, qs("Redo error"),
                              qs("Could not redo the last undone action:\n\n%1").arg(QString::fromLocal8Bit(error.what())));
    }
    populateFileList();
    updateSummary();
    updateUndoButton();
    focusFileList();
}

// =========================================================
// Selection / mass actions
// =========================================================

void MainWindow::selectRow(const fs::path& relativePath, bool selected)
{
    try {
        session_.setSelected(relativePath, selected);
    }
    catch (const std::exception& error) {
        QMessageBox::critical(this, qs("Error"), QString::fromLocal8Bit(error.what()));
    }
    populateFileList(); // the row and the selection bar follow
}

void MainWindow::selectAllClicked()
{
    if (session_.hasResult()) {
        const bool select = session_.selectionState(currentFilter_) != SelectionState::All;
        session_.setAllSelected(currentFilter_, select);
    }
    populateFileList();
    focusFileList();
}

void MainWindow::updateSelectionBar()
{
    const SelectionState state =
        session_.hasResult() ? session_.selectionState(currentFilter_) : SelectionState::None;
    switch (state) {
    case SelectionState::None:
        selectAllBox_->setCheckState(Qt::Unchecked);
        break;
    case SelectionState::Some:
        selectAllBox_->setCheckState(Qt::PartiallyChecked);
        break;
    case SelectionState::All:
        selectAllBox_->setCheckState(Qt::Checked);
        break;
    }
    selectAllBox_->setEnabled(session_.hasResult());

    selectionLabel_->setText(session_.hasResult() ? qs(formatCounts(session_.selectionCounts())) : QString());

    // The actions offered by every selected row, each in the column of its kind.
    for (QHBoxLayout* slot : massSlots_) {
        while (slot->count() > 0) {
            QLayoutItem* item = slot->takeAt(slot->count() - 1);
            if (QWidget* widget = item->widget()) {
                widget->hide();
                widget->deleteLater(); // may be the button being clicked
            }
            delete item;
        }
    }
    for (const RowButton& button : ComparisonSession::massButtons(session_.commonCommands())) {
        massSlots_.at(std::min(buttonSlot(button.command), std::size_t{2}))
            ->addWidget(makeActionButton(button, [this](RowCommand command) { runMassCommand(command); }));
    }
}

void MainWindow::runMassCommand(RowCommand command)
{
    std::vector<ActionFailure> failures;
    try {
        const WaitCursor waitCursor;
        failures = session_.applyToSelection(command);
    }
    catch (const std::exception& error) {
        QMessageBox::critical(this, commandErrorTitle(command), QString::fromLocal8Bit(error.what()));
    }
    populateFileList();
    updateSummary();
    updateUndoButton();
    focusFileList();

    if (!failures.empty()) {
        QString details;
        const auto listed = std::min(failures.size(), static_cast<std::size_t>(kMaxListedErrors));
        for (std::size_t index = 0; index < listed; ++index) {
            details += qs("\n\n%1\n%2").arg(nativePath(failures.at(index).path),
                                             QString::fromLocal8Bit(failures.at(index).message.c_str()));
        }
        if (failures.size() > listed) {
            details += qs("\n\n... and %1 more.").arg(failures.size() - listed);
        }
        QMessageBox::warning(this, commandErrorTitle(command),
                             qs("%1 row(s) could not be processed; the others were.").arg(failures.size()) + details);
    }
}

void MainWindow::focusFileList()
{
    scrollArea_->setFocus(Qt::OtherFocusReason);
}

// =========================================================
// Opening files
// =========================================================

std::optional<Location> MainWindow::chooseLocation(const fs::path& relativePath, const QString& action)
{
    const std::vector<Location> locations = session_.existingLocations(relativePath);
    if (locations.empty()) {
        QMessageBox::warning(this, qs("Not found"),
                             qs("This entry no longer exists in folder A nor in folder B:\n\n%1")
                                 .arg(nativePath(relativePath)));
        return std::nullopt;
    }
    if (locations.size() == 1) {
        return locations.front();
    }

    // Present on both sides: a small menu at the cursor picks the version.
    QMenu menu(this);
    std::vector<std::pair<const QAction*, Location>> choices;
    for (const Location& location : locations) {
        QAction* item = menu.addAction(qs("%1 in folder %2").arg(action, sideName(location.side)));
        item->setToolTip(nativePath(location.path));
        choices.emplace_back(item, location);
    }
    menu.setToolTipsVisible(true);

    const QAction* picked = menu.exec(QCursor::pos());
    for (const auto& [item, location] : choices) {
        if (item == picked) {
            return location;
        }
    }
    return std::nullopt;
}

void MainWindow::openEntry(const fs::path& relativePath)
{
    const auto location = chooseLocation(relativePath, qs("Open"));
    if (location && !QDesktopServices::openUrl(QUrl::fromLocalFile(toQString(location->path)))) {
        QMessageBox::warning(this, qs("Open error"), qs("Could not open:\n\n%1").arg(nativePath(location->path)));
    }
}

void MainWindow::revealEntry(const fs::path& relativePath)
{
    const auto location = chooseLocation(relativePath, qs("Show"));
    if (location && !revealInFileManager(location->path)) {
        QMessageBox::warning(this, qs("Open error"),
                             qs("Could not show in %1:\n\n%2").arg(fileManagerName(), nativePath(location->path)));
    }
}

} // namespace fc::gui

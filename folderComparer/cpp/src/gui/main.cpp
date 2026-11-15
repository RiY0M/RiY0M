#include "gui/MainWindow.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QString>

#include <exception>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv); // NOLINT(misc-const-correctness): owns the event loop
    QApplication::setApplicationName(QStringLiteral("Folder Comparer"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    try {
        fc::gui::MainWindow window;
        window.show();
        return QApplication::exec();
    }
    catch (const std::exception& error) {
        QMessageBox::critical(nullptr, QStringLiteral("Fatal error"), QString::fromLocal8Bit(error.what()));
        return 1;
    }
}

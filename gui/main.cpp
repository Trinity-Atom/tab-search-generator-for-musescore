#include "main_window.hpp"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Tab Search");
    QApplication::setOrganizationName("TabSearch");
    try {
        tabs::MainWindow window(std::make_shared<tabs::NativeHttpClient>());
        window.show();
        return app.exec();
    } catch (const std::exception& error) {
        QMessageBox::critical(nullptr, QObject::tr("Error al iniciar"),
                              QString::fromUtf8(error.what()));
        return 1;
    }
}

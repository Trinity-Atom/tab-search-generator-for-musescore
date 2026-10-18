#include "gui/main_window.hpp"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <map>
#include <thread>

namespace {
class FakeHttp final : public tabs::HttpClient {
public:
    std::map<std::string, std::string> pages;
    std::atomic<int> calls{0};
    std::atomic<bool> workerThread{true};
    int delay{0};
    std::string get(const std::string& url) override {
        ++calls;
        if (QThread::currentThread() == qApp->thread()) workerThread = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        const auto entry = pages.find(url);
        if (entry == pages.end()) throw tabs::ProviderError("HTTP 403 de prueba");
        return entry->second;
    }
};

const std::string firstUrl = "https://acordes.lacuerda.net/artista/primera.shtml";
const std::string secondUrl = "https://acordes.lacuerda.net/artista/segunda.shtml";

std::shared_ptr<FakeHttp> fixture() {
    auto http = std::make_shared<FakeHttp>();
    http->pages["https://acordes.lacuerda.net/busca.php?exp=prueba"] =
        "<ul id=i_main><a href='/artista/primera.shtml'>Primera</a>"
        "<a href='/artista/segunda.shtml'>Segunda</a></ul>";
    http->pages[firstUrl] = "<pre id=tCode>C G\nuno dos\nAm\nfin</pre>";
    http->pages[secondUrl] = "<pre id=tCode>F\notra</pre>";
    return http;
}

template<typename T>
T* control(tabs::MainWindow& window, const char* name) {
    auto* value = window.findChild<T*>(name);
    if (!value) qFatal("Control missing: %s", name);
    return value;
}

void search(tabs::MainWindow& window, const QString& query = "prueba") {
    control<QLineEdit>(window, "query")->setText(query);
    control<QPushButton>(window, "search")->click();
}

void answerDiscard(QMessageBox::StandardButton answer) {
    QTimer::singleShot(0, [answer] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog) dialog->button(answer)->click();
    });
}
} // namespace

class GuiTests : public QObject {
    Q_OBJECT

private slots:
    void searchLoadEditExport() {
        auto http = fixture();
        http->delay = 40;
        tabs::MainWindow window(http);
        window.show();
        auto* results = control<QTableWidget>(window, "results");
        auto* lines = control<QTableWidget>(window, "lines");
        auto* button = control<QPushButton>(window, "search");
        auto* exportButton = control<QPushButton>(window, "export");
        QVERIFY(!exportButton->isEnabled());
        search(window);
        QVERIFY(!button->isEnabled());
        bool responsive = false;
        QTimer::singleShot(0, [&] { responsive = true; });
        QTRY_VERIFY(responsive);
        QVERIFY(!button->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 5000);
        QCOMPARE(results->rowCount(), 2);
        QCOMPARE(http->calls.load(), 3);
        QVERIFY(http->workerThread);
        QVERIFY(control<QPlainTextEdit>(window, "warnings")->toPlainText().contains("CifraClub"));
        QVERIFY(control<QPlainTextEdit>(window, "warnings")->toPlainText().contains("UltimateGuitar"));
        QCOMPARE(lines->rowCount(), 0);
        results->setCurrentCell(0, 0);
        QTRY_VERIFY(exportButton->isEnabled());
        QCOMPARE(http->calls.load(), 4);
        QCOMPARE(lines->rowCount(), 3);
        lines->item(0, 0)->setText("Dm7");
        lines->item(0, 1)->setText("Texto sintetico");
        control<QLineEdit>(window, "title")->setText(QString::fromUtf8("Edicion \xC3\xB1 & prueba"));
        control<QSpinBox>(window, "capo")->setValue(3);
        QVERIFY(exportButton->isEnabled());

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QString::fromUtf8("partitura-\xC3\xB1.musicxml"));
        QVERIFY(window.saveCurrent(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto xml = file.readAll();
        QVERIFY(xml.contains("<root-step>D</root-step>"));
        QVERIFY(xml.contains(">minor-seventh</kind>"));
        QVERIFY(xml.contains("Capo 3"));
        QVERIFY(xml.contains(QString::fromUtf8("Edicion \xC3\xB1 &amp; prueba").toUtf8()));
        QVERIFY(xml.contains(firstUrl.c_str()));
        file.close();

        results->setCurrentCell(1, 0);
        QTRY_VERIFY(exportButton->isEnabled());
        QCOMPARE(lines->rowCount(), 1);
        results->setCurrentCell(0, 0);
        QCOMPARE(lines->item(0, 0)->text(), QString("Dm7"));
        QCOMPARE(lines->item(0, 1)->text(), QString("Texto sintetico"));
        QCOMPARE(http->calls.load(), 5);
        QCOMPARE(control<QSpinBox>(window, "capo")->value(), 3);

        lines->item(0, 0)->setText("incorrecto");
        QVERIFY(!exportButton->isEnabled());
        QVERIFY(!window.saveCurrent(path));
        QVERIFY(control<QLabel>(window, "validation")->text().contains("fila 1"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), xml); // An invalid edit must not overwrite the export.
        file.close();
        lines->item(0, 0)->setText("REm7");
        QVERIFY(exportButton->isEnabled());
        QVERIFY(window.saveCurrent(path));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("<root-step>D</root-step>"));
        file.close();
        lines->item(0, 0)->setText("C7(b9)/E");
        QVERIFY(exportButton->isEnabled());
        QVERIFY(window.saveCurrent(path));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto complexXml = file.readAll();
        QVERIFY(complexXml.contains("<bass-step>E</bass-step>"));
        QVERIFY(complexXml.contains("<degree-value>9</degree-value><degree-alter>-1"));
        QVERIFY(complexXml.contains("text=\"7(b9)\""));
        file.close();
        QVERIFY(!window.saveCurrent(directory.filePath("missing/score.musicxml")));
        QVERIFY(control<QLabel>(window, "status")->text().contains("No se pudo guardar"));
        QVERIFY(window.saveCurrent(path));
        QVERIFY(window.close());
    }

    void validationRowsAndDiscard() {
        auto http = fixture();
        tabs::MainWindow window(http);
        window.show();
        auto* button = control<QPushButton>(window, "search");
        auto* results = control<QTableWidget>(window, "results");
        auto* lines = control<QTableWidget>(window, "lines");
        auto* exportButton = control<QPushButton>(window, "export");
        search(window, "   ");
        QCOMPARE(http->calls.load(), 0);
        QVERIFY(control<QLabel>(window, "status")->text().contains("antes de buscar"));
        search(window);
        QTRY_VERIFY(button->isEnabled());
        results->setCurrentCell(0, 0);
        QTRY_VERIFY(exportButton->isEnabled());
        control<QLineEdit>(window, "title")->clear();
        QVERIFY(!exportButton->isEnabled());
        control<QLineEdit>(window, "title")->setText("Prueba");
        control<QLineEdit>(window, "artist")->setText(QString("Artista") + QChar(1));
        QVERIFY(!exportButton->isEnabled());
        QVERIFY(control<QLabel>(window, "validation")->text().contains("caracteres de control"));
        control<QLineEdit>(window, "artist")->setText("Artista");
        QVERIFY(exportButton->isEnabled());
        lines->selectAll();
        control<QPushButton>(window, "removeLines")->click();
        QCOMPARE(lines->rowCount(), 0);
        QVERIFY(!exportButton->isEnabled());
        control<QPushButton>(window, "addLine")->click();
        QCOMPARE(lines->rowCount(), 1);
        lines->item(0, 0)->setText("Bb");
        // Close the in-place editor before interacting with other controls.
        lines->setFocus();
        QTest::keyClick(lines, Qt::Key_Escape);
        QVERIFY(exportButton->isEnabled());
        answerDiscard(QMessageBox::Cancel);
        search(window);
        QCOMPARE(results->rowCount(), 2);
        QCOMPARE(lines->rowCount(), 1);
        answerDiscard(QMessageBox::Cancel);
        QVERIFY(!window.close());
        answerDiscard(QMessageBox::Discard);
        search(window);
        QTRY_VERIFY(button->isEnabled());
        QCOMPARE(lines->rowCount(), 0);
        QCOMPARE(results->rowCount(), 2);
        QVERIFY(!exportButton->isEnabled());
        QVERIFY(window.close());
    }

    void loadFailureRetryAndBusyClose() {
        auto http = fixture();
        http->pages.erase(firstUrl);
        http->delay = 35;
        tabs::MainWindow window(http);
        window.show();
        auto* button = control<QPushButton>(window, "search");
        auto* results = control<QTableWidget>(window, "results");
        search(window);
        QVERIFY(!window.close());
        QTRY_VERIFY(button->isEnabled());
        results->setCurrentCell(0, 0);
        QTRY_VERIFY(button->isEnabled());
        QVERIFY(!control<QPushButton>(window, "export")->isEnabled());
        QVERIFY(control<QLabel>(window, "status")->text().contains("HTTP 403"));
        QVERIFY(control<QLabel>(window, "source")->text().contains(firstUrl.c_str()));
        QVERIFY(control<QPushButton>(window, "openSource")->isEnabled());
        http->pages[firstUrl] = "<pre id=tCode>C\nprueba</pre>";
        results->cellDoubleClicked(0, 0);
        QTRY_VERIFY(control<QPushButton>(window, "export")->isEnabled());
        QCOMPARE(control<QTableWidget>(window, "lines")->rowCount(), 1);
        QVERIFY(window.close());
    }

    void emptyAndAllFailed() {
        auto http = fixture();
        http->pages["https://acordes.lacuerda.net/busca.php?exp=prueba"] =
            "<div class=s_res>0 resultados</div>";
        http->pages["https://solr.sscdn.co/cc/h2/?q=prueba"] = R"({"response":{"docs":[]}})";
        http->pages["https://www.ultimate-guitar.com/search.php?search_type=title&value=prueba"] =
            "<div class=js-store data-content='"
            "{\"store\":{\"page\":{\"data\":{\"results\":[]}}}}'></div>";
        tabs::MainWindow window(http);
        window.show();
        search(window);
        auto* button = control<QPushButton>(window, "search");
        QTRY_VERIFY(button->isEnabled());
        QVERIFY(control<QLabel>(window, "status")->text().contains("No se encontraron"));
        QVERIFY(control<QPlainTextEdit>(window, "warnings")->isHidden());
        search(window, "sin-red");
        QTRY_VERIFY(button->isEnabled());
        QVERIFY(control<QLabel>(window, "status")->text().contains("No hay resultados disponibles"));
        QVERIFY(!control<QPlainTextEdit>(window, "warnings")->isHidden());
        QCOMPARE(control<QTableWidget>(window, "results")->rowCount(), 0);
        QVERIFY(!control<QPushButton>(window, "export")->isEnabled());
        QVERIFY(window.close());
    }
};

QTEST_MAIN(GuiTests)
#include "gui_tests.moc"

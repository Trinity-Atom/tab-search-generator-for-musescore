#pragma once

#include "providers.hpp"

#include <QFutureWatcher>
#include <QMainWindow>
#include <memory>
#include <utility>

class QCloseEvent;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace tabs {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(std::shared_ptr<HttpClient> http, QWidget* parent = nullptr);
    bool saveCurrent(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Session {
        explicit Session(std::shared_ptr<HttpClient> client)
            : http(std::move(client)), service(*http) {}
        std::shared_ptr<HttpClient> http;
        SearchService service;
    };
    struct Outcome {
        SearchReport report;
        TabVersion version;
        QString error;
    };

    void startSearch();
    void selectVersion(int row);
    void showVersion();
    void clearEditor();
    void captureEdits();
    void addLine();
    void removeLines();
    void chooseExport();
    void setBusy(bool busy, const QString& message);
    void updateActions();
    QString validationError() const;
    bool confirmDiscard();
    void message(const QString& text);

    std::shared_ptr<Session> session_;
    QFutureWatcher<Outcome> watcher_;
    std::vector<TabVersion> versions_;
    std::vector<bool> loaded_;
    std::vector<bool> dirty_;
    int current_{-1};
    int loadingRow_{-1};
    bool busy_{false};
    bool searching_{false};
    bool populating_{false};

    QLineEdit* query_;
    QPushButton* search_;
    QTableWidget* results_;
    QPlainTextEdit* warnings_;
    QLabel* status_;
    QProgressBar* progress_;
    QGroupBox* editor_;
    QLineEdit* title_;
    QLineEdit* artist_;
    QSpinBox* capo_;
    QLabel* source_;
    QTableWidget* lines_;
    QPushButton* add_;
    QPushButton* remove_;
    QPushButton* export_;
    QPushButton* openSource_;
    QLabel* validation_;
};
} // namespace tabs

#include "main_window.hpp"
#include "musicxml.hpp"

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <algorithm>
#include <sstream>

namespace tabs {
namespace {
QString text(const std::string& value) { return QString::fromStdString(value); }

QTableWidget* table(const QString& name, const QStringList& columns, QWidget* parent) {
    auto* result = new QTableWidget(0, static_cast<int>(columns.size()), parent);
    result->setObjectName(name);
    result->setHorizontalHeaderLabels(columns);
    result->setAlternatingRowColors(true);
    result->setSelectionBehavior(QAbstractItemView::SelectRows);
    result->verticalHeader()->setVisible(false);
    result->horizontalHeader()->setStretchLastSection(true);
    return result;
}
} // namespace

MainWindow::MainWindow(std::shared_ptr<HttpClient> http, QWidget* parent)
    : QMainWindow(parent), session_(std::make_shared<Session>(std::move(http))) {
    setWindowTitle(tr("Tab Search - Acordes para MuseScore"));
    resize(1120, 780);
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    auto* heading = new QLabel(tr("Buscador y editor de acordes"), central);
    auto font = heading->font();
    font.setPointSize(font.pointSize() + 5);
    font.setBold(true);
    heading->setFont(font);
    layout->addWidget(heading);
    layout->addWidget(new QLabel(tr("LaCuerda / CifraClub / UltimateGuitar"), central));
    auto* searchRow = new QHBoxLayout;
    query_ = new QLineEdit(central);
    query_->setObjectName("query");
    query_->setPlaceholderText(tr("Nombre de la cancion o artista"));
    query_->setAccessibleName(tr("Cancion o artista"));
    query_->setClearButtonEnabled(true);
    search_ = new QPushButton(tr("&Buscar"), central);
    search_->setObjectName("search");
    searchRow->addWidget(query_, 1);
    searchRow->addWidget(search_);
    layout->addLayout(searchRow);
    status_ = new QLabel(tr("Escribe una cancion para comenzar."), central);
    status_->setObjectName("status");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->setAccessibleName(tr("Estado de la operacion"));
    layout->addWidget(status_);
    progress_ = new QProgressBar(central);
    progress_->setRange(0, 0);
    progress_->setTextVisible(false);
    progress_->setMaximumHeight(6);
    progress_->hide();
    layout->addWidget(progress_);
    auto* splitter = new QSplitter(Qt::Vertical, central);
    results_ = table("results", {tr("Cancion"), tr("Artista"), tr("Version"), tr("Proveedor")}, splitter);
    results_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results_->setSelectionMode(QAbstractItemView::SingleSelection);
    results_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    editor_ = new QGroupBox(tr("Vista previa y edicion"), splitter);
    auto* editorLayout = new QVBoxLayout(editor_);
    auto* form = new QFormLayout;
    title_ = new QLineEdit(editor_);
    title_->setObjectName("title");
    artist_ = new QLineEdit(editor_);
    artist_->setObjectName("artist");
    capo_ = new QSpinBox(editor_);
    capo_->setObjectName("capo");
    capo_->setRange(0, 24);
    form->addRow(tr("&Titulo:"), title_);
    form->addRow(tr("&Artista:"), artist_);
    form->addRow(tr("&Capo:"), capo_);
    editorLayout->addLayout(form);
    lines_ = table("lines", {tr("Acorde"), tr("Letra / seccion")}, editor_);
    lines_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    lines_->setMinimumHeight(150);
    lines_->setToolTip(tr("Un acorde por fila: C, Am7, Csus4, Cadd9, Bdim7, D/F# o C7(b9). "
                         "La letra es opcional."));
    editorLayout->addWidget(lines_, 1);
    auto* editRow = new QHBoxLayout;
    add_ = new QPushButton(tr("&Anadir fila"), editor_);
    add_->setObjectName("addLine");
    remove_ = new QPushButton(tr("&Eliminar filas"), editor_);
    remove_->setObjectName("removeLines");
    editRow->addWidget(add_);
    editRow->addWidget(remove_);
    editRow->addStretch();
    export_ = new QPushButton(tr("&Exportar MusicXML..."), editor_);
    export_->setObjectName("export");
    editRow->addWidget(export_);
    editorLayout->addLayout(editRow);
    validation_ = new QLabel(editor_);
    validation_->setObjectName("validation");
    validation_->setTextFormat(Qt::PlainText);
    validation_->setWordWrap(true);
    editorLayout->addWidget(validation_);
    auto* note = new QLabel(tr("Exportacion simplificada: 4/4, un acorde por negra y notas C4 de relleno. "
                              "La letra no se incluye en el MusicXML."), editor_);
    note->setWordWrap(true);
    editorLayout->addWidget(note);
    splitter->addWidget(results_);
    splitter->addWidget(editor_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter, 1);
    auto* sourceRow = new QHBoxLayout;
    source_ = new QLabel(central);
    source_->setObjectName("source");
    source_->setTextFormat(Qt::PlainText);
    source_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    source_->setWordWrap(true);
    openSource_ = new QPushButton(tr("Abrir fuente"), central);
    openSource_->setObjectName("openSource");
    sourceRow->addWidget(source_, 1);
    sourceRow->addWidget(openSource_);
    layout->addLayout(sourceRow);
    warnings_ = new QPlainTextEdit(central);
    warnings_->setObjectName("warnings");
    warnings_->setReadOnly(true);
    warnings_->setAccessibleName(tr("Avisos de proveedores"));
    warnings_->setMaximumHeight(90);
    warnings_->hide();
    layout->addWidget(warnings_);
    setCentralWidget(central);

    connect(search_, &QPushButton::clicked, this, [this] { startSearch(); });
    connect(query_, &QLineEdit::returnPressed, this, [this] { startSearch(); });
    connect(results_, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { selectVersion(row); });
    connect(results_, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) { selectVersion(row); });
    connect(title_, &QLineEdit::textChanged, this, [this] { captureEdits(); });
    connect(artist_, &QLineEdit::textChanged, this, [this] { captureEdits(); });
    connect(capo_, &QSpinBox::valueChanged, this, [this] { captureEdits(); });
    connect(lines_, &QTableWidget::cellChanged, this, [this] { captureEdits(); });
    connect(lines_, &QTableWidget::itemSelectionChanged, this, [this] { updateActions(); });
    connect(add_, &QPushButton::clicked, this, [this] { addLine(); });
    connect(remove_, &QPushButton::clicked, this, [this] { removeLines(); });
    connect(export_, &QPushButton::clicked, this, [this] { chooseExport(); });
    connect(openSource_, &QPushButton::clicked, this, [this] {
        if (current_ >= 0 && !QDesktopServices::openUrl(QUrl(text(versions_[current_].sourceUrl))))
            message(tr("No se pudo abrir el navegador para esta fuente."));
    });
    connect(&watcher_, &QFutureWatcher<Outcome>::finished, this, [this] {
        const auto outcome = watcher_.result();
        if (searching_) {
            if (!outcome.error.isEmpty()) {
                setBusy(false, tr("Error de busqueda: %1").arg(outcome.error));
                return;
            }
            versions_ = outcome.report.versions;
            loaded_.assign(versions_.size(), false);
            dirty_.assign(versions_.size(), false);
            QSignalBlocker blocker(results_);
            results_->setRowCount(static_cast<int>(versions_.size()));
            for (int row = 0; row < results_->rowCount(); ++row) {
                const auto& version = versions_[row];
                const QStringList values{text(version.title), text(version.artist),
                                         text(version.versionName), text(version.provider)};
                for (int col = 0; col < values.size(); ++col)
                    results_->setItem(row, col, new QTableWidgetItem(values[col]));
            }
            results_->clearSelection();
            results_->setCurrentCell(-1, -1);
            QStringList warnings;
            for (const auto& error : outcome.report.errors) warnings.push_back(text(error));
            warnings_->setPlainText(warnings.join('\n'));
            warnings_->setVisible(!warnings.isEmpty());
            const auto summary = versions_.empty()
                ? (warnings.isEmpty() ? tr("No se encontraron resultados.")
                                     : tr("No hay resultados disponibles. Revisa los avisos de proveedores."))
                : tr("%1 versiones. Selecciona una para descargar la vista previa.").arg(versions_.size());
            setBusy(false, summary);
        } else {
            if (!outcome.error.isEmpty()) {
                setBusy(false, tr("No se pudo cargar la version: %1. Haz doble clic para reintentar.")
                               .arg(outcome.error));
                return;
            }
            versions_[loadingRow_] = outcome.version;
            loaded_[loadingRow_] = true;
            showVersion();
            setBusy(false, tr("Contenido cargado. Puedes editar los acordes y exportar."));
        }
    });
    clearEditor();
    updateActions();
    query_->setFocus();
}

void MainWindow::message(const QString& value) { status_->setText(value); }

bool MainWindow::confirmDiscard() {
    if (std::none_of(dirty_.begin(), dirty_.end(), [](bool dirty) { return dirty; })) return true;
    return QMessageBox::question(this, tr("Cambios sin exportar"),
        tr("Hay cambios sin exportar. Se perderan al continuar. Deseas descartarlos?"),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Discard;
}

void MainWindow::startSearch() {
    if (busy_) return;
    const auto query = query_->text().trimmed();
    if (query.isEmpty()) {
        message(tr("Escribe una cancion o artista antes de buscar."));
        query_->setFocus();
        return;
    }
    if (!confirmDiscard()) return;
    current_ = -1;
    versions_.clear();
    loaded_.clear();
    dirty_.clear();
    {
        QSignalBlocker blocker(results_);
        results_->setRowCount(0);
    }
    clearEditor();
    warnings_->clear();
    warnings_->hide();
    searching_ = true;
    setBusy(true, tr("Buscando en LaCuerda, CifraClub y UltimateGuitar..."));
    watcher_.setFuture(QtConcurrent::run([session = session_, query] {
        Outcome outcome;
        try {
            outcome.report = session->service.search(query.toStdString());
        } catch (const std::exception& error) {
            outcome.error = text(error.what());
        }
        return outcome;
    }));
}

void MainWindow::selectVersion(int row) {
    if (busy_ || row < 0 || row >= static_cast<int>(versions_.size())) return;
    current_ = row;
    clearEditor();
    source_->setText(text(versions_[row].provider + " | " + versions_[row].sourceUrl));
    if (loaded_[row]) {
        showVersion();
        updateActions();
        message(tr("Version recuperada con sus cambios locales."));
        return;
    }
    searching_ = false;
    loadingRow_ = row;
    setBusy(true, tr("Descargando contenido publico de %1...").arg(text(versions_[row].provider)));
    watcher_.setFuture(QtConcurrent::run([session = session_, version = versions_[row]] {
        Outcome outcome;
        try {
            outcome.version = session->service.load(version);
        } catch (const std::exception& error) {
            outcome.error = text(error.what());
        }
        return outcome;
    }));
}

void MainWindow::clearEditor() {
    populating_ = true;
    title_->clear();
    artist_->clear();
    capo_->setValue(0);
    lines_->setRowCount(0);
    source_->clear();
    populating_ = false;
}

void MainWindow::showVersion() {
    populating_ = true;
    const auto& version = versions_[current_];
    title_->setText(text(version.title));
    artist_->setText(text(version.artist));
    capo_->setValue(version.capo);
    source_->setText(text(version.provider + " | " + version.sourceUrl));
    lines_->setRowCount(static_cast<int>(version.lines.size()));
    for (int row = 0; row < lines_->rowCount(); ++row) {
        lines_->setItem(row, 0, new QTableWidgetItem(text(version.lines[row].chord)));
        lines_->setItem(row, 1, new QTableWidgetItem(text(version.lines[row].lyric)));
    }
    populating_ = false;
}

void MainWindow::captureEdits() {
    if (populating_ || busy_ || current_ < 0 || !loaded_[current_]) return;
    auto& version = versions_[current_];
    version.title = title_->text().toStdString();
    version.artist = artist_->text().toStdString();
    version.capo = capo_->value();
    version.lines.clear();
    for (int row = 0; row < lines_->rowCount(); ++row) {
        const auto* chord = lines_->item(row, 0);
        const auto* lyric = lines_->item(row, 1);
        version.lines.push_back({chord ? chord->text().trimmed().toStdString() : "",
                                 lyric ? lyric->text().toStdString() : ""});
    }
    dirty_[current_] = true;
    results_->item(current_, 0)->setText(title_->text());
    results_->item(current_, 1)->setText(artist_->text());
    updateActions();
}

void MainWindow::addLine() {
    if (busy_ || current_ < 0 || !loaded_[current_]) return;
    const int row = lines_->rowCount();
    populating_ = true;
    lines_->insertRow(row);
    lines_->setItem(row, 0, new QTableWidgetItem);
    lines_->setItem(row, 1, new QTableWidgetItem);
    populating_ = false;
    captureEdits();
    lines_->setCurrentCell(row, 0);
    lines_->editItem(lines_->item(row, 0));
}

void MainWindow::removeLines() {
    auto selected = lines_->selectionModel()->selectedRows();
    std::sort(selected.begin(), selected.end(),
              [](const QModelIndex& a, const QModelIndex& b) { return a.row() > b.row(); });
    populating_ = true;
    for (const auto& row : selected) lines_->removeRow(row.row());
    populating_ = false;
    captureEdits();
}

QString MainWindow::validationError() const {
    if (current_ < 0 || !loaded_[current_]) return tr("Selecciona y carga una version.");
    const auto& version = versions_[current_];
    auto validXmlText = [](const std::string& value) {
        for (auto c : QString::fromStdString(value).toUcs4()) {
            if (c != 9 && c != 10 && c != 13 &&
                !(c >= 0x20 && c <= 0xD7FF) && !(c >= 0xE000 && c <= 0xFFFD) &&
                !(c >= 0x10000 && c <= 0x10FFFF))
                return false;
        }
        return true;
    };
    if (!validXmlText(version.title) || !validXmlText(version.artist))
        return tr("El titulo o artista contiene caracteres de control no permitidos en MusicXML.");
    if (QString::fromStdString(version.title).trimmed().isEmpty())
        return tr("El titulo no puede estar vacio.");
    bool hasChords = false;
    for (size_t i = 0; i < version.lines.size(); ++i) {
        const auto& chord = version.lines[i].chord;
        if (chord.empty()) continue;
        try {
            parseChord(chord);
        } catch (const ChordParseError& error) {
            return tr("Acorde no valido en la fila %1: %2").arg(i + 1).arg(text(error.what()));
        }
        hasChords = true;
    }
    return hasChords ? QString{} : tr("Anade al menos un acorde para exportar.");
}

void MainWindow::updateActions() {
    const bool editing = !busy_ && current_ >= 0 && loaded_[current_];
    editor_->setEnabled(editing);
    search_->setEnabled(!busy_);
    query_->setEnabled(!busy_);
    results_->setEnabled(!busy_);
    openSource_->setEnabled(!busy_ && current_ >= 0);
    remove_->setEnabled(editing && !lines_->selectionModel()->selectedRows().isEmpty());
    const auto error = validationError();
    validation_->setText(error);
    export_->setEnabled(editing && error.isEmpty());
}

void MainWindow::setBusy(bool busy, const QString& value) {
    busy_ = busy;
    progress_->setVisible(busy);
    message(value);
    updateActions();
}

bool MainWindow::saveCurrent(const QString& path) {
    if (busy_) {
        message(tr("Espera a que termine la operacion de red."));
        return false;
    }
    captureEdits();
    const auto error = validationError();
    if (!error.isEmpty()) {
        message(error);
        return false;
    }
    auto version = versions_[current_];
    std::ostringstream output;
    if (!writeMusicXml(version, output)) {
        message(tr("No se pudo generar el MusicXML."));
        return false;
    }
    QSaveFile file(path);
    const auto bytes = output.str();
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(bytes.data(), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size()) ||
        !file.commit()) {
        message(tr("No se pudo guardar el archivo: %1").arg(file.errorString()));
        return false;
    }
    dirty_[current_] = false;
    message(tr("Exportacion guardada: %1. Abrela en MuseScore.").arg(QDir::toNativeSeparators(path)));
    return true;
}

void MainWindow::chooseExport() {
    auto name = text(versions_[current_].title + "_" + versions_[current_].provider);
    name.replace(QRegularExpression(R"([<>:"/\\|?*\x00-\x1f])"), "_");
    QFileDialog dialog(this, tr("Exportar a MusicXML"), QDir::currentPath(),
                       tr("MusicXML (*.musicxml)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix("musicxml");
    dialog.selectFile(name + ".musicxml");
    if (dialog.exec() == QDialog::Accepted) saveCurrent(dialog.selectedFiles().front());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (busy_) {
        message(tr("Espera a que termine la operacion de red antes de cerrar."));
        event->ignore();
    } else if (!confirmDiscard()) {
        event->ignore();
    } else {
        event->accept();
    }
}
} // namespace tabs

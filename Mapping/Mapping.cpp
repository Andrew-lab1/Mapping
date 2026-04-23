#include "Mapping.h"

#include "HeatMapWindow.h"
#include "MotorController.h"
#include "PixelinkConnect.h"
#include "ThorlabsConnect.h"

#include <QDir>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QListWidgetItem>
#include <QLayout>
#include <QMetaObject>
#include <QMessageBox>
#include <QGuiApplication>
#include <QBrush>
#include <QColor>
#include <QSerialPortInfo>
#include <QScreen>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <algorithm>
#include <memory>

Mapping::Mapping(QWidget* parent)
    : QMainWindow(parent)
{
    setupUi();
    setupThreading();

    const Options options = currentOptions();
    m_heatMap.configure(options);
    updateResultPreview(options.scanArea.topLeft());
    updateButtons(State::Idle);
}

Mapping::~Mapping()
{
    emit stopRequested();
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }

    if (m_heatMapWindow) {
        m_heatMapWindow->close();
        delete m_heatMapWindow;
        m_heatMapWindow = nullptr;
    }
}

void Mapping::setupUi()
{
    ui.setupUi(this);

    setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget#centralWidget { background-color: #161616; color: #e6e6e6; }"
        "QTabWidget::pane { border: 1px solid #2a2a2a; background: #161616; }"
        "QTabBar::tab { background: #222222; color: #d7d7d7; padding: 6px 12px; }"
        "QTabBar::tab:selected { background: #2d2d2d; }"
        "QLabel { color: #e6e6e6; }"
        "QFrame#frameCameraPreview, QFrame#overlayMotorControls { background: #1d1d1d; border: 1px solid #2a2a2a; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QListWidget { background: #222222; color: #e6e6e6; border: 1px solid #373737; selection-background-color: #4a4a4a; }"
        "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { background: #4a4a4a; border-left: 1px solid #666666; width: 20px; }"
        "QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: #5a5a5a; }"
        "QAbstractSpinBox::up-button:pressed, QAbstractSpinBox::down-button:pressed { background: #3a3a3a; }"
        "QPushButton { background: #2a2a2a; color: #e6e6e6; border: 1px solid #3a3a3a; padding: 5px 10px; }"
        "QPushButton:hover { background: #333333; }"
        "QPushButton:pressed { background: #1f1f1f; }"
        "QStatusBar { background: #141414; color: #cfcfcf; }"));

    ui.comboDevice->clear();
    ui.comboDevice->addItem(QStringLiteral("PixelinkConnect"));
    ui.comboDevice->addItem(QStringLiteral("ThorlabsConnect"));
    ui.comboDevice->setCurrentIndex(0);
    m_selectedDevice = ui.comboDevice->currentText();

    if (ui.comboCalibration) {
        connect(ui.comboCalibration, QOverload<const QString&>::of(&QComboBox::currentTextChanged),
            this, &Mapping::onCalibrationSelectionChanged);
    }
    connect(ui.comboDevice, QOverload<const QString&>::of(&QComboBox::currentTextChanged),
        this, &Mapping::onDeviceSelectionChanged);

    populatePortCombos();

    ui.labelPixelinkPreview->setText(QStringLiteral("Ready"));
    ui.labelSpectrumPlaceholder->setText(QStringLiteral("Spectrum"));
    ui.labelResultsInfo->setText(QStringLiteral("Measurements: 0"));

    m_spectrumSeries = new QLineSeries(this);
    m_spectrumChart = new QChart();
    m_spectrumChart->legend()->hide();
    m_spectrumChart->addSeries(m_spectrumSeries);
    m_spectrumChart->setTitle(QString());
    m_spectrumChart->setTheme(QChart::ChartThemeDark);
    m_spectrumChart->setBackgroundRoundness(0.0);
    m_spectrumChart->setBackgroundBrush(QBrush(QColor(24, 24, 24)));
    m_spectrumChart->setPlotAreaBackgroundVisible(true);
    m_spectrumChart->setPlotAreaBackgroundBrush(QBrush(QColor(16, 16, 16)));

    m_axisX = new QValueAxis(this);
    m_axisY = new QValueAxis(this);
    m_axisX->setTitleText(QStringLiteral("Wavelength"));
    m_axisY->setTitleText(QStringLiteral("Intensity"));
    m_axisX->setRange(0.0, 10.0);
    m_axisY->setRange(0.0, 1.0);

    m_spectrumChart->addAxis(m_axisX, Qt::AlignBottom);
    m_spectrumChart->addAxis(m_axisY, Qt::AlignLeft);
    m_spectrumSeries->attachAxis(m_axisX);
    m_spectrumSeries->attachAxis(m_axisY);

    m_spectrumChartView = new QChartView(m_spectrumChart, this);
    m_spectrumChartView->setRenderHint(QPainter::Antialiasing, true);
    m_spectrumChartView->setMinimumHeight(260);

    if (ui.verticalLayoutSpectrumBottom) {
        ui.verticalLayoutSpectrumBottom->replaceWidget(ui.labelSpectrumPlaceholder, m_spectrumChartView);
        ui.labelSpectrumPlaceholder->hide();
    }

    onDeviceSelectionChanged(m_selectedDevice);

    if (ui.tabWidget && ui.tabSettings) {
        connect(ui.tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
            if (ui.tabWidget->widget(index) == ui.tabSettings) {
                populatePortCombos();
            }
        });
    }

    m_connectionStatusLabel = new QLabel(this);
    m_connectionStatusLabel->setText(QStringLiteral("Acquisition: Disconnected | Motor: Disconnected"));
    ui.statusBar->addWidget(m_connectionStatusLabel, 1);

    connect(ui.btnStartSequence, &QPushButton::clicked, this, &Mapping::onStartClicked);
    connect(ui.btnStopSequence, &QPushButton::clicked, this, &Mapping::onStopClicked);
    connect(ui.btnApplyRoi, &QPushButton::clicked, this, &Mapping::onApplyRoiClicked);
    connect(ui.btnResetRoi, &QPushButton::clicked, this, &Mapping::onResetRoiClicked);
    connect(ui.btnSaveSettings, &QPushButton::clicked, this, &Mapping::onSaveSettingsClicked);

    if (ui.spinRoiMin) {
        connect(ui.spinRoiMin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            refreshSpectrumPlotFromRoi();
        });
    }
    if (ui.spinRoiMax) {
        connect(ui.spinRoiMax, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            refreshSpectrumPlotFromRoi();
        });
    }

    connect(ui.btnMotorHome, &QPushButton::clicked, this, &Mapping::onManualHomeClicked);
    connect(ui.btnMotorUp, &QPushButton::clicked, this, &Mapping::onManualUpClicked);
    connect(ui.btnMotorDown, &QPushButton::clicked, this, &Mapping::onManualDownClicked);
    connect(ui.btnMotorLeft, &QPushButton::clicked, this, &Mapping::onManualLeftClicked);
    connect(ui.btnMotorRight, &QPushButton::clicked, this, &Mapping::onManualRightClicked);

    connect(ui.btnRefreshResults, &QPushButton::clicked, this, &Mapping::onRefreshResultsClicked);
    connect(ui.btnExportAll, &QPushButton::clicked, this, &Mapping::onExportAllClicked);
    connect(ui.btnOpenMeasurement, &QPushButton::clicked, this, &Mapping::onOpenMeasurementClicked);
    connect(ui.btnDeleteSelected, &QPushButton::clicked, this, &Mapping::onDeleteSelectedClicked);
    connect(ui.btnDeleteAll, &QPushButton::clicked, this, &Mapping::onDeleteAllClicked);
    connect(ui.btnBrowseResultsFolder, &QPushButton::clicked, this, &Mapping::onBrowseResultsFolderClicked);
    connect(ui.listMeasurements, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        onOpenMeasurementClicked();
    });

}

void Mapping::populatePortCombos()
{
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    const QString currentPortX = ui.comboPortX ? ui.comboPortX->currentData().toString() : QString();
    const QString currentPortY = ui.comboPortY ? ui.comboPortY->currentData().toString() : QString();

    const auto fillCombo = [&ports](QComboBox* combo, const QString& fallback, const QString& preferredPort, int preferredIndex) {
        combo->clear();
        if (ports.isEmpty()) {
            combo->addItem(fallback);
            return;
        }

        for (const QSerialPortInfo& port : ports) {
            QString label = port.portName();
            if (!port.description().isEmpty()) {
                label += QStringLiteral(" - ") + port.description();
            }
            combo->addItem(label, port.portName());
        }

        int selectedIndex = -1;
        if (!preferredPort.isEmpty()) {
            selectedIndex = combo->findData(preferredPort);
        }
        if (selectedIndex < 0) {
            selectedIndex = qBound(0, preferredIndex, combo->count() - 1);
        }
        combo->setCurrentIndex(selectedIndex);
    };

    fillCombo(ui.comboPortX, QStringLiteral("None"), currentPortX, 0);
    fillCombo(ui.comboPortY, QStringLiteral("None"), currentPortY, 1);
}

void Mapping::setupThreading()
{
    m_workerThread = new QThread(this);
    m_worker = new Worker();
    m_motorController = new MotorController();

    m_worker->moveToThread(m_workerThread);
    m_motorController->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, m_motorController, &QObject::deleteLater);

    connect(this, &Mapping::startRequested, m_worker, &Worker::start, Qt::QueuedConnection);
    connect(this, &Mapping::pauseRequested, m_worker, &Worker::pause, Qt::QueuedConnection);
    connect(this, &Mapping::resumeRequested, m_worker, &Worker::resume, Qt::QueuedConnection);
    connect(this, &Mapping::stopRequested, m_worker, &Worker::stop, Qt::QueuedConnection);
    connect(this, &Mapping::previewDecision, m_worker, &Worker::onPreviewAccepted, Qt::QueuedConnection);
    connect(this, &Mapping::deviceSelected, m_worker, &Worker::setDevice, Qt::QueuedConnection);
    connect(this, &Mapping::optionsChanged, m_worker, &Worker::setOptions, Qt::QueuedConnection);
    connect(this, &Mapping::manualMoveRequested, m_motorController, &MotorController::moveTo, Qt::QueuedConnection);

    connect(m_worker, &Worker::requestMove, m_motorController, &MotorController::moveTo, Qt::QueuedConnection);
    connect(m_motorController, &MotorController::motorReady, m_worker, &Worker::onMotorReady, Qt::QueuedConnection);

    connect(m_worker, &Worker::resultReady, this, &Mapping::onResultReady, Qt::QueuedConnection);
    connect(m_worker, &Worker::previewPointReached, this, &Mapping::onPreviewPointReached, Qt::QueuedConnection);
    connect(m_worker, &Worker::previewFinished, this, &Mapping::onPreviewFinished, Qt::QueuedConnection);
    connect(m_worker, &Worker::logMessage, this, &Mapping::onLogMessage, Qt::QueuedConnection);
    connect(m_motorController, &MotorController::logMessage, this, &Mapping::onLogMessage, Qt::QueuedConnection);
    connect(m_worker, &Worker::stateChanged, this, &Mapping::onStateChanged, Qt::QueuedConnection);
    connect(m_worker, &Worker::hardwareStatusChanged, this, &Mapping::onHardwareStatusChanged, Qt::QueuedConnection);
    connect(m_worker, &Worker::finished, this, &Mapping::onWorkerFinished, Qt::QueuedConnection);
    connect(m_worker, &Worker::finished, m_workerThread, &QThread::quit, Qt::QueuedConnection);

    m_workerThread->start();

    QMetaObject::invokeMethod(m_worker, [this]() {
        m_worker->setAcquisitionConnected(m_acquisitionConnected);
        m_worker->setMotorConnected(m_motorConnected);
    }, Qt::QueuedConnection);
}

void Mapping::onStartClicked()
{
    m_heatMap.clear();
    const Options options = currentOptions();
    m_heatMap.configure(options);
    m_lastPoint = options.scanArea.topLeft();
    updateResultPreview(m_lastPoint);
    m_measurements.clear();
    updateMeasurementsUi();
    ui.tabWidget->setCurrentWidget(ui.tabSpectrum);

    emit deviceSelected(ui.comboDevice->currentText());
    emit optionsChanged(options);
    emit startRequested();
}

void Mapping::onStopClicked()
{
    emit stopRequested();
}

void Mapping::onApplyRoiClicked()
{
    const double roiMin = ui.spinRoiMin->value();
    const double roiMax = ui.spinRoiMax->value();
    ui.statusBar->showMessage(QStringLiteral("ROI applied: %1 to %2").arg(roiMin).arg(roiMax), 3000);
    refreshSpectrumPlotFromRoi();
}

void Mapping::onResetRoiClicked()
{
    ui.spinRoiMin->setValue(0.0);
    ui.spinRoiMax->setValue(2048.0);
    ui.statusBar->showMessage(QStringLiteral("ROI reset"), 3000);
    refreshSpectrumPlotFromRoi();
}

void Mapping::onSaveSettingsClicked()
{
    emit optionsChanged(currentOptions());
    ui.statusBar->showMessage(QStringLiteral("Settings saved: device=%1, X=%2, Y=%3")
        .arg(ui.comboDevice->currentText())
        .arg(ui.comboPortX->currentText())
        .arg(ui.comboPortY->currentText()), 3000);
}

void Mapping::onManualHomeClicked()
{
    m_manualPosition = QPoint(0, 0);
    emit manualMoveRequested(m_manualPosition);
}

void Mapping::onManualUpClicked()
{
    const int step = std::max(1, ui.spinMotorStep->value());
    m_manualPosition.ry() -= step;
    emit manualMoveRequested(m_manualPosition);
}

void Mapping::onManualDownClicked()
{
    const int step = std::max(1, ui.spinMotorStep->value());
    m_manualPosition.ry() += step;
    emit manualMoveRequested(m_manualPosition);
}

void Mapping::onManualLeftClicked()
{
    const int step = std::max(1, ui.spinMotorStep->value());
    m_manualPosition.rx() -= step;
    emit manualMoveRequested(m_manualPosition);
}

void Mapping::onManualRightClicked()
{
    const int step = std::max(1, ui.spinMotorStep->value());
    m_manualPosition.rx() += step;
    emit manualMoveRequested(m_manualPosition);
}

void Mapping::onResultReady(const QPoint& position, const std::vector<double>& spectrum, const cv::Mat&)
{
    m_lastPoint = position;
    m_manualPosition = position;
    m_lastSpectrum = spectrum;
    m_heatMap.addSample(position, spectrum);
    updateSpectrumPlot(spectrum);
    updateResultPreview(position);

    m_measurements.push_back({ position, spectrum });
    updateMeasurementsUi();
}

void Mapping::onPreviewPointReached(const QPoint& position)
{
    ui.labelPixelinkPreview->setText(QStringLiteral("Preview point (%1,%2)")
        .arg(position.x())
        .arg(position.y()));
}

void Mapping::onPreviewFinished()
{
    auto* box = new QMessageBox(QMessageBox::Question,
        QStringLiteral("Preview"),
        QStringLiteral("Is the area correct?"),
        QMessageBox::Yes | QMessageBox::No,
        this);

    connect(box, &QMessageBox::finished, this, [this, box](int result) {
        const bool accepted = (result == QMessageBox::Yes);
        emit previewDecision(accepted);
        box->deleteLater();
    });

    box->open();
}

void Mapping::onLogMessage(const QString& message)
{
    appendLog(message);
}

void Mapping::onWorkerFinished()
{
    onLogMessage(QStringLiteral("Worker thread sequence finished"));
}

void Mapping::onStateChanged(State state)
{
    m_lastState = state;

    QString stateText;
    switch (state) {
    case State::Idle: stateText = QStringLiteral("Idle"); break;
    case State::Preview: stateText = QStringLiteral("Preview"); break;
    case State::WaitingForUser: stateText = QStringLiteral("WaitingForUser"); break;
    case State::WaitingForReconnect: stateText = QStringLiteral("WaitingForReconnect"); break;
    case State::Running: stateText = QStringLiteral("Running"); break;
    case State::Paused: stateText = QStringLiteral("Paused"); break;
    case State::Stopping: stateText = QStringLiteral("Stopping"); break;
    }

    ui.labelPixelinkPreview->setText(QStringLiteral("State: %1 | Last (%2,%3)")
        .arg(stateText)
        .arg(m_lastPoint.x())
        .arg(m_lastPoint.y()));
    updateButtons(state);
}

void Mapping::onHardwareStatusChanged(bool acquisitionConnected, bool motorConnected)
{
    m_acquisitionConnected = acquisitionConnected;
    m_motorConnected = motorConnected;
    if (m_connectionStatusLabel) {
        m_connectionStatusLabel->setText(QStringLiteral("Acquisition: %1 | Motor: %2")
            .arg(acquisitionConnected ? QStringLiteral("Connected") : QStringLiteral("Disconnected"))
            .arg(motorConnected ? QStringLiteral("Connected") : QStringLiteral("Disconnected")));
    }

    if (acquisitionConnected && motorConnected) {
        ui.statusBar->showMessage(QStringLiteral("All required devices connected"), 3000);
    }
    else {
        ui.statusBar->showMessage(QStringLiteral("Waiting for required devices"), 3000);
    }

    updateButtons(m_lastState);
}

void Mapping::updateResultPreview(const QPoint& lastPoint)
{
    const double value = m_heatMap.valueAt(lastPoint);
    ui.labelPixelinkPreview->setText(QStringLiteral("Last point (%1,%2), heat=%3")
        .arg(lastPoint.x())
        .arg(lastPoint.y())
        .arg(QString::number(value, 'f', 2)));
}

void Mapping::updateButtons(State state)
{
    const bool idle = (state == State::Idle);
    ui.btnStartSequence->setEnabled(idle && requiredHardwareConnected());
    ui.btnStopSequence->setEnabled(!idle);
}

bool Mapping::requiredHardwareConnected() const
{
    return m_acquisitionConnected && m_motorConnected;
}

Options Mapping::currentOptions() const
{
    Options options;
    const int stepX = std::max(1, ui.spinStepX->value());
    const int stepY = std::max(1, ui.spinStepY->value());
    options.stepSize = std::min(stepX, stepY);
    options.scanArea = QRect(0, 0, ui.spinScanWidth->value(), ui.spinScanHeight->value());
    options.previewEnabled = true;
    options.acquisitionDevice = ui.comboDevice ? ui.comboDevice->currentText() : m_selectedDevice;
    options.calibrationName = ui.comboCalibration ? ui.comboCalibration->currentText() : m_selectedCalibration;
    options.motorPortX = ui.comboPortX->currentData().isValid() ? ui.comboPortX->currentData().toString() : ui.comboPortX->currentText();
    options.motorPortY = ui.comboPortY->currentData().isValid() ? ui.comboPortY->currentData().toString() : ui.comboPortY->currentText();
    
    // Parse exposure sequence from UI (comma-separated values in milliseconds)
    const QString exposureText = ui.editExposureSequence->text().trimmed();
    if (!exposureText.isEmpty()) {
        const QStringList parts = exposureText.split(QChar(','), Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            bool ok = false;
            const double exposure = part.trimmed().toDouble(&ok);
            if (ok && exposure > 0.0) {
                options.exposureSequence.push_back(exposure);
            }
        }
    }
    // If no exposure sequence specified, use single default measurement
    if (options.exposureSequence.empty()) {
        options.exposureSequence.push_back(ui.spinExposureSpectrum->value());
    }
    
    return options;
}

void Mapping::onRefreshResultsClicked()
{
    updateMeasurementsUi();
    updateResultPreview(m_lastPoint);
    appendLog(QStringLiteral("Results refreshed"));
}

void Mapping::onExportAllClicked()
{
    if (m_measurements.empty()) {
        appendLog(QStringLiteral("No measurements to export"));
        return;
    }

    const QString baseDir = QDir::cleanPath(ui.editResultsFolderPath->text().trimmed());
    if (!baseDir.isEmpty()) {
        QDir().mkpath(baseDir);
    }

    const QString suggested = baseDir.isEmpty()
        ? QStringLiteral("measurements.csv")
        : QDir(baseDir).filePath(QStringLiteral("measurements.csv"));

    const QString filePath = QFileDialog::getSaveFileName(this,
        QStringLiteral("Export measurements"),
        suggested,
        QStringLiteral("CSV Files (*.csv)"));
    if (filePath.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        appendLog(QStringLiteral("Failed to open export file"));
        return;
    }

    QTextStream out(&file);
    out << "index,x,y,points,avg\n";
    for (int i = 0; i < static_cast<int>(m_measurements.size()); ++i) {
        const MeasurementEntry& m = m_measurements[static_cast<size_t>(i)];
        out << i << ","
            << m.position.x() << ","
            << m.position.y() << ","
            << m.spectrum.size() << ","
            << QString::number(averageSpectrum(m.spectrum), 'f', 4) << "\n";
    }

    appendLog(QStringLiteral("Exported %1 measurements to %2")
        .arg(m_measurements.size())
        .arg(QDir::toNativeSeparators(filePath)));
}

void Mapping::onOpenMeasurementClicked()
{
    const int row = ui.listMeasurements->currentRow();
    if (row < 0 || row >= static_cast<int>(m_measurements.size())) {
        appendLog(QStringLiteral("Select a measurement first"));
        return;
    }

    const MeasurementEntry& m = m_measurements[static_cast<size_t>(row)];
    m_lastPoint = m.position;
    updateSpectrumPlot(m.spectrum);
    showHeatMapWindow();
}

void Mapping::onDeleteSelectedClicked()
{
    const int row = ui.listMeasurements->currentRow();
    if (row < 0 || row >= static_cast<int>(m_measurements.size())) {
        appendLog(QStringLiteral("No measurement selected for deletion"));
        return;
    }

    m_measurements.erase(m_measurements.begin() + row);
    updateMeasurementsUi();
    appendLog(QStringLiteral("Deleted selected measurement"));
}

void Mapping::onDeleteAllClicked()
{
    if (m_measurements.empty()) {
        return;
    }

    m_measurements.clear();
    m_heatMap.clear();
    updateSpectrumPlot({});
    updateMeasurementsUi();
    updateResultPreview(m_lastPoint);
    appendLog(QStringLiteral("Deleted all measurements"));
}

void Mapping::onBrowseResultsFolderClicked()
{
    const QString selected = QFileDialog::getExistingDirectory(this,
        QStringLiteral("Select results folder"),
        QDir::cleanPath(ui.editResultsFolderPath->text()));
    if (selected.isEmpty()) {
        return;
    }

    ui.editResultsFolderPath->setText(QDir::toNativeSeparators(selected));
}

void Mapping::updateMeasurementsUi()
{
    ui.listMeasurements->clear();
    for (int i = 0; i < static_cast<int>(m_measurements.size()); ++i) {
        const MeasurementEntry& m = m_measurements[static_cast<size_t>(i)];
        ui.listMeasurements->addItem(QStringLiteral("%1: (%2,%3) pts=%4 avg=%5")
            .arg(i)
            .arg(m.position.x())
            .arg(m.position.y())
            .arg(m.spectrum.size())
            .arg(QString::number(averageSpectrum(m.spectrum), 'f', 2)));
    }
    ui.labelResultsInfo->setText(QStringLiteral("Measurements: %1").arg(m_measurements.size()));
}

void Mapping::appendLog(const QString& message)
{
    m_logs.push_back(message);
    while (m_logs.size() > 300) {
        m_logs.removeFirst();
    }
    ui.statusBar->showMessage(message, 3000);
}

double Mapping::averageSpectrum(const std::vector<double>& spectrum) const
{
    if (spectrum.empty()) {
        return 0.0;
    }

    double sum = 0.0;
    for (double value : spectrum) {
        sum += value;
    }
    return sum / static_cast<double>(spectrum.size());
}

void Mapping::updateSpectrumPlot(const std::vector<double>& spectrum)
{
    if (!m_spectrumSeries || !m_axisX || !m_axisY) {
        return;
    }

    m_spectrumSeries->clear();
    if (spectrum.empty()) {
        m_axisX->setRange(0.0, 10.0);
        m_axisY->setRange(0.0, 1.0);
        return;
    }

    double minY = spectrum.front();
    double maxY = spectrum.front();

    double roiMin = ui.spinRoiMin ? ui.spinRoiMin->value() : 0.0;
    double roiMax = ui.spinRoiMax ? ui.spinRoiMax->value() : double(qMax(1, static_cast<int>(spectrum.size()) - 1));
    if (roiMax < roiMin) {
        std::swap(roiMin, roiMax);
    }
    const double roiSpan = qMax(1e-9, roiMax - roiMin);

    for (int i = 0; i < static_cast<int>(spectrum.size()); ++i) {
        const double y = spectrum[static_cast<size_t>(i)];
        const double x = roiMin + (roiSpan * double(i) / qMax(1, static_cast<int>(spectrum.size()) - 1));
        m_spectrumSeries->append(x, y);
        minY = qMin(minY, y);
        maxY = qMax(maxY, y);
    }

    if (qFuzzyCompare(minY + 1.0, maxY + 1.0)) {
        minY -= 1.0;
        maxY += 1.0;
    }

    m_axisX->setRange(roiMin, roiMax);
    m_axisY->setRange(minY, maxY);
}

void Mapping::refreshSpectrumPlotFromRoi()
{
    updateSpectrumPlot(m_lastSpectrum);
}

void Mapping::showHeatMapWindow()
{
    if (!m_heatMapWindow) {
        m_heatMapWindow = new HeatMapWindow(this);
    }

    m_heatMapWindow->setHeatMapData(m_heatMap.toMatrix(), m_lastPoint);
    m_heatMapWindow->show();
    m_heatMapWindow->raise();
    m_heatMapWindow->activateWindow();
}

void Mapping::onDeviceSelectionChanged(const QString& deviceName)
{
    m_selectedDevice = deviceName;
    updateCalibrationCombo();
    appendLog(QStringLiteral("Device selected: %1").arg(deviceName));
}

void Mapping::onCalibrationSelectionChanged(const QString& calibrationName)
{
    m_selectedCalibration = calibrationName;
    appendLog(QStringLiteral("Calibration selected: %1").arg(calibrationName));
}

void Mapping::updateCalibrationCombo()
{
    if (!ui.comboCalibration) {
        return;
    }

    const QString previousCalibration = m_selectedCalibration;
    ui.comboCalibration->blockSignals(true);
    ui.comboCalibration->clear();

    std::unique_ptr<IDataAcquisition> device;
    if (m_selectedDevice.compare(QStringLiteral("ThorlabsConnect"), Qt::CaseInsensitive) == 0) {
        device = std::make_unique<ThorlabsConnect>();
    }
    else {
        device = std::make_unique<PixelinkConnect>();
    }

    const auto calibrations = device->getAvailableCalibrations();
    for (const QString& cal : calibrations) {
        ui.comboCalibration->addItem(cal);
    }

    int calibrationIndex = -1;
    if (!previousCalibration.isEmpty()) {
        calibrationIndex = ui.comboCalibration->findText(previousCalibration, Qt::MatchExactly);
    }
    if (calibrationIndex < 0 && ui.comboCalibration->count() > 0) {
        calibrationIndex = 0;
    }

    if (calibrationIndex >= 0) {
        ui.comboCalibration->setCurrentIndex(calibrationIndex);
        m_selectedCalibration = ui.comboCalibration->currentText();
    }
    else {
        m_selectedCalibration.clear();
    }

    ui.comboCalibration->blockSignals(false);
}


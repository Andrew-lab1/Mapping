#pragma once

#include "HeatMap.h"
#include "Options.h"
#include "worker.h"
#include "ui_Mapping.h"

#include <QMainWindow>
#include <QPoint>
#include <QStringList>
#include <QLabel>

#include <opencv2/core.hpp>

#include <vector>

class QThread;
class Worker;
class MotorController;
class HeatMapWindow;
class QChart;
class QChartView;
class QLineSeries;
class QValueAxis;

class Mapping : public QMainWindow {
    Q_OBJECT

public:
    explicit Mapping(QWidget* parent = nullptr);
    ~Mapping() override;

signals:
    void startRequested();
    void pauseRequested();
    void resumeRequested();
    void stopRequested();
    void previewDecision(bool accepted);
    void deviceSelected(const QString& deviceName);
    void optionsChanged(const Options& options);
    void manualMoveRequested(const QPoint& target);

private slots:
    void onStartClicked();
    void onStopClicked();
    void onApplyRoiClicked();
    void onResetRoiClicked();
    void onSaveSettingsClicked();
    void onManualHomeClicked();
    void onManualUpClicked();
    void onManualDownClicked();
    void onManualLeftClicked();
    void onManualRightClicked();
    void onRefreshResultsClicked();
    void onExportAllClicked();
    void onOpenMeasurementClicked();
    void onDeleteSelectedClicked();
    void onDeleteAllClicked();
    void onBrowseResultsFolderClicked();

    void onResultReady(const QPoint& position, const std::vector<double>& spectrum, const cv::Mat& image);
    void onPreviewPointReached(const QPoint& position);
    void onPreviewFinished();
    void onLogMessage(const QString& message);
    void onWorkerFinished();
    void onStateChanged(State state);
    void onHardwareStatusChanged(bool acquisitionConnected, bool motorConnected);
    void onDeviceSelectionChanged(const QString& deviceName);
    void onCalibrationSelectionChanged(const QString& calibrationName);

private:
    struct MeasurementEntry {
        QPoint position;
        std::vector<double> spectrum;
    };

    void setupUi();
    void setupThreading();
    void populatePortCombos();
    bool requiredHardwareConnected() const;
    void updateResultPreview(const QPoint& lastPoint);
    void updateButtons(State state);
    Options currentOptions() const;
    void updateMeasurementsUi();
    void appendLog(const QString& message);
    double averageSpectrum(const std::vector<double>& spectrum) const;
    void updateSpectrumPlot(const std::vector<double>& spectrum);
    void refreshSpectrumPlotFromRoi();
    void showHeatMapWindow();
    void updateCalibrationCombo();

    Ui::SpektrometrClass ui;

    QThread* m_workerThread = nullptr;
    Worker* m_worker = nullptr;
    MotorController* m_motorController = nullptr;
    HeatMapWindow* m_heatMapWindow = nullptr;

    QChartView* m_spectrumChartView = nullptr;
    QChart* m_spectrumChart = nullptr;
    QLineSeries* m_spectrumSeries = nullptr;
    QValueAxis* m_axisX = nullptr;
    QValueAxis* m_axisY = nullptr;
    QLabel* m_connectionStatusLabel = nullptr;

    QString m_selectedDevice = QStringLiteral("PixelinkConnect");
    QString m_selectedCalibration;
    std::vector<double> m_lastSpectrum;

    HeatMap m_heatMap;
    State m_lastState = State::Idle;
    bool m_acquisitionConnected = false;
    bool m_motorConnected = false;
    QPoint m_lastPoint = QPoint(0, 0);
    QPoint m_manualPosition = QPoint(0, 0);
    std::vector<MeasurementEntry> m_measurements;
    QStringList m_logs;
};

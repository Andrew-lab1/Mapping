#pragma once

#include "IDataAcquisition.h"
#include "Options.h"

#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QPoint>

#include <opencv2/core.hpp>

#include <memory>
#include <vector>

enum class State {
    Idle,
    Preview,
    WaitingForUser,
    WaitingForReconnect,
    Running,
    Paused,
    Stopping
};

Q_DECLARE_METATYPE(std::vector<double>)
Q_DECLARE_METATYPE(cv::Mat)
Q_DECLARE_METATYPE(State)

class Worker : public QObject {
    Q_OBJECT

public:
    explicit Worker(QObject* parent = nullptr);

public slots:
    void start();
    void pause();
    void resume();
    void stop();
    void onMotorReady();
    void onPreviewAccepted(bool accepted);
    void setDevice(const QString& deviceName);
    void setOptions(const Options& options);
    void setAcquisitionConnected(bool connected);
    void setMotorConnected(bool connected);
    bool acquisitionConnected() const;
    bool motorConnected() const;

signals:
    void requestMove(const QPoint& position);
    void resultReady(const QPoint& position, const std::vector<double>& spectrum, const cv::Mat& image);
    void logMessage(const QString& message);
    void previewPointReached(const QPoint& position);
    void previewFinished();
    void finished();
    void stateChanged(State state);
    void hardwareStatusChanged(bool acquisitionConnected, bool motorConnected);

    void stopSignal();
    void motorReadyInternal();
    void previewDecisionInternal();

private:
    bool ensureHardwareReady(bool allowWait, State resumeState);
    bool retryHardwareConnection(const QString& name, bool& connectedFlag);
    bool waitForHardwareReconnect(State resumeState);
    void updateHardwareStatus();
    bool runPreviewPhase();
    bool runMeasurementPhase();
    bool waitForMotorMove(const QPoint& target);
    bool waitForPreviewDecision();
    bool waitForAcquisitionDelay(int ms);
    bool waitIfPaused();
    bool isStopping() const;
    std::vector<QPoint> buildSnakeGrid() const;
    std::vector<QPoint> buildPreviewPoints() const;
    QPoint centerPoint() const;
    std::unique_ptr<IDataAcquisition> createDevice() const;
    void setState(State state);

    mutable QMutex m_mutex;
    QWaitCondition m_pauseCondition;

    bool m_paused = false;
    bool m_stopping = false;
    bool m_motorReady = false;
    bool m_previewDecisionPending = false;
    bool m_previewAccepted = false;
    bool m_running = false;

    QString m_deviceName = QStringLiteral("PixelinkConnect");
    Options m_options;
    State m_state = State::Idle;
    bool m_acquisitionConnected = true;
    bool m_motorDeviceConnected = true;
};

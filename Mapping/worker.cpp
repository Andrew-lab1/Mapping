#include "worker.h"

#include "PixelinkConnect.h"
#include "ThorlabsConnect.h"

#include <QEventLoop>
#include <QTimer>

#include <algorithm>

Worker::Worker(QObject* parent)
    : QObject(parent)
{
}

void Worker::start()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_running) {
            emit logMessage(QStringLiteral("Worker already running"));
            return;
        }
        m_running = true;
        m_paused = false;
        m_stopping = false;
        m_motorReady = false;
    }

    emit logMessage(QStringLiteral("Starting sequence with %1 (calibration: %2)")
        .arg(m_deviceName)
        .arg(m_options.calibrationName.isEmpty() ? QStringLiteral("<default>") : m_options.calibrationName));
    emit logMessage(QStringLiteral("Motion ports: X=%1, Y=%2")
        .arg(m_options.motorPortX.isEmpty() ? QStringLiteral("<none>") : m_options.motorPortX,
             m_options.motorPortY.isEmpty() ? QStringLiteral("<none>") : m_options.motorPortY));

    if (!ensureHardwareReady(false, State::Idle)) {
        emit logMessage(QStringLiteral("Start blocked: required devices are not connected"));
        setState(State::Idle);
        {
            QMutexLocker locker(&m_mutex);
            m_running = false;
        }
        emit finished();
        return;
    }

    // Worker runs entirely in its own thread; all communication to UI/motor is signal-based.
    bool ok = true;
    if (m_options.previewEnabled) {
        ok = runPreviewPhase();
    }

    if (ok && !isStopping()) {
        ok = runMeasurementPhase();
    }

    setState(State::Idle);
    {
        QMutexLocker locker(&m_mutex);
        m_running = false;
    }

    emit logMessage(ok ? QStringLiteral("Sequence finished") : QStringLiteral("Sequence interrupted"));
    emit finished();
}

void Worker::pause()
{
    bool changed = false;
    QMutexLocker locker(&m_mutex);
    if (!m_running || m_stopping) {
        return;
    }
    if (!m_paused) {
        m_paused = true;
        changed = true;
    }
    locker.unlock();
    if (changed) {
        setState(State::Paused);
    }
    emit logMessage(QStringLiteral("Pause requested"));
}

void Worker::resume()
{
    bool changed = false;
    QMutexLocker locker(&m_mutex);
    if (!m_running || m_stopping) {
        return;
    }
    if (m_paused) {
        m_paused = false;
        changed = true;
    }
    m_pauseCondition.wakeAll();
    locker.unlock();
    if (changed) {
        setState(State::Running);
    }
    emit logMessage(QStringLiteral("Resume requested"));
}

void Worker::stop()
{
    {
        QMutexLocker locker(&m_mutex);
        if (!m_running) {
            return;
        }
        m_stopping = true;
        m_paused = false;
        m_pauseCondition.wakeAll();
    }

    setState(State::Stopping);
    emit stopSignal();
    emit logMessage(QStringLiteral("Stop requested"));
}

void Worker::onMotorReady()
{
    {
        QMutexLocker locker(&m_mutex);
        m_motorReady = true;
    }
    emit motorReadyInternal();
}

void Worker::onPreviewAccepted(bool accepted)
{
    {
        QMutexLocker locker(&m_mutex);
        m_previewAccepted = accepted;
        m_previewDecisionPending = false;
    }
    emit previewDecisionInternal();
}

void Worker::setDevice(const QString& deviceName)
{
    QMutexLocker locker(&m_mutex);
    m_deviceName = deviceName;
}

void Worker::setOptions(const Options& options)
{
    QMutexLocker locker(&m_mutex);
    m_options = options;
    if (!options.acquisitionDevice.isEmpty()) {
        m_deviceName = options.acquisitionDevice;
    }
}

void Worker::setAcquisitionConnected(bool connected)
{
    {
        QMutexLocker locker(&m_mutex);
        m_acquisitionConnected = connected;
    }
    updateHardwareStatus();
}

void Worker::setMotorConnected(bool connected)
{
    {
        QMutexLocker locker(&m_mutex);
        m_motorDeviceConnected = connected;
    }
    updateHardwareStatus();
}

bool Worker::acquisitionConnected() const
{
    QMutexLocker locker(&m_mutex);
    return m_acquisitionConnected;
}

bool Worker::motorConnected() const
{
    QMutexLocker locker(&m_mutex);
    return m_motorDeviceConnected;
}

void Worker::updateHardwareStatus()
{
    emit hardwareStatusChanged(acquisitionConnected(), motorConnected());
}

bool Worker::retryHardwareConnection(const QString& name, bool& connectedFlag)
{
    Q_UNUSED(connectedFlag)
    for (int attempt = 1; attempt <= 3 && !isStopping(); ++attempt) {
        emit logMessage(QStringLiteral("Retrying %1 connection %2/3").arg(name).arg(attempt));
        QEventLoop loop;
        QTimer::singleShot(500, &loop, &QEventLoop::quit);
        connect(this, &Worker::stopSignal, &loop, &QEventLoop::quit, Qt::UniqueConnection);
        loop.exec();

        const bool connectedNow = name.contains(QStringLiteral("Acquisition"), Qt::CaseInsensitive)
            ? acquisitionConnected()
            : motorConnected();
        if (connectedNow) {
            emit logMessage(QStringLiteral("%1 reconnected").arg(name));
            return true;
        }
    }

    emit logMessage(QStringLiteral("%1 unavailable after 3 attempts; waiting for user reconnect").arg(name));
    return false;
}

bool Worker::waitForHardwareReconnect(State resumeState)
{
    setState(State::WaitingForReconnect);
    emit logMessage(QStringLiteral("Waiting for hardware reconnect"));

    while (!isStopping()) {
        if (acquisitionConnected() && motorConnected()) {
            emit logMessage(QStringLiteral("Hardware reconnected, resuming sequence"));
            setState(resumeState);
            return true;
        }

        QEventLoop loop;
        QTimer::singleShot(500, &loop, &QEventLoop::quit);
        connect(this, &Worker::stopSignal, &loop, &QEventLoop::quit, Qt::UniqueConnection);
        loop.exec();
    }

    return false;
}

bool Worker::ensureHardwareReady(bool allowWait, State resumeState)
{
    if (acquisitionConnected() && motorConnected()) {
        return true;
    }

    bool acquisition = acquisitionConnected();
    bool motor = motorConnected();

    if (!acquisition && !retryHardwareConnection(QStringLiteral("Acquisition device"), acquisition)) {
        acquisition = false;
    }
    if (!motor && !retryHardwareConnection(QStringLiteral("Motor device"), motor)) {
        motor = false;
    }

    if (acquisitionConnected() && motorConnected()) {
        return true;
    }

    if (!allowWait) {
        return false;
    }

    return waitForHardwareReconnect(resumeState);
}

bool Worker::runPreviewPhase()
{
    // Preview flow: center -> 4 corners -> wait for async UI accept/reject decision.
    setState(State::Preview);

    const QPoint center = centerPoint();
    if (!waitForMotorMove(center)) {
        return false;
    }

    const std::vector<QPoint> previewPoints = buildPreviewPoints();
    for (const QPoint& point : previewPoints) {
        if (!waitIfPaused() || isStopping()) {
            return false;
        }
        if (!ensureHardwareReady(true, State::Preview)) {
            emit logMessage(QStringLiteral("Hardware lost during preview"));
            return false;
        }
        if (!waitForMotorMove(point)) {
            return false;
        }
        emit previewPointReached(point);
    }

    setState(State::WaitingForUser);
    emit previewFinished();

    if (!waitForPreviewDecision()) {
        return false;
    }

    bool accepted = false;
    {
        QMutexLocker locker(&m_mutex);
        accepted = m_previewAccepted;
    }

    if (!accepted) {
        emit logMessage(QStringLiteral("Preview rejected by user, returning to center"));
        (void)waitForMotorMove(center);
        return false;
    }

    emit logMessage(QStringLiteral("Preview accepted"));
    return true;
}

bool Worker::runMeasurementPhase()
{
    std::unique_ptr<IDataAcquisition> device = createDevice();
    if (!device) {
        emit logMessage(QStringLiteral("Cannot create acquisition backend"));
        return false;
    }

    if (!m_options.calibrationName.isEmpty()) {
        device->setCalibration(m_options.calibrationName);
    }

    emit logMessage(QStringLiteral("Measurement backend: %1").arg(device->name()));
    setState(State::Running);

    // Get exposure sequence from options
    std::vector<double> exposureSequence;
    {
        QMutexLocker locker(&m_mutex);
        exposureSequence = m_options.exposureSequence;
    }

    if (exposureSequence.empty()) {
        emit logMessage(QStringLiteral("Error: No exposure times in sequence"));
        return false;
    }

    emit logMessage(QStringLiteral("Exposure sequence: %1 time(s)").arg(static_cast<int>(exposureSequence.size())));

    // Snake traversal minimizes long return moves between adjacent rows.
    const std::vector<QPoint> points = buildSnakeGrid();
    for (const QPoint& point : points) {
        if (!waitIfPaused() || isStopping()) {
            return false;
        }

        if (!ensureHardwareReady(true, State::Running)) {
            emit logMessage(QStringLiteral("Hardware lost during measurement, stopping and waiting for reconnect"));
            return false;
        }

        if (!waitForMotorMove(point)) {
            return false;
        }

        // For each point, collect measurements at all exposure times in the sequence
        for (size_t expIdx = 0; expIdx < exposureSequence.size(); ++expIdx) {
            if (!waitIfPaused() || isStopping()) {
                return false;
            }

            const double exposure = exposureSequence[expIdx];

            if (!acquisitionConnected() || !motorConnected()) {
                emit logMessage(QStringLiteral("Device disconnected during measurement, retrying"));
                if (!ensureHardwareReady(true, State::Running)) {
                    return false;
                }
            }

            // Set exposure time for this measurement
            emit logMessage(QStringLiteral("Setting exposure to %1 ms for point (%2, %3) [%4/%5]")
                .arg(exposure)
                .arg(point.x())
                .arg(point.y())
                .arg(static_cast<int>(expIdx) + 1)
                .arg(static_cast<int>(exposureSequence.size())));

            device->setExposure(exposure);

            // Wait for exposure to stabilize and collect new data
            if (!waitForAcquisitionDelay(90)) {
                return false;
            }

            if (!acquisitionConnected() || !motorConnected()) {
                emit logMessage(QStringLiteral("Device disconnected while collecting measurement, retrying"));
                if (!ensureHardwareReady(true, State::Running)) {
                    return false;
                }
            }

            // Collect spectrum and image at this exposure time
            std::vector<double> spectrum = device->getSpectrum();
            cv::Mat image = device->getImage();

            // Emit result with exposure information encoded in point or as separate signal
            // For now, emit same signal - could be enhanced to include exposure metadata
            emit resultReady(point, spectrum, image);

            // Wait before moving to next exposure (to ensure new data is acquired)
            if (expIdx < exposureSequence.size() - 1) {
                emit logMessage(QStringLiteral("Measurement at exposure %1 ms complete, waiting for next exposure...").arg(exposure));
                if (!waitForAcquisitionDelay(50)) {
                    return false;
                }
            }
        }

        emit logMessage(QStringLiteral("All exposures collected for point (%1, %2), moving to next point...")
            .arg(point.x())
            .arg(point.y()));
    }

    return true;
}

bool Worker::waitForMotorMove(const QPoint& target)
{
    {
        QMutexLocker locker(&m_mutex);
        m_motorReady = false;
    }

    emit requestMove(target);

    QEventLoop loop;
    connect(this, &Worker::motorReadyInternal, &loop, &QEventLoop::quit, Qt::UniqueConnection);
    connect(this, &Worker::stopSignal, &loop, &QEventLoop::quit, Qt::UniqueConnection);
    loop.exec();

    QMutexLocker locker(&m_mutex);
    return m_motorReady && !m_stopping;
}

bool Worker::waitForPreviewDecision()
{
    {
        QMutexLocker locker(&m_mutex);
        m_previewDecisionPending = true;
    }

    QEventLoop loop;
    connect(this, &Worker::previewDecisionInternal, &loop, &QEventLoop::quit, Qt::UniqueConnection);
    connect(this, &Worker::stopSignal, &loop, &QEventLoop::quit, Qt::UniqueConnection);
    loop.exec();

    QMutexLocker locker(&m_mutex);
    return !m_stopping && !m_previewDecisionPending;
}

bool Worker::waitForAcquisitionDelay(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    connect(this, &Worker::stopSignal, &loop, &QEventLoop::quit, Qt::UniqueConnection);
    loop.exec();
    return !isStopping();
}

bool Worker::waitIfPaused()
{
    // Pause uses condition variable so we do not spin or sleep while paused.
    bool wasPaused = false;
    QMutexLocker locker(&m_mutex);
    while (m_paused && !m_stopping) {
        wasPaused = true;
        m_pauseCondition.wait(&m_mutex);
    }
    const bool stopping = m_stopping;
    locker.unlock();

    if (wasPaused && !stopping) {
        setState(State::Running);
    }
    return !stopping;
}

bool Worker::isStopping() const
{
    QMutexLocker locker(&m_mutex);
    return m_stopping;
}

std::vector<QPoint> Worker::buildSnakeGrid() const
{
    Options options;
    {
        QMutexLocker locker(&m_mutex);
        options = m_options;
    }

    const int step = std::max(1, options.stepSize);
    const int left = options.scanArea.left();
    const int right = options.scanArea.right();
    const int top = options.scanArea.top();
    const int bottom = options.scanArea.bottom();

    std::vector<QPoint> points;
    bool leftToRight = true;

    for (int y = top; y <= bottom; y += step) {
        std::vector<int> xs;
        for (int x = left; x <= right; x += step) {
            xs.push_back(x);
        }
        if (!leftToRight) {
            std::reverse(xs.begin(), xs.end());
        }
        for (int x : xs) {
            points.emplace_back(x, y);
        }
        leftToRight = !leftToRight;
    }

    return points;
}

std::vector<QPoint> Worker::buildPreviewPoints() const
{
    Options options;
    {
        QMutexLocker locker(&m_mutex);
        options = m_options;
    }

    const QRect area = options.scanArea;
    return {
        QPoint(area.left(), area.top()),
        QPoint(area.right(), area.top()),
        QPoint(area.right(), area.bottom()),
        QPoint(area.left(), area.bottom())
    };
}

QPoint Worker::centerPoint() const
{
    QMutexLocker locker(&m_mutex);
    return m_options.scanArea.center();
}

std::unique_ptr<IDataAcquisition> Worker::createDevice() const
{
    QMutexLocker locker(&m_mutex);
    if (m_deviceName.compare(QStringLiteral("ThorlabsConnect"), Qt::CaseInsensitive) == 0) {
        return std::make_unique<ThorlabsConnect>();
    }
    return std::make_unique<PixelinkConnect>();
}

void Worker::setState(State state)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_state == state) {
            return;
        }
        m_state = state;
    }
    emit stateChanged(state);
}

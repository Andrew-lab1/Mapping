#include "MotorController.h"

MotorController::MotorController(QObject* parent)
    : QObject(parent)
{
    m_moveTimer.setSingleShot(true);
    connect(&m_moveTimer, &QTimer::timeout, this, [this]() {
        emit logMessage(QStringLiteral("Motor reached (%1, %2)").arg(m_target.x()).arg(m_target.y()));
        emit motorReady();
    });
}

void MotorController::moveTo(const QPoint& target)
{
    m_target = target;
    emit logMessage(QStringLiteral("Motor moving to (%1, %2)").arg(target.x()).arg(target.y()));
    m_moveTimer.start(120);
}

void MotorController::stopMotor()
{
    if (m_moveTimer.isActive()) {
        m_moveTimer.stop();
    }
    emit logMessage(QStringLiteral("Motor stop requested"));
}

#pragma once

#include <QObject>
#include <QPoint>
#include <QTimer>

class MotorController : public QObject {
    Q_OBJECT

public:
    explicit MotorController(QObject* parent = nullptr);

public slots:
    void moveTo(const QPoint& target);
    void stopMotor();

signals:
    void motorReady();
    void logMessage(const QString& message);

private:
    QPoint m_target;
    QTimer m_moveTimer;
};

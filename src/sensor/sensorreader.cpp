/*
  harbour-ibt — orientation / heading sensor reader for Sailfish OS
  Copyright (C) JimKnopfIoT — GPLv3 or later.
*/
#include "sensorreader.h"

#include <QtMath>
#include <QDebug>

SensorReader::SensorReader(QObject *parent)
    : QObject(parent)
    , m_acc(new QAccelerometer(this))
    , m_gyro(new QGyroscope(this))
    , m_compass(new QCompass(this))
    , m_timer(new QTimer(this))
    , m_heading(0.0)
    , m_rate(0.0)
    , m_gain(1.0)       // deg/s on this device
    , m_bias(0.0)
    , m_calibrating(false)
    , m_calibSum(0.0)
    , m_calibN(0)
    , m_active(false)
    , m_available(false)
    , m_source(QStringLiteral("idle"))
    , m_intervalMs(25)
    , m_lastMs(0)
{
    m_available = m_gyro->connectToBackend();
    m_acc->connectToBackend();
    const bool comp = m_compass->connectToBackend();
    qWarning() << "compass connectToBackend=" << comp;
    m_timer->setInterval(m_intervalMs);
    connect(m_timer, &QTimer::timeout, this, &SensorReader::onTick);
}

void SensorReader::setGain(double g)
{
    if (qFuzzyCompare(m_gain, g))
        return;
    m_gain = g;
    emit gainChanged();
}

void SensorReader::start()
{
    if (m_active)
        return;
    m_acc->start();
    const bool ok = m_gyro->start();
    m_compass->setDataRate(25);     // ask for ~25 Hz so the heading isn't laggy
    m_compass->start();
    m_clock.start();
    m_lastMs = 0;
    // (re)measure the gyro bias over the first ~1 s (phone assumed still)
    m_calibrating = true;
    m_calibSum = 0.0;
    m_calibN = 0;
    m_timer->start();
    m_active = true;
    m_source = ok ? QStringLiteral("gyro (relative)") : QStringLiteral("unavailable");
    qWarning() << "sensor start: gyro.connected=" << m_available
               << "gyro.start=" << ok
               << "acc.connected=" << m_acc->isConnectedToBackend()
               << "acc.active=" << m_acc->isActive()
               << "gyro.active=" << m_gyro->isActive();
    emit activeChanged();
    emit sourceChanged();
}

void SensorReader::stop()
{
    if (!m_active)
        return;
    m_timer->stop();
    m_gyro->stop();
    m_acc->stop();
    m_active = false;
    emit activeChanged();
}

void SensorReader::resetHeading()
{
    // heading is absolute (compass) now; snap straight to the current reading.
    QCompassReading *c = m_compass->reading();
    if (c)
        m_heading = c->azimuth();
    emit headingChanged();
}

void SensorReader::onTick()
{
    // Complementary filter: the GYRO gives instant, smooth short-term rotation
    // (responsive), the COMPASS provides the absolute, drift-free reference so
    // it stays locked to North. Best of both: snappy AND absolute.

    // --- fast: integrate the gyro yaw (rate around gravity) ---
    QGyroscopeReading *g = m_gyro->reading();
    QAccelerometerReading *a = m_acc->reading();
    if (g) {
        const double gx = g->x(), gy = g->y(), gz = g->z(); // deg/s
        double ax = 0, ay = 0, az = 0;
        if (a) { ax = a->x(); ay = a->y(); az = a->z(); }
        const double gn = std::sqrt(ax * ax + ay * ay + az * az);
        if (gn >= 0.1) {
            const double yaw = -(gx * ax + gy * ay + gz * az) / gn; // deg/s, CW+
            m_rate = yaw;
            m_heading += yaw * (m_intervalMs / 1000.0);
        }
    }

    // --- slow: pull toward the absolute compass heading (no drift) ---
    QCompassReading *c = m_compass->reading();
    if (c && c->calibrationLevel() > 0.0) {
        const double az = c->azimuth();
        double err = az - m_heading;
        while (err > 180.0) err -= 360.0;
        while (err < -180.0) err += 360.0;
        m_heading += 0.08 * err;
        if (m_source != QStringLiteral("compass+gyro")) {
            m_source = QStringLiteral("compass+gyro");
            emit sourceChanged();
        }
    }

    m_heading = std::fmod(m_heading, 360.0);
    if (m_heading < 0.0)
        m_heading += 360.0;
    emit headingChanged();
}

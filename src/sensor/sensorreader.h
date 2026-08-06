/*
  harbour-ibt — orientation / heading sensor reader for Sailfish OS
  Copyright (C) JimKnopfIoT — GPLv3 or later.

  Relative heading from gyroscope + accelerometer (no magnetometer needed).
  The yaw rate is the angular velocity projected onto the gravity vector, so it
  works regardless of how the phone is held/tilted. Integrated on a FIXED timer
  interval (not the sensor timestamp, which is unreliable on hybris). Heading is
  RELATIVE (degrees since the last reset).
*/
#ifndef SENSORREADER_H
#define SENSORREADER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <QtSensors/QAccelerometer>
#include <QtSensors/QGyroscope>
#include <QtSensors/QCompass>

class SensorReader : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double heading READ heading NOTIFY headingChanged)
    Q_PROPERTY(double rate READ rate NOTIFY headingChanged)
    Q_PROPERTY(double gain READ gain WRITE setGain NOTIFY gainChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString source READ source NOTIFY sourceChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
public:
    explicit SensorReader(QObject *parent = nullptr);

    double heading() const { return m_heading; }
    double rate() const { return m_rate; }
    double gain() const { return m_gain; }
    void setGain(double g);
    bool active() const { return m_active; }
    QString source() const { return m_source; }
    bool available() const { return m_available; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void resetHeading();

signals:
    void headingChanged();
    void activeChanged();
    void sourceChanged();
    void gainChanged();

private slots:
    void onTick();

private:
    QAccelerometer *m_acc;
    QGyroscope *m_gyro;
    QCompass *m_compass;
    QTimer *m_timer;
    double m_heading;     // relative degrees, 0..360
    double m_rate;        // last yaw rate (deg/s)
    double m_gain;        // calibration multiplier
    double m_bias;        // estimated gyro zero-rate bias (deg/s)
    bool m_calibrating;   // measuring the bias at start (phone held still)
    double m_calibSum;
    int m_calibN;
    bool m_active;
    bool m_available;
    QString m_source;
    int m_intervalMs;
    QElapsedTimer m_clock;
    qint64 m_lastMs;
};

#endif // SENSORREADER_H

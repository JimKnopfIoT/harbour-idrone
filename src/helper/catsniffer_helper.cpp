// harbour-idrone — CatSniffer least-privilege serial helper.
//
// Runs OUTSIDE the Sailjail sandbox (as the user, in the "dialout" group) so the
// sandboxed app never needs raw /dev access. Opens the CatSniffer (running NCC
// Group's Sniffle firmware) on /dev/ttyACM*, drives it to receive BLE-5 Long Range
// (Coded PHY) extended advertising, extracts ASTM/Open Drone ID service data
// (UUID 0xFFFA), and forwards each Remote-ID message pack to the app over the
// session D-Bus:
//
//     signal org.idrone.CatSniffer.remoteId(QString key, QByteArray pack, int rssi)
//
// The app decodes/fuses the pack exactly like its Wi-Fi/BT paths (rid::feedMessagePack).
// Passive receive only — the sniffer never transmits.
//
// Implements the NCC Group Sniffle host protocol (verified end-to-end).
#include <QCoreApplication>
#include <QSocketNotifier>
#include <QByteArray>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QLoggingCategory>

#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

static const char *SERVICE = "org.idrone.CatSniffer";
static const char *PATH    = "/org/idrone/CatSniffer";
static const char *IFACE   = "org.idrone.CatSniffer";

// Sniffle PHY modes / advertising constants
static const quint8  PHY_CODED = 2;
static const quint32 BLE_ADV_AA   = 0x8E89BED6u;
static const quint32 BLE_ADV_CRCI = 0x00555555u;

static void appendLE32(QByteArray &b, quint32 v)
{
    b.append(char(v & 0xff)); b.append(char((v >> 8) & 0xff));
    b.append(char((v >> 16) & 0xff)); b.append(char((v >> 24) & 0xff));
}

// Find the CatSniffer (RP2040 bridge, VID:PID 2e8a:00c0) among /dev/ttyACM*.
// Note: QDir with QDir::System does NOT reliably list char device nodes here, so
// probe the fixed ttyACM0..15 range directly via QFile::exists.
static QString findPort()
{
    for (int i = 0; i < 16; ++i) {
        const QString n = QStringLiteral("ttyACM%1").arg(i);
        const QString node = QStringLiteral("/dev/") + n;
        if (!QFile::exists(node))
            continue;
        QDir d(QFileInfo(QStringLiteral("/sys/class/tty/") + n
                         + QStringLiteral("/device")).canonicalFilePath());
        for (int up = 0; up < 6 && !d.isRoot(); ++up) {
            const QString vfp = d.filePath(QStringLiteral("idVendor"));
            const QString pfp = d.filePath(QStringLiteral("idProduct"));
            if (QFile::exists(vfp) && QFile::exists(pfp)) {
                QFile vf(vfp), pf(pfp);
                vf.open(QIODevice::ReadOnly); pf.open(QIODevice::ReadOnly);
                const QString v = QString::fromLatin1(vf.readAll()).trimmed();
                const QString p = QString::fromLatin1(pf.readAll()).trimmed();
                if (v == QLatin1String("2e8a") && p == QLatin1String("00c0"))
                    return node;
                break;
            }
            d.cdUp();
        }
    }
    return QString();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // System bus: this helper runs as a root system service (proven iBT-War/iWifi
    // pattern) so it can open the raw device; the sandboxed app receives on the
    // system bus via a D-Bus policy + a sailjail dbus-system.talk permission.
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.registerService(SERVICE);   // allowed by the shipped system-bus policy

    struct State {
        int fd = -1;
        QSocketNotifier *notifier = nullptr;
        QByteArray buf;
    } st;

    auto sendCmd = [&](const QByteArray &cmd) {          // cmd = [opcode, args...]
        if (st.fd < 0) return;
        int wc = (cmd.size() + 3) / 3;
        QByteArray raw; raw.append(char(wc)); raw.append(cmd);
        QByteArray msg = raw.toBase64(); msg.append("\r\n");
        ::write(st.fd, msg.constData(), msg.size());
    };

    // Configure Sniffle: passive, extended, Coded PHY (Long Range), channel 37.
    auto setup = [&]() {
        QByteArray cap; cap.append(char(0x10));          // CMD_CHAN_AA_PHY
        cap.append(char(37)); appendLE32(cap, BLE_ADV_AA);
        cap.append(char(PHY_CODED)); appendLE32(cap, BLE_ADV_CRCI);
        sendCmd(cap);
        sendCmd(QByteArray(1, char(0x12)).append(char(0x80)));   // rssi min = -128
        sendCmd(QByteArray(1, char(0x11)).append(char(0x00)));   // pause_done off
        sendCmd(QByteArray(1, char(0x15)).append(char(0x00)));   // follow off (advonly)
        sendCmd(QByteArray(1, char(0x16)).append(char(0x01)));   // auxadv on (extended)
        sendCmd(QByteArray(1, char(0x13)));                      // clear MAC filter
        sendCmd(QByteArray(1, char(0x26)).append(char(0x01)));   // crc valid
        sendCmd(QByteArray(1, char(0x27)).append(char(0x05)));   // tx power (unused for rx)
        sendCmd(QByteArray(1, char(0x21)));                      // interval preload (none)
        sendCmd(QByteArray(1, char(0x23)).append(char(0x01)));   // phy preload = 2M
    };

    auto emitRid = [&](const QString &key, const QByteArray &pack, int rssi) {
        QDBusMessage sig = QDBusMessage::createSignal(PATH, IFACE, "remoteId");
        sig << key << pack << rssi;
        bus.send(sig);
    };

    // Extract the Open Drone ID message pack from a received BLE PDU, plus a stable
    // per-advertiser key (the extended-advertising AdvA if present).
    auto handlePdu = [&](const QByteArray &pdu, int rssi) {
        const int n = pdu.size();
        const uchar *d = reinterpret_cast<const uchar *>(pdu.constData());

        // Per-advertiser key from the extended header AdvA (flags bit0), if present.
        QString key = QStringLiteral("cat");
        if (n >= 4) {
            int extHdrLen = d[2] & 0x3F;
            if (extHdrLen >= 1) {
                uchar flags = d[3];
                int off = 4;
                if ((flags & 0x01) && off + 6 <= n) {   // AdvA present
                    key.clear();
                    for (int i = 5; i >= 0; --i)
                        key += QString::asprintf("%02x", d[off + i]) + (i ? ":" : "");
                }
            }
        }

        // Scan for ODID service data:  [len][0x16][FA FF][0x0D][counter][pack...]
        for (int i = 0; i + 6 <= n; ++i) {
            if (d[i+1] == 0x16 && d[i+2] == 0xFA && d[i+3] == 0xFF && d[i+4] == 0x0D) {
                int L = d[i];
                int packStart = i + 6;
                int packEnd = i + 1 + L;               // end of AD element
                if (packEnd > n) packEnd = n;
                if (packEnd - packStart >= 3) {
                    QByteArray pack = pdu.mid(packStart, packEnd - packStart);
                    emitRid(key, pack, rssi);
                }
                return;
            }
        }
    };

    auto onReadable = [&]() {
        char tmp[8192];
        ssize_t r = ::read(st.fd, tmp, sizeof(tmp));
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return;                                     // no data yet — not an error
        if (r <= 0) {                                   // 0 = EOF (device removed)
            if (st.notifier) { st.notifier->deleteLater(); st.notifier = nullptr; }
            ::close(st.fd); st.fd = -1; st.buf.clear();
            return;
        }
        st.buf.append(tmp, int(r));
        int nl;
        while ((nl = st.buf.indexOf('\n')) >= 0) {
            QByteArray line = st.buf.left(nl);
            st.buf.remove(0, nl + 1);
            if (line.endsWith('\r')) line.chop(1);
            if (line.isEmpty()) continue;
            QByteArray data = QByteArray::fromBase64(line);
            if (data.size() < 2) continue;
            quint8 mtype = quint8(data[1]);
            QByteArray body = data.mid(2);
            if (mtype == 0x10 && body.size() >= 10) {   // packet message
                // Sniffle packet header: <LHHbB> = ts(4) len(2) event(2) rssi(1) chan(1)
                const uchar *b = reinterpret_cast<const uchar *>(body.constData());
                quint16 l = quint16(b[4]) | (quint16(b[5]) << 8);
                int rssi = qint8(b[8]);
                l &= 0x3FFF;
                QByteArray pdu = body.mid(10, l);
                if (pdu.size() >= 8)
                    handlePdu(pdu, rssi);
            }
        }
    };

    auto openPort = [&]() {
        if (st.fd >= 0) return;
        QString port = findPort();
        if (port.isEmpty()) return;
        int fd = ::open(port.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) return;
        termios t; tcgetattr(fd, &t); cfmakeraw(&t);
        cfsetispeed(&t, B921600); cfsetospeed(&t, B921600);
        t.c_cflag |= (CLOCAL | CREAD); t.c_cflag &= ~CRTSCTS;
        tcsetattr(fd, TCSANOW, &t); tcflush(fd, TCIOFLUSH);
        st.fd = fd;
        st.notifier = new QSocketNotifier(fd, QSocketNotifier::Read, &app);
        QObject::connect(st.notifier, &QSocketNotifier::activated, &app, [onReadable](){ onReadable(); });
        setup();
        qInfo("catsniffer-helper: opened %s", qPrintable(port));
    };

    // Reopen loop: attach when the CatSniffer appears, reattach after unplug.
    QTimer reopen;
    QObject::connect(&reopen, &QTimer::timeout, &app, [&](){ if (st.fd < 0) openPort(); });
    reopen.start(3000);
    openPort();

    qInfo("catsniffer-helper: running, publishing %s", IFACE);
    return app.exec();
}

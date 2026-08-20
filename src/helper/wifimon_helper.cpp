// harbour-idrone — Alfa Wi-Fi monitor-mode helper (Tier 2, least-privilege).
//
// Runs OUTSIDE the Sailjail sandbox as a root system service (like the CatSniffer
// helper) so it can put the USB Wi-Fi adapter into monitor mode and open a raw
// AF_PACKET socket — the sandboxed app never needs CAP_NET_ADMIN or /dev access.
//
// Captures 802.11 management frames from the monitor interface, channel-hops, and
// extracts ASTM/Open Drone ID broadcast Remote ID carried in a Wi-Fi vendor IE
// (ASD-STAN OUI FA:0B:BC, vendor-type 0x0D) in Beacons/Probe-Responses, plus the
// same signature inside NAN action frames. Each decoded message pack is forwarded
// to the app over the SYSTEM bus:
//
//     signal org.idrone.WifiMonitor.remoteId(QString key, QByteArray pack, int rssi)
//
// The app decodes/fuses the pack exactly like its Wi-Fi/BT paths (rid::feedMessagePack).
// Passive receive only — monitor mode never transmits; the adapter is wlan1/phyN,
// separate from the phone's own Wi-Fi (wlan0), which keeps its connection.
//
// Capture/parse logic ported from harbour-iwifi-war helper/iwsniff.c (GPLv3),
// stripped of the Wi-Fi-security detection it does not need.
#include <QCoreApplication>
#include <QByteArray>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSocketNotifier>
#include <QTimer>

#include <errno.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <linux/if_ether.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char *SERVICE = "org.idrone.WifiMonitor";
static const char *PATH    = "/org/idrone/WifiMonitor";
static const char *IFACE   = "org.idrone.WifiMonitor";

typedef unsigned char u8;

// Known monitor-capable USB Wi-Fi adapters (matches HwDetect's "wifi-monitor" set).
static bool isKnownAlfa(const QString &vid, const QString &pid)
{
    static const char *pairs[][2] = {
        {"0bda", "8812"},   // RTL8812AU  (Alfa AWUS036ACH / AC1200)
        {"0bda", "8813"},   // RTL8814AU  (Alfa AWUS036AC1900)
        {"0cf3", "9271"},   // AR9271     (Alfa AWUS036NHA, ath9k_htc)
        {"0e8d", "7612"},   // MT7612U    (Alfa AWUS036ACM)
        {"0e8d", "7610"},   // MT7610U    (Alfa)
    };
    for (const auto &p : pairs)
        if (vid == QLatin1String(p[0]) && pid == QLatin1String(p[1]))
            return true;
    return false;
}

// Find the network interface backed by a known Alfa USB id. Walks up from
// /sys/class/net/<if>/device to the USB device node holding idVendor/idProduct.
static QString findAlfaIface()
{
    const QStringList names =
        QDir(QStringLiteral("/sys/class/net")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : names) {
        if (!name.startsWith(QLatin1String("wlan")) && !name.startsWith(QLatin1String("wlx")))
            continue;   // only wireless net devices can be an Alfa
        const QString devLink = QStringLiteral("/sys/class/net/%1/device").arg(name);
        QDir d(QFileInfo(devLink).canonicalFilePath());
        for (int up = 0; up < 8 && !d.isRoot(); ++up) {
            const QString vfp = d.filePath(QStringLiteral("idVendor"));
            const QString pfp = d.filePath(QStringLiteral("idProduct"));
            if (QFile::exists(vfp) && QFile::exists(pfp)) {
                QFile vf(vfp), pf(pfp);
                vf.open(QIODevice::ReadOnly); pf.open(QIODevice::ReadOnly);
                const QString v = QString::fromLatin1(vf.readAll()).trimmed();
                const QString p = QString::fromLatin1(pf.readAll()).trimmed();
                if (isKnownAlfa(v, p))
                    return name;
                break;
            }
            d.cdUp();
        }
    }
    return QString();
}

static int runCmd(const QString &c) { return ::system(c.toLocal8Bit().constData()); }

// down -> set type monitor -> up. Separate phy from wlan0, so the phone's own
// Wi-Fi is untouched. Returns true if the monitor switch reported success.
static bool setMonitor(const QString &iface)
{
    runCmd(QStringLiteral("ip link set %1 down 2>/dev/null").arg(iface));
    int r = runCmd(QStringLiteral("iw dev %1 set type monitor 2>/dev/null").arg(iface));
    runCmd(QStringLiteral("ip link set %1 up 2>/dev/null").arg(iface));
    // The RTL8812AU re-creates its netdev on the type change and needs a moment to
    // settle before it will honour the channel set; without this the first channel
    // set is silently lost. Matches the working manual sequence (up -> settle -> chan).
    ::sleep(1);
    return r == 0;
}

static void setChannel(const QString &iface, int ch)
{
    // The RTL8812AU (rtl8812au/aircrack driver) only actually switches the monitor
    // channel when an explicit width is given — plain "set channel N" and "set freq"
    // are silently ignored, and `iw dev ... info` mis-reports the channel as 1 even
    // after a successful switch (verified on-device: ch6 HT20 receives, ch1 does not).
    // So always pass HT20. 5 GHz channels may still fail on some drivers; ignore that.
    runCmd(QStringLiteral("iw dev %1 set channel %2 HT20 2>/dev/null").arg(iface).arg(ch));
}

// Fixed monitor channel. The RTL8812AU (rtl8812au driver) here only reliably honours
// the channel set ONCE right after the interface comes up — live channel-hopping via
// `iw set channel` does not take effect (verified on-device: after a hop it never
// lands back on the target channel). Open Drone ID over Wi-Fi uses channel 6 almost
// universally — it is the Wi-Fi Aware / NAN social channel in 2.4 GHz and the usual
// Beacon-method channel — so we pin ch6. (Multi-channel would need a full interface
// down/up re-init per channel; deferred until the hardware supports live hopping.)
static const int kMonitorChannel = 6;

// ---- minimal radiotap parser: returns header length, fills *sig (dBm) --------
static int radiotapParse(const u8 *buf, int len, int *sig)
{
    if (len < 8 || buf[0] != 0) return -1;          // version 0
    int itLen = buf[2] | (buf[3] << 8);
    if (itLen < 8 || itLen > len) return -1;

    quint32 present = buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);
    int off = 8;
    while (present & (1u << 31)) {                    // skip extended present bitmaps
        if (off + 4 > itLen) break;
        present = buf[off] | (buf[off+1] << 8) | (buf[off+2] << 16) | (buf[off+3] << 24);
        off += 4;
    }
    // Walk standard early fields to DBM_ANTSIGNAL (bit 5). (size, align):
    // 0 TSFT(8,8) 1 FLAGS(1,1) 2 RATE(1,1) 3 CHANNEL(4,2) 4 FHSS(2,2) 5 DBM_ANTSIGNAL(1,1)
    static const int fsize[6]  = { 8, 1, 1, 4, 2, 1 };
    static const int falign[6] = { 8, 1, 1, 2, 2, 1 };
    quint32 first = buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);
    for (int b = 0; b <= 5; ++b) {
        if (!(first & (1u << b))) continue;
        int a = falign[b];
        off = (off + (a - 1)) & ~(a - 1);
        if (b == 5) {
            if (off < itLen) *sig = (signed char)buf[off];
            return itLen;
        }
        off += fsize[b];
        if (off > itLen) break;
    }
    return itLen;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.registerService(SERVICE);   // allowed by the shipped system-bus policy

    struct State {
        int fd = -1;
        QSocketNotifier *notifier = nullptr;
        QString iface;
        QHash<QString, qint64> lastEmit;   // per-drone throttle
    } st;

    auto macStr = [](const u8 *m) {
        return QString::asprintf("%02x:%02x:%02x:%02x:%02x:%02x",
                                 m[0], m[1], m[2], m[3], m[4], m[5]);
    };

    auto emitRid = [&](const QString &key, const QByteArray &pack, int rssi) {
        if (pack.size() < 3) return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 &last = st.lastEmit[key];
        if (last != 0 && now - last < 500) return;    // <=2 Hz per drone on the bus
        last = now;
        QDBusMessage sig = QDBusMessage::createSignal(PATH, IFACE, "remoteId");
        sig << key << pack << rssi;
        bus.send(sig);
    };

    // Extract Open Drone ID from an 802.11 management frame. Beacon/Probe-Resp carry
    // it in a vendor IE (FA:0B:BC / 0x0D); action frames (NAN) carry the same OUI+type
    // inside their payload, so we byte-scan those. Key = transmitter MAC (the drone).
    auto handleFrame = [&](const u8 *f, int len, int sig) {
        if (len < 24) return;
        const quint16 fc = f[0] | (f[1] << 8);
        if (((fc >> 2) & 3) != 0) return;             // management frames only
        const int subtype = (fc >> 4) & 0xf;
        const QString key = macStr(f + 10);           // addr2 = transmitter

        if (subtype == 8 || subtype == 5) {           // beacon / probe-response
            int p = 36;                               // 24 MAC hdr + 12 fixed params
            while (p + 2 <= len) {
                const int tag = f[p], tl = f[p + 1];
                if (p + 2 + tl > len) break;
                const u8 *v = f + p + 2;
                if (tag == 221 && tl >= 6 &&
                    v[0] == 0xFA && v[1] == 0x0B && v[2] == 0xBC && v[3] == 0x0D) {
                    // v[4] = message counter; the ODID message pack follows.
                    emitRid(key, QByteArray((const char *)(v + 5), tl - 5), sig);
                }
                p += 2 + tl;
            }
        } else if (subtype == 13) {                   // action (NAN) — scan for the OUI+type
            for (int i = 24; i + 5 <= len; ++i) {
                if (f[i] == 0xFA && f[i+1] == 0x0B && f[i+2] == 0xBC && f[i+3] == 0x0D) {
                    emitRid(key, QByteArray((const char *)(f + i + 5), len - (i + 5)), sig);
                    break;
                }
            }
        }
    };

    auto detach = [&]() {
        if (st.notifier) { st.notifier->deleteLater(); st.notifier = nullptr; }
        if (st.fd >= 0) { ::close(st.fd); st.fd = -1; }
        st.iface.clear();
    };

    auto onReadable = [&]() {
        u8 buf[4096];
        ssize_t n = ::recv(st.fd, buf, sizeof(buf), 0);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return;
        if (n <= 0) { detach(); return; }             // adapter removed / interface down
        int sig = 0;
        int rt = radiotapParse(buf, int(n), &sig);
        if (rt > 0 && rt < int(n))
            handleFrame(buf + rt, int(n) - rt, sig);
    };

    auto attach = [&]() {
        if (st.fd >= 0) return;
        const QString iface = findAlfaIface();
        if (iface.isEmpty()) return;
        if (!setMonitor(iface)) {
            qWarning("wifimon-helper: monitor mode failed on %s", qPrintable(iface));
            return;
        }
        int fd = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (fd < 0) return;
        unsigned idx = if_nametoindex(iface.toLocal8Bit().constData());
        if (!idx) { ::close(fd); return; }
        struct sockaddr_ll sll; std::memset(&sll, 0, sizeof(sll));
        sll.sll_family = AF_PACKET; sll.sll_protocol = htons(ETH_P_ALL);
        sll.sll_ifindex = int(idx);
        if (::bind(fd, (struct sockaddr *)&sll, sizeof(sll)) < 0) { ::close(fd); return; }

        st.fd = fd;
        st.iface = iface;
        st.notifier = new QSocketNotifier(fd, QSocketNotifier::Read, &app);
        QObject::connect(st.notifier, &QSocketNotifier::activated, &app,
                         [onReadable]() { onReadable(); });
        setChannel(iface, kMonitorChannel);
        qInfo("wifimon-helper: capturing on %s (monitor, channel %d)",
              qPrintable(iface), kMonitorChannel);
    };

    // Attach when the Alfa appears, reattach after unplug.
    QTimer reattach;
    QObject::connect(&reattach, &QTimer::timeout, &app, [&]() { if (st.fd < 0) attach(); });
    reattach.start(3000);
    attach();

    qInfo("wifimon-helper: running, publishing %s", IFACE);
    return app.exec();
}

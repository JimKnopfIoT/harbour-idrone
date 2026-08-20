# Neutral packaging metadata — no personal identifiers (see anonymity rules).
Name:       harbour-idrone
Summary:    Passive drone detector and proximity warning
Version:    0.2.0
Release:    1
# Neutral build host so built RPMs carry no real hostname.
%define _buildhost reproducible-builder
License:    GPL-3.0-or-later
URL:        https://github.com/JimKnopfIoT/harbour-idrone
Source0:    %{name}-%{version}.tar.bz2
Vendor:     harbour-idrone contributors
Packager:   harbour-idrone contributors

Requires:   sailfishsilica-qt5
BuildRequires: pkgconfig(sailfishapp)
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5Qml)
BuildRequires: pkgconfig(Qt5Quick)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: pkgconfig(Qt5Positioning)
BuildRequires: desktop-file-utils

%description
Passive, receive-only drone detector for SailfishOS. Decodes broadcast
Remote ID (Bluetooth and Wi-Fi) and warns on close approach. Optional
capability tiers add Bluetooth 5 Long Range, USB Wi-Fi monitor mode,
external BLE sniffers and SDR based detection. Never transmits, never
connects.

%prep
%setup -q

%build
%qmake5
%make_build

# Least-privilege helper binaries — separate programs that run OUTSIDE the Sailjail
# sandbox (as a root system service) and hand decoded Remote-ID packs to the app over
# D-Bus. Strictly additive to Tier 0.
#   catsniffer: reads the CatSniffer over /dev/ttyACM* (BT5 Long Range)
#   wifimon:    puts a USB Wi-Fi adapter (Alfa) into 802.11 monitor mode (Wi-Fi RID)
# Both live in src/helper and have distinct TARGET names, so building them one after
# the other in the same dir does not clobber (each qmake run rewrites the Makefile).
pushd src/helper
%qmake5 catsniffer_helper.pro
%make_build
%qmake5 wifimon_helper.pro
%make_build
popd

%install
%qmake5_install

# Install both helper binaries explicitly (the src/helper Makefile only reflects the
# last qmake run, so do not rely on `make install` there).
install -Dm755 src/helper/harbour-idrone-catsniffer \
    %{buildroot}%{_bindir}/harbour-idrone-catsniffer
install -Dm755 src/helper/harbour-idrone-wifimon \
    %{buildroot}%{_bindir}/harbour-idrone-wifimon

# Helper units, not enabled at boot: the app's switches start/stop them
# (polkit-scoped to these two units).
install -Dm644 systemd/harbour-idrone-catsniffer.service \
    %{buildroot}%{_unitdir}/harbour-idrone-catsniffer.service
install -Dm644 systemd/harbour-idrone-wifimon.service \
    %{buildroot}%{_unitdir}/harbour-idrone-wifimon.service

%post
systemctl daemon-reload || :
systemctl reload dbus || :
# Upgrades: disable the pre-0.2.0 boot services.
if [ "$1" -gt 1 ]; then
    systemctl disable --now harbour-idrone-catsniffer.service || :
    systemctl disable --now harbour-idrone-wifimon.service || :
fi

%preun
if [ "$1" = 0 ]; then
    systemctl disable --now harbour-idrone-catsniffer.service || :
    systemctl disable --now harbour-idrone-wifimon.service || :
fi

%postun
systemctl daemon-reload || :

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_bindir}/%{name}-catsniffer
%{_bindir}/%{name}-wifimon
%{_unitdir}/harbour-idrone-catsniffer.service
%{_unitdir}/harbour-idrone-wifimon.service
%{_datadir}/polkit-1/rules.d/50-harbour-idrone-catsniffer.rules
%{_datadir}/polkit-1/rules.d/50-harbour-idrone-wifimon.rules
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
%config %{_sysconfdir}/sailjail/permissions/WpaScan.permission
%config %{_sysconfdir}/sailjail/permissions/CatSniffer.permission
%config %{_sysconfdir}/sailjail/permissions/WifiMonitor.permission
%config %{_sysconfdir}/dbus-1/system.d/harbour-idrone-wpa.conf
%config %{_sysconfdir}/dbus-1/system.d/harbour-idrone-catsniffer.conf
%config %{_sysconfdir}/dbus-1/system.d/harbour-idrone-wifimon.conf

%changelog
* Thu Aug 20 2026 harbour-idrone contributors 0.2.0-1
- Add USB Wi-Fi monitor tier: 802.11 Remote ID (beacon + NAN, all channels) via
  a least-privilege root helper on the system D-Bus, for adapters like the Alfa
  (RTL8812AU).
- Neither helper is a root service enabled at boot. Switches in the app's
  settings (off by default) start and stop each one on demand, gated by polkit
  rules scoped to exactly those two units. Without a switch, and without the
  hardware, no root service runs. Upgrades disable the old boot services; the
  switches re-enable them when wanted.

* Tue Aug 18 2026 harbour-idrone contributors 0.1.1-1
- The external-CatSniffer helper is no longer a root service enabled at boot.
  A switch in the app's settings (off by default) starts and stops it on
  demand, gated by a polkit rule scoped to that one unit.
- The About page shows the app version.

* Thu Aug 06 2026 harbour-idrone contributors 0.1.0-1
- Add external CatSniffer sensor tier: Bluetooth 5 Long Range (Coded PHY)
  extended-advertising Remote ID via a least-privilege root serial helper on
  the system D-Bus. Auto-detected hardware indicator. Strictly additive to the
  phone-only Tier 0 (BLE + Wi-Fi) core.

* Thu Jul 09 2026 harbour-idrone contributors 0.0.1-1
- M0 scaffold: builds, app skeleton with cover, bundled opendroneid-core-c parser.

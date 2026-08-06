# Neutral packaging metadata — no personal identifiers (see anonymity rules).
Name:       harbour-idrone
Summary:    Passive drone detector and proximity warning
Version:    0.1.0
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

# Least-privilege serial helper — a separate binary that runs OUTSIDE the Sailjail
# sandbox (in the dialout group) to read the CatSniffer over /dev/ttyACM* and hand
# decoded Remote-ID packs to the app over D-Bus. Strictly additive to Tier 0.
pushd src/helper
%qmake5 catsniffer_helper.pro
%make_build
popd

%install
%qmake5_install

pushd src/helper
make install INSTALL_ROOT=%{buildroot}
popd

# Root system service that runs the serial helper (auto-enabled at boot).
install -Dm644 systemd/harbour-idrone-catsniffer.service \
    %{buildroot}%{_unitdir}/harbour-idrone-catsniffer.service
install -dm755 %{buildroot}%{_unitdir}/multi-user.target.wants
ln -sf ../harbour-idrone-catsniffer.service \
    %{buildroot}%{_unitdir}/multi-user.target.wants/harbour-idrone-catsniffer.service

%post
systemctl daemon-reload || :
systemctl reload dbus || :
systemctl enable --now harbour-idrone-catsniffer.service || :

%preun
if [ "$1" = 0 ]; then
    systemctl disable --now harbour-idrone-catsniffer.service || :
fi

%postun
systemctl daemon-reload || :

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_bindir}/%{name}-catsniffer
%{_unitdir}/harbour-idrone-catsniffer.service
%{_unitdir}/multi-user.target.wants/harbour-idrone-catsniffer.service
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
%config %{_sysconfdir}/sailjail/permissions/WpaScan.permission
%config %{_sysconfdir}/sailjail/permissions/CatSniffer.permission
%config %{_sysconfdir}/dbus-1/system.d/harbour-idrone-wpa.conf
%config %{_sysconfdir}/dbus-1/system.d/harbour-idrone-catsniffer.conf

%changelog
* Thu Aug 06 2026 harbour-idrone contributors 0.1.0-1
- Add external CatSniffer sensor tier: Bluetooth 5 Long Range (Coded PHY)
  extended-advertising Remote ID via a least-privilege root serial helper on
  the system D-Bus. Auto-detected hardware indicator. Strictly additive to the
  phone-only Tier 0 (BLE + Wi-Fi) core.
* Thu Jul 09 2026 harbour-idrone contributors 0.0.1-1
- M0 scaffold: builds, app skeleton with cover, bundled opendroneid-core-c parser.

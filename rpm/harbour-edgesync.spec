Name:       harbour-edgesync

Summary:    Edge Sync
Version:    0.1.0
Release:    1
License:    AGPLv3+
URL:        https://github.com/jarnose/harbour-edgesync
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Sql)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Network)
# No pkgconfig(Qt5Bluetooth): Sailfish OS's Qt5 has no QtConnectivity. The Edge
# is reached through BlueZ's D-Bus API with Qt5DBus instead.
# No pkgconfig(Qt5LinguistTools) virtual exists; CMake finds lrelease through
# the CMake package config that this plain package provides.
BuildRequires:  qt5-qttools-linguist
BuildRequires:  desktop-file-utils
BuildRequires:  cmake

%description
Downloads the activities recorded by a Garmin Edge bike computer over Bluetooth
LE, straight from the device and without a Garmin account or the cloud, and shows
them: distance, time, speed, climb, heart rate, power, cadence, the route and a
chart for each.

The original FIT files are kept on the phone, and any ride can be exported as GPX
or as the FIT file itself, and FIT files from other devices can be imported. The
rides never leave the phone. The one use of the network is the optional base map
under the route, which is off by default: when it is switched on, the map tiles for
the area of a ride are fetched from the tile server chosen in Settings
(OpenFreeMap by default).

The Edge's Bluetooth protocol is reverse engineered (by the Gadgetbridge
project) and differs between models and firmware; the app keeps a log of the
conversation for reporting problems.

%prep
%setup -q -n %{name}-%{version}

%build

%cmake
%make_build

%install
%make_install

desktop-file-install --delete-original         --dir %{buildroot}%{_datadir}/applications                %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png

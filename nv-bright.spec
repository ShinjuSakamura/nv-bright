Name:           nv-bright
Version:        0.1.0
Release:        1%{?dist}
Summary:        OLED brightness control via DPCD AUX on NVIDIA GPU

License:        MIT
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc

%description
Controls OLED panel brightness on NVIDIA GPUs by writing to the eDP
PANEL_TARGET_LUMINANCE DPCD register (0x734) via the NVIDIA RM AUX channel API.

Includes a watch daemon that monitors /sys/class/backlight/nvidia_0/brightness
and maps it to hardware luminance control, enabling the GNOME brightness slider
to control real hardware brightness on OLED laptops in dGPU mode.

Designed for the Lenovo Legion Pro 7 (83RU) with Samsung ATNA60HU02-0 OLED
panel, but may work on other NVIDIA dGPU laptops with eDP OLED panels that
support PANEL_LUMINANCE_CONTROL_CAP.

%prep
%setup -q

%build
%make_build

%install
%make_install

%post
%systemd_post nv-bright.service

%preun
%systemd_preun nv-bright.service

%postun
%systemd_postun_with_restart nv-bright.service

%files
%{_bindir}/nv-bright
%{_bindir}/nv-bright-wrapper
%{_unitdir}/nv-bright.service

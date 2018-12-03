Name:           rmlint
Version:        2.8.0
Release:        1%{?dist}
Summary:        Finds space waste and other broken things on your filesystem
License:        GPLv3
URL:            http://rmlint.rtfd.org
Source0:        https://github.com/sahib/rmlint/archive/v%{version}/%{name}-%{version}.tar.gz
BuildRequires:  scons
BuildRequires:  gcc
BuildRequires:  python3-sphinx
BuildRequires:  python3-devel
BuildRequires:  gettext
BuildRequires:  libblkid-devel
BuildRequires:  elfutils-libelf-devel
BuildRequires:  glib2-devel
BuildRequires:  sqlite-devel
BuildRequires:  json-glib-devel
BuildRequires:  desktop-file-utils
Requires:       hicolor-icon-theme

%description
rmlint finds space waste and other broken things and offers to remove it. It is
especially an extremely fast tool to remove duplicates from your filesystem.


%prep
%autosetup


%build
%set_build_flags
scons config 
scons %{?_smp_mflags} DEBUG=1 SYMBOLS=1 --prefix=%{buildroot}%{_prefix} --actual-prefix=%{_prefix} --libdir=%{_lib}


%install
scons install DEBUG=1 SYMBOLS=1 --prefix=%{buildroot}%{_prefix} --actual-prefix=%{_prefix} --libdir=%{_lib}.
desktop-file-validate %{buildroot}/%{_datadir}/applications/shredder.desktop
%find_lang %{name}


%files -f %{name}.lang
%doc README.rst
%license COPYING
%{_bindir}/rmlint
%{_datadir}/applications/shredder.desktop
%{_datadir}/glib-2.0/schemas/org.gnome.Shredder.gschema.xml
%exclude %{_datadir}/glib-2.0/schemas/gschemas.compiled
%{_datadir}/icons/hicolor/scalable/apps/shredder.svg
%{_mandir}/man1/rmlint.1*
%{python3_sitelib}/shredder/
%{python3_sitelib}/Shredder-%{version}.Maidenly.Moose-py?.?.egg-info


%changelog
* Sun Dec 02 2018 Robert-André Mauchin <zebob.m@gmail.com> - 2.8.0-1
- Update version to 2.8.0

* Sun May 10 2015 Christopher Pahl <sahib@online.de> - 2.2.0-1
- Update version to 2.2.0

* Sun Jan 12 2015 Christopher Pahl <sahib@online.de> - 2.0.0-4
- Fix rpm for lib separation.

* Sat Dec 20 2014 Christopher Pahl <sahib@online.de> - 2.0.0-3
- Use autosetup instead of setup -q

* Fri Dec 19 2014 Christopher Pahl <sahib@online.de> - 2.0.0-2
- Updated wrong dependency list

* Mon Dec 01 2014 Christopher Pahl <sahib@online.de> - 2.0.0-1
- Initial release of RPM package

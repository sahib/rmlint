Name:           rmlint
Version:        2.6.1
Release:        0%{?dist}
Summary:        rmlint finds space waste and other broken things on your filesystem and offers to remove it.
Group:          Applications/System
License:        GPLv3
URL:            http://rmlint.rtfd.org
Source0:        https://github.com/sahib/rmlint/archive/v%{version}/rmlint-%{version}.tar.gz
Requires:       glib2 libblkid elfutils-libelf json-glib
BuildRequires:  scons gettext libblkid-devel elfutils-libelf-devel glib2-devel json-glib-devel

%description
rmlint finds space waste and other broken things and offers to remove it. It is
especially an extremely fast tool to remove duplicates from your filesystem.

%package shredder
Summary:  GUI for rmlint
Group:    Applications/System
Requires: rmlint

%description shredder
shredder is a GUI frontend to the rmlint utility.

%prep
%setup -q

%build scons config; scons -j4 --prefix=%{buildroot}/usr --actual-prefix=/usr --libdir=lib64

%install

# Build rmlint, install it into BUILDROOT/<name>-<version>/,
# but take care rmlint thinks it's installed to /usr (--actual_prefix)
scons install -j4 --prefix=%{buildroot}/usr --actual-prefix=/usr --libdir=lib64

# Find all rmlint.mo files and put them in rmlint.lang
%find_lang %{name}
%clean
rm -rf %{buildroot}

# List all files that will be in the packaget
%files -f %{name}.lang
%doc README.rst COPYING
%{_bindir}/*
%{_mandir}/man1/*

# Not used yet:
# %{_libdir}/*
# %{_includedir}/*

%files shredder
%{python3_sitelib}/*
%{_datadir}/applications/shredder.desktop
%{_datadir}/glib-2.0/schemas/*
%{_datadir}/icons/hicolor/scalable/apps/shredder.svg

%changelog
* Sat Dec 16 2017 Patrick Hemmer <patrick.hemmer@gmail.com> - 2.6.1
- Fix source URL.
- Split shredder into subpackage.
* Fri Oct 27 2017 Vince Mele <vincentmele@gmail.com> - 2.6.1
- Update to version 2.6.1. Remove python-sphinx3 dependency.
- Use setup -q.
* Sun May 10 2015 Christopher Pahl <sahib@online.de> - 2.2.0
- Update version to 2.2.0
* Sun Jan 12 2015 Christopher Pahl <sahib@online.de> - 2.0.0
- Fix rpm for lib separation.
* Sat Dec 20 2014 Christopher Pahl <sahib@online.de> - 2.0.0
- Use autosetup instead of setup -q
* Fri Dec 19 2014 Christopher Pahl <sahib@online.de> - 2.0.0
- Updated wrong dependency list
* Mon Dec 01 2014 Christopher Pahl <sahib@online.de> - 2.0.0
- Initial release of RPM package

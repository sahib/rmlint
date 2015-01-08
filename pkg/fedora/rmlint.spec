Name:           rmlint
Version:        2.0.0
Release:        0%{?dist}
Summary:        rmlint finds space waste and other broken things on your filesystem and offers to remove it.
Group:          Applications/System
License:        GPLv3
URL:            http://rmlint.rtfd.org
Source0:        https://github.com/sahib/rmlint/archive/rmlint-%{version}.tar.gz
Requires:       glib2 libblkid elfutils-libelf json-glib
BuildRequires:  scons python3-sphinx gettext libblkid-devel elfutils-libelf-devel glib2-devel json-glib-devel

%description
rmlint finds space waste and other broken things and offers to remove it. It is
especially an extremely fast tool to remove duplicates from your filesystem.

%prep 
%autosetup -c rmlint-%{version}

%build scons config; scons -j4 --prefix=%{buildroot}/usr --actual-prefix=/usr

%install

# Build rmlint, install it into BUILDROOT/<name>-<version>/,
# but take care rmlint thinks it's installed to /usr (--actual_prefix)
scons install -j4 --prefix=%{buildroot}/usr --actual-prefix=/usr

# Find all rmlint.mo files and put them in rmlint.lang
%find_lang %{name}

%clean
rm -rf %{buildroot}

# List all files that will be in the packaget
%files -f %{name}.lang
%doc README.rst COPYING
%{_bindir}/*
%{_mandir}/man1/*

%changelog
* Sat Dec 20 2014 Christopher Pahl <sahib@online.de> - 2.0.0
- Use autosetup instead of setup -q
* Fri Dec 19 2014 Christopher Pahl <sahib@online.de> - 2.0.0
- Updated wrong dependency list
* Mon Dec 01 2014 Christopher Pahl <sahib@online.de> - 2.0.0
- Initial release of RPM package 

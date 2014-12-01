Name:           rmlint
Version:        2.0.0
Release:        0%{?dist}
Summary:        rmlint finds space waste and other broken things on your filesystem and offers to remove it.
Group:          Applications/System
License:        GPLv3
URL:            http://rmlint.rtfd.org
Source0:        https://github.com/sahib/rmlint/archive/2.0.0.tar.gz
Requires:       glib2-devel libblkid-devel elfutils-libelf-devel
BuildRequires:  scons python3-sphinx gettext

%description
rmlint finds space waste and other broken things and offers to remove it. It is
especially an extremely fast tool to remove duplicates from your filesystem.

%prep
%setup -q
%build
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
* Mon Dec 01 2014 Christopher Pahl <sahib@online.de> - 2.0.0
- Initial release of RPM package 

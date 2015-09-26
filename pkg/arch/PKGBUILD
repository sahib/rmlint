# Maintainer: SahibBommelig <sahib@online.de>
# rmlint PKBUILD for ArchLinux
 
_pkgname=rmlint
pkgname=${_pkgname}-git
pkgver=2.2.0.r33
pkgrel=1
pkgdesc="Tool to remove duplicates and other lint, being much faster than fdupes"
arch=('i686' 'x86_64')
url="https://github.com/sahib/rmlint"
license=('GPL3')
depends=('glibc' 'glib2>=2.31' 'libutil-linux' 'elfutils' 'gettext' 'json-glib')
makedepends=('git' 'scons' 'python-sphinx')
conflicts=("${_pkgname}")
provides=("$_pkgname")
source=("$pkgname"::"git+https://github.com/sahib/${_pkgname}.git")
md5sums=('SKIP')
 
pkgver() {
    cd "${srcdir}/${pkgname}"
    git describe master --long --abbrev=6 | sed 's/^v//;s/-g[0-9a-z]\+$//g;s/-/.r/g'
}

build() {
    cd "${srcdir}/${pkgname}"
    scons -j4 DEBUG=1 --prefix=${pkgdir}/usr --actual-prefix=/usr
}
 
package() {
    cd "${srcdir}/${pkgname}"
    scons DEBUG=1 --prefix=${pkgdir}/usr install --actual-prefix=/usr
}

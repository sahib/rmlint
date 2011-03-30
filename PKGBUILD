# Maintainer: SahibBommelig <sahib@online.de>
# rmlint PKBUILD for ArchLinux 
pkgname=rmlint
pkgver=20110330
pkgrel=1
pkgdesc="Tool to remove duplicates and other lint, being much faster than fdupes"
arch=('i686' 'x86_64')

license=('GPL3')
url=("https://github.com/sahib/rmlint")
depends=('glibc')

source=('https://github.com/downloads/sahib/rmlint/rmlint_1.0.0.tar.gz')
md5sums=('17cf1deff9aa5f7d89c8fa0ee8e2fc30')

build() 
{
    cd ${srcdir}/${pkgname}
    ./configure --prefix=/usr
    make || return 1
}

package() {
  cd ${srcdir}/${pkgname}
  make DESTDIR=$pkgdir install || return 1
}

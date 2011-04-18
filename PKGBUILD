# Maintainer: SahibBommelig <sahib@online.de>
# rmlint PKBUILD for ArchLinux 
pkgname=rmlint-git
pkgver=1.0.5
pkgrel=1
pkgdesc="Tool to remove duplicates and other lint, being much faster than fdupes"
arch=('i686' 'x86_64')
depends=(glibc)
provides=('rmlint')
conflicts=('rmlint')
makedepends=('git')
license=('GPL3')
url=("https://github.com/sahib/rmlint")

_gitroot="git://github.com/sahib/rmlint.git"
_gitname="rmlint"

build() 
{
    cd ${srcdir}/

    msg "Connecting to the GIT server...."
    if [[ -d ${srcdir}/${_gitname} ]] ; then
        cd ${_gitname}
        git pull origin
	msg "Updating existing repo..."
    else
        git clone ${_gitroot}
    fi
    
    msg "GIT checkout done."
    cd ${srcdir}/${_gitname}
    
    ./configure --prefix=/usr
    make || return 1
}

package() {
  cd "$srcdir/${_gitname}"
  make DESTDIR=$pkgdir install || return 1
}

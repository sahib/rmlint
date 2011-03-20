# Maintainer: SahibBommelig <sahib@online.de>
# rmlint PKBUILD for ArchLinux 
pkgname=rmlint
pkgver=20110321
pkgrel=1
pkgdesc="Tool to remove duplicates and other lint, being much faster than fdupes"
arch=(any)

# Only because namcap said it:
depends=(glibc)

makedepends=('git')
license=('GPL3')
url=("http://sahib.github.com/rmlint/")

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

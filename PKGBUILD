# Maintainer: Andy Stewart <lazycat.manatee@gmail.com>
pkgname=omarchy-screenshot
pkgver=0.1.6
pkgrel=1
pkgdesc="Qt 6 screenshot and annotation tool for Omarchy and Hyprland"
arch=('x86_64' 'aarch64')
url="https://github.com/manateelazycat/omarchy-screenshot"
license=('GPL-3.0-only' 'MIT')
depends=('qt6-base' 'qt6-declarative' 'qt6-wayland' 'layer-shell-qt' 'grim' 'wl-clipboard' 'hyprland' 'wayland')
makedepends=('cmake' 'pkgconf')
source=("$pkgname-$pkgver.tar.gz::$url/archive/v$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
    cmake -S "$pkgname-$pkgver" -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build build --parallel
}

package() {
    DESTDIR="$pkgdir" cmake --install build
    install -Dm644 "$pkgname-$pkgver/LICENSE" \
        "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}

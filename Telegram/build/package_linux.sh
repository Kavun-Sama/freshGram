#!/usr/bin/env bash
set -uo pipefail

usr="$(realpath "${1:?the install prefix with bin and share is required}")"
out="$(realpath -m "${2:?the output directory is required}")"
suffix="${3-}"
version="${4:?the package version is required}"
here="$(dirname "$(realpath "$0")")"

mkdir -p "$out"

optional() {
	local name="$1"
	shift
	if ! ( set -e; "$@" ); then
		echo "::warning::$name failed"
	fi
}

tarball="$out/freshGram-linux-x86_64$suffix.tar.gz"
tar -C "$(dirname "$usr")" -czf "$tarball" "$(basename "$usr")" || exit 1

pacman_version="$(awk '/^AppVersionStr /{print $2}' "$here/version")"
arch="$(mktemp -d)"
trap 'rm -rf "$arch"' EXIT
cp "$here/arch/PKGBUILD" "$arch/PKGBUILD"
tar -C "$(dirname "$usr")" -cf "$arch/freshgram-root.tar" "$(basename "$usr")" || exit 1
docker run --rm -e FRESHGRAM_VERSION="$pacman_version" -v "$arch:/pkg" archlinux:base-devel bash -c '
	set -e
	useradd -m builder
	chown -R builder /pkg
	su builder -c "cd /pkg && FRESHGRAM_VERSION=$FRESHGRAM_VERSION makepkg -f --nodeps"
' || exit 1
sudo chown -R "$(id -u):$(id -g)" "$arch" || exit 1
cp "$arch"/freshgram-*.pkg.tar.zst "$out/freshGram$suffix.pacman" || exit 1

optional "Flatpak bundle" "$here/flatpak/build.sh" "$usr" "$out/freshGram$suffix.flatpak"
optional "AppImage" "$here/appimage/build.sh" "$usr" "$out/freshGram$suffix.AppImage"
optional "Debian package" "$here/deb/build.sh" "$usr" "$out/freshGram$suffix.deb" "$version"
optional "RPM package" bash -c "
	set -e
	sudo apt-get update
	sudo apt-get install -y rpm
	'$here/rpm/build.sh' '$usr' '$out/freshGram$suffix.rpm' '$version'
"

exit 0

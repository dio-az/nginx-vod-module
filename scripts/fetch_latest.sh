#!/bin/sh

package="${2%%-*}"
version=$(wget -qO- "https://$1" | grep -oE "$2(\.[0-9]+)*\.tar\.gz" | sed -E "s/^$package-(.*)\.tar\.gz$/\1/" | sort -V | tail -n1)

mkdir -p "$package"
wget -qO- "https://$1/$package-$version.tar.gz" | tar -xz --strip-components 1 -C "$package"

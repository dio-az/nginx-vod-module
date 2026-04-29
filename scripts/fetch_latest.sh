#!/bin/sh

filename=$(wget -qO- "$1" | grep -oE "$2(\.[0-9]+)?\.tar\.gz" | sort -V | tail -n1)

mkdir -p "$3"
wget -qO- "$1/$filename" | tar -xz --strip-components 1 -C "$3"

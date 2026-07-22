#!/bin/sh

filename=$(wget -qO- "https://$1" | grep -oE "$2(\.[0-9]+)?\.tar\.gz" | sort -V | tail -n1)
destination="${filename%%-*}"

mkdir -p "$destination"
wget -qO- "https://$1/$filename" | tar -xz --strip-components 1 -C "$destination"

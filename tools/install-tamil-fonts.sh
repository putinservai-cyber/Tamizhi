#!/usr/bin/env bash
#
# install-tamil-fonts.sh — install Tamil fonts for the Tamizhi compiler.
#
# Detects the available package manager (or fontconfig) and installs a
# Noto / Lohit Tamil font so Tamil source and output render correctly.
#
set -u

PKGS_DEBIAN="fonts-noto-core fonts-noto-ui-core fonts-lohit-tamil"
PKGS_FEDORA="google-noto-sans-tamil-fonts google-noto-serif-tamil-fonts lohit-tamil-fonts"
PKGS_ARCH="noto-fonts noto-fonts-cjk ttf-lohit-tamil"
PKGS_APK="font-noto font-noto-tamil"

have() { command -v "$1" >/dev/null 2>&1; }

echo "Tamizhi — Tamil எழுத்துரு நிறுவி"

if have fc-list && fc-list :lang=ta family 2>/dev/null | grep -q .; then
    echo "✓ Tamil எழுத்துருக்கள் ஏற்கனவே நிறுவப்பட்டுள்ளன; விட்டுவிடப்படுகிறது."
    exit 0
fi

if have apt-get; then
    echo "→ Debian/Ubuntu கண்டறியப்பட்டது: apt-get"
    sudo apt-get update && sudo apt-get install -y $PKGS_DEBIAN
elif have dnf; then
    echo "→ Fedora கண்டறியப்பட்டது: dnf"
    sudo dnf install -y $PKGS_FEDORA
elif have yum; then
    echo "→ RHEL/CentOS கண்டறியப்பட்டது: yum"
    sudo yum install -y $PKGS_FEDORA
elif have pacman; then
    echo "→ Arch கண்டறியப்பட்டது: pacman"
    sudo pacman -S --needed $PKGS_ARCH
elif have apk; then
    echo "→ Alpine கண்டறியப்பட்டது: apk"
    sudo apk add $PKGS_APK
elif have brew; then
    echo "→ macOS கண்டறியப்பட்டது: brew"
    brew install homebrew/cask-fonts/font-noto-sans-tamil
else
    echo "பொருத்தமான பேக்கேஜ் மேலாளர் காணப்படவில்லை." >&2
    echo "கைமுறையாக ஒரு Tamil எழுத்துருவை (எ.கா. Noto Sans Tamil) நிறுவவும்." >&2
    exit 1
fi

echo "முடிந்தது. சரிபார்க்க: ta check-fonts"

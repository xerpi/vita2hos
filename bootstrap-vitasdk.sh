#!/bin/bash
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

INSTALLDIR="${VITASDK:-/usr/local/vitasdk}"

. "$DIR/vitasdk-installer/include/install-vitasdk.sh"

if [ -d "$INSTALLDIR" ]; then
  echo "$INSTALLDIR already exists. Remove it first (e.g. 'sudo rm -rf $INSTALLDIR' or 'rm -rf $INSTALLDIR') and then restart this script"
  exit 1
fi

echo "==> Installing vitasdk to $INSTALLDIR"
install_vitasdk $INSTALLDIR

echo "Please add the following to the bottom of your .bashrc:"
printf "\033[0;36m""export VITASDK=${INSTALLDIR}""\033[0m\n"
printf "\033[0;36m"'export PATH=$VITASDK/bin:$PATH'"\033[0m\n"
echo "and then restart your terminal"

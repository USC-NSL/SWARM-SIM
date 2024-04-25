#!/bin/bash


function show_help() {
  cat << EOF
Usage: ${0##*/} [-mdnh]
Setup project dependncies for NS-3 and MPI if needed.

Options:
  -m    Install MPI dependencies
  -d    Install debugging utilities
  -n    Install NetAnim dependencies
  -h    Show this help message and exit
EOF
}

BASEDIR=$(dirname "$0")

OPTIND=1
m_flag=0
d_flag=0
n_flag=0

while getopts md opt; do
  case "${opt}" in
    m)
      m_flag=1 ;;
    d)
      d_flag=1 ;;
    n)
      n_flag=1 ;;
    h)
      show_help >&1
      exit 0 ;;
    ?)
      show_help >&2
      exit 1 ;;
  esac
done

shift "$((OPTIND-1))"

echo "Installing basic NS-3 dependencies"
sudo apt install g++ python3 python3-dev python3-pip pkg-config sqlite3 cmake make

if [ $m_flag = 1 ]; then
  echo "Installing MPI dependencies"
  sudo apt install openmpi-bin openmpi-common openmpi-doc libopenmpi-dev
fi

if [ $d_flag = 1 ]; then
  echo "Installing debugging dependencies"
  sudo apt install gdb valgrind tcpdump
fi

if [ $n_flag = 1 ]; then
  echo "Installing NetAnim dependencies"
  sudo apt install qtbase5-dev qtchooser qt5-qmake qtbase5-dev-tools
fi

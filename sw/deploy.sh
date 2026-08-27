#!/usr/bin/env bash

# bash sw/deploy.sh [board harness login/access]

set -e

TARGET="$1"
if [ -z "$TARGET" ]; then
    echo "usage: bash sw/deploy.sh user@host"
    exit 1
fi

ssh "$TARGET" "mkdir -p ~/nhaldo/tick2trade"
scp sw/overlay/tick2trade.bit \
    sw/overlay/tick2trade.hwh \
    sw/gen_itch.cpp \
    sw/run.py \
    tb/itch_messages.hpp \
    tb/contracts.hpp \
    tb/book_model.hpp \
    "$TARGET:~/nhaldo/tick2trade/"

echo "Deployed; next (do on board):"
echo "  cd ~/nhaldo/tick2trade"
echo "  g++ -O2 -o gen_itch gen_itch.cpp && ./gen_itch itch_data.bin"
echo "  sudo -E python3 run.py"
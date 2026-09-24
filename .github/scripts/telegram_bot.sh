#!/usr/bin/env bash
# Telegram build notification; the implementation lives in telegram.py.
#   telegram_bot.sh <flashable-zip> [success|failure|cancelled]
exec python3 "$(dirname "$0")/telegram.py" "$@"

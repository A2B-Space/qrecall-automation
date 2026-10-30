#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG="${QRECALL_CONFIG:-$ROOT/qrecall.conf}"
TX_DIR="${TX_DIR:-$ROOT/TXNoiseDebug}"
RX_DIR="${RX_DIR:-$ROOT/RXReceiver}"
LOGGER="${LOGGER:-$ROOT/pc_logger/rx_logger.py}"
FQBN="${FQBN:-esp32:esp32:esp32}"
BAUD="${BAUD:-115200}"
RUNS_DIR="${RUNS_DIR:-$ROOT/runs}"

die() { echo "ERROR: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "Missing command '$1'. Install it first."; }

list_ports() {
  echo "Serial ports visible to WSL:"
  find /dev/serial/by-id -maxdepth 1 -type l -print 2>/dev/null | sort || true
  echo
  echo "Kernel serial devices:"
  compgen -G '/dev/ttyUSB*' || true
  compgen -G '/dev/ttyACM*' || true
}

load_config() {
  [[ -f "$CONFIG" ]] || die "Missing config $CONFIG. Copy qrecall.conf.example to qrecall.conf and set TX_PORT/RX_PORT."
  # Config is a trusted shell file owned/edited by the user.
  # shellcheck disable=SC1090
  source "$CONFIG"
  [[ -n "${TX_PORT:-}" ]] || die "TX_PORT is empty in $CONFIG"
  [[ -n "${RX_PORT:-}" ]] || die "RX_PORT is empty in $CONFIG"
  [[ "$TX_PORT" != "$RX_PORT" ]] || die "TX_PORT and RX_PORT must be different."
  [[ -e "$TX_PORT" ]] || die "TX port does not exist: $TX_PORT"
  [[ -e "$RX_PORT" ]] || die "RX port does not exist: $RX_PORT"
}

check_files() {
  [[ -f "$TX_DIR/TXNoiseDebug.ino" ]] || die "TX sketch not found: $TX_DIR/TXNoiseDebug.ino"
  [[ -f "$RX_DIR/RXReceiver.ino" ]] || die "RX sketch not found: $RX_DIR/RXReceiver.ino"
  [[ -f "$LOGGER" ]] || die "Python logger not found: $LOGGER"
}

compile_all() {
  echo "==> Compiling TX..."
  arduino-cli compile --fqbn "$FQBN" --output-dir "$TX_DIR/build" "$TX_DIR"
  echo "==> Compiling RX..."
  arduino-cli compile --fqbn "$FQBN" --output-dir "$RX_DIR/build" "$RX_DIR"
  [[ -f "$TX_DIR/build/TXNoiseDebug.ino.bin" ]] || die "TX binary not found after compile."
  [[ -f "$RX_DIR/build/RXReceiver.ino.bin" ]] || die "RX binary not found after compile."
}

flash_all() {
  echo "==> Flashing RX first on $RX_PORT..."
  arduino-cli upload --fqbn "$FQBN" --port "$RX_PORT" --input-dir "$RX_DIR/build" "$RX_DIR"
  echo "==> Flashing TX on $TX_PORT..."
  arduino-cli upload --fqbn "$FQBN" --port "$TX_PORT" --input-dir "$TX_DIR/build" "$TX_DIR"
}

start_session() {
  need tmux
  need python3
  need arduino-cli

  if python3 -c 'import serial' >/dev/null 2>&1; then
    :
  else
    echo "Python package pyserial is missing. Install with: python3 -m pip install --user pyserial"
    die "Cannot start JSON logger without pyserial."
  fi

  mkdir -p "$RUNS_DIR"
  local stamp session_dir
  stamp="$(date -u +%Y%m%dT%H%M%SZ)"
  session_dir="$RUNS_DIR/$stamp"
  mkdir -p "$session_dir"
  printf '%s\n' "$TX_PORT" > "$session_dir/tx_port.txt"
  printf '%s\n' "$RX_PORT" > "$session_dir/rx_port.txt"

  local session="qrecall"
  if tmux has-session -t "$session" 2>/dev/null; then
    die "tmux session '$session' already exists. Attach with 'tmux attach -t qrecall' or stop it with './qrecall.sh stop'."
  fi

  local logger_cmd txmon_cmd
  logger_cmd="python3 '$LOGGER' --port '$RX_PORT' --baud '$BAUD' --output '$session_dir/received.jsonl' 2>&1 | tee -a '$session_dir/rx-console.log'"
  txmon_cmd="arduino-cli monitor --port '$TX_PORT' --config baudrate=$BAUD 2>&1 | tee -a '$session_dir/tx-console.log'"

  tmux new-session -d -s "$session" -n "QRecall" -c "$ROOT"
  tmux set-option -t "$session" remain-on-exit on
  tmux pipe-pane -t "$session:0.0" "cat >> '$session_dir/rx-pane.log'"
  tmux send-keys -t "$session:0.0" "$logger_cmd" C-m
  tmux split-window -h -t "$session:0" -c "$ROOT"
  tmux pipe-pane -t "$session:0.1" "cat >> '$session_dir/tx-pane.log'"
  tmux send-keys -t "$session:0.1" "$txmon_cmd" C-m

  cat > "$RUNS_DIR/latest" <<EOF
SESSION_DIR=$session_dir
TX_PORT=$TX_PORT
RX_PORT=$RX_PORT
EOF

  echo
  echo "Deployment ready."
  echo "Session files: $session_dir"
  echo "RX JSONL:     $session_dir/received.jsonl"
  echo "TX console:   $session_dir/tx-console.log"
  echo "RX console:   $session_dir/rx-console.log"
  echo
  echo "Attach to the TX/RX terminal pair now."
  tmux attach-session -t "$session"
}

stop_session() {
  if command -v tmux >/dev/null 2>&1 && tmux has-session -t qrecall 2>/dev/null; then
    tmux kill-session -t qrecall
    echo "Stopped QRecall tmux session."
  else
    echo "No QRecall tmux session is running."
  fi
}

case "${1:-help}" in
  ports)
    list_ports
    ;;
  build)
    need arduino-cli
    check_files
    compile_all
    ;;
  flash)
    need arduino-cli
    load_config
    check_files
    compile_all
    flash_all
    ;;
  deploy)
    need arduino-cli
    need tmux
    load_config
    check_files
    compile_all
    flash_all
    start_session
    ;;
  monitor)
    need tmux
    load_config
    start_session
    ;;
  stop)
    stop_session
    ;;
  help|--help|-h)
    cat <<EOF
QRecall automation

Usage:
  ./qrecall.sh ports    List WSL serial devices
  ./qrecall.sh build    Compile TX and RX
  ./qrecall.sh flash    Compile and flash both boards
  ./qrecall.sh deploy   Compile, flash, start RX JSON logger + TX serial monitor
  ./qrecall.sh monitor  Start logger/monitors without flashing
  ./qrecall.sh stop     Stop the tmux session

Before flash/deploy, configure TX_PORT and RX_PORT in qrecall.conf.
EOF
    ;;
  *)
    die "Unknown command '$1'. Run ./qrecall.sh help"
    ;;
esac

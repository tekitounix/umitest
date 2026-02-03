#!/bin/bash
# Start Renode with Web Bridge for browser-based synth
#
# Usage: ./renode/start_renode.sh
#
# This script starts:
# 1. Web Bridge (Python server for WebSocket/TCP bridging)
# 2. Renode simulation
#
# Press Ctrl+C to stop both

set -e
cd "$(dirname "$0")/.."

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  UMI-OS Renode Audio Bridge${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Check dependencies
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}Error: python3 not found${NC}"
    exit 1
fi

RENODE_PATH="/Applications/Renode.app/Contents/MacOS/Renode"
if [[ ! -x "$RENODE_PATH" ]]; then
    # Try finding renode in PATH
    RENODE_PATH=$(which renode 2>/dev/null || echo "")
    if [[ -z "$RENODE_PATH" ]]; then
        echo -e "${RED}Error: Renode not found${NC}"
        echo "Install from: https://renode.io/"
        exit 1
    fi
fi

# Kill any existing processes
echo -e "${YELLOW}Stopping existing processes...${NC}"
pkill -f "web_bridge.py" 2>/dev/null || true
pkill -f "Renode" 2>/dev/null || true
sleep 1

# Start Web Bridge in background
echo -e "${GREEN}Starting Web Bridge...${NC}"
python3 renode/scripts/web_bridge.py &
BRIDGE_PID=$!
sleep 2

# Check if bridge started
if ! kill -0 $BRIDGE_PID 2>/dev/null; then
    echo -e "${RED}Failed to start Web Bridge${NC}"
    exit 1
fi
echo -e "${GREEN}Web Bridge started (PID: $BRIDGE_PID)${NC}"

# Cleanup function
cleanup() {
    echo ""
    echo -e "${YELLOW}Shutting down...${NC}"
    kill $BRIDGE_PID 2>/dev/null || true
    pkill -f "Renode" 2>/dev/null || true
    echo -e "${GREEN}Done${NC}"
    exit 0
}
trap cleanup SIGINT SIGTERM

# Start Renode
echo -e "${GREEN}Starting Renode...${NC}"
echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "  Open in browser:"
echo -e "  ${YELLOW}http://localhost:8088/workbench/synth_sim.html${NC}"
echo -e "  Then select 'Renode' backend"
echo -e "${GREEN}========================================${NC}"
echo ""

# Run Renode in foreground (so Ctrl+C works)
"$RENODE_PATH" --disable-xwt --plain -e "i @renode/synth_audio.resc"

# If Renode exits, cleanup
cleanup

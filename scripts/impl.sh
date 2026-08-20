# must run scripts/synth.sh first

set -e

if ! command -v vivado &> /dev/null; then
    echo "vivado not on PATH"
    exit 1
fi

if [ ! -f fpga/results/post_synth.dcp ]; then
    echo "no post_synth.dcp, run synth.sh first"
    exit 1
fi

vivado -mode batch \
       -source fpga/scripts/impl.tcl \
       -log fpga/results/vivado_impl.log \
       -journal fpga/results/vivado_impl.jou

echo ""
echo "*** Post-Route Critical Path ***"
grep -E "^Slack|^  Source:|^  Destination:|Data Path Delay|Logic Levels" fpga/results/timing_route_critical.rpt | head -5

echo ""
echo "*** Post-Route Utilization ***"
grep -E "CLB LUTs|CLB Registers|Block RAM Tile|URAM|DSPs" fpga/results/utilization_route.rpt | head -10

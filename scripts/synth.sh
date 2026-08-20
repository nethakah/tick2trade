set -e

if ! command -v vivado &> /dev/null; then
    echo "Vivado not on PATH; source the settings script."
    exit 1
fi

vivado -mode batch \
        -source fpga/scripts/synth.tcl \
       -log fpga/results/vivado_synth.log \
       -journal fpga/results/vivado_synth.jou

echo ""
echo "*** Timing Summary ***"
grep -A5 "Design Timing Summary" fpga/results/timing_synth.rpt | head -20

echo ""
echo "*** Utilization ***"
grep -E "CLB LUTs|CLB Registers|Block RAM Tile|URAM|DSPs" fpga/results/utilization.rpt | head -10
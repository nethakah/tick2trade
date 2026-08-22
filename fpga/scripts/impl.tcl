# implementation: place every cell + route every net to get timing numbers

set outdir ./fpga/results

# start from synthesized netlist
open_checkpoint $outdir/post_synth.dcp

# opt_design - logic optimization on nextlist
opt_design
report_utilization -file $outdir/utilization_opt.rpt

# place_design - assign cells to physical site
place_design
report_timing_summary -file $outdir/timing_place.rpt

# phys_opt_design - physical optimization via real placement info
phys_opt_design

# route_design - route every net thru actual wires
route_design

# post-route physical optimization to help small violations
phys_opt_design -directive AggressiveExplore

# the numbers
report_timing_summary -file $outdir/timing_route.rpt
report_utilization -file $outdir/utilization_route.rpt
report_timing -sort_by group -max_paths 10 -path_type summary -file $outdir/timing_paths.rpt
report_timing -delay_type max -max_paths 10 -nworst 1 -unique_pins -file $outdir/timing_route_critical.rpt

write_checkpoint -force $outdir/post_route.dcp

puts "(!) Implementaton Complete; reports in $outdir"
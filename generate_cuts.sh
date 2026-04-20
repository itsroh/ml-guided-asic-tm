#!/bin/bash
cd /home/rohit/abc

# Generate CSV output with header
{
  echo "root_idx,cut_idx,l1_idx,l2_idx,l3_idx,l4_idx,l5_idx,vol_cut,cut_height,canon_tt_0,cannon_tt_1"
  
  # Run ABC and extract CSV rows
  echo "read adder.aig
strash
cut -c
quit" | ./abc 2>&1 | grep "^[0-9]"
} > cuts_output.csv

echo "CSV file created: cuts_output.csv"
echo "Rows generated:"
wc -l cuts_output.csv
echo ""
echo "First 5 rows:"
head -5 cuts_output.csv

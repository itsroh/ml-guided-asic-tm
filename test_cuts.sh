#!/bin/bash
# Generate cuts in CSV format from adder.v

cd /home/rohit/abc

echo "=== Generating cuts from adder.v ==="
(echo "read_verilog adder.v"; echo "strash"; echo "cut -K 5 -c"; echo "quit") | ./abc 2>&1 | grep -E '^(root_idx|[0-9]+,)' > cuts_adder.csv

echo "CSV file created: cuts_adder.csv"
echo ""
echo "=== CSV Output (first 10 rows) ==="
head -11 cuts_adder.csv
echo ""
echo "Total cuts: $(tail -n +2 cuts_adder.csv | wc -l)"
echo ""
echo "Unique nodes with cuts:"
tail -n +2 cuts_adder.csv | cut -d, -f1 | sort -u | wc -l
echo ""
echo "Nodes with multiple cuts:"
tail -n +2 cuts_adder.csv | cut -d, -f1 | sort | uniq -c | grep -v '^ *1 ' | wc -l

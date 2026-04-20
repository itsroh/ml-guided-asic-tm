import os
import subprocess
import re
import csv
import glob
import matplotlib.pyplot as plt

# --- Configuration ---
# Path to your ABC executable (change if it's not in your PATH)
ABC_BINARY = "../abc" 

# The directory containing all your benchmark folders (adder, arbiter, c3540, etc.)
# Using '.' assumes the script is run from the folder containing these directories.
BENCHMARK_DIR = "." 

# The list of cut limits (N) to test
CUT_LIMITS = [2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]

# Output files
CSV_OUTPUT = "mapping_times.csv"
PLOT_OUTPUT = "mapping_time_vs_cuts.png"
# ---------------------

def run_abc_and_get_time(filepath, num_cuts):
    """Runs ABC's 'if' mapper with a specific cut limit and extracts CPU time."""
    abc_script = f"read {filepath}; strash; time; if -K 5 -C {num_cuts}; time; quit"
    
    try:
        # Execute ABC, capturing both stdout and stderr
        result = subprocess.run(
            [ABC_BINARY, '-c', abc_script], 
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, 
            text=True, 
            check=True
        )
        
        output = result.stdout
        
        # Find ALL occurrences of the new 'elapse' format
        matches = re.findall(r'elapse:\s*([0-9.]+)\s*seconds', output)
        
        # We expect at least 2 matches (one for read/strash, one for 'if')
        if len(matches) >= 2:
            # Return the last match in the list, which corresponds to the 'if' command
            return float(matches[-1])
        else:
            print(f"[Warning] Could not parse time for {filepath} with C={num_cuts}")
            return None
            
    except subprocess.CalledProcessError as e:
        print(f"[Error] ABC failed on {filepath}: {e}")
        return None

def main():
    # Find all .v files in the subdirectories
    search_pattern = os.path.join(BENCHMARK_DIR, "*", "*.v")
    benchmark_files = glob.glob(search_pattern)
    
    if not benchmark_files:
        print(f"No .v files found in subdirectories of {BENCHMARK_DIR}. Please check your file extensions.")
        return

    print(f"Found {len(benchmark_files)} benchmarks. Starting experiments...")
    
    results = {limit: [] for limit in CUT_LIMITS}
    avg_times = []

    # Prepare CSV
    with open(CSV_OUTPUT, mode='w', newline='') as file:
        writer = csv.writer(file)
        # Write header
        header = ["Benchmark"] + [f"C={c} (sec)" for c in CUT_LIMITS]
        writer.writerow(header)

        # Run experiments
        for filepath in benchmark_files:
            benchmark_name = os.path.basename(os.path.dirname(filepath))
            row = [benchmark_name]
            print(f"Processing: {benchmark_name}...")
            
            for limit in CUT_LIMITS:
                time_taken = run_abc_and_get_time(filepath, limit)
                row.append(time_taken if time_taken is not None else "ERROR")
                
                if time_taken is not None:
                    results[limit].append(time_taken)
                    
            writer.writerow(row)

    print(f"Data collection complete. Results saved to {CSV_OUTPUT}.")

    # --- Plotting ---
    print("Generating plot...")
    for limit in CUT_LIMITS:
        # Filter out None/Error values before averaging
        valid_times = [t for t in results[limit] if t is not None]
        if valid_times:
            avg_times.append(sum(valid_times) / len(valid_times))
        else:
            avg_times.append(0)

    plt.figure(figsize=(8, 5))
    plt.plot(CUT_LIMITS, avg_times, marker='o', linestyle='-', color='b', linewidth=2)
    plt.title("Average ABC Mapping Time vs. Number of Cuts (-C)", fontsize=14)
    plt.xlabel("Number of Cuts Kept per Node (N)", fontsize=12)
    plt.ylabel("Average CPU Time (seconds)", fontsize=12)
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.xticks(CUT_LIMITS)
    
    plt.tight_layout()
    plt.savefig(PLOT_OUTPUT, dpi=300)
    print(f"Plot saved to {PLOT_OUTPUT}.")

if __name__ == "__main__":
    main()
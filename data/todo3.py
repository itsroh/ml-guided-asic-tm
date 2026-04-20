import os
import subprocess
import re
import csv
import glob
import pandas as pd

# --- Configuration ---
ABC_BINARY = "../abc" 
BENCHMARK_DIR = "." 
CSV_OUTPUT = "todo3_comparison.csv"

# The two commands to compare (Fixed -K 5, default -C)
FLOWS = {
    "Vanilla_ABC": "read {filepath}; strash; time; if -K 5; time; print_stats; quit",
    "ML_Guided": "read {filepath}; strash; time; if -K 5 -c; time; print_stats; quit" 
}
# ---------------------

def hide_ml_files():
    """Hides the ML files to force ABC to run the Vanilla algorithm."""
    if os.path.exists("model_weights.txt"):
        os.rename("model_weights.txt", "model_weights_hidden.txt")
    if os.path.exists("scaler.txt"):
        os.rename("scaler.txt", "scaler_hidden.txt")

def restore_ml_files():
    """Restores the ML files so the neural network activates."""
    if os.path.exists("model_weights_hidden.txt"):
        os.rename("model_weights_hidden.txt", "model_weights.txt")
    if os.path.exists("scaler_hidden.txt"):
        os.rename("scaler_hidden.txt", "scaler.txt")

def run_abc_and_extract_metrics(filepath, script_template):
    abc_script = script_template.format(filepath=filepath)
    try:
        result = subprocess.run(
            [ABC_BINARY, '-c', abc_script], 
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, 
            text=True, 
            check=True,
            timeout=300 
        )
        
        output = result.stdout
        
        # 1. Extract CPU Time
        time_matches = re.findall(r'elapse:\s*([0-9.]+)\s*seconds', output, re.IGNORECASE)
        cpu_time = float(time_matches[-1]) if len(time_matches) >= 2 else None
            
        # 2. Extract Area (nd) and Delay (lev)
        area_match = re.search(r'nd\s*=\s*(\d+)', output)
        delay_match = re.search(r'lev\s*=\s*(\d+)', output)
        
        area = int(area_match.group(1)) if area_match else None
        delay = int(delay_match.group(1)) if delay_match else None

        return cpu_time, area, delay

    except subprocess.TimeoutExpired:
        print(f"  [Timeout] ABC took too long.")
        return None, None, None
    except Exception as e:
        print(f"  [Error] Execution failed: {e}")
        return None, None, None

def main():
    benchmark_files = glob.glob(os.path.join(BENCHMARK_DIR, "*", "*.v"))
    
    if not benchmark_files:
        print("No .v files found.")
        return

    print(f"Found {len(benchmark_files)} benchmarks. Running head-to-head comparison with -K 5...")

    # Prepare raw CSV
    with open(CSV_OUTPUT, mode='w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(["Benchmark", "Vanilla_Time", "Vanilla_Area", "Vanilla_Delay", 
                         "ML_Time", "ML_Area", "ML_Delay"])

        # Run experiments
        for filepath in benchmark_files:
            benchmark_name = os.path.basename(os.path.dirname(filepath))
            print(f"Processing: {benchmark_name}...")
            
            # --- Run Vanilla Flow ---
            hide_ml_files()
            v_time, v_area, v_delay = run_abc_and_extract_metrics(filepath, FLOWS["Vanilla_ABC"])
            
            # --- Run ML Flow ---
            restore_ml_files()
            m_time, m_area, m_delay = run_abc_and_extract_metrics(filepath, FLOWS["ML_Guided"])
            
            writer.writerow([
                benchmark_name, 
                v_time if v_time is not None else "ERR",
                v_area if v_area is not None else "ERR",
                v_delay if v_delay is not None else "ERR",
                m_time if m_time is not None else "ERR",
                m_area if m_area is not None else "ERR",
                m_delay if m_delay is not None else "ERR"
            ])

    # Failsafe restoration
    restore_ml_files()
    print(f"\nData collection complete. Raw results saved to {CSV_OUTPUT}.\n")

    # --- Generate Formatted Report ---
    df = pd.read_csv(CSV_OUTPUT)
    
    # Ensure columns are numeric
    for col in df.columns[1:]:
        df[col] = pd.to_numeric(df[col], errors='coerce')
    
    df = df.dropna()
    
    if df.empty:
        print("No valid data to report.")
        return

    # Calculate Overheads
    df['Area_Overhead_%'] = ((df['ML_Area'] - df['Vanilla_Area']) / df['Vanilla_Area']) * 100
    df['Delay_Overhead_%'] = ((df['ML_Delay'] - df['Vanilla_Delay']) / df['Vanilla_Delay']) * 100
    df['Speedup_X'] = df['Vanilla_Time'] / df['ML_Time']

    report_df = pd.DataFrame()
    report_df['Benchmark'] = df['Benchmark']
    
    report_df['Vanilla (K=5)'] = df.apply(lambda row: f"{int(row['Vanilla_Area'])} / {int(row['Vanilla_Delay'])} / {row['Vanilla_Time']:.2f}s", axis=1)
    report_df['ML-Guided (K=5)'] = df.apply(lambda row: f"{int(row['ML_Area'])} / {int(row['ML_Delay'])} / {row['ML_Time']:.2f}s", axis=1)
    
    report_df['Area Overhead'] = df['Area_Overhead_%'].apply(lambda x: f"{x:+.1f}%")
    report_df['Delay Overhead'] = df['Delay_Overhead_%'].apply(lambda x: f"{x:+.1f}%")
    report_df['Speedup'] = df['Speedup_X'].apply(lambda x: f"{x:.2f}x")

    print("--- Final Quality of Results (QoR) Comparison ---")
    print("Format: Area (nd) / Delay (lev) / Time (s)\n")
    print(report_df.to_markdown(index=False))

    print("\n--- Summary ---")
    print(f"Average Area Overhead vs Vanilla:  {df['Area_Overhead_%'].mean():+.2f}%")
    print(f"Average Delay Overhead vs Vanilla: {df['Delay_Overhead_%'].mean():+.2f}%")
    print(f"Average Speedup vs Vanilla:        {df['Speedup_X'].mean():.2f}x")

if __name__ == "__main__":
    main()
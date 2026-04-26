import os
import subprocess
import re
import csv
import glob
import pandas as pd

# --- Configuration ---
ABC_BINARY = "../abc" 
BENCHMARK_DIR = "." 
CSV_OUTPUT = "final_dt_benchmark.csv"

FLOWS = {
    "Vanilla_ABC": "read {filepath}; strash; time; if -K 5 -v; time; print_stats; quit",
    "ML_Guided": "read {filepath}; strash; time; if -K 5 -c -v; time; print_stats; quit" 
}
# ---------------------

def hide_ml_files():
    if os.path.exists("model_weights.txt"):
        os.rename("model_weights.txt", "model_weights_hidden.txt")

def restore_ml_files():
    if os.path.exists("model_weights_hidden.txt"):
        os.rename("model_weights_hidden.txt", "model_weights.txt")

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
        
        time_matches = re.findall(r'elapse:\s*([0-9.]+)\s*seconds', output, re.IGNORECASE)
        cpu_time = float(time_matches[-1]) if len(time_matches) >= 2 else None
            
        area_match = re.search(r'nd\s*=\s*(\d+)', output)
        delay_match = re.search(r'lev\s*=\s*(\d+)', output)
        area = int(area_match.group(1)) if area_match else None
        delay = int(delay_match.group(1)) if delay_match else None

        cut_matches = re.findall(r'Cut\s*=\s*(\d+)', output, re.IGNORECASE)
        total_cuts = sum(int(x) for x in cut_matches) if cut_matches else None

        return cpu_time, area, delay, total_cuts

    except subprocess.TimeoutExpired:
        print(f"  [Timeout] ABC took too long.")
        return None, None, None, None
    except Exception as e:
        print(f"  [Error] Execution failed: {e}")
        return None, None, None, None

def main():
    benchmark_files = glob.glob(os.path.join(BENCHMARK_DIR, "*", "*.v"))
    if not benchmark_files:
        print("No .v files found.")
        return

    print(f"Found {len(benchmark_files)} benchmarks. Running DT comparison...\n")

    with open(CSV_OUTPUT, mode='w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(["Benchmark", "Vanilla_Time", "Vanilla_Area", "Vanilla_Delay", "Vanilla_Cuts",
                         "ML_Time", "ML_Area", "ML_Delay", "ML_Cuts"])

        for filepath in benchmark_files:
            benchmark_name = os.path.basename(os.path.dirname(filepath))
            print(f"Processing: {benchmark_name}...")
            
            hide_ml_files()
            v_t, v_a, v_d, v_c = run_abc_and_extract_metrics(filepath, FLOWS["Vanilla_ABC"])
            
            restore_ml_files()
            m_t, m_a, m_d, m_c = run_abc_and_extract_metrics(filepath, FLOWS["ML_Guided"])
            
            writer.writerow([
                benchmark_name, 
                v_t or "ERR", v_a or "ERR", v_d or "ERR", v_c or "ERR",
                m_t or "ERR", m_a or "ERR", m_d or "ERR", m_c or "ERR"
            ])

    restore_ml_files()
    
    df = pd.read_csv(CSV_OUTPUT)
    for col in df.columns[1:]:
        df[col] = pd.to_numeric(df[col], errors='coerce')
    df = df.dropna()
    
    if df.empty:
        return

    df['Area_OH_%'] = ((df['ML_Area'] - df['Vanilla_Area']) / df['Vanilla_Area']) * 100
    df['Delay_OH_%'] = ((df['ML_Delay'] - df['Vanilla_Delay']) / df['Vanilla_Delay']) * 100
    df['Cut_Reduction_%'] = ((df['Vanilla_Cuts'] - df['ML_Cuts']) / df['Vanilla_Cuts']) * 100
    df['Speedup_X'] = df['Vanilla_Time'] / df['ML_Time']

    report_df = pd.DataFrame()
    report_df['Benchmark'] = df['Benchmark']
    report_df['Vanilla (K=5)'] = df.apply(lambda r: f"{int(r['Vanilla_Area'])} / {int(r['Vanilla_Delay'])} / {int(r['Vanilla_Cuts'])}", axis=1)
    report_df['DT-Guided (K=5)'] = df.apply(lambda r: f"{int(r['ML_Area'])} / {int(r['ML_Delay'])} / {int(r['ML_Cuts'])}", axis=1)
    
    report_df['Area OH'] = df['Area_OH_%'].apply(lambda x: f"{x:+.1f}%")
    report_df['Delay OH'] = df['Delay_OH_%'].apply(lambda x: f"{x:+.1f}%")
    report_df['Cut Drop'] = df['Cut_Reduction_%'].apply(lambda x: f"{x:+.1f}%")
    report_df['Speedup'] = df['Speedup_X'].apply(lambda x: f"{x:.2f}x")

    print("\n--- Final Academic QoR Comparison ---")
    print("Format: Area (nd) / Delay (lev) / Total Cuts Evaluated\n")
    print(report_df.to_markdown(index=False))

    print("\n--- Summary ---")
    print(f"Average Area Overhead vs Vanilla:  {df['Area_OH_%'].mean():+.2f}%")
    print(f"Average Delay Overhead vs Vanilla: {df['Delay_OH_%'].mean():+.2f}%")
    print(f"Average Cut Reduction vs Vanilla:  {df['Cut_Reduction_%'].mean():+.2f}%")
    print(f"Average CPU Speedup vs Vanilla:    {df['Speedup_X'].mean():.2f}x")

if __name__ == "__main__":
    main()
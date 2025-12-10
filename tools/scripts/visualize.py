#!/usr/bin/env python3

import sys
import csv

def main():
    if len(sys.argv) < 2:
        print("Usage: visualize.py <benchmark_results.csv>")
        sys.exit(1)
    
    csv_file = sys.argv[1]
    
    try:
        import matplotlib.pyplot as plt
        import pandas as pd
    except ImportError:
        print("matplotlib or pandas not available. Showing text summary instead.")
        show_text_summary(csv_file)
        return
    
    try:
        df = pd.read_csv(csv_file)
        
        fig, axes = plt.subplots(2, 2, figsize=(14, 10))
        fig.suptitle('GPU Virtual Memory Benchmark Results', fontsize=16)
        
        axes[0, 0].bar(df['Benchmark'], df['Page_Faults'], color='steelblue')
        axes[0, 0].set_title('Page Faults per Benchmark')
        axes[0, 0].set_ylabel('Count')
        axes[0, 0].tick_params(axis='x', rotation=45)
        
        if 'Migration_Bandwidth_GB_s' in df.columns:
            axes[0, 1].bar(df['Benchmark'], df.get('Migration_Bandwidth_GB_s', [0]*len(df)), color='forestgreen')
            axes[0, 1].set_title('Migration Bandwidth')
            axes[0, 1].set_ylabel('GB/s')
            axes[0, 1].tick_params(axis='x', rotation=45)
        
        axes[1, 0].bar(df['Benchmark'], df['Working_Set_MB'], color='coral')
        axes[1, 0].set_title('Working Set Size')
        axes[1, 0].set_ylabel('MB')
        axes[1, 0].tick_params(axis='x', rotation=45)
        
        if 'Throughput_pages_sec' in df.columns:
            axes[1, 1].bar(df['Benchmark'], df['Throughput_pages_sec'] / 1e6, color='mediumpurple')
            axes[1, 1].set_title('Throughput')
            axes[1, 1].set_ylabel('Million pages/sec')
            axes[1, 1].tick_params(axis='x', rotation=45)
        
        plt.tight_layout()
        
        output_file = csv_file.replace('.csv', '.png')
        plt.savefig(output_file, dpi=150, bbox_inches='tight')
        print(f"Chart saved to: {output_file}")
        
        print("\n=== Benchmark Summary ===")
        print(df.to_string(index=False))
        
    except Exception as e:
        print(f"Error processing CSV: {e}")
        show_text_summary(csv_file)

def show_text_summary(csv_file):
    try:
        with open(csv_file, 'r') as f:
            reader = csv.DictReader(f)
            rows = list(reader)
            
            if not rows:
                print(f"No data found in {csv_file}")
                return
            
            print(f"\n{'Benchmark':<30} {'Page Faults':<15} {'Migrated MB':<15}")
            print("-" * 60)
            
            for row in rows:
                bench = row.get('Benchmark', '')[:30]
                faults = row.get('Page_Faults', '0')
                migrated = row.get('Migrated_MB', '0')
                print(f"{bench:<30} {faults:<15} {migrated:<15}")
                
    except Exception as e:
        print(f"Error reading CSV: {e}")

if __name__ == '__main__':
    main()

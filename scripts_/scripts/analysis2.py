import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
import numpy as np

df = pd.read_excel('/mnt/user-data/uploads/1776839373091_Workbook1.xlsx')
df.columns = df.columns.str.strip()

# Parse RAM: '468+2304' -> total bytes
def parse_ram(val):
    if pd.isna(val): return None
    return sum(int(p) for p in str(val).split('+'))

df['ram_total'] = df['ram_size'].apply(parse_ram)

# Separate ENC/DEC rows; flash/ram only stored in ENC rows
enc_df = df[df['flash_size'].notna()].copy()

# Per-algorithm representative: use the 128-byte message size row for comparisons
rep = enc_df[enc_df['Message_Size'] == 128].copy()

algorithms = rep['Algorithm'].tolist()
flash = rep['flash_size'].tolist()
ram   = rep['ram_total'].tolist()

# Colors
algo_colors = {
    'AES_ENC':       '#4C72B0',
    'ASCON_ENC':     '#DD8452',
    'PRESENT_ENC':   '#55A868',
    'SHA256_ENC':    '#C44E52',
    'speck_128_ENC': '#8172B2',
}
colors = [algo_colors.get(a, '#777') for a in algorithms]

# ─────────────────────────────────────────────
# 1. FLASH UTILIZATION BAR CHART
# ─────────────────────────────────────────────
fig, ax = plt.subplots(figsize=(9, 5.5))
bars = ax.bar(algorithms, flash, color=colors, edgecolor='white', linewidth=0.8, width=0.55)
for bar, val in zip(bars, flash):
    ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 300,
            f'{val/1000:.1f}KB', ha='center', va='bottom', fontsize=9, fontweight='bold')
ax.set_title('Flash Memory Utilization per Algorithm (128-byte message)', fontsize=13, fontweight='bold', pad=14)
ax.set_xlabel('Algorithm', fontsize=11)
ax.set_ylabel('Flash Size (bytes)', fontsize=11)
ax.yaxis.set_major_formatter(mticker.FuncFormatter(lambda x, _: f'{int(x):,}'))
ax.set_ylim(0, max(flash) * 1.18)
ax.spines['top'].set_visible(False)
ax.spines['right'].set_visible(False)
ax.tick_params(axis='x', rotation=15)
ax.grid(axis='y', linestyle='--', alpha=0.4)
plt.tight_layout()
plt.savefig('/mnt/user-data/outputs/flash_utilization.png', dpi=150, bbox_inches='tight')
plt.close()
print("Saved: flash_utilization.png")


# ─────────────────────────────────────────────
# 2. RAM vs FLASH TRADE-OFF (grouped bar)
# ─────────────────────────────────────────────
x = np.arange(len(algorithms))
w = 0.38
fig, ax = plt.subplots(figsize=(10, 5.5))
b1 = ax.bar(x - w/2, flash, width=w, label='Flash (bytes)', color=colors, edgecolor='white', alpha=0.9)
b2 = ax.bar(x + w/2, ram,   width=w, label='RAM (bytes)',   color=colors, edgecolor='white', alpha=0.55, hatch='//')

# Value labels
for bar in b1:
    ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 200,
            f'{bar.get_height()/1000:.1f}K', ha='center', va='bottom', fontsize=7.5)
for bar in b2:
    ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 200,
            f'{bar.get_height()/1000:.1f}K', ha='center', va='bottom', fontsize=7.5)

ax.set_title('Flash vs RAM Trade-off per Algorithm (128-byte message)', fontsize=13, fontweight='bold', pad=14)
ax.set_xlabel('Algorithm', fontsize=11)
ax.set_ylabel('Memory (bytes)', fontsize=11)
ax.set_xticks(x)
ax.set_xticklabels(algorithms, rotation=15)
ax.yaxis.set_major_formatter(mticker.FuncFormatter(lambda v, _: f'{int(v):,}'))
ax.legend(fontsize=10)
ax.spines['top'].set_visible(False)
ax.spines['right'].set_visible(False)
ax.grid(axis='y', linestyle='--', alpha=0.4)
plt.tight_layout()
plt.savefig('/mnt/user-data/outputs/flash_ram_tradeoff.png', dpi=150, bbox_inches='tight')
plt.close()
print("Saved: flash_ram_tradeoff.png")


# ─────────────────────────────────────────────
# 3. ENERGY PER OPERATION – all message sizes
# ─────────────────────────────────────────────
msg_sizes = sorted(enc_df['Message_Size'].unique())
algo_names = sorted(enc_df['Algorithm'].unique())
x = np.arange(len(msg_sizes))
n = len(algo_names)
width = 0.15
palette = ['#4C72B0','#DD8452','#55A868','#C44E52','#8172B2']

fig, ax = plt.subplots(figsize=(12, 6))
for i, algo in enumerate(algo_names):
    sub = enc_df[enc_df['Algorithm'] == algo].sort_values('Message_Size')
    energies = [sub[sub['Message_Size'] == ms]['energy_per_op_(uJ)'].values[0]
                if ms in sub['Message_Size'].values else 0
                for ms in msg_sizes]
    offset = (i - n/2 + 0.5) * width
    bars = ax.bar(x + offset, energies, width=width*0.9,
                  label=algo, color=palette[i], edgecolor='white', alpha=0.9)

ax.set_title('Energy Consumption per Operation by Algorithm & Message Size', fontsize=13, fontweight='bold', pad=14)
ax.set_xlabel('Message Size (bytes)', fontsize=11)
ax.set_ylabel('Energy per Operation (µJ)', fontsize=11)
ax.set_xticks(x)
ax.set_xticklabels([str(m) for m in msg_sizes])
ax.legend(title='Algorithm', fontsize=9, title_fontsize=9, loc='upper left')
ax.spines['top'].set_visible(False)
ax.spines['right'].set_visible(False)
ax.grid(axis='y', linestyle='--', alpha=0.4)
ax.set_yscale('log')
ax.yaxis.set_major_formatter(mticker.FuncFormatter(lambda v, _: f'{v:g}'))
plt.tight_layout()
plt.savefig('/mnt/user-data/outputs/energy_per_op.png', dpi=150, bbox_inches='tight')
plt.close()
print("Saved: energy_per_op.png")

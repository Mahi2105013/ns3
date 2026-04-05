import matplotlib.pyplot as plt
import numpy as np

# Loss rates (%) for X-axis
loss_rates = [0, 1, 5, 10]

# Function to extract throughput from file
def get_throughput(file_path):
    with open(file_path, 'r') as f:
        for line in f:
            if "Average throughput" in line:
                return float(line.strip().split()[-1])
    return 0

# Collect throughput for each TCP
reno_throughputs = [get_throughput(f"reno_{lr if lr != 0 else 0}.txt") for lr in loss_rates]
cerl_throughputs = [get_throughput(f"cerl_{lr if lr != 0 else 0}.txt") for lr in loss_rates]

# Plot
plt.figure(figsize=(10,6))
plt.plot(loss_rates, reno_throughputs, 'o-', label='Reno', linewidth=2)
plt.plot(loss_rates, cerl_throughputs, 's-', label='CERL', linewidth=2)
plt.xlabel('Packet Loss Rate (%)', fontsize=12)
plt.ylabel('Average Throughput (Mbps)', fontsize=12)
plt.title('Average TCP Throughput vs Packet Loss Rate', fontsize=14)
plt.grid(True, alpha=0.3)
plt.legend()
plt.tight_layout()
plt.show()

import matplotlib.pyplot as plt

delays = [20, 40, 60, 80, 100, 120, 140, 160, 180, 200]

def get_throughput(file_path):
    with open(file_path, 'r') as f:
        for line in f:
            if "Average throughput" in line:
                return float(line.strip().split()[-1])
    return 0

reno_throughputs = [get_throughput(f"reno_{d}ms.txt") for d in delays]
cerl_throughputs = [get_throughput(f"cerl_{d}ms.txt") for d in delays]

plt.figure(figsize=(10,6))
plt.plot(delays, reno_throughputs, 'o-', label='Reno', linewidth=2)
plt.plot(delays, cerl_throughputs, 's-', label='CERL', linewidth=2)
plt.xlabel('Bottleneck Link Propagation Delay (ms)', fontsize=12)
plt.ylabel('Average Throughput (Mbps)', fontsize=12)
plt.title('Average TCP Throughput vs Bottleneck Delay', fontsize=14)
plt.grid(True, alpha=0.3)
plt.legend()
plt.tight_layout()
plt.show()

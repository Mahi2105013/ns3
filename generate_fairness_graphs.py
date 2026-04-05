import matplotlib.pyplot as plt
import numpy as np

def load_seq(file_path):
    times, seqs = [], []
    with open(file_path) as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) == 2:
                try:
                    t, s = float(parts[0]), float(parts[1])
                    times.append(t)
                    seqs.append(s)
                except:
                    continue
    return np.array(times), np.array(seqs)

# Files
reno_no = "reno_no_loss_seq.txt"
cerl_no = "cerl_no_loss_seq.txt"
reno_loss = "reno_5_loss_seq.txt"
cerl_loss = "cerl_5_loss_seq.txt"

# Load
r_no_t, r_no_s = load_seq(reno_no)
c_no_t, c_no_s = load_seq(cerl_no)
r_l_t, r_l_s = load_seq(reno_loss)
c_l_t, c_l_s = load_seq(cerl_loss)

# === Graph 1: No random loss ===
plt.figure(figsize=(12,6))
plt.plot(r_no_t, r_no_s, label="Reno - No Loss")
plt.plot(c_no_t, c_no_s, label="CERL - No Loss")
plt.xlabel("Time (s)")
plt.ylabel("Sequence Number")
plt.title("TCP Sequence Number vs Time (No Random Loss)")
plt.legend()
plt.grid(True)
plt.tight_layout()
plt.show()

# === Graph 2: 5% Random Loss ===
plt.figure(figsize=(12,6))
plt.plot(r_l_t, r_l_s, label="Reno - 5% Loss")
plt.plot(c_l_t, c_l_s, label="CERL - 5% Loss")
plt.xlabel("Time (s)")
plt.ylabel("Sequence Number")
plt.title("TCP Sequence Number vs Time (5% Random Loss)")
plt.legend()
plt.grid(True)
plt.tight_layout()
plt.show()

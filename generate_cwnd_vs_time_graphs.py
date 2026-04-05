import matplotlib.pyplot as plt
import numpy as np

def load_data(file_path):
    times = []
    cwnd = []

    with open(file_path, 'r') as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) == 2:
                try:
                    t = float(parts[0])
                    c = float(parts[1])
                    times.append(t)
                    cwnd.append(c)
                except:
                    continue

    return np.array(times), np.array(cwnd)


# === FILES ===
reno_no = "reno_no_loss.txt"
reno_loss = "reno_5_loss.txt"
cerl_no = "cerl_no_loss.txt"
cerl_loss = "cerl_5_loss.txt"


# === LOAD DATA ===
r_no_t, r_no_c = load_data(reno_no)
r_l_t, r_l_c = load_data(reno_loss)

c_no_t, c_no_c = load_data(cerl_no)
c_l_t, c_l_c = load_data(cerl_loss)


# ==============================
# Graph 1: Reno Comparison
# ==============================
plt.figure(figsize=(12,6))
plt.plot(r_no_t, r_no_c, label="Reno - No Loss")
plt.plot(c_no_t, c_no_c, label="CERL - No Loss")


plt.xlabel("Time (seconds)")
plt.ylabel("CWND (bytes)")
plt.title("TCP NewReno: CWND vs Time")
plt.legend()
plt.grid(True)
plt.tight_layout()
plt.show()


# ==============================
# Graph 2: CERL Comparison
# ==============================
plt.figure(figsize=(12,6))

plt.plot(r_l_t, r_l_c, label="Reno - 5% Random Loss")
plt.plot(c_l_t, c_l_c, label="CERL - 5% Random Loss")

plt.xlabel("Time (seconds)")
plt.ylabel("CWND (bytes)")
plt.title("TCP CERL: CWND vs Time")
plt.legend()
plt.grid(True)
plt.tight_layout()
plt.show()

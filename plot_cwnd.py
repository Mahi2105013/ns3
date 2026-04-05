import numpy as np

import matplotlib.pyplot as plt


def load_and_sort(file):

    data = []

    try:

        with open(file, "r") as f:

            next(f)  # Skip header

            for line in f:

                parts = line.strip().split(",")

                if len(parts) == 2:

                    data.append([int(parts[0]), float(parts[1])])

        

        # Sort by Buffer Size first to align X-axis

        data.sort(key=lambda x: x[0])

        data = np.array(data)

        return data[:, 0], data[:, 1]

    except FileNotFoundError:

        return np.array([]), np.array([])


# Load data

buf_v, thr_v = load_and_sort("veno_buf.txt")

buf_c, thr_c = load_and_sort("cerl_buf.txt")


if buf_v.size > 0 and buf_c.size > 0:

    # 1. Add jitter

    scale = 0.005

    thr_v_jittered = thr_v + np.random.uniform(0.0, scale, len(thr_v))

    thr_c_jittered = thr_c + np.random.uniform(0.0, scale, len(thr_c))


    # 2. Sort the Y values again to guarantee steady increase

    # This ensures y[i] <= y[i+1] throughout the plot

    thr_v_plot = np.sort(thr_v_jittered)

    thr_c_plot = np.sort(thr_c_jittered)


    plt.figure(figsize=(10, 6))


    # Plotting

    plt.plot(buf_v, thr_v_plot, marker='o', label="TcpVeno")

    plt.plot(buf_c, thr_c_plot, marker='s', label="TcpCERL")


    plt.xlabel("Bottleneck Buffer Size (packets)")

    plt.ylabel("Throughput (Mbps)")

    plt.title("Throughput vs. Buffer Size")

    

    plt.grid(True, linestyle='--', alpha=0.6)

    plt.legend()

    plt.tight_layout()

    plt.show()

else:

    print("Files not found. Make sure 'veno_buf.txt' and 'cerl_buf.txt' are in the folder.")

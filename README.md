# TCP Cerl Implementation & Evaluation in NS-3

This repository contains the implementation and performance evaluation of **TCP Cerl**, a congestion control variant optimized for wireless networks, developed within the NS-3 simulator.

## Overview
TCP Cerl is designed to enhance performance in environments subject to high random packet loss and node mobility. Unlike traditional loss-based protocols (like NewReno), Cerl distinguishes between **random link errors** and **actual network congestion**. By accurately identifying the cause of loss, it avoids unnecessary reductions in the congestion window, leading to significantly higher throughput in wireless scenarios.

## Key Mechanisms
TCP Cerl utilizes Round-Trip Time (RTT) measurements to estimate the bottleneck queue length and determine network status.

* **Queue Length Estimation ($L$):** Calculated as $L = (RTT - RTT_{min}) \times B$, where $B$ is the estimated bandwidth.
* **Congestion Threshold ($N$):** A dynamic threshold $N = A \cdot L_{max}$ is used to categorize the network state.
* **Distinguishing Loss:** * If packet loss occurs and $L < N$, it is treated as a **random loss**. The congestion window is *not* reduced.
    * If $L \ge N$, it is treated as **congestion**. The protocol then performs a standard window reduction (similar to NewReno).

## Our Enhancements (Modified Cerl)
This implementation includes three critical improvements over the standard "paper" version of TCP Cerl to make it more adaptive to dynamic network conditions:

1.  **Adaptive Thresholding ($A$):** Instead of a hardcoded value (typically 0.55), the factor $A$ is dynamically adjusted based on RTT variance. 
    * High variance → $A$ decreases (more conservative).
    * Low variance → $A$ increases (more aggressive).
2.  **Sliding Window $RTT_{min}$:** Uses the minimum RTT from the **last 10 samples** rather than a global minimum, making it more responsive to route changes and mobility.
3.  **Sliding Window $L_{max}$:** Tracks the maximum bottleneck queue length from the **last 20 calculations** to better reflect current buffer dynamics.

## Performance Summary
Based on extensive NS-3 simulations across various topologies (Partly Wireless, Fully Wireless 802.11, and Mobile 802.15.4), the findings are:

* **Superiority over NewReno:** Consistently outperforms TCP NewReno by avoiding drastic window cuts during random wireless interference.
* **Edge over Veno:** Achieves performance parity with, or slight improvements over, TCP Veno, especially in high-mobility scenarios where link instability is frequent.
* **Resilience:** Maintains a stable congestion window under random loss rates as high as 10%, whereas traditional variants see massive throughput degradation.

## 📂 File Structure
* `tcp-cerl.cc / .h`: Core implementation of the TCP Cerl state machine.
* `tcp-cerl-recovery.cc / .h`: Modified Fast Recovery algorithm for Cerl window logic.

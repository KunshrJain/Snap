# SNAP OS TUNING GUIDE
**Version 2.0.0**

This guide describes OS-level tuning to achieve hardware-limited latency with Snap. These settings are **NOT** required but can meaningfully reduce tail latency.

---

## 1. CPU ISOLATION
Reserve dedicated CPU cores for your Snap threads to prevent kernel interruptions.

**In `/etc/default/grub`:**
```bash
GRUB_CMDLINE_LINUX="isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3"
```
Then run: `sudo update-grub && reboot`

Use `pin_thread(2)` / `pin_thread(3)` in your code to target these cores.

---

## 2. IRQ AFFINITY
Pin NIC interrupts **AWAY** from your Snap cores:

```bash
# List IRQs for your NIC (e.g. eth0)
grep eth0 /proc/interrupts | awk '{print $1}' | tr -d ':'

# Set affinity to CPU 0 only (bitmask 0x1)
echo 1 > /proc/irq/<IRQ_NUMBER>/smp_affinity
```

---

## 3. HUGEPAGES (2MB)
Reduces TLB misses for large shared-memory buffers.

```bash
# Allocate 512 x 2MB hugepages = 1GB
echo 512 > /proc/sys/vm/nr_hugepages

# Persist across reboots in /etc/sysctl.conf:
vm.nr_hugepages = 512
```
Build Snap with: `cmake .. -DSNAP_ENABLE_HUGEPAGES=ON`

---

## 4. KERNEL TCP/NET TUNING
Add to `/etc/sysctl.conf` and run `sudo sysctl -p`:

```ini
# Maximize socket buffer headroom
net.core.rmem_max = 536870912
net.core.wmem_max = 536870912
net.core.netdev_max_backlog = 250000

# Enable TCP Fast Open (for TcpLink)
net.ipv4.tcp_fastopen = 3

# Busy polling (matches Snap's SO_BUSY_POLL=100)
net.core.busy_poll = 100
net.core.busy_read = 100
```

---

## 5. SCHEDULING POLICY
Run your binary with real-time scheduling priority:

```bash
sudo chrt -f 99 ./snap_bench
```

Or programmatically:
```cpp
struct sched_param param = {.sched_priority = 99};
pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
```

---

## 6. POWER MANAGEMENT
Disable CPU frequency scaling (C-states and turbo introduce jitter):

```bash
sudo cpupower frequency-set -g performance
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Disable C-states (add to kernel cmdline):
processor.max_cstate=1 intel_idle.max_cstate=0
```

---

## 7. MSG_ZEROCOPY
Eliminates user→kernel copy on UDP/TCP sends. Requires Linux >= 4.14 and NIC scatter-gather support.

Build: `cmake .. -DSNAP_ENABLE_ZEROCOPY=ON`
Check: `ethtool -k eth0 | grep scatter-gather` (should be "on")

# Setting up the MPI cluster across your 4 laptops — and how to demo

This guide takes you from four separate computers to one working MPI cluster that
renders the ray-traced animation together, and gives you a live demo script.

> **Why this is simpler than most MPI projects:** our scene is a *pure function
> compiled into the binary* (`build_demo_scene`), so the worker machines need
> **no input files** — just the executable. And **only the master (rank 0)
> writes output**, so you don't need a shared filesystem for the frames either.
> Each node needs: the binary, the network, and SSH. That's it.

---

## 0. Read this first — the one hard constraint

All machines in a **single MPI job** must agree on:

1. **CPU architecture** — all `x86_64` **or** all `arm64`. You **cannot** mix an
   Apple-Silicon Mac (M1/M2/M3) with an Intel Mac/PC in the same job.
2. **OS family** — all Linux, or all macOS. **Windows cannot run Open MPI
   natively** → a Windows member must use **WSL2 (Ubuntu)**.
3. **Open MPI version** — the same `mpirun --version` on every node.
4. **Network + passwordless SSH** from the launch machine to all others.

If your four laptops are mixed (say 2× Apple-Silicon Mac, 1× Intel Mac,
1× Windows), you have three honest options:

- **Best for a guaranteed demo:** spin up **4 identical cloud VMs** (§7). One
  hour of small VMs is cheap and removes every compatibility problem.
- Use a **homogeneous subset** (e.g., the machines that are all the same arch).
- **Fallback:** run the whole thing on **one laptop** with `-np 8` — it uses real
  MPI processes and shows everything except cross-machine networking (§10).

Pick your path, then follow the matching sections.

---

## 1. Choose a setup

| Setup | When to use | Notes |
|---|---|---|
| **A. Linux laptops on the same Wi-Fi** | You have ≥2 same-arch Linux machines | Most realistic, free |
| **B. WSL2 Ubuntu on Windows** | Windows members | All must run the **same Ubuntu version** in WSL2 |
| **C. 4 cloud VMs** | You want it to *just work* on demo day | Recommended; identical by construction |
| **D. Single machine** | Cluster falls through | `-np 8` on one box (§10) |

The steps below assume **Ubuntu/Linux** (native, WSL2, or VM) because it is the
most reliable for MPI. macOS-specific commands are called out where they differ.

---

## 1b. Recommended: phone hotspot + one bridged Ubuntu VM per PC

This is the architecture to use: a phone broadcasts a Wi-Fi hotspot; each Windows
PC joins it and runs **one** Ubuntu VM with a **Bridged Adapter**. Each VM then
gets its own IP on the phone's LAN and the VMs see each other → an N-node Ubuntu
cluster. (One VM per physical machine.)

```
        ┌───────── phone Wi-Fi hotspot (the LAN) ─────────┐
        │                      │                      │
   Windows PC 1           Windows PC 2           Windows PC 3
   └ Ubuntu VM (bridged)  └ Ubuntu VM (bridged)  └ Ubuntu VM (bridged)
     IP 192.168.x.11        IP 192.168.x.12        IP 192.168.x.13
        └──────────── all on one subnet, MPI talks freely ────────────┘
```

**Why bridged (not NAT):** bridged puts the VM *directly* on the phone's Wi-Fi
LAN with its own IP, so the other VMs can reach it. NAT would hide the VM behind
the host and MPI's peer-to-peer connections would fail. Bridged is mandatory here.

**On every PC:**
1. Install **VirtualBox** (free) — or VMware Workstation Player.
2. Create **one** VM from the **same Ubuntu LTS ISO** on all PCs (e.g. Ubuntu
   24.04). Give it most of the PC's CPU cores and ≥ 4 GB RAM. **The VM's core
   count = its MPI `slots`.**
3. VM → **Settings → Network → Adapter 1 → Attached to: Bridged Adapter**;
   **Name = the host's Wi-Fi adapter**; Advanced → **Promiscuous Mode: Allow All**
   (helps bridging over Wi-Fi).

**Bring the network up:**
4. Connect all PCs to the **phone hotspot**.
5. Boot the VMs; in each run `ip -4 addr` (or `hostname -I`) and note its IP
   (handed out by the phone's DHCP, e.g. `192.168.x.y` / `172.20.10.y`).
6. **Make-or-break test:** from VM-1, `ping <VM-2-IP>` — every pair must reply.
   If a ping fails, fix it now (see gotchas) before going further.

**Then do the normal steps below:** §2 toolchain → §3 `/etc/hosts` (map names to
those VM IPs) → §4 passwordless SSH → §5 clone + `make mpi` → §6 smoke test → §8
run. Pick one VM as `master` (it runs `mpirun`, writes frames, makes the video).

**Gotchas specific to phone hotspot + bridged Wi-Fi:**
- **Client (AP) isolation:** some hotspots block client-to-client traffic, so the
  ping in step 6 fails. Disable "client isolation" in the phone's hotspot
  settings if present; iPhone Personal Hotspot and most Android hotspots allow it,
  but verify. If you can't disable it, use a normal Wi-Fi router instead.
- **Bridging over Wi-Fi:** some Wi-Fi adapters resist bridging and the VM gets no
  IP. Set Promiscuous Mode = Allow All (step 3); if it still fails, connect the
  PCs to a router by **Ethernet** and bridge to the wired adapter.
- **DHCP IPs change on reboot:** re-check IPs and update `/etc/hosts` each session,
  or reserve static IPs on the phone/router.
- All hosts are x86_64 → all VMs x86_64 → **same architecture ✓** (don't try to add
  an Apple-Silicon Mac to this cluster — see §0).

---

## 2. Install the toolchain on EVERY machine

**Ubuntu / WSL2 / Linux VM:**
```bash
sudo apt update
sudo apt install -y build-essential openmpi-bin libopenmpi-dev \
                    openssh-server ffmpeg python3-pip git
pip3 install numpy matplotlib
```

**macOS (if your cluster is all-Mac, same arch):**
```bash
brew install open-mpi libomp ffmpeg python git
pip3 install numpy matplotlib
# enable incoming SSH: System Settings → General → Sharing → Remote Login = ON
```

**Verify the versions match on all nodes** (this bites people):
```bash
mpirun --version        # e.g. "Open MPI 4.1.x" — must be identical everywhere
mpic++ --version
```
If versions differ, install the same one everywhere (on Ubuntu they match if all
nodes are the same Ubuntu release).

---

## 3. Put the machines on one network

1. Connect every laptop to the **same Wi-Fi / LAN** (or the same VM subnet).
2. Find each machine's IP:
   - Linux: `hostname -I`
   - macOS: `ipconfig getifaddr en0`
3. Give them friendly names. On **every** machine, append to `/etc/hosts`
   (`sudo nano /etc/hosts`) the **same** lines — use your real IPs:
   ```
   192.168.1.10  master
   192.168.1.11  node1
   192.168.1.12  node2
   192.168.1.13  node3
   ```
4. Test from the master: `ping -c1 node1` (repeat for each). All must reply.

---

## 4. Passwordless SSH (launch machine → all nodes)

MPI starts remote processes over SSH, so the **master** must SSH into every node
(including itself) without a password.

On the **master** only:
```bash
ssh-keygen -t ed25519        # press Enter at every prompt (no passphrase)
ssh-copy-id <user>@master    # yes, the master too
ssh-copy-id <user>@node1
ssh-copy-id <user>@node2
ssh-copy-id <user>@node3
```
Then verify each returns the hostname with **no password prompt**:
```bash
ssh node1 hostname
```
(If `ssh-copy-id` is missing on macOS: `brew install ssh-copy-id`, or append your
`~/.ssh/id_ed25519.pub` to the node's `~/.ssh/authorized_keys` by hand.)

---

## 5. Get the code and build on EVERY machine (same path!)

Open MPI launches `./raytracer_mpi` on each node, so the binary must exist **at
the same path** on all of them. Two clean ways:

**Option A — identical clone path (simplest).** Use the **same username** on all
machines and clone to the same place:
```bash
# on EVERY machine
git clone <your-repo-url> ~/raytracer    # or copy the folder with: rsync -av ./ user@node1:~/raytracer
cd ~/raytracer
make mpi                                  # build the binary natively on each node
```

**Option B — one NFS share (build once).** Export a folder from the master and
mount it at the **same path** on every node; build once there. Cleaner for big
clusters; more setup. (Search "Ubuntu NFS server" — out of scope here.)

> The binary must be built on the **same architecture** it runs on. With Option A
> you build on each node, so this is automatic. With NFS + mixed arch, it breaks —
> keep the cluster homogeneous (see §0).

Create the **hostfile** in the project dir on the **master** (`slots` = CPU cores
on that machine; check with `nproc`):
```
master slots=4
node1  slots=4
node2  slots=4
node3  slots=4
```
Save it as `hostfile` (there's a `hostfile.example` to copy).

---

## 6. Smoke-test the cluster

From the master, in the project dir:
```bash
mpirun --hostfile hostfile -np 16 hostname
```
You should see all four hostnames printed (4 times each). **If this works, your
cluster is live.** If it hangs or errors, jump to §9 Troubleshooting.

Or use the helper:
```bash
tools/cluster_check.sh hostfile
```

---

## 7. Cloud VMs (the reliable path)

If laptops are mixed, rent four identical small VMs (e.g. AWS `t3.large`, GCP
`e2-standard-2`, or any provider) — **same image, same region**:

1. Launch 4 VMs from the **same Ubuntu image**; note their private IPs.
2. Open the security group/firewall **between the VMs** (all TCP within the
   subnet) so MPI can talk.
3. Do §2 (install), §3 (`/etc/hosts` with private IPs), §4 (SSH keys), §5 (clone
   + `make mpi`) on each.
4. §6 smoke test, then §8 run.

Cloud VMs are identical by construction, so the arch/OS/version problems in §0
disappear. Tear them down after the demo.

---

## 8. Run the renderer across the cluster

From the master, in the project dir. **Frames are written to `frames/` on the
master**, so assemble the video there afterwards.

**Flat MPI** — one process per core, dynamic scheduling (4 machines × 4 cores =
16 procs; rank 0 coordinates, 15 render):
```bash
mpirun --hostfile hostfile -np 16 ./raytracer_mpi \
    --width 960 --height 540 --spp 32 --depth 8 --shadow-samples 8 \
    --frames 96 --tile 32 --schedule dynamic --out frames

tools/assemble_video.sh frames output/render.mp4 24
```

**Hybrid MPI + OpenMP** — one MPI process *per machine*, each threading across
its cores (`--bind-to none` is required so OpenMP isn't pinned to one core):
```bash
mpirun --hostfile hostfile --map-by ppr:1:node --bind-to none -np 4 \
    ./raytracer_mpi --threads 4 \
    --width 960 --height 540 --spp 32 --depth 8 --shadow-samples 8 \
    --frames 96 --tile 32 --out frames
```

**Non-blocking prefetch** (its benefit is largest over a real network):
```bash
mpirun --hostfile hostfile -np 16 ./raytracer_mpi --prefetch \
    --width 960 --height 540 --spp 32 --frames 96 --out frames
```

---

## 9. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `Permission denied (publickey)` | SSH keys not copied, or `sshd` off on the node. Re-run §4; `sudo systemctl start ssh`. |
| Hangs immediately, no output | Firewall blocking MPI ports, or a hostname in `hostfile` isn't pingable. Open intra-cluster TCP; fix `/etc/hosts`. |
| `error while loading shared libraries: libmpi...` | MPI lib path differs on the node. Use the same install everywhere; or `mpirun --prefix /usr/lib/x86_64-linux-gnu/openmpi ...`. |
| "executable not found" on a node | Binary not at the same path. Use the identical clone path (§5A) or NFS (§5B). |
| Version-mismatch / handshake errors | `mpirun --version` differs across nodes. Install the same Open MPI on all. |
| Runs, but OpenMP uses 1 thread | Add `--bind-to none` (Open MPI pins each rank to one core by default). |
| `There are not enough slots` | Increase `slots=` in the hostfile, or add `--oversubscribe`. |
| Mixed-architecture error | You can't mix arm64 and x86_64 in one job. See §0 — use a subset or cloud VMs. |
| Worker machines have no `frames/` | **Expected** — only the master writes output. Not a bug. |

---

## 10. Single-machine fallback (always works)

If the cluster won't cooperate on demo day, run everything on one laptop — it
still uses real MPI processes, so the parallel story is intact (you just lose the
"across machines" networking):
```bash
make mpi
mpirun -np 8 ./raytracer_mpi --width 960 --height 540 --spp 32 --depth 8 \
       --shadow-samples 8 --frames 96 --tile 32 --schedule dynamic --out frames
tools/assemble_video.sh frames output/render.mp4 24
```
Have this ready as a backup regardless.

---

## 11. Live demo script (~8 minutes)

A suggested running order. Do a full dry-run the day before.

1. **Show the cluster is real** (~30s)
   ```bash
   mpirun --hostfile hostfile -np 4 hostname
   ```
   → four different machine names appear. "These are our four laptops."

2. **Prove correctness** (~1 min) — the distributed result equals the sequential
   one, exactly:
   ```bash
   ./raytracer_seq            --width 320 --height 240 --frames 4 --out frames_seq
   mpirun --hostfile hostfile -np 8 ./raytracer_mpi \
                               --width 320 --height 240 --frames 4 --out frames_mpi
   python3 tools/compare_frames.py frames_seq frames_mpi
   ```
   → **"PASS: byte-identical (MSE=0)."** Say *why*: deterministic pixel-seeded
   sampling, so it doesn't matter which machine renders which tile.

3. **Show speedup as you add machines** (~3 min) — make three hostfiles
   (`hosts1`, `hosts2`, `hosts4` with 1, 2, 4 machines) and run the same workload:
   ```bash
   for hf in hosts1 hosts2 hosts4; do
     echo "=== $hf ==="
     mpirun --hostfile $hf -np $(awk '{s+=$2}END{print s}' FS='slots=' $hf) \
       ./raytracer_mpi --width 800 --height 600 --spp 32 --depth 8 \
       --shadow-samples 8 --frames 48 --tile 32 --bench cluster.csv
   done
   ```
   → makespan drops as machines are added. "More machines, less time."

4. **Render the full animation across all four** (~1–2 min while it runs)
   ```bash
   mpirun --hostfile hostfile -np 16 ./raytracer_mpi \
       --width 960 --height 540 --spp 32 --depth 8 --shadow-samples 8 \
       --frames 96 --tile 32 --schedule dynamic --out frames
   tools/assemble_video.sh frames output/render.mp4 24
   ```

5. **Play the video** `output/render.mp4` — orbiting camera, soft moving shadows,
   glass + mirror + the triangle pyramid.

6. **Show the charts** (`docs/results/chart_*.png` or freshly generated) — speedup,
   granularity, dynamic-vs-static load balance, and the MPI×OpenMP hybrid split.
   Tie each back to a sentence (see `docs/PROJECT.md §6`).

7. **One slide on the design** — why it's correct (determinism), why dynamic
   scheduling wins (load balance), and the two-level MPI+OpenMP parallelism.

**Talking points / likely questions:** see `docs/PROJECT.md` §4 (parallel model),
§5 (correctness), §10 (honest limitations). Each member can speak to their
subsystem from their journal in `docs/members/`.

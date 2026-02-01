# MediaTek MT7927 Driver Build Requirements

This document details the minimum set of files required to compile the MediaTek proprietary driver for MT7927 PCIe support, based on analysis of the `reference_mtk_modules/connectivity/wlan/core/gen4m/` codebase.

## Summary

| Category | File Count |
|----------|------------|
| Chip-specific sources | 7 |
| PCIe HIF sources | 7 |
| Common sources | 7 |
| NIC layer sources | 18 |
| Management sources | 68 |
| OS abstraction sources | 33 |
| **Total source files** | **~140** |
| Header files | ~240 |
| Register definition headers | 37+ per chip |

**Important:** MT7927 (Device ID 0x7927) uses the MT6639 driver path internally.

---

## Build System Files

### Main Build Files

| File | Purpose |
|------|---------|
| `Kbuild` | Main build dispatcher |
| `Kbuild.main` | Master build logic (3495 lines) |
| `Kbuild.6989_6639` | MT6639 on MT6989 platform |
| `Kbuild.6985_6639` | MT6639 on MT6985 platform |

### Configuration Files

| File | Purpose |
|------|---------|
| `configs/MT6639/defconfig` | Primary MT6639 configuration |
| `configs/MT6639/mobile/defconfig` | Mobile platform config |
| `configs/MT6639/ce/defconfig` | CE (Consumer Electronics) config |
| `configs/MT7925/defconfig` | MT7925 configuration (alternative) |

---

## Chip-Specific Sources

### MT6639 Chip Files (Primary for MT7927)

```
chips/mt6639/mt6639.c                  # Main chip initialization
chips/mt6639/dbg_mt6639.c              # Debug/diagnostic functions
chips/mt6639/hal_dmashdl_mt6639.c      # DMA scheduler HAL
chips/mt6639/hal_wfsys_reset_mt6639.c  # WiFi subsystem reset
```

### SOC3.0 Common Files (Required)

```
chips/soc3_0/soc3_0.c                  # SOC3.0 architecture support
chips/soc3_0/dbg_soc3_0.c              # SOC3.0 debugging
chips/soc3_0/hal_dmashdl_soc3_0.c      # SOC3.0 DMA scheduler
```

### MT7925 Chip Files (Alternative)

```
chips/mt7925/mt7925.c
chips/mt7925/dbg_mt7925.c
chips/mt7925/hal_dmashdl_mt7925.c
chips/mt7925/hal_wfsys_reset_mt7925.c
```

---

## PCIe HIF (Host Interface) Sources

### PCIe-Specific Files

| File | Purpose |
|------|---------|
| `os/linux/hif/pcie/pcie.c` | Core PCIe device driver |
| `os/linux/hif/pcie/pcie_msi.c` | PCIe MSI/MSIX support |

### HIF Common Layer (Mandatory)

| File | Purpose |
|------|---------|
| `os/linux/hif/common/hal_pdma.c` | PDMA HAL layer |
| `os/linux/hif/common/kal_pdma.c` | PDMA kernel abstraction |
| `os/linux/hif/common/dbg_pdma.c` | PDMA debugging |
| `os/linux/hif/common/hif_mem.c` | Memory management |
| `os/linux/hif/common/sw_emi_ring.c` | EMI ring buffer management |

### Optional HIF Common Files

| File | Condition |
|------|-----------|
| `os/linux/hif/common/hal_offload.c` | Host offload enabled |
| `os/linux/hif/common/hal_mbu.c` | Multi-buffer unit enabled |
| `os/linux/hif/common/hal_wed.c` | WED support enabled |
| `os/linux/hif/common/sw_wfdma.c` | WFDMA DMA abstraction |

---

## Common/Core Sources

### Essential Base Files (7 files)

```
common/dump.c           # Memory/state dumping
common/wlan_lib.c       # Core WLAN library functions
common/wlan_oid.c       # OID/command handling
common/wlan_bow.c       # Band-of-Wireless/BT-over-WiFi
common/debug.c          # Debug utilities
common/wlan_he.c        # 802.11ax HE support
common/wlan_p2p.c       # P2P functionality
```

---

## NIC (Network Interface Card) Layer

### Core NIC Files (18 files)

```
nic/nic.c               # Main NIC device
nic/nic_tx.c            # TX path
nic/nic_rx.c            # RX path
nic/nic_pwr_mgt.c       # Power management
nic/nic_rate.c          # Rate control
nic/cmd_buf.c           # Command buffer management
nic/que_mgt.c           # Queue management
nic/nic_cmd_event.c     # Command/event handling
nic/nic_uni_cmd_event.c # Unified command support
nic/radiotap.c          # Radiotap header handling
nic/p2p_nic.c           # P2P NIC support
nic/nic_ext_cmd_event.c # Extended commands
nic/nic_rxd_v1.c        # RX descriptor v1
nic/nic_rxd_v2.c        # RX descriptor v2
nic/nic_rxd_v3.c        # RX descriptor v3
nic/nic_txd_v1.c        # TX descriptor v1
nic/nic_txd_v2.c        # TX descriptor v2
nic/nic_txd_v3.c        # TX descriptor v3
```

---

## Management Layer

### Core Management Files (68 files)

#### State Machines
```
mgmt/ais_fsm.c          # AIS (AP Infrastructure Station) FSM
mgmt/aaa_fsm.c          # AAA (Authentication) FSM
mgmt/saa_fsm.c          # SAA (Station Authentication) FSM
mgmt/scan_fsm.c         # Scan FSM
mgmt/roaming_fsm.c      # Roaming FSM
```

#### Association & Security
```
mgmt/assoc.c            # Association handling
mgmt/auth.c             # Authentication
mgmt/privacy.c          # Privacy/encryption
mgmt/rsn.c              # RSN/WPA handling
mgmt/wapi.c             # WAPI support
mgmt/fils.c             # FILS authentication
```

#### BSS & Channel Management
```
mgmt/bss.c              # BSS management
mgmt/cnm.c              # Channel management
mgmt/cnm_mem.c          # CNM memory management
mgmt/cnm_timer.c        # CNM timer management
```

#### Scanning
```
mgmt/scan.c             # Scan operations
mgmt/scan_cache.c       # Scan result caching
```

#### Regulatory
```
mgmt/rlm.c              # Radio Link Management
mgmt/rlm_domain.c       # Regulatory domain
mgmt/rlm_obss.c         # OBSS handling
mgmt/reg_rule.c         # Regulatory rules
```

#### P2P (12 files)
```
mgmt/p2p_assoc.c
mgmt/p2p_bss.c
mgmt/p2p_dev_fsm.c
mgmt/p2p_dev_state.c
mgmt/p2p_fsm.c
mgmt/p2p_func.c
mgmt/p2p_ie.c
mgmt/p2p_rlm.c
mgmt/p2p_rlm_obss.c
mgmt/p2p_role_fsm.c
mgmt/p2p_role_state.c
mgmt/p2p_scan.c
```

#### QoS & Advanced Features
```
mgmt/mib.c              # MIB management
mgmt/wmm.c              # WMM support
mgmt/qosmap.c           # QoS mapping
mgmt/tdls.c             # TDLS support
mgmt/wnm.c              # WNM support
mgmt/hs20.c             # Hotspot 2.0
```

#### Target Wake Time (TWT)
```
mgmt/twt.c
mgmt/twt_req_fsm.c
mgmt/twt_planner.c
```

#### 802.11ax/be Support
```
mgmt/he_rlm.c           # HE (802.11ax) RLM
mgmt/he_ie.c            # HE IE handling
mgmt/eht_rlm.c          # EHT (802.11be) RLM
```

#### Multi-Link Operations
```
mgmt/mlo.c              # MLO support
mgmt/t2lm.c             # TID-to-Link Mapping
```

#### Other Management Files
```
mgmt/stats.c            # Statistics
mgmt/mddp.c             # MDDP support
mgmt/thrm.c             # Thermal management
mgmt/aps.c              # APS support
mgmt/ap_selection.c     # AP selection
mgmt/mscs.c             # MSCS support
mgmt/rtt.c              # RTT/FTM
mgmt/mlr.c              # MLR support
mgmt/arp_mon.c          # ARP monitoring
mgmt/ccm.c              # CCM crypto
mgmt/gcm.c              # GCM crypto
mgmt/ie_sort.c          # IE sorting
mgmt/wlan_ring.c        # Ring buffer management
mgmt/epcs.c             # EPCS support
mgmt/hem_mbox.c         # HEM mailbox
mgmt/rate.c             # Rate handling
mgmt/rrm.c              # RRM support
```

---

## OS Abstraction Layer

### Core OS Integration Files (33 files)

#### Module & Initialization
```
os/linux/gl_init.c              # Module initialization
os/linux/gl_kal.c               # Kernel abstraction layer
os/linux/platform.c             # Platform abstraction
```

#### cfg80211 Integration
```
os/linux/gl_cfg80211.c          # cfg80211 integration
```

#### Memory & Reset
```
os/linux/gl_emi.c               # EMI memory management
os/linux/gl_rst.c               # Reset/recovery
```

#### Wireless Extensions
```
os/linux/gl_wext.c              # Wireless extensions
os/linux/gl_wext_priv.c         # Private wireless extensions
```

#### Interfaces
```
os/linux/gl_bow.c               # BT-over-WiFi
os/linux/gl_proc.c              # Procfs interface
os/linux/gl_vendor.c            # Vendor commands
```

#### Configuration & Testing
```
os/linux/gl_custom.c            # Custom configurations
os/linux/gl_ate_agent.c         # ATE testing
os/linux/gl_qa_agent.c          # QA testing
```

#### Miscellaneous
```
os/linux/gl_hook_api.c          # Hook APIs
os/linux/gl_csi.c               # CSI collection
os/linux/gl_sa_log.c            # Standalone logging
os/linux/gl_ics.c               # ICS support
os/linux/gl_reg_rule.c          # Regulatory rules
os/linux/gl_cmd_validate.c      # Command validation
os/linux/gl_met_log.c           # MET logging
os/linux/gl_sys_lock.c          # System locking
```

#### Optional Files
```
os/linux/gl_fw_log.c            # Firmware logging
os/linux/gl_coredump.c          # Coredump support
os/linux/gl_fw_dev.c            # FW dev interface
os/linux/gl_mbrain.c            # MBRAIN support
```

#### NAN Support
```
os/linux/gl_nan.c               # NAN support
os/linux/gl_vendor_nan.c        # NAN vendor commands
os/linux/gl_vendor_ndp.c        # NDP support
```

#### P2P OS Layer (4 files)
```
os/linux/gl_p2p.c
os/linux/gl_p2p_cfg80211.c
os/linux/gl_p2p_init.c
os/linux/gl_p2p_kal.c
```

---

## Header Files

### Core Headers

```
include/precomp.h               # Precompiled headers
include/typedef.h               # Type definitions
include/config.h                # Configuration
include/wlan_lib.h              # Library definitions
include/wlan_oid.h              # OID definitions
include/debug.h                 # Debug utilities
include/hif_cmm.h               # HIF common
include/CFG_Wifi_File.h         # Configuration file
include/link.h                  # Link management
include/link_drv.h              # Link driver
include/queue.h                 # Queue utilities
include/bitmap.h                # Bitmap utilities
```

### NIC Headers
```
include/nic/*.h                 # NIC internal headers
include/nic_uni_cmd_event.h     # Unified commands
```

### Management Headers
```
include/mgmt/*.h                # Management headers
```

### Chip API Headers

```
include/chips/mt6639.h
include/chips/dbg_mt6639.h
include/chips/hal_dmashdl_mt6639.h
include/chips/hal_wfsys_reset_mt6639.h

include/chips/mt7925.h
include/chips/dbg_mt7925.h
include/chips/hal_dmashdl_mt7925.h
include/chips/hal_wfsys_reset_mt7925.h
```

### Register Definition Headers (CODA)

Each chip requires ~37 CODA register definition files:

#### MT6639 Registers (`include/chips/coda/mt6639/`)
```
wf_wfdma_host_dma0.h
wf_hif_dmashdl_top.h
conn_host_csr_top.h
conn_cfg.h
conn_rgu_on.h
pcie_mac_ireg.h
wf_cr_sw_def.h
wf_ple_top.h
wf_pse_top.h
wf_pp_top.h
mawd_reg.h
wf_umib_top.h
... (30+ additional files)
```

#### MT7925 Registers (`include/chips/coda/mt7925/`)
```
(Similar structure to MT6639, ~37 files)
```

#### SOC3.0 Registers (`include/chips/coda/soc3_0/`)
```
(SOC3.0 architecture register definitions)
```

### HIF Headers
```
os/linux/hif/pcie/include/hif.h     # PCIe HIF header
os/linux/hif/common/include/*.h     # Common HIF headers
```

### ConnFEM Headers
```
include/chips/connfem/connfem_api.h # ConnFEM abstraction
```

---

## External Module Dependencies

### Required: conninfra

The Connectivity Infrastructure module is required for MT7927 operation.

**Location:** `/vendor/mediatek/kernel_modules/connectivity/conninfra/`

**Required headers from:**
```
conninfra/include/
conninfra/platform/include/
conninfra/base/include/
conninfra/debug_utility/
conninfra/conn_drv/connv2/debug_utility/
```

**Provides:**
- Subsystem initialization
- Power management
- Debug utilities
- Platform abstraction

### Optional: connfem

The Connectivity Front-End Module for RF tuning.

**Location:** `/vendor/mediatek/kernel_modules/connectivity/connfem/`

**Headers:** `connfem/include/`

**Used for:**
- RF front-end control
- Antenna tuning
- Required for MT6653

### Optional: WMT (Wireless Module Transporter)

**Condition:** `MTK_ANDROID_WMT=y`

**Used for:**
- WLAN/BT/GPS subsystem management
- Firmware download coordination
- Power sequencing

### Optional: EMI (Extended Memory Interface)

**Condition:** `CONFIG_MTK_WIFI_FW_LOG_EMI=y`

**Used for:**
- Firmware logging
- Ring buffer shared memory

---

## Compilation Flags

### Essential Flags for MT6639/MT7927 PCIe

```makefile
# Chip identification
-DMT6639

# HIF configuration
-DCONFIG_MTK_COMBO_WIFI_HIF=pcie
-DCONFIG_MTK_WIFI_PCIE_MSI_SUPPORT
-DCONFIG_MTK_WIFI_PCIE_MSI_MASK_BY_MMIO_WRITE

# Architecture
-DCONFIG_MTK_WIFI_CONNAC3X
-DCONFIG_MTK_WIFI_CONNV3_SUPPORT

# WiFi standards
-DCONFIG_MTK_WIFI_11AX_SUPPORT
-DCONFIG_MTK_WIFI_11BE_SUPPORT
-DCONFIG_MTK_WIFI_11BE_MLO_SUPPORT

# Features
-DCONFIG_MTK_WIFI_UNIFIED_COMMND_SUPPORT
-DCFG_SUPPORT_GLOBAL_AAD_NONCE
-DCFG_SUPPORT_BW320

# EMI ring support
-DCFG_MTK_WIFI_SW_EMI_RING
-DCFG_MTK_WIFI_FW_LOG_EMI
-DCFG_MTK_WIFI_MET_LOG_EMI

# Miscellaneous
-DCFG_USB_RX_PADDING_CSO_LEN=12
-DCFG_WIFI_TX_FIXED_RATE_NO_VTA=1
-DCFG_WIFI_SW_WTBL_SEARCH_FAIL=0
```

---

## WPA Supplicant Cryptography (Optional)

If WPA supplicant integration is needed:

### Core Files
```
wpa_supp/FourWayHandShake.c
wpa_supp/src/utils/common.c
```

### Crypto Files
```
wpa_supp/src/crypto/sha1.c
wpa_supp/src/crypto/sha1-internal.c
wpa_supp/src/crypto/sha1-prf.c
wpa_supp/src/crypto/sha256.c
wpa_supp/src/crypto/sha256-prf.c
wpa_supp/src/crypto/sha256-internal.c
wpa_supp/src/crypto/sha256-kdf.c
wpa_supp/src/crypto/sha384.c
wpa_supp/src/crypto/sha384-internal.c
wpa_supp/src/crypto/sha384-prf.c
wpa_supp/src/crypto/sha384-kdf.c
wpa_supp/src/crypto/sha512-internal.c
wpa_supp/src/crypto/aes-wrap.c
wpa_supp/src/crypto/aes-internal.c
wpa_supp/src/crypto/aes-unwrap.c
wpa_supp/src/crypto/aes-internal-enc.c
wpa_supp/src/crypto/aes-internal-dec.c
wpa_supp/src/crypto/aes-siv.c
wpa_supp/src/crypto/aes-ctr.c
wpa_supp/src/crypto/aes-omac1.c
wpa_supp/src/crypto/pbkdf2-sha256.c
```

---

## Directory Structure Summary

```
gen4m/
├── Kbuild                              # Build dispatcher
├── Kbuild.main                         # Master build (3495 lines)
├── Kbuild.6989_6639                    # MT6639 variant
├── Kbuild.6985_6639                    # MT6639 variant
│
├── configs/
│   ├── MT6639/defconfig                # MT6639 defaults
│   └── MT7925/defconfig                # MT7925 defaults
│
├── chips/
│   ├── mt6639/                         # 4 source files
│   ├── mt7925/                         # 4 source files (alternative)
│   └── soc3_0/                         # 3 common SOC3.0 files
│
├── os/linux/
│   ├── hif/
│   │   ├── pcie/                       # 2 PCIe files + headers
│   │   └── common/                     # 5-9 common HIF files
│   ├── gl_*.c                          # 33 OS abstraction files
│   └── include/                        # OS headers
│
├── nic/                                # 18 NIC layer files
├── common/                             # 7 core files
├── mgmt/                               # 68 management files
│
├── include/
│   ├── chips/
│   │   ├── coda/
│   │   │   ├── mt6639/                 # 37+ register headers
│   │   │   ├── mt7925/                 # 37+ register headers
│   │   │   └── soc3_0/                 # SOC3.0 registers
│   │   ├── mt6639.h
│   │   └── mt7925.h
│   ├── mgmt/                           # Management headers
│   ├── nic/                            # NIC headers
│   └── *.h                             # Core headers
│
└── wpa_supp/                           # Optional crypto/WPA files
```

---

## Build Complexity Metrics

| Metric | Value |
|--------|-------|
| Total source files | ~140 |
| Header files | ~240 |
| Register definition files | 37+ per chip |
| Build configuration options | 200+ |
| Lines in Kbuild.main | 3495 |
| Supported chips | 8+ |
| Supported HIF types | 5 (SDIO, PCIe, AXI, USB, none) |

---

## Practical Considerations

### Why the Driver Cannot Be Easily Reduced

1. **Tight Coupling**: The 68 management files are interdependent
2. **Architecture Requirements**: CONNAC3x requires SOC3.0 support
3. **External Dependencies**: `conninfra` module is mandatory for initialization
4. **Register Definitions**: Essential for any hardware access
5. **Feature Integration**: Even "optional" features are often compile-time enabled

### Minimum Viable Build

For a functional MT7927 PCIe driver, you need:

1. The entire `gen4m/` directory
2. The `conninfra` external module
3. Appropriate kernel headers (5.15+ recommended)
4. MediaTek platform headers (for Android builds)

### Alternative: Upstream mt76

For a simpler approach, consider the upstream Linux `mt76` driver in `reference_mt76_upstream/mt7925/`, which:
- Has fewer files (~20 source files)
- No external module dependencies
- Integrates with standard Linux wireless stack
- Actively maintained in mainline kernel

---

## References

- Source: `reference_mtk_modules/connectivity/wlan/core/gen4m/`
- Build system: `Kbuild.main` (3495 lines)
- Configuration: `configs/MT6639/defconfig`

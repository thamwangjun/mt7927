# MediaTek MT7927 Driver Kernel Compatibility

This document analyzes the Linux kernel version compatibility of the MediaTek proprietary driver for MT7927/MT7925 based on version checks found in the `reference_mtk_modules/` codebase.

## Summary

**Target Kernel Range:** Linux 5.10 - 6.6
**Primary Target:** Android GKI (Generic Kernel Image) kernels
**Recommended for Desktop:** Kernel 5.15 LTS or 6.1 LTS

---

## Kernel Version Checks

### Highest Version Checks (Kernel 6.x)

| Version | File | Purpose |
|---------|------|---------|
| 6.6.0 | `adaptor/wlan_page_pool/wifi_page_pool.c:20` | Page pool API changes |
| 6.4.0 | `adaptor/wmt_cdev_wifi.c:1028` | Character device changes |
| 6.4.0 | `os/linux/gl_cfg80211.c:5009` | cfg80211 API updates |
| 6.3.0 | `os/linux/gl_cfg80211.c:2100` | cfg80211 API updates |
| 6.2.0 | `os/linux/gl_p2p_cfg80211.c:3317` | P2P cfg80211 changes |
| 6.1.0 | `os/linux/gl_cfg80211.c` (multiple) | MLO/Wi-Fi 7 features |
| 6.0.0 | Multiple files | cfg80211 restructuring |

### Mid-Range Version Checks (Kernel 5.x)

| Version | File | Purpose |
|---------|------|---------|
| 5.12.0 | `os/linux/gl_cfg80211.c:7014` | cfg80211 updates |
| 5.10.0 | `os/linux/plat/mt6991/plat_priv.c:59` | Platform-specific code |
| 5.5.0 | `os/linux/gl_cfg80211.c:5369` | Scan API changes |
| 5.4.0 | `os/linux/plat/mt6991/plat_priv.c:8` | Platform initialization |
| 5.1.0 | `os/linux/gl_cfg80211.c:2108` | cfg80211 features |
| 5.0.0 | `os/linux/platform.c:69` | Platform changes |

### Legacy Version Checks (Kernel 4.x)

| Version | File | Purpose |
|---------|------|---------|
| 4.20.0 | `os/linux/gl_cfg80211.c:950` | Scan flags |
| 4.19.0 | `os/linux/gl_cfg80211.c:4597` | cfg80211 updates |
| 4.17.0 | `os/linux/platform.c:80` | Platform changes |
| 4.14.0 | `os/linux/gl_cfg80211.c:6944` | cfg80211 features |
| 4.12.0 | `os/linux/gl_cfg80211.c:4274` | RSN/security changes |
| 4.4.0 | `os/linux/gl_cfg80211.c:4204` | Scan API |
| 4.1.0 | `os/linux/gl_cfg80211.c:6872` | Channel flags |
| 4.0.0 | `os/linux/gl_cfg80211.c:241` | Basic cfg80211 |

### Minimum Supported (Kernel 3.x)

| Version | File | Purpose |
|---------|------|---------|
| 3.19.0 | `os/linux/gl_cfg80211.c:4883` | Regulatory |
| 3.18.0 | `os/linux/gl_cfg80211.c:5061` | Connect API |
| 3.16.0 | `os/linux/gl_cfg80211.c:641` | Basic features |
| 3.15.0 | `os/linux/gl_cfg80211.c:2042` | Scan features |
| 3.14.0 | `os/linux/gl_cfg80211.c:3160` | Basic cfg80211 |
| 3.13.0 | `os/linux/gl_cfg80211.c:6819` | Channel definitions |
| 3.12.0 | `os/linux/gl_cfg80211.c:4028` | TDLS support |
| 3.11.0 | `os/linux/platform.c:446` | Minimum for some features |

---

## Android GKI Support

The driver is primarily designed for Android Generic Kernel Image (GKI) builds.

### GKI Configuration

```c
// From build/connac3x/dtv_connac3/Kbuild.dtv
CONFIG_GKI_SUPPORT=y
PLATFORM_FLAGS += -DCFG_ENABLE_GKI_SUPPORT=1
```

### Android Version References

| Reference | Location | Android Version |
|-----------|----------|-----------------|
| "Android T" | `os/linux/include/gl_vendor.h:1227` | Android 13 |
| "Android U" | `os/linux/include/gl_vendor.h:1279` | Android 14 |

### Android to Kernel Version Mapping

| Android Version | GKI Kernel | Driver Support |
|-----------------|------------|----------------|
| Android 12 | 5.10 | ✓ Full |
| Android 13 | 5.15 | ✓ Full |
| Android 14 | 6.1 | ✓ Full (Primary Target) |
| Android 15 | 6.6 | ✓ Full |

---

## Platform-Specific Builds

The driver includes build configurations for various MediaTek SoCs:

### CONNAC3x Platforms (MT7925/MT7927)

| Build Path | SoC | WiFi Chip |
|------------|-----|-----------|
| `build/connac3x/6985_6639/` | MT6985 | MT6639 |
| `build/connac3x/6989_6639/` | MT6989 | MT6639 |
| `build/connac3x/6989_6653/` | MT6989 | MT6653 |
| `build/connac3x/6991_6653/` | MT6991 | MT6653 |
| `build/connac3x/eap_6639/` | EAP | MT6639 |
| `build/connac3x/eap_6653/` | EAP | MT6653 |

### CONNAC2x Platforms (Legacy)

| Build Path | SoC |
|------------|-----|
| `build/connac2x/6877/` | MT6877 |
| `build/connac2x/6891/` | MT6891 |
| `build/connac2x/6893/` | MT6893 |
| `build/connac2x/6897/` | MT6897 |

---

## Desktop Linux Recommendations

### Recommended Kernels

| Kernel | Status | Notes |
|--------|--------|-------|
| **6.1 LTS** | ✓ Recommended | Best compatibility, Android 14 GKI base |
| **5.15 LTS** | ✓ Recommended | Good compatibility, Android 13 GKI base |
| **6.6 LTS** | ✓ Supported | Latest features, Android 15 GKI base |
| 5.10 LTS | ✓ Supported | Older but stable |
| 6.8+ | ? Unknown | May require patches for newer API changes |

### Potential Issues on Desktop

1. **GKI Dependencies**: Some code paths assume Android GKI infrastructure
2. **Platform Drivers**: Expects MediaTek platform drivers (conninfra, connfem)
3. **Firmware Path**: May expect Android firmware locations
4. **Power Management**: Android-specific suspend/resume hooks

---

## cfg80211 Version Considerations

The driver uses `CFG80211_VERSION_CODE` extensively, which can differ from `LINUX_VERSION_CODE` when using backported wireless drivers.

### Key cfg80211 API Changes

| Kernel | API Change |
|--------|------------|
| 6.1+ | Multi-Link Operation (MLO) for Wi-Fi 7 |
| 6.0+ | Major cfg80211 restructuring |
| 5.12+ | New scan API |
| 4.12+ | Updated RSN/security handling |
| 4.0+ | Modern cfg80211 baseline |

### Backport Compatibility

The Makefile supports specifying a custom cfg80211 version:

```makefile
# From Makefile.x86
export CFG_CFG80211_VERSION ?= $(BACKPORTED_KERNEL_VERSION)
```

This allows building against backported wireless subsystems (e.g., from kernel.org backports project).

---

## Version Check Code Examples

### Kernel 6.1+ MLO Support

```c
// os/linux/gl_cfg80211.c:7440
#if (KERNEL_VERSION(6, 1, 0) <= CFG80211_VERSION_CODE)
    // Multi-Link Operation (MLO) code for Wi-Fi 7
#endif
```

### Kernel 6.6+ Page Pool

```c
// adaptor/wlan_page_pool/wifi_page_pool.c:20
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    // New page pool API
#endif
```

### Legacy Kernel Fallbacks

```c
// os/linux/gl_cfg80211.c:5061
#if KERNEL_VERSION(6, 4, 0) <= CFG80211_VERSION_CODE
    // Modern API
#elif KERNEL_VERSION(3, 18, 0) <= CFG80211_VERSION_CODE
    // Legacy API fallback
#endif
```

---

## Conclusion

The MediaTek proprietary driver for MT7927/MT7925 is designed for:

- **Primary Target**: Android 14/15 with kernel 6.1/6.6 (GKI)
- **Compatibility Range**: Kernel 3.11 to 6.6
- **Recommended for Desktop**: Kernel 5.15 LTS or 6.1 LTS

The extensive version checks indicate MediaTek maintains broad compatibility while optimizing for current Android releases. For reverse engineering or porting efforts, focusing on the kernel 6.1 code paths will provide the most relevant and actively maintained implementation.

# Contributing to MT7927 Linux Driver

## Welcome!

Thank you for your interest in contributing to the MT7927 Linux driver project! This guide will help you get started.

## Current Priority: Implement Firmware Loader 🎯

**ROOT CAUSE IDENTIFIED (2026-01-31)**: MT7927 ROM bootloader does NOT support mailbox command protocol!

The highest priority contribution is implementing the polling-based firmware loader. See [Implementation Needed](#implementation-needed) section below.

## Ways to Contribute

### 1. Implementation (High Priority)

**Implement polling-based firmware loader** based on zouyonghao reference:

See [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md) for complete implementation details.

**Quick test** (30 min - validate hypothesis):
```c
// File: src/mt7927_mcu.c
// Change one line to test if mailbox protocol is the blocker

// OLD (waits for mailbox response):
ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_CMD_PATCH_SEM_CONTROL,
                                &req, sizeof(req), true, &skb);

// NEW (don't wait for response):
ret = mt76_mcu_send_msg(&dev->mt76, MCU_CMD_PATCH_SEM_CONTROL,
                       &req, sizeof(req), false);  // Don't wait!
// Skip response parsing
```

**Full solution** (2-3 hours):
1. Create `src/mt7927_fw_load.c` based on `reference_zouyonghao_mt7927/mt7927_fw_load.c`
2. Implement:
   - Skip semaphore command
   - Never wait for mailbox responses (wait_resp = false)
   - Aggressive TX cleanup (before+after each chunk, force=true)
   - Polling delays (5-50ms for ROM processing)
   - Skip FW_START, manually set SW_INIT_DONE
   - MCU IDLE pre-check (poll 0x81021604 for 0x1D1E)
   - Status register polling (0x7c060204, 0x7c0600f0)

### 2. Testing and Validation

**Hardware testing**:
- Build and test modules on your MT7927 hardware
- Run diagnostic modules (`diag/mt7927_diag.ko`, etc.)
- Test in different system configurations
- Document any unexpected behavior

**Systematic testing**:
```bash
# 1. Clean build
make clean && make tests && make diag

# 2. Verify device state (reboot first for clean state)
sudo insmod diag/mt7927_diag.ko
sudo dmesg | tail -20
sudo rmmod mt7927_diag

# 3. Run test modules
sudo insmod tests/05_dma_impl/test_fw_load.ko
sudo dmesg | tail -60

# 4. Document results
# Include: kernel version, hardware details, full dmesg output
```

**What to report**:
- Success or failure
- Unexpected register values
- Different behavior from documented expectations
- Any kernel warnings or errors

### 3. Code Review

Review pull requests and changes:
- Check for coding style compliance
- Verify proper error handling
- Look for potential memory leaks
- Suggest improvements and optimizations
- Test changes on your hardware if possible

### 4. Documentation

Improve documentation:
- Fix typos and unclear explanations
- Add examples and use cases
- Clarify confusing sections
- Document undocumented features
- Add diagrams and illustrations

### 5. Bug Reports

Report issues you encounter:
- Use GitHub issues
- Include all required information (see below)
- Provide clear reproduction steps
- Attach relevant logs and outputs

## Development Workflow

### Setting Up Development Environment

```bash
# 1. Clone repository
git clone https://github.com/[your-username]/mt7927-linux-driver
cd mt7927-linux-driver

# 2. Install dependencies
# Debian/Ubuntu:
sudo apt install build-essential linux-headers-$(uname -r)

# Fedora:
sudo dnf install kernel-devel kernel-headers gcc make

# Arch Linux:
sudo pacman -S linux-headers base-devel

# 3. Build
make clean && make

# 4. Verify you have firmware
ls -la /lib/firmware/mediatek/mt7925/
```

### Making Changes

1. **Create a feature branch**:
   ```bash
   git checkout -b feature/polling-firmware-loader
   ```

2. **Make your changes**:
   - Follow Linux kernel coding style
   - Add comments for complex logic
   - Include SPDX license headers
   - Test thoroughly

3. **Build and test**:
   ```bash
   make clean && make
   sudo insmod [your-module].ko
   sudo dmesg | tail -40
   ```

4. **Commit with descriptive message**:
   ```bash
   git add [files]
   git commit -m "mcu: implement polling-based firmware loader

   MT7927 ROM bootloader doesn't support mailbox protocol.
   Implement polling-based approach with time delays instead
   of waiting for mailbox responses.

   Based on zouyonghao reference implementation."
   ```

5. **Push and create pull request**:
   ```bash
   git push origin feature/polling-firmware-loader
   # Create PR on GitHub
   ```

### Code Style Guidelines

Follow Linux kernel coding style:
- Indentation: tabs (8 spaces width)
- Line length: 80 characters preferred, 100 max
- Braces: K&R style
- Comments: `/* */` for multi-line, `//` for single-line acceptable in some contexts
- Function names: lowercase with underscores (`mt7927_load_firmware`)
- No trailing whitespace

**Example**:
```c
/* SPDX-License-Identifier: GPL-2.0 */
/* MT7927 firmware loader - polling-based (no mailbox) */

static int mt7927_load_patch(struct mt7927_dev *dev, const char *name)
{
	const struct firmware *fw;
	int ret;

	/* Request firmware from filesystem */
	ret = request_firmware(&fw, name, dev->dev);
	if (ret) {
		dev_err(dev->dev, "Failed to load firmware: %d\n", ret);
		return ret;
	}

	/* Send firmware without waiting for mailbox response */
	ret = mt7927_send_firmware_polling(dev, fw->data, fw->size);

	release_firmware(fw);
	return ret;
}
```

### Testing Requirements

**Before submitting code**:
1. Build without warnings: `make clean && make`
2. Test module loads without errors
3. Test module unloads cleanly
4. Check dmesg for kernel warnings
5. Verify no memory leaks (check dmesg after rmmod)
6. Test on fresh boot (known-good device state)

**Test module requirements**:
- Use BAR0 (2MB), NOT BAR2 (32KB read-only)
- Disable ASPM early in probe if doing DMA
- Check for chip error state (0xffffffff)
- Include safety checks and validation
- Clean up resources on error paths
- Document expected vs actual behavior

## Bug Report Requirements

Include all of the following in bug reports:

### 1. System Information
```bash
# Kernel version
uname -a

# Hardware details
lspci -vvv -s $(lspci -nn | grep 14c3:7927 | cut -d' ' -f1)

# Module version
git log -1 --oneline
```

### 2. Diagnostic Output
```bash
# Clean dmesg
sudo dmesg -C

# Run diagnostic
sudo insmod diag/mt7927_diag.ko
sudo dmesg > mt7927_diag.txt

# Include mt7927_diag.txt in report
```

### 3. Full Logs
- Complete dmesg from module load to error
- Any kernel oops or panic messages
- Output of all commands run

### 4. Reproduction Steps
1. What you did (exact commands)
2. What you expected to happen
3. What actually happened
4. How often it occurs (always, intermittent)

### 5. Additional Context
- Fresh boot or after previous tests?
- ASPM disabled or enabled?
- Any system configuration changes?
- Other modules loaded?

## Pull Request Guidelines

### PR Title
Format: `[component]: brief description`

Examples:
- `mcu: implement polling-based firmware loader`
- `dma: fix ring index calculation`
- `doc: add firmware loading architecture diagram`

### PR Description Template
```markdown
## Summary
Brief description of changes

## Motivation
Why is this change needed?

## Changes
- Detailed list of modifications
- Files added/changed/removed

## Testing
How was this tested? Results?

## Related Issues
Fixes #123
Related to #456

## Checklist
- [ ] Builds without warnings
- [ ] Tested on hardware
- [ ] Follows coding style
- [ ] Documentation updated
- [ ] Commit messages are clear
```

### Review Process
1. Automated checks run (build, style)
2. Maintainer review
3. Community feedback
4. Address review comments
5. Approval and merge

## Reference Material

### Essential Reading (In Order)
1. **[ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md)** - Root cause and solution
2. **[MT6639_ANALYSIS.md](MT6639_ANALYSIS.md)** - Architecture foundation
3. **[../DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md)** - Complete history (Phase 17!)
4. **[ROADMAP.md](ROADMAP.md)** - Current status and next steps

### Code References
1. **Primary**: `reference_zouyonghao_mt7927/` - Working implementation
2. **Secondary**: `reference_mtk_modules/connectivity/.../mt6639/` - Architectural parent
3. **Tertiary**: Linux kernel mt7925 driver - CONNAC3X sibling

### Technical Documentation
- [HARDWARE.md](HARDWARE.md) - Hardware specifications
- [mt7927_pci_documentation.md](mt7927_pci_documentation.md) - PCI layer
- [dma_mcu_documentation.md](dma_mcu_documentation.md) - DMA/MCU layer
- [headers_documentation.md](headers_documentation.md) - Register reference

## Communication

### GitHub Issues
- Bug reports
- Feature requests
- Questions and discussions
- Implementation proposals

### Pull Requests
- Code contributions
- Documentation improvements
- Test additions

### Linux Wireless Mailing List
After we have a working driver, upstream discussions will move to:
- [linux-wireless mailing list](http://vger.kernel.org/vger-lists.html#linux-wireless)

## Code of Conduct

### Be Respectful
- Treat all contributors with respect
- Welcome newcomers and help them get started
- Accept constructive criticism gracefully
- Focus on what's best for the project

### Be Collaborative
- Share knowledge and findings
- Help others understand complex code
- Document your work for future contributors
- Give credit where credit is due

### Be Professional
- Keep discussions technical and on-topic
- Avoid personal attacks or inflammatory language
- Assume good intentions
- Disagree professionally

## Recognition

Contributors will be recognized:
- In commit messages (`Co-authored-by:` tags)
- In CONTRIBUTORS file (when created)
- In upstream kernel submission (if merged)
- In project documentation

## Getting Help

If you need help contributing:

1. **Read the docs** - Start with [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md)
2. **Check existing issues** - Your question may already be answered
3. **Ask in GitHub issues** - Create a new issue with "Question:" prefix
4. **Study reference code** - zouyonghao, MT6639, MT7925 implementations
5. **Review development log** - [../DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md) has complete history

## License

All contributions will be licensed under GPL v2, same as the Linux kernel.

By contributing, you agree to:
- License your code under GPL v2
- Allow your code to be distributed and modified
- Ensure you have rights to contribute the code

## Thank You!

Your contributions help make WiFi 7 support available on Linux. Whether you're implementing features, testing on hardware, improving documentation, or reporting bugs - every contribution matters!

---

**Next Steps**:
1. Read [ZOUYONGHAO_ANALYSIS.md](ZOUYONGHAO_ANALYSIS.md) for root cause
2. Study `reference_zouyonghao_mt7927/mt7927_fw_load.c`
3. Implement polling-based firmware loader
4. Test and submit PR

Let's get MT7927 working! 🚀

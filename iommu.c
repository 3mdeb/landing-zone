/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <defs.h>
#include <boot.h>
#include <types.h>
#include <pci.h>
#include <iommu.h>
#include <iommu_defs.h>

static u64 *mmio_base;

char *device_table;
char *event_log;
iommu_command_t *command_buf;

static void print_char(char c)
{
	while ( !(inb(0x3f8 + 5) & 0x20) )
		;

	outb(0x3f8, c);
}

static void print(char * txt) {
	while (*txt != '\0') {
		if (*txt == '\n')
			print_char('\r');
		print_char(*txt++);
	}
}

static void print_p(const void * _p) {
	char tmp[sizeof(void*)*2 + 5] = "0x";
	int i;
	size_t p = (size_t)_p;

	for (i=0; i<sizeof(void*); i++) {
		if ((p & 0xf) >= 10)
			tmp[sizeof(void*)*2 + 1 - 2*i] = (p & 0xf) + 'a' - 10;
		else
			tmp[sizeof(void*)*2 + 1 - 2*i] = (p & 0xf) + '0';
		p >>= 4;
		if ((p & 0xf) >= 10)
			tmp[sizeof(void*)*2 - 2*i] = (p & 0xf) + 'a' - 10;
		else
			tmp[sizeof(void*)*2 - 2*i] = (p & 0xf) + '0';
		p >>= 4;
	}
	tmp[sizeof(void*)*2 + 2] = ':';
	tmp[sizeof(void*)*2 + 3] = ' ';
	tmp[sizeof(void*)*2 + 4] = '\0';
	print(tmp);
}

// __builtin_memcpy is too safe, it won't copy to a manually crafted pointer
static void __maybe_unused memcpy_unsafe(char *d, const char *s, u32 size)
{
	while (size--)
		*d++ = *s++;
}

u32 iommu_locate(void)
{
	u32 pci_cap_ptr;
	u32 next;

	/* read capabilities pointer */
	pci_read(0, IOMMU_PCI_BUS,
	         PCI_DEVFN(IOMMU_PCI_DEVICE, IOMMU_PCI_FUNCTION),
	         PCI_CAPABILITY_LIST,
	         4, &pci_cap_ptr);

	if (INVALID_CAP(pci_cap_ptr))
		return 0;

	pci_cap_ptr &= 0xFF;

	while (pci_cap_ptr != 0)
	{
		pci_read(0, IOMMU_PCI_BUS,
		         PCI_DEVFN(IOMMU_PCI_DEVICE, IOMMU_PCI_FUNCTION),
		         pci_cap_ptr,
		         4, &next);

		if (PCI_CAP_ID(next) == PCI_CAPABILITIES_POINTER_ID_DEV)
			break;

		pci_cap_ptr = PCI_CAP_PTR(next);
	}

	if (INVALID_CAP(pci_cap_ptr))
		return 0;

	// A - all tables outside SLB
	//device_table = (char *)&sl_header + 64 * 1024;
	//command_buf = (iommu_command_t*)((char *)&sl_header + 64 * 1024 + 2 * PAGE_SIZE);
	//event_log = (char *)&sl_header + 64 * 1024 + 3 * PAGE_SIZE;
	//memcpy_unsafe(device_table, old_device_table, 2 * PAGE_SIZE);

	// B - all tables inside SLB
	//device_table = &old_device_table[0];
	//event_log = &old_event_log[0];
	//command_buf = &old_command_buf[0];

	// C - Event Log outside SLB, other inside
	event_log = (char *)&sl_header + 64 * 1024;
	device_table = &old_device_table[0];
	command_buf = &old_command_buf[0];

	return pci_cap_ptr;
}

static void send_command(iommu_command_t cmd)
{
	u32 cmd_ptr = mmio_base[IOMMU_MMIO_COMMAND_BUF_TAIL] >> 4;
	command_buf[cmd_ptr++] = cmd;
	asm volatile("wbinvd; sfence" ::: "memory");
	mmio_base[IOMMU_MMIO_COMMAND_BUF_TAIL] = (cmd_ptr << 4) & 0xfff;
}

u32 iommu_load_device_table(u32 cap, volatile u64 *completed)
{
	u32 low, hi;
	iommu_command_t cmd = {0};
	void * ptr;

	pci_read(0, IOMMU_PCI_BUS,
	         PCI_DEVFN(IOMMU_PCI_DEVICE, IOMMU_PCI_FUNCTION),
	         IOMMU_CAP_BA_LOW(cap),
	         4, &low);

	/* IOMMU is enabled by AGESA */
	if ((low & IOMMU_CAP_BA_LOW_ENABLE) == 0)
		return 1;

	pci_read(0, IOMMU_PCI_BUS,
	         PCI_DEVFN(IOMMU_PCI_DEVICE, IOMMU_PCI_FUNCTION),
	         IOMMU_CAP_BA_HIGH(cap),
	         4, &hi);

	mmio_base = (u64 *)((u64)hi << 32 | (low & 0xffffc000));

	print("IOMMU MMIO Base Address = ");
	print_p(mmio_base);
	print("\n");

	ptr = (void *) mmio_base[IOMMU_MMIO_STATUS_REGISTER];
	print_p(ptr);
	print("IOMMU_MMIO_STATUS_REGISTER\n");

	/* disable IOMMU and all its features */
	mmio_base[IOMMU_MMIO_CONTROL_REGISTER] &= ~IOMMU_CR_ENABLE_ALL_MASK;
	barrier();

	/* address and size of Device Table (bits 8:0 = 0 -> 4KB; 1 -> 8KB ...) */
	mmio_base[IOMMU_MMIO_DEVICE_TABLE_BA] = (u64)device_table | 1;

	ptr = (void *) mmio_base[IOMMU_MMIO_DEVICE_TABLE_BA];
	print_p(ptr);
	print("IOMMU_MMIO_DEVICE_TABLE_BA\n");

	/* address and size of Command Buffer, reset head and tail registers */
	mmio_base[IOMMU_MMIO_COMMAND_BUF_BA] = (u64)command_buf | (0x8ULL << 56);
	mmio_base[IOMMU_MMIO_COMMAND_BUF_HEAD] = 0;
	mmio_base[IOMMU_MMIO_COMMAND_BUF_TAIL] = 0;

	ptr = (void *) mmio_base[IOMMU_MMIO_COMMAND_BUF_BA];
	print_p(ptr);
	print("IOMMU_MMIO_COMMAND_BUF_BA\n");

	/* address and size of Event Log, reset head and tail registers */
	mmio_base[IOMMU_MMIO_EVENT_LOG_BA] = (u64)event_log | (0x8ULL << 56);
	mmio_base[IOMMU_MMIO_EVENT_LOG_HEAD] = 0;
	mmio_base[IOMMU_MMIO_EVENT_LOG_TAIL] = 0;

	ptr = (void *) mmio_base[IOMMU_MMIO_EVENT_LOG_BA];
	print_p(ptr);
	print("IOMMU_MMIO_EVENT_LOG_BA\n");

	barrier();
	mmio_base[IOMMU_MMIO_CONTROL_REGISTER] |= IOMMU_CR_CmdBufEn | IOMMU_CR_EventLogEn;
	asm volatile("wbinvd; sfence" ::: "memory");

	mmio_base[IOMMU_MMIO_CONTROL_REGISTER] |= IOMMU_CR_IommuEn;

	ptr = (void *) mmio_base[IOMMU_MMIO_STATUS_REGISTER];
	print_p(ptr);
	print("IOMMU_MMIO_STATUS_REGISTER\n");

	if (mmio_base[IOMMU_MMIO_EXTENDED_FEATURE] & IOMMU_EF_IASup) {
		print("INVALIDATE_IOMMU_ALL\n");
		cmd.opcode = INVALIDATE_IOMMU_ALL;
		send_command(cmd);
	} /* TODO: else? */

	ptr = (void *) mmio_base[IOMMU_MMIO_EXTENDED_FEATURE];
	print_p(ptr);
	print("IOMMU_MMIO_EXTENDED_FEATURE\n");
	ptr = (void *) mmio_base[IOMMU_MMIO_STATUS_REGISTER];
	print_p(ptr);
	print("IOMMU_MMIO_STATUS_REGISTER\n");

	// Write to a variable inside SLB (does not work, but does not report error either)
	cmd.u0 = (u32)((u64)completed | 1);
	cmd.u1 = (u32)((u64)completed >> 32);

	cmd.opcode = COMPLETION_WAIT;
	cmd.u2 = 0x656e6f64;	/* "done" */
	send_command(cmd);

	ptr = (void *) mmio_base[IOMMU_MMIO_STATUS_REGISTER];
	print_p(ptr);
	print("IOMMU_MMIO_STATUS_REGISTER\n");

	return 0;
}

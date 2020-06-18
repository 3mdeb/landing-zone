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

#ifndef __IOMMU_H__
#define __IOMMU_H__

typedef struct __packed {
	u32 u0;
	union {
		u32 u1;
		struct { u32 padding:28; u32 opcode:4; };
	};
	u32 u2;
	u32 u3;
} iommu_command_t;

extern char old_device_table[];
extern char old_event_log[];
extern iommu_command_t old_command_buf[];

extern char *device_table;
extern char *event_log;
extern iommu_command_t *command_buf;

u32 iommu_locate(void);
u32 iommu_load_device_table(u32 cap, volatile u64 *completed);

#endif /* __IOMMU_H__ */

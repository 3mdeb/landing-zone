#include <defs.h>
#include <types.h>

#define GATE_P		(1 << 15)
#define GATE_DPL(x)	(((x) & 0x3) << 13)
#define GATE_SIZE_16	(0 << 11)
#define GATE_SIZE_32	(1 << 11)

#define IGATE_FLAGS (GATE_P | GATE_DPL(0) | GATE_SIZE_32 | (0x6 << 8))

struct intr_gate {
	u16 offset_0;
	u16 segsel;
	u16 flags;
	u16 offset_1;
#ifdef __x86_64__
	u32 offset_2;
	u32 reserved;
#endif
} __packed;

/* Even though the vecX symbols are interrupt entry points just treat them
   like data to more easily get the pointer values in C. Because IDT entries
   format splits the offset field up, one can't use the linker to resolve
   parts of a relocation on x86 ABI. An array of pointers is used to gather
   the symbols. The IDT is initialized at runtime when exception_init() is
   called. */
extern u8 vec0[], vec1[], vec2[], vec3[], vec4[], vec5[], vec6[], vec7[];
extern u8 vec8[], vec9[], vec10[], vec11[], vec12[], vec13[], vec14[], vec15[];
extern u8 vec16[], vec17[], vec18[], vec19[];

static const uintptr_t intr_entries[] = {
	(uintptr_t)vec0, (uintptr_t)vec1, (uintptr_t)vec2, (uintptr_t)vec3,
	(uintptr_t)vec4, (uintptr_t)vec5, (uintptr_t)vec6, (uintptr_t)vec7,
	(uintptr_t)vec8, (uintptr_t)vec9, (uintptr_t)vec10, (uintptr_t)vec11,
	(uintptr_t)vec12, (uintptr_t)vec13, (uintptr_t)vec14, (uintptr_t)vec15,
	(uintptr_t)vec16, (uintptr_t)vec17, (uintptr_t)vec18, (uintptr_t)vec19,
};

static struct intr_gate idt[ARRAY_SIZE(intr_entries)] __aligned(8);

static inline u16 get_cs(void)
{
	u16 segment;

	asm volatile (
		"mov	%%cs, %0\n"
		: "=r" (segment)
		:
		: "memory"
	);

	return segment;
}

struct lidtarg {
	u16 limit;
#ifdef __x86_64__
	u32 base;
#else
	uint64_t base;
#endif
} __packed;

static void load_idt(void *table, size_t sz)
{
	struct lidtarg lidtarg = {
		.limit = sz - 1,
		.base = (uintptr_t)table,
	};

	asm volatile (
		"lidt	%0"
		:
		: "m" (lidtarg)
		: "memory"
	);
}

void exception_init(void)
{
	int i;
	u16 segment;

	segment = get_cs();

	/* Initialize IDT. */
	for (i = 0; i < ARRAY_SIZE(idt); i++) {
		idt[i].offset_0 = intr_entries[i];
		idt[i].segsel = segment;
		idt[i].flags = IGATE_FLAGS;
		idt[i].offset_1 = intr_entries[i] >> 16;
#ifdef __x86_64__
		idt[i].offset_2 = intr_entries[i] >> 32;
#endif
	}

	load_idt(idt, sizeof(idt));
}

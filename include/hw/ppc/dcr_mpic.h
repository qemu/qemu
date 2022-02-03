#ifndef MM7705_MPIC_H
#define MM7705_MPIC_H

#define MAX_CPU_SUPPORTED	4

#define EXT_SOURCE_NUM		128
#define MAX_IPI_NUM			4
#define MAX_TIMER_NUM		4

enum {
	OUTPUT_NON_CRIT,
	OUTPUT_CRIT,
	OUTPUT_MCHECK,

	OUTPUT_IRQ_NUM,
};

typedef struct {
	// Vector/Priority register data
	uint32_t vector : 8;
	uint32_t priority : 4;
	uint32_t sense : 1;
	uint32_t polarity : 1;
	uint32_t activity : 1;
	uint32_t masked : 1;
	// Destination register data
	uint32_t destination : 4;
	// Internal data
	uint32_t pending : 1;
} irq_config_t;

typedef struct {
	/* private */
	DeviceState parent_obj;

	/* properties */
	CPUState *cpu;
	uint32_t baseaddr;

	/* public */
	bool pass_through_8259;

	irq_config_t irq[EXT_SOURCE_NUM + MAX_TIMER_NUM + MAX_IPI_NUM];

	uint32_t task_prio[MAX_CPU_SUPPORTED];

	uint32_t vitc_crit_border;
	uint32_t vitc_mcheck_border;

	// spurious vector
	uint32_t spv;

	irq_config_t *current_irqs[MAX_CPU_SUPPORTED][OUTPUT_IRQ_NUM];

	QemuMutex mutex;

	qemu_irq output_irq[OUTPUT_IRQ_NUM];
} MpicState;

#define TYPE_MPIC "mm7705"
#define MPIC(obj) \
    OBJECT_CHECK(MpicState, obj, TYPE_MPIC)

#endif
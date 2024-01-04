#ifndef _ASMARM64_SPINLOCK_H_
#define _ASMARM64_SPINLOCK_H_

struct spinlock {
	int v;
};

extern void spin_lock(struct spinlock *lock);
extern void spin_unlock(struct spinlock *lock);

#endif /* _ASMARM64_SPINLOCK_H_ */

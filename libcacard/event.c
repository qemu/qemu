/*
 * event queue implementation.
 *
 * This code is licensed under the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu-common.h"
#include "qemu-thread.h"

#include "vcard.h"
#include "vreader.h"
#include "vevent.h"

VEvent *
vevent_new(VEventType type, VReader *reader, VCard *card)
{
    VEvent *new_vevent;

    new_vevent = (VEvent *)g_malloc(sizeof(VEvent));
    new_vevent->next = NULL;
    new_vevent->type = type;
    new_vevent->reader = vreader_reference(reader);
    new_vevent->card = vcard_reference(card);

    return new_vevent;
}

void
vevent_delete(VEvent *vevent)
{
    if (vevent == NULL) {
        return;
    }
    vreader_free(vevent->reader);
    vcard_free(vevent->card);
    g_free(vevent);
}

/*
 * VEvent queue management
 */

static VEvent *vevent_queue_head;
static VEvent *vevent_queue_tail;
static QemuMutex vevent_queue_lock;
static QemuCond vevent_queue_condition;

void vevent_queue_init(void)
{
    qemu_mutex_init(&vevent_queue_lock);
    qemu_cond_init(&vevent_queue_condition);
    vevent_queue_head = vevent_queue_tail = NULL;
}

void
vevent_queue_vevent(VEvent *vevent)
{
    vevent->next = NULL;
    qemu_mutex_lock(&vevent_queue_lock);
    if (vevent_queue_head) {
        assert(vevent_queue_tail);
        vevent_queue_tail->next = vevent;
    } else {
        vevent_queue_head = vevent;
    }
    vevent_queue_tail = vevent;
    qemu_cond_signal(&vevent_queue_condition);
    qemu_mutex_unlock(&vevent_queue_lock);
}

/* must have lock */
static VEvent *
vevent_dequeue_vevent(void)
{
    VEvent *vevent = NULL;
    if (vevent_queue_head) {
        vevent = vevent_queue_head;
        vevent_queue_head = vevent->next;
        vevent->next = NULL;
    }
    return vevent;
}

VEvent *vevent_wait_next_vevent(void)
{
    VEvent *vevent;

    qemu_mutex_lock(&vevent_queue_lock);
    while ((vevent = vevent_dequeue_vevent()) == NULL) {
        qemu_cond_wait(&vevent_queue_condition, &vevent_queue_lock);
    }
    qemu_mutex_unlock(&vevent_queue_lock);
    return vevent;
}

VEvent *vevent_get_next_vevent(void)
{
    VEvent *vevent;

    qemu_mutex_lock(&vevent_queue_lock);
    vevent = vevent_dequeue_vevent();
    qemu_mutex_unlock(&vevent_queue_lock);
    return vevent;
}


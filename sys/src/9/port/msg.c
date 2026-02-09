#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "edf.h"
#include <trace.h>
#include "tos.h"
#include "ureg.h"

enum {
	MsgMagic = 0xdeadbeef,
	MsgHeaderSize = 3*sizeof(u32int),
};

#pragma pack on
typedef struct {
	u32int magic;
	u32int tag;
	u32int pid;
	char data[1];
} StdMessage;
#pragma pack off

Message*
newmessage(void *data, uintptr sz)
{
	Message *new;

	if(!(new = mallocz(sizeof(Message), 1)))
		error(Enomem);
	new->size = sz;
	new->next = nil;
	new->data = mallocz(sz, 1);
	if(!new->data){
		free(new);
		error(Enomem);
	}
	memset(new->data, 0, sz);
	memmove(new->data, data, sz);
	return new;
}

Message*
newstdmessage(int tag, uvlong pid, void *srcdata, uintptr sz)
{
	Message *new;
	StdMessage *stdmsg;

	if(!(new = mallocz(sizeof(Message), 1)))
		error(Enomem);

	new->size = sz + MsgHeaderSize;
	new->next = nil;
	new->data = mallocz(new->size, 1);
	if(!new->data){
		free(new);
		error(Enomem);
	}

	stdmsg = (void*)new->data;

	stdmsg->magic = MsgMagic;
	stdmsg->tag = tag;
	stdmsg->pid = (s32int)pid;
	memmove(&stdmsg->data[0], srcdata, sz);

	return new;
}

Message*
freemessage(Message *msg)
{
	Message *next;

	if(!msg)
		return nil;
	next = msg->next;
	free(msg->data);
	free(msg);
	return next;
}

void
flushmailbox(Mailbox *mbox)
{
	Message *mb;

	lock(&mbox->lock);
	for(mb = mbox->head; mb != nil; mb = freemessage(mb))
		;;
	mbox->head = nil;
	mbox->tail = nil;
	mbox->ctl = 0;
	mbox->msgin = 0;
	mbox->msgout = 0;
	unlock(&mbox->lock);
}

static int
add_message(Mailbox *mbox, Message *msg)
{
	if(mbox->head == nil){
		assert(mbox->tail == nil);
		mbox->head = msg;
		mbox->tail = msg;
	} else {
		mbox->tail->next = msg;
		mbox->tail = msg;
	}

	return 0;
}

static Message*
remove_message(Mailbox *mbox)
{
	Message *fetched;

	if(!(mbox->ctl & MSGENABLE))
		return nil;

	if(mbox->head == nil){
		assert(mbox->tail == nil);
		return nil;
	}

	fetched = mbox->head;
	mbox->head = fetched->next;
	if(fetched == mbox->tail){
		assert(mbox->tail->next == nil);
		mbox->tail = nil;
	}
	fetched->next = nil;

	return fetched;
}

uintptr
mailboxsz(Mailbox *mbox)
{
	uintptr sz = 0;
	Message *m;

	for(m = mbox->head; m != nil; m = m->next)
		sz++;

	return sz;
}

int
psendmsg(Proc *proc, Message *msg)
{
	assert(proc != nil);
	switch(proc->state){
	case Dead:
	case Moribund:
	case Broken:
		return -1;
	}

	lock(&proc->mbox.lock);

	// are messages enabled?
	if(!(proc->mbox.ctl & MSGENABLE)){
		// my question here is should send to a process that
		// isn't accepting messages be an error?
		// should it even signal to the sender that there's a
		// problem?
		unlock(&proc->mbox.lock);
		error(Egoaway);
	}

	// are user permissions good?
	if(!(proc->mbox.ctl & MSGALLUSERS)){
		// if you're hostowner you can send messages to anyone
		if(!iseve() && strcmp(up->user, proc->user) != 0) {
			unlock(&proc->mbox.lock);
			error(Eperm);
		}
	}

	add_message(&proc->mbox, msg);

	up->mbox.msgout++;
	proc->mbox.msgin++;

	unlock(&proc->mbox.lock);
	if(proc->state == Msgsleep)
		ready(proc);

	return 0;
}

uintptr
pwaitmsg(void)
{
	int waited = 0;
	uintptr sz;

	lock(&up->mbox.lock);
	if(!(up->mbox.ctl & MSGENABLE)){
		unlock(&up->mbox.lock);
		// there's arguments to throw errors if you wait when
		// messages are disabled. idk if I agree with that.
		// this would match with precvmsg, though.
		error(Egoaway);
		// return 0;
	}
	if(up->mbox.head == nil) {
		up->state = Msgsleep;
		unlock(&up->mbox.lock);
		waited = 1;
		sched();
	}
	if(waited){
		lock(&up->mbox.lock);
		if(up->mbox.head == nil){
			// you end up here if you get interrupted
			// and no messages have arrived
			unlock(&up->mbox.lock);
			error(Eintr);
		}
	}
	sz = up->mbox.head->size;
	unlock(&up->mbox.lock);
	return sz;
}

Message*
precvmsg(uintptr minsz)
{
	Message *newmsg;

	lock(&up->mbox.lock);
	if(!(up->mbox.ctl & MSGENABLE)){
		unlock(&up->mbox.lock);
		error(Egoaway);
	}
	if(up->mbox.head == nil){
		unlock(&up->mbox.lock);
		error(Enomsgs);
	}

	// for in kernel use you don't need to check the size of the
	// message before fetching it, so accept 0 for this case.
	if(minsz > 0 && up->mbox.head->size > minsz){
		unlock(&up->mbox.lock);
		error(Esmolbuf);
	}
	newmsg = remove_message(&up->mbox);
	unlock(&up->mbox.lock);
	return newmsg;
}

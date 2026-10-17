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

typedef struct MonitorMsg MonitorMsg;

#pragma pack on
struct MonitorMsg {
	s32int id; /* monitor id */
	u32int event; /* events triggered */
	s32int object; /* pid or fid */
	u64int len; /* for files: how much can be read/written */
	u64int offset; /* for files: where can be read/written */
};
#pragma pack off

static Ref nextmonid = {0};

static void
monitorinit(void)
{
	nextmonid.ref = 0;
}

static ObjMonitor*
allocmonitor(void)
{
	u64int newmid;
	ObjMonitor *newmon;

	newmid = incref(&nextmonid);

	newmon = mallocz(sizeof(ObjMonitor), 1);
	if(newmon == nil)
		error(Enomem);
	newmon->id = newmid;
	return newmon;
}

static int
removemonitorfromproc(Proc *p, ObjMonitor *m, int owner)
{
	uintptr i;
	uintptr nmons = 0;
	int found = 0;

	if(p == nil)
		panic("passed nil proc to removemonitorfromproc");
	if(owner){
		for(i = 0; i < p->own_monitors_len; i++){
			if(p->own_monitors[i] == nil)
				continue;
			if(p->own_monitors[i] == m){
				p->own_monitors[i] = nil;
				m->srcproc = nil;
				return 1;
			}
		}
	} else {
		for(i = 0; i < p->monitors_len; i++){
			if(p->monitors[i] == nil)
				continue;
			if(p->monitors[i] == m){
				p->monitors[i] = nil;
				found++;
				continue;
			}
			nmons++;
		}
		if(nmons == 0)
			p->monitored = 0;
		if(found)
			m->pobject = nil;
	}
	return found;
}

static int
removemonitorfromchan(Chan *c, ObjMonitor *m)
{
	uintptr i;
	uintptr nmons = 0;
	int found = 0;

	if(c == nil)
		panic("passed nil chan to removemonitorfromchan");

	for(i = 0; i < c->monitors_len; i++){
		if(c->monitors[i] == nil)
			continue;
		if(c->monitors[i] == m){
			c->monitors[i] = nil;
			found++;
			continue;
		}
		nmons++;
	}

	if(nmons == 0)
		c->monitored = 0;
	if(found)
		m->cobject = nil;

	return found;
}

int
_freemonitor(ObjMonitor *m)
{
	if(m == nil)
		panic("passed _freemonitor a nil monitor");

	if(m->srcproc)
		removemonitorfromproc(m->srcproc, m, 1);

	if((m->events & MT_Process) && m->pobject)
		removemonitorfromproc(m->pobject, m, 0);
	else if((m->events & MT_File) && m->cobject)
		removemonitorfromchan(m->cobject, m);

	free(m);

	return 0;
}

int /* free with locks */
freemonitor(ObjMonitor *m)
{
	Proc *parent = nil;
	Proc *target_proc = nil;
	Chan *target_chan = nil;
	int res;

	if(m == nil)
		panic("passed freemonitor a nil monitor");

	if(m->srcproc)
		parent = m->srcproc;

	if(m->object)
		if(m->events & MT_Process)
			target_proc = m->pobject;
		else if(m->events & MT_File)
			target_chan = m->cobject;
		else
			panic("tried to free monitor with unknown type");

	if(parent)
		lock(&parent->monitorlock);
	if(target_proc)
		lock(&target_proc->monitorlock);
	if(target_chan)
		lock(&target_chan->monitorlock);

	res = _freemonitor(m);

	if(target_chan)
		unlock(&target_chan->monitorlock);
	if(target_proc)
		unlock(&target_proc->monitorlock);
	if(parent)
		unlock(&parent->monitorlock);

	return res;
}

int
triggermonitor(ObjMonitor *m, u32int event, u64int len, u64int offset)
{
	Message *msg;
	MonitorMsg *monmsg;
	s32int object = -1;

	if(m == nil)
		return 0;

	// check event type
	if((event & (MT_Process|MT_File)) != (m->events & (MT_Process|MT_File)))
		return 0;

	// check events match
	if(!(event & m->events))
		return 0;

	// check to see if the process is dumb
	if(!(m->srcproc->mbox.ctl & (MSGENABLE|MSGMONITOR)))
		return -1;

	if(event & MT_Process)
		object = m->pobject->pid;
	else if(event & MT_File){
		// we need to work out the chan's fd for this proc
		for(uintptr i = 0; i < m->srcproc->fgrp->nfd; i++)
			if(m->srcproc->fgrp->fd[i] == m->cobject){
				object = (s32int)i;
				break;
			}
		if(object < 0){
			// invalid monitor, cancel it
			cancelmonitor(m->srcproc, m->id);
			return 0;
		}
	}

	msg = newmessage(TagMonitor, nil, sizeof(MonitorMsg));
	if(!msg)
		error(Enomem);
	monmsg = msg->data;
	monmsg->id = m->id;
	monmsg->object = object;
	monmsg->event = event;
	monmsg->len = len;
	monmsg->offset = offset;

	if(psendmsg(m->srcproc, msg) < 0){
		freemessage(msg);
		return -1;
	}

	return 0;
}

void
proctriggermonitors(Proc *p, u32int event, u64int len, u64int offset)
{
	uintptr i;

	if(p->monitored)
		for(i = 0; i < p->monitors_len; i++){
			if(p->monitors[i] == nil)
				continue;
			triggermonitor(p->monitors[i], event, len, offset);
		}
}

void
chantriggermonitors(Chan *c, u32int event, u64int len, u64int offset)
{
	uintptr i;

	if(c->monitored)
		for(i = 0; i < c->monitors_len; i++){
			if(c->monitors[i] == nil)
				continue;
			triggermonitor(c->monitors[i], event, len, offset);
		}
}

static int
procaddmonitor(ObjMonitor *mon, Proc *target, int parent)
{
	ObjMonitor **newmonarray, **monitors;
	uintptr newmonarraysz, monitors_len;
	uintptr i;

	lock(&target->monitorlock);
Retry:
	if(parent){
		monitors_len = target->own_monitors_len;
		monitors = target->own_monitors;
	} else {
		monitors_len = target->monitors_len;
		monitors = target->monitors;
	}
	for(i = 0; i < monitors_len; i++){
		if(monitors[i] == nil){
			monitors[i] = mon;
			if(!parent)
				target->monitored = 1;
			goto Done;
		}
	}

	newmonarraysz = monitors_len == 0 ? 2 : monitors_len*2;
	newmonarray = mallocz(sizeof(ObjMonitor*)*newmonarraysz, 1);
	if(newmonarray == nil){
		unlock(&target->monitorlock);
		return -1;
	}
	if(monitors_len != 0){
		memmove(newmonarray, monitors, monitors_len*sizeof(ObjMonitor*));
		free(monitors);
	}
	if(parent){
		target->own_monitors_len = newmonarraysz;
		target->own_monitors = newmonarray;
	} else {
		target->monitors_len = newmonarraysz;
		target->monitors = newmonarray;
	}
	goto Retry;
Done:
	unlock(&target->monitorlock);
	return 0;
}

static int
chanaddmonitor(ObjMonitor *mon, Chan *target)
{
	ObjMonitor **newmonarray;
	uintptr newmonarraysz;
	uintptr i;

	lock(&target->monitorlock);
Retry:
	for(i = 0; i < target->monitors_len; i++){
		if(target->monitors[i] == nil){
			target->monitors[i] = mon;
			target->monitored = 1;
			goto Done;
		}
	}

	newmonarraysz = target->monitors_len == 0 ? 2 : target->monitors_len*2;
	newmonarray = mallocz(sizeof(ObjMonitor*)*newmonarraysz, 1);
	if(newmonarray == nil){
		unlock(&target->monitorlock);
		return -1;
	}
	if(target->monitors_len != 0){
		memmove(newmonarray, target->monitors, target->monitors_len*sizeof(ObjMonitor*));
		free(target->monitors);
	}
	target->monitors_len = newmonarraysz;
	target->monitors = newmonarray;
	goto Retry;
Done:
	unlock(&target->monitorlock);
	return 0;
}

static ObjMonitor*
monitorobject(Proc *parent, void *target, u32int events)
{
	ObjMonitor *newmon;

	if((newmon = allocmonitor()) == nil)
		error(Enomem);
	newmon->srcproc = parent;
	newmon->object = target;
	newmon->events = events;

	/* add to the parent */
	if(procaddmonitor(newmon, parent, 1) < 0)
		error(Enomem);

	/* add to the target */
	if(events & MT_Process)
		if(procaddmonitor(newmon, newmon->pobject, 0) < 0)
			error(Enomem);
	else if(events & MT_File)
		if(chanaddmonitor(newmon, newmon->cobject) < 0)
			error(Enomem);

	return newmon;
}

ObjMonitor*
procmonitor(Proc *parent, Proc *target, u32int events)
{
	if(!(events & MT_Process))
		return nil;

	return monitorobject(parent, (void*)target, events);
}

ObjMonitor*
chanmonitor(Proc *parent, Chan *target, u32int events)
{
	if(!(events & MT_File))
		return nil;

	return monitorobject(parent, (void*)target, events);
}

ObjMonitor* /* for MM_Track and MM_Stalk */
dupprocmonitor(ObjMonitor *m, Proc *newtarget)
{
	return procmonitor(m->srcproc, newtarget, m->events);
}

int
cancelmonitor(Proc *parent, int mid)
{
	uintptr i;
	ObjMonitor *m;

	if(parent->own_monitors == nil)
		return -1;
	for(i = 0; i < parent->own_monitors_len; i++)
		if(parent->own_monitors[i] != nil && parent->own_monitors[i]->id == mid){
			m = parent->own_monitors[i];
			freemonitor(m);
			return 0;
		}
	return -1;
}

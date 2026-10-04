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
			if(p->own_monitors[i] == m && p->own_monitors[i]->id == m->id){
				p->own_monitors[i] = nil;
				return 1;
			}
		}
	} else {
		for(i = 0; i < p->monitors_len; i++){
			if(p->monitors[i] == nil)
				continue;
			if(p->monitors[i] == m && p->monitors[i]->id == m->id){
				p->monitors[i] = nil;
				found++;
				continue;
			}
			nmons++;
		}
		if(nmons == 0)
			p->monitored = 0;
	}
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

	free(m);

	return 0;
}

int /* free with locks */
freemonitor(ObjMonitor *m)
{
	int lock_parent = 0;
	int lock_target = 0;
	Proc *parent = nil;
	Proc *target = nil;
	int res;

	if(m == nil)
		panic("passed freemonitor a nil monitor");

	if(m->srcproc){
		lock_parent = 1;
		parent = m->srcproc;
	}
	if((m->events & MT_Process) && m->pobject){
		lock_target = 1;
		target = m->pobject;
	}

	if(lock_parent)
		lock(&parent->monitorlock);
	if(lock_target)
		lock(&target->monitorlock);

	res = _freemonitor(m);

	if(lock_target)
		unlock(&target->monitorlock);
	if(lock_parent)
		unlock(&parent->monitorlock);

	return res;
}

int
triggermonitor(ObjMonitor *m, u32int event)
{
	Message *msg;
	MonitorMsg *monmsg;

	if(m == nil)
		return 0;
	if(!(event & m->events))
		return 0;
	if(!(m->srcproc->mbox.ctl & (MSGENABLE|MSGMONITOR)))
		return -1;

	msg = newmessage(TagMonitor, nil, sizeof(MonitorMsg));
	if(!msg)
		error(Enomem);
	monmsg = msg->data;
	monmsg->id = m->id;
	monmsg->event = event;
	if(event & MT_Process)
		monmsg->object = m->pobject->pid;

	msg = newmessage(TagMonitor, monmsg, sizeof(MonitorMsg));
	if(psendmsg(m->srcproc, msg) < 0){
		freemessage(msg);
		return -1;
	}

	return 0;
}

void
proctriggermonitors(Proc *p, u32int event)
{
	uintptr i;

	if(p->monitored)
		for(i = 0; i < p->monitors_len; i++){
			if(p->monitors[i] == nil)
				continue;
			triggermonitor(p->monitors[i], event);
		}
}

ObjMonitor*
procmonitor(Proc *parent, Proc *target, u32int events)
{
	ObjMonitor *newmon;
	ObjMonitor **newmonarray;
	uintptr newmonarraysz;
	uintptr i;

	if(!(events & MT_Process))
		return nil;

	if((newmon = allocmonitor()) == nil)
		error(Enomem);
	newmon->srcproc = parent;
	newmon->pobject = target;
	newmon->events = events;

	/* add to the parent */
	lock(&parent->monitorlock);
ParentRetry:
	for(i = 0; i < parent->own_monitors_len; i++){
		if(parent->own_monitors[i] == nil){
			parent->own_monitors[i] = newmon;
			goto ParentDone;
		}
	}
	if(parent->own_monitors_len == 0)
		newmonarraysz = 2;
	else
		newmonarraysz = parent->own_monitors_len*2;
	newmonarray = mallocz(sizeof(ObjMonitor*)*newmonarraysz, 1);
	if(newmonarray == nil){
		_freemonitor(newmon);
		unlock(&parent->monitorlock);
		error(Enomem);
	}
	if(parent->own_monitors_len != 0){
		memmove(newmonarray, parent->own_monitors,
			parent->own_monitors_len*sizeof(ObjMonitor*));
		free(parent->own_monitors);
	}
	parent->own_monitors = newmonarray;
	parent->own_monitors_len = newmonarraysz;
	goto ParentRetry;
ParentDone:
	unlock(&parent->monitorlock);

	/* add to the target */
	lock(&target->monitorlock);
TargetRetry:
	for(i = 0; i < target->monitors_len; i++){
		if(target->monitors[i] == nil){
			target->monitors[i] = newmon;
			target->monitored = 1;
			goto TargetDone;
		}
	}
	if(target->monitors_len == 0)
		newmonarraysz = 2;
	else
		newmonarraysz = target->monitors_len*2;
	newmonarray = mallocz(sizeof(ObjMonitor*)*newmonarraysz, 1);
	if(newmonarray == nil){
		_freemonitor(newmon);
		unlock(&target->monitorlock);
		error(Enomem);
	}
	if(target->monitors_len != 0){
		memmove(newmonarray, target->monitors,
			target->monitors_len*sizeof(ObjMonitor*));
		free(target->monitors);
	}
	target->monitors = newmonarray;
	target->monitors_len = newmonarraysz;
	goto TargetRetry;
TargetDone:
	unlock(&target->monitorlock);
	return newmon;
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
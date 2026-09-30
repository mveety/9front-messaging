#include <u.h>
#include <libc.h>
#include <msg.h>

enum {
	MailboxSize = 2,
};

int
msgenable(void)
{
	u32int ctl;

	ctl = sys_msgctl(Mctlread, 0);
	if(ctl == 0)
		ctl = MSGENABLE|MSGMONITOR|MSGPROCS;
	else {
		if(!(ctl & MSGENABLE))
			ctl |= MSGENABLE;
	}
	if(sys_msgctl(Mctlwrite, ctl) != ctl)
		return -1;
	return 0;
}

int
msgdisable(void)
{
	u32int ctl;

	ctl = sys_msgctl(Mctlread, 0);
	if(!(ctl & MSGENABLE))
		return 0;
	ctl &= ~MSGENABLE;
	if(sys_msgctl(Mctlwrite, ctl) != ctl)
		return -1;
	return 0;
}

Message*
message(int tag, void *data, uintptr len)
{
	Message *msg;

	if(len == 0){
		werrstr("invalid message");
		return nil;
	}

	if(!(msg = mallocz(sizeof(Message), 1)))
		return nil;
	if(!(msg->rawmsg = mallocz(sizeof(s32int)+len, 1)))
		return nil;

	msg->tag = tag;
	msg->rawmsg->tag = tag;
	msg->len = len;
	msg->rawlen = len+sizeof(s32int);
	msg->data = &msg->rawmsg->data[0];
	if(data)
		memmove(msg->data, data, len);

	return msg;
}

Message*
freemessage(Message *msg)
{
	Message *next;

	next = msg->next;
	free(msg->rawmsg);
	free(msg);
	return next;
}

Mailbox*
mailbox(void)
{
	Mailbox *mbox;

	if(!(mbox = mallocz(sizeof(Mailbox), 1)))
		return nil;
	if(!(mbox->msgs = mallocz(MailboxSize*sizeof(Mailbox*), 1))){
		free(mbox);
		return nil;
	}
	mbox->len = MailboxSize;
	return mbox;
}

static int
grow_mailbox(Mailbox *mbox)
{
	Message **oldmsgs;
	uintptr oldlen;
	Message **newmsgs;
	uintptr newlen;

	oldmsgs = mbox->msgs;
	oldlen = mbox->len;
	newlen = oldlen*2;
	if(!(newmsgs = mallocz(newlen*sizeof(Message*), 1)))
		return -1;
	memcpy(newmsgs, oldmsgs, oldlen*sizeof(Message*));
	mbox->msgs = newmsgs;
	mbox->len = newlen;
	free(oldmsgs);
	return 0;
}

static void
_flushmailbox(Mailbox *mbox)
{
	uintptr i;

	for(i = 0; i < mbox->len; i++)
		if(mbox->msgs[i] != nil){
			freemessage(mbox->msgs[i]);
			mbox->msgs[i] = nil;
		}
	mbox->i = 0;
}

void
flushmailbox(Mailbox *mbox)
{
	qlock(&mbox->lock);
	_flushmailbox(mbox);
	qunlock(&mbox->lock);
}

void
freemailbox(Mailbox *mbox)
{
	qlock(&mbox->lock);
	_flushmailbox(mbox);
	free(mbox->msgs);
	free(mbox);
}

static int
add_message(Mailbox *mbox, Message *msg)
{
	uintptr i;

	for(i = 0; i < mbox->len; i++)
		if(mbox->msgs[i] == nil){
			mbox->msgs[i] = msg;
			return 0;
		}

	if(grow_mailbox(mbox) < 0)
		return -1;

	for(i = 0; i < mbox->len; i++)
		if(mbox->msgs[i] == nil){
			mbox->msgs[i] = msg;
			return 0;
		}

	return -2;
}

uvlong
mailboxsz(Mailbox *mbox)
{
	uvlong size = 0;
	uintptr i;

	for(i = 0; i < mbox->len; i++)
		if(mbox->msgs[i] != nil)
			size++;

	return size;
}

Message*
selectmsg(Mailbox *mbox, Message *msg)
{
	uintptr i;

	qlock(&mbox->lock);

	for(i = 0; i < mbox->len; i++)
		if(mbox->msgs[i] == msg){
			mbox->msgs[i] = nil;
			qunlock(&mbox->lock);
			return msg;
		}

	qunlock(&mbox->lock);
	return nil;
}

int
msgsend(int pid, Message *msg)
{
	int retval;

	retval = sys_msgsend(pid, msg->tag, msg->data, msg->len);
	return retval;
}

static Message*
_msgrecv(void)
{
	uintptr len;
	Message *msg;

	len = sys_msgwait();
	if(len == 0){
		werrstr("zero-length message");
		return nil;
	}
	if((intptr)(len) == -1)
		return nil; // errstr should == interrupted

	msg = message(TagDefault, nil, len);
	if(msg == nil)
		return nil;

	if(sys_msgrecv(msg->rawmsg, msg->rawlen) != 0){
		freemessage(msg);
		return nil;
	}

	msg->tag = msg->rawmsg->tag;

	return msg;
}

static Message*
_nextunread(Mailbox *mbox)
{
	uintptr i;

	// return the next unselected message from the mailbox
	for(i = mbox->i; i < mbox->len; i++)
		if(mbox->msgs[i] != nil){
			mbox->i = i+1;
			return mbox->msgs[i];
		}

	mbox->i = 0;
	return nil;
}

Message*
msgrecv(Mailbox *mbox)
{
	Message *msg;

	if(mbox == nil)
		return _msgrecv();

	qlock(&mbox->lock);

	// fetch the next unread message
	msg = _nextunread(mbox);
	if(msg){
		qunlock(&mbox->lock);
		return msg;
	}

	// reset and wait
	msg = _msgrecv();
	if(msg)
		add_message(mbox, msg);

	qunlock(&mbox->lock);
	return msg;
}

static Message*
_msgrecvfilter(int *tags, uvlong ntags)
{
	Message *msg;
	uvlong i;

	if(ntags == 0)
		return _msgrecv();
	if(tags == nil)
		return _msgrecv();

	for(;;){
		if(!(msg = _msgrecv()))
			return nil;
		for(i = 0; i < ntags; i++)
			if(msg->tag == tags[i])
				return msg;
		freemessage(msg);
	}
}

Message*
msgrecvfilter(Mailbox *mbox, int *tags, uvlong ntags)
{
	Message *msg;
	uvlong i;

	if(mbox == nil)
		return _msgrecvfilter(tags, ntags);

	if(ntags == 0 || tags == nil)
		return msgrecv(mbox);

	qlock(&mbox->lock);

	// check the mailbox first
	do {
		msg = _nextunread(mbox);
		if(msg){
			for(i = 0; i < ntags; i++)
				if(msg->tag == tags[i]){
					qunlock(&mbox->lock);
					return msg;
				}
		}
	} while(msg != nil);

	for(;;) {
		msg = _msgrecv();
		if(!msg)
			break;

		add_message(mbox, msg);
		for(i = 0; i < ntags; i++)
			if(msg->tag == tags[i]){
				qunlock(&mbox->lock);
				return msg;
			}
	}

	qunlock(&mbox->lock);
	return nil;
}

int
monitor(int object, u32int events)
{
	msgenable();
	return sys_monitor(object, events);
}

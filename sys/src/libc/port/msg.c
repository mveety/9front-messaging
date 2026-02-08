#include <u.h>
#include <libc.h>
#include <msg.h>

/*
wire format
basically the first four bytes are the magic number,
(u32int), the next four are the tag (s32int), the
sender's pid (s32int), and the rest is the actual
message contents.
*/

enum {
	MsgMagic = 0xdeadbeef,
	MailboxSize = 2,
	MsgHeaderSize = 3*sizeof(u32int),
};

#pragma pack on
typedef struct {
	u32int magic;
	u32int tag;
	u32int pid;
	char data[1];
} MMdata;
#pragma pack off

typedef struct {
	uintptr len; /* includes the tag */
	union {
		void *data;
		MMdata *payload;
	};
} MMessage;

static void
free_mmessage(MMessage *mm)
{
	free(mm->data);
	free(mm);
}

/* does not free Message, allocates MMessage */
static MMessage*
marshal_message(Message *src)
{
	MMessage *dst;

	assert(src);
	assert(src->len > 0);
	assert(src->data);

	if(!(dst = mallocz(sizeof(MMessage), 1)))
		return nil;
	if(!(dst->data = mallocz(src->len + MsgHeaderSize, 1))){
		free(dst);
		return nil;
	}
	dst->len = src->len + MsgHeaderSize;

	dst->payload->magic = MsgMagic;
	dst->payload->tag = src->tag;
	dst->payload->pid = src->pid;
	memmove(&dst->payload->data[0], src->data, src->len);

	return dst;
}

/* does not free MMessage, allocates Message */
static Message*
unmarshal_message(MMessage *src)
{
	Message *dst;

	if(!src)
		return nil;

	/* try to verify format validity */
	if(src->len <= MsgHeaderSize){
		werrstr("malformed message");
		return nil;
	}

	if(src->payload->magic != MsgMagic){
		werrstr("malformed message");
		return nil;
	}

	if(!(dst = mallocz(sizeof(Message), 1)))
		return nil;
	if(!(dst->data = mallocz(src->len-MsgHeaderSize, 1))){
		free(dst);
		return nil;
	}

	dst->len = src->len - MsgHeaderSize;
	dst->tag = src->payload->tag;
	dst->pid = src->payload->pid;
	memmove(dst->data, &src->payload->data[0], dst->len);

	return dst;
}

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

	if(data != nil && len == 0){
		werrstr("invalid message");
		return nil;
	}
	if(data == nil && len != 0){
		werrstr("invalid message");
		return nil;
	}

	if(!(msg = mallocz(sizeof(Message), 1)))
		return nil;

	msg->tag = tag;
	if(data == nil && len == 0) {
		/* zero-length messages need to have some payload */
		if(!(msg->data = mallocz(1, 1))){
			free(msg);
			return nil;
		}
		*((u8int*)msg->data) = 0;
		msg->len = 1;
		return msg;
	}

	if(!(msg->data = mallocz(len, 1))){
		free(msg);
		return nil;
	}
	msg->pid = getpid();
	msg->len = len;
	memmove(msg->data, data, len);

	return msg;
}

Message*
freemsg(Message *msg)
{
	Message *next;

	next = msg->next;
	free(msg->data);
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
			freemsg(mbox->msgs[i]);
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
	MMessage *mmsg;
	int retval;

	if(!(mmsg = marshal_message(msg)))
		return -1;

	retval = sys_msgsend(pid, mmsg->data, mmsg->len);
	free_mmessage(mmsg);
	return retval;
}

static Message*
_msgrecv(void)
{
	MMessage mmsg;
	Message *msg;

	mmsg.len = sys_msgwait();
	if(mmsg.len == 0){
		werrstr("zero-length message");
		return nil;
	}
	if((intptr)(mmsg.len) == -1)
		return nil; // errstr should = interrupted
	if(!(mmsg.data = mallocz(mmsg.len, 1)))
		return nil;

	if(sys_msgrecv(mmsg.data, mmsg.len) != 0){
		free(mmsg.data);
		return nil;
	}
	
	msg = unmarshal_message(&mmsg);
	free(mmsg.data);
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
		freemsg(msg);
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

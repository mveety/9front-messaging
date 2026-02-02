#include <u.h>
#include <libc.h>

/*
wire format
basically the first four bytes are the magic number,
(u32int), the next four are the sentinel (s32int),
 and the rest is the actual message contents.
*/

enum {
	MsgMagic = 0xdeadbeef,
};

typedef struct {
	uintptr len; /* includes the sentinel */
	char *data;
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
	if(!(dst->data = mallocz(src->len + (2*sizeof(s32int)), 1))){
		free(dst);
		return nil;
	}
	dst->len = src->len + 2*sizeof(s32int);

	*((u32int*)dst->data) = MsgMagic;
	*((s32int*)(dst->data+4)) = src->sentinel;
	memmove(dst->data+8, src->data, src->len);

	return dst;
}

/* does not free MMessage, allocates Message */
static Message*
unmarshal_message(MMessage *src)
{
	Message *dst;
	u32int magic;
	s32int sentinel;

	assert(src);
	assert(src->len > sizeof(u32int)+sizeof(s32int));
	assert(src->data);

	if(!(dst = mallocz(sizeof(Message), 1)))
		return nil;
	if(!(dst->data = mallocz(src->len-(2*sizeof(s32int)), 1))){
		free(dst);
		return nil;
	}
	dst->len = src->len - (2*sizeof(s32int));

	magic = *((u32int*)src->data);
	sentinel = *((s32int*)(src->data+4));
	if(magic != MsgMagic){
		free(dst->data);
		free(dst);
		werrstr("malformed message");
		return nil;
	}
	dst->sentinel = sentinel;
	memmove(dst->data, &src->data[8], dst->len);

	return dst;
}

int
msgenable(void)
{
	u32int ctl;

	ctl = sys_msgctl(Mctlread, 0);
	if(!(ctl & MSGENABLE))
		ctl |= MSGENABLE;
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
message(int sentinel, void *data, uintptr len)
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

	msg->sentinel = sentinel;
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
	return mbox;
}

static void
_flushmailbox(Mailbox *mbox)
{
	Message *msg;

	for(msg = mbox->head; msg != nil; msg = freemsg(msg))
		;;
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
	free(mbox);
}

static void
add_message(Mailbox *mbox, Message *msg)
{
	if(mbox->head == nil){
		assert(mbox->tail == nil);
		assert(mbox->cur == nil);
		mbox->head = msg;
		mbox->tail = msg;
	} else
		mbox->tail->next = msg;
}

uvlong
mailboxsz(Mailbox *mbox)
{
	uvlong size = 0;
	Message *m;

	for(m = mbox->head; m != nil; m = m->next)
		size++;

	return size;
}

Message*
selectmsg(Mailbox *mbox, Message *msg)
{
	Message *cur;
	Message *prev = nil;

	qlock(&mbox->lock);
	for(cur = mbox->head; cur != nil; cur = cur->next){
		if(cur == msg){
			if(prev == nil)
				mbox->head = cur->next;
			else
				prev->next = cur->next;
			if(msg == mbox->cur)
				mbox->cur = cur->next;
			if(msg == mbox->tail)
				mbox->tail = prev;
			msg->next = nil;
			qunlock(&mbox->lock);
			return msg;
		}
		prev = cur;
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

Message*
msgrecv(Mailbox *mbox)
{
	Message *msg;

	if(mbox == nil)
		return _msgrecv();

	qlock(&mbox->lock);
	msg = _msgrecv();
	add_message(mbox, msg);
	return msg;
}

static Message*
_msgrecvfilter(int *sentinels, uvlong nsentinels)
{
	Message *msg;
	uvlong i;

	if(nsentinels == 0)
		return _msgrecv();
	if(sentinels == nil)
		return _msgrecv();

	for(;;){
		if(!(msg = _msgrecv()))
			return nil;
		for(i = 0; i < nsentinels; i++)
			if(msg->sentinel == sentinels[i])
				return msg;
		freemsg(msg);
	}
}

Message*
msgrecvfilter(Mailbox *mbox, int *sentinels, uvlong nsentinels)
{
	Message *msg;
	uvlong i;

	if(mbox == nil)
		return _msgrecvfilter(sentinels, nsentinels);

	if(nsentinels == 0 || sentinels == nil){
		if(!(msg = _msgrecv()))
			return nil;
		add_message(mbox, msg);
		return msg;
	}

	for(;;){
		if(!(msg = _msgrecv()))
			return nil;
		add_message(mbox, msg);
		for(i = 0; i < nsentinels; i++)
			if(msg->sentinel == sentinels[i])
				return msg;
	}
}

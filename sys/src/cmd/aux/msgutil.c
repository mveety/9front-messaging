#include <u.h>
#include <libc.h>
#include <msg.h>

enum {
	Usage,
	Send,
	Receive,
	Default,
	Raw,
	Formatted,
	Detect,
	MsgMagic = 0xdeadbeef,
	MsgHeaderSize = 3*sizeof(u32int),
};

char *argv0;

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

void
usage(void)
{
	fprint(2, "usage: %s [-R|-F] [-T tag] [-n times] -s pid message\n", argv0);
	fprint(2, "       %s [-R|-F|-D] [-A] [-n times] -r\n", argv0);
	exits("usage");
}

int
main(int argc, char *argv[])
{
	ulong target = 0;
	int job = Usage;
	int type = Default;
	char *msgdata;
	uintptr msglen;
	int ntimes = 1;
	int tag = 0;
	u32int octl;
	u32int ctlextra = 0;
	Message *msg = nil;
	MMessage tmp;

	argv0 = argv[0];
	ARGBEGIN{
	case 's': // send
		target = atoi(EARGF(usage()));
		job = Send;
		break;
	case 'r':
		job = Receive;
		break;
	case 'n':
		ntimes = atoi(EARGF(usage()));
		break;
	case 'R':
		type = Raw;
		break;
	case 'F':
		type = Formatted;
		break;
	case 'D':
		type = Detect;
		break;
	case 'T':
		tag = atoi(EARGF(usage()));
		break;
	case 'A':
		ctlextra |= MSGALLUSERS;
		break;
	case 'h':
	default:
		usage();
		break;
	}ARGEND;

	switch(job){
	default:
		assert(0);
		break;
	case Usage:
		usage();
		break;
	case Send:
		if(argc != 1)
			usage();
		if(type == Default)
			type = Formatted;
		msgdata = strdup(argv[0]);
		msglen = strlen(msgdata)+1;
		if(type == Formatted){
			msg = message(tag, msgdata, msglen);
			for(int i = 0; i < ntimes; i++)
				if(msgsend(target, msg) < 0) {
					fprint(2, "error: unable to send message to %lud: %r\n", target);
					exits("msgsend fail");
				}
		} else {
			for(int i = 0; i < ntimes; i++){
				if(sys_msgsend(target, msgdata, msglen) < 0){
					fprint(2, "error: unable to send message to %lud: %r\n", target);
					exits("msgsend fail");
				}
				fprint(2, "sent \"%s\" (size %p)\n", msgdata, msglen);
			}
		}
		exits(nil);
		break;
	case Receive:
		if(type == Default)
			type = Detect;
		fprint(2, "pid %d: waiting for messages...\n", getpid());
		switch(type){
		case Formatted:
			msgenable();
			octl = sys_msgctl(Mctlread, 0);
			sys_msgctl(Mctlwrite, octl|ctlextra);
			for(int i = 0; i < ntimes; i++) {
				if(!(msg = msgrecv(nil))){
					fprint(2, "error: msgrecv: %r\n");
					exits("msgrecv fail");
				}
				fprint(2, "got message (type %d, size %p) \"%s\"\n", msg->tag, msg->len, msg->data);
			}
			break;
		case Detect:
		case Raw:
			sys_msgctl(Mctlwrite, MSGENABLE|ctlextra); // accept messages
			for(int i = 0; i < ntimes; i++){
				msglen = sys_msgwait();
				if(msglen == 0 || (intptr)msglen == -1){
					fprint(2, "error: msglen == 0: %r\n");
					exits("msgwait fail");
				}
				msgdata = mallocz(msglen, 1);
				if(!msgdata)
					exits("malloc");
				if(sys_msgrecv(msgdata, msglen) != 0){
					fprint(2, "error: msgrecv: %r\n");
					exits("msgrecv fail");
				}
				if(type == Detect){
					tmp.data = msgdata;
					tmp.len = msglen;
					msg = unmarshal_message(&tmp);
				}
				if(msg != nil){
					fprint(2, "got message (type %d, size %p) \"%s\"\n", msg->tag, msg->len, msg->data);
					freemsg(msg);
					msg = nil;
				} else
					fprint(2, "got message (size %p) \"%s\"\n", msglen, msgdata);
			}
			break;
		}
		exits(nil);
	}
	return 0;
}

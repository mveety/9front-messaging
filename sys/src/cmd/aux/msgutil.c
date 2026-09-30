#include <u.h>
#include <libc.h>
#include <msg.h>

enum {
	Usage,
	Send,
	Receive,
	Monitor,
};

char *argv0;

void
usage(void)
{
	fprint(2, "usage: %s [-t tag] [-n times] -s pid message\n", argv0);
	fprint(2, "       %s [-A] [-n times] -r\n", argv0);
	fprint(2, "       %s -m pid\n", argv0);
	exits("usage");
}

int
main(int argc, char *argv[])
{
	ulong target = 0;
	int job = Usage;
	char *msgdata;
	uintptr msglen;
	int ntimes = 1;
	int tag = 0;
	u32int octl;
	u32int ctlextra = 0;
	Message *msg;
	int mid;
	MonitorMsg *monmsg;
	char *tmp = nil;
	uintptr tmpsz = 0;

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
	case 'm':
		job = Monitor;
		target = atoi(EARGF(usage()));
		break;
	case 't':
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
	case Monitor:
		msgenable();
		mid = sys_monitor(target, (MT_Process|ME_Death));
		if(mid < 0){
			fprint(2, "error: unable to monitor process %lud: %r\n", target);
			exits("monitor");
		}
		msg = msgrecv(nil);
		if(msg == nil){
			fprint(2, "error: got nil message: %r\n");
			exits("nil message");
		}
		if(msg->tag != TagMonitor){
			fprint(2, "error: recieved spurious message\n");
			exits("spurious message");
		}
		monmsg = msg->data;
		fprint(2, "got monitor %d: event = %x, pid = %d\n",
					monmsg->id, monmsg->event, monmsg->object);
		exits(nil);
		break;
	case Send:
		if(argc != 1)
			usage();
		msgdata = strdup(argv[0]);
		msglen = strlen(msgdata)+1;
		msg = message(tag, msgdata, msglen);
		for(int i = 0; i < ntimes; i++)
			if(msgsend(target, msg) < 0) {
				fprint(2, "error: unable to send message to %lud: %r\n", target);
				exits("msgsend fail");
			}
		exits(nil);
		break;
	case Receive:
		fprint(2, "pid %d: waiting for messages...\n", getpid());
		msgenable();
		octl = sys_msgctl(Mctlread, 0);
		sys_msgctl(Mctlwrite, octl|ctlextra);
		for(int i = 0; i < ntimes; i++) {
			if(!(msg = msgrecv(nil))){
				fprint(2, "error: msgrecv: %r\n");
				exits("msgrecv fail");
			}
			if(!tmp || tmpsz <= msg->len){
				tmp = malloc(msg->len+1);
				tmpsz = msg->len+1;
				if(!tmp)
					exits("malloc");
			}
			memset(tmp, 0, tmpsz);
			memmove(tmp, msg->data, msg->len);
			fprint(2, "got message (tag %d, size %p) \"%s\"\n",
				msg->tag, msg->len, msg->data);
		}
		exits(nil);
	}
	return 0;
}

#include <u.h>
#include <libc.h>

enum {
	Usage,
	Send,
	Receive,
};

char *argv0;

void
usage(void)
{
	fprint(2, "usage: %s [-n times] -s pid message\n", argv0);
	fprint(2, "       %s [-n times] -r\n", argv0);
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
		msgdata = strdup(argv[0]);
		msglen = strlen(msgdata)+1;
		for(int i = 0; i < ntimes; i++){
			if(sys_msgsend(target, msgdata, msglen) < 0){
				fprint(2, "error: unable to send message to %lud: %r\n", target);
				exits("msgsend fail");
			}
			fprint(2, "sent \"%s\" (size %p)\n", msgdata, msglen);
		}
		exits(nil);
		break;
	case Receive:
		sys_msgctl(Mctlwrite, MSGENABLE); // accept messages
		fprint(2, "pid %d: waiting for messages...\n", getpid());
		for(int i = 0; i < ntimes; i++){
			msglen = sys_msgwait();
			if(msglen == 0){
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
			fprint(2, "got message (size %p) \"%s\"\n", msglen, msgdata);
		}
		exits(nil);
	}
	return 0;
}

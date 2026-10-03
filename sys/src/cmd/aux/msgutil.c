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

char* /* gross but functional */
append_string(char *s1, char *s2)
{
	char *res;

	if(s1 == nil)
		return smprint("%s", s2);

	res = smprint("%s|%s", s1, s2);
	free(s1);
	return res;
}

char*
parse_event(u32int event)
{
	char *res = nil;

	assert(event & (MT_Process|MT_File));

	if(event & MT_Process)
		res = append_string(res, "MT_Process");
	if(event & MT_File)
		res = append_string(res, "MT_File");
	if(event & MM_Track)
		res = append_string(res, "MM_Track");
	if(event & MM_Stalk)
		res = append_string(res, "MM_Stalk");
	if(event & MM_Exec)
		res = append_string(res, "MM_Exec");
	if(event & ME_Death)
		res = append_string(res, "ME_Death");
	if(event & ME_Rfork)
		res = append_string(res, "ME_Rfork");
	if(event & ME_Exec)
		res = append_string(res, "ME_Exec");
	if(event & ME_Interrupt)
		res = append_string(res, "ME_Interrupt");
	if(event & ME_Hangup)
		res = append_string(res, "ME_Hangup");
	if(event & ME_Alarm)
		res = append_string(res, "ME_Alarm");
	if(event & ME_Abort)
		res = append_string(res, "ME_Abort");
	if(event & ME_Read)
		res = append_string(res, "ME_Read");
	if(event & ME_Write)
		res = append_string(res, "ME_Write");
	if(event & ME_Remove)
		res = append_string(res, "ME_Remove");
	if(event & ME_Close)
		res = append_string(res, "ME_Close");
	if(event & ME_Open)
		res = append_string(res, "ME_Open");

	return res;
}

void
usage(void)
{
	fprint(2, "usage: %s [-t tag] [-n times] -s pid message\n", argv0);
	fprint(2, "       %s [-A] [-n times] -r\n", argv0);
	fprint(2, "       %s [-o] [-TSE] [-n times] -m pid\n", argv0);
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
	int oneshot = 0;
	char *parsedevent;
	u32int events = (MT_Process|ME_Death|ME_Rfork|ME_Exec|ME_Interrupt|
		ME_Hangup|ME_Alarm|ME_Abort);

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
	case 'o':
		oneshot = 1;
		break;
	case 'A':
		ctlextra |= MSGALLUSERS;
		break;
	case 'T':
		events |= MM_Track;
		break;
	case 'S':
		events |= MM_Stalk;
		break;
	case 'E':
		events |= MM_Exec;
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
		mid = sys_monitor(target, events);
		parsedevent = parse_event(events);
		fprint(2, "monitoring %lud for %s (%x)\n", target, parsedevent, events);
		free(parsedevent);
		if(mid < 0){
			fprint(2, "error: unable to monitor process %lud: %r\n", target);
			exits("monitor");
		}
		for(int i = 0; i < ntimes;){
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
			parsedevent = parse_event(monmsg->event);
			assert(parsedevent != nil);
			fprint(2, "got monitor %d: event = %s (%x), pid = %d\n",
						monmsg->id, parsedevent, monmsg->event, monmsg->object);
			if(monmsg->event & ME_Death)
				i++;
			if(oneshot){
				fprint(2, "exiting after one shot monitor\n");
				exits("oneshot");
			}
			free(parsedevent);
			freemessage(msg);
		}
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
			freemessage(msg);
		}
		exits(nil);
	}
	return 0;
}

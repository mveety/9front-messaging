#include <u.h>
#include <libc.h>
#include <bio.h>
#include <msg.h>

void	ps(char*);
void	error(char*);
int	cmp(void*, void*);

Biobuf	bout;
int	pflag;
int	aflag;
int	nflag;
int	rflag;
int mflag;

void
main(int argc, char *argv[])
{
	int fd, i, tot, none = 1;
	Dir *dir, **mem;


	ARGBEGIN {
	case 'a':
		aflag++;
		break;
	case 'p':
		pflag++;
		break;
	case 'n':
		nflag++;
		break;
	case 'r':
		rflag++;
		break;
	case 'm':
		mflag++;
		break;
	} ARGEND;
	Binit(&bout, 1, OWRITE);
	if(chdir("/proc")==-1)
		error("/proc");
	fd=open(".", OREAD);
	if(fd<0)
		error("/proc");
	tot = dirreadall(fd, &dir);
	if(tot <= 0){
		fprint(2, "ps: empty directory /proc\n");
		exits("empty");
	}
	mem = malloc(tot*sizeof(Dir*));
	for(i=0; i<tot; i++)
		mem[i] = dir++;

	qsort(mem, tot, sizeof(Dir*), cmp);
	for(i=0; i<tot; i++){
		ps(mem[i]->name);
		none = 0;
	}

	if(none)
		error("no processes; bad #p");
	exits(0);
}

void
ps(char *s)
{
	ulong utime, stime, rtime, size, msgin, msgout, mboxsz;
	u32int msgctl;
	int argc, basepri, fd, i, n, pri, mfd, margc;
	char args[256], *argv[16], buf[64], nbuf[13], pbuf[8], rbuf[20], rbuf1[20], status[4096], mstatus[4096], *margv[5], mbuf[64];

	sprint(buf, "%s/status", s);
	fd = open(buf, OREAD);
	if(fd<0)
		return;
	n = read(fd, status, sizeof status-1);
	close(fd);
	if(n <= 0)
		return;
	status[n] = '\0';

	if((argc = tokenize(status, argv, nelem(argv)-1)) < 12)
		return;
	argv[argc] = 0;

	if(mflag){
		sprint(buf, "%s/mailbox", s);
		mfd = open(buf, OREAD);
		if(mfd<0)
			return;
		n = read(mfd, mstatus, sizeof(mstatus)-1);
		close(mfd);
		if(n <= 0)
			return;
		mstatus[n] = '\0';
		if((margc = tokenize(mstatus, margv, nelem(margv)-1)) < 4)
			return;
		margv[margc] = nil;
		msgctl = (u32int)strtol(margv[0], 0, 16);
		msgin = strtoul(margv[1], 0, 0);
		msgout = strtoul(margv[2], 0, 0);
		mboxsz = strtoul(margv[3], 0, 0);
		snprint(mbuf, sizeof(mbuf), "%c%C%C%c %7uld %7uld %7uld",
			(msgctl & MSGENABLE) ? 'e' : '-',
			(msgctl & MSGMONITOR) ? 'm' : '-',
			(msgctl & MSGPROCS) ? 'p' : '-',
			(msgctl & MSGALLUSERS) ? 'a' : '-',
			msgin, msgout, mboxsz);
	} else
		mbuf[0] = 0;

	/*
	 * 0  text
	 * 1  user
	 * 2  state
	 * 3  cputime[6]
	 * 9  memory
	 * 10 basepri
	 * 11 pri
	 */
	utime = strtoul(argv[3], 0, 0)/1000;
	stime = strtoul(argv[4], 0, 0)/1000;
	rtime = strtoul(argv[5], 0, 0)/1000;
	size  = strtoul(argv[9], 0, 0);

	if(nflag){
		snprint(nbuf, sizeof nbuf, " %8s", "?");
		sprint(buf, "%s/noteid", s);
		fd = open(buf, OREAD);
		if(fd >= 0) {
			n = read(fd, buf, sizeof buf-1);
			close(fd);
			if(n > 0)
				snprint(nbuf, sizeof nbuf, " %7ud", atoi(buf));
		}
	}else
		nbuf[0] = 0;
			
	if(pflag){
		basepri = strtoul(argv[10], 0, 0);
		pri = strtoul(argv[11], 0, 0);
		sprint(pbuf, " %2d %2d", basepri, pri);
	} else
		pbuf[0] = 0;

	if(rflag){
		if(rtime >= 86400)
			sprint(rbuf, " %lud:%02lud:%02lud:%02lud", rtime/86400, (rtime/3600)%24, (rtime/60)%60, rtime%60);
		else if(rtime >= 3600)
			sprint(rbuf, " %lud:%02lud:%02lud", rtime/3600, (rtime/60)%60, rtime%60);
		else
			sprint(rbuf, " %lud:%02lud", rtime/60, rtime%60);
		sprint(rbuf1, "%12s", rbuf);
	}else
		rbuf1[0] = 0;

	Bprint(&bout, "%-10s %8s%s%s %4lud:%.2lud %3lud:%.2lud %s %7ludK %s %-8.8s ",
			argv[1],
			s,
			nbuf,
			rbuf1,
			utime/60, utime%60,
			stime/60, stime%60,
			pbuf,
			size,
			mbuf,
			argv[2]);

	if(aflag == 0){
    Noargs:
		Bprint(&bout, "%s\n", argv[0]);
		return;
	}

	sprint(buf, "%s/args", s);
	fd = open(buf, OREAD);
	if(fd < 0)
		goto Badargs;
	n = read(fd, args, sizeof args-1);
	close(fd);
	if(n < 0)
		goto Badargs;
	if(n == 0)
		goto Noargs;
	args[n] = '\0';
	for(i=0; i<n; i++)
		if(args[i] == '\n')
			args[i] = ' ';
	Bprint(&bout, "%s\n", args);
	return;

    Badargs:
	Bprint(&bout, "%s ?\n", argv[0]);
}

void
error(char *s)
{
	fprint(2, "ps: %s: ", s);
	perror("error");
	exits(s);
}

int
cmp(void *va, void *vb)
{
	Dir **a, **b;

	a = va;
	b = vb;
	return atoi((*a)->name) - atoi((*b)->name);
}

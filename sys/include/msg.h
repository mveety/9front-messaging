/* userspace api */
typedef struct Mailbox Mailbox;
typedef struct Message Message;
typedef struct MonitorMsg MonitorMsg;

/* message passing */
enum {
// msgctl ops
	Mctlread = 0,
	Mctlwrite = 1,

// msgctl flags
	MSGENABLE = (1<<0), /* allow process to receive messages */
	MSGMONITOR = (1<<1), /* accept monitor messages */
	MSGPROCS = (1<<2), /* accept process messages */
	MSGALLUSERS = (1<<3), /* allow messages from other users */

// standard tags
	TagMonitor = -128,

// monitor types
	MT_Process = 1<<0,	// get events on processes
	MT_File = 1<<1,		// get events on file descriptors

// monitor modifiers
	MM_Track = 1<<3,	// (for processes) implicitly monitor target children until exec
	MM_Stalk = 1<<4,	// (for processes) implicitly monitor all target children forever

// process events
	ME_Death = 1<<6,	// process death
	ME_Rfork = 1<<7,	// process rforked
	ME_Exec = 1<<8,		// process exec'd
	ME_Interrupt = 1<<9,	// process interrupted (del key, etc)
	ME_Hangup = 1<<10,	// process got hangup note
	ME_Alarm = 1<<11,	// process got alarm note
	ME_Abort = 1<<12,	// process aborted

// file events
	ME_Read = 1<<16, /* ready to be read */
	ME_Write = 1<<17, /* ready to be written */
	ME_Remove = 1<<18, /* file got unlinked */
	ME_Close = 1<<19, /* you or someone else closed the file */
	ME_Open = 1<<20, /* someone else opened the file */
};

struct Message {
	s32int tag;
	uintptr len;
	s32int pid;
	void *data;
	Message *next;
};

struct Mailbox {
	QLock lock;
	uintptr len;
	uintptr i;
	Message **msgs;
};

#pragma pack on
struct MonitorMsg {
	s32int id; /* monitor id */
	u32int event; /* events triggered */
	s32int object; /* pid or fid */
	u64int len; /* for files: how much can be read/written */
	u64int offset; /* for files: where can be read/written */
};
#pragma pack off

int			msgenable(void);
int			msgdisable(void);
Mailbox*	mailbox(void);
void		flushmailbox(Mailbox*);
void		freemailbox(Mailbox*);
uvlong		mailboxsz(Mailbox*);
Message*	message(int, void*, uintptr);
Message*	freemsg(Message*);
Message*	selectmsg(Mailbox*, Message*);
int			msgsend(int, Message*);
Message*	msgrecv(Mailbox*);
Message*	msgrecvfilter(Mailbox*, int*, uvlong);
int			monitor(int, u32int);

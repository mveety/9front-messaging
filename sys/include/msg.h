/* userspace api */
typedef struct Mailbox Mailbox;
typedef struct Message Message;

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

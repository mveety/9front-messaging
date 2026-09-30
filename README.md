# Plan 9 Message Passing Kernel + libc + userland

This repo contains a kernel which add four message passing system calls. Some examples are [here](https://github.com/mveety/p9messaging). The kernel portions are more or less stable (I am currently them on my file server and terminals), but the userspace portions are somewhat still up in the air. Use at your own risk.

## System Calls
* `int sys_msgsend(ulong pid, s32int tag, void *message, uintptr message_size)`
	Asynchonously sends a message `message` to process `pid`. Returns 0 if successful and -1 otherwise. Sets errstr.

* `uintptr sys_msgwait(void)`

	Blocks until a message is ready to be received. Returns the size of the message to be received, 0 if there is a recoverable error, or (uintptr)-1 if there is a serious error. Sets errstr. Kernel returns a MessageData.

* `uintptr sys_msgrecv(void *buffer, uintptr buffer_size)`

	Receives the next available message if the buffer is large enough. Returns 0 on success and (uintptr)-1 on error. Sets errstr.

* `u32int sys_msgctl(int op, u32int ctl)`

	Gets or sets the processes message control bitmap. If op = `Mctlread`, ctl is ignored and sys_msgctl returns the current message control bitmap. If op = `Mctlwrite`, sys_msgctl tries to set the bitmap to `ctl` and returns the resulting bitmap. When `Mctlwrite` is set an error occured when `ctl` is not equal to the return value.

* 's32int sys_monitor(int object, u32int events)'

	Sets a monitor on process id or file id `object`. Monitors send messages when `events` occur on `object`. Returns a monitor id. Sets errstr.

## Control Flags
* `MSGENABLE`

	By default a process can't receive messages until `MSGENABLE` is set.

* 'MSGMONITOR'

	Allows acceptance of monitor (tag = -128) messages.

* `MSGALLUSERS`

	By default a process will reject messages from other users. If this is set the process will accept them.

## User API (`msg.h`)

Standard message format (in rough C):
```
typedef struct {
	s32int tag;
	char data[];
} MessageData;
```

Defined constants:
```
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
	TagDefault = 0,
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
```

Defined data structures:
```
typedef struct Mailbox Mailbox;
typedef struct MessageData MessageData;
typedef struct Message Message;
typedef struct MonitorMsg MonitorMsg;

#pragma pack on
struct MessageData {
	s32int tag;
	char data[1];
};
#pragma pack off

struct Message {
	s32int tag;
	uintptr len;
	void *data;
	MessageData *rawmsg;
	uintptr rawlen;
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

```

* `int msgenable(void)`

	Sets at least the `MSGENABLE` and 'MSGMONITOR' flag.

* `int msgdisable(void)`

	Unsets the `MSGENABLE` flag.

* `Mailbox* mailbox(void)`

	Creates a new userspace mailbox.

* `void flushmailbox(Mailbox *mbox)`

	Empties a userspace mailbox and frees the messages.

* `void freemailbox(Mailbox *mbox)`

	Empties a userspace mailbox, freeing the messages, then frees the mailbox.

* `uvlong mailboxsz(Mailbox *mbox)`

	Returns the number of messages in `mbox`.

* `Message* message(int tag, void *data, uintptr datasz)`

	Creates a new message with `tag` = `tag`, and containing `data`. Allocates a MessageData and Message.data points to &Message.rawmsg.data[0].

* `Message* freemessage(Message *msg)`

	Frees `msg`.

* `Message* selectmsg(Mailbox *mbox, Message *msg)`

	Removes `msg` from the mailbox `mbox`.

* `int msgsend(int pid, Message *msg)`

	Sends `msg` to process `pid`.

* `Message* msgrecv(Mailbox *mbox)`

	Waits for a message to received, returning it. If `mbox` is not nil, each call to `msgrecv` will iterate over the Mailbox `mbox` returning each message with each call. Once `mbox` has been iterated over, `msgrecv` will wait for a message to arrive, place it in `mbox`, and then return it.

* `Message* msgrecvfilter(Mailbox *mbox, int *tags, uvlong tagslen)`

	Works identically to msgrecv but only returns messages with tags matching one of the `tags`. Messages that don't match will be saved into the mailbox. If `mbox` is nil then `msgrecvfilter` will throw away messages that don't match.

## `aux/msgutil`
`aux/msgutil` is a tool to send and receive messages. Mostly usable for debugging.

```
usage: aux/msgutil [-t tag] [-n times] -s pid message
       aux/msgutil [-A] [-n times] -r
       aux/msgutil -m pid
    -s pid message -- send a message
    -r             -- receive a message
    -n times       -- repeat the send or receive times times
    -t tag         -- if sending, set message tag.
	-m pid         -- monitor pid, exiting when an event is received
    -A             -- if receiving, set MSGALLUSERS
```

## Adding this branch to your 9front repo

```
bind -ac /dist/9front /
cd /
{
	echo '[remote "mveety"]'
	echo '    url=https://github.com/mveety/9front-messaging.git'
	echo ''
} >> /.git/config
git/pull -f -u mveety
git/branch -b remotes/mveety/msgpassing -n msgpassing
```

You can also pull this anywhere and bind over `/sys/src/9`, `/sys/src/libc`, and `/sys/src/cmd/aux`.

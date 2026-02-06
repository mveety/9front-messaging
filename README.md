# Plan 9 Message Passing Kernel + libc

This repo contains a kernel which add four message passing system calls. Some examples are [here](https://github.com/mveety/p9messaging). The kernel portions are more or less stable (I am currently them on my file server and terminals), but the userspace portions are somewhat still up in the air. Use at your own risk.

## System Calls
* `int msg_send(ulong pid, void *message, uintptr message_size)`
	Asynchonously sends a message `message` to process `pid`. Returns 0 if successful and -1 otherwise. Sets errstr.

* `uintptr msg_wait(void)`

	Blocks until a message is ready to be received. Returns the size of the message to be received, 0 if there is a recoverable error, or (uintptr)-1 if there is a serious error. Sets errstr.

* `uintptr sys_msgrecv(void *buffer, uintptr buffer_size)`

	Receives the next available message if the buffer is large enough. Returns 0 on success and (uintptr)-1 on error. Sets errstr.

* `u32int sys_msgctl(int op, u32int ctl)`

	Gets or sets the processes message control bitmap. If op = `Mctlread`, ctl is ignored and sys_msgctl returns the current message control bitmap. If op = `Mctlwrite`, sys_msgctl tries to set the bitmap to `ctl` and returns the resulting bitmap. When `Mctlwrite` is set an error occured when `ctl` is not equal to the return value.

## Control Flags
* `MSGENABLE`

	By default a process can't receive messages until `MSGENABLE` is set.

* `MSGALLUSERS`

	By default a process will reject messages from other users. If this is set the process will accept them.

## User API (`msg.h`)

Standard message format (in rough C):
```
struct {
	u32int pid;
	u32int tag;
	u32int size;
	void payload[size];
}
```

Defined constants:
```
enum {
	Mctlread = 0,
	Mctlwrite = 1,

	MSGENABLE = (1<<0), /* allow process to receive messages */
//	MSGMONITOR = (1<<1), /* accept monitor messages */
//	MSGPROCS = (1<<2), /* accept process messages */

	MSGALLUSERS = (1<<3), /* allow messages from other users */
};
```

Defined enums and data structures:
```
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
```

* `int msgenable(void)`

	Sets the `MSGENABLE` flag.

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

	Creates a new standardly formatted message with `pid` = `getpid()`, `tag` = `tag`, and containing `data`.

* `Message* freemsg(Message *msg)`

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
usage: aux/msgutil [-R|-F] [-T tag] [-n times] -s pid message
       aux/msgutil [-R|-F|-D] [-A] [-n times] -r
    -s pid message -- send a message
    -r             -- receive a message
    -n times       -- repeat the send or receive times times
    -T tag         -- if sending, set message tag.
    -F             -- send or receive a standard formatted message
    -R             -- send or receive a raw unformatted message
    -D             -- receive a message and try to detect if its formatted
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

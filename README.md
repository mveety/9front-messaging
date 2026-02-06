# Plan 9 Message Passing Kernel + libc

This repo contains my kernel which add four message passing system calls. Some examples are [here](https://github.com/mveety/p9messaging).

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

* `int msgenable(void);`

* `int msgdisable(void);`

* `Mailbox* mailbox(void);`

* `void flushmailbox(Mailbox*);`

* `void freemailbox(Mailbox*);`

* `uvlong mailboxsz(Mailbox*);`

* `Message* message(int, void*, uintptr);`

* `Message* freemsg(Message*);`

* `Message* selectmsg(Mailbox*, Message*);`

* `int msgsend(int, Message*);`

* `Message* msgrecv(Mailbox*);`

* `Message* msgrecvfilter(Mailbox*, int*, uvlong);`

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

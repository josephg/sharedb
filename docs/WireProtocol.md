# Wire protocol

ShareDB uses a binary wire protocol, because its easy to do that sort of thing in C and I'm afraid of parsing strings.

The wire protocol is loosely based on [ShareJS's wire protocol](https://github.com/josephg/ShareJS/wiki/Wire-Protocol)

The server listens for incoming TCP connections on port 8766. (liable to change).

All integers are transmitted using little endian. All strings are sent as null terminated UTF-8.

The server and client exchange a series of messages over this connection. Each message starts with:

- Length (bytes) of remaining packet (uint32, little endian)
- Message type (one byte) OR'ed with optional docName flag (0x80) and error flag (0x40). Message types are listed below. The docName flag indicates that this packet replaces the currently *in use* docName.
- If the docName flag is set, the packet next lists the new *in use* docName (a string). The named document does not have to exist.
- If the error flag is set, the packet contains a string error message and nothing more.


## Message types:

- **Hello**: An initial introduction message exchange sent between server & client. Both parties exchange protocol version number.
- **Open**: Open the named document. All operations applied to an open document are sent to all clients with that document open. Clients can open as many documents as they like.
- **Close**: Close the specified document. No more operations will be sent. Note that there may already be operations in-flight for the named document.
- **Op**: Apply an operation to the named document
- **Cursor**: Move the user's cursor to the specified position
- **Get Ops**: Get historical operations for the specified document
- **Snapshot**: Get a snapshot of the document at its current version

### Hello

For now, this packet simply consists of a one byte protocol version number. If the version numbers don't match, the connection is dropped.

The client needs to send this packet immediately upon connecting. The server will respond in kind. This exchange must happen before any other packets are sent. (Though obviously, neither the client nor the server needs to wait on the hello packet being received before sending further messages).

### Open

The open message requests that the server stream the client any operations applied to a document. The server only sends operations applied to the document by other clients. An open document can be closed using the `close` message.

An open request can come in 2 different forms:

- Open a document at a specified version. This is used when you have a cached document snapshot and want to reopen the document. All operations applied since the specified version are sent to the client immediately.
- Send the client a document snapshot, and open a document at the version specified by the snapshot. This is the equivalent of sending a snapshot request, then opening the document at the snapshot's version.

Open requests can also be combined with `create` messages. In this case, the server will automatically create the document if it does not exist. In this case, the type must be specified.

Errors:

- __Doc does not exist__
- __Doc already open__: The document has already been opened.


### Submit op

Text ops are encoded in binary as follows:

- 2 bytes: unsigned short counting the number of components (N)
- Followed by N:
	- 1 byte op type (SKIP = 1, INSERT = 3, DELETE = 4)
	- If the op type is an insert, a null-terminated string. Otherwise 4 bytes of skip / delete size.

A submit op message is type 1, followed by the version (uint32) and then the op.

### Op acknowledgement

When a client sends an op to the server, the server replies to that client with an op acknowledgement. The acknowledgement simply contains the new server version number (uint32).
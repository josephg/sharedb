# Wire protocol

ShareDB uses a binary wire protocol, because its easy to do that sort of thing in C and I'm afraid of parsing strings.

The wire protocol is loosely based on [ShareJS's wire protocol](https://github.com/josephg/ShareJS/wiki/Wire-Protocol)

The server listens for incoming TCP connections on port 8766. (liable to change).

All integers are transmitted using little endian. All strings are sent in UTF-8 and null terminated.

The server and client exchange a series of messages over this connection. Each message starts with:

- Length (bytes) of remaining packet (uint32, little endian)
- Message type (one byte) OR'ed with optional docName flag (0x80). Message types are listed below. The docName flag indicates that this packet replaces the currently *in use* docName.
- If the docName flag is set, the packet next contains the new in use docName. The named document does not have to exist.


## Message types:

- **Open**: Open the named document. All operations applied to an open document are sent to all clients with that document open. Clients can open as many documents as they like.
- **Close**: Close the specified document. No more operations will be sent. Note that there may already be operations in-flight for the named document.
- **Op**: Apply an operation to the named document
- **Cursor**: Move the user's cursor to the specified position
- **Get Ops**: Get historical operations for the specified document
- **Snapshot**: Get a snapshot of the document at its current version

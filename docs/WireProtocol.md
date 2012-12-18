# Wire protocol

ShareDB uses a binary wire protocol, because its easy to do that sort of thing in C and I'm afraid of parsing strings.

The wire protocol is loosely based on [ShareJS's wire protocol](https://github.com/josephg/ShareJS/wiki/Wire-Protocol)

The server listens for incoming TCP connections on port 8766. (liable to change).

All integers are transmitted little endian. All strings are sent as null terminated UTF-8. The server does not currently work if you run it on a big endian system.

Immediately upon connecting, the client must first transmit the shared magic bytes (*"WAVE"*). The server will reply in kind. 

Once the magic bytes have been exchanged, the server and client can send message to each other. Each message starts with:

- Length (bytes) of remaining packet (uint32, little endian)
- Message type (one byte) OR'ed with optional docName flag (0x80) and error flag (0x40). Message types are listed below. The docName flag indicates that this packet replaces the currently *in use* docName.
- If the docName flag is set, the packet next lists the new *in use* docName (a string). The named document does not have to exist.
- If the error flag is set, the packet contains a string error message and nothing more.

## Message types:

- **Hello**: An initial introduction message exchange sent between server & client. Both parties exchange protocol version number.
- **Open**: Open the named document. All operations applied to an open document are sent to all clients with that document open. Clients can open as many documents as they like.
- **Close**: Close the specified document. No more operations will be sent. Note that there may already be operations in-flight for the named document.
- **Op**: Apply an operation to a document
- **Op ack**: Acknowledgement that the client's operation has been applied
- **Cursor**: Move the user's cursor to the specified position
- **Get Ops**: Get historical operations for the specified document
- **Snapshot**: Get a snapshot of the document at its current version

### Hello

For now, this packet simply consists of a one byte protocol version number (currently 0). If the version numbers don't match, the connection is dropped.

The client needs to send this packet immediately upon connecting. The server will respond in kind. This exchange must happen before any other packets are sent. (Though obviously, neither the client nor the server needs to wait on the hello packet being received before sending further messages).

This packet will eventually also contain auth tokens.

+ This now contains a client ID (uint32) as well

### Open

The open message requests that the server stream the client any operations applied to a document. This does not include the client's own operations.

An open document can be closed using the `close` message.

Open messages have two optional flags: _snapshot_ and _create_. These flags are OR'ed with the message type field.
- The _snapshot_ flag (`0x10`) indicates that the server should send the client a copy of the current document snapshot.
- The _create_ flag (`0x20`) indicates that the server should create the document if it does not already exist.

After the regular packet header, the open request contains the following fields:
- (*string*) Requested **docType**, or an empty string. If the create flag is set, this is the type of the created document. If the document already exists in the database but has a different type, a *Type mismatch* error is returned.
- (*uint32*) Document opening **version**. This is the version from which the server will stream operations to the client. Any operations between the requested version and the current version will be sent to the client immediately after the document is open as if they were new operations. If you don't care about the version (for example, you're creating a new document or you want a document snapshot), pass `UINT32_MAX` in the version field.

Practically, open requests usually come in one of 2 forms:

- The document is unknown, and potentially new. Usually you'll want to send both the _snapshot_ and _create_ flags. Send the requested type and `UINT32_MAX` for the version. The server will respond by opening (and potentially creating) the document and the client will receive a snapshot.
- You already have a cached document snapshot. Pass no flags. Send the known type & version number in the open request. The client should automatically catch up to the current document version.

Open requests can also be combined with `create` messages. In this case, the server will automatically create the document if it does not exist. In this case, the type must be specified.

Errors:

- __Doc does not exist__: Requested document does not exist and the _create_ flag was not set.
- __Doc already open__: The document is already open by this client.
- __Unknown type__: The requested type is unknown to the server
- __Type mismatch__: The document is not of the requested type. (Pass an empty string in the type field to specify any type)
- __Invalid version__: The requested version is newer than the document's actual version. (Use `UINT32_MAX` to open the most recent version of the document)
- __Cannot fetch historical snapshots__: The snapshot at a particular version was requested. You shouldn't request a document snapshot and specify a version in the same open request.

### Close

Close the current document.

Errors:

- __Doc is not open__: Document isn't open anyway.

### Submit op

A submit op message is used to send an operation to the server. The server will reply with an _op ack_ message.

A submit op request contains the following fields:
- (_uint32_) Document version at which the operation should be applied
- (op) The operation itself. The serialization format depends on the op type. Refer to the appendix below to see how ops of each type are serialised.

### Op acknowledgement

When a client sends an op to the server, the server replies to that client with an op acknowledgement. The acknowledgement simply contains the new server version number (uint32).

The acknowledgement will be in-order with surrounding ops from other clients.

----

# Serialization of common types

## Text

Text ops are encoded in binary as follows:

- (_uint16_): Number of operation components (N)
- Followed by N op components:
-- (_uint8_) Op type (SKIP = 1, INSERT = 3, DELETE = 4)
-- If the type is SKIP, _uint32_ skip size
-- If the type is INSERT, _string_ inserted text
-- If the type is DELETE, _uint32_ deleted region size

Text documents are simply serialized as strings.
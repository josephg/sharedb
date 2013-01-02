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

After the regular packet header, the open request contains the following fields:
- (*byte*) **Open flags**. See below for details.
- (*string*) Requested **docType**, or an empty string. If the create flag is set, this is the type of the created document. If the document already exists in the database but has a different type, a *Type mismatch* error is returned.
- (*uint32*) Document opening **version**. This is the version from which the server will stream operations to the client. Any operations between the requested version and the current version will be sent to the client immediately after the document is open as if they were new operations. If you don't care about the version (for example, you're creating a new document or you want a document snapshot), pass `UINT32_MAX` in the version field.

The open flags byte can contain:
- The _snapshot_ flag (`0x1`) indicates that the server should send the client a copy of the current document snapshot.
- The _create_ flag (`0x2`) indicates that the server should create the document if it does not already exist.
- The _track cursors_ flag (`0x4`) indicates the client should be told about user cursors in the document. See the section on cursors.
- The _has cursor_ flag (`0x8`) tells the server to place a cursor for this client in the document.

Practically, open requests usually come in one of 2 forms:

- The document is unknown, and potentially new. Usually you'll want to send both the _snapshot_ and _create_ flags. Send the requested type and `UINT32_MAX` for the version. The server will respond by opening (and potentially creating) the document and the client will receive a snapshot.
- You already have a cached document snapshot. Pass no flags. Send the known type & version number in the open request. The client should automatically catch up to the current document version.

Open requests can also be combined with `create` messages. In this case, the server will automatically create the document if it does not exist. In this case, the type must be specified.

An open request is always followed by an **open response**. The open response contains:

- (*byte*) **Open flags**. In a response, these flags indicate whether a snapshot is being given to the client and whether the document was actually created.
- (*uint32*) Open **version**. This is the version at which the client has opened the document. If the server is storing the document at a more recent version, the intervening operations will be sent to the client in the normal way after the open response.
- If the open response has the *snapshot flag* set (0x1), the open response then contains a document snapshot. This contains:
-- (*string*) **docType**: This is `text` for text documents.
-- (*uint64*) **ctime**: The document's creation time, given in MS since the epoch.
-- (*uint64*) **mtime**: The document's last modified time, given in MS since the epoch.
-- (*doc*) The actual document. The format of this data is type-dependant. Text documents are sent as a null-terminated string.

Errors:

- __Doc does not exist__: Requested document does not exist and the _create_ flag was not set.
- __Doc already open__: The document is already open by this client.
- __Unknown type__: The requested type is unknown to the server
- __Type mismatch__: The document is not of the requested type. (Pass an empty string in the type field to specify any type)
- __Invalid version__: The requested version is newer than the document's actual version. (Use `UINT32_MAX` to open the most recent version of the document)
- __Cannot fetch historical snapshots__: The snapshot at a particular version was requested. You shouldn't request a document snapshot and specify a version in the same open request.

Once a document is open, the server will send any submitted operations to the client. These operations have a packet type of _op_ and have the form of:

- (_uint32_) Document version at which the operation was applied
- (_uint32_) The ID of the client which submitted the operation
- (_op_) The operation itself. Serialization format depends on the op type. Refer to the appendix below.


### Close

Close the current document. This message has no payload. The server responds with another close message, again with no payload beyond the optional error message.

Be aware when writing clients that you may still receive operations between when the close request is sent and when the close response comes back from the server.

The server will not send any more operations to the client after sending the close response. Closing also removes the user's cursor from the document, and announces that to other uses who have the document open.

Errors:

- __Doc is not open__: Document isn't open anyway.

### Submit op

A submit op message is used to send an operation to the server. The server will reply with an _op ack_ message.

A submit op request contains the following fields:
- (_uint32_) Document version at which the operation should be applied
- (_op_) The operation itself. The serialization format depends on the op type. Refer to the appendix below to see how ops of each type are serialised.

### Op acknowledgement

When a client sends an op to the server, the server replies to that client with an op acknowledgement. The acknowledgement simply contains the new server version number (uint32).

The acknowledgement will be in-order with surrounding ops from other clients.

### Cursors

Each client on each document can have a cursor. For text documents, the cursor is simply an int32 specifying where the user's looking. For more complicated types like JSON, the cursor may be a full path.

Cursor data is transitory - that is, its not stored between server restarts or anything like that. Its not implemented yet, but I'd like to have cursors disappear after a timeout as well.

When a client opens a document, it can specify the *track cursors* flag. This requests cursor information from the server about the other connected clients.

There are three cursor information packet types - _set_, _remove_ and _replace all_. The cursor packet type is sent in the flags (high 4 bits) of the packet type byte.

- _Set_ (`0x10`) messages contain the cursor information for a single client. These are first sent by a client then rebroadcast by the server to all interested parties listening on a document.
- _Remove_ (`0x20`) messages remove the cursor information for one client from a document. These are usually sent when a client disconnects from the server or closes their open document.
- _Replace all_ (`0x30`) messages contain the cursor information for an entire document. These are usually sent by the server to a client with a newly open document.

By default, clients do not have cursors in the documents they edit. To get a visible cursor, a client must initialise their cursor position with a set message.

Cursors are automatically moved whenever a client submits an operation using the OT type's `transformCursor` function. When an operation is submitted, the user who submitted the operation's cursor is teleported to the site of the edit. Other user's cursors slide around appropriately.

#### Cursor set operations

When a user moves their cursor inside a document, their client should send a cursor set operation to the server specifying the new cursor position. The client also tells the server what version the user was looking at when they moved the cursor.

**Note:** This operation cannot be sent while the client has an editing operation in-flight to the server. This is because there is no valid version number which describes the state of the document on the client. The client should wait until the server acknowledges their operation before sending any new cursor information. The operation itself will move the user's cursor automatically, so while a user is typing you shouldn't be sending cursor set packets anyway.

Client to server cursor set operations have the following fields:

- (_uint32_) Version at which the cursor was moved. This will usually be the most recent document version the client knows about.
- (_cursor_) The cursor data itself. For the text type, this is simply a _uint32_.

The server usually doesn't reply to set cursor operations. It will when there's an error though.

Errors:

- __Doc is not open__: The client tried to set a cursor on a closed document
- __Cursor at future version__: The version specified in the cursor set message was invalid


Server to client set operations are slightly different:

- (_uint32_) ID of the client whose cursor has moved
- (_cursor_) The cursor data itself. For the text type, this is simply a _uint32_.

The version is implicitly the document's current version.

####  Cursor remove operations

Client originated cursor remove messages aren't currently supported.

The server will broadcast cursor remove messages which only contain the ID of the client whose cursor has moved (_uint32_).

#### Cursor replace all operations

Some time after a client opens a document, the server will send the client a cursor replace all operation. This operation will give the client a snapshot of all cursors currently in the document.

This packet looks like this:

- Pairs of:
-- (_uint32_) Client ID
-- (_cursor_) Cursor data
- (_uint32_) Zero.

The list is null client ID-terminated. (Zero is not a valid client ID).


----

# Serialization of common types

## Text

Text ops are encoded in binary as follows:

- List of:
-- (_uint8_) Op type (SKIP = 1, INSERT = 3, DELETE = 4)
-- If the type is SKIP, _uint32_ skip size
-- If the type is INSERT, _string_ inserted text
-- If the type is DELETE, _uint32_ deleted region size

The list is terminated with a null op type.

Text documents are simply serialized as strings.
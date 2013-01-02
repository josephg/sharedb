# This is a simple test of the binary writer code.

connect = require './client'


c = connect null, (err) ->
  console.error err if err
  console.log 'connected'
console.log c

docName = 'hi2'
c.auth()

c.once 'has id', (id) -> console.log "my id is #{id}"

c.open docName, type:'text', snapshot:true, create:true, (e, {v, type, snapshot}) ->
  return console.log "error opening document: #{e}" if e
  console.log "opened #{docName} at version #{v} type #{type} snapshot '#{snapshot}'"

  c.setCursor docName, v, [0,0]
  c.sendOp docName, v, ["#{v} "]

  #c.close docName

c.on 'op applied', (e, docName, v) ->
  return console.error "Could not apply op: #{e}" if e
  console.log "op applied on #{docName} -> version #{v}"

c.on 'op', (e, docName, v, op) ->
  console.log "Got an op on #{docName}. Now at v#{v}"
  console.log op

c.on 'cursor', (e, docName, data) ->
  console.log 'cursor data: ', data


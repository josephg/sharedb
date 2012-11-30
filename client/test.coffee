# This is a simple test of the binary writer code.

connect = require './client'


c = connect null, -> console.log 'connected'
console.log c

docName = 'hi2'
c.auth()
c.open docName, type:'text', snapshot:true, create:true, (e, {v, type, snapshot}) ->
  return console.log "error opening document: #{e}" if e
  console.log "opened #{docName} at version #{v} type #{type} snapshot '#{snapshot}'"

  c.sendOp docName, v, ["#{v} "]

  c.close docName

c.on 'op applied', (e, docName, v) ->
  return console.error "Could not apply op: #{e}" if e
  console.log "op applied on #{docName} -> version #{v}"

c.on 'op', (e, docName, v, op) ->
  console.log "Got an op on #{docName}. Now at v#{v}"
  console.log op



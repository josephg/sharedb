elem = document.getElementById 'pad'

#s = new BCSocket null, reconnect:true
sharejs.open 'hi', 'text2', (err, doc) ->
  return console.log "err: #{err}" if err
  window.d = doc
  doc.attach_textarea elem


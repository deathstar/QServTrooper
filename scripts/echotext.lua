function echotext(ci, text)
	client = clientinfo(ci)
	qserv.toserver("\f3" .. client:name() .. " said " .. text)
end

qserv.AddCallback(qserv.LUAEVENT_TEXT, "echotext")

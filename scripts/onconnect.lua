function greetclient(ci)
	client = clientinfo(ci)
	qserv.toserver("\f3Welcome " .. client:name())
end

qserv.AddCallback(qserv.LUAEVENT_CONNECTED, "greetclient")

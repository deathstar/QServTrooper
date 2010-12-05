function onsuicide(ci)
	client = clientinfo(ci)
	qserv.toserver("\f3" .. client:name() .. " Suicided!")
end

qserv.AddCallback(qserv.LUAEVENT_SUICIDE, "onsuicide")

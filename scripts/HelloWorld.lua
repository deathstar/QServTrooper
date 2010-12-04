colors = { "Green","Blue","Yellow","Red","Grey","Purple","Orange","White" }

function helloworld(numcol)
	for i = 0,numcol,1 do
		qserv.toserver("\f" .. i .. "Hello World! in " .. colors[i+1] .. "(" .. i .. ")")
	end
	msg = qserv.getclientname(0)
	qserv.toserver("\f3Client 0's name is " .. msg)
end

helloworld(7)

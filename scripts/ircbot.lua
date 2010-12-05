function IrcInit()
	socket = httpd.connect("irc.freenode.net", 6667)
	httpd.write(socket, "USER " .. ircnick .. " 0 * :" .. ircnick .. "\r\n")
	httpd.write(socket, "NICK " .. ircnick .. "\r\n")	
	httpd.write(socket, "JOIN " .. ircchannel .. "\r\n")
	for i = 0,1000,1 do
		length,data = httpd.read(socket)
		print(data)
	end
end

IrcInit()
		

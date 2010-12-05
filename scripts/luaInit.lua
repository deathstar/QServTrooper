function loadscript(filename)
	local f = assert(loadfile(filename))
	return f()
end

loadscript("scripts/configuration.lua")
loadscript("scripts/echotext.lua")
loadscript("scripts/onconnect.lua")
loadscript("scripts/onsuicide.lua")
--loadscript("scripts/ircbot.lua")

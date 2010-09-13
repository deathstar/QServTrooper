// frogmod.js

var info_update_millis = 100000; // server info rarely changes
var players_update_millis = 2000;
var status_update_millis = 2000;
var admin = false;
var states = [ 'Alive', 'Dead', 'Spawning', 'Lagged', 'Editing', 'Spectator' ];
var icons = [ 'fixit.png', 'ironsnout.png', 'ogro.png', 'inky.png', 'cannon.png' ];

function ajaxCall(uri, cb) {
	var xhr;
	if(window.XMLHttpRequest) { // code for IE7+, Firefox, Chrome, Opera, Safari
		xhr=new XMLHttpRequest();
	} else { // code for IE6, IE5
		xhr=new ActiveXObject("Microsoft.XMLHTTP");
	}
	xhr.onreadystatechange = cb;
	xhr.open('GET', uri + '?' + Date(), true);
	xhr.send();
}

function format_time(t, show_hours) {
	var seconds = Math.floor(t / 1000) % 60;
	if(seconds < 10) seconds = '0'+seconds; // zero pad x_x
	var minutes = Math.floor(t / 60000) % 60;
	if(minutes < 10) minutes = '0'+minutes;
	var hours = Math.floor(t / 3600000) % 24;
	if(hours < 10) hours = '0' + hours;
	var days = Math.floor(t / 86400000);
	return (days > 0 ? days + 'd ': '') + (show_hours?hours+':':(parseInt(hours)>0?hours+':':'')) + minutes + ':' + seconds;
}

//! written by quaker66:
//! Let's convert a Cube string colorification into proper HTML spans
//! Accepts just one argument, returns the html string.

function convert_cube_string(str) {
    var tmp = str; // some temp we'll return later
    var found = false; // have we found some colorz??!
    var pos = tmp.indexOf('\f'); // first occurence of \f
    while (pos != -1) { // loop till there is 0 occurs.
        var color = parseInt(tmp.substr(pos + 1, 1));
        if (found) { // if we've found something before, close the span on > 6 or any character, or close+create new on 0-6
            if (color <= 6 && color >= 0) { // yay! color exists. It means we'll want to close last span.
                tmp = tmp.replace(/\f[0-6]/, "</span><span class=\"color" + tmp.substr(pos + 1, 1) + "\">");
            } else { // There is no color. It means the num is higher than 6 (or any char).
                tmp = tmp.replace(/\f./, "</span>");
                found = false; // pretend we've never found anything
            }
        } else { // if it's first occurence and its num is bigger than 6 (or any char), simply ignore.
            if (color <= 6 && color >= 0) { // this means the num is 0-6. In that case, create our first span.
                tmp = tmp.replace(/\f[0-6]/, "<span class=\"color" + tmp.substr(pos + 1, 1) + "\">");
                found = true; // yay! we've found a color! (or again?)
            }
        }
        pos = tmp.indexOf('\f', pos + 1); // move to next position to feed while
    }
    // if we've found anything lately and didn't close it with \f > 6 (or \fCHAR), let's do it at the end
    if (found) tmp = tmp.replace(/$/, "</span>");

    // we can finally return our html string.
    return tmp;
}

function update_info() {
	ajaxCall('/info', function() {
		if(this.readyState == 4) {
			var div = document.getElementById('info');
			if(div) {
				if(this.status == 200) {
					var st = eval('('+this.responseText+')');
					if(st) {
						if(st.serverdesc && st.serverdesc != '') {
							var h1 = document.getElementById('title');
							if(h1) h1.innerHTML = convert_cube_string(st.serverdesc) + (admin?' admin':'') + ' console';
							document.title = st.serverdesc.replace(/\f./g, '') + (admin?' admin':'') + ' console';
						}

						var html = new Array();
						html.push('<ul>');
						html.push('<li>Server name: '+convert_cube_string(st.serverdesc)+'</li>');
						html.push('<li>Message of the day: '+convert_cube_string(st.servermotd)+'</li>');
						html.push('<li>Max players: '+st.maxclients+'</li>');
						html.push('</ul>');
						div.innerHTML = html.join('');
					} else div.innerHTML = 'Could not parse response';
				} else div.innerHTML = 'Error ' + (this.status ? this.status : '(unreachable)');
				setTimeout(update_status, status_update_millis);
			}
		}
	});
}

function update_status() {
	ajaxCall('/status', function() {
		if(this.readyState == 4) {
			var div = document.getElementById('status');
			if(div) {
				if(this.status == 200) {
					var st = eval('('+this.responseText+')');
					if(st) {
						var html = new Array();
						html.push('<ul>');
						html.push('<li>Uptime: '+format_time(st.totalmillis, true)+'</li>');
						html.push('<li>Mastermode: '+st.mastermodename+' ('+st.mastermode+')</li>');
						html.push('<li>Game mode: '+st.gamemodename+' ('+st.gamemode+')</li>');
						html.push('<li>Game time: '+format_time(st.gamemillis)+' / '+format_time(st.gamelimit)+'</li>');
						html.push('<li>Map: '+(st.map?'<b>'+st.map+'</b>':'<i>No map</i>')+'</li>');
						html.push('</ul>');
						div.innerHTML = html.join('');
					} else div.innerHTML = 'No players on the server.';
				} else div.innerHTML = 'Error ' + (this.status ? this.status : '(unreachable)');
				setTimeout(update_status, status_update_millis);
			}
		}
	});
}

function update_players_cb(xhr) {
	if(xhr.readyState == 4) {
		var div = document.getElementById('players');
		if(div) {
			if(xhr.status == 200) {
				var players = eval('(' + xhr.responseText + ')');
				if(players && players.length > 0) {
					var html = new Array();
					html.push('<table><tr><th>Cn</th><th>Name</th><th>Team</th><th>Status</th>');
					html.push('<th>Ping</th><th>Frags</th><th>Deaths</th><th>Teamkills</th><th>Shotdamage</th>');
					html.push('<th>Damage</th><th>Effectiveness</th>');
					html.push('<th>Uptime</th>'+(admin?'<th>Actions</th>':'')+'</tr>');
					for(p in players) {
						html.push('<tr'+(players[p].state == 5 ? ' class="spec"': (players[p].state == 1 ? ' class="dead"' :''))+'>');
						html.push('<td>'+players[p].clientnum+'</td>');
						html.push('<td class="privilege'+players[p].privilege+'"><img title="'+icons[players[p].playermodel].replace('.png', '')+'" src="'+icons[players[p].playermodel]+'">&nbsp;'+players[p].name+'</td>');
						html.push('<td>'+players[p].team+'</td>');
						html.push('<td>'+states[players[p].state]+'</td>');
						html.push('<td>'+players[p].ping+'</td>');
						html.push('<td>'+players[p].frags+'</td>');
						html.push('<td>'+players[p].deaths+'</td>');
						html.push('<td>'+players[p].teamkills+'</td>');
						html.push('<td>'+players[p].shotdamage+'</td>');
						html.push('<td>'+players[p].damage+'</td>');
						html.push('<td>'+players[p].effectiveness+'</td>');
						html.push('<td>'+format_time(players[p].connectmillis)+'</td>');
						if(admin) {
							html.push('<td><a href="?kick='+players[p].clientnum+'" title="Kick">[K]</a> ');
							if(players[p].state == 5)
								html.push('<a href="?unspec='+players[p].clientnum+'" title="Unspec">[U]</a> ');
							else
								html.push('<a href="?spec='+players[p].clientnum+'" title="Spec">[S]</a> ');
							if(players[p].privilege)
								html.push('<a href="?takemaster=true" title="Take master">[T]</a> ');
							else
								html.push('<a href="?givemaster='+players[p].clientnum+'" title="Give master">[M]</a> ');
							html.push('</td>');
						}
						html.push('</tr>');
					}
					html.push('</table>');
					div.innerHTML = html.join('');
				} else div.innerHTML = 'No players on the server.';
			} else div.innerHTML = 'Error ' + (xhr.status ? xhr.status : '(unreachable)');
			setTimeout(update_players, players_update_millis);
		}
	}
}

function update_players(admin) {
	ajaxCall('/players', admin ? function() { update_players_cb(this, true); } : function() { update_players_cb(this, false); });
}

function init() {
	update_info();
	update_status();
	update_players();
}

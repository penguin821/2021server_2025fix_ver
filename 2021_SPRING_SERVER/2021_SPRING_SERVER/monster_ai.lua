myid = -1
count = 0
isBye = false

function set_o_id(x)
	myid = x
end

function event_player_move(p_id)
	p_x = API_get_x(p_id)
	p_y = API_get_y(p_id)
	m_x = API_get_x(myid)
	m_y = API_get_y(myid)
	if (p_x == m_x) then
		if (p_y == m_y) then
			API_end_message(myid, p_id, "Attacked !")
		end
	end
end

function event_npc_bye()
	count = count + 1
	if (count > 2) then
		count = 0
		isBye = false
		API_send_message(myid, p_id, "BYE")
	end
end
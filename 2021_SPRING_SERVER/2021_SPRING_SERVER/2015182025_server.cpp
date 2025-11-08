#include <iostream>
#include <map>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <atomic>
#include <ctime>
#include <chrono>

#include <WS2tcpip.h>
#include <MSWSock.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "lua54.lib")

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

using namespace std;
using namespace chrono;

#include "2021_SPRING_PROTOCOL.h"
#include "Struct.h"

constexpr int NUM_THREADS = 4;
constexpr short HP_MAX = 100;

priority_queue <timer_event> timer_queue;
mutex timer_lock;

unordered_map<int, SESSION> players;
SOCKET listenSocket;
HANDLE h_iocp;

void display_error(const char* msg, int err_no)
{
	WCHAR* lpMsgBuf;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	cout << msg;
	wcout << L" 에러 " << lpMsgBuf << endl;
	LocalFree(lpMsgBuf);
}

void add_event(int id, OP_TYPE ev, int delay_ms)
{
	timer_event event{ id, ev, system_clock::now() + milliseconds(delay_ms), 0 };
	timer_lock.lock();
	timer_queue.push(event);
	timer_lock.unlock();
}

void npc_awake(int id)
{
	S_STATE old_status = STATE_FREE;
	if (true == atomic_compare_exchange_strong(&players[id].m_state, &old_status, STATE_INGAME))
		add_event(id, OP_NPC_MOVE, 1000);
}

bool is_npc(int id)
{
	return id >= NPC_ID_START;
}

bool is_aggro(int id)
{
	return id >= NPC_AGGRO_START;
}

bool can_see(int id_a, int id_b)
{
	return VIEW_RADIUS * VIEW_RADIUS >= (players[id_a].m_x - players[id_b].m_x)
		* (players[id_a].m_x - players[id_b].m_x)
		+ (players[id_a].m_y - players[id_b].m_y)
		* (players[id_a].m_y - players[id_b].m_y);
}

void send_packet(int p_id, void* buf)
{
	EX_OVER* s_over = new EX_OVER;

	unsigned char packet_size = reinterpret_cast<unsigned char*>(buf)[0];
	s_over->m_op = OP_SEND;
	memset(&s_over->m_over, 0, sizeof(s_over->m_over));
	memcpy(s_over->m_netbuf, buf, packet_size);
	s_over->m_wsabuf[0].buf = reinterpret_cast<char*>(s_over->m_netbuf);
	s_over->m_wsabuf[0].len = packet_size;

	WSASend(players[p_id].m_s, s_over->m_wsabuf, 1, 0, 0, &s_over->m_over, 0);
}

void send_login_ok(int p_id)
{
	sc_packet_login_ok packet;
	packet.HP = 100;
	packet.id = p_id;
	packet.LEVEL = 3;
	packet.EXP = 0;
	packet.size = sizeof(packet);
	packet.type = SC_LOGIN_OK;
	packet.x = players[p_id].m_x;
	packet.y = players[p_id].m_y;
	send_packet(p_id, &packet);
}

void send_login_fail(int p_id)
{
	sc_packet_login_fail packet;
	packet.size = sizeof(packet);
	packet.type = SC_LOGIN_FAIL;
	send_packet(p_id, &packet);
}

void send_move_packet(int c_id, int p_id)
{
	sc_packet_position packet;
	packet.id = p_id;
	packet.size = sizeof(packet);
	packet.type = SC_POSITION;
	packet.x = players[p_id].m_x;
	packet.y = players[p_id].m_y;
	packet.move_time = players[p_id].last_move_time;
	send_packet(c_id, &packet);
}

void send_add_object(int c_id, int p_id)
{
	sc_packet_add_object packet;
	packet.id = p_id;
	packet.size = sizeof(packet);
	packet.type = SC_ADD_OBJECT;
	packet.x = players[p_id].m_x;
	packet.y = players[p_id].m_y;
	strcpy_s(packet.name, players[p_id].m_name);
	packet.EXP = players[p_id].m_exp;
	packet.HP = players[p_id].m_hp;
	if (true == is_npc(p_id))
	{
		if (true == is_aggro(p_id))
			packet.obj_class = 2;
		else
			packet.obj_class = 1;
	}
	else
		packet.obj_class = 0;
	send_packet(c_id, &packet);
}

void send_chat(int c_id, int p_id, const char* mess)
{
	sc_packet_chat packet;
	packet.id = p_id;
	packet.size = sizeof(packet);
	packet.type = SC_CHAT;
	strcpy_s(packet.message, mess);
	send_packet(c_id, &packet);
}

void send_update_packet(int p_id)
{
	sc_packet_update p;
	p.size = sizeof(p);
	p.type = SC_UPDATE;
	p.HP = players[p_id].m_hp;
	p.LEVEL = players[p_id].m_level;
	p.EXP = players[p_id].m_exp;

	send_packet(p_id, &p);
}

void send_remove_object(int c_id, int p_id)
{
	sc_packet_remove_object packet;
	packet.id = p_id;
	packet.size = sizeof(packet);
	packet.type = SC_REMOVE_OBJECT;
	send_packet(c_id, &packet);
}

void player_move(int p_id, char dir)
{
	short x = players[p_id].m_x;
	short y = players[p_id].m_y;
	switch (dir)
	{
	case 0:
		if (y > 0)
			y--;
		break;
	case 3:
		if (x < (WORLD_WIDTH - 1))
			x++;
		break;
	case 1:
		if (y < (WORLD_HEIGHT - 1))
			y++;
		break;
	case 2:
		if (x > 0)
			x--;
		break;
	}
	players[p_id].m_x = x;
	players[p_id].m_y = y;

	players[p_id].m_vl.lock();
	unordered_set <int> old_vl = players[p_id].m_viewlist;
	players[p_id].m_vl.unlock();

	send_move_packet(p_id, p_id);

	unordered_set <int> new_vl;
	for (auto& cl : players)
	{
		if (p_id == cl.second.m_id)
			continue;
		if (!can_see(p_id, cl.second.m_id))
			continue;
		if (is_npc(cl.second.m_id))
		{
			if (STATE_FREE == cl.second.m_state)
			{
				npc_awake(cl.second.m_id);
				new_vl.insert(cl.second.m_id);
			}
			else if (STATE_INGAME == cl.second.m_state)
			{
				new_vl.insert(cl.second.m_id);
			}
			EX_OVER* ex_over = new EX_OVER;
			ex_over->m_op = OP_PLAYER_MOVE;
			ex_over->m_target_id = p_id;
			PostQueuedCompletionStatus(h_iocp, 1, cl.second.m_id, &ex_over->m_over);
		}
		else
		{
			cl.second.m_lock.lock();
			if (STATE_INGAME != cl.second.m_state) {
				cl.second.m_lock.unlock();
				continue;
			}
			else
			{
				new_vl.insert(cl.second.m_id);
				cl.second.m_lock.unlock();
			}
		}
	}

	for (auto pl : new_vl)
	{
		players[p_id].m_vl.lock();
		if (0 == players[p_id].m_viewlist.count(pl))
		{
			// 1. 새로 시야에 들어오는 플레이어 처리
			players[p_id].m_viewlist.insert(pl);
			players[p_id].m_vl.unlock();
			send_add_object(p_id, pl);
			if (true == is_npc(pl)) continue;
			players[pl].m_vl.lock();
			if (0 == players[pl].m_viewlist.count(p_id))
			{
				players[pl].m_viewlist.insert(p_id);
				players[pl].m_vl.unlock();
				send_add_object(pl, p_id);
			}
			else
			{
				players[pl].m_vl.unlock();
				send_move_packet(pl, p_id);
			}
		}
		else {
			// 2. 처음 부터 끝까지 시야에 존재하는 플레이어 처리
			players[p_id].m_vl.unlock();
			if (true == is_npc(p_id)) continue;
			players[pl].m_vl.lock();
			if (0 == players[pl].m_viewlist.count(p_id)) {
				players[pl].m_viewlist.insert(p_id);
				players[pl].m_vl.unlock();
				send_add_object(pl, p_id);
			}
			else {
				players[pl].m_vl.unlock();
				send_move_packet(pl, p_id);
			}
		}
	}

	// 3. 시야에서 벗어나는 플레이어 처리
	for (auto pl : old_vl) {
		if (0 == new_vl.count(pl)) {
			players[p_id].m_vl.lock();
			players[p_id].m_viewlist.erase(pl);
			players[p_id].m_vl.unlock();
			send_remove_object(p_id, pl);
			if (true == is_npc(pl)) continue;
			players[pl].m_vl.lock();
			if (0 != players[pl].m_viewlist.count(p_id)) {
				players[pl].m_viewlist.erase(p_id);
				players[pl].m_vl.unlock();
				send_remove_object(pl, p_id);
			}
			else
				players[pl].m_vl.unlock();
		}
	}
}

void player_attack(int p_id)
{
	players[p_id].m_max_exp = pow(2, players[p_id].m_level - 1) * 100;

	for (auto vl : players[p_id].m_viewlist)
	{
		if (is_npc(vl) && players[vl].isAlive)
		{
			if ((abs(players[p_id].m_x - players[vl].m_x) == 1 && players[p_id].m_y == players[vl].m_y) || // 좌우에 있는 적
				(abs(players[p_id].m_y - players[vl].m_y) == 1 && players[p_id].m_x == players[vl].m_x)) // 상하에 있는 적
			{
				char mess[MAX_STR_LEN];
				sprintf_s(mess, "%s attacked %s with %d damage!", players[p_id].m_name, players[vl].m_name, players[p_id].m_level * 5);
				players[vl].m_hp -= players[p_id].m_level * 10;
				send_chat(p_id, vl, mess);
			}

			if (players[vl].m_hp <= 0) // 엔피씨가 죽으면
			{
				players[vl].isAlive = false;
				players[p_id].m_lock.lock();
				players[p_id].m_exp += 50; // players[vl].m_level * players[vl].m_level * 2
				players[p_id].m_lock.unlock();
				if (players[p_id].m_exp >= players[p_id].m_max_exp)
				{
					players[p_id].m_lock.lock();
					players[p_id].m_level += 1;
					players[p_id].m_lock.unlock();
					players[p_id].m_exp = 0;
				}
				char mess[MAX_STR_LEN];
				if (is_aggro(vl))
					sprintf_s(mess, "Killed %s, got %d EXP!", players[vl].m_name,
						players[vl].m_level * players[vl].m_level * 2 * 2);
				else
					sprintf_s(mess, "Killed %s, got %d EXP!", players[vl].m_name,
						players[vl].m_level * players[vl].m_level * 2);
				send_chat(p_id, vl, mess);
				send_update_packet(p_id);
				players[vl].m_x = WORLD_WIDTH * 10;
				add_event(vl, OP_NPC_DEAD, 30000);
			}
		}
	}
}

void process_packet(int p_id, unsigned char* packet)
{
	switch (packet[1])
	{
	case CS_LOGIN:
	{
		cs_packet_login* p = reinterpret_cast<cs_packet_login*>(packet);

		bool isLogin = true;
		for (int i = 0; i < MAX_USER; ++i)
		{
			if (STATE_FREE == players[i].m_state) continue;
			if (p_id == i) continue;
			if (0 == strcmp(players[i].m_name, p->player_id))
			{
				isLogin = false;
				send_login_fail(p_id);
				break;
			}
		}
		if (!isLogin)
			break;

		players[p_id].m_lock.lock();
		strcpy_s(players[p_id].m_name, p->player_id);
		players[p_id].m_x = rand() % WORLD_WIDTH;
		players[p_id].m_y = rand() % WORLD_HEIGHT;
		send_login_ok(p_id);
		players[p_id].m_state = STATE_INGAME;
		players[p_id].m_lock.unlock();

		//cout << "num [" << p_id << "] : " << players[p_id].m_name << " connected.\n";

		for (auto& p : players)
		{
			if (p.second.m_id == p_id) continue;
			if (!can_see(p_id, p.second.m_id)) continue;

			p.second.m_lock.lock();
			if (p.second.m_state != STATE_INGAME && !is_npc(p.second.m_id))
			{
				p.second.m_lock.unlock();
				continue;
			}
			players[p_id].m_vl.lock();
			players[p_id].m_viewlist.insert(p.second.m_id);
			players[p_id].m_vl.unlock();
			send_add_object(p_id, p.second.m_id);

			// 남는거 npc랑 온라인플레이어
			if (false == is_npc(p.second.m_id))
			{
				p.second.m_vl.lock();
				p.second.m_viewlist.insert(p_id);
				p.second.m_vl.unlock();
				send_add_object(p.second.m_id, p_id);
			}
			else // 시야범위 안에 npc가 있으면
			{
				if (STATE_FREE == p.second.m_state)
				{
					npc_awake(p.second.m_id);
				}
			}
			p.second.m_lock.unlock();
		}
	}
	break;
	case CS_MOVE: 
	{
		cs_packet_move* move_packet = reinterpret_cast<cs_packet_move*>(packet);
		players[p_id].last_move_time = move_packet->move_time;
		player_move(p_id, move_packet->direction);
	}
	break;
	case CS_ATTACK:
	{
		cs_packet_attack* move_packet = reinterpret_cast<cs_packet_attack*>(packet);
		player_attack(p_id);
	}
	break;
	default:
		cout << "Unknown Packet Type [" << packet[1] << "] Error\n";
		::exit(-1);
	}
}

void do_recv(int p_id)
{
	SESSION& pl = players[p_id];
	EX_OVER& r_over = pl.m_recv_over;
	// r_over.m_op = OP_RECV;
	memset(&r_over.m_over, 0, sizeof(r_over.m_over));
	r_over.m_wsabuf[0].buf = reinterpret_cast<CHAR*>(r_over.m_netbuf) + pl.m_prev_recv;
	r_over.m_wsabuf[0].len = MAX_BUFFER - pl.m_prev_recv;
	DWORD r_flag = 0;
	WSARecv(pl.m_s, r_over.m_wsabuf, 1, 0, &r_flag, &r_over.m_over, 0);
}

int get_new_player_id()
{
	for (int i = 0; i < MAX_USER; ++i) {
		players[i].m_lock.lock();
		if (STATE_FREE == players[i].m_state) {
			players[i].m_state = STATE_CONNECTED;
			players[i].m_vl.lock();
			players[i].m_viewlist.clear();
			players[i].m_vl.unlock();
			players[i].m_lock.unlock();
			return i;
		}
		players[i].m_lock.unlock();
	}
	return -1;
}

void do_accept(SOCKET s_socket, EX_OVER* a_over)
{
	SOCKET c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	memset(&a_over->m_over, 0, sizeof(a_over->m_over));
	DWORD num_byte;
	int addr_size = sizeof(SOCKADDR_IN) + 16;
	a_over->m_csocket = c_socket;
	BOOL ret = AcceptEx(s_socket, c_socket, a_over->m_netbuf, 0, addr_size, addr_size, &num_byte, &a_over->m_over);
	if (FALSE == ret) {
		int err = WSAGetLastError();
		if (WSA_IO_PENDING != err) {
			display_error("AcceptEx : ", err);
			::exit(-1);
		}
	}
}

void disconnect(int p_id)
{

	players[p_id].m_lock.lock();
	unordered_set <int> old_vl = players[p_id].m_viewlist;
	players[p_id].m_state = STATE_CONNECTED;
	closesocket(players[p_id].m_s);
	players[p_id].m_state = STATE_FREE;
	players[p_id].m_lock.unlock();
	cout << p_id << " disconnect\n";
	// players.erase(p_id);
	for (auto& cl : old_vl) {
		if (true == is_npc(cl)) continue;
		players[cl].m_lock.lock();
		if (STATE_INGAME != players[cl].m_state) {
			players[cl].m_lock.unlock();
			continue;
		}
		send_remove_object(players[cl].m_id, p_id);
		players[cl].m_lock.unlock();
	}
}

void do_move_npc(int id)
{
	unordered_set <int> old_vl;
	for (auto& pl : players) {
		if (true == is_npc(pl.second.m_id)) continue;
		if (STATE_INGAME != pl.second.m_state) continue;
		if (true == can_see(id, pl.second.m_id))
			old_vl.insert(pl.second.m_id);
	}

	int x = players[id].m_x;
	int y = players[id].m_y;

	if (is_aggro(id)) // 어그로 몬스터가 아닐때
	{
		for (int i = 0; i < MAX_USER; ++i)
		{
			if (can_see(i, id))
			{
				if (players[i].m_hp <= 0) // 죽거나 부활대기 상태면 무시
					break;
				if (y < players[i].m_y) // y축 먼저 접근
					++y;
				else if (y > players[i].m_y)
					--y;
				else
				{
					if (x < players[i].m_x) // 그다음 x축 접근
						++x;
					else if (x > players[i].m_x)
						--x;
					else // 겹치면 랜덤무브
					{
						switch (rand() % 4)
						{
						case 0: if (x > 0) x--; break;
						case 1: if (x < (WORLD_WIDTH - 1)) x++; break;
						case 2: if (y > 0) y--; break;
						case 3: if (y < (WORLD_HEIGHT - 1)) y++; break;
						}
					}
				}
				break; // 인덱스 순으로 가장 가까운 플레이어한테 접근하기
			}
		}
	}
	else // 어그로 몬스터 일때
	{
		switch (rand() % 4)
		{
		case 0: if (x > 0) x--; break;
		case 1: if (x < (WORLD_WIDTH - 1)) x++; break;
		case 2: if (y > 0) y--; break;
		case 3: if (y < (WORLD_HEIGHT - 1)) y++; break;
		}
	}
	players[id].m_x = x;
	players[id].m_y = y;

	
	unordered_set <int> new_vl;
	for (auto& pl : players) {
		if (true == is_npc(pl.second.m_id)) continue;
		if (STATE_INGAME != pl.second.m_state) continue;
		if (true == can_see(id, pl.second.m_id))
			new_vl.insert(pl.second.m_id);
	}

	for (auto pl : old_vl) {
		if (0 == new_vl.count(pl)) { // 시야에서 벗어남
			players[pl].m_vl.lock();
			players[pl].m_viewlist.erase(id);
			players[pl].m_vl.unlock();
			send_remove_object(pl, id);
		}
		else {
			send_move_packet(pl, id);
			EX_OVER* ex_over = new EX_OVER; // 엔피씨가 플레이어 시야 내에서 움직일때마다 플레이어 좌표체크
			ex_over->m_op = OP_PLAYER_MOVE;
			ex_over->m_target_id = pl;
			PostQueuedCompletionStatus(h_iocp, 1, id, &ex_over->m_over);

		}
	}
	for (auto pl : new_vl) {
		if (0 == old_vl.count(pl)) {
			players[pl].m_vl.lock();
			players[pl].m_viewlist.insert(id);
			players[pl].m_vl.unlock();
			send_add_object(pl, id);
		}
	}
}

void worker()
{
	while (true) 
	{
		DWORD num_byte;
		ULONG_PTR i_key;
		WSAOVERLAPPED* over;
		BOOL ret = GetQueuedCompletionStatus(h_iocp, &num_byte, &i_key, &over, INFINITE);
		int key = static_cast<int> (i_key);
		if (FALSE == ret) {
			int err = WSAGetLastError();
			display_error("GQCS : ", err);
			disconnect(key);
			continue;
		}
		EX_OVER* ex_over = reinterpret_cast<EX_OVER*>(over);

		players[key].m_lock.lock();
		if (!is_npc(key) && !players[key].isRecovery && players[key].m_hp < HP_MAX)
		{
			players[key].isRecovery = true;
			add_event(key, OP_RECOVERY, 5000);
		}
		players[key].m_lock.unlock();

		switch (ex_over->m_op) {
		case OP_RECV:
		{
			unsigned char* ps = ex_over->m_netbuf;
			int remain_data = num_byte + players[key].m_prev_recv;
			while (remain_data > 0) {
				int packet_size = ps[0];
				if (packet_size > remain_data) break;
				process_packet(key, ps);
				remain_data -= packet_size;
				ps += packet_size;
			}
			if (remain_data > 0)
				memcpy(ex_over->m_netbuf, ps, remain_data);
			players[key].m_prev_recv = remain_data;
			do_recv(key);
		}
		break;
		case OP_SEND:
			if (num_byte != ex_over->m_wsabuf[0].len)
				disconnect(key);
			delete ex_over;
			break;
		case OP_ACCEPT:
		{
			SOCKET c_socket = ex_over->m_csocket;
			int p_id = get_new_player_id();
			if (-1 == p_id) {
				closesocket(c_socket);
				do_accept(listenSocket, ex_over);
				continue;
			}

			SESSION& n_s = players[p_id];
			n_s.m_lock.lock();
			n_s.m_state = STATE_CONNECTED;
			n_s.m_id = p_id;
			n_s.m_prev_recv = 0;
			n_s.m_exp = 0;
			n_s.m_hp = 100;
			n_s.m_level = 1;
			n_s.m_recv_over.m_op = OP_RECV;
			n_s.m_s = c_socket;
			n_s.isAlive = true;
			n_s.m_x = rand() % WORLD_WIDTH;
			n_s.m_y = rand() % WORLD_HEIGHT;
			n_s.m_fx = n_s.m_x;
			n_s.m_fy = n_s.m_y;
			n_s.m_name[0] = 0;
			n_s.m_lock.unlock();

			CreateIoCompletionPort(reinterpret_cast<HANDLE>(c_socket), h_iocp, p_id, 0);

			do_recv(p_id);
			do_accept(listenSocket, ex_over);
			//cout << "New CLient [" << p_id << "] connected.\n";
		}
		break;
		case OP_NPC_MOVE:
		{
			do_move_npc(key);
			bool keep_alive = false;
			for (int i = 0; i < NPC_ID_START; ++i) // 모든 플레이어에 대해서
			{
				if (true == can_see(key, i)) // 플레이어 시야범위 안에 있고
				{
					if (STATE_INGAME == players[i].m_state) // 접속해있는 플레이어일때
					{
						keep_alive = true; // npc가 활성화 되어있다
						break;
					}
				}
			}

			if (true == keep_alive) // 처음 만난 플레이어 기준으로 활성화 중복 방지
				add_event(key, OP_NPC_MOVE, 1000);
			else
				players[key].m_state = STATE_FREE; // 주변에 플레이어 없으면 다시 슬립 상태

			delete ex_over;
		}
		break;
		case OP_PLAYER_MOVE:
		{
			players[key].sl.lock();
			lua_State* L = players[key].L;
			lua_getglobal(L, "event_player_move");
			lua_pushnumber(L, ex_over->m_target_id);
			lua_pcall(L, 1, 0, 0);
			players[key].sl.unlock();

			delete ex_over;
		}
		break;
		case OP_RECOVERY:
		{
			players[key].m_lock.lock();
			if (players[key].m_hp <= 0) 
			{
				players[key].m_lock.unlock();
				delete ex_over;
				return;
			}
			else
			{
				players[key].m_hp += (HP_MAX / 10);
				if (players[key].m_hp >= HP_MAX)
				{
					players[key].m_hp = HP_MAX;
					
				}
				players[key].isRecovery = false;
				players[key].m_lock.unlock();
				send_update_packet(key);
			}
			delete ex_over;
		}
		break;
		case OP_NPC_DEAD:
		{
			players[key].m_hp = 100;
			players[key].m_level = 1 + rand() % 10;
			players[key].m_x = rand() % WORLD_WIDTH;
			players[key].m_y = rand() % WORLD_HEIGHT;
			players[key].isAlive = true;
			delete ex_over;
		}
		break;
		case OP_PLAYER_DEATH:
		{
			players[key].m_hp = HP_MAX;
			players[key].m_x = players[key].m_fx;
			players[key].m_y = players[key].m_fy;
			send_add_object(key, key);
			send_update_packet(key);
			delete ex_over;
		}
		break;
		default: cout << "Unknown GQCS Error!\n";
			::exit(-1);
		}
	}
}

void do_ai()
{
	while (true) {
		auto start_t = chrono::system_clock::now();
		for (int i = NPC_ID_START; i < MAX_USER; ++i) {
			bool ai_on = false;
			for (int pl = 0; pl < NPC_ID_START; ++pl) {
				if (players[pl].m_state != STATE_INGAME) continue;
				if (can_see(i, pl)) {
					ai_on = true;
					break;
				}
			}
			if (true == ai_on)
				do_move_npc(i);
		}
		auto end_t = chrono::system_clock::now();
		auto duration = end_t - start_t;
		cout << "AI Exec Time : " <<
			chrono::duration_cast<chrono::milliseconds>(duration).count()
			<< "ms.\n";
		this_thread::sleep_for(start_t + chrono::seconds(1) - end_t);
	}
}

void timer()
{
	using namespace chrono;

	for (;;)
	{
		timer_lock.lock();
		if (true == timer_queue.empty()) // 타이머 큐에 아무것도 없으면
		{
			timer_lock.unlock();
			this_thread::sleep_for(10ms);
		}
		else
		{
			if (timer_queue.top().exec_time > system_clock::now())
			{
				timer_lock.unlock();
				this_thread::sleep_for(10ms);
			}
			else
			{
				auto ev = timer_queue.top();
				timer_queue.pop();
				timer_lock.unlock();

				switch (ev.event_type)
				{
				case OP_NPC_MOVE:
				{
					EX_OVER* ex_over = new EX_OVER;
					ex_over->m_op = OP_NPC_MOVE;
					PostQueuedCompletionStatus(h_iocp, 1, ev.object_id, &ex_over->m_over);
				}
				break;
				case OP_RECOVERY:
				{
					EX_OVER* ex_over = new EX_OVER;
					ex_over->m_op = OP_RECOVERY;
					PostQueuedCompletionStatus(h_iocp, 1, ev.object_id, &ex_over->m_over);
				}
				break;
				case OP_NPC_DEAD:
				{
					EX_OVER* ex_over = new EX_OVER;
					ex_over->m_op = OP_NPC_DEAD;
					PostQueuedCompletionStatus(h_iocp, 1, ev.object_id, &ex_over->m_over);
				}
				break;
				case OP_PLAYER_DEATH:
				{
					EX_OVER* ex_over = new EX_OVER;
					ex_over->m_op = OP_PLAYER_DEATH;
					PostQueuedCompletionStatus(h_iocp, 1, ev.object_id, &ex_over->m_over);
				}
				break;
				}
			}
		}
	}
}

int API_get_x(lua_State* L)
{
	int obj_id = lua_tonumber(L, -1);
	lua_pop(L, 2);
	int x = players[obj_id].m_x;
	lua_pushnumber(L, x);
	return 1;
}

int API_get_y(lua_State* L)
{
	int obj_id = lua_tonumber(L, -1);
	lua_pop(L, 2);
	int y = players[obj_id].m_y;
	lua_pushnumber(L, y);
	return 1;
}

int API_send_message(lua_State* L)
{
	int obj_id = lua_tonumber(L, -3);
	int p_id = lua_tonumber(L, -2);
	const char* mess = lua_tostring(L, -1);
	lua_pop(L, 4);

	if (is_aggro(obj_id))
		players[p_id].m_hp -= 10;
	else
		players[p_id].m_hp -= 5;

	if (0 >= players[p_id].m_hp)
	{
		players[p_id].m_lock.lock();
		players[p_id].m_hp = 0;
		players[p_id].m_exp /= 2;
		players[p_id].isAlive = false;
		players[p_id].m_lock.unlock();
		send_remove_object(p_id, p_id);
		add_event(p_id, OP_PLAYER_DEATH, 1000);
	}
	send_update_packet(p_id);
	send_chat(p_id, obj_id, mess);
	return 0;
}

int main()
{
	wcout.imbue(locale("korean"));

	for (int i = 0; i < MAX_USER; ++i)
	{
		auto& pl = players[i];
		pl.m_id = i;
		pl.m_state = STATE_FREE;
		pl.last_move_time = 0;
		pl.isRecovery = false;
		pl.isAlive = false;
		// pl.m_viewlist.clear();
		if (is_npc(i))
		{
			pl.m_x = rand() % WORLD_WIDTH;
			pl.m_y = rand() % WORLD_HEIGHT;
			pl.m_state = STATE_FREE;
			pl.isAlive = true;
			pl.m_level = 1 + rand() % 10;
			if (is_aggro(i))
			{
				sprintf_s(pl.m_name, "WOLF Lv%d_%d", pl.m_level, i);
				pl.m_hp = 300;
			}
			else
			{
				sprintf_s(pl.m_name, "FOX Lv%d_%d", pl.m_level, i);
				pl.m_hp = 100;
			}
			pl.L = luaL_newstate();
			luaL_openlibs(pl.L);
			luaL_loadfile(pl.L, "monster_ai.lua");
			lua_pcall(pl.L, 0, 0, 0);

			lua_getglobal(pl.L, "set_o_id");
			lua_pushnumber(pl.L, i);
			lua_pcall(pl.L, 1, 0, 0);

			lua_register(pl.L, "API_send_message", API_send_message);
			lua_register(pl.L, "API_get_x", API_get_x);
			lua_register(pl.L, "API_get_y", API_get_y);
		}
	}
	cout << "Init finish\n";

	srand(time(NULL));
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	listenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN serverAddr;
	memset(&serverAddr, 0, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
	listen(listenSocket, SOMAXCONN);

	h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(listenSocket), h_iocp, 100000, 0);

	EX_OVER a_over;
	a_over.m_op = OP_ACCEPT;
	do_accept(listenSocket, &a_over);

	vector <thread> worker_threads;
	for (int i = 0; i < NUM_THREADS; ++i)
		worker_threads.emplace_back(worker);

	thread timer_thread{ timer };
	timer_thread.join();

	for (auto& th : worker_threads) th.join();
	closesocket(listenSocket);
	WSACleanup();
}


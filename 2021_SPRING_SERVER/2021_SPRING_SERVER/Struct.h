#pragma once
enum OP_TYPE { OP_RECV, OP_SEND, OP_ACCEPT, OP_NPC_MOVE, OP_PLAYER_MOVE, OP_RECOVERY, OP_NPC_DEAD, OP_PLAYER_DEATH };
enum S_STATE { STATE_FREE, STATE_CONNECTED, STATE_INGAME };
enum DB_TYPE { DB_INSERT, DB_UPDATE, DB_LOAD };

struct EX_OVER
{
	WSAOVERLAPPED	m_over;
	WSABUF			m_wsabuf[1];
	unsigned char	m_netbuf[MAX_BUFFER];
	OP_TYPE			m_op;
	SOCKET			m_csocket;
	int				m_target_id;
};

struct SESSION
{
	int				m_id;
	EX_OVER			m_recv_over;
	unsigned char	m_prev_recv;
	SOCKET   m_s;

	atomic <S_STATE> m_state;
	mutex	m_lock;
	char	m_name[MAX_ID_LEN];
	short	m_x, m_y;
	short	m_fx, m_fy;
	short	m_level, m_hp, m_exp;
	short	m_max_exp;
	int		last_move_time;
	unordered_set <int> m_viewlist;
	mutex	m_vl;

	bool isRecovery;
	bool isAlive;

	lua_State* L;
	mutex sl;
};

struct timer_event 
{
	int object_id;
	OP_TYPE	event_type;
	system_clock::time_point exec_time;
	int target_id;

	constexpr bool operator < (const timer_event& l) const
	{
		return exec_time > l.exec_time;
	}
};
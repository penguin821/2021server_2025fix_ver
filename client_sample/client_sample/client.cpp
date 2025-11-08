#define SFML_STATIC 1
#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <unordered_map>
#include <chrono>

using namespace std;
using namespace chrono;

#ifdef _DEBUG
#pragma comment (lib, "lib/sfml-graphics-s-d.lib")
#pragma comment (lib, "lib/sfml-window-s-d.lib")
#pragma comment (lib, "lib/sfml-system-s-d.lib")
#pragma comment (lib, "lib/sfml-network-s-d.lib")
#else
#pragma comment (lib, "lib/sfml-graphics-s.lib")
#pragma comment (lib, "lib/sfml-window-s.lib")
#pragma comment (lib, "lib/sfml-system-s.lib")
#pragma comment (lib, "lib/sfml-network-s.lib")
#endif
#pragma comment (lib, "opengl32.lib")
#pragma comment (lib, "winmm.lib")
#pragma comment (lib, "ws2_32.lib")

#include "protocol.h"

sf::TcpSocket socket;

constexpr auto SCREEN_WIDTH = 15;
constexpr auto SCREEN_HEIGHT = 15;

constexpr auto TILE_WIDTH = 65;
constexpr auto WINDOW_WIDTH = TILE_WIDTH * SCREEN_WIDTH + 10;   // size of window
constexpr auto WINDOW_HEIGHT = TILE_WIDTH * SCREEN_WIDTH + 10;
constexpr auto BUF_SIZE = MAX_BUFFER;

int g_left_x;
int g_top_y;
int g_myid;

high_resolution_clock::time_point attack_cool;

sf::RenderWindow* g_window;
sf::Font g_font;

class OBJECT {
public:
	bool m_showing;
	sf::Sprite m_sprite;
	sf::Text m_name;
	int m_x, m_y;
	int m_level, m_hp, m_exp;
	chrono::system_clock::time_point m_mess_end_time;
	sf::Text m_chat;

	OBJECT(sf::Texture& t, int x, int y, int x2, int y2) 
	{
		m_showing = false;
		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
		set_name(-1, "NONAME");
		m_mess_end_time = chrono::system_clock::now();
		m_level = 1;
		m_hp = 100;
		m_exp = 0;
	}
	OBJECT() {
		m_showing = false;
	}
	void show()
	{
		m_showing = true;
	}
	void hide()
	{
		m_showing = false;
	}

	void a_move(int x, int y) {
		m_sprite.setPosition((float)x, (float)y);
	}

	void a_draw() {
		g_window->draw(m_sprite);
	}
public:

	void move(int x, int y) {
		m_x = x;
		m_y = y;
	}

	void draw() {
		if (false == m_showing) return;
		float rx = (m_x - g_left_x) * 65.0f + 8;
		float ry = (m_y - g_top_y) * 65.0f + 8;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);
		if (system_clock::now() < m_mess_end_time) {
			m_chat.setPosition(rx - 10, ry + 40);
			g_window->draw(m_chat);
		}
		else 
		{
			m_name.setPosition(rx - 10, ry - 45);
			g_window->draw(m_name);
		}
	}
	void set_name(int id,const char* str) {
		m_name.setFont(g_font);
		m_name.setString(str);
		if (NPC_ID_START > id)
			m_name.setFillColor(sf::Color(0, 255, 0));
		else if (NPC_AGGRO_START <= id)
			m_name.setFillColor(sf::Color(255, 0, 0));
		else
			m_name.setFillColor(sf::Color(255, 255, 0));
		m_name.setStyle(sf::Text::Bold);
	}
	void set_chat(const char* str) {
		m_chat.setFont(g_font);
		m_chat.setString(str);
		m_chat.setFillColor(sf::Color(255, 255, 255));
		m_chat.setStyle(sf::Text::Bold);
		m_mess_end_time = chrono::system_clock::now() + chrono::seconds(1);
	}
};

OBJECT avatar;
unordered_map <int, OBJECT> players;
unordered_map <int, OBJECT> npcs;

OBJECT white_tile;
OBJECT black_tile;

sf::Texture* board;
sf::Texture* pieces;

void client_initialize()
{
	board = new sf::Texture;
	pieces = new sf::Texture;
	if (false == g_font.loadFromFile("cour.ttf")) {
		cout << "Font Loading Error!\n";
		while (true);
	}
	board->loadFromFile("chessmap.bmp");
	pieces->loadFromFile("npc.png");
	white_tile = OBJECT{ *board, 5, 5, TILE_WIDTH, TILE_WIDTH };
	black_tile = OBJECT{ *board, 69, 5, TILE_WIDTH, TILE_WIDTH };
	avatar = OBJECT{ *pieces, 0, 0, 64, 64 };
	avatar.hide();
}

void client_finish()
{
	delete board;
	delete pieces;
}

void send_move_packet(int dir)
{
	cs_packet_move packet;
	packet.size = sizeof(packet);
	packet.type = CS_MOVE;
	packet.direction = dir;
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}

void send_attack_packet(int dir)
{
	cs_packet_attack packet;
	packet.size = sizeof(packet);
	packet.type = CS_ATTACK;
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}

void send_login_packet(char* id)
{
	cs_packet_login packet;
	packet.size = sizeof(packet);
	packet.type = CS_LOGIN;
	avatar.set_name(0, id);
	strcpy_s(packet.player_id, id);

	/*cs_packet_login packet;
	packet.size = sizeof(packet);
	packet.type = CS_LOGIN;
	string name = "PL";
	auto tt = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
	name += to_string(tt % 1000);
	avatar.set_name(-1, name.c_str());
	strcpy_s(packet.player_id, to_string(rand() % 100).c_str());*/

	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}

void ProcessPacket(char* ptr)
{
	static bool first_time = true;
	switch (ptr[1])
	{
	case SC_LOGIN_OK:
	{
		sc_packet_login_ok* packet = reinterpret_cast<sc_packet_login_ok*>(ptr);
		g_myid = packet->id;
		avatar.m_x = packet->x;
		avatar.m_y = packet->y;
		g_left_x = packet->x - SCREEN_WIDTH / 2;
		g_top_y = packet->y - SCREEN_HEIGHT / 2;
		avatar.move(packet->x, packet->y);
		avatar.m_exp = packet->EXP;
		avatar.m_level = packet->LEVEL;
		avatar.m_hp = packet->HP;
		avatar.show();
		attack_cool = high_resolution_clock::now();
	}
	break;
	case SC_LOGIN_FAIL:
	{
		sc_packet_login_fail* packet = reinterpret_cast<sc_packet_login_fail*>(ptr);
		char id[MAX_ID_LEN];
		cout << "로그인 실패! 다른 아이디 입력하세요 : ";
		cin >> id;
		send_login_packet(id);
	}
	break;
	case SC_ADD_OBJECT:
	{
		sc_packet_add_object* my_packet = reinterpret_cast<sc_packet_add_object*>(ptr);
		int id = my_packet->id;

		if (g_myid == id)
		{
			avatar.move(my_packet->x, my_packet->y);
			avatar.m_exp = my_packet->EXP;
			avatar.m_level = my_packet->LEVEL;
			avatar.m_hp = my_packet->HP;
			avatar.show();
		}
		else if (my_packet->obj_class == 0)
		{
			players[id] = OBJECT{ *pieces, 64, 0, 64, 64 };
			players[id].move(my_packet->x, my_packet->y);
			players[id].show();
			players[id].set_name(id, my_packet->name);
		}
		else {
			if (my_packet->obj_class == 1)
				npcs[id] = OBJECT{ *pieces, 128, 0, 64, 64 };
			else
				npcs[id] = OBJECT{ *pieces, 192, 0, 64, 64 };
			npcs[id].move(my_packet->x, my_packet->y);
			npcs[id].show();
			npcs[id].set_name(id, my_packet->name);
		}
		break;
	}
	case SC_POSITION:
	{
		sc_packet_position* my_packet = reinterpret_cast<sc_packet_position*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - SCREEN_WIDTH / 2;
			g_top_y = my_packet->y - SCREEN_HEIGHT / 2;
		}
		else if (other_id < NPC_ID_START) {
			players[other_id].move(my_packet->x, my_packet->y);
		}
		else {
			npcs[other_id].move(my_packet->x, my_packet->y);
		}
		break;
	}
	case SC_REMOVE_OBJECT:
	{
		sc_packet_remove_object* my_packet = reinterpret_cast<sc_packet_remove_object*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.hide();
		}
		else if (other_id < NPC_ID_START) {
			players[other_id].hide();
			players.erase(other_id);
		}
		else {
			npcs[other_id].hide();
			npcs.erase(other_id);
		}
		break;
	}
	case SC_CHAT:
	{
		sc_packet_chat* my_packet = reinterpret_cast<sc_packet_chat*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.set_chat(my_packet->message);
		}
		else if (other_id < NPC_ID_START) {
			players[other_id].set_chat(my_packet->message);
		}
		else {
			npcs[other_id].set_chat(my_packet->message);
		}
		break;
	}
	case SC_UPDATE:
	{
		sc_packet_update* my_packet = reinterpret_cast<sc_packet_update*>(ptr);
		avatar.m_hp = my_packet->HP;
		avatar.m_level = my_packet->LEVEL;
		avatar.m_exp = my_packet->EXP;
	}
	break;
	default:
		printf("Unknown PACKET type [%d]\n", ptr[1]);
	}
}

void process_data(char* net_buf, size_t io_byte)
{
	char* ptr = net_buf;
	static size_t in_packet_size = 0;
	static size_t saved_packet_size = 0;
	static char packet_buffer[BUF_SIZE];

	while (0 != io_byte) {
		if (0 == in_packet_size) in_packet_size = ptr[0];
		if (io_byte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			io_byte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
			saved_packet_size += io_byte;
			io_byte = 0;
		}
	}
}

void client_main()
{
	char net_buf[BUF_SIZE];
	size_t	received;

	auto recv_result = socket.receive(net_buf, BUF_SIZE, received);
	if (recv_result == sf::Socket::Error)
	{
		wcout << L"Recv 에러!";
		while (true);
	}
	if (recv_result != sf::Socket::NotReady)
		if (received > 0) process_data(net_buf, received);

	for (int i = 0; i < SCREEN_WIDTH; ++i)
		for (int j = 0; j < SCREEN_HEIGHT; ++j)
		{
			int tile_x = i + g_left_x;
			int tile_y = j + g_top_y;
			if ((tile_x < 0) || (tile_y < 0)) continue;
			if (((tile_x % 6) < 3) || ((tile_y % 6) < 3)) {
				white_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
				white_tile.a_draw();
			}
			else
			{
				black_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
				black_tile.a_draw();
			}
		}
	avatar.draw();
	for (auto& pl : players) pl.second.draw();
	for (auto& pl : npcs) pl.second.draw();

	char info[70];
	int exp_max = pow(2, avatar.m_level - 1) * 100;
	sprintf_s(info, "Lv: %d | Hp: %d | Exp: <%d / %d> | Pos: <%d, %d>", avatar.m_level, avatar.m_hp, avatar.m_exp, exp_max, avatar.m_x, avatar.m_y);
	
	sf::Text Info;
	Info.setFont(g_font);
	Info.setString(info);
	Info.setFillColor(sf::Color(0, 0, 255));
	Info.setStyle(sf::Text::Bold);
	Info.setPosition(0, 0);
	g_window->draw(Info);
}

int main()
{
	//string ip;
	//cout << "IP 입력: ";
	//cin >> ip;

	wcout.imbue(locale("korean"));
	//sf::Socket::Status status = socket.connect(ip, SERVER_PORT);
	sf::Socket::Status status = socket.connect("127.0.0.1", SERVER_PORT);
	socket.setBlocking(false);

	if (status != sf::Socket::Done) 
	{
		wcout << L"서버와 연결할 수 없습니다.\n";
		while (true);
	}

	char id[MAX_ID_LEN];
	cout << "ID 입력: ";
	cin >> id;

	client_initialize();

	send_login_packet(id);

	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2D CLIENT");
	g_window = &window;

	while (window.isOpen())
	{
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();
			if (event.type == sf::Event::KeyPressed) 
			{
				int dir = -1;
				switch (event.key.code)
				{
				case sf::Keyboard::Left:
					dir = 2;
					break;
				case sf::Keyboard::Right:
					dir = 3;
					break;
				case sf::Keyboard::Up:
					dir = 0;
					break;
				case sf::Keyboard::Down:
					dir = 1;
					break;
				case sf::Keyboard::A:
				{
					duration<double> cool_time = duration_cast<duration<double>>(high_resolution_clock::now() - attack_cool);
					if (cool_time.count() > 1) // ↑ 쿨타임 2초 계산해주는 식
					{
						send_attack_packet(g_myid);
						attack_cool = high_resolution_clock::now();
					}
				}
					break;
				case sf::Keyboard::Escape:
					window.close();
					break;
				}
				if (-1 != dir) send_move_packet(dir);
			}
		}

		window.clear();
		client_main();
		window.display();
	}
	client_finish();

	return 0;
}
#pragma once
#include "binary_protocol.h"
#include <memory>
#include <unordered_map>

// ── CompactPos ───────────────────────────────────────────────────────
struct CompactPos {
    uint16_t x_bits=0, y_bits=0;
    CompactPos() = default;
    CompactPos(float x, float y) : x_bits(f32_to_f16(x)), y_bits(f32_to_f16(y)) {}
    float x() const { return f16_to_f32(x_bits); }
    float y() const { return f16_to_f32(y_bits); }
    static CompactPos read_binary(BinaryReader& r) { CompactPos p; p.x_bits=r.read_u16(); p.y_bits=r.read_u16(); return p; }
    void write_binary(BinaryWriter& w) const { w.write_u16(x_bits); w.write_u16(y_bits); }
};

// ── RoomId ───────────────────────────────────────────────────────────
struct RoomId {
    std::string value;
    RoomId() = default;
    explicit RoomId(std::string s) : value(std::move(s)) {}
    bool operator==(const RoomId& o) const { return value==o.value; }
    bool operator!=(const RoomId& o) const { return !(*this==o); }
    static bool validate(const std::string& s) {
        if (s.empty()||s.size()>20) return false;
        for (char c:s) if (c!='-'&&c!='_'&&!isalnum((unsigned char)c)) return false;
        return true;
    }
    static RoomId read_binary(BinaryReader& r) {
        std::string s=r.read_varchar(20);
        if (!validate(s)) throw std::runtime_error("invalid room id");
        return RoomId(std::move(s));
    }
    void write_binary(BinaryWriter& w) const { w.write_string(value); }
    const std::string& to_string() const { return value; }
};
struct RoomIdHash { size_t operator()(const RoomId& r) const { return std::hash<std::string>()(r.value); } };

// ── TouchFrame ───────────────────────────────────────────────────────
struct TouchFrame {
    float time=0;
    std::vector<std::pair<int8_t,CompactPos>> points;
    static TouchFrame read_binary(BinaryReader& r) {
        TouchFrame f; f.time=r.read_f32();
        uint64_t n=r.read_uleb(); f.points.reserve(n);
        for (uint64_t i=0;i<n;i++) { int8_t id=r.read_i8(); auto pos=CompactPos::read_binary(r); f.points.emplace_back(id,pos); }
        return f;
    }
    void write_binary(BinaryWriter& w) const {
        w.write_f32(time); w.write_uleb(points.size());
        for (auto&[id,pos]:points) { w.write_i8(id); pos.write_binary(w); }
    }
};

// ── Judgement / JudgeEvent ───────────────────────────────────────────
enum class Judgement:uint8_t { Perfect=0, Good=1, Bad=2, Miss=3, HoldPerfect=4, HoldGood=5 };

struct JudgeEvent {
    float time=0; uint32_t line_id=0, note_id=0; Judgement judgement=Judgement::Perfect;
    static JudgeEvent read_binary(BinaryReader& r) {
        JudgeEvent e; e.time=r.read_f32(); e.line_id=r.read_u32(); e.note_id=r.read_u32();
        e.judgement=static_cast<Judgement>(r.read_u8()); return e;
    }
    void write_binary(BinaryWriter& w) const {
        w.write_f32(time); w.write_u32(line_id); w.write_u32(note_id); w.write_u8(static_cast<uint8_t>(judgement));
    }
};

// ── UserInfo ─────────────────────────────────────────────────────────
struct UserInfo {
    int32_t id=0; std::string name; bool monitor=false;
    static UserInfo read_binary(BinaryReader& r) { UserInfo u; u.id=r.read_i32(); u.name=r.read_string(); u.monitor=r.read_bool(); return u; }
    void write_binary(BinaryWriter& w) const { w.write_i32(id); w.write_string(name); w.write_bool(monitor); }
};

// ── RoomState ────────────────────────────────────────────────────────
enum class RoomStateType:uint8_t { SelectChart=0, WaitingForReady=1, Playing=2 };

struct RoomState {
    RoomStateType type=RoomStateType::SelectChart;
    std::optional<int32_t> chart_id;
    static RoomState select_chart(std::optional<int32_t> id=std::nullopt) { return {RoomStateType::SelectChart,id}; }
    static RoomState waiting_for_ready() { return {RoomStateType::WaitingForReady,std::nullopt}; }
    static RoomState playing() { return {RoomStateType::Playing,std::nullopt}; }
    void write_binary(BinaryWriter& w) const {
        w.write_u8(static_cast<uint8_t>(type));
        if (type==RoomStateType::SelectChart) {
            if (chart_id) { w.write_bool(true); w.write_i32(*chart_id); } else w.write_bool(false);
        }
    }
};

// ── ClientRoomState ──────────────────────────────────────────────────
struct ClientRoomState {
    RoomId id; RoomState state; bool live=false,locked=false,cycle_flag=false,is_host=false,is_ready=false;
    std::unordered_map<int32_t,UserInfo> users;
    void write_binary(BinaryWriter& w) const {
        id.write_binary(w); state.write_binary(w);
        w.write_bool(live); w.write_bool(locked); w.write_bool(cycle_flag);
        w.write_bool(is_host); w.write_bool(is_ready);
        w.write_uleb(users.size());
        for (auto&[k,v]:users) { w.write_i32(k); v.write_binary(w); }
    }
};

// ── JoinRoomResponse ─────────────────────────────────────────────────
struct JoinRoomResponse {
    RoomState state; std::vector<UserInfo> users; bool live=false;
    void write_binary(BinaryWriter& w) const {
        state.write_binary(w);
        w.write_uleb(users.size()); for (auto&u:users) u.write_binary(w);
        w.write_bool(live);
    }
};

// ── Message ──────────────────────────────────────────────────────────
enum class MessageType:uint8_t {
    Chat=0,CreateRoom=1,JoinRoom=2,LeaveRoom=3,NewHost=4,SelectChart=5,
    GameStart=6,Ready=7,CancelReady=8,CancelGame=9,StartPlaying=10,
    Played=11,GameEnd=12,Abort=13,LockRoom=14,CycleRoom=15
};

struct Message {
    MessageType type; int32_t user=0; std::string content; int32_t chart_id=0;
    int32_t score=0; float accuracy=0; bool full_combo=false; bool flag=false;

    void write_binary(BinaryWriter& w) const {
        w.write_u8(static_cast<uint8_t>(type));
        switch(type) {
        case MessageType::Chat:         w.write_i32(user); w.write_string(content); break;
        case MessageType::CreateRoom:   w.write_i32(user); break;
        case MessageType::JoinRoom:     w.write_i32(user); w.write_string(content); break;
        case MessageType::LeaveRoom:    w.write_i32(user); w.write_string(content); break;
        case MessageType::NewHost:      w.write_i32(user); break;
        case MessageType::SelectChart:  w.write_i32(user); w.write_string(content); w.write_i32(chart_id); break;
        case MessageType::GameStart:    w.write_i32(user); break;
        case MessageType::Ready:        w.write_i32(user); break;
        case MessageType::CancelReady:  w.write_i32(user); break;
        case MessageType::CancelGame:   w.write_i32(user); break;
        case MessageType::StartPlaying: break;
        case MessageType::Played:       w.write_i32(user); w.write_i32(score); w.write_f32(accuracy); w.write_bool(full_combo); break;
        case MessageType::GameEnd:      break;
        case MessageType::Abort:        w.write_i32(user); break;
        case MessageType::LockRoom:     w.write_bool(flag); break;
        case MessageType::CycleRoom:    w.write_bool(flag); break;
        }
    }
    static Message chat(int32_t u, const std::string& c) { Message m; m.type=MessageType::Chat; m.user=u; m.content=c; return m; }
    static Message create_room(int32_t u) { Message m; m.type=MessageType::CreateRoom; m.user=u; return m; }
    static Message join_room(int32_t u, const std::string& n) { Message m; m.type=MessageType::JoinRoom; m.user=u; m.content=n; return m; }
    static Message leave_room(int32_t u, const std::string& n) { Message m; m.type=MessageType::LeaveRoom; m.user=u; m.content=n; return m; }
    static Message new_host(int32_t u) { Message m; m.type=MessageType::NewHost; m.user=u; return m; }
    static Message select_chart(int32_t u, const std::string& n, int32_t id) { Message m; m.type=MessageType::SelectChart; m.user=u; m.content=n; m.chart_id=id; return m; }
    static Message game_start(int32_t u) { Message m; m.type=MessageType::GameStart; m.user=u; return m; }
    static Message ready(int32_t u) { Message m; m.type=MessageType::Ready; m.user=u; return m; }
    static Message cancel_ready(int32_t u) { Message m; m.type=MessageType::CancelReady; m.user=u; return m; }
    static Message cancel_game(int32_t u) { Message m; m.type=MessageType::CancelGame; m.user=u; return m; }
    static Message start_playing() { Message m; m.type=MessageType::StartPlaying; return m; }
    static Message played(int32_t u,int32_t s,float a,bool fc) { Message m; m.type=MessageType::Played; m.user=u; m.score=s; m.accuracy=a; m.full_combo=fc; return m; }
    static Message game_end() { Message m; m.type=MessageType::GameEnd; return m; }
    static Message abort_msg(int32_t u) { Message m; m.type=MessageType::Abort; m.user=u; return m; }
    static Message lock_room(bool l) { Message m; m.type=MessageType::LockRoom; m.flag=l; return m; }
    static Message cycle_room(bool c) { Message m; m.type=MessageType::CycleRoom; m.flag=c; return m; }
};

// ── ClientCommand ────────────────────────────────────────────────────
enum class ClientCommandType:uint8_t {
    Ping=0,Authenticate=1,Chat=2,Touches=3,Judges=4,CreateRoom=5,
    JoinRoom=6,LeaveRoom=7,LockRoom=8,CycleRoom=9,SelectChart=10,
    RequestStart=11,Ready=12,CancelReady=13,Played=14,Abort=15
};

struct ClientCommand {
    ClientCommandType type;
    std::string token, message; RoomId room_id;
    std::shared_ptr<std::vector<TouchFrame>> frames;
    std::shared_ptr<std::vector<JudgeEvent>> judges;
    bool monitor=false, flag=false; int32_t chart_id=0;

    static ClientCommand read_binary(BinaryReader& r) {
        ClientCommand c; c.type=static_cast<ClientCommandType>(r.read_u8());
        switch(c.type) {
        case ClientCommandType::Ping: break;
        case ClientCommandType::Authenticate: c.token=r.read_varchar(32); break;
        case ClientCommandType::Chat: c.message=r.read_varchar(200); break;
        case ClientCommandType::Touches: {
            uint64_t n=r.read_uleb(); auto f=std::make_shared<std::vector<TouchFrame>>();
            f->reserve(n); for(uint64_t i=0;i<n;i++) f->push_back(TouchFrame::read_binary(r));
            c.frames=std::move(f); break;
        }
        case ClientCommandType::Judges: {
            uint64_t n=r.read_uleb(); auto j=std::make_shared<std::vector<JudgeEvent>>();
            j->reserve(n); for(uint64_t i=0;i<n;i++) j->push_back(JudgeEvent::read_binary(r));
            c.judges=std::move(j); break;
        }
        case ClientCommandType::CreateRoom: c.room_id=RoomId::read_binary(r); break;
        case ClientCommandType::JoinRoom: c.room_id=RoomId::read_binary(r); c.monitor=r.read_bool(); break;
        case ClientCommandType::LeaveRoom: break;
        case ClientCommandType::LockRoom: c.flag=r.read_bool(); break;
        case ClientCommandType::CycleRoom: c.flag=r.read_bool(); break;
        case ClientCommandType::SelectChart: c.chart_id=r.read_i32(); break;
        case ClientCommandType::RequestStart: break;
        case ClientCommandType::Ready: break;
        case ClientCommandType::CancelReady: break;
        case ClientCommandType::Played: c.chart_id=r.read_i32(); break;
        case ClientCommandType::Abort: break;
        }
        return c;
    }
};

// ── ServerCommand ────────────────────────────────────────────────────
enum class ServerCommandType:uint8_t {
    Pong=0,Authenticate=1,Chat=2,Touches=3,Judges=4,SMessage=5,
    ChangeState=6,ChangeHost=7,CreateRoom=8,SJoinRoom=9,OnJoinRoom=10,
    LeaveRoom=11,LockRoom=12,CycleRoom=13,SelectChart=14,RequestStart=15,
    Ready=16,CancelReady=17,Played=18,Abort=19
};

struct ServerCommand {
    ServerCommandType type;
    bool ok=true; std::string error_msg;
    UserInfo auth_user; std::optional<ClientRoomState> auth_room_state;
    int32_t player_id=0;
    std::shared_ptr<std::vector<TouchFrame>> frames;
    std::shared_ptr<std::vector<JudgeEvent>> judges_data;
    Message message; RoomState room_state; bool is_host=false;
    JoinRoomResponse join_response; UserInfo join_user;

    void write_binary(BinaryWriter& w) const {
        w.write_u8(static_cast<uint8_t>(type));
        switch(type) {
        case ServerCommandType::Pong: break;
        case ServerCommandType::Authenticate:
            w.write_bool(ok);
            if (ok) { auth_user.write_binary(w); if(auth_room_state){w.write_bool(true);auth_room_state->write_binary(w);}else w.write_bool(false); }
            else w.write_string(error_msg);
            break;
        case ServerCommandType::Chat: case ServerCommandType::CreateRoom:
        case ServerCommandType::LeaveRoom: case ServerCommandType::LockRoom:
        case ServerCommandType::CycleRoom: case ServerCommandType::SelectChart:
        case ServerCommandType::RequestStart: case ServerCommandType::Ready:
        case ServerCommandType::CancelReady: case ServerCommandType::Played:
        case ServerCommandType::Abort:
            w.write_bool(ok); if(!ok) w.write_string(error_msg); break;
        case ServerCommandType::Touches:
            w.write_i32(player_id); w.write_uleb(frames->size());
            for(auto&f:*frames) f.write_binary(w);
            break;
        case ServerCommandType::Judges:
            w.write_i32(player_id); w.write_uleb(judges_data->size());
            for(auto&j:*judges_data) j.write_binary(w);
            break;
        case ServerCommandType::SMessage: message.write_binary(w); break;
        case ServerCommandType::ChangeState: room_state.write_binary(w); break;
        case ServerCommandType::ChangeHost: w.write_bool(is_host); break;
        case ServerCommandType::SJoinRoom:
            w.write_bool(ok);
            if(ok) join_response.write_binary(w); else w.write_string(error_msg); break;
        case ServerCommandType::OnJoinRoom: join_user.write_binary(w); break;
        }
    }

    static ServerCommand pong() { ServerCommand c; c.type=ServerCommandType::Pong; return c; }
    static ServerCommand authenticate_ok(const UserInfo& u, std::optional<ClientRoomState> rs) {
        ServerCommand c; c.type=ServerCommandType::Authenticate; c.ok=true; c.auth_user=u; c.auth_room_state=std::move(rs); return c;
    }
    static ServerCommand authenticate_err(const std::string& e) {
        ServerCommand c; c.type=ServerCommandType::Authenticate; c.ok=false; c.error_msg=e; return c;
    }
    static ServerCommand simple_ok(ServerCommandType t) { ServerCommand c; c.type=t; c.ok=true; return c; }
    static ServerCommand simple_err(ServerCommandType t, const std::string& e) { ServerCommand c; c.type=t; c.ok=false; c.error_msg=e; return c; }
    static ServerCommand touches(int32_t p, std::shared_ptr<std::vector<TouchFrame>> f) {
        ServerCommand c; c.type=ServerCommandType::Touches; c.player_id=p; c.frames=std::move(f); return c;
    }
    static ServerCommand judges_cmd(int32_t p, std::shared_ptr<std::vector<JudgeEvent>> j) {
        ServerCommand c; c.type=ServerCommandType::Judges; c.player_id=p; c.judges_data=std::move(j); return c;
    }
    static ServerCommand msg(Message m) { ServerCommand c; c.type=ServerCommandType::SMessage; c.message=std::move(m); return c; }
    static ServerCommand change_state(RoomState s) { ServerCommand c; c.type=ServerCommandType::ChangeState; c.room_state=s; return c; }
    static ServerCommand change_host(bool h) { ServerCommand c; c.type=ServerCommandType::ChangeHost; c.is_host=h; return c; }
    static ServerCommand join_room_ok(JoinRoomResponse r) { ServerCommand c; c.type=ServerCommandType::SJoinRoom; c.ok=true; c.join_response=std::move(r); return c; }
    static ServerCommand join_room_err(const std::string& e) { ServerCommand c; c.type=ServerCommandType::SJoinRoom; c.ok=false; c.error_msg=e; return c; }
    static ServerCommand on_join_room(const UserInfo& u) { ServerCommand c; c.type=ServerCommandType::OnJoinRoom; c.join_user=u; return c; }
};

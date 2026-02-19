use crate::{Error, Result, api_host::HostApi};
use std::sync::Arc;
use tracing::info;

/// Server command implementations for all 45 commands
pub struct ServerCommands {
    host_api: Arc<HostApi>,
}

impl ServerCommands {
    /// Create a new server commands instance
    pub fn new(host_api: Arc<HostApi>) -> Self {
        Self { host_api }
    }

    // ===== Command implementations =====

    /// 帮助命令
    pub fn help(&self, args: &[String]) -> Result<String> {
        let help_text = r#"可用的服务器命令:

用户管理:
  /kick <用户ID>                    - 踢出用户
  /banid <用户ID> <原因>            - 封禁用户(ID)
  /unbanid <用户ID>                 - 解封用户(ID)
  /banip <IP地址> <原因>            - 封禁用户(IP)
  /unbanip <IP地址>                 - 解封用户(IP)
  /userinfo <用户ID>                - 获取用户完整信息
  /username <用户ID>                - 获取用户名
  /userlang <用户ID>                - 获取用户语言
  /playtime <用户ID>                - 获取用户游玩时间
  /playtop <数量>                   - 获取用户游玩时间总排行
  /bannedids                        - 获取封禁用户列表(ID)
  /bannedips                        - 获取封禁用户列表(IP)
  /checkbanid <用户ID>              - 查询用户是否被封禁(ID)
  /checkbanip <IP地址>              - 查询用户是否被封禁(IP)

房间封禁:
  /banroomid <用户ID> <房间ID>      - 封禁用户进入特定房间(ID)
  /unbanroomid <用户ID> <房间ID>    - 解封用户进入特定房间(ID)
  /banroomip <IP地址> <房间ID>      - 封禁用户进入特定房间(IP)
  /unbanroomip <IP地址> <房间ID>    - 解封用户进入特定房间(IP)
  /checkroomban <用户ID> <房间ID>   - 查询用户是否被特定房间封禁

房间管理:
  /createroom <最大人数>            - 创建房间
  /disbandroom <房间ID>             - 解散房间
  /joinroom <用户ID> <房间ID>       - 将用户加入至房间
  /kickroom <用户ID> <房间ID>       - 将用户踢出房间
  /roominfo <房间ID>                - 获取房间完整信息
  /roomusers <房间ID>               - 获取房间用户数
  /roomuserids <房间ID>             - 获取房间内用户ID列表
  /roomhost <房间ID>                - 获取房间房主ID
  /setmaxusers <房间ID> <数量>      - 设置房间最大人数
  /startprep <房间ID>               - 开始房间内准备游戏
  /endprep <房间ID>                 - 结束房间内准备游戏
  /forcestart <房间ID>              - 强制开始房间内游戏
  /setlock <房间ID> <是/否>         - 设定房间锁定状态
  /normalmode <房间ID>              - 切换房间为普通模式
  /cyclemode <房间ID>               - 切换房间为循环模式
  /selectchart <房间ID> <谱面ID>    - 选择房间谱面ID

消息管理:
  /sendmsg <用户ID> <消息>          - 向指定用户发送消息
  /broadcastall <消息>              - 向所有用户广播消息
  /broadcastroom <房间ID> <消息>    - 向指定房间广播消息
  /broadcastrooms <消息>            - 向所有房间广播消息

服务器管理:
  /shutdown                         - 关闭服务器
  /restart                          - 重启服务器
  /reloadall                        - 重载所有插件
  /reload <插件名>                  - 重载指定插件
  /plugins                          - 获取插件列表

查询统计:
  /playtotal                        - 获取用户游玩时间总排行榜
  /onlinecount                      - 获取在线用户数
  /availablerooms                   - 获取可加入房间数
  /rooms                            - 获取房间列表
  /availableroomlist                - 获取可加入房间列表
  /onlineusers                      - 获取在线用户ID列表

输入 /help <命令名> 获取特定命令的详细用法"#;

        if args.is_empty() {
            Ok(help_text.to_string())
        } else {
            let command = &args[0];
            let detail = match command.as_str() {
                "kick" => "踢出用户命令\n用法: /kick <用户ID>\n示例: /kick 123",
                "banid" => "封禁用户(ID)\n用法: /banid <用户ID> <原因>\n示例: /banid 123 \"作弊\"",
                "unbanid" => "解封用户(ID)\n用法: /unbanid <用户ID>\n示例: /unbanid 123",
                "banip" => "封禁用户(IP)\n用法: /banip <IP地址> <原因>\n示例: /banip 192.168.1.1 \"滥用\"",
                "unbanip" => "解封用户(IP)\n用法: /unbanip <IP地址>\n示例: /unbanip 192.168.1.1",
                "userinfo" => "获取用户完整信息\n用法: /userinfo <用户ID>\n示例: /userinfo 123",
                "username" => "获取用户名\n用法: /username <用户ID>\n示例: /username 123",
                "userlang" => "获取用户语言\n用法: /userlang <用户ID>\n示例: /userlang 123",
                "playtime" => "获取用户游玩时间\n用法: /playtime <用户ID>\n示例: /playtime 123",
                "playtop" => "获取用户游玩时间总排行\n用法: /playtop <数量>\n示例: /playtop 10",
                "bannedids" => "获取封禁用户列表(ID)\n用法: /bannedids",
                "bannedips" => "获取封禁用户列表(IP)\n用法: /bannedips",
                "checkbanid" => "查询用户是否被封禁(ID)\n用法: /checkbanid <用户ID>\n示例: /checkbanid 123",
                "checkbanip" => "查询用户是否被封禁(IP)\n用法: /checkbanip <IP地址>\n示例: /checkbanip 192.168.1.1",
                "banroomid" => "封禁用户进入特定房间(ID)\n用法: /banroomid <用户ID> <房间ID>\n示例: /banroomid 123 1",
                "unbanroomid" => "解封用户进入特定房间(ID)\n用法: /unbanroomid <用户ID> <房间ID>\n示例: /unbanroomid 123 1",
                "banroomip" => "封禁用户进入特定房间(IP)\n用法: /banroomip <IP地址> <房间ID>\n示例: /banroomip 192.168.1.1 1",
                "unbanroomip" => "解封用户进入特定房间(IP)\n用法: /unbanroomip <IP地址> <房间ID>\n示例: /unbanroomip 192.168.1.1 1",
                "checkroomban" => "查询用户是否被特定房间封禁\n用法: /checkroomban <用户ID> <房间ID>\n示例: /checkroomban 123 1",
                "createroom" => "创建房间\n用法: /createroom <最大人数>\n示例: /createroom 4",
                "disbandroom" => "解散房间\n用法: /disbandroom <房间ID>\n示例: /disbandroom 1",
                "joinroom" => "将用户加入至房间\n用法: /joinroom <用户ID> <房间ID>\n示例: /joinroom 123 1",
                "kickroom" => "将用户踢出房间\n用法: /kickroom <用户ID> <房间ID>\n示例: /kickroom 123 1",
                "roominfo" => "获取房间完整信息\n用法: /roominfo <房间ID>\n示例: /roominfo 1",
                "roomusers" => "获取房间用户数\n用法: /roomusers <房间ID>\n示例: /roomusers 1",
                "roomuserids" => "获取房间内用户ID列表\n用法: /roomuserids <房间ID>\n示例: /roomuserids 1",
                "roomhost" => "获取房间房主ID\n用法: /roomhost <房间ID>\n示例: /roomhost 1",
                "setmaxusers" => "设置房间最大人数\n用法: /setmaxusers <房间ID> <数量>\n示例: /setmaxusers 1 8",
                "startprep" => "开始房间内准备游戏\n用法: /startprep <房间ID>\n示例: /startprep 1",
                "endprep" => "结束房间内准备游戏\n用法: /endprep <房间ID>\n示例: /endprep 1",
                "forcestart" => "强制开始房间内游戏\n用法: /forcestart <房间ID>\n示例: /forcestart 1",
                "setlock" => "设定房间锁定状态\n用法: /setlock <房间ID> <是/否>\n示例: /setlock 1 是",
                "normalmode" => "切换房间为普通模式\n用法: /normalmode <房间ID>\n示例: /normalmode 1",
                "cyclemode" => "切换房间为循环模式\n用法: /cyclemode <房间ID>\n示例: /cyclemode 1",
                "selectchart" => "选择房间谱面ID\n用法: /selectchart <房间ID> <谱面ID>\n示例: /selectchart 1 100",
                "sendmsg" => "向指定用户发送消息\n用法: /sendmsg <用户ID> <消息>\n示例: /sendmsg 123 \"你好\"",
                "broadcastall" => "向所有用户广播消息\n用法: /broadcastall <消息>\n示例: /broadcastall \"服务器重启中...\"",
                "broadcastroom" => "向指定房间广播消息\n用法: /broadcastroom <房间ID> <消息>\n示例: /broadcastroom 1 \"准备开始游戏\"",
                "broadcastrooms" => "向所有房间广播消息\n用法: /broadcastrooms <消息>\n示例: /broadcastrooms \"活动即将开始\"",
                "shutdown" => "关闭服务器\n用法: /shutdown\n注意: 需要管理员权限",
                "restart" => "重启服务器\n用法: /restart\n注意: 需要管理员权限",
                "reloadall" => "重载所有插件\n用法: /reloadall",
                "reload" => "重载指定插件\n用法: /reload <插件名>\n示例: /reload test-plugin",
                "plugins" => "获取插件列表\n用法: /plugins",
                "playtotal" => "获取用户游玩时间总排行榜\n用法: /playtotal",
                "onlinecount" => "获取在线用户数\n用法: /onlinecount",
                "availablerooms" => "获取可加入房间数\n用法: /availablerooms",
                "rooms" => "获取房间列表\n用法: /rooms",
                "availableroomlist" => "获取可加入房间列表\n用法: /availableroomlist",
                "onlineusers" => "获取在线用户ID列表\n用法: /onlineusers",
                _ => return Err(Error::Command(format!("未知命令: {}", command))),
            };
            Ok(detail.to_string())
        }
    }

    /// 踢出用户命令
    pub fn kick_user(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /kick <用户ID>".to_string()));
        }

        let user_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的用户ID".to_string()))?;

        self.host_api.kick_user(user_id)?;
        info!("用户 {} 已被踢出", user_id);
        Ok(format!("用户 {} 已被踢出", user_id))
    }

    /// 封禁用户(id)命令
    pub fn ban_user_by_id(&self, args: &[String]) -> Result<String> {
        if args.len() < 2 {
            return Err(Error::Command("用法: /banid <用户ID> <原因>".to_string()));
        }

        let user_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的用户ID".to_string()))?;
        let reason = args[1..].join(" ");

        self.host_api.ban_user_by_id(user_id, &reason)?;
        info!("用户 {} 已被封禁，原因: {}", user_id, reason);
        Ok(format!("用户 {} 已被封禁，原因: {}", user_id, reason))
    }

    /// 解封用户(id)命令
    pub fn unban_user_by_id(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /unbanid <用户ID>".to_string()));
        }

        let user_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的用户ID".to_string()))?;

        self.host_api.unban_user_by_id(user_id)?;
        info!("用户 {} 已解封", user_id);
        Ok(format!("用户 {} 已解封", user_id))
    }

    /// 封禁用户(ip)命令
    pub fn ban_user_by_ip(&self, args: &[String]) -> Result<String> {
        if args.len() < 2 {
            return Err(Error::Command("用法: /banip <IP地址> <原因>".to_string()));
        }

        let ip = &args[0];
        let reason = args[1..].join(" ");

        // 简单的IP验证
        if !is_valid_ip(ip) {
            return Err(Error::Command("无效的IP地址".to_string()));
        }

        self.host_api.ban_user_by_ip(ip, &reason)?;
        info!("IP {} 已被封禁，原因: {}", ip, reason);
        Ok(format!("IP {} 已被封禁，原因: {}", ip, reason))
    }

    /// 解封用户(ip)命令
    pub fn unban_user_by_ip(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /unbanip <IP地址>".to_string()));
        }

        let ip = &args[0];
        
        if !is_valid_ip(ip) {
            return Err(Error::Command("无效的IP地址".to_string()));
        }

        self.host_api.unban_user_by_ip(ip)?;
        info!("IP {} 已解封", ip);
        Ok(format!("IP {} 已解封", ip))
    }

    /// 获取用户完整信息命令
    pub fn get_user_info(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /userinfo <用户ID>".to_string()));
        }

        let user_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的用户ID".to_string()))?;

        let info = self.host_api.get_user_info(user_id)?;
        Ok(serde_json::to_string_pretty(&info)
            .map_err(|e| Error::Command(format!("序列化失败: {}", e)))?)
    }

    /// 获取用户名命令
    pub fn get_username(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /username <用户ID>".to_string()));
        }

        let user_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的用户ID".to_string()))?;

        let name = self.host_api.get_username(user_id)?;
        Ok(format!("用户 {} 的用户名: {}", user_id, name))
    }

    /// 获取用户语言命令
    pub fn get_user_language(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /userlang <用户ID>".to_string()));
        }

        let user_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的用户ID".to_string()))?;

        let language = self.host_api.get_user_language(user_id)?;
        Ok(format!("用户 {} 的语言: {}", user_id, language))
    }

    /// 获取用户游玩时间（插件实现）命令
    pub fn get_user_playtime(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /playtime <用户ID>".to_string()));
        }

        let user_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的用户ID".to_string()))?;

        let playtime = self.host_api.get_user_playtime(user_id)?;
        let hours = playtime / 3600;
        let minutes = (playtime % 3600) / 60;
        let seconds = playtime % 60;
        Ok(format!("用户 {} 的游玩时间: {}小时{}分钟{}秒", 
                   user_id, hours, minutes, seconds))
    }

    /// 获取用户游玩时间总排行（插件实现）命令
    pub fn get_playtime_leaderboard(&self, args: &[String]) -> Result<String> {
        let limit = if args.is_empty() {
            10
        } else {
            args[0].parse::<u32>()
                .map_err(|_| Error::Command("无效的数量".to_string()))?
        };

        let leaderboard = self.host_api.get_playtime_leaderboard(limit)?;
        Ok(serde_json::to_string_pretty(&leaderboard)
            .map_err(|e| Error::Command(format!("序列化失败: {}", e)))?)
    }

    /// 获取封禁用户列表(id)命令
    pub fn get_banned_users_by_id(&self, _args: &[String]) -> Result<String> {
        let banned_users = self.host_api.get_banned_users_by_id()?;
        Ok(serde_json::to_string_pretty(&banned_users)
            .map_err(|e| Error::Command(format!("序列化失败: {}", e)))?)
    }

    /// 获取封禁用户列表(ip)命令
    pub fn get_banned_users_by_ip(&self, _args: &[String]) -> Result<String> {
        let banned_ips = self.host_api.get_banned_users_by_ip()?;
        Ok(serde_json::to_string_pretty(&banned_ips)
            .map_err(|e| Error::Command(format!("序列化失败: {}", e)))?)
    }

    /// 查询用户是否被封禁(id)命令
    pub fn is_user_banned_by_id(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /checkbanid <用户ID>".to_string()));
        }

        let user_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的用户ID".to_string()))?;

        let banned = self.host_api.is_user_banned_by_id(user_id)?;
        if banned {
            Ok(format!("用户 {} 已被封禁", user_id))
        } else {
            Ok(format!("用户 {} 未被封禁", user_id))
        }
    }

    /// 查询用户是否被封禁(ip)命令
    pub fn is_user_banned_by_ip(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /checkbanip <IP地址>".to_string()));
        }

        let ip = &args[0];
        
        if !is_valid_ip(ip) {
            return Err(Error::Command("无效的IP地址".to_string()));
        }

        let banned = self.host_api.is_user_banned_by_ip(ip)?;
        if banned {
            Ok(format!("IP {} 已被封禁", ip))
        } else {
            Ok(format!("IP {} 未被封禁", ip))
        }
    }

    /// 封禁用户进入特定房间(id)命令
    pub fn ban_user_from_room_by_id(&self, args: &[String]) -> Result<String> {
        if args.len() != 2 {
            return Err(Error::Command("用法: /banroomid <用户ID> <房间ID>".to_string()));
        }

        let user_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的用户ID".to_string()))?;
        let room_id = args[1].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        self.host_api.ban_user_from_room_by_id(user_id, room_id)?;
        info!("用户 {} 已被封禁进入房间 {}", user_id, room_id);
        Ok(format!("用户 {} 已被封禁进入房间 {}", user_id, room_id))
    }

    /// 解封用户进入特定房间(id)命令
    pub fn unban_user_from_room_by_id(&self, args: &[String]) -> Result<String> {
        if args.len() != 2 {
            return Err(Error::Command("用法: /unbanroomid <用户ID> <房间ID>".to_string()));
        }

        let user_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的用户ID".to_string()))?;
        let room_id = args[1].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        self.host_api.unban_user_from_room_by_id(user_id, room_id)?;
        info!("用户 {} 已解封进入房间 {}", user_id, room_id);
        Ok(format!("用户 {} 已解封进入房间 {}", user_id, room_id))
    }

    /// 封禁用户进入特定房间(ip)命令
    pub fn ban_user_from_room_by_ip(&self, args: &[String]) -> Result<String> {
        if args.len() != 2 {
            return Err(Error::Command("用法: /banroomip <IP地址> <房间ID>".to_string()));
        }

        let ip = &args[0];
        let room_id = args[1].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        if !is_valid_ip(ip) {
            return Err(Error::Command("无效的IP地址".to_string()));
        }

        self.host_api.ban_user_from_room_by_ip(ip, room_id)?;
        info!("IP {} 已被封禁进入房间 {}", ip, room_id);
        Ok(format!("IP {} 已被封禁进入房间 {}", ip, room_id))
    }

    /// 解封用户进入特定房间(ip)命令
    pub fn unban_user_from_room_by_ip(&self, args: &[String]) -> Result<String> {
        if args.len() != 2 {
            return Err(Error::Command("用法: /unbanroomip <IP地址> <房间ID>".to_string()));
        }

        let ip = &args[0];
        let room_id = args[1].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        if !is_valid_ip(ip) {
            return Err(Error::Command("无效的IP地址".to_string()));
        }

        self.host_api.unban_user_from_room_by_ip(ip, room_id)?;
        info!("IP {} 已解封进入房间 {}", ip, room_id);
        Ok(format!("IP {} 已解封进入房间 {}", ip, room_id))
    }

    /// 查询用户是否被特定房间封禁命令
    pub fn is_user_banned_from_room(&self, args: &[String]) -> Result<String> {
        if args.len() != 2 {
            return Err(Error::Command("用法: /checkroomban <用户ID> <房间ID>".to_string()));
        }

        let user_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的用户ID".to_string()))?;
        let room_id = args[1].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        let banned = self.host_api.is_user_banned_from_room(user_id, room_id)?;
        if banned {
            Ok(format!("用户 {} 在房间 {} 中被封禁", user_id, room_id))
        } else {
            Ok(format!("用户 {} 在房间 {} 中未被封禁", user_id, room_id))
        }
    }

    /// 创建房间命令
    pub fn create_room(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /createroom <最大人数>".to_string()));
        }

        let max_users = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的最大人数".to_string()))?;

        if max_users < 1 || max_users > 100 {
            return Err(Error::Command("最大人数必须在1-100之间".to_string()));
        }

        let room_id = self.host_api.create_room(max_users)?;
        info!("创建房间 {}，最大人数: {}", room_id, max_users);
        Ok(format!("创建房间 {}，最大人数: {}", room_id, max_users))
    }

    /// 解散房间命令
    pub fn disband_room(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /disbandroom <房间ID>".to_string()));
        }

        let room_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        self.host_api.disband_room(room_id)?;
        info!("解散房间 {}", room_id);
        Ok(format!("房间 {} 已解散", room_id))
    }

    /// 将用户加入至房间命令
    pub fn add_user_to_room(&self, args: &[String]) -> Result<String> {
        if args.len() != 2 {
            return Err(Error::Command("用法: /joinroom <用户ID> <房间ID>".to_string()));
        }

        let user_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的用户ID".to_string()))?;
        let room_id = args[1].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        self.host_api.add_user_to_room(user_id, room_id)?;
        info!("用户 {} 加入房间 {}", user_id, room_id);
        Ok(format!("用户 {} 已加入房间 {}", user_id, room_id))
    }

    /// 将用户踢出房间命令
    pub fn kick_user_from_room(&self, args: &[String]) -> Result<String> {
        if args.len() != 2 {
            return Err(Error::Command("用法: /kickroom <用户ID> <房间ID>".to_string()));
        }

        let user_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的用户ID".to_string()))?;
        let room_id = args[1].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        self.host_api.kick_user_from_room(user_id, room_id)?;
        info!("用户 {} 被踢出房间 {}", user_id, room_id);
        Ok(format!("用户 {} 已被踢出房间 {}", user_id, room_id))
    }

    /// 获取房间完整信息命令
    pub fn get_room_info(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /roominfo <房间ID>".to_string()));
        }

        let room_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        let info = self.host_api.get_room_info(room_id)?;
        Ok(serde_json::to_string_pretty(&info)
            .map_err(|e| Error::Command(format!("序列化失败: {}", e)))?)
    }

    /// 获取房间用户数命令
    pub fn get_room_user_count(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /roomusers <房间ID>".to_string()));
        }

        let room_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        let count = self.host_api.get_room_user_count(room_id)?;
        Ok(format!("房间 {} 的用户数: {}", room_id, count))
    }

    /// 获取房间内用户ID列表命令
    pub fn get_room_user_ids(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /roomuserids <房间ID>".to_string()));
        }

        let room_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        let user_ids = self.host_api.get_room_user_ids(room_id)?;
        Ok(serde_json::to_string_pretty(&user_ids)
            .map_err(|e| Error::Command(format!("序列化失败: {}", e)))?)
    }

    /// 获取房间房主ID命令
    pub fn get_room_host_id(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /roomhost <房间ID>".to_string()));
        }

        let room_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        let host_id = self.host_api.get_room_host_id(room_id)?;
        Ok(format!("房间 {} 的房主ID: {}", room_id, host_id))
    }

    /// 设置房间最大人数命令
    pub fn set_room_max_users(&self, args: &[String]) -> Result<String> {
        if args.len() != 2 {
            return Err(Error::Command("用法: /setmaxusers <房间ID> <数量>".to_string()));
        }

        let room_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;
        let max_users = args[1].parse::<u32>()
            .map_err(|_| Error::Command("无效的最大人数".to_string()))?;

        if max_users < 1 || max_users > 100 {
            return Err(Error::Command("最大人数必须在1-100之间".to_string()));
        }

        self.host_api.set_room_max_users(room_id, max_users)?;
        info!("设置房间 {} 最大人数为 {}", room_id, max_users);
        Ok(format!("房间 {} 最大人数设置为 {}", room_id, max_users))
    }

    /// 开始房间内准备游戏命令
    pub fn start_room_preparation(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /startprep <房间ID>".to_string()));
        }

        let room_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        self.host_api.start_room_preparation(room_id)?;
        info!("开始房间 {} 的准备游戏", room_id);
        Ok(format!("房间 {} 开始准备游戏", room_id))
    }

    /// 结束房间内准备游戏命令
    pub fn end_room_preparation(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /endprep <房间ID>".to_string()));
        }

        let room_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        self.host_api.end_room_preparation(room_id)?;
        info!("结束房间 {} 的准备游戏", room_id);
        Ok(format!("房间 {} 结束准备游戏", room_id))
    }

    /// 强制开始房间内游戏命令
    pub fn force_start_room_game(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /forcestart <房间ID>".to_string()));
        }

        let room_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        self.host_api.force_start_room_game(room_id)?;
        info!("强制开始房间 {} 的游戏", room_id);
        Ok(format!("房间 {} 强制开始游戏", room_id))
    }

    /// 设定房间锁定锁定状态（是或否）命令
    pub fn set_room_lock(&self, args: &[String]) -> Result<String> {
        if args.len() != 2 {
            return Err(Error::Command("用法: /setlock <房间ID> <是/否>".to_string()));
        }

        let room_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;
        let locked_str = &args[1].to_lowercase();

        let locked = match locked_str.as_str() {
            "是" | "true" | "1" | "yes" => true,
            "否" | "false" | "0" | "no" => false,
            _ => return Err(Error::Command("锁定状态必须是'是'或'否'".to_string())),
        };

        self.host_api.set_room_lock(room_id, locked)?;
        info!("设置房间 {} 锁定状态为 {}", room_id, if locked { "锁定" } else { "未锁定" });
        Ok(format!("房间 {} 锁定状态设置为 {}", room_id, if locked { "锁定" } else { "未锁定" }))
    }

    /// 切换房间为普通模式命令
    pub fn switch_room_to_normal_mode(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /normalmode <房间ID>".to_string()));
        }

        let room_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        self.host_api.switch_room_to_normal_mode(room_id)?;
        info!("切换房间 {} 为普通模式", room_id);
        Ok(format!("房间 {} 切换为普通模式", room_id))
    }

    /// 切换房间为循环模式命令
    pub fn switch_room_to_cycle_mode(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /cyclemode <房间ID>".to_string()));
        }

        let room_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;

        self.host_api.switch_room_to_cycle_mode(room_id)?;
        info!("切换房间 {} 为循环模式", room_id);
        Ok(format!("房间 {} 切换为循环模式", room_id))
    }

    /// 选择房间谱面ID 命令
    pub fn select_room_chart(&self, args: &[String]) -> Result<String> {
        if args.len() != 2 {
            return Err(Error::Command("用法: /selectchart <房间ID> <谱面ID>".to_string()));
        }

        let room_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;
        let chart_id = args[1].parse::<u32>()
            .map_err(|_| Error::Command("无效的谱面ID".to_string()))?;

        self.host_api.select_room_chart(room_id, chart_id)?;
        info!("房间 {} 选择谱面 {}", room_id, chart_id);
        Ok(format!("房间 {} 选择谱面 {}", room_id, chart_id))
    }

    /// 向指定用户发送消息命令
    pub fn send_message_to_user(&self, args: &[String]) -> Result<String> {
        if args.len() < 2 {
            return Err(Error::Command("用法: /sendmsg <用户ID> <消息>".to_string()));
        }

        let user_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的用户ID".to_string()))?;
        let message = args[1..].join(" ");

        self.host_api.send_message_to_user(user_id, &message)?;
        info!("向用户 {} 发送消息: {}", user_id, message);
        Ok(format!("消息已发送给用户 {}", user_id))
    }

    /// 向所有用户广播消息命令
    pub fn broadcast_message_to_all(&self, args: &[String]) -> Result<String> {
        if args.is_empty() {
            return Err(Error::Command("用法: /broadcastall <消息>".to_string()));
        }

        let message = args.join(" ");
        self.host_api.broadcast_message_to_all(&message)?;
        info!("向所有用户广播消息: {}", message);
        Ok("消息已广播给所有用户".to_string())
    }

    /// 向指定房间广播消息命令
    pub fn broadcast_message_to_room(&self, args: &[String]) -> Result<String> {
        if args.len() < 2 {
            return Err(Error::Command("用法: /broadcastroom <房间ID> <消息>".to_string()));
        }

        let room_id = args[0].parse::<u32>()
            .map_err(|_| Error::Command("无效的房间ID".to_string()))?;
        let message = args[1..].join(" ");

        self.host_api.broadcast_message_to_room(room_id, &message)?;
        info!("向房间 {} 广播消息: {}", room_id, message);
        Ok(format!("消息已广播给房间 {}", room_id))
    }

    /// 向所有房间广播消息命令
    pub fn broadcast_message_to_all_rooms(&self, args: &[String]) -> Result<String> {
        if args.is_empty() {
            return Err(Error::Command("用法: /broadcastrooms <消息>".to_string()));
        }

        let message = args.join(" ");
        self.host_api.broadcast_message_to_all_rooms(&message)?;
        info!("向所有房间广播消息: {}", message);
        Ok("消息已广播给所有房间".to_string())
    }

    /// 关闭服务器命令
    pub fn shutdown_server(&self, _args: &[String]) -> Result<String> {
        self.host_api.shutdown_server()?;
        info!("服务器关闭请求已发送");
        Ok("服务器将在5秒后关闭".to_string())
    }

    /// 重启服务器命令
    pub fn restart_server(&self, _args: &[String]) -> Result<String> {
        self.host_api.restart_server()?;
        info!("服务器重启请求已发送");
        Ok("服务器将在5秒后重启".to_string())
    }

    /// 重载所有插件命令
    pub fn reload_all_plugins(&self, _args: &[String]) -> Result<String> {
        self.host_api.reload_all_plugins()?;
        info!("重载所有插件请求已发送");
        Ok("所有插件正在重载".to_string())
    }

    /// 重载指定插件命令
    pub fn reload_plugin(&self, args: &[String]) -> Result<String> {
        if args.len() != 1 {
            return Err(Error::Command("用法: /reload <插件名>".to_string()));
        }

        let plugin_name = &args[0];
        self.host_api.reload_plugin(plugin_name)?;
        info!("重载插件请求已发送: {}", plugin_name);
        Ok(format!("插件 {} 正在重载", plugin_name))
    }

    /// 获取插件列表命令
    pub fn get_plugin_list(&self, _args: &[String]) -> Result<String> {
        let plugins = self.host_api.get_plugin_list()?;
        Ok(serde_json::to_string_pretty(&plugins)
            .map_err(|e| Error::Command(format!("序列化失败: {}", e)))?)
    }

    /// 获取用户游玩时间总排行榜命令
    pub fn get_playtime_total_leaderboard(&self, _args: &[String]) -> Result<String> {
        let leaderboard = self.host_api.get_playtime_total_leaderboard()?;
        Ok(serde_json::to_string_pretty(&leaderboard)
            .map_err(|e| Error::Command(format!("序列化失败: {}", e)))?)
    }

    /// 获取在线用户数命令
    pub fn get_online_user_count(&self, _args: &[String]) -> Result<String> {
        let count = self.host_api.get_online_user_count()?;
        Ok(format!("在线用户数: {}", count))
    }

    /// 获取可加入房间数命令
    pub fn get_available_room_count(&self, _args: &[String]) -> Result<String> {
        let count = self.host_api.get_available_room_count()?;
        Ok(format!("可加入房间数: {}", count))
    }

    /// 获取房间列表命令
    pub fn get_room_list(&self, _args: &[String]) -> Result<String> {
        let rooms = self.host_api.get_room_list()?;
        Ok(serde_json::to_string_pretty(&rooms)
            .map_err(|e| Error::Command(format!("序列化失败: {}", e)))?)
    }

    /// 获取可加入房间列表命令
    pub fn get_available_room_list(&self, _args: &[String]) -> Result<String> {
        let rooms = self.host_api.get_available_room_list()?;
        Ok(serde_json::to_string_pretty(&rooms)
            .map_err(|e| Error::Command(format!("序列化失败: {}", e)))?)
    }

    /// 获取在线用户ID列表命令
    pub fn get_online_user_ids(&self, _args: &[String]) -> Result<String> {
        let user_ids = self.host_api.get_online_user_ids()?;
        Ok(serde_json::to_string_pretty(&user_ids)
            .map_err(|e| Error::Command(format!("序列化失败: {}", e)))?)
    }

    /// 执行命令的通用入口点
    pub fn execute(&self, command: &str, args: &[String]) -> Result<String> {
        match command {
            "help" | "帮助" => self.help(args),
            "kick" | "踢出" => self.kick_user(args),
            "banid" | "封禁id" => self.ban_user_by_id(args),
            "unbanid" | "解封id" => self.unban_user_by_id(args),
            "banip" | "封禁ip" => self.ban_user_by_ip(args),
            "unbanip" | "解封ip" => self.unban_user_by_ip(args),
            "userinfo" | "用户信息" => self.get_user_info(args),
            "username" | "用户名" => self.get_username(args),
            "userlang" | "用户语言" => self.get_user_language(args),
            "playtime" | "游玩时间" => self.get_user_playtime(args),
            "playtop" | "游玩排行" => self.get_playtime_leaderboard(args),
            "bannedids" | "封禁列表id" => self.get_banned_users_by_id(args),
            "bannedips" | "封禁列表ip" => self.get_banned_users_by_ip(args),
            "checkbanid" | "检查封禁id" => self.is_user_banned_by_id(args),
            "checkbanip" | "检查封禁ip" => self.is_user_banned_by_ip(args),
            "banroomid" | "房间封禁id" => self.ban_user_from_room_by_id(args),
            "unbanroomid" | "房间解封id" => self.unban_user_from_room_by_id(args),
            "banroomip" | "房间封禁ip" => self.ban_user_from_room_by_ip(args),
            "unbanroomip" | "房间解封ip" => self.unban_user_from_room_by_ip(args),
            "checkroomban" | "检查房间封禁" => self.is_user_banned_from_room(args),
            "createroom" | "创建房间" => self.create_room(args),
            "disbandroom" | "解散房间" => self.disband_room(args),
            "joinroom" | "加入房间" => self.add_user_to_room(args),
            "kickroom" | "踢出房间" => self.kick_user_from_room(args),
            "roominfo" | "房间信息" => self.get_room_info(args),
            "roomusers" | "房间用户" => self.get_room_user_count(args),
            "roomuserids" | "房间用户id" => self.get_room_user_ids(args),
            "roomhost" | "房间房主" => self.get_room_host_id(args),
            "setmaxusers" | "设置最大用户" => self.set_room_max_users(args),
            "startprep" | "开始准备" => self.start_room_preparation(args),
            "endprep" | "结束准备" => self.end_room_preparation(args),
            "forcestart" | "强制开始" => self.force_start_room_game(args),
            "setlock" | "设置锁定" => self.set_room_lock(args),
            "normalmode" | "普通模式" => self.switch_room_to_normal_mode(args),
            "cyclemode" | "循环模式" => self.switch_room_to_cycle_mode(args),
            "selectchart" | "选择谱面" => self.select_room_chart(args),
            "sendmsg" | "发送消息" => self.send_message_to_user(args),
            "broadcastall" | "广播所有" => self.broadcast_message_to_all(args),
            "broadcastroom" | "广播房间" => self.broadcast_message_to_room(args),
            "broadcastrooms" | "广播所有房间" => self.broadcast_message_to_all_rooms(args),
            "shutdown" | "关闭" => self.shutdown_server(args),
            "restart" | "重启" => self.restart_server(args),
            "reloadall" | "重载所有" => self.reload_all_plugins(args),
            "reload" | "重载" => self.reload_plugin(args),
            "plugins" | "插件列表" => self.get_plugin_list(args),
            "playtotal" | "总游玩排行" => self.get_playtime_total_leaderboard(args),
            "onlinecount" | "在线数量" => self.get_online_user_count(args),
            "availablerooms" | "可用房间" => self.get_available_room_count(args),
            "rooms" | "房间列表" => self.get_room_list(args),
            "availableroomlist" | "可用房间列表" => self.get_available_room_list(args),
            "onlineusers" | "在线用户" => self.get_online_user_ids(args),
            _ => Err(Error::Command(format!("未知命令: {}", command))),
        }
    }
}

/// 简单的IP地址验证
fn is_valid_ip(ip: &str) -> bool {
    // 简单的IPv4验证
    if ip.split('.').count() == 4 {
        return ip.split('.').all(|part| {
            part.parse::<u8>().is_ok()
        });
    }
    
    // 简单的IPv6验证
    if ip.contains(':') {
        return ip.split(':').all(|part| {
            part.is_empty() || u16::from_str_radix(part, 16).is_ok()
        });
    }
    
    false
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        plugin_manager::PluginManager,
        event_system::EventBus,
        command_system::CommandRegistry,
    };
    use std::sync::Arc;

    #[test]
    fn test_is_valid_ip() {
        assert!(is_valid_ip("192.168.1.1"));
        assert!(is_valid_ip("127.0.0.1"));
        assert!(is_valid_ip("255.255.255.255"));
        assert!(!is_valid_ip("256.0.0.1"));
        assert!(!is_valid_ip("192.168.1"));
        assert!(!is_valid_ip("192.168.1.1.1"));
    }

    #[test]
    fn test_server_commands_creation() {
        let event_bus = Arc::new(EventBus::new());
        let command_registry = Arc::new(CommandRegistry::new());
        
        // 创建一个临时的插件管理器
        let plugin_manager = Arc::new(PluginManager::new(
            "/tmp",
            Arc::clone(&event_bus),
            Arc::clone(&command_registry),
            Arc::new(HostApi::new(
                Arc::clone(&event_bus),
                Arc::clone(&command_registry),
                Arc::new(PluginManager::new(
                    "/tmp",
                    Arc::clone(&event_bus),
                    Arc::clone(&command_registry),
                    Arc::new(HostApi::new(
                        Arc::clone(&event_bus),
                        Arc::clone(&command_registry),
                        Arc::new(PluginManager::new(
                            "/tmp",
                            Arc::clone(&event_bus),
                            Arc::clone(&command_registry),
                            Arc::new(HostApi::new(
                                Arc::clone(&event_bus),
                                Arc::clone(&command_registry),
                                Arc::new(PluginManager::new(
                                    "/tmp",
                                    Arc::clone(&event_bus),
                                    Arc::clone(&command_registry),
                                    Arc::new(HostApi::new(
                                        Arc::clone(&event_bus),
                                        Arc::clone(&command_registry),
                                        Arc::new(()),
                                    )?),
                                )?),
                            )?),
                        )?),
                    )?),
                )?),
            )?),
        ).expect("Failed to create plugin manager"));

        let host_api = Arc::new(HostApi::new(
            event_bus,
            command_registry,
            plugin_manager,
        ));
        
        let commands = ServerCommands::new(host_api);
        assert!(commands.help(&[]).is_ok());
    }
}
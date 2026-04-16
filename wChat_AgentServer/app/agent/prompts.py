"""Prompt templates.

Design notes:
  - SYSTEM_PROMPT is intentionally stable (no per-request interpolation)
    so the LLM provider can cache it as a prefix. DeepSeek's pricing
    rewards cache hits.
  - Friend profile + relationship summary form a secondary stable prefix
    keyed on (self_uid, peer_uid). Recent messages (volatile) go last.
  - JSON-mode output keeps parsing deterministic.
"""
from __future__ import annotations

from ..schemas.chat import TextChatData


INTENT_SYSTEM_PROMPT = """你是一个中文 IM 聊天分析助手。
根据最近聊天记录,分析对方最后一条消息想表达的意图,并决定是否需要调用工具获取更多上下文。

输出严格 JSON:
{
  "intent": "对方意图(30字内)",
  "needs_tools": ["get_friend_profile" | "get_chat_history" | "get_relationship_summary", ...]
}
工具调用原则:
- 对方身份/关系模糊时 → get_friend_profile
- 明显引用了最近窗口外的话题 → get_chat_history
- 需要理解整体关系基调时 → get_relationship_summary
- 如果现有信息已足够,needs_tools 返回空数组
"""


GENERATE_SYSTEM_PROMPT = """你是一个中文 IM 智能回复助手。
基于聊天上下文 + 场景要求,为用户生成多条风格各异的候选回复,供用户挑选后发送。

必须输出严格 JSON:
{
  "strategy": "整体回应策略(50字内,先分析再给策略)",
  "candidates": [
    {"index": 0, "style": "风格短标签", "content": "回复正文", "reasoning": "为什么这么回(30字内)"},
    ...
  ]
}

硬性原则:
1. 候选之间必须有明显差异(长度/语气/切入角度)
2. 贴合中文 IM 口语,不要书面化、不要换行符堆砌
3. 不编造未发生的共同经历、不编造事实
4. 拒绝任何敏感/违法/攻击/色情内容
5. 候选数量严格等于请求数
6. content 字段是纯文本,不要带引号包裹的 JSON
"""


REFINE_SYSTEM_PROMPT = """你是一个中文 IM 回复润色助手。
用户对某条候选不满意,给出了修改指令。
请基于原候选 + 指令,生成一条改进后的候选。

输出严格 JSON:
{"style": "...", "content": "...", "reasoning": "..."}
"""


def format_messages(msgs: list[TextChatData], self_uid: int) -> str:
    if not msgs:
        return "(无)"
    lines = []
    for m in msgs:
        role = "我" if m.from_uid == self_uid else "对方"
        # non-text messages get a placeholder; we don't feed file metadata to LLM
        content = m.content if m.msg_type == 1 else f"[{_msg_type_label(m.msg_type)}]"
        lines.append(f"{role}: {content}")
    return "\n".join(lines)


def _msg_type_label(t: int) -> str:
    return {1: "文本", 2: "图片", 3: "文件", 4: "语音"}.get(int(t), "未知")


def build_generate_user_prompt(
    *,
    self_uid: int,
    recent_messages: list[TextChatData],
    extended_history: list[TextChatData] | None,
    friend_profile_block: str,
    relationship_summary: str,
    preset_block: str,
    custom_prompt: str,
    intent: str,
    num_candidates: int,
) -> str:
    ext_block = (
        format_messages(extended_history, self_uid)
        if extended_history
        else "(未拉取)"
    )
    return f"""【好友画像】
{friend_profile_block}

【关系概述】
{relationship_summary}

【更早历史(旧→新)】
{ext_block}

【最近聊天(旧→新)】
{format_messages(recent_messages, self_uid)}

【已识别的对方意图】{intent}

【场景要求】
{preset_block}

【用户自定义要求】
{custom_prompt or "(无)"}

请生成 {num_candidates} 条候选回复。
"""

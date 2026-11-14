-- pime-punctuation-zh: half-width ASCII punctuation -> Chinese full-width.
--
-- Settings:
--   enabled (bool)          master switch
--   quotes  ("chinese" | "straight")
--                           Chinese quotes alternate “ ” / ‘ ’ per session;
--                           straight keeps " and '.
--
-- The plugin commits one character per punctuation key. Multi-character
-- conversions (ellipsis, dash) and auto-paired brackets need editor cursor
-- control, which the pime command ABI does not expose; they are therefore not
-- silently approximated.

local settings = {
  enabled = true,
  quotes = "chinese",
}

local mapped = {
  [","] = "，",
  ["."] = "。",
  ["?"] = "？",
  ["!"] = "！",
  [":"] = "：",
  [";"] = "；",
  ["("] = "（",
  [")"] = "）",
  ["["] = "【",
  ["]"] = "】",
  ["{"] = "｛",
  ["}"] = "｝",
  ["\\"] = "、",
  ["~"] = "～",
  ["@"] = "＠",
  ["#"] = "＃",
  ["$"] = "￥",
  ["%"] = "％",
  ["^"] = "＾",
  ["&"] = "＆",
  ["*"] = "＊",
  ["-"] = "－",
  ["_"] = "＿",
  ["+"] = "＋",
  ["="] = "＝",
  ["|"] = "｜",
  ["<"] = "《",
  [">"] = "》",
  ["/"] = "／",
  ["`"] = "｀",
}

local function empty()
  return { state = {}, handled = false, commands = {} }
end

local function is_modified(event)
  local m = event.modifiers
  return type(m) == "number" and m ~= 0
end

local function state_of(input)
  local s = input.state
  if type(s) ~= "table" then return {} end
  return s
end

local function quote_for(raw, state)
  local open = state.quote_open == true
  state.quote_open = not open
  if raw == "\"" then return open and "“" or "”" end
  return open and "‘" or "’"
end

local function with_state(state, out)
  if state.quote_open == nil then return out end
  out.state = { quote_open = state.quote_open }
  return out
end

function pime.init(config)
  settings.enabled = true
  settings.quotes = "chinese"
  local s = config and config.settings
  if type(s) == "table" then
    if type(s.enabled) == "boolean" then settings.enabled = s.enabled end
    if s.quotes == "chinese" or s.quotes == "straight" then
      settings.quotes = s.quotes
    end
  end
end

function pime.process(input)
  local out = empty()
  if not settings.enabled then return out end

  local event = input.event or {}
  if event.type ~= "text" and event.type ~= "key" then return out end
  if is_modified(event) then return out end

  local action = pime.workspace.get("plugin/pime-keymap-default/action")
  if action == "space" or action == "enter" or action == "escape" or
     action == "backspace" or action == "tab" or action == "delete" then
    return out
  end

  local raw = event.text
  if type(raw) ~= "string" or #raw ~= 1 then return out end

  local state = state_of(input)
  local value
  if raw == "\"" or raw == "'" then
    if settings.quotes == "chinese" then
      value = quote_for(raw, state)
    else
      value = raw
    end
  else
    value = mapped[raw]
  end
  if value == nil then return out end

  out.handled = true
  out.commands[1] = { type = "commit", text = value }
  return with_state(state, out)
end

function pime.on_event(event)
  return empty()
end

function pime.shutdown()
end

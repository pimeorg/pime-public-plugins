-- pime-punctuation-zh: half-width punctuation -> Chinese full-width.
--
-- Settings: enabled (bool), quotes ("chinese" | "straight").
-- Output: a single `commit` command for the mapped punctuation.

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
  ["\\"] = "、",
  ["~"] = "～",
}

local chinese_quotes = {
  ["\""] = "”",
  ["'"] = "’",
}

local function empty()
  return { state = {}, handled = false, commands = {} }
end

local function is_modified(event)
  local m = event.modifiers
  return type(m) == "number" and m ~= 0
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
     action == "backspace" then
    return out
  end

  local raw = event.text
  if type(raw) ~= "string" then return out end

  local value = mapped[raw]
  if value == nil and settings.quotes == "chinese" then
    value = chinese_quotes[raw]
  end
  if value == nil then return out end

  out.handled = true
  out.commands[1] = { type = "commit", text = value }
  return out
end

function pime.on_event(event)
  return empty()
end

function pime.shutdown()
end

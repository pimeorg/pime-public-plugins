-- pime-user-dict: user word list on top of the plugin KV store.
--
-- Events:
--   user_dict.add    { word, count? }  -> set/increase a word
--   user_dict.remove { word }          -> delete a word
--   user_dict.list                     -> write the top words to the workspace
-- Any committed word from the formatter is added with count 1.

local settings = {
  enabled = true,
  max_words = 32,
}

local function empty()
  return { state = {}, handled = false, commands = {} }
end

local function add(word, delta)
  if type(word) ~= "string" or #word == 0 or #word > 64 then return false end
  local key = "words/" .. word
  local count = pime.store.get(key)
  if type(count) ~= "number" then count = 0 end
  pime.store.set(key, count + delta)
  pime.workspace.set("word", word)
  pime.workspace.set("count", count + delta)
  return true
end

local function list()
  local words = {}
  for key, count in pime.store.scan("words/", settings.max_words) do
    if type(key) == "string" and type(count) == "number" then
      words[#words + 1] = { word = key:sub(7), count = count }
      if #words >= settings.max_words then break end
    end
  end
  pime.workspace.set("words", words)
  return #words
end

function pime.init(config)
  settings.enabled = true
  settings.max_words = 32
  local s = config and config.settings
  if type(s) == "table" then
    if type(s.enabled) == "boolean" then settings.enabled = s.enabled end
    if type(s.max_words) == "number" and s.max_words >= 1 then
      settings.max_words = s.max_words
    end
  end
end

function pime.process(input)
  local out = empty()
  if not settings.enabled then return out end

  local event = input.event or {}
  if event.type == "user_dict.add" then
    out.handled = add(event.word, 1)
    pime.workspace.set("status", out.handled and "added" or "invalid")
    return out
  end

  if event.type == "user_dict.remove" and type(event.word) == "string" then
    pime.store.delete("words/" .. event.word)
    pime.workspace.set("status", "removed")
    out.handled = true
    return out
  end

  if event.type == "user_dict.list" then
    local count = list()
    pime.workspace.set("status", "listed")
    pime.workspace.set("total", count)
    out.handled = true
    return out
  end

  local committed = pime.workspace.get("plugin/pime-candidates-format/committed")
  if type(committed) == "string" and #committed > 0 then
    add(committed, 1)
  end
  return out
end

function pime.on_event(event)
  return empty()
end

function pime.shutdown()
end

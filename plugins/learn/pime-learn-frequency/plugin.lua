-- pime-learn-frequency: record committed words and their counts.
--
-- Reads plugin/pime-candidates-format/committed, bumps a per-word counter in
-- the plugin KV, and exposes the latest word/count through the workspace.
-- A timer flushes the KV store in periodic batches.

local settings = {
  enabled = true,
  flush_interval_ms = 30000,
}

local timer_id = "pime-learn-frequency/flush"
local flush_handle = nil

local function empty()
  return { state = {}, handled = false, commands = {} }
end

local function bump(word)
  local key = "freq/" .. word
  local count = pime.store.get(key)
  if type(count) ~= "number" then count = 0 end
  count = count + 1
  pime.store.set(key, count)
  return count
end

function pime.init(config)
  settings.enabled = true
  settings.flush_interval_ms = 30000
  local s = config and config.settings
  if type(s) == "table" then
    if type(s.enabled) == "boolean" then settings.enabled = s.enabled end
    if type(s.flush_interval_ms) == "number" and s.flush_interval_ms >= 1000 then
      settings.flush_interval_ms = s.flush_interval_ms
    end
  end
  if settings.enabled then
    flush_handle = pime.timer.every(settings.flush_interval_ms, timer_id)
  end
end

function pime.process(input)
  local out = empty()
  if not settings.enabled then return out end

  local word = pime.workspace.get("plugin/pime-candidates-format/committed")
  if type(word) ~= "string" or #word == 0 or #word > 64 then return out end

  local count = bump(word)
  pime.workspace.set("last", word)
  pime.workspace.set("count", count)
  return out
end

function pime.on_event(event)
  if type(event) == "table" and event.type == "timer" and
     event.id == timer_id then
    pime.store.flush()
  end
  return empty()
end

function pime.shutdown()
  if flush_handle ~= nil then pime.timer.cancel(flush_handle) end
  pime.store.flush()
end

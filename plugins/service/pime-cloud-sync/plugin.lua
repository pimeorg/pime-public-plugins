-- pime-cloud-sync: incremental sync of the learned word-frequency table.
--
-- Wire format (text/plain, one entry per line):
--   <word>\t<count>
--
-- Flow:
--   event "sync.start" -> POST the local table to `endpoint`;
--   event "net.result" -> merge the server reply into the local KV.
-- The plugin is disabled by default: no request is made without opt-in.

local settings = {
  enabled = false,
  endpoint = "https://sync.pime.dev/v1/freq",
  timeout_ms = 5000,
}

local pending = nil

local function empty()
  return { state = {}, handled = false, commands = {} }
end

local function encode_table()
  local lines = {}
  for key, value in pime.store.scan("freq/", 1000) do
    if type(key) == "string" and type(value) == "number" then
      lines[#lines + 1] = key:sub(6) .. "\t" .. tostring(value)
    end
  end
  return table.concat(lines, "\n")
end

local function merge(body)
  if type(body) ~= "string" then return 0 end
  local merged = 0
  for line in body:gmatch("[^\r\n]+") do
    local word, count = line:match("^(.-)\t(%d+)$")
    if word and #word > 0 and #word <= 64 then
      local value = tonumber(count)
      local key = "freq/" .. word
      local current = pime.store.get(key)
      if type(current) ~= "number" then current = 0 end
      if value > current then
        pime.store.set(key, value)
        merged = merged + 1
      end
    end
  end
  return merged
end

function pime.init(config)
  settings.enabled = false
  settings.endpoint = "https://sync.pime.dev/v1/freq"
  settings.timeout_ms = 5000
  local s = config and config.settings
  if type(s) == "table" then
    if type(s.enabled) == "boolean" then settings.enabled = s.enabled end
    if type(s.endpoint) == "string" and #s.endpoint > 0 then
      settings.endpoint = s.endpoint
    end
    if type(s.timeout_ms) == "number" and s.timeout_ms >= 500 then
      settings.timeout_ms = s.timeout_ms
    end
  end
end

function pime.process(input)
  local out = empty()
  local event = input.event or {}

  if event.type == "sync.start" then
    if not settings.enabled then return out end
    local body = encode_table()
    if #body == 0 then
      pime.workspace.set("status", "empty")
      return out
    end
    local id = pime.net.request({
      method = "POST",
      url = settings.endpoint,
      headers = { ["content-type"] = "text/plain" },
      body = body,
      timeout_ms = settings.timeout_ms,
    })
    if type(id) == "number" and id ~= 0 then
      pending = id
      pime.workspace.set("status", "pending")
      pime.workspace.set("request", id)
      out.handled = true
    else
      pime.workspace.set("status", "rejected")
    end
    return out
  end

  if event.type == "net.result" and pending ~= nil then
    local id = event.id
    if type(id) ~= "number" then id = pending end
    local result = pime.net.result(id)
    pending = nil
    local body = nil
    if type(result) == "table" then
      if type(result.body) == "string" then
        body = result.body
      elseif type(result.entries) == "string" then
        body = result.entries
      end
    elseif type(result) == "string" then
      body = result
    end
    local merged = merge(body)
    pime.workspace.set("status", "synced")
    pime.workspace.set("merged", merged)
    out.handled = true
    return out
  end

  return out
end

function pime.on_event(event)
  return empty()
end

function pime.shutdown()
  pending = nil
end

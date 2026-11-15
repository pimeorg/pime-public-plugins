-- pime-translate-online: online translation service.
--
-- WeaveText parity (core/weave-engine/src/translate.rs online path): a host
-- request is forwarded over HTTPS and the translated text is committed when the
-- response arrives. Only text sent by the host leaves the device; nothing from
-- the user dictionary or learning store is attached.
--
-- Events:
--   { type = "translate.request", id = tstr, text = tstr,
--     from = tstr?, to = tstr? }
--   { type = "net.data", id = number, data = tstr }     (matching request)
--   { type = "net.close", id = number }
--   { type = "net.error", id = number }
--
-- Settings:
--   endpoint   string  https POST endpoint (empty disables the plugin)
--   from       string  source language (default auto)
--   to         string  target language (default zh)
--   timeout_ms int     request timeout (default 8000)

local settings = {
  endpoint = "",
  from = "auto",
  to = "zh",
  timeout_ms = 8000,
}

local pending = {}          -- request token -> { id = net id, text = tstr }
local next_token = 1
local MAX_BODY = 64 * 1024

local function empty()
  return { state = {}, handled = false, commands = {} }
end

local function percent_encode(text)
  return (text:gsub("[^%w%-%._~]", function(c)
    return string.format("%%%02X", string.byte(c))
  end))
end

local function start_request(event)
  if settings.endpoint == "" then return end
  local text = event.text
  if type(text) ~= "string" or #text == 0 or #text > 2000 then return end
  local from = event.from
  if type(from) ~= "string" or #from == 0 then from = settings.from end
  local to = event.to
  if type(to) ~= "string" or #to == 0 then to = settings.to end
  local body = "from=" .. percent_encode(from) ..
               "&to=" .. percent_encode(to) ..
               "&text=" .. percent_encode(text)
  local net_id = pime.net.request({
    method = "POST",
    url = settings.endpoint,
    headers = { ["content-type"] = "application/x-www-form-urlencoded" },
    body = body,
    timeout_ms = settings.timeout_ms,
  })
  if type(net_id) == "number" and net_id ~= 0 then
    local token = next_token
    next_token = next_token + 1
    pending[token] = { id = net_id, text = "" }
    return token, net_id
  end
end

local function find_token(net_id)
  for token, item in pairs(pending) do
    if item.id == net_id then return token end
  end
  return nil
end

function pime.init(config)
  local s = config and config.settings
  if type(s) == "table" then
    if type(s.endpoint) == "string" then settings.endpoint = s.endpoint end
    if type(s.from) == "string" then settings.from = s.from end
    if type(s.to) == "string" then settings.to = s.to end
    if type(s.timeout_ms) == "number" and s.timeout_ms >= 1000 then
      settings.timeout_ms = s.timeout_ms
    end
  end
end

function pime.process(input)
  local out = empty()
  local event = input.event or {}
  if event.type == "translate.request" then
    start_request(event)
  end
  return out
end

function pime.on_event(event)
  local out = empty()
  if type(event) ~= "table" then return out end

  if event.type == "net.data" then
    local token = find_token(event.id)
    if token ~= nil and type(event.data) == "string" then
      pending[token].text = pending[token].text .. event.data
      if #pending[token].text > MAX_BODY then
        pending[token] = nil
      end
    end
    return out
  end

  if event.type == "net.close" then
    local token = find_token(event.id)
    if token ~= nil then
      local translated = pending[token].text
      pending[token] = nil
      if #translated > 0 then
        pime.store.set("translate/last", translated)
        out.commands[1] = { type = "commit", text = translated }
      end
    end
    return out
  end

  if event.type == "net.error" then
    local token = find_token(event.id)
    if token ~= nil then
      pending[token] = nil
      out.commands[1] = { type = "notify", code = 4 }
    end
  end
  return out
end

function pime.shutdown()
  pending = {}
end

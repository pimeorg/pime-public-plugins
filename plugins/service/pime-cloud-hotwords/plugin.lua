-- pime-cloud-hotwords: download-only, signature-verified hotword service.
--
-- WeaveText parity (core/weave-engine/src/cloud.rs):
--   * fetch a hotword payload over HTTPS on a timer;
--   * verify an Ed25519 detached signature before accepting it;
--   * store only, never upload anything (no user data leaves the device).
--
-- Wire contract (documented in README):
--   response body = "<hex ed25519 signature>\n<payload>"
--   the signature covers <payload> bytes exactly.
--
-- Config settings:
--   enabled      bool    master switch (default true)
--   url          string  hotword endpoint (https only)
--   public_key   string  PEM/openssl public key handed to pime.crypto.verify
--   interval_ms  int     refresh period (default 6h)

local settings = {
  enabled = true,
  url = "",
  public_key = "",
  interval_ms = 6 * 60 * 60 * 1000,
}

local timer_id = "pime-cloud-hotwords/refresh"
local timer_handle = nil
local pending = { id = nil, body = "", valid = false }

local MAX_BODY = 512 * 1024

local function empty()
  return { state = {}, handled = false, commands = {} }
end

local function hex_to_bin(text)
  local out = {}
  for i = 1, #text - 1, 2 do
    local byte = tonumber(text:sub(i, i + 1), 16)
    if byte == nil then return nil end
    out[#out + 1] = string.char(byte)
  end
  return table.concat(out)
end

local function split_signature(body)
  local newline = body:find("\n", 1, true)
  if newline == nil then return nil, nil end
  local signature = body:sub(1, newline - 1)
  local payload = body:sub(newline + 1)
  if #signature < 2 or #signature % 2 ~= 0 then return nil, nil end
  return hex_to_bin(signature), payload
end

local function refresh()
  if not settings.enabled or settings.url == "" then return end
  local id = pime.net.request({
    method = "GET",
    url = settings.url,
    headers = { ["accept"] = "application/octet-stream" },
    timeout_ms = 10000,
  })
  if type(id) == "number" and id ~= 0 then
    pending.id = id
    pending.body = ""
    pending.valid = false
  end
end

local function accept_payload(payload)
  local signature, signed = split_signature(payload)
  if signature == nil then return false end
  local ok = pime.crypto.verify(settings.public_key, "ed25519", signed, signature)
  if ok ~= true then return false end
  pime.store.set("hotwords/latest", signed)
  pime.store.set("hotwords/updated_ms", pime.time.wall_ms())
  pime.store.flush()
  return true
end

function pime.init(config)
  local s = config and config.settings
  if type(s) == "table" then
    if type(s.enabled) == "boolean" then settings.enabled = s.enabled end
    if type(s.url) == "string" then settings.url = s.url end
    if type(s.public_key) == "string" then settings.public_key = s.public_key end
    if type(s.interval_ms) == "number" and s.interval_ms >= 60000 then
      settings.interval_ms = s.interval_ms
    end
  end
  if settings.enabled then
    timer_handle = pime.timer.every(settings.interval_ms, timer_id)
    refresh()
  end
end

function pime.process(input)
  local out = empty()
  local event = input.event or {}
  if event.type == "cloudhotwords.refresh" then
    refresh()
    return out
  end
  return out
end

function pime.on_event(event)
  local out = empty()
  if type(event) ~= "table" then return out end

  if event.type == "timer" and event.id == timer_id then
    refresh()
    return out
  end

  if event.type == "net.data" then
    if pending.id ~= nil and event.id == pending.id then
      local chunk = event.data
      if type(chunk) == "string" then
        pending.body = pending.body .. chunk
        if #pending.body > MAX_BODY then
          pending.valid = false
          pending.body = ""
          pending.id = nil
        end
      end
    end
    return out
  end

  if event.type == "net.close" then
    if pending.id ~= nil and event.id == pending.id then
      local accepted = accept_payload(pending.body)
      if accepted then
        out.commands[1] = { type = "notify", code = 1 }
      else
        out.commands[1] = { type = "notify", code = 2 }
      end
      pending.id = nil
      pending.body = ""
    end
    return out
  end

  if event.type == "net.error" and event.id == pending.id then
    pending.id = nil
    pending.body = ""
    out.commands[1] = { type = "notify", code = 3 }
  end
  return out
end

function pime.shutdown()
  if timer_handle ~= nil then pime.timer.cancel(timer_handle) end
  pime.store.flush()
end

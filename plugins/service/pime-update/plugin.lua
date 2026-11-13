-- pime-update: check a text feed for a newer plugin/core release.
--
-- Feed format (text/plain, key=value per line):
--   version=1.2.0
--   url=https://updates.pime.dev/pime-plugin-1.2.0.pimepkg
--   sha256=<hex>
--
-- Events:
--   update.check { current_version } -> GET feed_url
--   net.result                       -> compare and publish availability
--
-- Package authenticity is verified by core's package loader against
-- trust/roots.pem; this plugin must not install anything itself.

local settings = {
  enabled = true,
  feed_url = "https://updates.pime.dev/plugin-feed.txt",
  timeout_ms = 5000,
}

local pending = nil
local current_version = nil

local function empty()
  return { state = {}, handled = false, commands = {} }
end

local function parse_version(text)
  if type(text) ~= "string" then return nil end
  local a, b, c = text:match("^(%d+)%.(%d+)%.(%d+)")
  if not a then return nil end
  return { tonumber(a), tonumber(b), tonumber(c) }
end

local function newer(candidate, base)
  local a = parse_version(candidate)
  local b = parse_version(base)
  if not a or not b then return false end
  for i = 1, 3 do
    if a[i] ~= b[i] then return a[i] > b[i] end
  end
  return false
end

local function parse_feed(body)
  if type(body) ~= "string" then return nil end
  local feed = {}
  for line in body:gmatch("[^\r\n]+") do
    local key, value = line:match("^([%w_]+)=(.*)$")
    if key then feed[key] = value end
  end
  if not feed.version or not feed.url then return nil end
  return feed
end

function pime.init(config)
  settings.enabled = true
  settings.feed_url = "https://updates.pime.dev/plugin-feed.txt"
  settings.timeout_ms = 5000
  local s = config and config.settings
  if type(s) == "table" then
    if type(s.enabled) == "boolean" then settings.enabled = s.enabled end
    if type(s.feed_url) == "string" and #s.feed_url > 0 then
      settings.feed_url = s.feed_url
    end
    if type(s.timeout_ms) == "number" and s.timeout_ms >= 500 then
      settings.timeout_ms = s.timeout_ms
    end
  end
end

function pime.process(input)
  local out = empty()
  local event = input.event or {}

  if event.type == "update.check" then
    if not settings.enabled then return out end
    current_version = event.current_version
    if parse_version(current_version) == nil then
      pime.workspace.set("status", "need-version")
      return out
    end
    local id = pime.net.request({
      method = "GET",
      url = settings.feed_url,
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
    if type(result) == "table" and type(result.body) == "string" then
      body = result.body
    elseif type(result) == "string" then
      body = result
    end

    local feed = parse_feed(body)
    if not feed then
      pime.workspace.set("status", "bad-feed")
      out.handled = true
      return out
    end

    pime.workspace.set("latest", feed.version)
    pime.workspace.set("url", feed.url)
    if newer(feed.version, current_version) then
      pime.workspace.set("status", "available")
    else
      pime.workspace.set("status", "up-to-date")
    end
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
  current_version = nil
end

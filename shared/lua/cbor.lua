-- Small deterministic CBOR codec for Lua plugins.
--
-- Lua plugins exchange tables with the core directly; this module exists for
-- persisting structured values (store.kv values are strings) and for reading
-- byte payloads received over net.*. Encoded maps sort keys canonically, so a
-- re-encoded value is byte-stable.

local M = {}

local function head(major, value)
  local prefix = major * 32
  if value < 24 then
    return string.char(prefix + value)
  elseif value <= 0xFF then
    return string.char(prefix + 24, value)
  elseif value <= 0xFFFF then
    return string.char(prefix + 25) .. string.pack(">I2", value)
  elseif value <= 0xFFFFFFFF then
    return string.char(prefix + 26) .. string.pack(">I4", value)
  end
  return string.char(prefix + 27) .. string.pack(">I8", value)
end

local function is_array(value)
  local count = 0
  for key in pairs(value) do
    if type(key) ~= "number" then return false, 0 end
    count = count + 1
  end
  return count > 0 and count == #value, count
end

local encode

encode = function(value)
  local kind = type(value)
  if value == nil then return "\246" end
  if kind == "boolean" then return value and "\245" or "\244" end
  if kind == "number" then
    if value >= 0 then return head(0, value) end
    return head(1, -1 - value)
  end
  if kind == "string" then
    return head(3, #value) .. value
  end
  if kind == "table" then
    local array, count = is_array(value)
    if array then
      local out = { head(4, count) }
      for i = 1, count do out[#out + 1] = encode(value[i]) end
      return table.concat(out)
    end
    local keys = {}
    for key in pairs(value) do
      if type(key) ~= "string" then
        error("cbor: table keys must be strings or a pure array")
      end
      keys[#keys + 1] = key
    end
    table.sort(keys, function(a, b)
      if #a ~= #b then return #a < #b end
      return a < b
    end)
    local out = { head(5, #keys) }
    for _, key in ipairs(keys) do
      out[#out + 1] = encode(key)
      out[#out + 1] = encode(value[key])
    end
    return table.concat(out)
  end
  error("cbor: unsupported type " .. kind)
end

function M.encode(value)
  return encode(value)
end

local function read_head(s, i)
  local b = string.byte(s, i)
  if not b then error("cbor: truncated") end
  local major, info = math.floor(b / 32), b % 32
  i = i + 1
  if info < 24 then return major, info, i end
  local size = ({ [24] = 1, [25] = 2, [26] = 4, [27] = 8 })[info]
  if not size or i + size - 1 > #s then error("cbor: bad head") end
  local value = 0
  for k = 0, size - 1 do
    value = value * 256 + string.byte(s, i + k)
  end
  return major, value, i + size
end

local decode

decode = function(s, i)
  local major, arg, pos = read_head(s, i)
  if major == 0 then return arg, pos end
  if major == 1 then return -1 - arg, pos end
  if major == 2 or major == 3 then
    if pos + arg - 1 > #s then error("cbor: truncated payload") end
    return string.sub(s, pos, pos + arg - 1), pos + arg
  end
  if major == 4 then
    local out = {}
    for k = 1, arg do
      local item
      item, pos = decode(s, pos)
      out[k] = item
    end
    return out, pos
  end
  if major == 5 then
    local out = {}
    for _ = 1, arg do
      local key, value
      key, pos = decode(s, pos)
      value, pos = decode(s, pos)
      out[key] = value
    end
    return out, pos
  end
  if major == 6 then return decode(s, pos) end
  if major == 7 then
    if arg == 20 then return false, pos end
    if arg == 21 then return true, pos end
    if arg == 22 then return nil, pos end
    return arg, pos
  end
  error("cbor: unsupported major type " .. major)
end

function M.decode(s)
  local value = decode(s, 1)
  return value
end

return M
